/**
 * @file config.c
 * @brief Implementation of configuration loader for NTRIP RTCM 3.x Stream Analyzer
 *
 * Project: NTRIP RTCM 3.x Stream Analyzer
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause (see LICENSE for details)
 */

#include "core/config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"

/**
 * @brief Copy a JSON string field into a fixed buffer, bounded.
 *
 * A missing key, or one holding something other than a string, yields an
 * empty result rather than a crash.
 *
 * @param json Object to read from.
 * @param key  Field name.
 * @param dst  Destination buffer, always NUL-terminated on return.
 * @param cap  Capacity of @p dst.
 */
static void cfg_copy_string(const cJSON *json, const char *key,
                            char *dst, size_t cap)
{
    if (!dst || cap == 0) return;
    dst[0] = '\0';

    const cJSON *item = cJSON_GetObjectItem(json, key);
    if (!item || !cJSON_IsString(item) || !item->valuestring) return;

    strncpy(dst, item->valuestring, cap - 1);
    dst[cap - 1] = '\0';
}

/* Read a number, or leave the default in place. */
static int cfg_int(const cJSON *json, const char *key, int fallback)
{
    const cJSON *item = cJSON_GetObjectItem(json, key);
    return (item && cJSON_IsNumber(item)) ? item->valueint : fallback;
}

static double cfg_double(const cJSON *json, const char *key, double fallback)
{
    const cJSON *item = cJSON_GetObjectItem(json, key);
    return (item && cJSON_IsNumber(item)) ? item->valuedouble : fallback;
}

/* Read a boolean, or leave the default in place.  Absent means off for
 * the TLS flags -- a file written before the flag existed described a
 * plain-text connection, and must keep meaning what it meant. */
static bool cfg_bool(const cJSON *json, const char *key, bool fallback)
{
    const cJSON *item = cJSON_GetObjectItem(json, key);
    return (item && cJSON_IsBool(item)) ? cJSON_IsTrue(item) : fallback;
}

/**
 * @brief Read one entry of the `mountpoints` array.
 *
 * The exchange format the whole project uses: the same file the
 * monitoring daemon reads, so a set of connections written anywhere is
 * readable everywhere.  Keys are the daemon's, lower case.
 *
 * The ephemeris block stays optional exactly as it was in the older
 * single-connection format -- absent means the sky plot has no stream to
 * borrow, not that the file is wrong.
 */
static void cfg_load_entry(const cJSON *e, NTRIP_Config *config)
{
    cfg_copy_string(e, "caster", config->NTRIP_CASTER,
                    sizeof(config->NTRIP_CASTER));
    cfg_copy_string(e, "mountpoint", config->MOUNTPOINT,
                    sizeof(config->MOUNTPOINT));
    cfg_copy_string(e, "username", config->USERNAME,
                    sizeof(config->USERNAME));
    cfg_copy_string(e, "password", config->PASSWORD,
                    sizeof(config->PASSWORD));

    config->NTRIP_PORT = cfg_int(e, "port", 0);
    config->LATITUDE   = cfg_double(e, "latitude", 0.0);
    config->LONGITUDE  = cfg_double(e, "longitude", 0.0);

    cfg_copy_string(e, "eph_caster", config->EPH_CASTER,
                    sizeof(config->EPH_CASTER));
    cfg_copy_string(e, "eph_mountpoint", config->EPH_MOUNTPOINT,
                    sizeof(config->EPH_MOUNTPOINT));
    cfg_copy_string(e, "eph_username", config->EPH_USERNAME,
                    sizeof(config->EPH_USERNAME));
    cfg_copy_string(e, "eph_password", config->EPH_PASSWORD,
                    sizeof(config->EPH_PASSWORD));
    config->EPH_PORT = cfg_int(e, "eph_port", 0);

    config->TLS     = cfg_bool(e, "tls", false);
    config->EPH_TLS = cfg_bool(e, "eph_tls", false);
}

/**
 * @brief Read the older single-connection layout: `NTRIP_CASTER` and
 *        friends, upper case, at the top level.
 *
 * Kept because configurations written by every release before the shared
 * format exist on people's disks, in support e-mails and in the release
 * assets.  Reading one is not deprecated behaviour to be warned about --
 * it is a file that still says exactly what it meant.
 */
static void cfg_load_legacy(const cJSON *json, NTRIP_Config *config)
{
    /* Extract configuration values.
     *
     * A missing key leaves the field empty rather than crashing: these
     * were `strcpy(dst, cJSON_GetObjectItem(...)->valuestring)`, which
     * dereferenced NULL when a key was absent and overran the fixed
     * buffer when a value was longer than it.  A hand-edited config with
     * one key misspelt was enough to take the program down.
     *
     * Empty required fields are already reported downstream -- by
     * `--check-config`, and by the connection attempt itself -- so this
     * matches how the optional EPH_* fields below have always behaved. */
    cfg_copy_string(json, "NTRIP_CASTER", config->NTRIP_CASTER,
                    sizeof(config->NTRIP_CASTER));
    cfg_copy_string(json, "MOUNTPOINT", config->MOUNTPOINT,
                    sizeof(config->MOUNTPOINT));
    cfg_copy_string(json, "USERNAME", config->USERNAME,
                    sizeof(config->USERNAME));
    cfg_copy_string(json, "PASSWORD", config->PASSWORD,
                    sizeof(config->PASSWORD));

    cJSON *port = cJSON_GetObjectItem(json, "NTRIP_PORT");
    config->NTRIP_PORT = (port && cJSON_IsNumber(port)) ? port->valueint : 0;

    // New: Extract latitude and longitude if present
    cJSON *lat = cJSON_GetObjectItem(json, "LATITUDE");
    cJSON *lon = cJSON_GetObjectItem(json, "LONGITUDE");
    config->LATITUDE = (lat && cJSON_IsNumber(lat)) ? lat->valuedouble : 0.0;
    config->LONGITUDE = (lon && cJSON_IsNumber(lon)) ? lon->valuedouble : 0.0;

    /* ── Optional secondary ephemeris stream ──────────────────────────
     * Missing fields stay empty so the eph worker stays disabled by
     * default; the user enables it by entering values manually or by
     * loading a config that includes EPH_* keys.  The template-config
     * writer (OnGenerateConfig / initialize_config) still emits BKG
     * defaults so first-time users have a starting point to edit. */
    cJSON *eph_caster = cJSON_GetObjectItem(json, "EPH_CASTER");
    cJSON *eph_port   = cJSON_GetObjectItem(json, "EPH_PORT");
    cJSON *eph_mp     = cJSON_GetObjectItem(json, "EPH_MOUNTPOINT");
    cJSON *eph_user   = cJSON_GetObjectItem(json, "EPH_USERNAME");
    cJSON *eph_pwd    = cJSON_GetObjectItem(json, "EPH_PASSWORD");

    strncpy(config->EPH_CASTER,
            (eph_caster && cJSON_IsString(eph_caster))
                ? eph_caster->valuestring : "",
            sizeof(config->EPH_CASTER) - 1);
    config->EPH_CASTER[sizeof(config->EPH_CASTER) - 1] = '\0';

    config->EPH_PORT = (eph_port && cJSON_IsNumber(eph_port))
                       ? eph_port->valueint : 0;

    strncpy(config->EPH_MOUNTPOINT,
            (eph_mp && cJSON_IsString(eph_mp))
                ? eph_mp->valuestring : "",
            sizeof(config->EPH_MOUNTPOINT) - 1);
    config->EPH_MOUNTPOINT[sizeof(config->EPH_MOUNTPOINT) - 1] = '\0';

    strncpy(config->EPH_USERNAME,
            (eph_user && cJSON_IsString(eph_user)) ? eph_user->valuestring : "",
            sizeof(config->EPH_USERNAME) - 1);
    config->EPH_USERNAME[sizeof(config->EPH_USERNAME) - 1] = '\0';

    strncpy(config->EPH_PASSWORD,
            (eph_pwd && cJSON_IsString(eph_pwd)) ? eph_pwd->valuestring : "",
            sizeof(config->EPH_PASSWORD) - 1);
    config->EPH_PASSWORD[sizeof(config->EPH_PASSWORD) - 1] = '\0';

    config->TLS     = cfg_bool(json, "TLS", false);
    config->EPH_TLS = cfg_bool(json, "EPH_TLS", false);
}

int load_config(const char *filename, NTRIP_Config *config)
{
    FILE *file = fopen(filename, "r");
    if (!file) {
        perror("Failed to open config file");
        return -1;
    }

    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    fseek(file, 0, SEEK_SET);

    if (length < 0) {
        perror("Failed to size config file");
        fclose(file);
        return -1;
    }

    char *data = (char *)malloc((size_t)length + 1);
    if (!data) {
        perror("Failed to allocate memory for config file");
        fclose(file);
        return -1;
    }

    /* Terminate at what was actually read, not at the size on disk.
     *
     * These differ routinely: the file is opened in text mode, so on
     * Windows every CRLF becomes one LF and fread returns fewer bytes
     * than ftell reported.  Writing the NUL at `length` therefore left
     * uninitialised heap between the real end and the terminator, which
     * cJSON then parsed.  A truncated read did the same. */
    size_t got = fread(data, 1, (size_t)length, file);
    if (ferror(file)) {
        perror("Failed to read config file");
        free(data);
        fclose(file);
        return -1;
    }
    data[got] = '\0';
    fclose(file);

    cJSON *json = cJSON_Parse(data);
    free(data);

    if (!json) {
        fprintf(stderr, "Failed to parse JSON: %s\n", cJSON_GetErrorPtr());
        return -1;
    }

    /* One exchange format for the whole project: the `mountpoints` array
     * the monitoring daemon reads.  A file written anywhere is readable
     * everywhere, and a set of connections survives the trip between a
     * phone, a desktop and a server.
     *
     * These programs analyse one stream at a time, so they take the first
     * entry -- and say so when there are more, because a user who
     * exported five connections and sees one is otherwise entitled to
     * think the file was truncated. */
    const cJSON *arr = cJSON_GetObjectItem(json, "mountpoints");
    if (cJSON_IsArray(arr) && cJSON_GetArraySize(arr) > 0) {
        cfg_load_entry(cJSON_GetArrayItem(arr, 0), config);

        int n = cJSON_GetArraySize(arr);
        if (n > 1) {
            fprintf(stderr,
                    "[CONFIG] %s lists %d connections; using the first (%s) "
                    "and ignoring the other %d.\n",
                    filename, n,
                    config->MOUNTPOINT[0] ? config->MOUNTPOINT : "unnamed",
                    n - 1);
        }
    } else {
        cfg_load_legacy(json, config);
    }

    config->EPH_AUTH_BASIC[0] = '\0';   /* recomputed by caller from user/pwd */

    cJSON_Delete(json);
    return 0;
}

int initialize_config(const char *filename) {
    FILE *test = fopen(filename, "r");
    if (test) {
        fclose(test);
        fprintf(stderr, "Config file '%s' already exists. Aborting to avoid overwrite.\n", filename);
        fprintf(stderr, "If you want to create a new config, please remove or rename the existing file first.\n");
        return 1;
    }

    FILE *f = fopen(filename, "w");
    if (!f) {
        fprintf(stderr, "Could not create %s\n", filename);
        return 1;
    }
    /* The shared exchange format: a list, even for one connection.  The
     * analysers take the first entry; the monitoring daemon takes them
     * all.  `output_dir` and `interval_s` mean something only to the
     * daemon and are ignored here, but writing them means one file runs
     * everywhere without editing. */
    fprintf(f,
        "{\n"
        "    \"output_dir\": \"/var/lib/ntrip-monitor\",\n"
        "    \"interval_s\": 10,\n"
        "    \"mountpoints\": [\n"
        "        {\n"
        "            \"name\": \"my station\",\n"
        "            \"caster\": \"your.caster.example.com\",\n"
        "            \"port\": 2101,\n"
        "            \"tls\": false,\n"
        "            \"mountpoint\": \"MOUNTPOINT\",\n"
        "            \"username\": \"your_username\",\n"
        "            \"password\": \"your_password\",\n"
        "            \"send_gga\": false,\n"
        "            \"latitude\": 0.0,\n"
        "            \"longitude\": 0.0,\n"
        "            \"eph_caster\": \"products.igs-ip.net\",\n"
        "            \"eph_port\": 2101,\n"
        "            \"eph_tls\": false,\n"
        "            \"eph_mountpoint\": \"BCEP00BKG0\",\n"
        "            \"eph_username\": \"\",\n"
        "            \"eph_password\": \"\"\n"
        "        }\n"
        "    ]\n"
        "}\n"
    );
    fclose(f);
    printf("A template config file '%s' has been created. Please edit it and set the values manually before running the program.\n", filename);
    return 0;
}
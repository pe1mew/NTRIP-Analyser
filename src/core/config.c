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

int load_config(const char *filename, NTRIP_Config *config) {
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
    fprintf(f,
        "{\n"
        "    \"NTRIP_CASTER\": \"your.caster.example.com\",\n"
        "    \"NTRIP_PORT\": 2101,\n"
        "    \"MOUNTPOINT\": \"MOUNTPOINT\",\n"
        "    \"USERNAME\": \"your_username\",\n"
        "    \"PASSWORD\": \"your_password\",\n"
        "    \"LATITUDE\": 0.0,\n"
        "    \"LONGITUDE\": 0.0,\n"
        "    \"EPH_CASTER\": \"products.igs-ip.net\",\n"
        "    \"EPH_PORT\": 2101,\n"
        "    \"EPH_MOUNTPOINT\": \"BCEP00BKG0\",\n"
        "    \"EPH_USERNAME\": \"\",\n"
        "    \"EPH_PASSWORD\": \"\"\n"
        "}\n"
    );
    fclose(f);
    printf("A template config file '%s' has been created. Please edit it and set the values manually before running the program.\n", filename);
    return 0;
}
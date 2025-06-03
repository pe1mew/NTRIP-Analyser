/**
 * @file config.c
 * @brief Implementation of configuration loader for NTRIP RTCM 3.x Stream Analyzer
 *
 * Project: NTRIP RTCM 3.x Stream Analyzer
 * Author: Remko Welling, PE1MEW
 * License: Apache License 2.0 with Commons Clause (see LICENSE for details)
 */

#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"

int load_config(const char *filename, NTRIP_Config *config) {
    FILE *file = fopen(filename, "r");
    if (!file) {
        perror("Failed to open config file");
        return -1;
    }

    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    fseek(file, 0, SEEK_SET);

    char *data = (char *)malloc(length + 1);
    if (!data) {
        perror("Failed to allocate memory for config file");
        fclose(file);
        return -1;
    }

    fread(data, 1, length, file);
    data[length] = '\0';
    fclose(file);

    cJSON *json = cJSON_Parse(data);
    free(data);

    if (!json) {
        fprintf(stderr, "Failed to parse JSON: %s\n", cJSON_GetErrorPtr());
        return -1;
    }

    // Extract configuration values
    strcpy(config->NTRIP_CASTER, cJSON_GetObjectItem(json, "NTRIP_CASTER")->valuestring);
    config->NTRIP_PORT = cJSON_GetObjectItem(json, "NTRIP_PORT")->valueint;
    strcpy(config->MOUNTPOINT, cJSON_GetObjectItem(json, "MOUNTPOINT")->valuestring);
    strcpy(config->USERNAME, cJSON_GetObjectItem(json, "USERNAME")->valuestring);
    strcpy(config->PASSWORD, cJSON_GetObjectItem(json, "PASSWORD")->valuestring);

    // New: Extract latitude and longitude if present
    cJSON *lat = cJSON_GetObjectItem(json, "LATITUDE");
    cJSON *lon = cJSON_GetObjectItem(json, "LONGITUDE");
    config->LATITUDE = (lat && cJSON_IsNumber(lat)) ? lat->valuedouble : 0.0;
    config->LONGITUDE = (lon && cJSON_IsNumber(lon)) ? lon->valuedouble : 0.0;

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
        "    \"LONGITUDE\": 0.0\n"
        "}\n"
    );
    fclose(f);
    printf("A template config file '%s' has been created. Please edit it and set the values manually before running the program.\n", filename);
    return 0;
}
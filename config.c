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
#include "lib/cJSON/cJSON.h"

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

    cJSON_Delete(json);
    return 0;
}
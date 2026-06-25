//
// io_csv.c - CSV 读写
//

#include "DataService.h"
#include "DataServiceInternal.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LINE_BUFFER_SIZE 1024

static int parse_field_token(const char* token, double* value, int* missing) {
    char buffer[128];
    size_t len = strlen(token);
    if (len >= sizeof(buffer)) return 0;
    strcpy(buffer, token);
    ds_trim_in_place(buffer);
    if (buffer[0] == '\0' ||
        ds_equals_ignore_case(buffer, "nan") ||
        strcmp(buffer, "-999") == 0 ||
        strcmp(buffer, "-9999") == 0) {
        *value = NAN;
        if (missing) (*missing)++;
        return 1;
    }
    errno = 0;
    char* end = NULL;
    double parsed = strtod(buffer, &end);
    while (end != NULL && isspace((unsigned char)*end)) end++;
    if (errno != 0 || end == buffer || (end != NULL && *end != '\0')) return 0;
    *value = parsed;
    return 1;
}

static int parse_csv_line(const char* line, Data* data, int* missing) {
    const char* cursor = line;
    for (int field = 0; field < WQ_FIELD_COUNT; field++) {
        char token[128];
        int len = 0;
        while (*cursor != '\0' && *cursor != '\n' && *cursor != '\r' && *cursor != ',') {
            if (len < (int)sizeof(token) - 1) token[len++] = *cursor;
            cursor++;
        }
        token[len] = '\0';
        double value = NAN;
        if (!parse_field_token(token, &value, missing)) return 0;
        data_service_set_field_value(data, field, value);
        if (*cursor == ',') cursor++;
        else if (field < WQ_FIELD_COUNT - 1) return 0;
    }
    return 1;
}

int data_service_read_csv(const char* path, DataSet* set, ReadSummary* summary) {
    FILE* fp = fopen(path, "r");
    char line[LINE_BUFFER_SIZE];
    data_service_dataset_init(set);
    if (summary) memset(summary, 0, sizeof(ReadSummary));
    if (fp == NULL) return 0;
    if (fgets(line, sizeof(line), fp) == NULL) { fclose(fp); return 1; }

    int record_index = 1;
    while (fgets(line, sizeof(line), fp) != NULL) {
        if (summary) summary->total_records++;
        Data data;
        memset(&data, 0, sizeof(Data));
        data.record_index = record_index++;
        int missing = 0;
        if (!parse_csv_line(line, &data, &missing)) {
            if (summary) summary->format_errors++;
            continue;
        }
        if (!data_service_dataset_push(set, data)) {
            data_service_dataset_free(set);
            fclose(fp);
            return 0;
        }
        if (summary) {
            summary->parsed_records++;
            summary->missing_values += missing;
        }
    }
    fclose(fp);
    return 1;
}

int data_service_write_csv(const char* path, const DataSet* set) {
    FILE* fp = fopen(path, "w");
    if (fp == NULL) return 0;
    fprintf(fp, "Temp(degC),Salinity(PSU),pH,DO(mg/l),precipitation(mm),Air_temp(degC)\n");
    for (int i = 0; i < set->size; i++) {
        const Data* d = &set->items[i];
        fprintf(fp, "%.8f,%.8f,%.8f,%.8f,%.8f,%.8f\n",
                d->temp, d->salinity, d->ph, d->do_value, d->precipitation, d->air_temp);
    }
    fclose(fp);
    return 1;
}

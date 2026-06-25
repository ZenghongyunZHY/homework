//
// field_meta.c - 字段元数据与字段值访问
//

#include "DataService.h"
#include "DataServiceInternal.h"

#include <ctype.h>
#include <math.h>
#include <string.h>

/* ── field definitions ── */
static const FieldMeta FIELDS[WQ_FIELD_COUNT] = {
    {"temp", "Temp", "degC", -5.0, 40.0},
    {"salinity", "Salinity", "PSU", 0.0, 45.0},
    {"ph", "pH", "", 6.5, 9.0},
    {"do", "DO", "mg/l", 0.0, 15.0},
    {"precipitation", "Precipitation", "mm", 0.0, 500.0},
    {"air_temp", "Air temp", "degC", -10.0, 50.0}
};

const FieldMeta* data_service_fields(void) {
    return FIELDS;
}

int ds_equals_ignore_case(const char* a, const char* b) {
    if (a == NULL || b == NULL) return 0;
    while (*a != '\0' && *b != '\0') {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
        a++; b++;
    }
    return *a == '\0' && *b == '\0';
}

void ds_trim_in_place(char* text) {
    char* start = text;
    while (isspace((unsigned char)*start)) start++;
    if (start != text) memmove(text, start, strlen(start) + 1);
    size_t len = strlen(text);
    while (len > 0 && isspace((unsigned char)text[len - 1])) {
        text[len - 1] = '\0'; len--;
    }
}

int data_service_field_index(const char* field) {
    if (field == NULL) return -1;
    for (int i = 0; i < WQ_FIELD_COUNT; i++) {
        if (ds_equals_ignore_case(field, FIELDS[i].key) ||
            ds_equals_ignore_case(field, FIELDS[i].label)) return i;
    }
    if (ds_equals_ignore_case(field, "do_value")) return 3;
    return -1;
}

double data_service_get_field_value(const Data* data, int field) {
    switch (field) {
        case 0: return data->temp;
        case 1: return data->salinity;
        case 2: return data->ph;
        case 3: return data->do_value;
        case 4: return data->precipitation;
        case 5: return data->air_temp;
        default: return NAN;
    }
}

void data_service_set_field_value(Data* data, int field, double value) {
    switch (field) {
        case 0: data->temp = value; break;
        case 1: data->salinity = value; break;
        case 2: data->ph = value; break;
        case 3: data->do_value = value; break;
        case 4: data->precipitation = value; break;
        case 5: data->air_temp = value; break;
        default: break;
    }
}

int data_service_is_field_in_range(int field, double value) {
    if (field < 0 || field >= WQ_FIELD_COUNT) return 0;
    return !isnan(value) && value >= FIELDS[field].min_value && value <= FIELDS[field].max_value;
}

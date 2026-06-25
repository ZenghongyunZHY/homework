#include "DataService.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

#define LINE_BUFFER_SIZE 1024

/* ── static sort state ── */
static int g_sort_field = 0;
static int g_sort_desc = 0;

/* ── field definitions ── */
static const FieldMeta FIELDS[WQ_FIELD_COUNT] = {
    {"temp", "Temp", "degC", -5.0, 40.0},
    {"salinity", "Salinity", "PSU", 0.0, 45.0},
    {"ph", "pH", "", 6.5, 9.0},
    {"do", "DO", "mg/l", 0.0, 15.0},
    {"precipitation", "Precipitation", "mm", 0.0, 500.0},
    {"air_temp", "Air temp", "degC", -10.0, 50.0}
};

/* ════════════════════════════════════════════════════════════════════
   utility helpers
   ════════════════════════════════════════════════════════════════════ */

const FieldMeta* data_service_fields(void) {
    return FIELDS;
}

static int equals_ignore_case(const char* a, const char* b) {
    if (a == NULL || b == NULL) return 0;
    while (*a != '\0' && *b != '\0') {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
        a++; b++;
    }
    return *a == '\0' && *b == '\0';
}

int data_service_field_index(const char* field) {
    if (field == NULL) return -1;
    for (int i = 0; i < WQ_FIELD_COUNT; i++) {
        if (equals_ignore_case(field, FIELDS[i].key) ||
            equals_ignore_case(field, FIELDS[i].label)) return i;
    }
    if (equals_ignore_case(field, "do_value")) return 3;
    return -1;
}

static void trim_in_place(char* text) {
    char* start = text;
    while (isspace((unsigned char)*start)) start++;
    if (start != text) memmove(text, start, strlen(start) + 1);
    size_t len = strlen(text);
    while (len > 0 && isspace((unsigned char)text[len - 1])) {
        text[len - 1] = '\0'; len--;
    }
}

/* ════════════════════════════════════════════════════════════════════
   dataset lifecycle
   ════════════════════════════════════════════════════════════════════ */

void data_service_dataset_init(DataSet* set) {
    set->items = NULL;
    set->size = 0;
    set->capacity = 0;
}

void data_service_dataset_free(DataSet* set) {
    if (set == NULL) return;
    free(set->items);
    set->items = NULL;
    set->size = 0;
    set->capacity = 0;
}

static int dataset_reserve(DataSet* set, int capacity) {
    if (capacity <= set->capacity) return 1;
    Data* next = (Data*)realloc(set->items, sizeof(Data) * capacity);
    if (next == NULL) return 0;
    set->items = next;
    set->capacity = capacity;
    return 1;
}

int data_service_dataset_push(DataSet* set, Data item) {
    if (set->size == set->capacity) {
        int next_capacity = set->capacity == 0 ? 1000 : set->capacity * 2;
        if (!dataset_reserve(set, next_capacity)) return 0;
    }
    set->items[set->size++] = item;
    return 1;
}

/* ════════════════════════════════════════════════════════════════════
   field access
   ════════════════════════════════════════════════════════════════════ */

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
    return !isnan(value) && value >= FIELDS[field].min_value && value <= FIELDS[field].max_value;
}

/* ════════════════════════════════════════════════════════════════════
   CSV I/O
   ════════════════════════════════════════════════════════════════════ */

static int parse_field_token(const char* token, double* value, int* missing) {
    char buffer[128];
    size_t len = strlen(token);
    if (len >= sizeof(buffer)) return 0;
    strcpy(buffer, token);
    trim_in_place(buffer);
    if (buffer[0] == '\0' ||
        equals_ignore_case(buffer, "nan") ||
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

/* ════════════════════════════════════════════════════════════════════
   marks I/O
   ════════════════════════════════════════════════════════════════════ */

int data_service_write_marks(const char* path, const DataSet* source) {
    FILE* fp = fopen(path, "w");
    if (fp == NULL) return 0;
    fprintf(fp, "row,action,missing_count,abnormal_count\n");
    for (int i = 0; i < source->size; i++) {
        int missing_count = 0, abnormal_count = 0;
        for (int field = 0; field < WQ_FIELD_COUNT; field++) {
            double value = data_service_get_field_value(&source->items[i], field);
            if (isnan(value)) missing_count++;
            else if (!data_service_is_field_in_range(field, value)) abnormal_count++;
        }
        const char* action = "none";
        if (abnormal_count >= 3) action = "delete";
        else if (missing_count > 0 && abnormal_count > 0) action = "repair_fill";
        else if (abnormal_count > 0) action = "repair";
        else if (missing_count > 0) action = "fill";
        fprintf(fp, "%d,%s,%d,%d\n", source->items[i].record_index, action, missing_count, abnormal_count);
    }
    fclose(fp);
    return 1;
}

int data_service_read_marks(const char* path, MarkSet* marks) {
    FILE* fp = fopen(path, "r");
    char line[256];
    marks->items = NULL; marks->size = 0; marks->capacity = 0;
    if (fp == NULL) return 0;
    fgets(line, sizeof(line), fp);
    while (fgets(line, sizeof(line), fp) != NULL) {
        RowMark mark;
        memset(&mark, 0, sizeof(RowMark));
        if (sscanf(line, "%d,%23[^,],%d,%d",
                   &mark.row, mark.action, &mark.missing_count, &mark.abnormal_count) >= 2) {
            /* manual dynamic push */
            if (marks->size == marks->capacity) {
                int nc = marks->capacity == 0 ? 1000 : marks->capacity * 2;
                RowMark* next = (RowMark*)realloc(marks->items, sizeof(RowMark) * nc);
                if (next == NULL) break;
                marks->items = next;
                marks->capacity = nc;
            }
            marks->items[marks->size++] = mark;
        }
    }
    fclose(fp);
    return 1;
}

const RowMark* data_service_find_mark(const MarkSet* marks, int row) {
    if (marks == NULL) return NULL;
    for (int i = 0; i < marks->size; i++)
        if (marks->items[i].row == row) return &marks->items[i];
    return NULL;
}

/* ════════════════════════════════════════════════════════════════════
   operation marks I/O
   ════════════════════════════════════════════════════════════════════ */

void data_service_operation_mark_set_init(OperationMarkSet* set) {
    set->items = NULL; set->size = 0; set->capacity = 0;
}

void data_service_operation_mark_set_free(OperationMarkSet* set) {
    free(set->items);
    set->items = NULL; set->size = 0; set->capacity = 0;
}

int data_service_operation_mark_set_push(OperationMarkSet* set, OperationMark item) {
    if (set->size == set->capacity) {
        int nc = set->capacity == 0 ? 1000 : set->capacity * 2;
        OperationMark* next = (OperationMark*)realloc(set->items, sizeof(OperationMark) * nc);
        if (next == NULL) return 0;
        set->items = next;
        set->capacity = nc;
    }
    set->items[set->size++] = item;
    return 1;
}

static char* read_text_file(const char* path) {
    FILE* fp = fopen(path, "rb");
    if (fp == NULL) return NULL;
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    if (size < 0) { fclose(fp); return NULL; }
    rewind(fp);
    char* text = (char*)malloc((size_t)size + 1);
    if (text == NULL) { fclose(fp); return NULL; }
    size_t read_size = fread(text, 1, (size_t)size, fp);
    text[read_size] = '\0';
    fclose(fp);
    return text;
}

static int parse_json_string_value(const char* object_start, const char* key, char* out, int out_size) {
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char* key_pos = strstr(object_start, pattern);
    if (key_pos == NULL) return 0;
    const char* colon = strchr(key_pos, ':');
    if (colon == NULL) return 0;
    const char* quote = strchr(colon, '"');
    if (quote == NULL) return 0;
    quote++;
    int len = 0;
    while (*quote != '\0' && *quote != '"' && len < out_size - 1) out[len++] = *quote++;
    out[len] = '\0';
    return len > 0;
}

static void parse_json_fields(const char* object_start, OperationMark* mark) {
    const char* fields_pos = strstr(object_start, "\"fields\"");
    if (fields_pos == NULL) return;
    const char* start = strchr(fields_pos, '[');
    const char* end = strchr(fields_pos, ']');
    if (start == NULL || end == NULL || end <= start) return;
    for (int field = 0; field < WQ_FIELD_COUNT; field++) {
        char pattern[32];
        snprintf(pattern, sizeof(pattern), "\"%s\"", FIELDS[field].key);
        if (strstr(start, pattern) != NULL && strstr(start, pattern) < end)
            mark->fields[field] = 1;
    }
}

int data_service_read_operation_marks(const char* path, OperationMarkSet* marks) {
    data_service_operation_mark_set_init(marks);
    char* text = read_text_file(path);
    if (text == NULL) return 0;
    char* rows = strstr(text, "\"rows\"");
    if (rows == NULL) { free(text); return 1; }
    char* cursor = strchr(rows, '{');
    if (cursor == NULL) { free(text); return 1; }
    while ((cursor = strchr(cursor, '"')) != NULL) {
        char* end_quote = strchr(cursor + 1, '"');
        if (end_quote == NULL) break;
        char row_text[32];
        int len = (int)(end_quote - cursor - 1);
        if (len <= 0 || len >= (int)sizeof(row_text)) { cursor = end_quote + 1; continue; }
        memcpy(row_text, cursor + 1, (size_t)len);
        row_text[len] = '\0';
        int all_digits = 1;
        for (int i = 0; row_text[i] != '\0'; i++)
            if (!isdigit((unsigned char)row_text[i])) { all_digits = 0; break; }
        if (!all_digits) { cursor = end_quote + 1; continue; }
        char* object_start = strchr(end_quote, '{');
        char* object_end = strchr(end_quote, '}');
        if (object_start == NULL || object_end == NULL) break;
        OperationMark mark;
        memset(&mark, 0, sizeof(OperationMark));
        mark.row = atoi(row_text);
        if (!parse_json_string_value(object_start, "status", mark.status, sizeof(mark.status)))
            strcpy(mark.status, "none");
        parse_json_fields(object_start, &mark);
        data_service_operation_mark_set_push(marks, mark);
        cursor = object_end + 1;
    }
    free(text);
    return 1;
}

int data_service_write_operation_marks(const char* path, const OperationMarkSet* marks) {
    FILE* fp = fopen(path, "w");
    if (fp == NULL) return 0;
    fprintf(fp, "{\"rows\":{");
    int first = 1;
    for (int i = 0; i < marks->size; i++) {
        if (!first) fprintf(fp, ",");
        first = 0;
        fprintf(fp, "\"%d\":{\"status\":\"%s\",\"fields\":[", marks->items[i].row, marks->items[i].status);
        int ffirst = 1;
        for (int f = 0; f < WQ_FIELD_COUNT; f++) {
            if (marks->items[i].fields[f]) {
                if (!ffirst) fprintf(fp, ",");
                ffirst = 0;
                fprintf(fp, "\"%s\"", FIELDS[f].key);
            }
        }
        fprintf(fp, "]}");
    }
    fprintf(fp, "}}\n");
    fclose(fp);
    return 1;
}

const OperationMark* data_service_find_operation_mark(const OperationMarkSet* marks, int row) {
    if (marks == NULL) return NULL;
    for (int i = 0; i < marks->size; i++)
        if (marks->items[i].row == row) return &marks->items[i];
    return NULL;
}

/* ════════════════════════════════════════════════════════════════════
   preprocessing
   ════════════════════════════════════════════════════════════════════ */

static double approximate_value(const DataSet* set, int index, int field, double global_mean) {
    double sum = 0.0;
    int count = 0;
    for (int step = 1; step <= 10; step++) {
        int prev = index - step, next = index + step;
        if (prev >= 0) {
            double value = data_service_get_field_value(&set->items[prev], field);
            if (!isnan(value)) { sum += value; count++; }
        }
        if (next < set->size) {
            double value = data_service_get_field_value(&set->items[next], field);
            if (!isnan(value)) { sum += value; count++; }
        }
    }
    return count > 0 ? sum / count : global_mean;
}

int data_service_preprocess(const DataSet* source, DataSet* clean, PreprocessResult* result) {
    data_service_dataset_init(clean);
    memset(result, 0, sizeof(PreprocessResult));
    result->total_records = source->size;

    for (int i = 0; i < source->size; i++) {
        Data item = source->items[i];
        int abnormal_fields = 0, missing_fields = 0;
        for (int field = 0; field < WQ_FIELD_COUNT; field++) {
            double value = data_service_get_field_value(&item, field);
            if (isnan(value)) { missing_fields++; result->missing_values++; }
            else if (!data_service_is_field_in_range(field, value)) {
                abnormal_fields++; result->abnormal_values++;
            }
        }
        if (abnormal_fields > 0) result->abnormal_records++;
        if (abnormal_fields >= 3) { result->deleted_records++; continue; }
        for (int field = 0; field < WQ_FIELD_COUNT; field++) {
            double value = data_service_get_field_value(&item, field);
            if (!isnan(value) && !data_service_is_field_in_range(field, value))
                data_service_set_field_value(&item, field, NAN);
        }
        if (!data_service_dataset_push(clean, item)) { data_service_dataset_free(clean); return 0; }
    }

    double global_means[WQ_FIELD_COUNT] = {0};
    int global_counts[WQ_FIELD_COUNT] = {0};
    for (int field = 0; field < WQ_FIELD_COUNT; field++) {
        for (int i = 0; i < clean->size; i++) {
            double value = data_service_get_field_value(&clean->items[i], field);
            if (!isnan(value)) { global_means[field] += value; global_counts[field]++; }
        }
        global_means[field] = global_counts[field] == 0 ? 0.0 : global_means[field] / global_counts[field];
    }

    for (int i = 0; i < clean->size; i++) {
        for (int field = 0; field < WQ_FIELD_COUNT; field++) {
            if (isnan(data_service_get_field_value(&clean->items[i], field))) {
                double fill = approximate_value(clean, i, field, global_means[field]);
                data_service_set_field_value(&clean->items[i], field, fill);
                result->filled_values++;
            }
        }
    }
    result->kept_records = clean->size;
    return 1;
}

/* ════════════════════════════════════════════════════════════════════
   statistics
   ════════════════════════════════════════════════════════════════════ */

void data_service_compute_basic_stats(const DataSet* set, BasicStats stats[WQ_FIELD_COUNT]) {
    for (int field = 0; field < WQ_FIELD_COUNT; field++) {
        stats[field].count = 0;
        stats[field].mean = 0.0;
        stats[field].min_value = INFINITY;
        stats[field].max_value = -INFINITY;
        stats[field].stddev = 0.0;
    }
    for (int field = 0; field < WQ_FIELD_COUNT; field++) {
        double sum = 0.0;
        for (int i = 0; i < set->size; i++) {
            double value = data_service_get_field_value(&set->items[i], field);
            if (isnan(value)) continue;
            stats[field].count++;
            sum += value;
            if (value < stats[field].min_value) stats[field].min_value = value;
            if (value > stats[field].max_value) stats[field].max_value = value;
        }
        stats[field].mean = stats[field].count > 0 ? sum / stats[field].count : 0.0;
        if (stats[field].count == 0) { stats[field].min_value = NAN; stats[field].max_value = NAN; }
    }
    for (int field = 0; field < WQ_FIELD_COUNT; field++) {
        if (stats[field].count == 0) continue;
        double ss = 0.0;
        for (int i = 0; i < set->size; i++) {
            double value = data_service_get_field_value(&set->items[i], field);
            if (!isnan(value)) { double delta = value - stats[field].mean; ss += delta * delta; }
        }
        stats[field].stddev = sqrt(ss / stats[field].count);
    }
}

double data_service_pearson(const DataSet* set, int x_field, int y_field) {
    int count = 0;
    double sum_x = 0, sum_y = 0, sum_x2 = 0, sum_y2 = 0, sum_xy = 0;
    for (int i = 0; i < set->size; i++) {
        double x = data_service_get_field_value(&set->items[i], x_field);
        double y = data_service_get_field_value(&set->items[i], y_field);
        if (isnan(x) || isnan(y)) continue;
        count++;
        sum_x += x; sum_y += y;
        sum_x2 += x * x; sum_y2 += y * y;
        sum_xy += x * y;
    }
    if (count < 2) return NAN;
    double numerator = count * sum_xy - sum_x * sum_y;
    double denominator = sqrt((count * sum_x2 - sum_x * sum_x) *
                              (count * sum_y2 - sum_y * sum_y));
    return denominator == 0.0 ? NAN : numerator / denominator;
}

/* ════════════════════════════════════════════════════════════════════
   warnings
   ════════════════════════════════════════════════════════════════════ */

static void time_string_for_index(int index, char* buffer, size_t size) {
    struct tm start;
    memset(&start, 0, sizeof(start));
    start.tm_year = 2025 - 1900; start.tm_mon = 0; start.tm_mday = 1;
    start.tm_hour = 12; start.tm_min = 0;
    time_t base = mktime(&start);
    time_t value = base + (time_t)index * 5 * 60;
    struct tm out;
#ifdef _WIN32
    localtime_s(&out, &value);
#else
    localtime_r(&value, &out);
#endif
    strftime(buffer, size, "%Y-%m-%d %H:%M", &out);
}

int data_service_detect_warnings(const DataSet* set, WarningSet* warnings) {
    warnings->items = NULL; warnings->size = 0; warnings->capacity = 0;
    int max_day = (12 * 60 + set->size * 5) / 1440 + 1;

    for (int day = 0; day <= max_day; day++) {
        double sum = 0.0; int count = 0; int first_index = -1;
        for (int i = 0; i < set->size; i++) {
            int original_index = set->items[i].record_index - 1;
            int absolute_minutes = 12 * 60 + original_index * 5;
            int current_day = absolute_minutes / 1440;
            int minute_of_day = absolute_minutes % 1440;
            if (current_day == day && minute_of_day >= 180 && minute_of_day <= 300) {
                double value = set->items[i].do_value;
                if (!isnan(value)) { sum += value; count++; if (first_index < 0) first_index = original_index; }
            }
        }
        if (count > 0) {
            double avg = sum / count;
            const char* level = NULL; const char* advice = NULL;
            if (avg < 3.0) { level = "severe_low_oxygen"; advice = "Add granular oxygen immediately and reduce feeding."; }
            else if (avg < 4.0) { level = "low_oxygen"; advice = "Turn on bottom aerator."; }
            if (level != NULL) {
                if (warnings->size == warnings->capacity) {
                    int nc = warnings->capacity == 0 ? 256 : warnings->capacity * 2;
                    WarningItem* next = (WarningItem*)realloc(warnings->items, sizeof(WarningItem) * nc);
                    if (next == NULL) return warnings->size;
                    warnings->items = next; warnings->capacity = nc;
                }
                WarningItem* w = &warnings->items[warnings->size++];
                time_string_for_index(first_index, w->time, sizeof(w->time));
                snprintf(w->type, sizeof(w->type), "%s", level);
                w->value = avg;
                snprintf(w->advice, sizeof(w->advice), "%s", advice);
            }
        }
    }

    /* salinity 1h drop */
    for (int i = 12; i < set->size; i++) {
        double prev = set->items[i - 12].salinity, cur = set->items[i].salinity;
        if (!isnan(prev) && !isnan(cur) && prev - cur > 2.0) {
            if (warnings->size == warnings->capacity) {
                int nc = warnings->capacity == 0 ? 256 : warnings->capacity * 2;
                WarningItem* next = (WarningItem*)realloc(warnings->items, sizeof(WarningItem) * nc);
                if (next == NULL) return warnings->size;
                warnings->items = next; warnings->capacity = nc;
            }
            WarningItem* w = &warnings->items[warnings->size++];
            time_string_for_index(set->items[i].record_index - 1, w->time, sizeof(w->time));
            snprintf(w->type, sizeof(w->type), "salinity_drop_1h");
            w->value = prev - cur;
            snprintf(w->advice, sizeof(w->advice), "Close inlet and add VC or glucose.");
        }
    }
    /* salinity 24h drop */
    for (int i = 288; i < set->size; i++) {
        double prev = set->items[i - 288].salinity, cur = set->items[i].salinity;
        if (!isnan(prev) && !isnan(cur) && prev - cur > 5.0) {
            if (warnings->size == warnings->capacity) {
                int nc = warnings->capacity == 0 ? 256 : warnings->capacity * 2;
                WarningItem* next = (WarningItem*)realloc(warnings->items, sizeof(WarningItem) * nc);
                if (next == NULL) return warnings->size;
                warnings->items = next; warnings->capacity = nc;
            }
            WarningItem* w = &warnings->items[warnings->size++];
            time_string_for_index(set->items[i].record_index - 1, w->time, sizeof(w->time));
            snprintf(w->type, sizeof(w->type), "salinity_drop_24h");
            w->value = prev - cur;
            snprintf(w->advice, sizeof(w->advice), "Close inlet and add VC or glucose.");
        }
    }
    return warnings->size;
}

/* ════════════════════════════════════════════════════════════════════
   prediction
   ════════════════════════════════════════════════════════════════════ */

RegressionResult data_service_linear_regression(const DataSet* set, int x_field) {
    RegressionResult result;
    memset(&result, 0, sizeof(result));
    int count = 0;
    double sum_x = 0, sum_y = 0, sum_x2 = 0, sum_xy = 0;
    for (int i = 0; i < set->size; i++) {
        double x = data_service_get_field_value(&set->items[i], x_field);
        double y = data_service_get_field_value(&set->items[i], 3);
        if (isnan(x) || isnan(y)) continue;
        count++;
        sum_x += x; sum_y += y;
        sum_x2 += x * x; sum_xy += x * y;
    }
    result.count = count;
    if (count < 2) { result.slope = result.intercept = result.r2 = result.rmse = NAN; return result; }
    double denominator = count * sum_x2 - sum_x * sum_x;
    if (denominator == 0.0) { result.slope = result.intercept = result.r2 = result.rmse = NAN; return result; }
    result.slope = (count * sum_xy - sum_x * sum_y) / denominator;
    result.intercept = (sum_y - result.slope * sum_x) / count;
    double mean_y = sum_y / count, ss_res = 0, ss_tot = 0;
    for (int i = 0; i < set->size; i++) {
        double x = data_service_get_field_value(&set->items[i], x_field);
        double y = data_service_get_field_value(&set->items[i], 3);
        if (isnan(x) || isnan(y)) continue;
        double predicted = result.slope * x + result.intercept;
        ss_res += (y - predicted) * (y - predicted);
        ss_tot += (y - mean_y) * (y - mean_y);
    }
    result.r2 = ss_tot == 0.0 ? NAN : 1.0 - ss_res / ss_tot;

    int train_target = (int)(count * 0.8);
    if (train_target < 2 || count - train_target < 1) { result.rmse = NAN; return result; }
    int train_count = 0;
    double tx = 0, ty = 0, tx2 = 0, txy = 0;
    for (int i = 0; i < set->size && train_count < train_target; i++) {
        double x = data_service_get_field_value(&set->items[i], x_field);
        double y = data_service_get_field_value(&set->items[i], 3);
        if (isnan(x) || isnan(y)) continue;
        train_count++;
        tx += x; ty += y; tx2 += x * x; txy += x * y;
    }
    double train_den = train_count * tx2 - tx * tx;
    if (train_den == 0.0) { result.rmse = NAN; return result; }
    double train_slope = (train_count * txy - tx * ty) / train_den;
    double train_intercept = (ty - train_slope * tx) / train_count;
    int valid_index = 0, test_count = 0;
    double mse = 0;
    for (int i = 0; i < set->size; i++) {
        double x = data_service_get_field_value(&set->items[i], x_field);
        double y = data_service_get_field_value(&set->items[i], 3);
        if (isnan(x) || isnan(y)) continue;
        if (valid_index >= train_target) {
            double predicted = train_slope * x + train_intercept;
            mse += (y - predicted) * (y - predicted);
            test_count++;
        }
        valid_index++;
    }
    result.rmse = test_count == 0 ? NAN : sqrt(mse / test_count);
    return result;
}

/* ════════════════════════════════════════════════════════════════════
   moving average filter
   ════════════════════════════════════════════════════════════════════ */

int data_service_moving_average_filter(const DataSet* source, int window,
    DataSet* filtered, BasicStats before[WQ_FIELD_COUNT], BasicStats after[WQ_FIELD_COUNT]) {
    data_service_dataset_init(filtered);
    for (int i = 0; i < source->size; i++)
        if (!data_service_dataset_push(filtered, source->items[i])) { data_service_dataset_free(filtered); return 0; }

    int half = window / 2;
    for (int i = 0; i < source->size; i++) {
        int start = i - half, end = i + half;
        if (start < 0) start = 0;
        if (end >= source->size) end = source->size - 1;
        for (int field = 0; field < 4; field++) {
            double sum = 0; int count = 0;
            for (int row = start; row <= end; row++) {
                double value = data_service_get_field_value(&source->items[row], field);
                if (!isnan(value)) { sum += value; count++; }
            }
            if (count > 0) data_service_set_field_value(&filtered->items[i], field, sum / count);
        }
    }
    data_service_compute_basic_stats(source, before);
    data_service_compute_basic_stats(filtered, after);
    return 1;
}

/* ════════════════════════════════════════════════════════════════════
   query
   ════════════════════════════════════════════════════════════════════ */

static int compare_data_for_sort(const void* a, const void* b) {
    const Data* left = (const Data*)a;
    const Data* right = (const Data*)b;
    double lv = data_service_get_field_value(left, g_sort_field);
    double rv = data_service_get_field_value(right, g_sort_field);
    int result = 0;
    if (isnan(lv) && isnan(rv)) result = 0;
    else if (isnan(lv)) return 1;
    else if (isnan(rv)) return -1;
    else if (lv < rv) result = -1;
    else if (lv > rv) result = 1;
    return g_sort_desc ? -result : result;
}

static int raw_mark_matches_filter(const RowMark* mark, const char* filter) {
    const char* action = mark == NULL ? "none" : mark->action;
    if (filter == NULL || strcmp(filter, "all") == 0 || filter[0] == '\0') return 1;
    return strcmp(action, filter) == 0;
}

static int op_mark_matches_filter(const OperationMark* mark, const char* filter) {
    const char* status = mark == NULL ? "none" : mark->status;
    if (filter == NULL || strcmp(filter, "all") == 0 || filter[0] == '\0') return 1;
    return strcmp(status, filter) == 0;
}

QueryPage data_service_query_page(const DataSet* set, int page, int page_size,
    int filter_field, double filter_min, double filter_max,
    int sort_field, int sort_desc,
    const MarkSet* raw_marks, const OperationMarkSet* op_marks,
    const char* view_mode, const char* operation_filter) {
    QueryPage result;
    memset(&result, 0, sizeof(result));
    if (page < 1) page = 1;
    if (page_size < 1 || page_size > 200) page_size = 15;

    DataSet filtered;
    data_service_dataset_init(&filtered);
    for (int i = 0; i < set->size; i++) {
        int keep = 1;
        if (filter_field >= 0) {
            double value = data_service_get_field_value(&set->items[i], filter_field);
            if (isnan(value) || value < filter_min || value > filter_max) keep = 0;
        }
        if (keep && view_mode != NULL && strcmp(view_mode, "processed") == 0) {
            const OperationMark* m = data_service_find_operation_mark(op_marks, set->items[i].record_index);
            keep = op_mark_matches_filter(m, operation_filter);
        } else if (keep) {
            const RowMark* m = data_service_find_mark(raw_marks, set->items[i].record_index);
            keep = raw_mark_matches_filter(m, operation_filter);
        }
        if (keep) data_service_dataset_push(&filtered, set->items[i]);
    }

    if (sort_field >= 0 && sort_field < WQ_FIELD_COUNT) {
        g_sort_field = sort_field; g_sort_desc = sort_desc;
        qsort(filtered.items, (size_t)filtered.size, sizeof(Data), compare_data_for_sort);
    }

    result.total = filtered.size;
    result.total_pages = result.total == 0 ? 1 : (result.total + page_size - 1) / page_size;
    result.page_size = page_size;
    if (page > result.total_pages) page = result.total_pages;
    result.page = page;

    int start = (page - 1) * page_size;
    int end = start + page_size;
    if (end > result.total) end = result.total;
    int count = end - start;
    result.items = (Data*)malloc(sizeof(Data) * (size_t)(count > 0 ? count : 1));
    if (result.items && count > 0)
        memcpy(result.items, &filtered.items[start], sizeof(Data) * (size_t)count);
    data_service_dataset_free(&filtered);
    return result;
}

/* ════════════════════════════════════════════════════════════════════
   data modification
   ════════════════════════════════════════════════════════════════════ */

int data_service_modify_record(DataSet* set, int row, int field, double new_value, double* old_value_out) {
    for (int i = 0; i < set->size; i++) {
        if (set->items[i].record_index == row) {
            if (old_value_out) *old_value_out = data_service_get_field_value(&set->items[i], field);
            data_service_set_field_value(&set->items[i], field, new_value);
            return 1;
        }
    }
    return 0;
}

int data_service_delete_records(DataSet* set, int row, int field,
    double min_value, double max_value, int** deleted_rows_out, int* deleted_count_out) {
    int deleted = 0;
    int* rows = (int*)malloc(sizeof(int) * (size_t)set->size);
    if (rows == NULL) return -1;

    for (int i = 0; i < set->size; i++) {
        int remove = 0;
        if (row >= 1 && set->items[i].record_index == row) remove = 1;
        if (field >= 0) {
            double value = data_service_get_field_value(&set->items[i], field);
            if (!isnan(value) && value >= min_value && value <= max_value) remove = 1;
        }
        if (remove) rows[deleted++] = set->items[i].record_index;
    }
    if (deleted == 0) { free(rows); *deleted_rows_out = NULL; *deleted_count_out = 0; return 0; }

    /* actually remove from dataset */
    DataSet kept;
    data_service_dataset_init(&kept);
    for (int i = 0; i < set->size; i++) {
        int skip = 0;
        for (int j = 0; j < deleted; j++)
            if (set->items[i].record_index == rows[j]) { skip = 1; break; }
        if (!skip) data_service_dataset_push(&kept, set->items[i]);
    }
    free(set->items);
    set->items = kept.items;
    set->size = kept.size;
    set->capacity = kept.capacity;

    *deleted_rows_out = rows;
    *deleted_count_out = deleted;
    return deleted;
}

int data_service_add_record(DataSet* set, Data item) {
    item.record_index = set->size > 0 ? set->items[set->size - 1].record_index + 1 : 1;
    return data_service_dataset_push(set, item) ? item.record_index : 0;
}

/* ════════════════════════════════════════════════════════════════════
   login
   ════════════════════════════════════════════════════════════════════ */

User* data_service_validate_login(const char* username, const char* password) {
    static User users[2] = {
        {"admin", "123456", 1},
        {"guest", "guest", 0}
    };
    for (int i = 0; i < 2; i++) {
        if (strcmp(username, users[i].name) == 0 && strcmp(password, users[i].password) == 0)
            return &users[i];
    }
    return NULL;
}

/* ════════════════════════════════════════════════════════════════════
   binary storage
   ════════════════════════════════════════════════════════════════════ */

int data_service_write_binary(const char* path, const DataSet* set) {
    FILE* fp = fopen(path, "wb");
    if (fp == NULL) return 0;
    int32_t count = (int32_t)set->size;
    if (fwrite(&count, sizeof(count), 1, fp) != 1) { fclose(fp); return 0; }
    if (fwrite(set->items, sizeof(Data), (size_t)count, fp) != (size_t)count) { fclose(fp); return 0; }
    fclose(fp);
    return 1;
}

int data_service_read_binary(const char* path, DataSet* set, ReadSummary* summary) {
    FILE* fp = fopen(path, "rb");
    data_service_dataset_init(set);
    if (summary) memset(summary, 0, sizeof(ReadSummary));
    if (fp == NULL) return 0;
    int32_t count = 0;
    if (fread(&count, sizeof(count), 1, fp) != 1) { fclose(fp); return 0; }
    if (count <= 0) { fclose(fp); return 1; }
    set->items = (Data*)malloc(sizeof(Data) * (size_t)count);
    if (set->items == NULL) { fclose(fp); return 0; }
    size_t read_count = fread(set->items, sizeof(Data), (size_t)count, fp);
    fclose(fp);
    if (read_count != (size_t)count) { data_service_dataset_free(set); return 0; }
    set->size = (int)read_count;
    set->capacity = (int)read_count;
    if (summary) { summary->total_records = set->size; summary->parsed_records = set->size; }
    return 1;
}

StorageBenchmark data_service_benchmark_storage(const char* csv_path) {
    StorageBenchmark bench;
    memset(&bench, 0, sizeof(bench));

    DataSet set;
    ReadSummary summary;
    if (!data_service_read_csv(csv_path, &set, &summary)) return bench;

    /* CSV file size */
    FILE* fp = fopen(csv_path, "rb");
    if (fp) { fseek(fp, 0, SEEK_END); bench.csv_size_bytes = ftell(fp); fclose(fp); }

    /* CSV write time */
    clock_t t0 = clock();
    data_service_write_csv("_bench_csv.csv", &set);
    bench.csv_write_seconds = (double)(clock() - t0) / CLOCKS_PER_SEC;

    /* CSV read time */
    DataSet csv_set;
    t0 = clock();
    data_service_read_csv("_bench_csv.csv", &csv_set, NULL);
    bench.csv_read_seconds = (double)(clock() - t0) / CLOCKS_PER_SEC;
    data_service_dataset_free(&csv_set);

    /* binary write time */
    t0 = clock();
    data_service_write_binary("_bench_bin.dat", &set);
    bench.bin_write_seconds = (double)(clock() - t0) / CLOCKS_PER_SEC;

    /* binary file size */
    fp = fopen("_bench_bin.dat", "rb");
    if (fp) { fseek(fp, 0, SEEK_END); bench.bin_size_bytes = ftell(fp); fclose(fp); }

    /* binary read time */
    DataSet bin_set;
    t0 = clock();
    data_service_read_binary("_bench_bin.dat", &bin_set, NULL);
    bench.bin_read_seconds = (double)(clock() - t0) / CLOCKS_PER_SEC;
    data_service_dataset_free(&bin_set);

    data_service_dataset_free(&set);
    remove("_bench_csv.csv");
    remove("_bench_bin.dat");
    return bench;
}

/* ════════════════════════════════════════════════════════════════════
   backup / restore
   ════════════════════════════════════════════════════════════════════ */

static void make_backup_dir(void) {
#ifdef _WIN32
    CreateDirectoryA("backup", NULL);
#else
    mkdir("backup", 0755);
#endif
}

char* data_service_backup_file(const char* src_path) {
    make_backup_dir();
    time_t now = time(NULL);
    struct tm tm_now;
#ifdef _WIN32
    localtime_s(&tm_now, &now);
#else
    localtime_r(&now, &tm_now);
#endif
    char filename[256];
    snprintf(filename, sizeof(filename), "backup/backup_%04d%02d%02d_%02d%02d%02d.csv",
             tm_now.tm_year + 1900, tm_now.tm_mon + 1, tm_now.tm_mday,
             tm_now.tm_hour, tm_now.tm_min, tm_now.tm_sec);

    FILE* src = fopen(src_path, "rb");
    if (src == NULL) return NULL;
    FILE* dst = fopen(filename, "wb");
    if (dst == NULL) { fclose(src); return NULL; }
    char buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), src)) > 0) fwrite(buf, 1, n, dst);
    fclose(src); fclose(dst);

    char* result = (char*)malloc(strlen(filename) + 1);
    if (result) strcpy(result, filename);
    return result;
}

char** data_service_list_backups(const char* dir_path, int* count_out) {
    *count_out = 0;
    char** list = NULL;
    int cap = 0;
#ifdef _WIN32
    char search_path[512];
    snprintf(search_path, sizeof(search_path), "%s\\backup_*.csv", dir_path);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(search_path, &fd);
    if (h == INVALID_HANDLE_VALUE) return NULL;
    do {
        if (*count_out == cap) {
            cap = cap == 0 ? 16 : cap * 2;
            char** next = (char**)realloc(list, sizeof(char*) * (size_t)cap);
            if (next == NULL) break;
            list = next;
        }
        list[*count_out] = (char*)malloc(strlen(fd.cFileName) + 1);
        if (list[*count_out]) strcpy(list[*count_out], fd.cFileName);
        (*count_out)++;
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR* dir = opendir(dir_path);
    if (dir == NULL) return NULL;
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, "backup_", 7) != 0) continue;
        if (*count_out == cap) {
            cap = cap == 0 ? 16 : cap * 2;
            char** next = (char**)realloc(list, sizeof(char*) * (size_t)cap);
            if (next == NULL) break;
            list = next;
        }
        list[*count_out] = (char*)malloc(strlen(entry->d_name) + 1);
        if (list[*count_out]) strcpy(list[*count_out], entry->d_name);
        (*count_out)++;
    }
    closedir(dir);
#endif
    return list;
}

int data_service_restore_from_backup(const char* backup_path, DataSet* set, ReadSummary* summary) {
    char full_path[512];
    snprintf(full_path, sizeof(full_path), "backup/%s", backup_path);
    return data_service_read_csv(full_path, set, summary);
}

/* ════════════════════════════════════════════════════════════════════
   report generation
   ════════════════════════════════════════════════════════════════════ */

static FILE* report_open(const char* path) {
    FILE* fp = fopen(path, "w");
    if (fp == NULL) return NULL;
    time_t now = time(NULL);
    struct tm tm_now;
#ifdef _WIN32
    localtime_s(&tm_now, &now);
#else
    localtime_r(&now, &tm_now);
#endif
    fprintf(fp, "========================================\n");
    fprintf(fp, "  海水养殖水质数据分析系统 - 报告\n");
    fprintf(fp, "  生成时间: %04d-%02d-%02d %02d:%02d:%02d\n",
            tm_now.tm_year + 1900, tm_now.tm_mon + 1, tm_now.tm_mday,
            tm_now.tm_hour, tm_now.tm_min, tm_now.tm_sec);
    fprintf(fp, "========================================\n\n");
    return fp;
}

int data_service_generate_overview_report(const char* path, const DataSet* set,
    const ReadSummary* summary, const PreprocessResult* preprocess) {
    FILE* fp = report_open(path);
    if (fp == NULL) return 0;
    fprintf(fp, "【数据概览】\n\n");
    fprintf(fp, "  当前记录数:     %d\n", set->size);
    if (summary) {
        fprintf(fp, "  原始总记录数:   %d\n", summary->total_records);
        fprintf(fp, "  成功解析记录数: %d\n", summary->parsed_records);
        fprintf(fp, "  格式错误记录数: %d\n", summary->format_errors);
        fprintf(fp, "  缺失值个数:     %d\n", summary->missing_values);
    }
    if (preprocess) {
        fprintf(fp, "\n【预处理结果】\n\n");
        fprintf(fp, "  异常记录数:     %d\n", preprocess->abnormal_records);
        fprintf(fp, "  异常值个数:     %d\n", preprocess->abnormal_values);
        fprintf(fp, "  删除记录数:     %d\n", preprocess->deleted_records);
        fprintf(fp, "  填充缺失值数:   %d\n", preprocess->filled_values);
        fprintf(fp, "  保留记录数:     %d\n", preprocess->kept_records);
    }
    fprintf(fp, "\n========================================\n");
    fclose(fp);
    return 1;
}

int data_service_generate_stats_report(const char* path,
    const BasicStats stats[WQ_FIELD_COUNT], double corr[WQ_FIELD_COUNT][WQ_FIELD_COUNT]) {
    FILE* fp = report_open(path);
    if (fp == NULL) return 0;
    fprintf(fp, "【基本统计量】\n\n");
    fprintf(fp, "%-16s %10s %10s %10s %10s\n", "参数", "均值", "最小值", "最大值", "标准差");
    fprintf(fp, "------------------------------------------------------------\n");
    const FieldMeta* fields = data_service_fields();
    for (int i = 0; i < WQ_FIELD_COUNT; i++) {
        fprintf(fp, "%-16s %10.4f %10.4f %10.4f %10.4f\n",
                fields[i].label, stats[i].mean, stats[i].min_value,
                stats[i].max_value, stats[i].stddev);
    }

    fprintf(fp, "\n【6x6 相关系数矩阵】\n\n");
    fprintf(fp, "%-12s", "");
    for (int i = 0; i < WQ_FIELD_COUNT; i++) fprintf(fp, " %10s", fields[i].key);
    fprintf(fp, "\n");
    for (int i = 0; i < WQ_FIELD_COUNT; i++) {
        fprintf(fp, "%-12s", fields[i].key);
        for (int j = 0; j < WQ_FIELD_COUNT; j++) fprintf(fp, " %10.4f", corr[i][j]);
        fprintf(fp, "\n");
    }

    /* find strongest correlations */
    double max_pos = -2, max_neg = 2;
    int pi = 0, pj = 0, ni = 0, nj = 0;
    for (int i = 0; i < WQ_FIELD_COUNT; i++) {
        for (int j = 0; j < WQ_FIELD_COUNT; j++) {
            if (i == j) continue;
            if (corr[i][j] > max_pos) { max_pos = corr[i][j]; pi = i; pj = j; }
            if (corr[i][j] < max_neg) { max_neg = corr[i][j]; ni = i; nj = j; }
        }
    }
    fprintf(fp, "\n【相关性结论】\n\n");
    fprintf(fp, "  最强正相关: %s - %s (r = %.4f)\n", fields[pi].label, fields[pj].label, max_pos);
    fprintf(fp, "  最强负相关: %s - %s (r = %.4f)\n", fields[ni].label, fields[nj].label, max_neg);
    fprintf(fp, "\n========================================\n");
    fclose(fp);
    return 1;
}

int data_service_generate_warning_report(const char* path, const WarningSet* warnings) {
    FILE* fp = report_open(path);
    if (fp == NULL) return 0;
    fprintf(fp, "【预警报告】\n\n");
    fprintf(fp, "  预警总数: %d\n\n", warnings->size);
    fprintf(fp, "%-22s %-24s %10s   %s\n", "时间", "类型", "数值", "处理建议");
    fprintf(fp, "------------------------------------------------------------------------\n");
    for (int i = 0; i < warnings->size; i++) {
        const char* type_cn = "未知";
        if (strcmp(warnings->items[i].type, "severe_low_oxygen") == 0) type_cn = "严重缺氧";
        else if (strcmp(warnings->items[i].type, "low_oxygen") == 0) type_cn = "亚缺氧";
        else if (strcmp(warnings->items[i].type, "salinity_drop_1h") == 0) type_cn = "盐度突变(1h)";
        else if (strcmp(warnings->items[i].type, "salinity_drop_24h") == 0) type_cn = "盐度突变(24h)";
        fprintf(fp, "%-22s %-24s %10.4f   %s\n",
                warnings->items[i].time, type_cn, warnings->items[i].value, warnings->items[i].advice);
    }
    fprintf(fp, "\n========================================\n");
    fclose(fp);
    return 1;
}

int data_service_generate_predict_report(const char* path,
    const RegressionResult* primary, const RegressionResult models[4]) {
    FILE* fp = report_open(path);
    if (fp == NULL) return 0;
    const FieldMeta* fields = data_service_fields();
    fprintf(fp, "【预测模型报告】\n\n");
    fprintf(fp, "  目标变量: 溶解氧 (DO)\n\n");
    fprintf(fp, "  主模型 (气温 -> DO):\n");
    fprintf(fp, "    回归方程: DO = %.6f * Air_temp + %.6f\n", primary->slope, primary->intercept);
    fprintf(fp, "    R^2:      %.6f\n", primary->r2);
    fprintf(fp, "    RMSE:     %.6f\n\n", primary->rmse);

    fprintf(fp, "【多因子对比】\n\n");
    fprintf(fp, "%-16s %12s %12s %12s %12s\n", "自变量", "斜率", "截距", "R^2", "RMSE");
    fprintf(fp, "------------------------------------------------------------\n");
    int candidate_fields[] = {0, 1, 2, 5};
    for (int i = 0; i < 4; i++) {
        fprintf(fp, "%-16s %12.6f %12.6f %12.6f %12.6f\n",
                fields[candidate_fields[i]].label,
                models[i].slope, models[i].intercept, models[i].r2, models[i].rmse);
    }
    fprintf(fp, "\n========================================\n");
    fclose(fp);
    return 1;
}

/* ════════════════════════════════════════════════════════════════════
   CLI mode (backward compatible JSON output)
   ════════════════════════════════════════════════════════════════════ */

static void json_string(const char* value) {
    putchar('"');
    if (value != NULL) {
        for (const char* p = value; *p != '\0'; p++) {
            if (*p == '"' || *p == '\\') { putchar('\\'); putchar(*p); }
            else if (*p == '\n') printf("\\n");
            else if (*p == '\r') printf("\\r");
            else putchar(*p);
        }
    }
    putchar('"');
}

static void json_number(double value) {
    if (isnan(value) || isinf(value)) printf("null");
    else printf("%.8f", value);
}

static void print_error_json(const char* message) {
    printf("{\"success\":false,\"message\":");
    json_string(message);
    printf("}\n");
}

static const char* arg_value(int argc, char** argv, const char* key) {
    for (int i = 2; i < argc - 1; i++)
        if (strcmp(argv[i], key) == 0) return argv[i + 1];
    return NULL;
}

static int has_arg(int argc, char** argv, const char* key) {
    for (int i = 2; i < argc; i++)
        if (strcmp(argv[i], key) == 0) return 1;
    return 0;
}

static int require_input_path(int argc, char** argv, const char** input) {
    *input = arg_value(argc, argv, "--input");
    if (*input == NULL || (*input)[0] == '\0') {
        print_error_json("--input is required");
        return 0;
    }
    return 1;
}

static void print_data_object(const Data* d, const RowMark* mark, const OperationMark* op_mark) {
    printf("{\"row\":%d,\"temp\":", d->record_index); json_number(d->temp);
    printf(",\"salinity\":");
    json_number(d->salinity);
    printf(",\"ph\":");
    json_number(d->ph);
    printf(",\"do\":");
    json_number(d->do_value);
    printf(",\"precipitation\":");
    json_number(d->precipitation);
    printf(",\"air_temp\":");
    json_number(d->air_temp);
    if (mark != NULL) {
        printf(",\"mark\":"); json_string(mark->action);
        printf(",\"missing_count\":%d,\"abnormal_count\":%d", mark->missing_count, mark->abnormal_count);
    }
    if (op_mark != NULL) {
        printf(",\"op_status\":"); json_string(op_mark->status);
        printf(",\"modified_fields\":[");
        int first = 1;
        for (int field = 0; field < WQ_FIELD_COUNT; field++) {
            if (op_mark->fields[field]) {
                if (!first) printf(",");
                json_string(FIELDS[field].key);
                first = 0;
            }
        }
        printf("]");
    }
    printf("}");
}

static int cli_login(int argc, char** argv) {
    const char* username = arg_value(argc, argv, "--username");
    const char* password = arg_value(argc, argv, "--password");
    if (username == NULL || password == NULL) { print_error_json("username and password are required"); return 1; }
    User* u = data_service_validate_login(username, password);
    if (u == NULL) { print_error_json("invalid username or password"); return 1; }
    printf("{\"success\":true,\"username\":\"%s\",\"role\":\"%s\"}\n", u->name, u->role == 1 ? "admin" : "guest");
    return 0;
}

static int cli_overview(int argc, char** argv) {
    const char* input = NULL;
    if (!require_input_path(argc, argv, &input)) return 1;
    DataSet set; ReadSummary summary;
    if (!data_service_read_csv(input, &set, &summary)) { print_error_json("failed to read data file"); return 1; }
    int abnormal_records = 0, valid_records = 0;
    for (int i = 0; i < set.size; i++) {
        int abnormal = 0, missing = 0;
        for (int field = 0; field < WQ_FIELD_COUNT; field++) {
            double value = data_service_get_field_value(&set.items[i], field);
            if (isnan(value)) missing = 1;
            else if (!data_service_is_field_in_range(field, value)) abnormal = 1;
        }
        abnormal_records += abnormal;
        valid_records += (!abnormal && !missing);
    }
    printf("{\"success\":true,\"input\":"); json_string(input);
    printf(",\"total_records\":%d,\"parsed_records\":%d,\"valid_records\":%d,"
           "\"abnormal_records\":%d,\"missing_values\":%d,\"format_errors\":%d}\n",
           summary.total_records, summary.parsed_records, valid_records,
           abnormal_records, summary.missing_values, summary.format_errors);
    data_service_dataset_free(&set);
    return 0;
}

static int cli_preprocess(int argc, char** argv) {
    const char* input = NULL, *output = arg_value(argc, argv, "--output"), *marks = arg_value(argc, argv, "--marks");
    if (!require_input_path(argc, argv, &input)) return 1;
    if (output == NULL || marks == NULL) { print_error_json("--output and --marks are required"); return 1; }
    clock_t start = clock();
    DataSet source; ReadSummary summary;
    if (!data_service_read_csv(input, &source, &summary)) { print_error_json("failed to read input data"); return 1; }
    if (!data_service_write_marks(marks, &source)) { data_service_dataset_free(&source); print_error_json("failed to write preprocess marks"); return 1; }
    DataSet clean; PreprocessResult result;
    if (!data_service_preprocess(&source, &clean, &result)) { data_service_dataset_free(&source); print_error_json("failed to preprocess data"); return 1; }
    if (!data_service_write_csv(output, &clean)) { data_service_dataset_free(&source); data_service_dataset_free(&clean); print_error_json("failed to write preprocessed data"); return 1; }
    double seconds = (double)(clock() - start) / CLOCKS_PER_SEC;
    printf("{\"success\":true,\"input\":"); json_string(input);
    printf(",\"output\":"); json_string(output); printf(",\"marks\":"); json_string(marks);
    printf(",\"total_records\":%d,\"kept_records\":%d,\"deleted_records\":%d,"
           "\"abnormal_records\":%d,\"abnormal_values\":%d,\"missing_values\":%d,"
           "\"filled_values\":%d,\"format_errors\":%d,\"processing_seconds\":%.6f}\n",
           result.total_records, result.kept_records, result.deleted_records,
           result.abnormal_records, result.abnormal_values, result.missing_values,
           result.filled_values, summary.format_errors, seconds);
    data_service_dataset_free(&source); data_service_dataset_free(&clean);
    return 0;
}

static int cli_query(int argc, char** argv) {
    const char* input = NULL;
    if (!require_input_path(argc, argv, &input)) return 1;
    DataSet set; ReadSummary summary;
    if (!data_service_read_csv(input, &set, &summary)) { print_error_json("failed to read data file"); return 1; }
    int page = arg_value(argc, argv, "--page") ? atoi(arg_value(argc, argv, "--page")) : 1;
    int page_size = arg_value(argc, argv, "--page-size") ? atoi(arg_value(argc, argv, "--page-size")) : 15;
    if (page < 1) page = 1;
    if (page_size < 1 || page_size > 200) page_size = 15;
    int filter_field = data_service_field_index(arg_value(argc, argv, "--field"));
    int has_min = arg_value(argc, argv, "--min") != NULL, has_max = arg_value(argc, argv, "--max") != NULL;
    double min_value = has_min ? atof(arg_value(argc, argv, "--min")) : -INFINITY;
    double max_value = has_max ? atof(arg_value(argc, argv, "--max")) : INFINITY;
    const char* view = arg_value(argc, argv, "--view");
    const char* operation_filter = arg_value(argc, argv, "--operation-filter");
    const char* raw_marks_path = arg_value(argc, argv, "--raw-marks");
    const char* legacy_marks_path = arg_value(argc, argv, "--marks");
    const char* op_marks_path = arg_value(argc, argv, "--op-marks");

    MarkSet raw_marks; MarkSet* raw_marks_ptr = NULL;
    if ((view == NULL || strcmp(view, "processed") != 0)) {
        const char* path = raw_marks_path ? raw_marks_path : legacy_marks_path;
        if (path != NULL && data_service_read_marks(path, &raw_marks)) raw_marks_ptr = &raw_marks;
    }
    OperationMarkSet op_marks; OperationMarkSet* op_marks_ptr = NULL;
    if ((view != NULL && strcmp(view, "processed") == 0) || op_marks_path != NULL) {
        if (op_marks_path != NULL && data_service_read_operation_marks(op_marks_path, &op_marks))
            op_marks_ptr = &op_marks;
    }

    /* parse sort */
    int sort_field = -1, sort_desc = 0;
    const char* sort_arg = arg_value(argc, argv, "--sort");
    if (sort_arg != NULL && sort_arg[0] != '\0') {
        char buffer[64]; snprintf(buffer, sizeof(buffer), "%s", sort_arg);
        char* suffix = strrchr(buffer, '_');
        if (suffix && (strcmp(suffix, "_desc") == 0 || strcmp(suffix, "_asc") == 0)) {
            sort_desc = strcmp(suffix, "_desc") == 0; *suffix = '\0';
        }
        sort_field = data_service_field_index(buffer);
    }

    QueryPage qp = data_service_query_page(&set, page, page_size,
        filter_field, min_value, max_value, sort_field, sort_desc,
        raw_marks_ptr, op_marks_ptr, view, operation_filter);

    printf("{\"success\":true,\"input\":"); json_string(input);
    printf(",\"page\":%d,\"page_size\":%d,\"total\":%d,\"total_pages\":%d,\"data\":[",
           qp.page, qp.page_size, qp.total, qp.total_pages);
    int count = (qp.page - 1) * qp.page_size;
    for (int i = 0; i < qp.total && i < qp.page_size; i++) {
        if (i > 0) printf(",");
        Data* d = &qp.items[i];
        print_data_object(d,
            data_service_find_mark(raw_marks_ptr, d->record_index),
            data_service_find_operation_mark(op_marks_ptr, d->record_index));
    }
    printf("]}\n");
    free(qp.items);
    if (raw_marks_ptr) { free(raw_marks.items); }
    if (op_marks_ptr) { data_service_operation_mark_set_free(&op_marks); }
    data_service_dataset_free(&set);
    return 0;
}

static int cli_stats(int argc, char** argv) {
    const char* input = NULL;
    if (!require_input_path(argc, argv, &input)) return 1;
    DataSet set; ReadSummary summary;
    if (!data_service_read_csv(input, &set, &summary)) { print_error_json("failed to read data file"); return 1; }
    OperationMarkSet op_marks; OperationMarkSet* op_marks_ptr = NULL;
    const char* op_marks_path = arg_value(argc, argv, "--op-marks");
    if (op_marks_path != NULL && data_service_read_operation_marks(op_marks_path, &op_marks))
        op_marks_ptr = &op_marks;
    DataSet active; DataSet* stats_set = &set; int excluded_deleted = 0;
    if (op_marks_ptr) {
        data_service_dataset_init(&active);
        for (int i = 0; i < set.size; i++) {
            const OperationMark* m = data_service_find_operation_mark(op_marks_ptr, set.items[i].record_index);
            if (m && strcmp(m->status, "deleted") == 0) { excluded_deleted++; continue; }
            data_service_dataset_push(&active, set.items[i]);
        }
        stats_set = &active;
    }
    BasicStats stats[WQ_FIELD_COUNT];
    double corr[WQ_FIELD_COUNT][WQ_FIELD_COUNT];
    data_service_compute_basic_stats(stats_set, stats);
    for (int i = 0; i < WQ_FIELD_COUNT; i++)
        for (int j = 0; j < WQ_FIELD_COUNT; j++)
            corr[i][j] = data_service_pearson(stats_set, i, j);
    printf("{\"success\":true,\"records\":%d,\"excluded_deleted\":%d,\"stats\":[", stats_set->size, excluded_deleted);
    for (int i = 0; i < WQ_FIELD_COUNT; i++) {
        if (i > 0) printf(",");
        printf("{\"field\":\"%s\",\"label\":\"%s\",\"mean\":", FIELDS[i].key, FIELDS[i].label);
        json_number(stats[i].mean); printf(",\"min\":"); json_number(stats[i].min_value);
        printf(",\"max\":"); json_number(stats[i].max_value); printf(",\"stddev\":"); json_number(stats[i].stddev);
        printf("}");
    }
    printf("],\"correlation\":[");
    for (int i = 0; i < WQ_FIELD_COUNT; i++) {
        if (i > 0) printf(","); printf("[");
        for (int j = 0; j < WQ_FIELD_COUNT; j++) { if (j > 0) printf(","); json_number(corr[i][j]); }
        printf("]");
    }
    printf("]}\n");
    if (op_marks_ptr) { data_service_dataset_free(&active); data_service_operation_mark_set_free(&op_marks); }
    data_service_dataset_free(&set);
    return 0;
}

static int cli_warnings(int argc, char** argv) {
    const char* input = NULL;
    if (!require_input_path(argc, argv, &input)) return 1;
    DataSet set; ReadSummary summary;
    if (!data_service_read_csv(input, &set, &summary)) { print_error_json("failed to read data file"); return 1; }
    WarningSet warnings;
    data_service_detect_warnings(&set, &warnings);
    printf("{\"success\":true,\"warnings\":[");
    for (int i = 0; i < warnings.size; i++) {
        if (i > 0) printf(",");
        printf("{\"time\":"); json_string(warnings.items[i].time);
        printf(",\"type\":"); json_string(warnings.items[i].type);
        printf(",\"value\":%.8f,\"advice\":", warnings.items[i].value);
        json_string(warnings.items[i].advice); printf("}");
    }
    printf("],\"count\":%d}\n", warnings.size);
    free(warnings.items);
    data_service_dataset_free(&set);
    return 0;
}

static int cli_predict(int argc, char** argv) {
    const char* input = NULL;
    if (!require_input_path(argc, argv, &input)) return 1;
    DataSet set; ReadSummary summary;
    if (!data_service_read_csv(input, &set, &summary)) { print_error_json("failed to read data file"); return 1; }
    int candidate_fields[] = {0, 1, 2, 5};
    RegressionResult air = data_service_linear_regression(&set, 5);
    printf("{\"success\":true,\"target\":\"do\",\"primary\":{\"x_field\":\"air_temp\",\"slope\":");
    json_number(air.slope); printf(",\"intercept\":"); json_number(air.intercept);
    printf(",\"r2\":"); json_number(air.r2); printf(",\"rmse\":"); json_number(air.rmse);
    printf("},\"models\":[");
    for (int i = 0; i < 4; i++) {
        RegressionResult r = data_service_linear_regression(&set, candidate_fields[i]);
        if (i > 0) printf(",");
        printf("{\"x_field\":\"%s\",\"label\":\"%s\",\"slope\":", FIELDS[candidate_fields[i]].key, FIELDS[candidate_fields[i]].label);
        json_number(r.slope); printf(",\"intercept\":"); json_number(r.intercept);
        printf(",\"r2\":"); json_number(r.r2); printf(",\"rmse\":"); json_number(r.rmse); printf("}");
    }
    printf("]}\n");
    data_service_dataset_free(&set);
    return 0;
}

static int cli_filter(int argc, char** argv) {
    const char* input = NULL, *output = arg_value(argc, argv, "--output"), *window_arg = arg_value(argc, argv, "--window");
    if (!require_input_path(argc, argv, &input)) return 1;
    if (output == NULL || window_arg == NULL) { print_error_json("--output and --window are required"); return 1; }
    int window = atoi(window_arg);
    if (!(window == 3 || window == 5 || window == 7 || window == 9 || window == 11)) { print_error_json("--window must be one of 3, 5, 7, 9, 11"); return 1; }
    DataSet set; ReadSummary summary;
    if (!data_service_read_csv(input, &set, &summary)) { print_error_json("failed to read data file"); return 1; }
    DataSet filtered; BasicStats before[WQ_FIELD_COUNT], after[WQ_FIELD_COUNT];
    if (!data_service_moving_average_filter(&set, window, &filtered, before, after)) { data_service_dataset_free(&set); print_error_json("filter failed"); return 1; }
    if (!data_service_write_csv(output, &filtered)) { data_service_dataset_free(&filtered); data_service_dataset_free(&set); print_error_json("failed to write filtered file"); return 1; }
    printf("{\"success\":true,\"input\":"); json_string(input);
    printf(",\"output\":"); json_string(output); printf(",\"window\":%d,\"stats\":[", window);
    for (int field = 0; field < 4; field++) {
        if (field > 0) printf(",");
        printf("{\"field\":\"%s\",\"label\":\"%s\",\"before_stddev\":", FIELDS[field].key, FIELDS[field].label);
        json_number(before[field].stddev); printf(",\"after_stddev\":"); json_number(after[field].stddev); printf("}");
    }
    printf("]}\n");
    data_service_dataset_free(&filtered); data_service_dataset_free(&set);
    return 0;
}

static int cli_modify(int argc, char** argv) {
    const char* input = NULL, *output = arg_value(argc, argv, "--output");
    if (!require_input_path(argc, argv, &input)) return 1;
    if (output == NULL) output = input;
    int row = arg_value(argc, argv, "--row") ? atoi(arg_value(argc, argv, "--row")) : -1;
    int field = data_service_field_index(arg_value(argc, argv, "--field"));
    const char* value_arg = arg_value(argc, argv, "--value");
    if (row < 1 || field < 0 || value_arg == NULL) { print_error_json("--row, --field, and --value are required"); return 1; }
    double value = atof(value_arg);
    if (!data_service_is_field_in_range(field, value)) { print_error_json("value is outside the allowed range"); return 1; }
    DataSet set; ReadSummary summary;
    if (!data_service_read_csv(input, &set, &summary)) { print_error_json("failed to read data file"); return 1; }
    double old_value = NAN;
    if (!data_service_modify_record(&set, row, field, value, &old_value)) { data_service_dataset_free(&set); print_error_json("row not found"); return 1; }
    if (!data_service_write_csv(output, &set)) { data_service_dataset_free(&set); print_error_json("failed to write output data"); return 1; }
    printf("{\"success\":true,\"row\":%d,\"field\":\"%s\",\"old_value\":", row, FIELDS[field].key);
    json_number(old_value); printf(",\"new_value\":"); json_number(value); printf(",\"output\":"); json_string(output); printf("}\n");
    data_service_dataset_free(&set);
    return 0;
}

static int cli_delete(int argc, char** argv) {
    const char* input = NULL, *output = arg_value(argc, argv, "--output");
    if (!require_input_path(argc, argv, &input)) return 1;
    if (output == NULL) output = input;
    int row = arg_value(argc, argv, "--row") ? atoi(arg_value(argc, argv, "--row")) : -1;
    int field = data_service_field_index(arg_value(argc, argv, "--field"));
    int has_min = arg_value(argc, argv, "--min") != NULL, has_max = arg_value(argc, argv, "--max") != NULL;
    double min_value = has_min ? atof(arg_value(argc, argv, "--min")) : -INFINITY;
    double max_value = has_max ? atof(arg_value(argc, argv, "--max")) : INFINITY;
    if (row < 1 && field < 0) { print_error_json("row or range condition is required"); return 1; }
    DataSet set; ReadSummary summary;
    if (!data_service_read_csv(input, &set, &summary)) { print_error_json("failed to read data file"); return 1; }
    int* deleted_rows = NULL; int deleted_count = 0;
    int ret = data_service_delete_records(&set, row, field, min_value, max_value, &deleted_rows, &deleted_count);
    if (ret < 0) { data_service_dataset_free(&set); print_error_json("memory allocation failed"); return 1; }
    if (ret == 0) { data_service_dataset_free(&set); print_error_json("no rows matched"); return 1; }
    if (!data_service_write_csv(output, &set)) { data_service_dataset_free(&set); free(deleted_rows); print_error_json("failed to write output data"); return 1; }
    printf("{\"success\":true,\"deleted\":%d,\"rows\":[", deleted_count);
    for (int i = 0; i < deleted_count; i++) { if (i > 0) printf(","); printf("%d", deleted_rows[i]); }
    printf("],\"remaining\":%d,\"output\":", set.size); json_string(output); printf("}\n");
    data_service_dataset_free(&set); free(deleted_rows);
    return 0;
}

static int cli_add(int argc, char** argv) {
    const char* input = NULL, *output = arg_value(argc, argv, "--output");
    if (!require_input_path(argc, argv, &input)) return 1;
    if (output == NULL) output = input;
    Data item; memset(&item, 0, sizeof(Data));
    for (int field = 0; field < WQ_FIELD_COUNT; field++) {
        const char* value_arg = arg_value(argc, argv, FIELDS[field].key);
        if (value_arg == NULL) { char msg[96]; snprintf(msg, sizeof(msg), "missing value for %s", FIELDS[field].key); print_error_json(msg); return 1; }
        double value = atof(value_arg);
        if (!data_service_is_field_in_range(field, value)) { char msg[128]; snprintf(msg, sizeof(msg), "value for %s is outside the allowed range", FIELDS[field].key); print_error_json(msg); return 1; }
        data_service_set_field_value(&item, field, value);
    }
    DataSet set; ReadSummary summary;
    if (!data_service_read_csv(input, &set, &summary)) { print_error_json("failed to read data file"); return 1; }
    int new_row = data_service_add_record(&set, item);
    if (new_row == 0) { data_service_dataset_free(&set); print_error_json("failed to append data"); return 1; }
    if (!data_service_write_csv(output, &set)) { data_service_dataset_free(&set); print_error_json("failed to write output data"); return 1; }
    printf("{\"success\":true,\"row\":%d,\"output\":", new_row); json_string(output);
    printf(",\"record\":"); print_data_object(&set.items[set.size - 1], NULL, NULL); printf("}\n");
    data_service_dataset_free(&set);
    return 0;
}

int data_service_run_cli(int argc, char** argv) {
    if (argc < 2) { print_error_json("command is required"); return 1; }
    const char* command = argv[1];
    if (strcmp(command, "login") == 0) return cli_login(argc, argv);
    if (strcmp(command, "overview") == 0) return cli_overview(argc, argv);
    if (strcmp(command, "preprocess") == 0) return cli_preprocess(argc, argv);
    if (strcmp(command, "query") == 0) return cli_query(argc, argv);
    if (strcmp(command, "stats") == 0 || strcmp(command, "stat-report") == 0) return cli_stats(argc, argv);
    if (strcmp(command, "warnings") == 0) return cli_warnings(argc, argv);
    if (strcmp(command, "predict") == 0) return cli_predict(argc, argv);
    if (strcmp(command, "filter") == 0) return cli_filter(argc, argv);
    if (strcmp(command, "modify") == 0) return cli_modify(argc, argv);
    if (strcmp(command, "delete") == 0) return cli_delete(argc, argv);
    if (strcmp(command, "add") == 0) return cli_add(argc, argv);
    if (has_arg(argc, argv, "--help")) {
        printf("{\"success\":true,\"commands\":[\"login\",\"overview\",\"preprocess\","
               "\"query\",\"stats\",\"warnings\",\"predict\",\"filter\",\"modify\",\"delete\",\"add\"]}\n");
        return 0;
    }
    print_error_json("unknown command");
    return 1;
}

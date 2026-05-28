#include "DataService.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define LINE_BUFFER_SIZE 1024

typedef struct {
    int total_records;
    int parsed_records;
    int missing_values;
    int format_errors;
} ReadSummary;

typedef struct {
    int total_records;
    int kept_records;
    int deleted_records;
    int abnormal_records;
    int abnormal_values;
    int filled_values;
    int missing_values;
} PreprocessResult;

typedef struct {
    int count;
    double mean;
    double min_value;
    double max_value;
    double stddev;
} BasicStats;

typedef struct {
    int count;
    double slope;
    double intercept;
    double r2;
    double rmse;
} RegressionResult;

typedef struct {
    int row;
    char action[24];
    int missing_count;
    int abnormal_count;
} RowMark;

typedef struct {
    RowMark* items;
    int size;
    int capacity;
} MarkSet;

typedef struct {
    int row;
    char status[24];
    int fields[WQ_FIELD_COUNT];
} OperationMark;

typedef struct {
    OperationMark* items;
    int size;
    int capacity;
} OperationMarkSet;

static const FieldMeta FIELDS[WQ_FIELD_COUNT] = {
    {"temp", "Temp", "degC", -5.0, 40.0},
    {"salinity", "Salinity", "PSU", 0.0, 45.0},
    {"ph", "pH", "", 6.5, 9.0},
    {"do", "DO", "mg/l", 0.0, 15.0},
    {"precipitation", "Precipitation", "mm", 0.0, 500.0},
    {"air_temp", "Air temp", "degC", -10.0, 50.0}
};

static int g_sort_field = 0;
static int g_sort_desc = 0;

const FieldMeta* data_service_fields(void) {
    return FIELDS;
}

static int equals_ignore_case(const char* a, const char* b) {
    if (a == NULL || b == NULL) {
        return 0;
    }
    while (*a != '\0' && *b != '\0') {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
            return 0;
        }
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

int data_service_field_index(const char* field) {
    if (field == NULL) {
        return -1;
    }
    for (int i = 0; i < WQ_FIELD_COUNT; i++) {
        if (equals_ignore_case(field, FIELDS[i].key) ||
            equals_ignore_case(field, FIELDS[i].label)) {
            return i;
        }
    }
    if (equals_ignore_case(field, "do_value")) {
        return 3;
    }
    return -1;
}

static void dataset_init(DataSet* set) {
    set->items = NULL;
    set->size = 0;
    set->capacity = 0;
}

static void dataset_free(DataSet* set) {
    if (set == NULL) {
        return;
    }
    free(set->items);
    set->items = NULL;
    set->size = 0;
    set->capacity = 0;
}

static int dataset_reserve(DataSet* set, int capacity) {
    if (capacity <= set->capacity) {
        return 1;
    }
    Data* next = (Data*)realloc(set->items, sizeof(Data) * capacity);
    if (next == NULL) {
        return 0;
    }
    set->items = next;
    set->capacity = capacity;
    return 1;
}

static int dataset_push(DataSet* set, Data item) {
    if (set->size == set->capacity) {
        int next_capacity = set->capacity == 0 ? 1000 : set->capacity * 2;
        if (!dataset_reserve(set, next_capacity)) {
            return 0;
        }
    }
    set->items[set->size++] = item;
    return 1;
}

static void mark_set_init(MarkSet* set) {
    set->items = NULL;
    set->size = 0;
    set->capacity = 0;
}

static void mark_set_free(MarkSet* set) {
    free(set->items);
    set->items = NULL;
    set->size = 0;
    set->capacity = 0;
}

static int mark_set_push(MarkSet* set, RowMark item) {
    if (set->size == set->capacity) {
        int next_capacity = set->capacity == 0 ? 1000 : set->capacity * 2;
        RowMark* next = (RowMark*)realloc(set->items, sizeof(RowMark) * next_capacity);
        if (next == NULL) {
            return 0;
        }
        set->items = next;
        set->capacity = next_capacity;
    }
    set->items[set->size++] = item;
    return 1;
}

static void operation_mark_set_init(OperationMarkSet* set) {
    set->items = NULL;
    set->size = 0;
    set->capacity = 0;
}

static void operation_mark_set_free(OperationMarkSet* set) {
    free(set->items);
    set->items = NULL;
    set->size = 0;
    set->capacity = 0;
}

static int operation_mark_set_push(OperationMarkSet* set, OperationMark item) {
    if (set->size == set->capacity) {
        int next_capacity = set->capacity == 0 ? 1000 : set->capacity * 2;
        OperationMark* next = (OperationMark*)realloc(set->items, sizeof(OperationMark) * next_capacity);
        if (next == NULL) {
            return 0;
        }
        set->items = next;
        set->capacity = next_capacity;
    }
    set->items[set->size++] = item;
    return 1;
}

static double get_field_value(const Data* data, int field) {
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

static void set_field_value(Data* data, int field, double value) {
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

static void trim_in_place(char* text) {
    char* start = text;
    while (isspace((unsigned char)*start)) {
        start++;
    }
    if (start != text) {
        memmove(text, start, strlen(start) + 1);
    }
    size_t len = strlen(text);
    while (len > 0 && isspace((unsigned char)text[len - 1])) {
        text[len - 1] = '\0';
        len--;
    }
}

static int parse_field_token(const char* token, double* value, int* missing) {
    char buffer[128];
    size_t len = strlen(token);
    if (len >= sizeof(buffer)) {
        return 0;
    }
    strcpy(buffer, token);
    trim_in_place(buffer);
    if (buffer[0] == '\0' ||
        equals_ignore_case(buffer, "nan") ||
        strcmp(buffer, "-999") == 0 ||
        strcmp(buffer, "-9999") == 0) {
        *value = NAN;
        (*missing)++;
        return 1;
    }

    errno = 0;
    char* end = NULL;
    double parsed = strtod(buffer, &end);
    while (end != NULL && isspace((unsigned char)*end)) {
        end++;
    }
    if (errno != 0 || end == buffer || (end != NULL && *end != '\0')) {
        return 0;
    }
    *value = parsed;
    return 1;
}

static int parse_csv_line(const char* line, Data* data, int* missing) {
    const char* cursor = line;
    for (int field = 0; field < WQ_FIELD_COUNT; field++) {
        char token[128];
        int len = 0;
        while (*cursor != '\0' && *cursor != '\n' && *cursor != '\r' && *cursor != ',') {
            if (len < (int)sizeof(token) - 1) {
                token[len++] = *cursor;
            }
            cursor++;
        }
        token[len] = '\0';
        double value = NAN;
        if (!parse_field_token(token, &value, missing)) {
            return 0;
        }
        set_field_value(data, field, value);
        if (*cursor == ',') {
            cursor++;
        } else if (field < WQ_FIELD_COUNT - 1) {
            return 0;
        }
    }
    return 1;
}

static int read_dataset_csv(const char* path, DataSet* set, ReadSummary* summary) {
    FILE* fp = fopen(path, "r");
    char line[LINE_BUFFER_SIZE];
    dataset_init(set);
    if (summary != NULL) {
        memset(summary, 0, sizeof(ReadSummary));
    }
    if (fp == NULL) {
        return 0;
    }
    if (fgets(line, sizeof(line), fp) == NULL) {
        fclose(fp);
        return 1;
    }

    int record_index = 1;
    while (fgets(line, sizeof(line), fp) != NULL) {
        if (summary != NULL) {
            summary->total_records++;
        }
        Data data;
        memset(&data, 0, sizeof(Data));
        data.record_index = record_index++;
        int missing = 0;
        if (!parse_csv_line(line, &data, &missing)) {
            if (summary != NULL) {
                summary->format_errors++;
            }
            continue;
        }
        if (!dataset_push(set, data)) {
            dataset_free(set);
            fclose(fp);
            return 0;
        }
        if (summary != NULL) {
            summary->parsed_records++;
            summary->missing_values += missing;
        }
    }
    fclose(fp);
    return 1;
}

static int write_dataset_csv(const char* path, const DataSet* set) {
    FILE* fp = fopen(path, "w");
    if (fp == NULL) {
        return 0;
    }
    fprintf(fp, "Temp(degC),Salinity(PSU),pH,DO(mg/l),precipitation(mm),Air_temp(degC)\n");
    for (int i = 0; i < set->size; i++) {
        const Data* d = &set->items[i];
        fprintf(fp, "%.8f,%.8f,%.8f,%.8f,%.8f,%.8f\n",
                d->temp, d->salinity, d->ph, d->do_value, d->precipitation, d->air_temp);
    }
    fclose(fp);
    return 1;
}

static int is_field_in_range(int field, double value) {
    return !isnan(value) && value >= FIELDS[field].min_value && value <= FIELDS[field].max_value;
}

static void json_string(const char* value) {
    putchar('"');
    if (value != NULL) {
        for (const char* p = value; *p != '\0'; p++) {
            if (*p == '"' || *p == '\\') {
                putchar('\\');
                putchar(*p);
            } else if (*p == '\n') {
                printf("\\n");
            } else if (*p == '\r') {
                printf("\\r");
            } else {
                putchar(*p);
            }
        }
    }
    putchar('"');
}

static void json_number(double value) {
    if (isnan(value) || isinf(value)) {
        printf("null");
    } else {
        printf("%.8f", value);
    }
}

static void print_error_json(const char* message) {
    printf("{\"success\":false,\"message\":");
    json_string(message);
    printf("}\n");
}

static const char* arg_value(int argc, char** argv, const char* key) {
    for (int i = 2; i < argc - 1; i++) {
        if (strcmp(argv[i], key) == 0) {
            return argv[i + 1];
        }
    }
    return NULL;
}

static int has_arg(int argc, char** argv, const char* key) {
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], key) == 0) {
            return 1;
        }
    }
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

static double approximate_value(const DataSet* set, int index, int field, double global_mean) {
    double sum = 0.0;
    int count = 0;
    for (int step = 1; step <= 10; step++) {
        int prev = index - step;
        int next = index + step;
        if (prev >= 0) {
            double value = get_field_value(&set->items[prev], field);
            if (!isnan(value)) {
                sum += value;
                count++;
            }
        }
        if (next < set->size) {
            double value = get_field_value(&set->items[next], field);
            if (!isnan(value)) {
                sum += value;
                count++;
            }
        }
    }
    return count > 0 ? sum / count : global_mean;
}

static int write_marks_csv(const char* path, const DataSet* source) {
    FILE* fp = fopen(path, "w");
    if (fp == NULL) {
        return 0;
    }
    fprintf(fp, "row,action,missing_count,abnormal_count\n");
    for (int i = 0; i < source->size; i++) {
        int missing_count = 0;
        int abnormal_count = 0;
        for (int field = 0; field < WQ_FIELD_COUNT; field++) {
            double value = get_field_value(&source->items[i], field);
            if (isnan(value)) {
                missing_count++;
            } else if (!is_field_in_range(field, value)) {
                abnormal_count++;
            }
        }
        const char* action = "none";
        if (abnormal_count >= 3) {
            action = "delete";
        } else if (missing_count > 0 && abnormal_count > 0) {
            action = "repair_fill";
        } else if (abnormal_count > 0) {
            action = "repair";
        } else if (missing_count > 0) {
            action = "fill";
        }
        fprintf(fp, "%d,%s,%d,%d\n", source->items[i].record_index, action, missing_count, abnormal_count);
    }
    fclose(fp);
    return 1;
}

static int read_marks_csv(const char* path, MarkSet* marks) {
    FILE* fp = fopen(path, "r");
    char line[256];
    mark_set_init(marks);
    if (fp == NULL) {
        return 0;
    }
    fgets(line, sizeof(line), fp);
    while (fgets(line, sizeof(line), fp) != NULL) {
        RowMark mark;
        memset(&mark, 0, sizeof(RowMark));
        int ret = sscanf(line, "%d,%23[^,],%d,%d",
                         &mark.row, mark.action, &mark.missing_count, &mark.abnormal_count);
        if (ret >= 2) {
            mark_set_push(marks, mark);
        }
    }
    fclose(fp);
    return 1;
}

static const RowMark* find_mark(const MarkSet* marks, int row) {
    if (marks == NULL) {
        return NULL;
    }
    for (int i = 0; i < marks->size; i++) {
        if (marks->items[i].row == row) {
            return &marks->items[i];
        }
    }
    return NULL;
}

static char* read_text_file(const char* path) {
    FILE* fp = fopen(path, "rb");
    if (fp == NULL) {
        return NULL;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }
    long size = ftell(fp);
    if (size < 0) {
        fclose(fp);
        return NULL;
    }
    rewind(fp);
    char* text = (char*)malloc((size_t)size + 1);
    if (text == NULL) {
        fclose(fp);
        return NULL;
    }
    size_t read_size = fread(text, 1, (size_t)size, fp);
    text[read_size] = '\0';
    fclose(fp);
    return text;
}

static int parse_json_string_value(const char* object_start, const char* key, char* out, int out_size) {
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char* key_pos = strstr(object_start, pattern);
    if (key_pos == NULL) {
        return 0;
    }
    const char* colon = strchr(key_pos, ':');
    if (colon == NULL) {
        return 0;
    }
    const char* quote = strchr(colon, '"');
    if (quote == NULL) {
        return 0;
    }
    quote++;
    int len = 0;
    while (*quote != '\0' && *quote != '"' && len < out_size - 1) {
        out[len++] = *quote++;
    }
    out[len] = '\0';
    return len > 0;
}

static void parse_json_fields(const char* object_start, OperationMark* mark) {
    const char* fields_pos = strstr(object_start, "\"fields\"");
    if (fields_pos == NULL) {
        return;
    }
    const char* start = strchr(fields_pos, '[');
    const char* end = strchr(fields_pos, ']');
    if (start == NULL || end == NULL || end <= start) {
        return;
    }
    for (int field = 0; field < WQ_FIELD_COUNT; field++) {
        char pattern[32];
        snprintf(pattern, sizeof(pattern), "\"%s\"", FIELDS[field].key);
        const char* found = strstr(start, pattern);
        if (found != NULL && found < end) {
            mark->fields[field] = 1;
        }
    }
}

static int read_operation_marks_json(const char* path, OperationMarkSet* marks) {
    operation_mark_set_init(marks);
    char* text = read_text_file(path);
    if (text == NULL) {
        return 0;
    }
    char* rows = strstr(text, "\"rows\"");
    if (rows == NULL) {
        free(text);
        return 1;
    }
    char* cursor = strchr(rows, '{');
    if (cursor == NULL) {
        free(text);
        return 1;
    }
    while ((cursor = strchr(cursor, '"')) != NULL) {
        char* end_quote = strchr(cursor + 1, '"');
        if (end_quote == NULL) {
            break;
        }
        char row_text[32];
        int len = (int)(end_quote - cursor - 1);
        if (len <= 0 || len >= (int)sizeof(row_text)) {
            cursor = end_quote + 1;
            continue;
        }
        memcpy(row_text, cursor + 1, (size_t)len);
        row_text[len] = '\0';
        int all_digits = 1;
        for (int i = 0; row_text[i] != '\0'; i++) {
            if (!isdigit((unsigned char)row_text[i])) {
                all_digits = 0;
                break;
            }
        }
        if (!all_digits) {
            cursor = end_quote + 1;
            continue;
        }
        char* object_start = strchr(end_quote, '{');
        char* object_end = strchr(end_quote, '}');
        if (object_start == NULL || object_end == NULL) {
            break;
        }
        OperationMark mark;
        memset(&mark, 0, sizeof(OperationMark));
        mark.row = atoi(row_text);
        if (!parse_json_string_value(object_start, "status", mark.status, sizeof(mark.status))) {
            strcpy(mark.status, "none");
        }
        parse_json_fields(object_start, &mark);
        operation_mark_set_push(marks, mark);
        cursor = object_end + 1;
    }
    free(text);
    return 1;
}

static const OperationMark* find_operation_mark(const OperationMarkSet* marks, int row) {
    if (marks == NULL) {
        return NULL;
    }
    for (int i = 0; i < marks->size; i++) {
        if (marks->items[i].row == row) {
            return &marks->items[i];
        }
    }
    return NULL;
}

static int preprocess_dataset(const DataSet* source, DataSet* clean, PreprocessResult* result) {
    dataset_init(clean);
    memset(result, 0, sizeof(PreprocessResult));
    result->total_records = source->size;

    for (int i = 0; i < source->size; i++) {
        Data item = source->items[i];
        int abnormal_fields = 0;
        int missing_fields = 0;
        for (int field = 0; field < WQ_FIELD_COUNT; field++) {
            double value = get_field_value(&item, field);
            if (isnan(value)) {
                missing_fields++;
                result->missing_values++;
            } else if (!is_field_in_range(field, value)) {
                abnormal_fields++;
                result->abnormal_values++;
            }
        }
        if (abnormal_fields > 0) {
            result->abnormal_records++;
        }
        if (abnormal_fields >= 3) {
            result->deleted_records++;
            continue;
        }
        for (int field = 0; field < WQ_FIELD_COUNT; field++) {
            double value = get_field_value(&item, field);
            if (!isnan(value) && !is_field_in_range(field, value)) {
                set_field_value(&item, field, NAN);
            }
        }
        if (!dataset_push(clean, item)) {
            dataset_free(clean);
            return 0;
        }
        (void)missing_fields;
    }

    double global_means[WQ_FIELD_COUNT] = {0};
    int global_counts[WQ_FIELD_COUNT] = {0};
    for (int field = 0; field < WQ_FIELD_COUNT; field++) {
        for (int i = 0; i < clean->size; i++) {
            double value = get_field_value(&clean->items[i], field);
            if (!isnan(value)) {
                global_means[field] += value;
                global_counts[field]++;
            }
        }
        global_means[field] = global_counts[field] == 0 ? 0.0 : global_means[field] / global_counts[field];
    }

    for (int i = 0; i < clean->size; i++) {
        for (int field = 0; field < WQ_FIELD_COUNT; field++) {
            if (isnan(get_field_value(&clean->items[i], field))) {
                double fill = approximate_value(clean, i, field, global_means[field]);
                set_field_value(&clean->items[i], field, fill);
                result->filled_values++;
            }
        }
    }
    result->kept_records = clean->size;
    return 1;
}

static void compute_basic_stats(const DataSet* set, BasicStats stats[WQ_FIELD_COUNT]) {
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
            double value = get_field_value(&set->items[i], field);
            if (isnan(value)) {
                continue;
            }
            stats[field].count++;
            sum += value;
            if (value < stats[field].min_value) {
                stats[field].min_value = value;
            }
            if (value > stats[field].max_value) {
                stats[field].max_value = value;
            }
        }
        if (stats[field].count > 0) {
            stats[field].mean = sum / stats[field].count;
        } else {
            stats[field].min_value = NAN;
            stats[field].max_value = NAN;
        }
    }
    for (int field = 0; field < WQ_FIELD_COUNT; field++) {
        if (stats[field].count == 0) {
            continue;
        }
        double ss = 0.0;
        for (int i = 0; i < set->size; i++) {
            double value = get_field_value(&set->items[i], field);
            if (!isnan(value)) {
                double delta = value - stats[field].mean;
                ss += delta * delta;
            }
        }
        stats[field].stddev = sqrt(ss / stats[field].count);
    }
}

static double pearson(const DataSet* set, int x_field, int y_field) {
    int count = 0;
    double sum_x = 0.0;
    double sum_y = 0.0;
    double sum_x2 = 0.0;
    double sum_y2 = 0.0;
    double sum_xy = 0.0;
    for (int i = 0; i < set->size; i++) {
        double x = get_field_value(&set->items[i], x_field);
        double y = get_field_value(&set->items[i], y_field);
        if (isnan(x) || isnan(y)) {
            continue;
        }
        count++;
        sum_x += x;
        sum_y += y;
        sum_x2 += x * x;
        sum_y2 += y * y;
        sum_xy += x * y;
    }
    if (count < 2) {
        return NAN;
    }
    double numerator = count * sum_xy - sum_x * sum_y;
    double denominator = sqrt((count * sum_x2 - sum_x * sum_x) *
                              (count * sum_y2 - sum_y * sum_y));
    return denominator == 0.0 ? NAN : numerator / denominator;
}

static RegressionResult linear_regression_for_field(const DataSet* set, int x_field) {
    RegressionResult result;
    memset(&result, 0, sizeof(result));
    int count = 0;
    double sum_x = 0.0;
    double sum_y = 0.0;
    double sum_x2 = 0.0;
    double sum_xy = 0.0;
    for (int i = 0; i < set->size; i++) {
        double x = get_field_value(&set->items[i], x_field);
        double y = get_field_value(&set->items[i], 3);
        if (isnan(x) || isnan(y)) {
            continue;
        }
        count++;
        sum_x += x;
        sum_y += y;
        sum_x2 += x * x;
        sum_xy += x * y;
    }
    result.count = count;
    if (count < 2) {
        result.slope = result.intercept = result.r2 = result.rmse = NAN;
        return result;
    }
    double denominator = count * sum_x2 - sum_x * sum_x;
    if (denominator == 0.0) {
        result.slope = result.intercept = result.r2 = result.rmse = NAN;
        return result;
    }
    result.slope = (count * sum_xy - sum_x * sum_y) / denominator;
    result.intercept = (sum_y - result.slope * sum_x) / count;
    double mean_y = sum_y / count;
    double ss_res = 0.0;
    double ss_tot = 0.0;
    for (int i = 0; i < set->size; i++) {
        double x = get_field_value(&set->items[i], x_field);
        double y = get_field_value(&set->items[i], 3);
        if (isnan(x) || isnan(y)) {
            continue;
        }
        double predicted = result.slope * x + result.intercept;
        ss_res += (y - predicted) * (y - predicted);
        ss_tot += (y - mean_y) * (y - mean_y);
    }
    result.r2 = ss_tot == 0.0 ? NAN : 1.0 - ss_res / ss_tot;

    int train_target = (int)(count * 0.8);
    if (train_target < 2 || count - train_target < 1) {
        result.rmse = NAN;
        return result;
    }
    int train_count = 0;
    double tx = 0.0, ty = 0.0, tx2 = 0.0, txy = 0.0;
    for (int i = 0; i < set->size && train_count < train_target; i++) {
        double x = get_field_value(&set->items[i], x_field);
        double y = get_field_value(&set->items[i], 3);
        if (isnan(x) || isnan(y)) {
            continue;
        }
        train_count++;
        tx += x;
        ty += y;
        tx2 += x * x;
        txy += x * y;
    }
    double train_den = train_count * tx2 - tx * tx;
    if (train_den == 0.0) {
        result.rmse = NAN;
        return result;
    }
    double train_slope = (train_count * txy - tx * ty) / train_den;
    double train_intercept = (ty - train_slope * tx) / train_count;
    int valid_index = 0;
    int test_count = 0;
    double mse = 0.0;
    for (int i = 0; i < set->size; i++) {
        double x = get_field_value(&set->items[i], x_field);
        double y = get_field_value(&set->items[i], 3);
        if (isnan(x) || isnan(y)) {
            continue;
        }
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

static int compare_data_for_sort(const void* a, const void* b) {
    const Data* left = (const Data*)a;
    const Data* right = (const Data*)b;
    double lv = get_field_value(left, g_sort_field);
    double rv = get_field_value(right, g_sort_field);
    int result = 0;
    if (isnan(lv) && isnan(rv)) {
        result = 0;
    } else if (isnan(lv)) {
        return 1;
    } else if (isnan(rv)) {
        return -1;
    } else if (lv < rv) {
        result = -1;
    } else if (lv > rv) {
        result = 1;
    }
    return g_sort_desc ? -result : result;
}

static int parse_sort(const char* sort) {
    if (sort == NULL || sort[0] == '\0') {
        return 0;
    }
    char buffer[64];
    snprintf(buffer, sizeof(buffer), "%s", sort);
    char* suffix = strrchr(buffer, '_');
    if (suffix != NULL && (strcmp(suffix, "_desc") == 0 || strcmp(suffix, "_asc") == 0)) {
        g_sort_desc = strcmp(suffix, "_desc") == 0;
        *suffix = '\0';
    } else {
        g_sort_desc = 0;
    }
    int field = data_service_field_index(buffer);
    if (field < 0) {
        return 0;
    }
    g_sort_field = field;
    return 1;
}

static void print_data_object(const Data* d, const RowMark* mark, const OperationMark* op_mark) {
    printf("{\"row\":%d,\"temp\":", d->record_index);
    json_number(d->temp);
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
        printf(",\"mark\":");
        json_string(mark->action);
        printf(",\"missing_count\":%d,\"abnormal_count\":%d", mark->missing_count, mark->abnormal_count);
    }
    if (op_mark != NULL) {
        printf(",\"op_status\":");
        json_string(op_mark->status);
        printf(",\"modified_fields\":[");
        int first = 1;
        for (int field = 0; field < WQ_FIELD_COUNT; field++) {
            if (op_mark->fields[field]) {
                if (!first) {
                    printf(",");
                }
                json_string(FIELDS[field].key);
                first = 0;
            }
        }
        printf("]");
    }
    printf("}");
}

static int run_login(int argc, char** argv) {
    const char* username = arg_value(argc, argv, "--username");
    const char* password = arg_value(argc, argv, "--password");
    if (username == NULL || password == NULL) {
        print_error_json("username and password are required");
        return 1;
    }
    if (strcmp(username, "admin") == 0 && strcmp(password, "123456") == 0) {
        printf("{\"success\":true,\"username\":\"admin\",\"role\":\"admin\"}\n");
        return 0;
    }
    if (strcmp(username, "guest") == 0 && strcmp(password, "guest") == 0) {
        printf("{\"success\":true,\"username\":\"guest\",\"role\":\"guest\"}\n");
        return 0;
    }
    print_error_json("invalid username or password");
    return 1;
}

static int run_overview(int argc, char** argv) {
    const char* input = NULL;
    if (!require_input_path(argc, argv, &input)) {
        return 1;
    }
    DataSet set;
    ReadSummary summary;
    if (!read_dataset_csv(input, &set, &summary)) {
        print_error_json("failed to read data file");
        return 1;
    }
    int abnormal_records = 0;
    int valid_records = 0;
    for (int i = 0; i < set.size; i++) {
        int abnormal = 0;
        int missing = 0;
        for (int field = 0; field < WQ_FIELD_COUNT; field++) {
            double value = get_field_value(&set.items[i], field);
            if (isnan(value)) {
                missing = 1;
            } else if (!is_field_in_range(field, value)) {
                abnormal = 1;
            }
        }
        abnormal_records += abnormal;
        valid_records += (!abnormal && !missing);
    }
    printf("{\"success\":true,\"input\":");
    json_string(input);
    printf(",\"total_records\":%d,\"parsed_records\":%d,\"valid_records\":%d,"
           "\"abnormal_records\":%d,\"missing_values\":%d,\"format_errors\":%d}\n",
           summary.total_records, summary.parsed_records, valid_records,
           abnormal_records, summary.missing_values, summary.format_errors);
    dataset_free(&set);
    return 0;
}

static int run_preprocess(int argc, char** argv) {
    const char* input = NULL;
    const char* output = arg_value(argc, argv, "--output");
    const char* marks = arg_value(argc, argv, "--marks");
    if (!require_input_path(argc, argv, &input)) {
        return 1;
    }
    if (output == NULL || marks == NULL) {
        print_error_json("--output and --marks are required");
        return 1;
    }

    clock_t start = clock();
    DataSet source;
    ReadSummary summary;
    if (!read_dataset_csv(input, &source, &summary)) {
        print_error_json("failed to read input data");
        return 1;
    }
    if (!write_marks_csv(marks, &source)) {
        dataset_free(&source);
        print_error_json("failed to write preprocess marks");
        return 1;
    }
    DataSet clean;
    PreprocessResult result;
    if (!preprocess_dataset(&source, &clean, &result)) {
        dataset_free(&source);
        print_error_json("failed to preprocess data");
        return 1;
    }
    if (!write_dataset_csv(output, &clean)) {
        dataset_free(&source);
        dataset_free(&clean);
        print_error_json("failed to write preprocessed data");
        return 1;
    }
    double seconds = (double)(clock() - start) / CLOCKS_PER_SEC;
    printf("{\"success\":true,\"input\":");
    json_string(input);
    printf(",\"output\":");
    json_string(output);
    printf(",\"marks\":");
    json_string(marks);
    printf(",\"total_records\":%d,\"kept_records\":%d,\"deleted_records\":%d,"
           "\"abnormal_records\":%d,\"abnormal_values\":%d,\"missing_values\":%d,"
           "\"filled_values\":%d,\"format_errors\":%d,\"processing_seconds\":%.6f}\n",
           result.total_records, result.kept_records, result.deleted_records,
           result.abnormal_records, result.abnormal_values, result.missing_values,
           result.filled_values, summary.format_errors, seconds);
    dataset_free(&source);
    dataset_free(&clean);
    return 0;
}

static int raw_mark_matches_filter(const RowMark* mark, const char* filter) {
    const char* action = mark == NULL ? "none" : mark->action;
    if (filter == NULL || strcmp(filter, "all") == 0 || filter[0] == '\0') {
        return 1;
    }
    return strcmp(action, filter) == 0;
}

static int op_mark_matches_filter(const OperationMark* mark, const char* filter) {
    const char* status = mark == NULL ? "none" : mark->status;
    if (filter == NULL || strcmp(filter, "all") == 0 || filter[0] == '\0') {
        return 1;
    }
    return strcmp(status, filter) == 0;
}

static int copy_without_deleted(const DataSet* source, const OperationMarkSet* marks, DataSet* target) {
    dataset_init(target);
    dataset_reserve(target, source->size);
    int excluded = 0;
    for (int i = 0; i < source->size; i++) {
        const OperationMark* mark = find_operation_mark(marks, source->items[i].record_index);
        if (mark != NULL && strcmp(mark->status, "deleted") == 0) {
            excluded++;
            continue;
        }
        dataset_push(target, source->items[i]);
    }
    return excluded;
}

static int run_query(int argc, char** argv) {
    const char* input = NULL;
    if (!require_input_path(argc, argv, &input)) {
        return 1;
    }
    DataSet set;
    ReadSummary summary;
    if (!read_dataset_csv(input, &set, &summary)) {
        print_error_json("failed to read data file");
        return 1;
    }
    int page = arg_value(argc, argv, "--page") ? atoi(arg_value(argc, argv, "--page")) : 1;
    int page_size = arg_value(argc, argv, "--page-size") ? atoi(arg_value(argc, argv, "--page-size")) : 15;
    if (page < 1) {
        page = 1;
    }
    if (page_size < 1 || page_size > 200) {
        page_size = 15;
    }
    int filter_field = data_service_field_index(arg_value(argc, argv, "--field"));
    int has_min = arg_value(argc, argv, "--min") != NULL;
    int has_max = arg_value(argc, argv, "--max") != NULL;
    double min_value = has_min ? atof(arg_value(argc, argv, "--min")) : 0.0;
    double max_value = has_max ? atof(arg_value(argc, argv, "--max")) : 0.0;
    const char* view = arg_value(argc, argv, "--view");
    const char* operation_filter = arg_value(argc, argv, "--operation-filter");
    const char* raw_marks_path = arg_value(argc, argv, "--raw-marks");
    const char* legacy_marks_path = arg_value(argc, argv, "--marks");
    const char* op_marks_path = arg_value(argc, argv, "--op-marks");

    MarkSet raw_marks;
    MarkSet* raw_marks_ptr = NULL;
    if ((view == NULL || strcmp(view, "processed") != 0)) {
        const char* path = raw_marks_path != NULL ? raw_marks_path : legacy_marks_path;
        if (path != NULL && read_marks_csv(path, &raw_marks)) {
            raw_marks_ptr = &raw_marks;
        }
    }

    OperationMarkSet op_marks;
    OperationMarkSet* op_marks_ptr = NULL;
    if ((view != NULL && strcmp(view, "processed") == 0) || op_marks_path != NULL) {
        if (op_marks_path != NULL && read_operation_marks_json(op_marks_path, &op_marks)) {
            op_marks_ptr = &op_marks;
        }
    }

    DataSet filtered;
    dataset_init(&filtered);
    dataset_reserve(&filtered, set.size);
    for (int i = 0; i < set.size; i++) {
        int keep = 1;
        const RowMark* raw_mark = find_mark(raw_marks_ptr, set.items[i].record_index);
        const OperationMark* op_mark = find_operation_mark(op_marks_ptr, set.items[i].record_index);
        if (filter_field >= 0) {
            double value = get_field_value(&set.items[i], filter_field);
            if (isnan(value) ||
                (has_min && value < min_value) ||
                (has_max && value > max_value)) {
                keep = 0;
            }
        }
        if (keep && view != NULL && strcmp(view, "processed") == 0) {
            keep = op_mark_matches_filter(op_mark, operation_filter);
        } else if (keep) {
            keep = raw_mark_matches_filter(raw_mark, operation_filter);
        }
        if (keep) {
            dataset_push(&filtered, set.items[i]);
        }
    }
    if (parse_sort(arg_value(argc, argv, "--sort"))) {
        qsort(filtered.items, (size_t)filtered.size, sizeof(Data), compare_data_for_sort);
    }

    int total_pages = filtered.size == 0 ? 1 : (filtered.size + page_size - 1) / page_size;
    if (page > total_pages) {
        page = total_pages;
    }
    int start = (page - 1) * page_size;
    int end = start + page_size;
    if (end > filtered.size) {
        end = filtered.size;
    }
    printf("{\"success\":true,\"input\":");
    json_string(input);
    printf(",\"page\":%d,\"page_size\":%d,\"total\":%d,\"total_pages\":%d,\"data\":[",
           page, page_size, filtered.size, total_pages);
    for (int i = start; i < end; i++) {
        if (i > start) {
            printf(",");
        }
        print_data_object(&filtered.items[i],
                          find_mark(raw_marks_ptr, filtered.items[i].record_index),
                          find_operation_mark(op_marks_ptr, filtered.items[i].record_index));
    }
    printf("]}\n");
    if (raw_marks_ptr != NULL) {
        mark_set_free(&raw_marks);
    }
    if (op_marks_ptr != NULL) {
        operation_mark_set_free(&op_marks);
    }
    dataset_free(&set);
    dataset_free(&filtered);
    return 0;
}

static int run_stats(int argc, char** argv) {
    const char* input = NULL;
    if (!require_input_path(argc, argv, &input)) {
        return 1;
    }
    DataSet set;
    ReadSummary summary;
    if (!read_dataset_csv(input, &set, &summary)) {
        print_error_json("failed to read data file");
        return 1;
    }
    OperationMarkSet op_marks;
    OperationMarkSet* op_marks_ptr = NULL;
    const char* op_marks_path = arg_value(argc, argv, "--op-marks");
    if (op_marks_path != NULL && read_operation_marks_json(op_marks_path, &op_marks)) {
        op_marks_ptr = &op_marks;
    }
    DataSet active;
    DataSet* stats_set = &set;
    int excluded_deleted = 0;
    if (op_marks_ptr != NULL) {
        excluded_deleted = copy_without_deleted(&set, op_marks_ptr, &active);
        stats_set = &active;
    }
    BasicStats stats[WQ_FIELD_COUNT];
    double corr[WQ_FIELD_COUNT][WQ_FIELD_COUNT];
    compute_basic_stats(stats_set, stats);
    for (int i = 0; i < WQ_FIELD_COUNT; i++) {
        for (int j = 0; j < WQ_FIELD_COUNT; j++) {
            corr[i][j] = pearson(stats_set, i, j);
        }
    }
    printf("{\"success\":true,\"records\":%d,\"excluded_deleted\":%d,\"stats\":[",
           stats_set->size, excluded_deleted);
    for (int i = 0; i < WQ_FIELD_COUNT; i++) {
        if (i > 0) {
            printf(",");
        }
        printf("{\"field\":\"%s\",\"label\":\"%s\",\"mean\":", FIELDS[i].key, FIELDS[i].label);
        json_number(stats[i].mean);
        printf(",\"min\":");
        json_number(stats[i].min_value);
        printf(",\"max\":");
        json_number(stats[i].max_value);
        printf(",\"stddev\":");
        json_number(stats[i].stddev);
        printf("}");
    }
    printf("],\"correlation\":[");
    for (int i = 0; i < WQ_FIELD_COUNT; i++) {
        if (i > 0) {
            printf(",");
        }
        printf("[");
        for (int j = 0; j < WQ_FIELD_COUNT; j++) {
            if (j > 0) {
                printf(",");
            }
            json_number(corr[i][j]);
        }
        printf("]");
    }
    printf("]}\n");
    if (op_marks_ptr != NULL) {
        dataset_free(&active);
        operation_mark_set_free(&op_marks);
    }
    dataset_free(&set);
    return 0;
}

static void time_string_for_index(int index, char* buffer, size_t size) {
    struct tm start;
    memset(&start, 0, sizeof(start));
    start.tm_year = 2025 - 1900;
    start.tm_mon = 0;
    start.tm_mday = 1;
    start.tm_hour = 12;
    start.tm_min = 0;
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

static int run_warnings(int argc, char** argv) {
    const char* input = NULL;
    if (!require_input_path(argc, argv, &input)) {
        return 1;
    }
    DataSet set;
    ReadSummary summary;
    if (!read_dataset_csv(input, &set, &summary)) {
        print_error_json("failed to read data file");
        return 1;
    }
    OperationMarkSet op_marks;
    OperationMarkSet* op_marks_ptr = NULL;
    const char* op_marks_path = arg_value(argc, argv, "--op-marks");
    if (op_marks_path != NULL && read_operation_marks_json(op_marks_path, &op_marks)) {
        op_marks_ptr = &op_marks;
    }
    DataSet active;
    DataSet* warn_set = &set;
    int excluded_deleted = 0;
    if (op_marks_ptr != NULL) {
        excluded_deleted = copy_without_deleted(&set, op_marks_ptr, &active);
        warn_set = &active;
    }
    int warning_count = 0;
    int json_items = 0;
    printf("{\"success\":true,\"warnings\":[");
    int max_day = (12 * 60 + set.size * 5) / 1440 + 1;
    for (int day = 0; day <= max_day; day++) {
        double sum = 0.0;
        int count = 0;
        int first_index = -1;
        for (int i = 0; i < warn_set->size; i++) {
            int original_index = warn_set->items[i].record_index - 1;
            int absolute_minutes = 12 * 60 + original_index * 5;
            int current_day = absolute_minutes / 1440;
            int minute_of_day = absolute_minutes % 1440;
            if (current_day == day && minute_of_day >= 180 && minute_of_day <= 300) {
                double value = warn_set->items[i].do_value;
                if (!isnan(value)) {
                    sum += value;
                    count++;
                    if (first_index < 0) {
                        first_index = original_index;
                    }
                }
            }
        }
        if (count > 0) {
            double avg = sum / count;
            const char* level = NULL;
            const char* advice = NULL;
            if (avg < 3.0) {
                level = "severe_low_oxygen";
                advice = "Add granular oxygen immediately and reduce feeding.";
            } else if (avg < 4.0) {
                level = "low_oxygen";
                advice = "Turn on bottom aerator.";
            }
            if (level != NULL) {
                char time_text[32];
                time_string_for_index(first_index, time_text, sizeof(time_text));
                if (json_items > 0) {
                    printf(",");
                }
                printf("{\"time\":");
                json_string(time_text);
                printf(",\"type\":");
                json_string(level);
                printf(",\"value\":%.8f,\"advice\":", avg);
                json_string(advice);
                printf("}");
                json_items++;
                warning_count++;
            }
        }
    }
    for (int i = 12; i < warn_set->size; i++) {
        double previous = warn_set->items[i - 12].salinity;
        double current = warn_set->items[i].salinity;
        if (!isnan(previous) && !isnan(current) && previous - current > 2.0) {
            char time_text[32];
            time_string_for_index(warn_set->items[i].record_index - 1, time_text, sizeof(time_text));
            if (json_items > 0) {
                printf(",");
            }
            printf("{\"time\":");
            json_string(time_text);
            printf(",\"type\":\"salinity_drop_1h\",\"value\":%.8f,"
                   "\"advice\":\"Close inlet and add VC or glucose.\"}", previous - current);
            json_items++;
            warning_count++;
        }
    }
    for (int i = 288; i < warn_set->size; i++) {
        double previous = warn_set->items[i - 288].salinity;
        double current = warn_set->items[i].salinity;
        if (!isnan(previous) && !isnan(current) && previous - current > 5.0) {
            char time_text[32];
            time_string_for_index(warn_set->items[i].record_index - 1, time_text, sizeof(time_text));
            if (json_items > 0) {
                printf(",");
            }
            printf("{\"time\":");
            json_string(time_text);
            printf(",\"type\":\"salinity_drop_24h\",\"value\":%.8f,"
                   "\"advice\":\"Close inlet and add VC or glucose.\"}", previous - current);
            json_items++;
            warning_count++;
        }
    }
    printf("],\"count\":%d,\"excluded_deleted\":%d}\n", warning_count, excluded_deleted);
    if (op_marks_ptr != NULL) {
        dataset_free(&active);
        operation_mark_set_free(&op_marks);
    }
    dataset_free(&set);
    return 0;
}

static int run_predict(int argc, char** argv) {
    const char* input = NULL;
    if (!require_input_path(argc, argv, &input)) {
        return 1;
    }
    DataSet set;
    ReadSummary summary;
    if (!read_dataset_csv(input, &set, &summary)) {
        print_error_json("failed to read data file");
        return 1;
    }
    OperationMarkSet op_marks;
    OperationMarkSet* op_marks_ptr = NULL;
    const char* op_marks_path = arg_value(argc, argv, "--op-marks");
    if (op_marks_path != NULL && read_operation_marks_json(op_marks_path, &op_marks)) {
        op_marks_ptr = &op_marks;
    }
    DataSet active;
    DataSet* predict_set = &set;
    int excluded_deleted = 0;
    if (op_marks_ptr != NULL) {
        excluded_deleted = copy_without_deleted(&set, op_marks_ptr, &active);
        predict_set = &active;
    }
    int candidate_fields[] = {0, 1, 2, 5};
    RegressionResult air = linear_regression_for_field(predict_set, 5);
    printf("{\"success\":true,\"target\":\"do\",\"primary\":{\"x_field\":\"air_temp\",\"slope\":");
    json_number(air.slope);
    printf(",\"intercept\":");
    json_number(air.intercept);
    printf(",\"r2\":");
    json_number(air.r2);
    printf(",\"rmse\":");
    json_number(air.rmse);
    printf("},\"excluded_deleted\":%d,\"models\":[", excluded_deleted);
    for (int i = 0; i < 4; i++) {
        RegressionResult r = linear_regression_for_field(predict_set, candidate_fields[i]);
        if (i > 0) {
            printf(",");
        }
        printf("{\"x_field\":\"%s\",\"label\":\"%s\",\"slope\":",
               FIELDS[candidate_fields[i]].key, FIELDS[candidate_fields[i]].label);
        json_number(r.slope);
        printf(",\"intercept\":");
        json_number(r.intercept);
        printf(",\"r2\":");
        json_number(r.r2);
        printf(",\"rmse\":");
        json_number(r.rmse);
        printf("}");
    }
    printf("]}\n");
    if (op_marks_ptr != NULL) {
        dataset_free(&active);
        operation_mark_set_free(&op_marks);
    }
    dataset_free(&set);
    return 0;
}

static int run_filter(int argc, char** argv) {
    const char* input = NULL;
    const char* output = arg_value(argc, argv, "--output");
    const char* window_arg = arg_value(argc, argv, "--window");
    if (!require_input_path(argc, argv, &input)) {
        return 1;
    }
    if (output == NULL || output[0] == '\0' || window_arg == NULL) {
        print_error_json("--output and --window are required");
        return 1;
    }
    int window = atoi(window_arg);
    if (!(window == 3 || window == 5 || window == 7 || window == 9 || window == 11)) {
        print_error_json("--window must be one of 3, 5, 7, 9, 11");
        return 1;
    }

    DataSet set;
    ReadSummary summary;
    if (!read_dataset_csv(input, &set, &summary)) {
        print_error_json("failed to read data file");
        return 1;
    }
    DataSet filtered;
    dataset_init(&filtered);
    if (!dataset_reserve(&filtered, set.size)) {
        dataset_free(&set);
        print_error_json("memory allocation failed");
        return 1;
    }
    for (int i = 0; i < set.size; i++) {
        if (!dataset_push(&filtered, set.items[i])) {
            dataset_free(&filtered);
            dataset_free(&set);
            print_error_json("memory allocation failed");
            return 1;
        }
    }

    int half = window / 2;
    for (int i = 0; i < set.size; i++) {
        int start = i - half;
        int end = i + half;
        if (start < 0) {
            start = 0;
        }
        if (end >= set.size) {
            end = set.size - 1;
        }
        for (int field = 0; field < 4; field++) {
            double sum = 0.0;
            int count = 0;
            for (int row = start; row <= end; row++) {
                double value = get_field_value(&set.items[row], field);
                if (!isnan(value)) {
                    sum += value;
                    count++;
                }
            }
            if (count > 0) {
                set_field_value(&filtered.items[i], field, sum / count);
            }
        }
    }

    if (!write_dataset_csv(output, &filtered)) {
        dataset_free(&filtered);
        dataset_free(&set);
        print_error_json("failed to write filtered file");
        return 1;
    }

    BasicStats before[WQ_FIELD_COUNT];
    BasicStats after[WQ_FIELD_COUNT];
    compute_basic_stats(&set, before);
    compute_basic_stats(&filtered, after);

    printf("{\"success\":true,\"input\":");
    json_string(input);
    printf(",\"output\":");
    json_string(output);
    printf(",\"window\":%d,\"stats\":[", window);
    for (int field = 0; field < 4; field++) {
        if (field > 0) {
            printf(",");
        }
        printf("{\"field\":\"%s\",\"label\":\"%s\",\"before_stddev\":",
               FIELDS[field].key, FIELDS[field].label);
        json_number(before[field].stddev);
        printf(",\"after_stddev\":");
        json_number(after[field].stddev);
        printf("}");
    }
    printf("]}\n");

    dataset_free(&filtered);
    dataset_free(&set);
    return 0;
}

static int run_modify(int argc, char** argv) {
    const char* input = NULL;
    const char* output = arg_value(argc, argv, "--output");
    if (!require_input_path(argc, argv, &input)) {
        return 1;
    }
    if (output == NULL) {
        output = input;
    }
    int row = arg_value(argc, argv, "--row") ? atoi(arg_value(argc, argv, "--row")) : -1;
    int field = data_service_field_index(arg_value(argc, argv, "--field"));
    const char* value_arg = arg_value(argc, argv, "--value");
    if (row < 1 || field < 0 || value_arg == NULL) {
        print_error_json("--row, --field, and --value are required");
        return 1;
    }
    double value = atof(value_arg);
    if (!is_field_in_range(field, value)) {
        print_error_json("value is outside the allowed range");
        return 1;
    }
    DataSet set;
    ReadSummary summary;
    if (!read_dataset_csv(input, &set, &summary)) {
        print_error_json("failed to read data file");
        return 1;
    }
    int found = 0;
    double old_value = NAN;
    for (int i = 0; i < set.size; i++) {
        if (set.items[i].record_index == row) {
            old_value = get_field_value(&set.items[i], field);
            set_field_value(&set.items[i], field, value);
            found = 1;
            break;
        }
    }
    if (!found) {
        dataset_free(&set);
        print_error_json("row not found");
        return 1;
    }
    if (!write_dataset_csv(output, &set)) {
        dataset_free(&set);
        print_error_json("failed to write output data");
        return 1;
    }
    printf("{\"success\":true,\"row\":%d,\"field\":\"%s\",\"old_value\":", row, FIELDS[field].key);
    json_number(old_value);
    printf(",\"new_value\":");
    json_number(value);
    printf(",\"output\":");
    json_string(output);
    printf("}\n");
    dataset_free(&set);
    return 0;
}

static int run_delete(int argc, char** argv) {
    const char* input = NULL;
    const char* output = arg_value(argc, argv, "--output");
    if (!require_input_path(argc, argv, &input)) {
        return 1;
    }
    if (output == NULL) {
        output = input;
    }
    int row = arg_value(argc, argv, "--row") ? atoi(arg_value(argc, argv, "--row")) : -1;
    int field = data_service_field_index(arg_value(argc, argv, "--field"));
    int has_min = arg_value(argc, argv, "--min") != NULL;
    int has_max = arg_value(argc, argv, "--max") != NULL;
    double min_value = has_min ? atof(arg_value(argc, argv, "--min")) : 0.0;
    double max_value = has_max ? atof(arg_value(argc, argv, "--max")) : 0.0;
    if (row < 1 && field < 0) {
        print_error_json("row or range condition is required");
        return 1;
    }
    DataSet set;
    ReadSummary summary;
    if (!read_dataset_csv(input, &set, &summary)) {
        print_error_json("failed to read data file");
        return 1;
    }
    int deleted = 0;
    int* rows = (int*)malloc(sizeof(int) * (size_t)set.size);
    if (rows == NULL) {
        dataset_free(&set);
        print_error_json("memory allocation failed");
        return 1;
    }
    for (int i = 0; i < set.size; i++) {
        int remove = 0;
        if (row >= 1 && set.items[i].record_index == row) {
            remove = 1;
        }
        if (field >= 0) {
            double value = get_field_value(&set.items[i], field);
            if (!isnan(value) &&
                (!has_min || value >= min_value) &&
                (!has_max || value <= max_value)) {
                remove = 1;
            }
        }
        if (remove) {
            rows[deleted] = set.items[i].record_index;
            deleted++;
        }
    }
    if (deleted == 0) {
        dataset_free(&set);
        free(rows);
        print_error_json("no rows matched");
        return 1;
    }
    if (!write_dataset_csv(output, &set)) {
        dataset_free(&set);
        free(rows);
        print_error_json("failed to write output data");
        return 1;
    }
    printf("{\"success\":true,\"deleted\":%d,\"rows\":[", deleted);
    for (int i = 0; i < deleted; i++) {
        if (i > 0) {
            printf(",");
        }
        printf("%d", rows[i]);
    }
    printf("],\"remaining\":%d,\"output\":", set.size - deleted);
    json_string(output);
    printf("}\n");
    dataset_free(&set);
    free(rows);
    return 0;
}

static int run_add(int argc, char** argv) {
    const char* input = NULL;
    const char* output = arg_value(argc, argv, "--output");
    if (!require_input_path(argc, argv, &input)) {
        return 1;
    }
    if (output == NULL) {
        output = input;
    }

    Data item;
    memset(&item, 0, sizeof(Data));
    for (int field = 0; field < WQ_FIELD_COUNT; field++) {
        const char* value_arg = arg_value(argc, argv, FIELDS[field].key);
        char dashed_key[32];
        snprintf(dashed_key, sizeof(dashed_key), "--%s", FIELDS[field].key);
        if (value_arg == NULL) {
            value_arg = arg_value(argc, argv, dashed_key);
        }
        if (value_arg == NULL) {
            char message[96];
            snprintf(message, sizeof(message), "missing value for %s", FIELDS[field].key);
            print_error_json(message);
            return 1;
        }
        double value = atof(value_arg);
        if (!is_field_in_range(field, value)) {
            char message[128];
            snprintf(message, sizeof(message), "value for %s is outside the allowed range", FIELDS[field].key);
            print_error_json(message);
            return 1;
        }
        set_field_value(&item, field, value);
    }

    DataSet set;
    ReadSummary summary;
    if (!read_dataset_csv(input, &set, &summary)) {
        print_error_json("failed to read data file");
        return 1;
    }
    item.record_index = set.size + 1;
    if (!dataset_push(&set, item)) {
        dataset_free(&set);
        print_error_json("failed to append data");
        return 1;
    }
    if (!write_dataset_csv(output, &set)) {
        dataset_free(&set);
        print_error_json("failed to write output data");
        return 1;
    }
    printf("{\"success\":true,\"row\":%d,\"output\":", item.record_index);
    json_string(output);
    printf(",\"record\":");
    print_data_object(&item, NULL, NULL);
    printf("}\n");
    dataset_free(&set);
    return 0;
}

int data_service_run_cli(int argc, char** argv) {
    if (argc < 2) {
        print_error_json("command is required");
        return 1;
    }
    const char* command = argv[1];
    if (strcmp(command, "login") == 0) {
        return run_login(argc, argv);
    }
    if (strcmp(command, "overview") == 0) {
        return run_overview(argc, argv);
    }
    if (strcmp(command, "preprocess") == 0) {
        return run_preprocess(argc, argv);
    }
    if (strcmp(command, "query") == 0) {
        return run_query(argc, argv);
    }
    if (strcmp(command, "stats") == 0 || strcmp(command, "stat-report") == 0) {
        return run_stats(argc, argv);
    }
    if (strcmp(command, "warnings") == 0) {
        return run_warnings(argc, argv);
    }
    if (strcmp(command, "predict") == 0) {
        return run_predict(argc, argv);
    }
    if (strcmp(command, "filter") == 0) {
        return run_filter(argc, argv);
    }
    if (strcmp(command, "modify") == 0) {
        return run_modify(argc, argv);
    }
    if (strcmp(command, "delete") == 0) {
        return run_delete(argc, argv);
    }
    if (strcmp(command, "add") == 0) {
        return run_add(argc, argv);
    }
    if (has_arg(argc, argv, "--help")) {
        printf("{\"success\":true,\"commands\":[\"login\",\"overview\",\"preprocess\","
               "\"query\",\"stats\",\"warnings\",\"predict\",\"filter\",\"modify\",\"delete\",\"add\"]}\n");
        return 0;
    }
    print_error_json("unknown command");
    return 1;
}

//
// io_marks.c - 预处理标记与操作标记的 I/O
//

#include "DataService.h"
#include "DataServiceInternal.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
            if (marks->size == marks->capacity) {
                int nc = marks->capacity == 0 ? 1000 : marks->capacity * 2;
                if (!ds_dyn_reserve((void**)&marks->items, &marks->capacity, sizeof(RowMark), nc))
                    break;
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

/* ── operation marks ── */

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
        if (!ds_dyn_reserve((void**)&set->items, &set->capacity, sizeof(OperationMark), nc))
            return 0;
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
    const FieldMeta* fields = data_service_fields();
    const char* fields_pos = strstr(object_start, "\"fields\"");
    if (fields_pos == NULL) return;
    const char* start = strchr(fields_pos, '[');
    const char* end = strchr(fields_pos, ']');
    if (start == NULL || end == NULL || end <= start) return;
    for (int field = 0; field < WQ_FIELD_COUNT; field++) {
        char pattern[32];
        snprintf(pattern, sizeof(pattern), "\"%s\"", fields[field].key);
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
    const FieldMeta* fields = data_service_fields();
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
                fprintf(fp, "\"%s\"", fields[f].key);
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

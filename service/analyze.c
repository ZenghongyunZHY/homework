//
// analyze.c - 预处理、统计、预警、预测、滤波、查询与数据变更
//

#include "DataService.h"
#include "DataServiceInternal.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ── preprocessing ── */

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

/* ── statistics ── */

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

/* ── warnings ── */

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

static int warnings_reserve(WarningSet* warnings) {
    if (warnings->size < warnings->capacity) return 1;
    int nc = warnings->capacity == 0 ? 256 : warnings->capacity * 2;
    return ds_dyn_reserve((void**)&warnings->items, &warnings->capacity, sizeof(WarningItem), nc);
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
                if (!warnings_reserve(warnings)) return warnings->size;
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
            if (!warnings_reserve(warnings)) return warnings->size;
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
            if (!warnings_reserve(warnings)) return warnings->size;
            WarningItem* w = &warnings->items[warnings->size++];
            time_string_for_index(set->items[i].record_index - 1, w->time, sizeof(w->time));
            snprintf(w->type, sizeof(w->type), "salinity_drop_24h");
            w->value = prev - cur;
            snprintf(w->advice, sizeof(w->advice), "Close inlet and add VC or glucose.");
        }
    }
    return warnings->size;
}

/* ── prediction ── */

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

/* ── moving average filter ── */

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

/* ── query ── */

static int g_sort_field = 0;
static int g_sort_desc = 0;

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

/* ── data modification ── */

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

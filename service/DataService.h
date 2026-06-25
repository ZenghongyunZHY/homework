//
// Created by Chen on 2026/5/14.
//

#ifndef HOMEWORK_DATASERVICE_H
#define HOMEWORK_DATASERVICE_H

#include "../pojo/Data.h"
#include "../pojo/User.h"

#define WQ_FIELD_COUNT 6

/* ── field metadata ── */
typedef struct {
    const char* key;
    const char* label;
    const char* unit;
    double min_value;
    double max_value;
} FieldMeta;

/* ── read summary ── */
typedef struct {
    int total_records;
    int parsed_records;
    int missing_values;
    int format_errors;
} ReadSummary;

/* ── preprocess result ── */
typedef struct {
    int total_records;
    int kept_records;
    int deleted_records;
    int abnormal_records;
    int abnormal_values;
    int filled_values;
    int missing_values;
} PreprocessResult;

/* ── basic statistics ── */
typedef struct {
    int count;
    double mean;
    double min_value;
    double max_value;
    double stddev;
} BasicStats;

/* ── regression result ── */
typedef struct {
    int count;
    double slope;
    double intercept;
    double r2;
    double rmse;
} RegressionResult;

/* ── row mark (for preprocess tracking) ── */
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

/* ── operation mark (for modify/delete tracking) ── */
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

/* ── warning ── */
typedef struct {
    char time[32];
    char type[32];
    double value;
    char advice[128];
} WarningItem;

typedef struct {
    WarningItem* items;
    int size;
    int capacity;
} WarningSet;

/* ── query page ── */
typedef struct {
    Data* items;
    int total;
    int page;
    int page_size;
    int total_pages;
} QueryPage;

/* ── storage benchmark ── */
typedef struct {
    long csv_size_bytes;
    long bin_size_bytes;
    double csv_write_seconds;
    double csv_read_seconds;
    double bin_write_seconds;
    double bin_read_seconds;
} StorageBenchmark;

/* ── metadata ── */
const FieldMeta* data_service_fields(void);
int data_service_field_index(const char* field);

/* ── dataset lifecycle ── */
void data_service_dataset_init(DataSet* set);
void data_service_dataset_free(DataSet* set);
int data_service_dataset_push(DataSet* set, Data item);

/* ── field access ── */
double data_service_get_field_value(const Data* data, int field);
void data_service_set_field_value(Data* data, int field, double value);
int data_service_is_field_in_range(int field, double value);

/* ── CSV I/O ── */
int data_service_read_csv(const char* path, DataSet* set, ReadSummary* summary);
int data_service_write_csv(const char* path, const DataSet* set);

/* ── marks I/O ── */
int data_service_write_marks(const char* path, const DataSet* source);
int data_service_read_marks(const char* path, MarkSet* marks);
const RowMark* data_service_find_mark(const MarkSet* marks, int row);

/* ── operation marks I/O ── */
int data_service_read_operation_marks(const char* path, OperationMarkSet* marks);
int data_service_write_operation_marks(const char* path, const OperationMarkSet* marks);
const OperationMark* data_service_find_operation_mark(const OperationMarkSet* marks, int row);
void data_service_operation_mark_set_init(OperationMarkSet* set);
void data_service_operation_mark_set_free(OperationMarkSet* set);
int data_service_operation_mark_set_push(OperationMarkSet* set, OperationMark item);

/* ── preprocessing ── */
int data_service_preprocess(const DataSet* source, DataSet* clean, PreprocessResult* result);

/* ── statistics ── */
void data_service_compute_basic_stats(const DataSet* set, BasicStats stats[WQ_FIELD_COUNT]);
double data_service_pearson(const DataSet* set, int x_field, int y_field);

/* ── warnings ── */
int data_service_detect_warnings(const DataSet* set, WarningSet* warnings);

/* ── prediction ── */
RegressionResult data_service_linear_regression(const DataSet* set, int x_field);

/* ── moving average filter ── */
int data_service_moving_average_filter(const DataSet* source, int window,
    DataSet* filtered, BasicStats before[WQ_FIELD_COUNT], BasicStats after[WQ_FIELD_COUNT]);

/* ── query ── */
QueryPage data_service_query_page(const DataSet* set, int page, int page_size,
    int filter_field, double filter_min, double filter_max,
    int sort_field, int sort_desc,
    const MarkSet* raw_marks, const OperationMarkSet* op_marks,
    const char* view_mode, const char* operation_filter);

/* ── data modification ── */
int data_service_modify_record(DataSet* set, int row, int field, double new_value,
    double* old_value_out);
int data_service_delete_records(DataSet* set, int row, int field,
    double min_value, double max_value, int** deleted_rows_out, int* deleted_count_out);
int data_service_add_record(DataSet* set, Data item);

/* ── login ── */
User* data_service_validate_login(const char* username, const char* password);

/* ── binary storage ── */
int data_service_write_binary(const char* path, const DataSet* set);
int data_service_read_binary(const char* path, DataSet* set, ReadSummary* summary);
StorageBenchmark data_service_benchmark_storage(const char* csv_path);

/* ── backup / restore ── */
char* data_service_backup_file(const char* src_path);
char** data_service_list_backups(const char* dir_path, int* count_out);
int data_service_restore_from_backup(const char* backup_path, DataSet* set, ReadSummary* summary);

/* ── report generation ── */
int data_service_generate_overview_report(const char* path, const DataSet* set,
    const ReadSummary* summary, const PreprocessResult* preprocess);
int data_service_generate_stats_report(const char* path,
    const BasicStats stats[WQ_FIELD_COUNT], double corr[WQ_FIELD_COUNT][WQ_FIELD_COUNT]);
int data_service_generate_warning_report(const char* path, const WarningSet* warnings);
int data_service_generate_predict_report(const char* path,
    const RegressionResult* primary, const RegressionResult models[4]);

/* ── CLI mode ── */
int data_service_run_cli(int argc, char** argv);

#endif // HOMEWORK_DATASERVICE_H

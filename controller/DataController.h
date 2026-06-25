//
// Created by Chen on 2026/5/14.
//

#ifndef HOMEWORK_DATACONTROLLER_H
#define HOMEWORK_DATACONTROLLER_H

#include "../pojo/Data.h"
#include "../pojo/User.h"
#include "../service/DataService.h"

typedef struct {
    User current_user;
    int is_logged_in;
    int login_attempts;
    DataSet current_dataset;
    DataSet clean_dataset;
    char current_file_path[512];
    char clean_file_path[512];
    char marks_file_path[512];
    char op_marks_file_path[512];
    MarkSet raw_marks;
    OperationMarkSet op_marks;
    int is_data_loaded;
    int is_preprocessed;
    int query_page;
    int query_page_size;
    int query_filter_field;
    double query_filter_min;
    double query_filter_max;
    int query_sort_field;
    int query_sort_desc;
    char query_view_mode[32];
    char query_operation_filter[32];
} AppState;

/* ── 生命周期 ── */
void controller_init(AppState* state);
void controller_destroy(AppState* state);

/* ── 数据加载 ── */
int controller_load_data(AppState* state, const char* filepath);

/* ── 预处理与滤波 ── */
int controller_run_preprocess(AppState* state, PreprocessResult* result);
int controller_run_filter(AppState* state, int window,
    BasicStats before[WQ_FIELD_COUNT], BasicStats after[WQ_FIELD_COUNT]);

/* ── 数据维护 ── */
int controller_modify_record(AppState* state, int row, int field, double value, double* old_val);
int controller_delete_records(AppState* state, int row, int field, double min, double max,
    int** rows, int* count);
int controller_add_record(AppState* state, Data item);
int controller_save_data(AppState* state);

/* ── 交互菜单入口 ── */
int controller_run_interactive(void);

#endif // HOMEWORK_DATACONTROLLER_H

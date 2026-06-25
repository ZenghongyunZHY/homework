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

int controller_run_interactive(void);

#endif // HOMEWORK_DATACONTROLLER_H

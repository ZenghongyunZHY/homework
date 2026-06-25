#include "DataController.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* forward declarations from DataView */
int view_show_login_screen(AppState* state);
int view_prompt_load_file(AppState* state);
int view_show_main_menu(AppState* state);
void view_handle_data_operations(AppState* state);
void view_handle_preprocess(AppState* state);
void view_handle_statistics(AppState* state);
void view_handle_prediction(AppState* state);
void view_handle_overview(AppState* state);
void view_handle_warnings(AppState* state);
void view_handle_reports(AppState* state);
void view_handle_backup(AppState* state);
void view_clear_screen(void);
int view_confirm(const char* prompt);

void controller_init(AppState* state) {
    memset(state, 0, sizeof(AppState));
    data_service_dataset_init(&state->current_dataset);
    data_service_dataset_init(&state->clean_dataset);
    state->query_page = 1;
    state->query_page_size = 15;
    state->query_filter_field = -1;
    state->query_sort_field = -1;
    state->query_sort_desc = 0;
    strcpy(state->query_view_mode, "raw");
    strcpy(state->query_operation_filter, "all");
}

void controller_destroy(AppState* state) {
    data_service_dataset_free(&state->current_dataset);
    data_service_dataset_free(&state->clean_dataset);
    if (state->raw_marks.items) { free(state->raw_marks.items); state->raw_marks.items = NULL; }
    data_service_operation_mark_set_free(&state->op_marks);
}

int controller_load_data(AppState* state, const char* filepath) {
    data_service_dataset_free(&state->current_dataset);
    data_service_dataset_free(&state->clean_dataset);
    if (state->raw_marks.items) { free(state->raw_marks.items); state->raw_marks.items = NULL; state->raw_marks.size = 0; state->raw_marks.capacity = 0; }
    data_service_operation_mark_set_free(&state->op_marks);
    state->is_data_loaded = 0;
    state->is_preprocessed = 0;

    ReadSummary summary;
    if (!data_service_read_csv(filepath, &state->current_dataset, &summary)) return 0;
    strncpy(state->current_file_path, filepath, sizeof(state->current_file_path) - 1);
    state->is_data_loaded = 1;
    return 1;
}

int controller_run_preprocess(AppState* state, PreprocessResult* result) {
    if (!state->is_data_loaded) return 0;
    data_service_dataset_free(&state->clean_dataset);

    snprintf(state->marks_file_path, sizeof(state->marks_file_path), "dao/data_marks.csv");
    snprintf(state->clean_file_path, sizeof(state->clean_file_path), "dao/data_clean.csv");
    snprintf(state->op_marks_file_path, sizeof(state->op_marks_file_path), "dao/data_op_marks.json");

    data_service_write_marks(state->marks_file_path, &state->current_dataset);
    int ok = data_service_preprocess(&state->current_dataset, &state->clean_dataset, result);
    if (!ok) return 0;
    data_service_write_csv(state->clean_file_path, &state->clean_dataset);
    state->is_preprocessed = 1;
    return 1;
}

int controller_run_filter(AppState* state, int window, BasicStats before[WQ_FIELD_COUNT], BasicStats after[WQ_FIELD_COUNT]) {
    DataSet* src = state->is_preprocessed ? &state->clean_dataset : &state->current_dataset;
    DataSet filtered;
    int ok = data_service_moving_average_filter(src, window, &filtered, before, after);
    if (!ok) return 0;
    data_service_write_csv("dao/data_filtered.csv", &filtered);
    data_service_dataset_free(&filtered);
    return 1;
}

int controller_modify_record(AppState* state, int row, int field, double value, double* old_val) {
    if (!state->is_data_loaded) return 0;
    /* auto-backup */
    data_service_backup_file(state->current_file_path);
    return data_service_modify_record(&state->current_dataset, row, field, value, old_val);
}

int controller_delete_records(AppState* state, int row, int field, double min, double max, int** rows, int* count) {
    if (!state->is_data_loaded) return 0;
    data_service_backup_file(state->current_file_path);
    return data_service_delete_records(&state->current_dataset, row, field, min, max, rows, count);
}

int controller_add_record(AppState* state, Data item) {
    if (!state->is_data_loaded) return 0;
    return data_service_add_record(&state->current_dataset, item);
}

int controller_save_data(AppState* state) {
    if (!state->is_data_loaded) return 0;
    return data_service_write_csv(state->current_file_path, &state->current_dataset);
}

int controller_run_interactive(void) {
    AppState state;
    controller_init(&state);

#ifdef _WIN32
    system("chcp 65001 > nul");
#endif

    if (view_show_login_screen(&state) != 0) {
        controller_destroy(&state);
        return 1;
    }

    /* prompt for data file */
    if (!view_prompt_load_file(&state)) {
        printf("未能加载数据文件，程序将退出。\n");
        controller_destroy(&state);
        return 1;
    }

    int running = 1;
    while (running) {
        int choice = view_show_main_menu(&state);
        switch (choice) {
            case 0:
                if (view_confirm("确定要退出系统吗？")) running = 0;
                break;
            case 1: view_handle_data_operations(&state); break;
            case 2: view_handle_preprocess(&state); break;
            case 3: view_handle_statistics(&state); break;
            case 4: view_handle_prediction(&state); break;
            case 5: view_handle_overview(&state); break;
            case 6: view_handle_warnings(&state); break;
            case 7: view_handle_reports(&state); break;
            case 8: view_handle_backup(&state); break;
            case 9: view_clear_screen(); break;
            default: break;
        }
    }

    controller_destroy(&state);
    return 0;
}

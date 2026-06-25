//
// DataViewInternal.h - 视图层跨模块共享的内部 helper
//
// 仅 DataView 拆分后的各 .c 文件包含此头；不对外暴露。
//

#ifndef HOMEWORK_DATAVIEW_INTERNAL_H
#define HOMEWORK_DATAVIEW_INTERNAL_H

#include "../controller/DataController.h"

/* ── 输入与 UI 工具（view_util.c）── */
void view_print_separator(int width);
void view_print_header(const char* title);
void view_read_line(char* buffer, int size);
int  view_read_int(const char* prompt, int min_val, int max_val);
double view_read_double(const char* prompt, double min_val, double max_val);
const char* role_label(int role);

/* ── 复用 helper ── */
DataSet* view_get_active_dataset(AppState* state);
void view_print_field_options(void);
void view_prompt_save_changes(AppState* state);
void view_compute_correlation(DataSet* src, double corr[WQ_FIELD_COUNT][WQ_FIELD_COUNT]);
char** view_show_backup_list(int* count_out);

#endif // HOMEWORK_DATAVIEW_INTERNAL_H

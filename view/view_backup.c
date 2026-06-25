//
// view_backup.c - 数据备份与恢复子菜单
//

#include "DataView.h"
#include "DataViewInternal.h"

#include <stdio.h>
#include <stdlib.h>

static void free_string_list(char** list, int count) {
    if (list == NULL) return;
    for (int i = 0; i < count; i++) free(list[i]);
    free(list);
}

void view_handle_backup(AppState* state) {
    int running = 1;
    while (running) {
        view_clear_screen();
        printf("========================================\n");
        printf("  数据备份与恢复\n");
        printf("========================================\n");
        printf("  [1] 创建备份\n");
        printf("  [2] 查看备份列表\n");
        printf("  [3] 从备份恢复\n");
        printf("  [0] 返回主菜单\n");
        printf("========================================\n");

        int choice = view_read_int("请选择 (0-3): ", 0, 3);
        switch (choice) {
            case 0: running = 0; break;
            case 1: {
                view_clear_screen();
                view_print_header("创建备份");
                char* path = data_service_backup_file(state->current_file_path);
                if (path) {
                    printf("备份成功，文件已保存至: %s\n", path);
                    free(path);
                } else {
                    printf("备份失败！\n");
                }
                view_pause();
                break;
            }
            case 2: {
                view_clear_screen();
                view_print_header("备份列表");
                int count = 0;
                char** list = view_show_backup_list(&count);
                free_string_list(list, count);
                view_pause();
                break;
            }
            case 3: {
                view_clear_screen();
                view_print_header("从备份恢复");
                int count = 0;
                char** list = view_show_backup_list(&count);
                if (list != NULL && count > 0) {
                    int idx = view_read_int("请选择要恢复的备份编号 (0=取消): ", 0, count);
                    if (idx > 0 && view_confirm("确定要恢复此备份？当前数据将被替换！")) {
                        ReadSummary summary;
                        DataSet restored;
                        if (data_service_restore_from_backup(list[idx - 1], &restored, &summary)) {
                            data_service_dataset_free(&state->current_dataset);
                            data_service_dataset_free(&state->clean_dataset);
                            state->current_dataset = restored;
                            state->is_preprocessed = 0;
                            printf("恢复成功！共加载 %d 条记录。\n", restored.size);
                        } else {
                            printf("恢复失败！备份文件可能已损坏。\n");
                        }
                    }
                    free_string_list(list, count);
                }
                view_pause();
                break;
            }
        }
    }
}

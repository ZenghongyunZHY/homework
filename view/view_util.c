//
// view_util.c - 屏幕控制、输入读取与跨视图复用 helper
//

#include "DataView.h"
#include "DataViewInternal.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <conio.h>
#include <windows.h>
#else
#include <termios.h>
#include <unistd.h>
#endif

void view_clear_screen(void) {
#ifdef _WIN32
    system("cls");
#else
    system("clear");
#endif
}

void view_pause(void) {
    printf("\n按 Enter 键继续...");
    int c;
    while ((c = getchar()) != '\n' && c != EOF) {}
}

void view_print_separator(int width) {
    for (int i = 0; i < width; i++) putchar('=');
    printf("\n");
}

void view_print_header(const char* title) {
    view_print_separator(40);
    printf("  %s\n", title);
    view_print_separator(40);
}

void view_read_line(char* buffer, int size) {
    if (fgets(buffer, size, stdin) == NULL) { buffer[0] = '\0'; return; }
    size_t len = strlen(buffer);
    while (len > 0 && (buffer[len - 1] == '\n' || buffer[len - 1] == '\r'))
        buffer[--len] = '\0';
}

int view_read_int(const char* prompt, int min_val, int max_val) {
    int value;
    char buffer[64];
    while (1) {
        printf("%s", prompt);
        view_read_line(buffer, sizeof(buffer));
        if (sscanf(buffer, "%d", &value) == 1 && value >= min_val && value <= max_val)
            return value;
        printf("输入无效，请输入 %d-%d 之间的整数。\n", min_val, max_val);
    }
}

double view_read_double(const char* prompt, double min_val, double max_val) {
    double value;
    char buffer[64];
    while (1) {
        printf("%s", prompt);
        view_read_line(buffer, sizeof(buffer));
        if (sscanf(buffer, "%lf", &value) == 1 && value >= min_val && value <= max_val)
            return value;
        printf("输入无效，请输入 %.2f-%.2f 之间的数值。\n", min_val, max_val);
    }
}

int view_confirm(const char* prompt) {
    char buffer[16];
    printf("%s (y/n): ", prompt);
    view_read_line(buffer, sizeof(buffer));
    return buffer[0] == 'y' || buffer[0] == 'Y';
}

const char* role_label(int role) {
    return role == 1 ? "管理员" : "访客";
}

/* ── 复用 helper ── */

DataSet* view_get_active_dataset(AppState* state) {
    return state->is_preprocessed ? &state->clean_dataset : &state->current_dataset;
}

void view_print_field_options(void) {
    const FieldMeta* fm = data_service_fields();
    printf("可选参数: ");
    for (int i = 0; i < WQ_FIELD_COUNT; i++) printf("%s ", fm[i].key);
    printf("\n");
}

void view_prompt_save_changes(AppState* state) {
    if (!view_confirm("是否保存到文件？")) return;
    if (controller_save_data(state))
        printf("数据已保存到 %s\n", state->current_file_path);
    else
        printf("保存失败！\n");
}

void view_compute_correlation(DataSet* src, double corr[WQ_FIELD_COUNT][WQ_FIELD_COUNT]) {
    for (int i = 0; i < WQ_FIELD_COUNT; i++)
        for (int j = 0; j < WQ_FIELD_COUNT; j++)
            corr[i][j] = data_service_pearson(src, i, j);
}

char** view_show_backup_list(int* count_out) {
    *count_out = 0;
    char** list = data_service_list_backups("backup", count_out);
    if (list == NULL || *count_out == 0) {
        printf("  暂无备份文件。\n");
        return list;
    }
    for (int i = 0; i < *count_out; i++)
        printf("  [%d] %s\n", i + 1, list[i]);
    return list;
}

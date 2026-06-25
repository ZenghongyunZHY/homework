#include "DataView.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <conio.h>
#include <windows.h>
#else
#include <termios.h>
#include <unistd.h>
#endif

/* ════════════════════════════════════════════════════════════════════
   utility functions
   ════════════════════════════════════════════════════════════════════ */

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

static void read_password(char* buffer, int size) {
#ifdef _WIN32
    int i = 0;
    while (i < size - 1) {
        int c = _getch();
        if (c == '\r' || c == '\n') break;
        if (c == '\b' || c == 127) {
            if (i > 0) { i--; printf("\b \b"); }
        } else {
            buffer[i++] = (char)c;
            putchar('*');
        }
    }
    buffer[i] = '\0';
    printf("\n");
#else
    struct termios oldt, newt;
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ECHO | ICANON);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    int i = 0;
    while (i < size - 1) {
        char c = getchar();
        if (c == '\n' || c == '\r') break;
        if (c == 127 || c == '\b') {
            if (i > 0) { i--; printf("\b \b"); }
        } else {
            buffer[i++] = c;
            putchar('*');
        }
    }
    buffer[i] = '\0';
    printf("\n");
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
#endif
}

static const char* role_label(int role) {
    return role == 1 ? "管理员" : "访客";
}

/* ════════════════════════════════════════════════════════════════════
   login screen
   ════════════════════════════════════════════════════════════════════ */

int view_show_login_screen(AppState* state) {
    view_clear_screen();
    printf("========================================\n");
    printf("   海水养殖水质数据分析系统 v1.0\n");
    printf("========================================\n\n");

    state->login_attempts = 0;
    while (state->login_attempts < 3) {
        char username[64], password[64];
        printf("请输入用户名: ");
        view_read_line(username, sizeof(username));
        printf("请输入密码: ");
        read_password(password, sizeof(password));

        User* user = data_service_validate_login(username, password);
        if (user != NULL) {
            state->current_user = *user;
            state->is_logged_in = 1;
            printf("\n========================================\n");
            printf("  登录成功！欢迎您，%s！\n", role_label(user->role));
            printf("========================================\n");
            view_pause();
            return 0;
        }

        state->login_attempts++;
        int remaining = 3 - state->login_attempts;
        if (remaining > 0)
            printf("\n用户名或密码错误！（剩余尝试次数：%d）\n\n", remaining);
    }
    printf("\n登录失败次数过多，程序退出。\n");
    return -1;
}

/* ════════════════════════════════════════════════════════════════════
   file loading prompt
   ════════════════════════════════════════════════════════════════════ */

int view_prompt_load_file(AppState* state) {
    char filepath[512];
    printf("请输入数据文件路径（默认: dao/data_modify.csv）: ");
    view_read_line(filepath, sizeof(filepath));
    if (filepath[0] == '\0') strcpy(filepath, "dao/data_modify.csv");

    printf("正在加载数据文件 %s ...\n", filepath);
    if (!controller_load_data(state, filepath)) {
        printf("错误：无法读取文件 %s，请检查文件是否存在。\n", filepath);
        return 0;
    }
    printf("成功加载 %d 条记录。\n", state->current_dataset.size);
    view_pause();
    return 1;
}

/* ════════════════════════════════════════════════════════════════════
   main menu
   ════════════════════════════════════════════════════════════════════ */

int view_show_main_menu(AppState* state) {
    view_clear_screen();
    printf("========================================\n");
    printf("  海水养殖水质分析系统 v1.0\n");
    printf("  当前用户: %s (%s)\n", state->current_user.name, role_label(state->current_user.role));
    printf("  当前数据文件: %s (%d 条记录)\n", state->current_file_path, state->current_dataset.size);
    if (state->is_preprocessed)
        printf("  预处理状态: 已完成 (清洗后 %d 条)\n", state->clean_dataset.size);
    printf("========================================\n");

    if (state->current_user.role == 1) {
        printf("  [1] 数据基础操作\n");
        printf("  [2] 数据预处理\n");
        printf("  [3] 统计分析\n");
        printf("  [4] 预测分析\n");
        printf("  [5] 查看数据概览\n");
        printf("  [6] 查看预警报告\n");
        printf("  [7] 查看分析报告\n");
        printf("  [8] 数据备份与恢复\n");
        printf("  [9] 清屏\n");
        printf("  [0] 退出系统\n");
    } else {
        printf("  [1] 查看数据概览\n");
        printf("  [2] 查看预警报告\n");
        printf("  [3] 查看分析报告\n");
        printf("  [4] 清屏\n");
        printf("  [0] 退出系统\n");
    }
    printf("========================================\n");

    if (state->current_user.role == 1)
        return view_read_int("请选择操作 (0-9): ", 0, 9);
    else
        return view_read_int("请选择操作 (0-4): ", 0, 4);
}

/* ════════════════════════════════════════════════════════════════════
   data operations sub-menu
   ════════════════════════════════════════════════════════════════════ */

static void view_query_data(AppState* state) {
    int running = 1;
    int page = 1;
    const int page_size = 15;
    int filter_field = -1;
    double filter_min = -INFINITY, filter_max = INFINITY;
    int sort_field = -1, sort_desc = 0;
    char view_mode[32] = "raw";
    char op_filter[32] = "all";

    while (running) {
        view_clear_screen();
        printf("========================================\n");
        printf("  数据浏览\n");
        if (filter_field >= 0) {
            const FieldMeta* fm = data_service_fields();
            printf("  筛选: %s (%.2f ~ %.2f)\n", fm[filter_field].label, filter_min, filter_max);
        }
        if (sort_field >= 0) {
            const FieldMeta* fm = data_service_fields();
            printf("  排序: %s (%s)\n", fm[sort_field].label, sort_desc ? "降序" : "升序");
        }
        printf("========================================\n");

        DataSet* src = (view_mode[0] == 'p') ? &state->clean_dataset : &state->current_dataset;
        if (src->size == 0) {
            printf("  暂无数据。\n");
            view_pause();
            return;
        }

        QueryPage qp = data_service_query_page(src, page, page_size,
            filter_field, filter_min, filter_max, sort_field, sort_desc,
            NULL, NULL, view_mode, op_filter);

        printf("  序号  |  水温(℃)  |  盐度(PSU) |   pH   |  DO(mg/l) |  降水(mm)  |  气温(℃)\n");
        printf("--------|-----------|-----------|--------|-----------|-----------|----------\n");

        int start_idx = (qp.page - 1) * qp.page_size + 1;
        for (int i = 0; i < qp.total && i < qp.page_size; i++) {
            Data* d = &qp.items[i];
            printf(" %6d | %9.4f | %9.4f | %6.2f | %9.4f | %9.6f | %9.4f\n",
                   start_idx + i, d->temp, d->salinity, d->ph, d->do_value,
                   d->precipitation, d->air_temp);
        }

        printf("========================================\n");
        printf("  第 %d/%d 页，共 %d 条记录\n", qp.page, qp.total_pages, qp.total);
        printf("========================================\n");
        printf("  [N] 下一页  [P] 上一页  [J] 跳转到指定页\n");
        printf("  [F] 按条件筛选  [S] 按参数排序  [R] 重置条件\n");
        printf("  [V] 切换视图 (%s)\n", view_mode);
        printf("  [B] 返回上级菜单\n", state->clean_dataset.size);
        printf("========================================\n");

        free(qp.items);

        printf("请选择: ");
        char cmd[16];
        view_read_line(cmd, sizeof(cmd));

        if (cmd[0] == 'N' || cmd[0] == 'n') {
            if (page < qp.total_pages) page++;
        } else if (cmd[0] == 'P' || cmd[0] == 'p') {
            if (page > 1) page--;
        } else if (cmd[0] == 'J' || cmd[0] == 'j') {
            int p = view_read_int("跳转到页: ", 1, qp.total_pages);
            page = p;
        } else if (cmd[0] == 'F' || cmd[0] == 'f') {
            const FieldMeta* fm = data_service_fields();
            printf("可选参数: ");
            for (int i = 0; i < WQ_FIELD_COUNT; i++) printf("%s ", fm[i].key);
            printf("\n");
            char field_name[32];
            printf("请输入参数名（直接回车跳过筛选）: ");
            view_read_line(field_name, sizeof(field_name));
            if (field_name[0] != '\0') {
                int fi = data_service_field_index(field_name);
                if (fi >= 0) {
                    filter_field = fi;
                    filter_min = view_read_double("最小值: ", -1e9, 1e9);
                    filter_max = view_read_double("最大值: ", -1e9, 1e9);
                    page = 1;
                } else {
                    printf("未知参数: %s\n", field_name);
                    view_pause();
                }
            }
        } else if (cmd[0] == 'S' || cmd[0] == 's') {
            const FieldMeta* fm = data_service_fields();
            printf("可选参数: ");
            for (int i = 0; i < WQ_FIELD_COUNT; i++) printf("%s ", fm[i].key);
            printf("\n");
            char field_name[32];
            printf("请输入参数名: ");
            view_read_line(field_name, sizeof(field_name));
            int fi = data_service_field_index(field_name);
            if (fi >= 0) {
                sort_field = fi;
                sort_desc = view_read_int("排序方式 (0=升序, 1=降序): ", 0, 1);
                page = 1;
            } else {
                printf("未知参数: %s\n", field_name);
                view_pause();
            }
        } else if (cmd[0] == 'R' || cmd[0] == 'r') {
            filter_field = -1;
            sort_field = -1;
            sort_desc = 0;
            page = 1;
            strcpy(op_filter, "all");
        } else if (cmd[0] == 'V' || cmd[0] == 'v') {
            if (strcmp(view_mode, "raw") == 0) {
                strcpy(view_mode, "processed");
            } else {
                strcpy(view_mode, "raw");
            }
            page = 1;
        } else if (cmd[0] == 'B' || cmd[0] == 'b') {
            running = 0;
        }
    }
}

static void view_modify_data(AppState* state) {
    view_clear_screen();
    view_print_header("数据修改");

    int row = view_read_int("请输入要修改的记录号: ", 1, state->current_dataset.size);

    /* find and display the record */
    Data* found = NULL;
    for (int i = 0; i < state->current_dataset.size; i++) {
        if (state->current_dataset.items[i].record_index == row) { found = &state->current_dataset.items[i]; break; }
    }
    if (found == NULL) { printf("未找到记录号 %d。\n", row); view_pause(); return; }

    printf("\n当前记录:\n");
    printf("  水温: %.4f | 盐度: %.4f | pH: %.2f | DO: %.4f | 降水: %.6f | 气温: %.4f\n",
           found->temp, found->salinity, found->ph, found->do_value, found->precipitation, found->air_temp);

    const FieldMeta* fm = data_service_fields();
    printf("\n可选参数: ");
    for (int i = 0; i < WQ_FIELD_COUNT; i++) printf("%s ", fm[i].key);
    printf("\n");

    char field_name[32];
    printf("请输入要修改的参数名: ");
    view_read_line(field_name, sizeof(field_name));
    int field = data_service_field_index(field_name);
    if (field < 0) { printf("未知参数: %s\n", field_name); view_pause(); return; }

    double new_value = view_read_double("请输入新值: ", fm[field].min_value, fm[field].max_value);

    if (!view_confirm("确认修改？")) return;

    double old_value = NAN;
    if (!controller_modify_record(state, row, field, new_value, &old_value)) {
        printf("修改失败。\n"); view_pause(); return;
    }

    printf("修改成功！%s: %.4f -> %.4f\n", fm[field].label, old_value, new_value);

    if (view_confirm("是否保存到文件？")) {
        if (controller_save_data(state))
            printf("数据已保存到 %s\n", state->current_file_path);
        else
            printf("保存失败！\n");
    }
    view_pause();
}

static void view_delete_data(AppState* state) {
    view_clear_screen();
    view_print_header("数据删除");

    printf("  [1] 单条删除（按记录号）\n");
    printf("  [2] 批量删除（按条件）\n");
    printf("  [0] 返回\n");
    int choice = view_read_int("请选择: ", 0, 2);

    int row = -1, field = -1;
    double min_val = -INFINITY, max_val = INFINITY;

    if (choice == 1) {
        row = view_read_int("请输入要删除的记录号: ", 1, state->current_dataset.size);
    } else if (choice == 2) {
        const FieldMeta* fm = data_service_fields();
        printf("可选参数: ");
        for (int i = 0; i < WQ_FIELD_COUNT; i++) printf("%s ", fm[i].key);
        printf("\n");
        char field_name[32];
        printf("请输入参数名: ");
        view_read_line(field_name, sizeof(field_name));
        field = data_service_field_index(field_name);
        if (field < 0) { printf("未知参数: %s\n", field_name); view_pause(); return; }
        min_val = view_read_double("最小值: ", -1e9, 1e9);
        max_val = view_read_double("最大值: ", -1e9, 1e9);
    } else {
        return;
    }

    if (!view_confirm("确定要删除吗？此操作不可撤销！")) return;

    int* deleted_rows = NULL; int deleted_count = 0;
    int ret = controller_delete_records(state, row, field, min_val, max_val, &deleted_rows, &deleted_count);
    if (ret <= 0) { printf("未找到匹配的记录。\n"); view_pause(); return; }

    printf("已删除 %d 条记录。\n", deleted_count);
    free(deleted_rows);

    if (view_confirm("是否保存到文件？")) {
        if (controller_save_data(state))
            printf("数据已保存到 %s\n", state->current_file_path);
        else
            printf("保存失败！\n");
    }
    view_pause();
}

static void view_add_data(AppState* state) {
    view_clear_screen();
    view_print_header("添加新记录");

    Data item; memset(&item, 0, sizeof(Data));
    const FieldMeta* fm = data_service_fields();
    for (int i = 0; i < WQ_FIELD_COUNT; i++) {
        char prompt[64];
        snprintf(prompt, sizeof(prompt), "%s (%s) [%.1f~%.1f]: ",
                 fm[i].label, fm[i].unit, fm[i].min_value, fm[i].max_value);
        double value = view_read_double(prompt, fm[i].min_value, fm[i].max_value);
        data_service_set_field_value(&item, i, value);
    }

    printf("\n新记录:\n");
    printf("  水温: %.4f | 盐度: %.4f | pH: %.2f | DO: %.4f | 降水: %.6f | 气温: %.4f\n",
           item.temp, item.salinity, item.ph, item.do_value, item.precipitation, item.air_temp);

    if (!view_confirm("确认添加？")) return;

    int new_row = controller_add_record(state, item);
    if (new_row == 0) { printf("添加失败。\n"); view_pause(); return; }
    printf("成功添加记录，记录号: %d\n", new_row);

    if (view_confirm("是否保存到文件？")) {
        if (controller_save_data(state))
            printf("数据已保存到 %s\n", state->current_file_path);
        else
            printf("保存失败！\n");
    }
    view_pause();
}

static void view_benchmark(AppState* state) {
    view_clear_screen();
    view_print_header("存储格式性能对比");

    printf("正在计算，请稍候...\n");
    StorageBenchmark bench = data_service_benchmark_storage(state->current_file_path);

    printf("\n  格式    | 文件大小    | 写入时间    | 读取时间    | 人类可读\n");
    printf("----------|------------|------------|------------|----------\n");
    printf("  CSV文本 | %8ld B | %8.3f s | %8.3f s | 是\n",
           bench.csv_size_bytes, bench.csv_write_seconds, bench.csv_read_seconds);
    printf("  二进制  | %8ld B | %8.3f s | %8.3f s | 否\n",
           bench.bin_size_bytes, bench.bin_write_seconds, bench.bin_read_seconds);

    printf("\n分析讨论：\n");
    printf("  - CSV 格式适合人类直接阅读和编辑，适合数据交换和调试场景。\n");
    printf("  - 二进制格式读写速度更快，文件体积更小，适合程序内部存储和频繁读写场景。\n");
    printf("  - 实际存储大小与理论原始数据大小的区别源于编码方式和存储格式的差异。\n");
    printf("    CSV 使用文本编码，每个数字字符占用 1 字节，而二进制直接存储 IEEE 754\n");
    printf("    双精度浮点数（每字段 8 字节），因此更紧凑。\n");

    view_pause();
}

void view_handle_data_operations(AppState* state) {
    int running = 1;
    while (running) {
        view_clear_screen();
        printf("========================================\n");
        printf("  数据基础操作\n");
        printf("========================================\n");
        printf("  [1] 数据查询（分页浏览）\n");
        printf("  [2] 数据修改\n");
        printf("  [3] 数据删除\n");
        printf("  [4] 添加新记录\n");
        printf("  [5] 二进制存储性能对比\n");
        printf("  [6] 重新加载数据文件\n");
        printf("  [7] 保存数据\n");
        printf("  [0] 返回主菜单\n");
        printf("========================================\n");

        int choice = view_read_int("请选择 (0-7): ", 0, 7);
        switch (choice) {
            case 0: running = 0; break;
            case 1: view_query_data(state); break;
            case 2: view_modify_data(state); break;
            case 3: view_delete_data(state); break;
            case 4: view_add_data(state); break;
            case 5: view_benchmark(state); break;
            case 6: {
                char fp[512];
                printf("请输入数据文件路径: ");
                view_read_line(fp, sizeof(fp));
                if (fp[0] != '\0') {
                    if (controller_load_data(state, fp))
                        printf("成功加载 %d 条记录。\n", state->current_dataset.size);
                    else
                        printf("加载失败！\n");
                    view_pause();
                }
                break;
            }
            case 7:
                if (controller_save_data(state))
                    printf("数据已保存到 %s\n", state->current_file_path);
                else
                    printf("保存失败！\n");
                view_pause();
                break;
        }
    }
}

/* ════════════════════════════════════════════════════════════════════
   preprocess sub-menu
   ════════════════════════════════════════════════════════════════════ */

static void view_do_preprocess(AppState* state) {
    view_clear_screen();
    view_print_header("数据预处理");

    printf("正在执行预处理...\n");
    PreprocessResult result;
    if (!controller_run_preprocess(state, &result)) {
        printf("预处理失败！\n"); view_pause(); return;
    }

    printf("\n预处理完成！\n");
    printf("  总记录数:     %d\n", result.total_records);
    printf("  异常记录数:   %d\n", result.abnormal_records);
    printf("  异常值个数:   %d\n", result.abnormal_values);
    printf("  缺失值个数:   %d\n", result.missing_values);
    printf("  删除记录数:   %d\n", result.deleted_records);
    printf("  填充缺失值数: %d\n", result.filled_values);
    printf("  保留记录数:   %d\n", result.kept_records);
    printf("\n清洗后数据已保存到: %s\n", state->clean_file_path);
    printf("预处理标记已保存到: %s\n", state->marks_file_path);

    view_pause();
}

static void view_do_filter(AppState* state) {
    view_clear_screen();
    view_print_header("移动平均滤波");

    printf("窗口大小选项: 3, 5, 7, 9, 11\n");
    int window = view_read_int("请选择窗口大小: ", 3, 11);
    /* enforce odd */
    if (window % 2 == 0) { printf("窗口大小必须为奇数。\n"); view_pause(); return; }

    printf("正在执行滤波...\n");
    BasicStats before[WQ_FIELD_COUNT], after[WQ_FIELD_COUNT];
    if (!controller_run_filter(state, window, before, after)) {
        printf("滤波失败！\n"); view_pause(); return;
    }

    printf("\n滤波完成！结果已保存到 dao/data_filtered.csv\n\n");
    printf("  参数      | 滤波前标准差 | 滤波后标准差 | 噪声减少\n");
    printf("------------|------------|------------|----------\n");
    const FieldMeta* fm = data_service_fields();
    for (int field = 0; field < 4; field++) {
        double reduction = before[field].stddev > 0
            ? (1.0 - after[field].stddev / before[field].stddev) * 100 : 0;
        printf("  %-10s | %10.4f | %10.4f | %7.1f%%\n",
               fm[field].label, before[field].stddev, after[field].stddev, reduction);
    }
    printf("\n分析：窗口越大，噪声抑制越强，但也会平滑掉更多细节。\n");
    printf("建议根据实际应用场景选择合适的窗口大小。\n");

    view_pause();
}

void view_handle_preprocess(AppState* state) {
    int running = 1;
    while (running) {
        view_clear_screen();
        printf("========================================\n");
        printf("  数据预处理\n");
        printf("========================================\n");
        printf("  [1] 执行预处理（异常检测 + 缺失值填充）\n");
        printf("  [2] 移动平均滤波（选择窗口大小）\n");
        printf("  [0] 返回主菜单\n");
        printf("========================================\n");

        int choice = view_read_int("请选择 (0-2): ", 0, 2);
        switch (choice) {
            case 0: running = 0; break;
            case 1: view_do_preprocess(state); break;
            case 2: view_do_filter(state); break;
        }
    }
}

/* ════════════════════════════════════════════════════════════════════
   statistics sub-menu
   ════════════════════════════════════════════════════════════════════ */

static void view_basic_stats(AppState* state) {
    view_clear_screen();
    view_print_header("基本统计量");

    DataSet* src = state->is_preprocessed ? &state->clean_dataset : &state->current_dataset;
    BasicStats stats[WQ_FIELD_COUNT];
    data_service_compute_basic_stats(src, stats);

    const FieldMeta* fm = data_service_fields();
    printf("  %-12s %10s %10s %10s %10s %8s\n", "参数", "均值", "最小值", "最大值", "标准差", "记录数");
    printf("  -------------------------------------------------------------\n");
    for (int i = 0; i < WQ_FIELD_COUNT; i++) {
        printf("  %-12s %10.4f %10.4f %10.4f %10.4f %8d\n",
               fm[i].label, stats[i].mean, stats[i].min_value,
               stats[i].max_value, stats[i].stddev, stats[i].count);
    }
    view_pause();
}

static void view_correlation(AppState* state) {
    view_clear_screen();
    view_print_header("相关性分析");

    DataSet* src = state->is_preprocessed ? &state->clean_dataset : &state->current_dataset;
    double corr[WQ_FIELD_COUNT][WQ_FIELD_COUNT];
    for (int i = 0; i < WQ_FIELD_COUNT; i++)
        for (int j = 0; j < WQ_FIELD_COUNT; j++)
            corr[i][j] = data_service_pearson(src, i, j);

    const FieldMeta* fm = data_service_fields();
    printf("  %-12s", "");
    for (int i = 0; i < WQ_FIELD_COUNT; i++) printf(" %10s", fm[i].key);
    printf("\n");
    for (int i = 0; i < WQ_FIELD_COUNT; i++) {
        printf("  %-12s", fm[i].key);
        for (int j = 0; j < WQ_FIELD_COUNT; j++) printf(" %10.4f", corr[i][j]);
        printf("\n");
    }

    /* strongest correlations */
    double max_pos = -2, max_neg = 2;
    int pi = 0, pj = 0, ni = 0, nj = 0;
    for (int i = 0; i < WQ_FIELD_COUNT; i++) {
        for (int j = 0; j < WQ_FIELD_COUNT; j++) {
            if (i == j) continue;
            if (corr[i][j] > max_pos) { max_pos = corr[i][j]; pi = i; pj = j; }
            if (corr[i][j] < max_neg) { max_neg = corr[i][j]; ni = i; nj = j; }
        }
    }
    printf("\n  最强正相关: %s - %s (r = %.4f)\n", fm[pi].label, fm[pj].label, max_pos);
    printf("  最强负相关: %s - %s (r = %.4f)\n", fm[ni].label, fm[nj].label, max_neg);

    /* key relationships */
    int pairs[4][2] = {{0,3}, {2,3}, {5,0}, {0,1}};
    const char* pair_names[] = {"水温-DO", "pH-DO", "气温-水温", "水温-盐度"};
    printf("\n  关键参数相关性:\n");
    for (int k = 0; k < 4; k++) {
        double r = corr[pairs[k][0]][pairs[k][1]];
        const char* strength = "极弱相关";
        double ar = fabs(r);
        if (ar >= 0.8) strength = "极强相关";
        else if (ar >= 0.6) strength = "强相关";
        else if (ar >= 0.4) strength = "中等相关";
        else if (ar >= 0.2) strength = "弱相关";
        printf("    %s: r = %.4f (%s, %s)\n", pair_names[k], r,
               r > 0 ? "正相关" : "负相关", strength);
    }

    view_pause();
}

static void view_generate_stats_report(AppState* state) {
    view_clear_screen();
    view_print_header("生成统计分析报告");

    DataSet* src = state->is_preprocessed ? &state->clean_dataset : &state->current_dataset;
    BasicStats stats[WQ_FIELD_COUNT];
    double corr[WQ_FIELD_COUNT][WQ_FIELD_COUNT];
    data_service_compute_basic_stats(src, stats);
    for (int i = 0; i < WQ_FIELD_COUNT; i++)
        for (int j = 0; j < WQ_FIELD_COUNT; j++)
            corr[i][j] = data_service_pearson(src, i, j);

    if (data_service_generate_stats_report("reports/stats_report.txt", stats, corr))
        printf("统计分析报告已生成: reports/stats_report.txt\n");
    else
        printf("报告生成失败！请确保 reports/ 目录存在。\n");
    view_pause();
}

void view_handle_statistics(AppState* state) {
    int running = 1;
    while (running) {
        view_clear_screen();
        printf("========================================\n");
        printf("  统计分析\n");
        printf("========================================\n");
        printf("  [1] 基本统计量（均值/最值/标准差）\n");
        printf("  [2] 相关性分析（6x6矩阵）\n");
        printf("  [3] 生成统计分析报告\n");
        printf("  [0] 返回主菜单\n");
        printf("========================================\n");

        int choice = view_read_int("请选择 (0-3): ", 0, 3);
        switch (choice) {
            case 0: running = 0; break;
            case 1: view_basic_stats(state); break;
            case 2: view_correlation(state); break;
            case 3: view_generate_stats_report(state); break;
        }
    }
}

/* ════════════════════════════════════════════════════════════════════
   prediction
   ════════════════════════════════════════════════════════════════════ */

void view_handle_prediction(AppState* state) {
    view_clear_screen();
    view_print_header("预测分析");

    DataSet* src = state->is_preprocessed ? &state->clean_dataset : &state->current_dataset;
    int candidate_fields[] = {0, 1, 2, 5};
    RegressionResult air = data_service_linear_regression(src, 5);

    const FieldMeta* fm = data_service_fields();
    printf("\n  目标变量: 溶解氧 (DO)\n\n");
    printf("  主模型 (气温 -> DO):\n");
    printf("    回归方程: DO = %.6f * Air_temp + %.6f\n", air.slope, air.intercept);
    printf("    决定系数 R^2: %.6f\n", air.r2);
    printf("    均方根误差 RMSE: %.6f\n", air.rmse);

    printf("\n  多因子对比:\n");
    printf("  %-16s %12s %12s %12s %12s\n", "自变量", "斜率", "截距", "R^2", "RMSE");
    printf("  -------------------------------------------------------------\n");
    for (int i = 0; i < 4; i++) {
        RegressionResult r = data_service_linear_regression(src, candidate_fields[i]);
        printf("  %-16s %12.6f %12.6f %12.6f %12.6f\n",
               fm[candidate_fields[i]].label, r.slope, r.intercept, r.r2, r.rmse);
    }

    /* find best R² */
    int best_idx = 5; double best_r2 = air.r2;
    for (int i = 0; i < 4; i++) {
        RegressionResult r = data_service_linear_regression(src, candidate_fields[i]);
        if (r.r2 > best_r2) { best_r2 = r.r2; best_idx = candidate_fields[i]; }
    }
    printf("\n  结论: %s 对溶解氧 (DO) 的影响最大 (R^2 = %.6f)\n", fm[best_idx].label, best_r2);
    printf("\n  分析讨论：\n");
    printf("  单因素线性回归假设变量之间是线性关系，但实际水质参数之间的关系\n");
    printf("  往往是非线性的，且受多种因素共同影响。因此单因素模型的预测准确度\n");
    printf("  有限。如需更准确的预测，应考虑多因素回归、时间序列分析等方法。\n");

    if (view_confirm("\n是否生成预测报告？")) {
        RegressionResult models[4];
        for (int i = 0; i < 4; i++)
            models[i] = data_service_linear_regression(src, candidate_fields[i]);
        if (data_service_generate_predict_report("reports/predict_report.txt", &air, models))
            printf("预测报告已生成: reports/predict_report.txt\n");
        else
            printf("报告生成失败！请确保 reports/ 目录存在。\n");
    }

    view_pause();
}

/* ════════════════════════════════════════════════════════════════════
   overview
   ════════════════════════════════════════════════════════════════════ */

void view_handle_overview(AppState* state) {
    view_clear_screen();
    view_print_header("数据概览");

    int abnormal = 0, missing = 0, valid = 0;
    for (int i = 0; i < state->current_dataset.size; i++) {
        int row_abnormal = 0, row_missing = 0;
        for (int field = 0; field < WQ_FIELD_COUNT; field++) {
            double value = data_service_get_field_value(&state->current_dataset.items[i], field);
            if (isnan(value)) row_missing = 1;
            else if (!data_service_is_field_in_range(field, value)) row_abnormal = 1;
        }
        if (row_abnormal) abnormal++;
        if (row_missing) missing++;
        if (!row_abnormal && !row_missing) valid++;
    }

    printf("  当前文件:     %s\n", state->current_file_path);
    printf("  总记录数:     %d\n", state->current_dataset.size);
    printf("  有效记录数:   %d\n", valid);
    printf("  异常记录数:   %d\n", abnormal);
    printf("  含缺失值记录: %d\n", missing);

    if (state->is_preprocessed) {
        printf("\n  预处理状态:   已完成\n");
        printf("  清洗后记录数: %d\n", state->clean_dataset.size);
        printf("  清洗文件:     %s\n", state->clean_file_path);
    } else {
        printf("\n  预处理状态:   未执行\n");
    }

    view_pause();
}

/* ════════════════════════════════════════════════════════════════════
   warnings
   ════════════════════════════════════════════════════════════════════ */

void view_handle_warnings(AppState* state) {
    view_clear_screen();
    view_print_header("预警报告");

    DataSet* src = state->is_preprocessed ? &state->clean_dataset : &state->current_dataset;
    WarningSet warnings;
    data_service_detect_warnings(src, &warnings);

    if (warnings.size == 0) {
        printf("  未检测到预警。\n");
    } else {
        printf("  共检测到 %d 条预警:\n\n", warnings.size);
        printf("  %-22s %-18s %10s   %s\n", "时间", "类型", "数值", "处理建议");
        printf("  -------------------------------------------------------------\n");
        for (int i = 0; i < warnings.size; i++) {
            const char* type_cn = "未知";
            if (strcmp(warnings.items[i].type, "severe_low_oxygen") == 0) type_cn = "严重缺氧";
            else if (strcmp(warnings.items[i].type, "low_oxygen") == 0) type_cn = "亚缺氧";
            else if (strcmp(warnings.items[i].type, "salinity_drop_1h") == 0) type_cn = "盐度突变(1h)";
            else if (strcmp(warnings.items[i].type, "salinity_drop_24h") == 0) type_cn = "盐度突变(24h)";
            printf("  %-22s %-18s %10.4f   %s\n",
                   warnings.items[i].time, type_cn, warnings.items[i].value, warnings.items[i].advice);
        }
    }

    free(warnings.items);

    if (view_confirm("\n是否生成预警报告文件？")) {
        data_service_detect_warnings(src, &warnings);
        if (data_service_generate_warning_report("reports/warning_report.txt", &warnings))
            printf("预警报告已生成: reports/warning_report.txt\n");
        else
            printf("报告生成失败！请确保 reports/ 目录存在。\n");
        free(warnings.items);
    }

    view_pause();
}

/* ════════════════════════════════════════════════════════════════════
   reports
   ════════════════════════════════════════════════════════════════════ */

void view_handle_reports(AppState* state) {
    view_clear_screen();
    view_print_header("分析报告");

    DataSet* src = state->is_preprocessed ? &state->clean_dataset : &state->current_dataset;

    printf("  [1] 生成数据概览报告\n");
    printf("  [2] 生成统计分析报告\n");
    printf("  [3] 生成预警报告\n");
    printf("  [4] 生成预测报告\n");
    printf("  [5] 生成全部报告\n");
    printf("  [0] 返回\n");

    int choice = view_read_int("请选择 (0-5): ", 0, 5);
    if (choice == 0) return;

    view_clear_screen();
    view_print_header("生成报告");

#ifdef _WIN32
    CreateDirectoryA("reports", NULL);
#else
    mkdir("reports", 0755);
#endif

    if (choice == 1 || choice == 5) {
        ReadSummary summary = {0};
        data_service_generate_overview_report("reports/overview_report.txt", src, &summary, NULL);
        printf("  [OK] 数据概览报告: reports/overview_report.txt\n");
    }
    if (choice == 2 || choice == 5) {
        BasicStats stats[WQ_FIELD_COUNT]; double corr[WQ_FIELD_COUNT][WQ_FIELD_COUNT];
        data_service_compute_basic_stats(src, stats);
        for (int i = 0; i < WQ_FIELD_COUNT; i++)
            for (int j = 0; j < WQ_FIELD_COUNT; j++)
                corr[i][j] = data_service_pearson(src, i, j);
        data_service_generate_stats_report("reports/stats_report.txt", stats, corr);
        printf("  [OK] 统计分析报告: reports/stats_report.txt\n");
    }
    if (choice == 3 || choice == 5) {
        WarningSet warnings;
        data_service_detect_warnings(src, &warnings);
        data_service_generate_warning_report("reports/warning_report.txt", &warnings);
        free(warnings.items);
        printf("  [OK] 预警报告: reports/warning_report.txt\n");
    }
    if (choice == 4 || choice == 5) {
        int candidate_fields[] = {0, 1, 2, 5};
        RegressionResult air = data_service_linear_regression(src, 5);
        RegressionResult models[4];
        for (int i = 0; i < 4; i++)
            models[i] = data_service_linear_regression(src, candidate_fields[i]);
        data_service_generate_predict_report("reports/predict_report.txt", &air, models);
        printf("  [OK] 预测报告: reports/predict_report.txt\n");
    }

    printf("\n报告生成完毕！\n");
    view_pause();
}

/* ════════════════════════════════════════════════════════════════════
   backup & restore
   ════════════════════════════════════════════════════════════════════ */

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
                char** list = data_service_list_backups("backup", &count);
                if (list == NULL || count == 0) {
                    printf("  暂无备份文件。\n");
                } else {
                    for (int i = 0; i < count; i++) {
                        printf("  [%d] %s\n", i + 1, list[i]);
                        free(list[i]);
                    }
                    free(list);
                }
                view_pause();
                break;
            }
            case 3: {
                view_clear_screen();
                view_print_header("从备份恢复");
                int count = 0;
                char** list = data_service_list_backups("backup", &count);
                if (list == NULL || count == 0) {
                    printf("  暂无备份文件。\n");
                } else {
                    for (int i = 0; i < count; i++) {
                        printf("  [%d] %s\n", i + 1, list[i]);
                    }
                    int idx = view_read_int("请选择要恢复的备份编号 (0=取消): ", 0, count);
                    if (idx > 0) {
                        if (!view_confirm("确定要恢复此备份？当前数据将被替换！")) {
                            for (int i = 0; i < count; i++) free(list[i]);
                            free(list);
                            break;
                        }
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
                    for (int i = 0; i < count; i++) free(list[i]);
                    free(list);
                }
                view_pause();
                break;
            }
        }
    }
}

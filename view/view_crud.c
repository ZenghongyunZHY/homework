//
// view_crud.c - 数据基础操作子菜单（查询/修改/删除/添加/存储对比）
//

#include "DataView.h"
#include "DataViewInternal.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
        printf("  [B] 返回上级菜单 (有效记录: %d)\n", state->clean_dataset.size);
        printf("========================================\n");

        printf("请选择: ");
        char cmd[16];
        view_read_line(cmd, sizeof(cmd));

        if (cmd[0] == 'N' || cmd[0] == 'n') {
            if (page < qp.total_pages) page++;
        } else if (cmd[0] == 'P' || cmd[0] == 'p') {
            if (page > 1) page--;
        } else if (cmd[0] == 'J' || cmd[0] == 'j') {
            page = view_read_int("跳转到页: ", 1, qp.total_pages);
        } else if (cmd[0] == 'F' || cmd[0] == 'f') {
            view_print_field_options();
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
            view_print_field_options();
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
            strcpy(view_mode, strcmp(view_mode, "raw") == 0 ? "processed" : "raw");
            page = 1;
        } else if (cmd[0] == 'B' || cmd[0] == 'b') {
            running = 0;
        }

        free(qp.items);
    }
}

static void view_modify_data(AppState* state) {
    view_clear_screen();
    view_print_header("数据修改");

    int row = view_read_int("请输入要修改的记录号: ", 1, state->current_dataset.size);

    Data* found = NULL;
    for (int i = 0; i < state->current_dataset.size; i++) {
        if (state->current_dataset.items[i].record_index == row) { found = &state->current_dataset.items[i]; break; }
    }
    if (found == NULL) { printf("未找到记录号 %d。\n", row); view_pause(); return; }

    printf("\n当前记录:\n");
    printf("  水温: %.4f | 盐度: %.4f | pH: %.2f | DO: %.4f | 降水: %.6f | 气温: %.4f\n",
           found->temp, found->salinity, found->ph, found->do_value, found->precipitation, found->air_temp);

    view_print_field_options();
    char field_name[32];
    printf("请输入要修改的参数名: ");
    view_read_line(field_name, sizeof(field_name));
    int field = data_service_field_index(field_name);
    if (field < 0) { printf("未知参数: %s\n", field_name); view_pause(); return; }

    const FieldMeta* fm = data_service_fields();
    double new_value = view_read_double("请输入新值: ", fm[field].min_value, fm[field].max_value);

    if (!view_confirm("确认修改？")) return;

    double old_value = NAN;
    if (!controller_modify_record(state, row, field, new_value, &old_value)) {
        printf("修改失败。\n"); view_pause(); return;
    }

    printf("修改成功！%s: %.4f -> %.4f\n", fm[field].label, old_value, new_value);
    view_prompt_save_changes(state);
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
        view_print_field_options();
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

    view_prompt_save_changes(state);
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

    view_prompt_save_changes(state);
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

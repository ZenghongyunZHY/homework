//
// view_analyze.c - 预处理、统计、预测、概览、预警子菜单
//

#include "DataView.h"
#include "DataViewInternal.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── 预处理 ── */

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

/* ── 统计 ── */

static void view_basic_stats(AppState* state) {
    view_clear_screen();
    view_print_header("基本统计量");

    DataSet* src = view_get_active_dataset(state);
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

    DataSet* src = view_get_active_dataset(state);
    double corr[WQ_FIELD_COUNT][WQ_FIELD_COUNT];
    view_compute_correlation(src, corr);

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

    DataSet* src = view_get_active_dataset(state);
    BasicStats stats[WQ_FIELD_COUNT];
    double corr[WQ_FIELD_COUNT][WQ_FIELD_COUNT];
    data_service_compute_basic_stats(src, stats);
    view_compute_correlation(src, corr);

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

/* ── 预测 ── */

void view_handle_prediction(AppState* state) {
    view_clear_screen();
    view_print_header("预测分析");

    DataSet* src = view_get_active_dataset(state);
    int candidate_fields[] = {0, 1, 2, 5};
    RegressionResult air = data_service_linear_regression(src, 5);

    const FieldMeta* fm = data_service_fields();
    printf("\n  目标变量: 溶解氧 (DO)\n\n");
    printf("  主模型 (气温 -> DO):\n");
    printf("    回归方程: DO = %.6f * Air_temp + %.6f\n", air.slope, air.intercept);
    printf("    决定系数 R^2: %.6f\n", air.r2);
    printf("    均方根误差 RMSE: %.6f\n", air.rmse);

    RegressionResult models[4];
    printf("\n  多因子对比:\n");
    printf("  %-16s %12s %12s %12s %12s\n", "自变量", "斜率", "截距", "R^2", "RMSE");
    printf("  -------------------------------------------------------------\n");
    int best_idx = 5; double best_r2 = air.r2;
    for (int i = 0; i < 4; i++) {
        models[i] = data_service_linear_regression(src, candidate_fields[i]);
        printf("  %-16s %12.6f %12.6f %12.6f %12.6f\n",
               fm[candidate_fields[i]].label, models[i].slope, models[i].intercept, models[i].r2, models[i].rmse);
        if (models[i].r2 > best_r2) { best_r2 = models[i].r2; best_idx = candidate_fields[i]; }
    }

    printf("\n  结论: %s 对溶解氧 (DO) 的影响最大 (R^2 = %.6f)\n", fm[best_idx].label, best_r2);
    printf("\n  分析讨论：\n");
    printf("  单因素线性回归假设变量之间是线性关系，但实际水质参数之间的关系\n");
    printf("  往往是非线性的，且受多种因素共同影响。因此单因素模型的预测准确度\n");
    printf("  有限。如需更准确的预测，应考虑多因素回归、时间序列分析等方法。\n");

    if (view_confirm("\n是否生成预测报告？")) {
        if (data_service_generate_predict_report("reports/predict_report.txt", &air, models))
            printf("预测报告已生成: reports/predict_report.txt\n");
        else
            printf("报告生成失败！请确保 reports/ 目录存在。\n");
    }

    view_pause();
}

/* ── 概览 ── */

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

/* ── 预警 ── */

void view_handle_warnings(AppState* state) {
    view_clear_screen();
    view_print_header("预警报告");

    DataSet* src = view_get_active_dataset(state);
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

    int generate = view_confirm("\n是否生成预警报告文件？");
    if (generate) {
        if (data_service_generate_warning_report("reports/warning_report.txt", &warnings))
            printf("预警报告已生成: reports/warning_report.txt\n");
        else
            printf("报告生成失败！请确保 reports/ 目录存在。\n");
    }
    free(warnings.items);

    view_pause();
}

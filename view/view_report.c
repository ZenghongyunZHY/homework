//
// view_report.c - 分析报告生成子菜单
//

#include "DataView.h"
#include "DataViewInternal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#endif

void view_handle_reports(AppState* state) {
    view_clear_screen();
    view_print_header("分析报告");

    DataSet* src = view_get_active_dataset(state);

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
        view_compute_correlation(src, corr);
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

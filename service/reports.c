//
// reports.c - 报告文件生成
//

#include "DataService.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

static FILE* report_open(const char* path) {
    FILE* fp = fopen(path, "w");
    if (fp == NULL) return NULL;
    time_t now = time(NULL);
    struct tm tm_now;
#ifdef _WIN32
    localtime_s(&tm_now, &now);
#else
    localtime_r(&now, &tm_now);
#endif
    fprintf(fp, "========================================\n");
    fprintf(fp, "  海水养殖水质数据分析系统 - 报告\n");
    fprintf(fp, "  生成时间: %04d-%02d-%02d %02d:%02d:%02d\n",
            tm_now.tm_year + 1900, tm_now.tm_mon + 1, tm_now.tm_mday,
            tm_now.tm_hour, tm_now.tm_min, tm_now.tm_sec);
    fprintf(fp, "========================================\n\n");
    return fp;
}

static void report_close(FILE* fp) {
    fprintf(fp, "\n========================================\n");
    fclose(fp);
}

int data_service_generate_overview_report(const char* path, const DataSet* set,
    const ReadSummary* summary, const PreprocessResult* preprocess) {
    FILE* fp = report_open(path);
    if (fp == NULL) return 0;
    fprintf(fp, "【数据概览】\n\n");
    fprintf(fp, "  当前记录数:     %d\n", set->size);
    if (summary) {
        fprintf(fp, "  原始总记录数:   %d\n", summary->total_records);
        fprintf(fp, "  成功解析记录数: %d\n", summary->parsed_records);
        fprintf(fp, "  格式错误记录数: %d\n", summary->format_errors);
        fprintf(fp, "  缺失值个数:     %d\n", summary->missing_values);
    }
    if (preprocess) {
        fprintf(fp, "\n【预处理结果】\n\n");
        fprintf(fp, "  异常记录数:     %d\n", preprocess->abnormal_records);
        fprintf(fp, "  异常值个数:     %d\n", preprocess->abnormal_values);
        fprintf(fp, "  删除记录数:     %d\n", preprocess->deleted_records);
        fprintf(fp, "  填充缺失值数:   %d\n", preprocess->filled_values);
        fprintf(fp, "  保留记录数:     %d\n", preprocess->kept_records);
    }
    report_close(fp);
    return 1;
}

int data_service_generate_stats_report(const char* path,
    const BasicStats stats[WQ_FIELD_COUNT], double corr[WQ_FIELD_COUNT][WQ_FIELD_COUNT]) {
    FILE* fp = report_open(path);
    if (fp == NULL) return 0;
    fprintf(fp, "【基本统计量】\n\n");
    fprintf(fp, "%-16s %10s %10s %10s %10s\n", "参数", "均值", "最小值", "最大值", "标准差");
    fprintf(fp, "------------------------------------------------------------\n");
    const FieldMeta* fields = data_service_fields();
    for (int i = 0; i < WQ_FIELD_COUNT; i++) {
        fprintf(fp, "%-16s %10.4f %10.4f %10.4f %10.4f\n",
                fields[i].label, stats[i].mean, stats[i].min_value,
                stats[i].max_value, stats[i].stddev);
    }

    fprintf(fp, "\n【6x6 相关系数矩阵】\n\n");
    fprintf(fp, "%-12s", "");
    for (int i = 0; i < WQ_FIELD_COUNT; i++) fprintf(fp, " %10s", fields[i].key);
    fprintf(fp, "\n");
    for (int i = 0; i < WQ_FIELD_COUNT; i++) {
        fprintf(fp, "%-12s", fields[i].key);
        for (int j = 0; j < WQ_FIELD_COUNT; j++) fprintf(fp, " %10.4f", corr[i][j]);
        fprintf(fp, "\n");
    }

    /* find strongest correlations */
    double max_pos = -2, max_neg = 2;
    int pi = 0, pj = 0, ni = 0, nj = 0;
    for (int i = 0; i < WQ_FIELD_COUNT; i++) {
        for (int j = 0; j < WQ_FIELD_COUNT; j++) {
            if (i == j) continue;
            if (corr[i][j] > max_pos) { max_pos = corr[i][j]; pi = i; pj = j; }
            if (corr[i][j] < max_neg) { max_neg = corr[i][j]; ni = i; nj = j; }
        }
    }
    fprintf(fp, "\n【相关性结论】\n\n");
    fprintf(fp, "  最强正相关: %s - %s (r = %.4f)\n", fields[pi].label, fields[pj].label, max_pos);
    fprintf(fp, "  最强负相关: %s - %s (r = %.4f)\n", fields[ni].label, fields[nj].label, max_neg);
    report_close(fp);
    return 1;
}

int data_service_generate_warning_report(const char* path, const WarningSet* warnings) {
    FILE* fp = report_open(path);
    if (fp == NULL) return 0;
    fprintf(fp, "【预警报告】\n\n");
    fprintf(fp, "  预警总数: %d\n\n", warnings->size);
    fprintf(fp, "%-22s %-24s %10s   %s\n", "时间", "类型", "数值", "处理建议");
    fprintf(fp, "------------------------------------------------------------------------\n");
    for (int i = 0; i < warnings->size; i++) {
        const char* type_cn = "未知";
        if (strcmp(warnings->items[i].type, "severe_low_oxygen") == 0) type_cn = "严重缺氧";
        else if (strcmp(warnings->items[i].type, "low_oxygen") == 0) type_cn = "亚缺氧";
        else if (strcmp(warnings->items[i].type, "salinity_drop_1h") == 0) type_cn = "盐度突变(1h)";
        else if (strcmp(warnings->items[i].type, "salinity_drop_24h") == 0) type_cn = "盐度突变(24h)";
        fprintf(fp, "%-22s %-24s %10.4f   %s\n",
                warnings->items[i].time, type_cn, warnings->items[i].value, warnings->items[i].advice);
    }
    report_close(fp);
    return 1;
}

int data_service_generate_predict_report(const char* path,
    const RegressionResult* primary, const RegressionResult models[4]) {
    FILE* fp = report_open(path);
    if (fp == NULL) return 0;
    const FieldMeta* fields = data_service_fields();
    fprintf(fp, "【预测模型报告】\n\n");
    fprintf(fp, "  目标变量: 溶解氧 (DO)\n\n");
    fprintf(fp, "  主模型 (气温 -> DO):\n");
    fprintf(fp, "    回归方程: DO = %.6f * Air_temp + %.6f\n", primary->slope, primary->intercept);
    fprintf(fp, "    R^2:      %.6f\n", primary->r2);
    fprintf(fp, "    RMSE:     %.6f\n\n", primary->rmse);

    fprintf(fp, "【多因子对比】\n\n");
    fprintf(fp, "%-16s %12s %12s %12s %12s\n", "自变量", "斜率", "截距", "R^2", "RMSE");
    fprintf(fp, "------------------------------------------------------------\n");
    int candidate_fields[] = {0, 1, 2, 5};
    for (int i = 0; i < 4; i++) {
        fprintf(fp, "%-16s %12.6f %12.6f %12.6f %12.6f\n",
                fields[candidate_fields[i]].label,
                models[i].slope, models[i].intercept, models[i].r2, models[i].rmse);
    }
    report_close(fp);
    return 1;
}

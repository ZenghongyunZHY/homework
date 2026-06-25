//
// analyze_driver.c - 对真实数据跑完整分析，输出报告所需的真实数值
//
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../service/DataService.h"

static const char* FNAME[WQ_FIELD_COUNT] = {"Temp","Salinity","pH","DO","Precip","AirTemp"};

int main(void) {
    DataSet raw; ReadSummary sum;
    if (!data_service_read_csv("dao/data_modify.csv", &raw, &sum)) {
        printf("read fail\n"); return 1;
    }
    printf("RAW total=%d parsed=%d fmt_err=%d missing=%d size=%d\n",
           sum.total_records, sum.parsed_records, sum.format_errors, sum.missing_values, raw.size);

    /* preprocess */
    DataSet clean; PreprocessResult pr;
    data_service_preprocess(&raw, &clean, &pr);
    printf("PRE total=%d kept=%d deleted=%d abnormal_recs=%d abnormal_vals=%d filled=%d missing=%d\n",
           pr.total_records, pr.kept_records, pr.deleted_records,
           pr.abnormal_records, pr.abnormal_values, pr.filled_values, pr.missing_values);

    /* basic stats on clean */
    BasicStats st[WQ_FIELD_COUNT];
    data_service_compute_basic_stats(&clean, st);
    printf("STATS field mean min max stddev count\n");
    for (int i=0;i<WQ_FIELD_COUNT;i++)
        printf("  %s %.4f %.4f %.4f %.4f %d\n", FNAME[i], st[i].mean, st[i].min_value, st[i].max_value, st[i].stddev, st[i].count);

    /* correlation matrix */
    printf("CORR 6x6\n");
    printf("        ");
    for (int j=0;j<WQ_FIELD_COUNT;j++) printf("%10s", FNAME[j]);
    printf("\n");
    double corr[WQ_FIELD_COUNT][WQ_FIELD_COUNT];
    for (int i=0;i<WQ_FIELD_COUNT;i++) {
        printf("%8s", FNAME[i]);
        for (int j=0;j<WQ_FIELD_COUNT;j++) {
            corr[i][j] = data_service_pearson(&clean, i, j);
            printf("%10.4f", corr[i][j]);
        }
        printf("\n");
    }
    printf("CORR_KEY temp-DO=%.4f pH-DO=%.4f temp-air=%.4f temp-sal=%.4f\n",
           corr[0][3], corr[2][3], corr[0][5], corr[0][1]);

    /* regression multi-factor */
    printf("REGRESS field slope intercept r2 rmse count\n");
    int fields[4] = {0,2,1,5};
    for (int k=0;k<4;k++) {
        RegressionResult r = data_service_linear_regression(&clean, fields[k]);
        printf("  %s slope=%.6f intercept=%.6f r2=%.6f rmse=%.6f n=%d\n",
               FNAME[fields[k]], r.slope, r.intercept, r.r2, r.rmse, r.count);
    }

    /* moving average filter windows 3,5,7,9,11 on temp/DO/pH/salinity */
    printf("FILTER window temp_std_before temp_std_after do_before do_after\n");
    int windows[5] = {3,5,7,9,11};
    for (int w=0;w<5;w++) {
        DataSet flt; BasicStats b[WQ_FIELD_COUNT], a[WQ_FIELD_COUNT];
        data_service_moving_average_filter(&clean, windows[w], &flt, b, a);
        printf("  win=%d temp_b=%.4f temp_a=%.4f do_b=%.4f do_a=%.4f ph_b=%.4f ph_a=%.4f sal_b=%.4f sal_a=%.4f\n",
               windows[w], b[0].stddev, a[0].stddev, b[3].stddev, a[3].stddev,
               b[2].stddev, a[2].stddev, b[1].stddev, a[1].stddev);
        data_service_dataset_free(&flt);
    }

    /* storage benchmark */
    StorageBenchmark bm = data_service_benchmark_storage("dao/data_modify.csv");
    printf("STORAGE csv_size=%ld bin_size=%ld csv_write=%.4f csv_read=%.4f bin_write=%.4f bin_read=%.4f\n",
           bm.csv_size_bytes, bm.bin_size_bytes, bm.csv_write_seconds, bm.csv_read_seconds,
           bm.bin_write_seconds, bm.bin_read_seconds);

    /* warnings count */
    WarningSet ws;
    int wn = data_service_detect_warnings(&clean, &ws);
    printf("WARNINGS total=%d\n", wn);
    free(ws.items);

    data_service_dataset_free(&raw);
    data_service_dataset_free(&clean);
    return 0;
}

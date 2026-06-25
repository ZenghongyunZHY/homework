//
// test_suite.c - 海水养殖水质分析系统 单元/集成测试
//
// 编译:
//   gcc -std=c11 -I. test/test_suite.c service/dataset.c service/field_meta.c \
//       service/io_csv.c service/auth.c service/analyze.c service/io_marks.c \
//       service/reports.c service/storage_backup.c -lm -o test/test_suite.exe
//

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../service/DataService.h"

static int g_pass = 0;
static int g_fail = 0;

#define ASSERT_TRUE(cond, msg) do { \
    if (cond) { g_pass++; printf("  [PASS] %s\n", msg); } \
    else { g_fail++; printf("  [FAIL] %s (line %d)\n", msg, __LINE__); } \
} while (0)

#define ASSERT_DOUBLE_NEAR(actual, expected, tol, msg) do { \
    int _ok = (isnan(expected) && isnan(actual)) || \
              (!isnan(expected) && !isnan(actual) && fabs((actual)-(expected)) <= (tol)); \
    if (_ok) { g_pass++; printf("  [PASS] %s\n", msg); } \
    else { g_fail++; printf("  [FAIL] %s (got=%.6f exp=%.6f line %d)\n", \
         msg, (double)(actual), (double)(expected), __LINE__); } \
} while (0)

static Data mk(double t, double s, double ph, double dox, double p, double at) {
    Data d; memset(&d, 0, sizeof(d));
    d.temp = t; d.salinity = s; d.ph = ph; d.do_value = dox;
    d.precipitation = p; d.air_temp = at;
    return d;
}

/* ------------------------------------------------------------------ */
/* 1. 登录校验                                                          */
/* ------------------------------------------------------------------ */
static void test_login(void) {
    printf("\n[1] 登录校验 data_service_validate_login\n");

    /* 正确用例 */
    User* u1 = data_service_validate_login("admin", "123456");
    ASSERT_TRUE(u1 != NULL && u1->role == 1, "admin/123456 应登录成功且为管理员(role=1)");

    User* u2 = data_service_validate_login("guest", "guest");
    ASSERT_TRUE(u2 != NULL && u2->role == 0, "guest/guest 应登录成功且为访客(role=0)");

    /* 错误用例 */
    ASSERT_TRUE(data_service_validate_login("admin", "wrong") == NULL, "错误密码应返回 NULL");
    ASSERT_TRUE(data_service_validate_login("unknown", "123456") == NULL, "不存在的用户应返回 NULL");
    ASSERT_TRUE(data_service_validate_login("", "") == NULL, "空用户名空密码应返回 NULL");
    ASSERT_TRUE(data_service_validate_login("Admin", "123456") == NULL, "用户名大小写敏感: Admin 应失败");
    ASSERT_TRUE(data_service_validate_login("admin", "123456 ") == NULL, "密码含多余空格应失败(严格匹配)");
}

/* ------------------------------------------------------------------ */
/* 2. 字段元数据与取值范围                                                */
/* ------------------------------------------------------------------ */
static void test_field_meta(void) {
    printf("\n[2] 字段元数据与范围 data_service_is_field_in_range\n");

    const FieldMeta* f = data_service_fields();
    ASSERT_DOUBLE_NEAR(f[0].min_value, -5.0, 1e-9, "temp 下限 -5");
    ASSERT_DOUBLE_NEAR(f[0].max_value, 40.0, 1e-9, "temp 上限 40");
    ASSERT_DOUBLE_NEAR(f[2].min_value, 6.5, 1e-9, "pH 下限 6.5");
    ASSERT_DOUBLE_NEAR(f[3].max_value, 15.0, 1e-9, "DO 上限 15");

    /* 正确用例：边界值在范围内 */
    ASSERT_TRUE(data_service_is_field_in_range(0, -5.0), "temp 边界 -5 应在范围内(含)");
    ASSERT_TRUE(data_service_is_field_in_range(0, 40.0), "temp 边界 40 应在范围内(含)");
    ASSERT_TRUE(data_service_is_field_in_range(2, 7.0), "pH=7 在范围内");

    /* 错误用例：越界 */
    ASSERT_TRUE(!data_service_is_field_in_range(0, -5.1), "temp=-5.1 越界");
    ASSERT_TRUE(!data_service_is_field_in_range(0, 40.1), "temp=40.1 越界");
    ASSERT_TRUE(!data_service_is_field_in_range(2, 6.0), "pH=6.0 越下界");
    ASSERT_TRUE(!data_service_is_field_in_range(3, 16.0), "DO=16 越界");

    /* 错误用例：NAN 与非法字段索引 */
    ASSERT_TRUE(!data_service_is_field_in_range(0, NAN), "NAN 不在范围内");
    ASSERT_TRUE(!data_service_is_field_in_range(-1, 10.0), "字段索引 -1 非法");
    ASSERT_TRUE(!data_service_is_field_in_range(6, 10.0), "字段索引 6 越界");

    /* 字段名解析 */
    ASSERT_TRUE(data_service_field_index("temp") == 0, "字段名 temp -> 0");
    ASSERT_TRUE(data_service_field_index("PH") == 2, "大小写不敏感 PH -> 2");
    ASSERT_TRUE(data_service_field_index("Air temp") == 5, "label 'Air temp' -> 5");
    ASSERT_TRUE(data_service_field_index("do_value") == 3, "别名 do_value -> 3");
    ASSERT_TRUE(data_service_field_index("nope") == -1, "未知字段 -> -1");
    ASSERT_TRUE(data_service_field_index(NULL) == -1, "NULL -> -1");
}

/* ------------------------------------------------------------------ */
/* 3. CSV 解析（含缺失/异常标记）                                         */
/* ------------------------------------------------------------------ */
static void test_csv_parse(void) {
    printf("\n[3] CSV 解析 data_service_read_csv\n");

    /* 正确用例：含缺失标记 nan/-999 与正常值 */
    FILE* fp = fopen("test/_t_parse.csv", "w");
    fprintf(fp, "Temp(degC),Salinity(PSU),pH,DO(mg/l),precipitation(mm),Air_temp(degC)\n");
    fprintf(fp, "25.5,34.0,8.2,8.0,0.0,28.0\n");   /* 正常 */
    fprintf(fp, "nan,34.0,8.2,8.0,0.0,28.0\n");    /* 缺失1 */
    fprintf(fp, "-999,34.0,8.2,8.0,0.0,28.0\n");   /* 缺失1 */
    fprintf(fp, "25.5,34.0,8.2,8.0\n");            /* 列数不足 -> 格式错误 */
    fprintf(fp, "abc,34.0,8.2,8.0,0.0,28.0\n");    /* 非数字 -> 格式错误 */
    fclose(fp);

    DataSet set; ReadSummary sum;
    int ok = data_service_read_csv("test/_t_parse.csv", &set, &sum);
    ASSERT_TRUE(ok == 1, "正常文件应返回1");
    ASSERT_TRUE(sum.total_records == 5, "总记录数=5");
    ASSERT_TRUE(sum.parsed_records == 3, "成功解析3条");
    ASSERT_TRUE(sum.format_errors == 2, "格式错误2条");
    ASSERT_TRUE(sum.missing_values == 2, "缺失值2个(nan + -999)");
    ASSERT_TRUE(set.size == 3, "数据集大小=3");
    ASSERT_TRUE(isnan(set.items[1].temp), "第2行 temp 应为 NAN");
    ASSERT_DOUBLE_NEAR(set.items[0].temp, 25.5, 1e-9, "第1行 temp=25.5");
    data_service_dataset_free(&set);
    remove("test/_t_parse.csv");

    /* 错误用例：文件不存在 */
    DataSet s2; ReadSummary sum2;
    ASSERT_TRUE(data_service_read_csv("test/_not_exist.csv", &s2, &sum2) == 0, "不存在的文件返回0");
    ASSERT_TRUE(s2.size == 0, "失败时数据集为空");
}

/* ------------------------------------------------------------------ */
/* 4. 预处理（异常剔除/修复 + 缺失填充）                                  */
/* ------------------------------------------------------------------ */
static void test_preprocess(void) {
    printf("\n[4] 数据预处理 data_service_preprocess\n");

    DataSet src; data_service_dataset_init(&src);
    /* 0: 正常 */
    data_service_dataset_push(&src, mk(25.0, 34.0, 8.1, 8.0, 0.0, 28.0));
    /* 1: 1个异常(temp=100越界) -> 修复为NAN后填充 */
    data_service_dataset_push(&src, mk(100.0, 34.0, 8.1, 8.0, 0.0, 28.0));
    /* 2: 缺失 temp */
    data_service_dataset_push(&src, mk(NAN, 34.0, 8.1, 8.0, 0.0, 28.0));
    /* 3: 6个字段中3个异常 -> 整条删除 */
    data_service_dataset_push(&src, mk(100.0, 99.0, 99.0, 8.0, 0.0, 28.0));
    /* record_index 未设置, preprocess 不依赖它, 但填充用相邻索引, 需设置以对齐 */
    for (int i = 0; i < src.size; i++) src.items[i].record_index = i + 1;

    DataSet clean; PreprocessResult r;
    int ok = data_service_preprocess(&src, &clean, &r);
    ASSERT_TRUE(ok == 1, "预处理返回1");
    ASSERT_TRUE(r.total_records == 4, "原始记录4条");
    ASSERT_TRUE(r.deleted_records == 1, "删除1条(3异常)");
    ASSERT_TRUE(r.kept_records == 3, "保留3条");
    ASSERT_TRUE(r.abnormal_records == 2, "含异常字段记录2条");
    ASSERT_TRUE(r.abnormal_values >= 4, "异常值>=4 (100temp + 99sal + 99ph + 100temp)");
    ASSERT_TRUE(r.missing_values == 1, "原始缺失1个(temp NAN)");
    ASSERT_TRUE(r.filled_values >= 2, "填充值>=2 (异常修复后temp + 原缺失temp)");
    /* 清洗后不应再含 NAN */
    int has_nan = 0;
    for (int i = 0; i < clean.size && !has_nan; i++)
        for (int fld = 0; fld < WQ_FIELD_COUNT; fld++)
            if (isnan(data_service_get_field_value(&clean.items[i], fld))) { has_nan = 1; break; }
    ASSERT_TRUE(!has_nan, "清洗后数据集无 NAN");
    /* 异常 temp=100 应被修复并填充为相邻均值(约25) */
    ASSERT_TRUE(clean.items[1].temp < 30.0, "异常 temp=100 修复后应接近相邻值");

    data_service_dataset_free(&src);
    data_service_dataset_free(&clean);

    /* 错误用例：空数据集 */
    DataSet empty; data_service_dataset_init(&empty);
    DataSet c2; PreprocessResult r2;
    ASSERT_TRUE(data_service_preprocess(&empty, &c2, &r2) == 1, "空数据集预处理返回1");
    ASSERT_TRUE(r2.total_records == 0 && r2.kept_records == 0, "空数据集结果全0");
    data_service_dataset_free(&c2);
}

/* ------------------------------------------------------------------ */
/* 5. 基本统计量与皮尔逊相关                                              */
/* ------------------------------------------------------------------ */
static void test_stats(void) {
    printf("\n[5] 统计分析 compute_basic_stats / pearson\n");

    DataSet set; data_service_dataset_init(&set);
    data_service_dataset_push(&set, mk(20.0, 33.0, 8.0, 9.0, 0.0, 25.0));
    data_service_dataset_push(&set, mk(22.0, 34.0, 8.1, 8.5, 0.0, 26.0));
    data_service_dataset_push(&set, mk(24.0, 35.0, 8.2, 8.0, 0.0, 27.0));
    data_service_dataset_push(&set, mk(26.0, 36.0, 8.3, 7.5, 0.0, 28.0));

    BasicStats st[WQ_FIELD_COUNT];
    data_service_compute_basic_stats(&set, st);
    ASSERT_DOUBLE_NEAR(st[0].mean, 23.0, 1e-9, "temp 均值=23");
    ASSERT_DOUBLE_NEAR(st[0].min_value, 20.0, 1e-9, "temp 最小=20");
    ASSERT_DOUBLE_NEAR(st[0].max_value, 26.0, 1e-9, "temp 最大=26");
    ASSERT_TRUE(st[0].count == 4, "temp 计数=4");
    ASSERT_TRUE(st[0].stddev > 0, "temp 标准差>0");
    /* temp 与 air_temp 完全线性正相关 -> pearson=1 */
    double r = data_service_pearson(&set, 0, 5);
    ASSERT_DOUBLE_NEAR(r, 1.0, 1e-9, "temp-air_temp 完全正相关 r=1");
    /* temp 与 do 完全线性负相关 -> pearson=-1 */
    double r2 = data_service_pearson(&set, 0, 3);
    ASSERT_DOUBLE_NEAR(r2, -1.0, 1e-9, "temp-DO 完全负相关 r=-1");

    data_service_dataset_free(&set);

    /* 错误用例：单条数据无法计算相关 */
    DataSet one; data_service_dataset_init(&one);
    data_service_dataset_push(&one, mk(20.0, 33.0, 8.0, 9.0, 0.0, 25.0));
    ASSERT_TRUE(isnan(data_service_pearson(&one, 0, 5)), "样本数<2 时 pearson=NAN");
    data_service_dataset_free(&one);
}

/* ------------------------------------------------------------------ */
/* 6. 线性回归预测                                                      */
/* ------------------------------------------------------------------ */
static void test_regression(void) {
    printf("\n[6] 线性回归 data_service_linear_regression\n");

    /* 构造 y = 2x + 1 的完全线性关系 (x=temp, y=DO) */
    DataSet set; data_service_dataset_init(&set);
    for (int i = 0; i < 20; i++) {
        double x = 20.0 + i;
        Data d = mk(x, 33.0, 8.0, 2 * x + 1, 0.0, 25.0);
        d.record_index = i + 1;
        data_service_dataset_push(&set, d);
    }
    RegressionResult res = data_service_linear_regression(&set, 0);
    ASSERT_DOUBLE_NEAR(res.slope, 2.0, 1e-6, "斜率 slope=2");
    ASSERT_DOUBLE_NEAR(res.intercept, 1.0, 1e-6, "截距 intercept=1");
    ASSERT_DOUBLE_NEAR(res.r2, 1.0, 1e-6, "决定系数 R^2=1(完全拟合)");
    ASSERT_TRUE(res.count == 20, "回归样本数=20");
    ASSERT_TRUE(!isnan(res.rmse), "RMSE 可计算(留出法)");
    ASSERT_DOUBLE_NEAR(res.rmse, 0.0, 1e-6, "完全线性 RMSE≈0");

    data_service_dataset_free(&set);

    /* 错误用例：样本不足 */
    DataSet tiny; data_service_dataset_init(&tiny);
    data_service_dataset_push(&tiny, mk(20.0, 33.0, 8.0, 9.0, 0.0, 25.0));
    RegressionResult r2 = data_service_linear_regression(&tiny, 0);
    ASSERT_TRUE(isnan(r2.slope) && isnan(r2.r2), "样本<2 时回归返回 NAN");
    data_service_dataset_free(&tiny);
}

/* ------------------------------------------------------------------ */
/* 7. 查询分页/筛选/排序                                                */
/* ------------------------------------------------------------------ */
static void test_query(void) {
    printf("\n[7] 查询分页 data_service_query_page\n");

    DataSet set; data_service_dataset_init(&set);
    for (int i = 0; i < 40; i++) {
        Data d = mk(20.0 + i, 33.0, 8.0, 8.0, 0.0, 25.0);
        d.record_index = i + 1;
        data_service_dataset_push(&set, d);
    }
    /* 正确用例：每页15条，第1页应15条 */
    QueryPage p1 = data_service_query_page(&set, 1, 15, -1, 0, 0, -1, 0, NULL, NULL, "raw", "all");
    ASSERT_TRUE(p1.total == 40, "总记录40");
    ASSERT_TRUE(p1.total_pages == 3, "总页数3 (40/15向上取整)");
    ASSERT_TRUE(p1.page == 1, "当前页1");
    ASSERT_TRUE(p1.page_size == 15, "页大小15");

    /* 正确用例：越界页码自动修正到末页 */
    QueryPage p9 = data_service_query_page(&set, 9, 15, -1, 0, 0, -1, 0, NULL, NULL, "raw", "all");
    ASSERT_TRUE(p9.page == 3, "page=9 自动修正为末页3");
    free(p1.items); free(p9.items);

    /* 正确用例：范围筛选 temp in [25,27] */
    QueryPage pf = data_service_query_page(&set, 1, 100, 0, 25.0, 27.0, -1, 0, NULL, NULL, "raw", "all");
    ASSERT_TRUE(pf.total == 3, "temp∈[25,27] 共3条");
    free(pf.items);

    /* 正确用例：按 temp 降序排序 */
    QueryPage ps = data_service_query_page(&set, 1, 5, -1, 0, 0, 0, 1, NULL, NULL, "raw", "all");
    ASSERT_DOUBLE_NEAR(ps.items[0].temp, 59.0, 1e-9, "降序首条 temp=59(最大)");
    ASSERT_DOUBLE_NEAR(ps.items[4].temp, 55.0, 1e-9, "降序第5条 temp=55");
    free(ps.items);

    /* 正确用例：非法 page_size 自动取15 */
    QueryPage pp = data_service_query_page(&set, 1, 0, -1, 0, 0, -1, 0, NULL, NULL, "raw", "all");
    ASSERT_TRUE(pp.page_size == 15, "page_size=0 自动修正为15");
    free(pp.items);

    /* 错误用例：空数据集分页 */
    DataSet empty; data_service_dataset_init(&empty);
    QueryPage pe = data_service_query_page(&empty, 1, 15, -1, 0, 0, -1, 0, NULL, NULL, "raw", "all");
    ASSERT_TRUE(pe.total == 0 && pe.total_pages == 1, "空集 total=0, total_pages=1");
    free(pe.items);

    data_service_dataset_free(&set);
}

/* ------------------------------------------------------------------ */
/* 8. 增/删/改记录                                                      */
/* ------------------------------------------------------------------ */
static void test_crud(void) {
    printf("\n[8] 数据维护 modify / delete / add\n");

    DataSet set; data_service_dataset_init(&set);
    for (int i = 0; i < 5; i++) {
        Data d = mk(20.0 + i, 33.0, 8.0, 8.0, 0.0, 25.0);
        d.record_index = i + 1;
        data_service_dataset_push(&set, d);
    }

    /* 正确用例：修改 row=2 的 temp */
    double oldv = -1;
    int ok = data_service_modify_record(&set, 2, 0, 99.9, &oldv);
    ASSERT_TRUE(ok == 1, "修改 row=2 返回1");
    ASSERT_DOUBLE_NEAR(oldv, 21.0, 1e-9, "返回旧值 21.0");
    ASSERT_DOUBLE_NEAR(set.items[1].temp, 99.9, 1e-9, "新值已写入 99.9");

    /* 正确用例：添加记录 */
    int newrow = data_service_add_record(&set, mk(30.0, 33.0, 8.0, 8.0, 0.0, 25.0));
    ASSERT_TRUE(newrow == 6, "新增记录 record_index=6");
    ASSERT_TRUE(set.size == 6, "数据集大小=6");

    /* 正确用例：按字段范围删除 temp>=99 */
    int* rows = NULL; int cnt = 0;
    int del = data_service_delete_records(&set, -1, 0, 99.0, 100.0, &rows, &cnt);
    ASSERT_TRUE(del == 1, "删除 temp∈[99,100] 共1条");
    ASSERT_TRUE(cnt == 1 && rows[0] == 2, "删除的是 row=2");
    ASSERT_TRUE(set.size == 5, "删除后大小=5");
    free(rows);

    /* 错误用例：修改不存在的行 */
    ASSERT_TRUE(data_service_modify_record(&set, 999, 0, 1.0, NULL) == 0, "修改不存在行返回0");

    /* 错误用例：删除条件不匹配任何记录 */
    int* r2 = NULL; int c2 = 0;
    int d2 = data_service_delete_records(&set, -1, 0, 1000.0, 2000.0, &r2, &c2);
    ASSERT_TRUE(d2 == 0 && c2 == 0, "无匹配删除返回0");
    ASSERT_TRUE(set.size == 5, "无匹配时大小不变");

    data_service_dataset_free(&set);
}

/* ------------------------------------------------------------------ */
/* 9. 移动平均滤波                                                      */
/* ------------------------------------------------------------------ */
static void test_filter(void) {
    printf("\n[9] 移动平均滤波 data_service_moving_average_filter\n");

    DataSet set; data_service_dataset_init(&set);
    /* 带噪声的序列: 10,12,8,11,9,13,7,12,8,10 */
    double v[10] = {10, 12, 8, 11, 9, 13, 7, 12, 8, 10};
    for (int i = 0; i < 10; i++) {
        Data d = mk(v[i], 33.0, 8.0, 8.0, 0.0, 25.0);
        d.record_index = i + 1;
        data_service_dataset_push(&set, d);
    }
    DataSet filtered; BasicStats b[WQ_FIELD_COUNT], a[WQ_FIELD_COUNT];
    int ok = data_service_moving_average_filter(&set, 3, &filtered, b, a);
    ASSERT_TRUE(ok == 1, "滤波返回1");
    ASSERT_TRUE(filtered.size == 10, "滤波后大小不变=10");
    /* 窗口3 中心点滤波后 std 应减小 */
    ASSERT_TRUE(a[0].stddev < b[0].stddev, "滤波后 temp 标准差应减小");
    /* 中心点(i=1)滤波=(10+12+8)/3=10 */
    ASSERT_DOUBLE_NEAR(filtered.items[1].temp, 10.0, 1e-9, "窗口3 中心点1 均值=10");

    data_service_dataset_free(&set);
    data_service_dataset_free(&filtered);

    /* 错误用例：空数据集滤波 */
    DataSet empty; data_service_dataset_init(&empty);
    DataSet fe; BasicStats be[WQ_FIELD_COUNT], ae[WQ_FIELD_COUNT];
    ASSERT_TRUE(data_service_moving_average_filter(&empty, 3, &fe, be, ae) == 1, "空集滤波返回1");
    ASSERT_TRUE(fe.size == 0, "空集滤波后大小=0");
    data_service_dataset_free(&fe);
}

/* ------------------------------------------------------------------ */
/* 10. 数据集生命周期与扩容                                             */
/* ------------------------------------------------------------------ */
static void test_dataset(void) {
    printf("\n[10] 数据集生命周期 data_service_dataset_push\n");

    DataSet set; data_service_dataset_init(&set);
    ASSERT_TRUE(set.items == NULL && set.size == 0 && set.capacity == 0, "init 后状态为空");
    for (int i = 0; i < 2500; i++) {
        Data d = mk(i, i, i, i, i, i);
        ASSERT_TRUE(data_service_dataset_push(&set, d) == 1, "push 成功(触发多次扩容)");
        if (g_fail) break;
    }
    ASSERT_TRUE(set.size == 2500, "大小=2500");
    ASSERT_TRUE(set.capacity >= 2500, "capacity>=2500");
    ASSERT_DOUBLE_NEAR(set.items[1500].temp, 1500.0, 1e-9, "第1500条 temp=1500");
    data_service_dataset_free(&set);
    ASSERT_TRUE(set.items == NULL && set.size == 0, "free 后状态为空");
}

int main(void) {
    printf("============================================================\n");
    printf("  海水养殖水质分析系统 - 测试套件\n");
    printf("============================================================\n");

    test_login();
    test_field_meta();
    test_csv_parse();
    test_preprocess();
    test_stats();
    test_regression();
    test_query();
    test_crud();
    test_filter();
    test_dataset();

    printf("\n============================================================\n");
    printf("  汇总: 通过 %d, 失败 %d, 总计 %d\n", g_pass, g_fail, g_pass + g_fail);
    printf("  结果: %s\n", g_fail == 0 ? "ALL PASS" : "HAS FAILURES");
    printf("============================================================\n");
    return g_fail == 0 ? 0 : 1;
}

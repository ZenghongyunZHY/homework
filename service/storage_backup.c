//
// storage_backup.c - 二进制存储、存储性能对比、备份与恢复
//

#include "DataService.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

/* ── binary storage ── */

int data_service_write_binary(const char* path, const DataSet* set) {
    FILE* fp = fopen(path, "wb");
    if (fp == NULL) return 0;
    int32_t count = (int32_t)set->size;
    if (fwrite(&count, sizeof(count), 1, fp) != 1) { fclose(fp); return 0; }
    if (fwrite(set->items, sizeof(Data), (size_t)count, fp) != (size_t)count) { fclose(fp); return 0; }
    fclose(fp);
    return 1;
}

int data_service_read_binary(const char* path, DataSet* set, ReadSummary* summary) {
    FILE* fp = fopen(path, "rb");
    data_service_dataset_init(set);
    if (summary) memset(summary, 0, sizeof(ReadSummary));
    if (fp == NULL) return 0;
    int32_t count = 0;
    if (fread(&count, sizeof(count), 1, fp) != 1) { fclose(fp); return 0; }
    if (count <= 0) { fclose(fp); return 1; }
    set->items = (Data*)malloc(sizeof(Data) * (size_t)count);
    if (set->items == NULL) { fclose(fp); return 0; }
    size_t read_count = fread(set->items, sizeof(Data), (size_t)count, fp);
    fclose(fp);
    if (read_count != (size_t)count) { data_service_dataset_free(set); return 0; }
    set->size = (int)read_count;
    set->capacity = (int)read_count;
    if (summary) { summary->total_records = set->size; summary->parsed_records = set->size; }
    return 1;
}

StorageBenchmark data_service_benchmark_storage(const char* csv_path) {
    StorageBenchmark bench;
    memset(&bench, 0, sizeof(bench));

    DataSet set;
    ReadSummary summary;
    if (!data_service_read_csv(csv_path, &set, &summary)) return bench;

    const char* csv_tmp = "_bench_csv.csv";
    const char* bin_tmp = "_bench_bin.dat";

    /* CSV file size */
    FILE* fp = fopen(csv_path, "rb");
    if (fp) { fseek(fp, 0, SEEK_END); bench.csv_size_bytes = ftell(fp); fclose(fp); }

    /* CSV write time */
    clock_t t0 = clock();
    data_service_write_csv(csv_tmp, &set);
    bench.csv_write_seconds = (double)(clock() - t0) / CLOCKS_PER_SEC;

    /* CSV read time */
    DataSet csv_set;
    t0 = clock();
    data_service_read_csv(csv_tmp, &csv_set, NULL);
    bench.csv_read_seconds = (double)(clock() - t0) / CLOCKS_PER_SEC;
    data_service_dataset_free(&csv_set);

    /* binary write time */
    t0 = clock();
    data_service_write_binary(bin_tmp, &set);
    bench.bin_write_seconds = (double)(clock() - t0) / CLOCKS_PER_SEC;

    /* binary file size */
    fp = fopen(bin_tmp, "rb");
    if (fp) { fseek(fp, 0, SEEK_END); bench.bin_size_bytes = ftell(fp); fclose(fp); }

    /* binary read time */
    DataSet bin_set;
    t0 = clock();
    data_service_read_binary(bin_tmp, &bin_set, NULL);
    bench.bin_read_seconds = (double)(clock() - t0) / CLOCKS_PER_SEC;
    data_service_dataset_free(&bin_set);

    data_service_dataset_free(&set);
    remove(csv_tmp);
    remove(bin_tmp);
    return bench;
}

/* ── backup / restore ── */

static void make_backup_dir(void) {
#ifdef _WIN32
    CreateDirectoryA("backup", NULL);
#else
    mkdir("backup", 0755);
#endif
}

char* data_service_backup_file(const char* src_path) {
    make_backup_dir();
    time_t now = time(NULL);
    struct tm tm_now;
#ifdef _WIN32
    localtime_s(&tm_now, &now);
#else
    localtime_r(&now, &tm_now);
#endif
    char filename[256];
    snprintf(filename, sizeof(filename), "backup/backup_%04d%02d%02d_%02d%02d%02d.csv",
             tm_now.tm_year + 1900, tm_now.tm_mon + 1, tm_now.tm_mday,
             tm_now.tm_hour, tm_now.tm_min, tm_now.tm_sec);

    FILE* src = fopen(src_path, "rb");
    if (src == NULL) return NULL;
    FILE* dst = fopen(filename, "wb");
    if (dst == NULL) { fclose(src); return NULL; }
    char buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), src)) > 0) fwrite(buf, 1, n, dst);
    fclose(src); fclose(dst);

    char* result = (char*)malloc(strlen(filename) + 1);
    if (result) strcpy(result, filename);
    return result;
}

static void free_string_list(char** list, int count) {
    if (list == NULL) return;
    for (int i = 0; i < count; i++) free(list[i]);
    free(list);
}

char** data_service_list_backups(const char* dir_path, int* count_out) {
    *count_out = 0;
    char** list = NULL;
    int cap = 0;
    int n = 0;
#ifdef _WIN32
    char search_path[512];
    snprintf(search_path, sizeof(search_path), "%s\\backup_*.csv", dir_path);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(search_path, &fd);
    if (h == INVALID_HANDLE_VALUE) return NULL;
    do {
        if (n == cap) {
            int new_cap = cap == 0 ? 16 : cap * 2;
            char** next = (char**)realloc(list, sizeof(char*) * (size_t)new_cap);
            if (next == NULL) { free_string_list(list, n); FindClose(h); *count_out = 0; return NULL; }
            list = next; cap = new_cap;
        }
        char* dup = (char*)malloc(strlen(fd.cFileName) + 1);
        if (dup) strcpy(dup, fd.cFileName);
        list[n++] = dup;
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR* dir = opendir(dir_path);
    if (dir == NULL) return NULL;
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, "backup_", 7) != 0) continue;
        if (n == cap) {
            int new_cap = cap == 0 ? 16 : cap * 2;
            char** next = (char**)realloc(list, sizeof(char*) * (size_t)new_cap);
            if (next == NULL) { free_string_list(list, n); closedir(dir); *count_out = 0; return NULL; }
            list = next; cap = new_cap;
        }
        char* dup = (char*)malloc(strlen(entry->d_name) + 1);
        if (dup) strcpy(dup, entry->d_name);
        list[n++] = dup;
    }
    closedir(dir);
#endif
    *count_out = n;
    return list;
}

int data_service_restore_from_backup(const char* backup_path, DataSet* set, ReadSummary* summary) {
    char full_path[512];
    snprintf(full_path, sizeof(full_path), "backup/%s", backup_path);
    return data_service_read_csv(full_path, set, summary);
}

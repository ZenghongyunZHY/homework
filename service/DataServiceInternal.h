//
// DataServiceInternal.h - 服务层跨模块共享的内部 helper
//
// 仅 DataService 拆分后的各 .c 文件包含此头；不对外暴露。
//

#ifndef HOMEWORK_DATASERVICE_INTERNAL_H
#define HOMEWORK_DATASERVICE_INTERNAL_H

#include <stddef.h>

/* 动态数组扩容：成功返回 1 并更新 *items / *capacity，失败返回 0（原块不变） */
int ds_dyn_reserve(void** items, int* capacity, size_t item_size, int new_capacity);

/* 大小写不敏感字符串比较 */
int ds_equals_ignore_case(const char* a, const char* b);

/* 原地去除首尾空白 */
void ds_trim_in_place(char* text);

#endif // HOMEWORK_DATASERVICE_INTERNAL_H

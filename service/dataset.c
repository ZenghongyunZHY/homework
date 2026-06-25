//
// dataset.c - 数据集生命周期与动态数组扩容
//

#include "DataService.h"
#include "DataServiceInternal.h"

#include <stdlib.h>

int ds_dyn_reserve(void** items, int* capacity, size_t item_size, int new_capacity) {
    if (new_capacity <= *capacity) return 1;
    void* next = realloc(*items, item_size * (size_t)new_capacity);
    if (next == NULL) return 0;
    *items = next;
    *capacity = new_capacity;
    return 1;
}

void data_service_dataset_init(DataSet* set) {
    set->items = NULL;
    set->size = 0;
    set->capacity = 0;
}

void data_service_dataset_free(DataSet* set) {
    if (set == NULL) return;
    free(set->items);
    set->items = NULL;
    set->size = 0;
    set->capacity = 0;
}

int data_service_dataset_push(DataSet* set, Data item) {
    if (set->size == set->capacity) {
        int next_capacity = set->capacity == 0 ? 1000 : set->capacity * 2;
        if (!ds_dyn_reserve((void**)&set->items, &set->capacity, sizeof(Data), next_capacity))
            return 0;
    }
    set->items[set->size++] = item;
    return 1;
}

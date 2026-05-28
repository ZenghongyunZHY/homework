//
// Created by Chen on 2026/5/14.
//

#ifndef HOMEWORK_DATASERVICE_H
#define HOMEWORK_DATASERVICE_H

#include "../pojo/Data.h"

#define WQ_FIELD_COUNT 6

typedef struct {
    const char* key;
    const char* label;
    const char* unit;
    double min_value;
    double max_value;
} FieldMeta;

int data_service_run_cli(int argc, char** argv);
const FieldMeta* data_service_fields(void);
int data_service_field_index(const char* field);

#endif //HOMEWORK_DATASERVICE_H

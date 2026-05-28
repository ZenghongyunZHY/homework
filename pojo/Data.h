//
// Created by Chen on 2026/5/14.
//

#ifndef HOMEWORK_ST_DATA_H
#define HOMEWORK_ST_DATA_H

#include <stdio.h>

typedef struct {
    int record_index;
    double temp;
    double salinity;
    double ph;
    double do_value;
    double precipitation;
    double air_temp;
} Data;

typedef struct {
    Data* items;
    int size;
    int capacity;
} DataSet;

typedef struct Node {
    Data* data;
    struct Node* next_data;
    struct Node* prev_data;
} Node;

typedef struct {
    Node* front;
    Node* rear;
    int size;
} Queue;

typedef struct {
    FILE* cin_file;
    FILE* cout_file;
    int is_read_finished;
    Node* prev_read_address;
    Data* cin_data;
    Data* temp_data;
    Data* cout_data;
    Queue* data_queue;
} Data_Manager;

#endif // HOMEWORK_ST_DATA_H

//
// Created by Chen on 2026/5/14.
//
#include <stdio.h>
#ifndef HOMEWORK_ST_DATA_H
#define HOMEWORK_ST_DATA_H
typedef struct {
    double temp;//温度
    double salinity;//盐度
    double ph;//酸碱度
    double do_value;//溶解氧
    double precipitation;//降水量
    double air_temp;//空气温度
} Data;

//利用链表来实现队列，front指向队头，rear指向队尾,queue指向存储数据的数组
typedef struct Node{
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
    FILE* cin_file;//输入文件名
    FILE* cout_file;//结果文件名
    Node* prev_read_address;//上次读取的数据地址(队列中的地址)
    Data* cin_data;//输入数据
    Data* temp_data;//处理后的数据
    Data* cout_data;//结果数据
    Queue* data_queue;//数据队列
} Data_Manager;
#endif //HOMEWORK_ST_DATA_H

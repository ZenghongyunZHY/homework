#ifndef OPERATION_FILE_H
#define OPERATION_FILE_H

#include "../pojo/Data.h"

typedef Data Data_Struct;

typedef void (*Data_Read_Func)(Data_Manager* manager, int size);
typedef void (*Data_Process_Func)(Data_Manager* manager);

Data_Manager* init_file(Data_Manager* manager, const char* cin_filename);
Queue* create_queue(void);
Node* get_node_at(Queue* q, int index);
void read_file(Data_Manager* manager, int size, Data_Read_Func diff_read_func);
void process_data(Data_Manager* manager, Data_Process_Func diff_process_func);
int write_file(Data_Manager* manager);
void prev_read_func(Data_Manager* manager, int size);
void prev_process_func(Data_Manager* manager);

#endif // OPERATION_FILE_H

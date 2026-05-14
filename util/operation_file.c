#include "operation_file.h"
#include <stdio.h>
#include <stdlib.h>

/**
 * @brief 初始化文件句柄和运行时缓冲区。
 * @param manager 需要初始化的数据管理器。
 * @param cin_filename 输入的 CSV 文件名。
 * @return 成功返回初始化后的管理器，失败返回 NULL。
 */
Data_Manager* init_file(Data_Manager* manager, const char* cin_filename) {
    //创建读和写的结构体
    manager->cout_data = NULL;
    //创建需要写入的文件
    FILE* cout_file = fopen("result.csv", "w");
    if (cout_file == NULL) {
        fprintf(stderr, "无法创建结果文件\n");
        return NULL;
    }
    //打开输入文件
    FILE* cin_file = fopen(cin_filename, "r");
    if (cin_file == NULL) {
        fprintf(stderr, "无法打开输入文件\n");
        return NULL;
    }
    //跳过文件的第一行（表头）
    char line[256];
    fgets(line, sizeof(line), cin_file);
    manager->cout_file = cout_file;
    manager->cin_file = cin_file;
    manager->data_queue = create_queue();
    return manager;
}

/**
 * @brief 创建一个空队列，用于存放 Data_Struct 节点。
 * @return 成功返回新建队列，失败返回 NULL。
 */
Queue* create_queue() {
    Queue* q = (Queue*)malloc(sizeof(Queue));
    q->front = NULL;
    q->rear = NULL;
    q->size = 0;
    return q;
    
}
/**
 * @brief 从输入文件读取一行，并将解析后的数据入队。
 * @param manager 包含输入文件和队列的数据管理器。
 * @param is_finished 读取完成标志，遇到 EOF 时置为 1。
 * @return 读取并处理成功返回 1，遇到 EOF 或错误返回 0。
 */
int read_file(Data_Manager* manager,int* is_finished) {
    if(manager == NULL) {
        fprintf(stderr, "Data_Manager未初始化\n");
        return 0;
    }
    char line[256];
    if(fgets(line, sizeof(line), manager->cin_file) == NULL) {
        //读取完毕要将队列中剩余的数据写入结果文件中
        

        //占位，后续根据需求进行数据处理


        *is_finished = 1;
        return 0;//文件读取完毕
    }
    //将读到数据转化为Data_Struct类型，并存入队列中
    Data_Struct* temp_data = (Data_Struct*)malloc(sizeof(Data_Struct));
    if(temp_data == NULL) {
        fprintf(stderr, "内存分配失败\n");
        return 0;
    }
    int ret = sscanf(line, "%lf,%lf,%lf,%lf,%lf,%lf", &temp_data->temp, &temp_data->salinity, &temp_data->ph, &temp_data->do_value, &temp_data->precipitation, &temp_data->air_temp);
    if(ret != 6) {
        fprintf(stderr, "数据格式错误\n");
        free(temp_data);
        return 0;
    }
    if(manager->data_queue->size < 21) {
        //队列未满，直接入队
        Node* new_node = (Node*)malloc(sizeof(Node));
        new_node->data = temp_data;
        new_node->next_data = NULL;
        new_node->prev_data = NULL;
        if (manager->data_queue->rear == NULL) {
            manager->data_queue->front = new_node;
            manager->data_queue->rear = new_node;
        } else {
            manager->data_queue->rear->next_data = new_node;
            new_node->prev_data = manager->data_queue->rear;
            manager->data_queue->rear = new_node;
        }
        manager->data_queue->size++;
    }
    else {
        //队列已满，出队一个元素，再入队
        Node* temp = manager->data_queue->front;
        manager->data_queue->front = temp->next_data;
        if (manager->data_queue->front != NULL) {
            manager->data_queue->front->prev_data = NULL;
        }
        free(temp->data);
        free(temp);
        manager->data_queue->size--;
        
        Node* new_node = (Node*)malloc(sizeof(Node));
        new_node->data = temp_data;
        new_node->next_data = NULL;
        new_node->prev_data = NULL;
        if (manager->data_queue->rear == NULL) {
            manager->data_queue->front = new_node;
            manager->data_queue->rear = new_node;
        } else {
            manager->data_queue->rear->next_data = new_node;
            new_node->prev_data = manager->data_queue->rear;
            manager->data_queue->rear = new_node;
        }
        manager->data_queue->size++;
    }

    return 1;
}

    

/**
 * @brief 数据处理逻辑的占位函数。
 */
void process_data() {
    
}

/**
 * @brief 从队列中选择合适的数据并写入结果文件。
 * @param manager 包含队列和输出文件的数据管理器。
 * @return 成功写入返回 1，条件不足或失败返回 0。
 */
int write_file(Data_Manager* manager) {
    if (manager == NULL || manager->data_queue == NULL || manager->cout_file == NULL) {
        return 0;
    }

    Queue* q = manager->data_queue;
    int index;

    if (q->size < 11) {
        // 后继数据还不够 10 个，先不写
        return 0;
    } else if (q->size < 21) {
        // size = 11 时写第 1 条，下标 0
        // size = 12 时写第 2 条，下标 1
        // ...
        // size = 20 时写第 10 条，下标 9
        index = q->size - 11;
    } else {
        // size = 21 时，写中间第 11 条，下标 10
        index = 10;
    }

    Node* target = get_node_at(q, index);
    if (target == NULL || target->data == NULL) {
        return 0;
    }

    manager->cout_data = target->data;

    process_data();

    fprintf(manager->cout_file, "%lf,%lf,%lf,%lf,%lf,%lf\n",
            manager->cout_data->temp,
            manager->cout_data->salinity,
            manager->cout_data->ph,
            manager->cout_data->do_value,
            manager->cout_data->precipitation,
            manager->cout_data->air_temp);

    return 1;
}

/**
 * @brief 获取队列中指定位置的节点。
 * @param q 队列指针。
 * @param index 目标位置下标（从 0 开始）。
 * @return 成功返回节点指针，越界或失败返回 NULL。
 */
Node* get_node_at(Queue* q, int index) {
    if (q == NULL || index < 0 || index >= q->size) {
        return NULL;
    }

    Node* p = q->front;

    for (int i = 0; i < index; i++) {
        p = p->next_data;
    }

    return p;
}
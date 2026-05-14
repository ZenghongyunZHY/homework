#include "operation_file.h"
#include <stdio.h>
#include <stdlib.h>

/**
 * @brief 初始化文件句柄和数据管理器运行状态。
 *
 * @param manager 需要初始化的数据管理器。
 * @param cin_filename 输入 CSV 文件路径。
 * @return 成功返回初始化后的数据管理器，失败返回 NULL。
 */
Data_Manager* init_file(Data_Manager* manager, const char* cin_filename) {
    if (manager == NULL) {
        fprintf(stderr, "Data_Manager is NULL\n");
        return NULL;
    }

    manager->cout_data = NULL;

    FILE* cout_file = fopen("result.csv", "w");
    if (cout_file == NULL) {
        fprintf(stderr, "Failed to create result file\n");
        return NULL;
    }

    FILE* cin_file = fopen(cin_filename, "r");
    if (cin_file == NULL) {
        fprintf(stderr, "Failed to open input file\n");
        fclose(cout_file);
        return NULL;
    }

    char line[256];
    fgets(line, sizeof(line), cin_file);
    manager->cout_file = cout_file;
    manager->cin_file = cin_file;
    if (create_queue()) {
        manager->data_queue = create_queue();
    }
    return manager;
}

/**
 * @brief 创建用于保存水质数据节点的空队列。
 *
 * @return 成功返回新分配的队列，内存分配失败时返回 NULL。
 */
Queue* create_queue(void) {
    Queue* q = (Queue*)malloc(sizeof(Queue));
    q->front = NULL;
    q->rear = NULL;
    q->size = 0;
    return q;
}

/**
 * @brief 通过具体读取策略读取一条数据记录。
 *
 * @param manager 保存文件和队列状态的数据管理器。
 * @param size 读取策略使用的队列最大容量。
 * @param diff_read_func 具体读取策略回调函数。
 */
void read_file(Data_Manager* manager, int size, void (*diff_read_func)(Data_Manager*, int)) {
    if (manager == NULL) {
        fprintf(stderr, "Data_Manager is not initialized\n");
        return;
    }
    if (diff_read_func == NULL) {
        fprintf(stderr, "Read strategy is not provided\n");
        return;
    }
    diff_read_func(manager, size);
}

/**
 * @brief 通过具体处理策略处理当前数据。
 *
 * @param manager 保存队列和输出状态的数据管理器。
 * @param diff_process_func 具体处理策略回调函数。
 */
void process_data(Data_Manager* manager, void (*diff_process_func)(Data_Manager*)) {
    if (diff_process_func == NULL) {
        fprintf(stderr, "Process strategy is not provided\n");
        return;
    }

    diff_process_func(manager);
}

/**
 * @brief 将当前输出数据记录写入结果文件。
 *
 * @param manager 保存输出文件和输出数据的数据管理器。
 * @return 写入成功返回 1，状态无效或写入失败返回 0。
 */
int write_file(Data_Manager* manager) {
    if (manager == NULL ||
        manager->data_queue == NULL ||
        manager->cout_file == NULL ||
        manager->cout_data == NULL) {
        return 0;
    }

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
 * @brief 获取队列中指定下标的节点。
 *
 * @param q 需要查询的队列。
 * @param index 从 0 开始的节点下标。
 * @return 成功返回节点指针，队列或下标无效时返回 NULL。
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

/**
 * @brief 默认处理策略，用于选择当前应输出的数据记录。
 *
 * @param manager 保存队列和输出状态的数据管理器。
 */
void prev_process_func(Data_Manager* manager) {
    if (manager == NULL || manager->data_queue == NULL || manager->cout_file == NULL) {
        return;
    }

    Queue* q = manager->data_queue;

    int index;

    if (q->size < 11) {
        return;
    } else if (q->size < 21) {
        index = q->size - 11;
    } else {
        index = 10;
    }

    Node* target = get_node_at(q, index);
    if (target == NULL || target->data == NULL) {
        return;
    }

    manager->cout_data = target->data;
}

/**
 * @brief 默认读取策略，用于读取一条 CSV 记录并加入队列。
 *
 * @param manager 保存输入文件和队列状态的数据管理器。
 * @param size 队列最大容量。
 */
void prev_read_func(Data_Manager* manager, int size) {
    char line[256];
    if (fgets(line, sizeof(line), manager->cin_file) == NULL) {
        manager->is_read_finished = 1;
        return;
    }

    Data_Struct* temp_data = (Data_Struct*)malloc(sizeof(Data_Struct));
    if (temp_data == NULL) {
        fprintf(stderr, "Memory allocation failed\n");
        return;
    }

    int ret = sscanf(line, "%lf,%lf,%lf,%lf,%lf,%lf",
                     &temp_data->temp,
                     &temp_data->salinity,
                     &temp_data->ph,
                     &temp_data->do_value,
                     &temp_data->precipitation,
                     &temp_data->air_temp);
    if (ret != 6) {
        fprintf(stderr, "Invalid data format\n");
        free(temp_data);
        return;
    }
    if (size <= 0) {
        fprintf(stderr, "Queue capacity must be greater than 0\n");
        free(temp_data);
        return;
    }

    if (manager->data_queue->size < size) {
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
    } else {
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
}

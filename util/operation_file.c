#include "operation_file.h"

#include <stdio.h>
#include <stdlib.h>

Data_Manager* init_file(Data_Manager* manager, const char* cin_filename) {
    if (manager == NULL || cin_filename == NULL) {
        fprintf(stderr, "Data_Manager or filename is NULL\n");
        return NULL;
    }

    manager->cin_file = fopen(cin_filename, "r");
    if (manager->cin_file == NULL) {
        fprintf(stderr, "failed to open input file\n");
        return NULL;
    }

    manager->cout_file = fopen("result.csv", "w");
    if (manager->cout_file == NULL) {
        fprintf(stderr, "failed to open output file\n");
        fclose(manager->cin_file);
        manager->cin_file = NULL;
        return NULL;
    }

    char header[256];
    fgets(header, sizeof(header), manager->cin_file);
    manager->is_read_finished = 0;
    manager->cout_data = NULL;
    manager->data_queue = create_queue();
    if (manager->data_queue == NULL) {
        fclose(manager->cin_file);
        fclose(manager->cout_file);
        manager->cin_file = NULL;
        manager->cout_file = NULL;
        return NULL;
    }
    return manager;
}

Queue* create_queue(void) {
    Queue* q = (Queue*)malloc(sizeof(Queue));
    if (q == NULL) {
        fprintf(stderr, "queue allocation failed\n");
        return NULL;
    }
    q->front = NULL;
    q->rear = NULL;
    q->size = 0;
    return q;
}

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

void read_file(Data_Manager* manager, int size, Data_Read_Func diff_read_func) {
    if (manager == NULL || diff_read_func == NULL) {
        return;
    }
    diff_read_func(manager, size);
}

void process_data(Data_Manager* manager, Data_Process_Func diff_process_func) {
    if (manager == NULL || diff_process_func == NULL) {
        return;
    }
    diff_process_func(manager);
}

int write_file(Data_Manager* manager) {
    if (manager == NULL || manager->cout_file == NULL || manager->cout_data == NULL) {
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

static int enqueue_data(Queue* q, Data* data, int max_size) {
    if (q == NULL || data == NULL || max_size <= 0) {
        return 0;
    }
    if (q->size >= max_size && q->front != NULL) {
        Node* old = q->front;
        q->front = old->next_data;
        if (q->front != NULL) {
            q->front->prev_data = NULL;
        } else {
            q->rear = NULL;
        }
        free(old->data);
        free(old);
        q->size--;
    }

    Node* node = (Node*)malloc(sizeof(Node));
    if (node == NULL) {
        return 0;
    }
    node->data = data;
    node->next_data = NULL;
    node->prev_data = q->rear;
    if (q->rear != NULL) {
        q->rear->next_data = node;
    } else {
        q->front = node;
    }
    q->rear = node;
    q->size++;
    return 1;
}

void prev_read_func(Data_Manager* manager, int size) {
    if (manager == NULL || manager->cin_file == NULL || manager->data_queue == NULL) {
        return;
    }

    char line[256];
    if (fgets(line, sizeof(line), manager->cin_file) == NULL) {
        manager->is_read_finished = 1;
        return;
    }

    Data* data = (Data*)malloc(sizeof(Data));
    if (data == NULL) {
        return;
    }
    data->record_index = manager->data_queue->size + 1;
    int ret = sscanf(line, "%lf,%lf,%lf,%lf,%lf,%lf",
                     &data->temp,
                     &data->salinity,
                     &data->ph,
                     &data->do_value,
                     &data->precipitation,
                     &data->air_temp);
    if (ret != 6) {
        free(data);
        return;
    }
    if (!enqueue_data(manager->data_queue, data, size)) {
        free(data);
    }
}

void prev_process_func(Data_Manager* manager) {
    if (manager == NULL || manager->data_queue == NULL || manager->data_queue->size == 0) {
        return;
    }

    int index = manager->data_queue->size >= 21 ? 10 : manager->data_queue->size - 1;
    Node* node = get_node_at(manager->data_queue, index);
    manager->cout_data = node == NULL ? NULL : node->data;
}

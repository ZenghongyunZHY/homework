#include "operation_file.h"
#include <stdio.h>
#include <stdlib.h>

/**
 * @brief Initialize file handles and runtime data manager state.
 *
 * @param manager Data manager to initialize.
 * @param cin_filename Input CSV file path.
 * @return Initialized manager on success, NULL on failure.
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
 * @brief Create an empty queue for water quality data nodes.
 *
 * @return Newly allocated queue on success, NULL on allocation failure.
 */
Queue* create_queue(void) {
    Queue* q = (Queue*)malloc(sizeof(Queue));
    q->front = NULL;
    q->rear = NULL;
    q->size = 0;
    return q;
}

/**
 * @brief Read one data record by delegating to a concrete read strategy.
 *
 * @param manager Data manager containing file and queue state.
 * @param size Maximum queue size used by the read strategy.
 * @param diff_read_func Concrete read strategy callback.
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
 * @brief Process current data by delegating to a concrete process strategy.
 *
 * @param manager Data manager containing queue and output state.
 * @param diff_process_func Concrete process strategy callback.
 */
void process_data(Data_Manager* manager, void (*diff_process_func)(Data_Manager*)) {
    if (diff_process_func == NULL) {
        fprintf(stderr, "Process strategy is not provided\n");
        return;
    }

    diff_process_func(manager);
}

/**
 * @brief Write the current output data record to the result file.
 *
 * @param manager Data manager containing output file and output data.
 * @return 1 on success, 0 on invalid state or write failure.
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
 * @brief Get the node at the specified queue index.
 *
 * @param q Queue to search.
 * @param index Zero-based node index.
 * @return Node pointer on success, NULL if the queue or index is invalid.
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
 * @brief Default process strategy for selecting the output data record.
 *
 * @param manager Data manager containing queue and output state.
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
 * @brief Default read strategy for reading one CSV record into the queue.
 *
 * @param manager Data manager containing input file and queue state.
 * @param size Maximum queue size.
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

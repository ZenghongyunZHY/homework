#ifndef OPERATION_FILE_H
#define OPERATION_FILE_H

#include "../pojo/Data.h"

typedef Data Data_Struct;

typedef void (*Data_Read_Func)(Data_Manager* manager, int size);
typedef void (*Data_Process_Func)(Data_Manager* manager);

/**
 * @brief Initialize file handles and runtime data manager state.
 *
 * @param manager Data manager to initialize.
 * @param cin_filename Input CSV file path.
 * @return Initialized manager on success, NULL on failure.
 */
Data_Manager* init_file(Data_Manager* manager, const char* cin_filename);

/**
 * @brief Create an empty queue for water quality data nodes.
 *
 * @return Newly allocated queue on success, NULL on allocation failure.
 */
Queue* create_queue(void);

/**
 * @brief Get the node at the specified queue index.
 *
 * @param q Queue to search.
 * @param index Zero-based node index.
 * @return Node pointer on success, NULL if the queue or index is invalid.
 */
Node* get_node_at(Queue* q, int index);

/**
 * @brief Read one data record by delegating to a concrete read strategy.
 *
 * @param manager Data manager containing file and queue state.
 * @param size Maximum queue size used by the read strategy.
 * @param diff_read_func Concrete read strategy callback.
 */
void read_file(Data_Manager* manager, int size, Data_Read_Func diff_read_func);

/**
 * @brief Process current data by delegating to a concrete process strategy.
 *
 * @param manager Data manager containing queue and output state.
 * @param diff_process_func Concrete process strategy callback.
 */
void process_data(Data_Manager* manager, Data_Process_Func diff_process_func);

/**
 * @brief Write the current output data record to the result file.
 *
 * @param manager Data manager containing output file and output data.
 * @return 1 on success, 0 on invalid state or write failure.
 */
int write_file(Data_Manager* manager);

/**
 * @brief Default read strategy for reading one CSV record into the queue.
 *
 * @param manager Data manager containing input file and queue state.
 * @param size Maximum queue size.
 */
void prev_read_func(Data_Manager* manager, int size);

/**
 * @brief Default process strategy for selecting the output data record.
 *
 * @param manager Data manager containing queue and output state.
 */
void prev_process_func(Data_Manager* manager);

#endif // OPERATION_FILE_H

#ifndef ASYNC_FILE_OPS_H
#define ASYNC_FILE_OPS_H

#include <stddef.h>
#include <stdbool.h>

/**
 * @brief Callback function type for read operations.
 * @param buffer The text buffer containing the file content.
 * @param size The size of the buffer.
 */
typedef void (*read_callback_t)(const char *buffer, size_t size);

/**
 * @brief Callback function type for write operations.
 * @param status The status message indicating success or error.
 */
typedef void (*write_callback_t)(const char *status);

/**
 * @brief Validation function type.
 * @param buffer The text buffer to test.
 * @param size The size of the buffer.
 * @return True if the buffer is valid, false otherwise.
 */
typedef bool (*validation_func_t)(const char *buffer, size_t size);

/**
 * @brief Structure to hold file operation context.
 */
typedef struct {
    validation_func_t validate; /**< Pointer to the validation function. */
} file_ops_context_t;

/**
 * @brief Asynchronously reads a text file and fills a text buffer.
 * @param context The file operation context.
 * @param filepath The path to the text file.
 * @param callback The callback function to call with the text buffer and size.
 */
void async_read_file(file_ops_context_t *context, const char *filepath, read_callback_t callback);

/**
 * @brief Asynchronously writes a text buffer to a file.
 * @param filepath The path to the text file.
 * @param buffer The text buffer to write.
 * @param size The size of the buffer.
 * @param callback The callback function to call with the status message.
 */
void async_write_file(const char *filepath, const char *buffer, size_t size, write_callback_t callback);

#endif // ASYNC_FILE_OPS_H

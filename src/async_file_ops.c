#include "../includes/async_io/async_file_ops.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

/**
 * @brief Structure to hold arguments for the read thread.
 */
typedef struct {
    file_ops_context_t *context; /**< The file operation context. */
    const char *filepath; /**< The path to the file. */
    read_callback_t callback; /**< The callback function. */
} read_thread_args_t;

/**
 * @brief Structure to hold arguments for the write thread.
 */
typedef struct {
    const char *filepath; /**< The path to the file. */
    const char *buffer; /**< The buffer to write. */
    size_t size; /**< The size of the buffer. */
    write_callback_t callback; /**< The callback function. */
} write_thread_args_t;

/**
 * @brief Thread function for reading a file asynchronously.
 * @param arg The argument containing the filepath and callback.
 * @return Always returns NULL.
 */
void *read_file_thread(void *arg) {
    read_thread_args_t *args = (read_thread_args_t *)arg;
    file_ops_context_t *context = args->context;
    const char *filepath = args->filepath;
    read_callback_t callback = args->callback;

    FILE *file = fopen(filepath, "r");
    if (!file) {
        // Call the callback with an error message if the file cannot be opened
        callback("invalid genomic file", strlen("invalid genomic file"));
        free(args);
        return NULL;
    }

    // Determine the size of the file
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);

    // Allocate memory for the buffer
    char *buffer = (char *)malloc(size + 1);
    if (!buffer) {
        fclose(file);
        // Call the callback with an error message if memory allocation fails
        callback("invalid genomic file", strlen("invalid genomic file"));
        free(args);
        return NULL;
    }

    // Read the file content into the buffer
    fread(buffer, 1, size, file);
    buffer[size] = '\0';
    fclose(file);

    // Validate the buffer and call the callback with the result
    if (context->validate(buffer, size)) {
        callback(buffer, size);
    } else {
        callback("invalid genomic file", strlen("invalid genomic file"));
    }

    // Free the allocated memory
    free(buffer);
    free(args);
    return NULL;
}

/**
 * @brief Thread function for writing a file asynchronously.
 * @param arg The argument containing the filepath, buffer, size, and callback.
 * @return Always returns NULL.
 */
void *write_file_thread(void *arg) {
    write_thread_args_t *args = (write_thread_args_t *)arg;
    const char *filepath = args->filepath;
    const char *buffer = args->buffer;
    size_t size = args->size;
    write_callback_t callback = args->callback;

    // Open the file in append mode
    FILE *file = fopen(filepath, "a");
    if (!file) {
        // Call the callback with an error message if the file cannot be opened
        callback("Error opening file");
        free(args);
        return NULL;
    }

    // Write the buffer to the file
    if (fwrite(buffer, 1, size, file) != size) {
        fclose(file);
        // Call the callback with an error message if writing fails
        callback("Error writing to file");
        free(args);
        return NULL;
    }

    // Close the file and call the callback with a success message
    fclose(file);
    callback("Success");
    free(args);
    return NULL;
}

/**
 * @brief Asynchronously reads a text file and fills a text buffer.
 * @param context The file operation context.
 * @param filepath The path to the text file.
 * @param callback The callback function to call with the text buffer and size.
 */
void async_read_file(file_ops_context_t *context, const char *filepath, read_callback_t callback) {
    pthread_t thread;
    // Allocate memory for the thread arguments
    read_thread_args_t *args = (read_thread_args_t *)malloc(sizeof(read_thread_args_t));
    args->context = context;
    args->filepath = filepath;
    args->callback = callback;
    // Create the thread
    pthread_create(&thread, NULL, read_file_thread, (void *)args);
    // Detach the thread to allow it to run independently
    pthread_detach(thread);
}

/**
 * @brief Asynchronously writes a text buffer to a file.
 * @param filepath The path to the text file.
 * @param buffer The text buffer to write.
 * @param size The size of the buffer.
 * @param callback The callback function to call with the status message.
 */
void async_write_file(const char *filepath, const char *buffer, size_t size, write_callback_t callback) {
    pthread_t thread;
    // Allocate memory for the thread arguments
    write_thread_args_t *args = (write_thread_args_t *)malloc(sizeof(write_thread_args_t));
    args->filepath = filepath;
    args->buffer = buffer;
    args->size = size;
    args->callback = callback;
    // Create the thread
    pthread_create(&thread, NULL, write_file_thread, (void *)args);
    // Detach the thread to allow it to run independently
    pthread_detach(thread);
}

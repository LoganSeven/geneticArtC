#ifndef GENETIC_ART_H
#define GENETIC_ART_H



#include "genetic_structs.h"
#include <pthread.h>
#include <stdatomic.h>

/**
 * @brief Function pointer type for GA fitness evaluation.
 *
 * The GA engine calls this function to obtain the fitness of a Chromosome.
 * The user_data pointer is passed through from GAContext and can be used
 * to provide access to evaluation buffers or constants.
 *
 * @param c         Pointer to the Chromosome to be evaluated.
 * @param user_data Opaque pointer to user data, as provided in the GAContext.
 * @return The computed fitness value (lower is better).
 */
typedef double (*GAFitnessFunc)(const Chromosome *c, void *user_data);

/**
 * @brief Enum for log message severity level.
 *
 * Can be used by the application to color or route logs appropriately.
 */
typedef enum {
    GA_LOG_INFO,  /**< Informational message. */
    GA_LOG_WARN,  /**< Warning message.       */
    GA_LOG_ERROR  /**< Error message.         */
} GALogLevel;

/**
 * @brief Function pointer type for GA logging.
 *
 * If provided in the GAContext, the GA engine will call this callback
 * to report internal info, warnings, or errors. Logging is optional.
 *
 * @param level     The severity level of the log message.
 * @param msg       The message string to log.
 * @param user_data Opaque pointer to user data, as provided in GAContext.
 */
typedef void (*GALogFunc)(GALogLevel level, const char *msg, void *user_data);

/**
 * @brief GAContext — central structure for the GA engine.
 *
 * This structure defines the entire runtime configuration of the GA engine.
 * It must be fully initialized before being passed to ga_thread_func().
 */
typedef struct GAContext {
    /**
     * @brief Pointer to the GA parameters used during evolution.
     */
    const GAParams     *params;

    /**
     * @brief Stop flag; set to 0 to signal the GA thread to exit.
     */
    atomic_int         *running;

    /**
     * @brief Memory management function to allocate a new Chromosome.
     * The function must create a Chromosome with space for @p n_shapes genes.
     */
    Chromosome        *(*alloc_chromosome)(size_t n_shapes);

    /**
     * @brief Memory management function to free a Chromosome.
     */
    void              (*free_chromosome)(Chromosome *c);

    /**
     * @brief Pointer to a mutex protecting best_snapshot access.
     */
    pthread_mutex_t    *best_mutex;

    /**
     * @brief Holds a copy of the best Chromosome found so far, updated periodically.
     */
    Chromosome         *best_snapshot;

    /**
     * @brief Callback-based fitness evaluation.
     */
    GAFitnessFunc       fitness_func;

    /**
     * @brief Opaque pointer to user-defined fitness data.
     */
    void               *fitness_data;

    /**
     * @brief Optional logging interface.
     */
    GALogFunc           log_func;

    /**
     * @brief Opaque pointer to user-defined log data.
     */
    void               *log_user_data;
} GAContext;

/**
 * @brief Entry point for the GA thread — meant to be used with pthread_create().
 *
 * This function runs the full GA loop (initialization, iterations, shutdown).
 * The @p arg must point to a valid GAContext that remains alive for the
 * duration of the thread.
 *
 * @param arg Must point to a fully initialized GAContext.
 * @return Always returns NULL on thread exit.
 */
void *ga_thread_func(void *arg);


#endif /* GENETIC_ART_H */

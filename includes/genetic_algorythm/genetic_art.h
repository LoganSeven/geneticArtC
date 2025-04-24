#ifndef GENETIC_ART_H
#define GENETIC_ART_H

#ifdef __cplusplus
extern "C" {
#endif

#include "genetic_structs.h"
#include <pthread.h>
#include <stdatomic.h>

/* ------------------------------------------------------------------------
 * @brief Function pointer type for GA fitness evaluation.
 *
 * The GA engine calls this function to obtain the fitness of a Chromosome.
 * The user_data pointer is passed through from GAContext and can be used
 * to provide access to evaluation buffers or constants.
 * ----------------------------------------------------------------------*/
typedef double (*GAFitnessFunc)(const Chromosome *c, void *user_data);

/* ------------------------------------------------------------------------
 * @brief Enum for log message severity level.
 * Can be used by the application to color or route logs appropriately.
 * ----------------------------------------------------------------------*/
typedef enum {
    GA_LOG_INFO,
    GA_LOG_WARN,
    GA_LOG_ERROR
} GALogLevel;

/* ------------------------------------------------------------------------
 * @brief Function pointer type for GA logging.
 *
 * If provided in the GAContext, the GA engine will call this callback
 * to report internal info, warnings or errors. Logging is optional.
 * ----------------------------------------------------------------------*/
typedef void (*GALogFunc)(GALogLevel level, const char *msg, void *user_data);

/* ------------------------------------------------------------------------
 * @brief GAContext — central structure for the GA engine.
 *
 * This structure defines the entire runtime configuration of the GA engine.
 * It must be fully initialized before being passed to ga_thread_func().
 * ----------------------------------------------------------------------*/
typedef struct GAContext {
    /* Runtime GA tunables */
    const GAParams     *params;

    /* Stop flag: 0 ⇒ GA thread must exit */
    atomic_int         *running;

    /* Memory mgmt function pointers (allocation of chromosomes) */
    Chromosome        *(*alloc_chromosome)(size_t n_shapes);
    void              (*free_chromosome)(Chromosome *c);

    /* Best solution (thread-safe copy updated periodically) */
    pthread_mutex_t    *best_mutex;
    Chromosome         *best_snapshot;

    /* Callback-based fitness evaluation */
    GAFitnessFunc       fitness_func;
    void               *fitness_data;

    /* Optional logging interface (can be NULL) */
    GALogFunc           log_func;
    void               *log_user_data;
} GAContext;

/* ------------------------------------------------------------------------
 * @brief Entry point for the GA thread — meant to be used with pthread_create().
 *
 * This function runs the full GA loop (init, iterations, shutdown).
 * The @p arg must point to a valid GAContext that remains alive for
 * the duration of the thread.
 *
 * @param arg Must point to a fully initialized GAContext.
 * @return NULL on thread exit
 * ----------------------------------------------------------------------*/
void *ga_thread_func(void *arg);

#ifdef __cplusplus
}
#endif

#endif /* GENETIC_ART_H */

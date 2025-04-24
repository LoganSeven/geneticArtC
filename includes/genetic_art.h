#ifndef GENETIC_ART_H
#define GENETIC_ART_H

#ifdef __cplusplus
extern "C" {
#endif

#include "genetic_structs.h"
#include <pthread.h>
#include <stdatomic.h>

/**
 * @brief Function pointer type for GA fitness evaluation.
 *
 * The GA engine calls this function to obtain the fitness of a Chromosome.
 */
typedef double (*GAFitnessFunc)(const Chromosome *c, void *user_data);

/**
 * @brief GAContext — central structure for the GA engine.
 */
typedef struct GAContext {
    /* runtime GA tunables */
    const GAParams     *params;

    /* Stop flag: 0 ⇒ GA thread must exit */
    atomic_int         *running;

    /* Memory mgmt function pointers */
    Chromosome *(*alloc_chromosome)(size_t n_shapes);
    void        (*free_chromosome)(Chromosome *c);

    /* Best solution (thread-safe) */
    pthread_mutex_t    *best_mutex;
    Chromosome         *best_snapshot;

    /* Callback-based fitness evaluation */
    GAFitnessFunc       fitness_func;
    void               *fitness_data;
} GAContext;

/**
 * @brief Entry point for the GA thread — meant to be used with pthread_create().
 *
 * @param arg Must point to a fully initialized GAContext.
 * @return NULL on exit
 */
void *ga_thread_func(void *arg);

#ifdef __cplusplus
}
#endif

#endif /* GENETIC_ART_H */

// =============================================================
// >>>>>>>>>>>>>>>>>>>>>>  genetic_art.h  <<<<<<<<<<<<<<<<<<<<<<
// =============================================================

/**
 * @file   genetic_art.h
 * @brief  Public API for the genetic‑algorithm renderer.
 *
 * The GA runs on its own POSIX thread.  Synchronisation between the
 * GUI thread and GA thread is limited to:
 *   • a single pthread_mutex_t protecting @ref GAContext::best_pixels;
 *   • an atomic boolean flag that signals termination.
 *
 * @note  The GA thread owns *all* random number generation to avoid the
 *        need for TLS‑based PRNGs.
 */

#ifndef GENETIC_ART_H
#define GENETIC_ART_H

#include <SDL2/SDL.h>
#include <pthread.h>
#include <stdatomic.h>

/* -------------------------- Fixed geometry ----------------------------- */
#define WIDTH     1280   /* window width  ( = 2 × IMAGE_W ) */
#define HEIGHT     480   /* window height               */
#define IMAGE_W    640
#define IMAGE_H    480

/* -------------------- Genetic‑algorithm parameters -------------------- */
#define POPULATION_SIZE  50      /* chromosomes per generation        */
#define NB_SHAPES        50      /* genes (shapes) per chromosome     */
#define MUTATION_RATE    0.05f   /* probability gene mutates          */
#define CROSSOVER_RATE   0.7f    /* probability we do crossover       */
#define MAX_ITERATIONS   1000000 /* hard stop to avoid run‑aways      */

/* -------------------------- Gene definitions -------------------------- */
typedef enum {
    SHAPE_CIRCLE = 0,
    SHAPE_TRIANGLE
} ShapeType;

typedef struct {
    ShapeType type;  /* which rasteriser to invoke                */
    /* circle data */
    int cx, cy, radius;
    /* triangle data */
    int x1, y1, x2, y2, x3, y3;
    /* colour */
    unsigned char r, g, b, a;
} Gene;

typedef struct {
    Gene   shapes[NB_SHAPES];
    double fitness;         /* lower is better                       */
} Chromosome;

/* ------------------------- Thread context ----------------------------- */
typedef struct {
    const Uint32      *src_pixels;   /* immutable reference image      */
    SDL_PixelFormat   *fmt;          /* ARGB8888 (copied from SDL)     */
    Uint32            *best_pixels;  /* shared buffer – protected by mutex */
    int                pitch;        /* bytes per row                  */
    pthread_mutex_t   *best_mutex;   /* protects best_pixels & best fitness */
    atomic_int        *running;      /* 0 ⇒ GA thread must exit        */
} GAContext;

#ifdef __cplusplus
extern "C" {
#endif

/** Worker entry point passed to @c pthread_create().
 *  @param arg pointer to a valid, fully‑initialised @ref GAContext.
 */
void *ga_thread_func(void *arg);

#ifdef __cplusplus
}
#endif

#endif /* GENETIC_ART_H */

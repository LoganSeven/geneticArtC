// =============================================================
// >>>>>>>>>>>>>>>>>>>>>>  genetic_art.h  <<<<<<<<<<<<<<<<<<<<<<
// =============================================================
// Public API for the genetic‑algorithm worker.  This header now
// depends only on SDL and the lightweight "genetic_structs.h"
// where all runtime‑tunable GA parameters live.
// =============================================================
#ifndef GENETIC_ART_H
#define GENETIC_ART_H

#ifdef __cplusplus
extern "C" {
#endif


#include "./genetic_structs.h"
#include <SDL2/SDL.h>
#include <pthread.h>
#include <stdatomic.h>

/* -------------------------- Fixed GUI geometry -------------------------
 * These macros are kept here because the SDL demo still assumes a
 * 640×480 reference image and shows the candidate on the right.
 * They will move to a GUI‑specific header later because I want the GA core
 * to be 100 % display‑agnostic.
 */
#define WIDTH     1280   /* window width  ( = 2 × IMAGE_W ) */
#define HEIGHT     960   /* << CHANGED from 480 to 960 for new GUI area */
#define IMAGE_W    640
#define IMAGE_H    480

/*--------------thread workers & Island Model config --------------*/
#define FIT_MAX_WORKERS    8

/*  STEP 0: Add new Island Model macros  */
#define ISLAND_COUNT       4    /* default # of islands (and threads) */
#define MIGRATION_INTERVAL 5    /* generations between migrations     */
#define MIGRANTS_PER_ISL   1    /* #elite copies exchanged            */

/* ----------------------------------------------------------------------
 * GAContext — shared, *read‑only* after initialisation except where
 * explicitly documented.  The GUI thread owns the struct and must keep
 * it alive while the worker runs.
 * --------------------------------------------------------------------*/
typedef struct GAContext {
    /* --- immutable fields -------------------------------------------- */
    const GAParams     *params;      /* runtime GA tunables (non‑NULL)  */
    const Uint32       *src_pixels;  /* immutable reference image       */
    SDL_PixelFormat    *fmt;         /* ARGB8888 (copied from SDL)      */
    int                 pitch;       /* bytes per row for *all* buffers */

    /* --- shared / mutable fields ------------------------------------- */
    Uint32            *best_pixels;  /* worker writes under best_mutex  */
    pthread_mutex_t   *best_mutex;   /* guards best_pixels + best stats */

    /* Stop flag owned by GUI thread, polled by worker.                  */
    atomic_int        *running;      /* 0 ⇒ GA thread must exit         */
} GAContext;

/* ------------------------------- API ---------------------------------- */

/**
 * @brief Entry point for the GA worker — meant to be passed directly to
 *        pthread_create().  The @p arg must be a pointer to a fully
 *        initialised GAContext that remains valid for the thread’s
 *        lifetime.
 */
void *ga_thread_func(void *arg);

/**
 * @brief Deep-copies the genome of a Chromosome into another.
 *
 * This function copies the array of Gene from src to dst in-place.
 * Both chromosomes must already be allocated and must have the same n_shapes.
 * It does **not** reallocate memory or clone the Chromosome itself, only the
 * internal Gene array is overwritten.
 *
 * @param dst Pointer to the destination Chromosome (already allocated).
 * @param src Pointer to the source Chromosome whose genes will be copied.
 */
 void copy_chromosome(Chromosome *dst, const Chromosome *src);


#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* GENETIC_ART_H */

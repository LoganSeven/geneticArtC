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

/* IMPORTANT: Ensure we include genetic_structs.h first
 * so that GAParams is defined before we use it below.
 */
#include "genetic_structs.h"

#include <SDL2/SDL.h>
#include <pthread.h>
#include <stdatomic.h>

/* -------------------------- Fixed GUI geometry -------------------------
 * These macros are kept here because the SDL demo still assumes a
 * 640×480 reference image and shows the candidate on the right.
 * They can move to a GUI‑specific header later if you want the GA core
 * to be 100 % display‑agnostic.
 */
#define WIDTH     1280   /* window width  ( = 2 × IMAGE_W ) */
#define HEIGHT     480   /* window height                    */
#define IMAGE_W    640
#define IMAGE_H    480
/*--------------thread workers --------------*/
#define FIT_MAX_WORKERS 8
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

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* GENETIC_ART_H */

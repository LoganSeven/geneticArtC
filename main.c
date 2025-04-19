// =============================================================
// Genetic Algorithm Art Demo (SDL2, POSIX threads)
// -------------------------------------------------------------
// This project demonstrates how a very small genetic algorithm
// (GA) can gradually approximate a reference bitmap using only
// a handful of filled circles and triangles.  It is **pure C**
// (C99‑compatible) and builds on every modern desktop Linux with
//   gcc main.c genetic_art.c -o genetic_art -lSDL2 -lm -pthread
// I will also provide cmake files 
// -------------------------------------------------------------
// The code base is split into three units:
//   • main.c          — program entry, SDL2 window / texture logic
//   • genetic_art.h   — public interface shared by main & GA core
//   • genetic_art.c   — GA engine, shapes rasteriser & worker thread
//
// The goal of this code is not to demonstrate rasterization tricks nor
// to build a graphic library.
// SDL is well known, the foot print is reasonable, and it's X platform and C,
// and C friendly
// I would like to present my capacity to handle some concepts floating arounds
// the framework. In pure C if needed ;)
//
// I plan to use G.A. in much more usefull ways. e.g. system prompt optimisation
// graph node optimisation research etc..
//
// =============================================================

// =============================================================
// >>>>>>>>>>>>>>>>>>>>>>>  main.c  <<<<<<<<<<<<<<<<<<<<<<<<<<<<
// =============================================================

/**
 * @file   main.c
 * @brief  SDL2 front‑end for the genetic art demo.
 *
 * The main thread is responsible for
 *   1. initialising SDL2 & loading the reference bitmap;
 *   2. spawning the GA worker thread;
 *   3. presenting both the reference image (left) and the best
 *      candidate so far produced by the GA (right).
 *
 * No SDL calls are made from the GA thread – all rendering‑related
 * data is shared through a small, immutable @ref GAContext structure
 * that the worker receives *by pointer* at start‑up.  Only a single
 * critical section protects @c bestPixels so the GUI can read while
 * the GA writes.
 */

#include <SDL2/SDL.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <stdatomic.h>
#include "genetic_art.h"

/************************* Forward decls *************************/
static SDL_Surface *load_and_resize_bmp(const char *filename);

/************************* Globals *******************************/
static SDL_Window   *g_window   = NULL;
static SDL_Renderer *g_renderer = NULL;
static SDL_Texture  *g_ref_tex  = NULL; /* immutable */
static SDL_Texture  *g_best_tex = NULL; /* updated every frame */

/* shared GA data ------------------------------------------------*/
static Uint32       *g_ref_pixels  = NULL;
static Uint32       *g_best_pixels = NULL;
static SDL_PixelFormat *g_fmt      = NULL;
static int             g_pitch     = 0;   /* bytes‑per‑row for best/ref */
static pthread_mutex_t g_best_mutex = PTHREAD_MUTEX_INITIALIZER;
static atomic_int      g_running    = 1;   /* 0 -> stop GA thread        */

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <image.bmp>\n", argv[0]);
        return EXIT_FAILURE;
    }

    srand((unsigned)time(NULL));

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return EXIT_FAILURE;
    }

    /* 1. Window & renderer ------------------------------------------------*/
    g_window = SDL_CreateWindow("Genetic Art (GA demo)",
                                SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                WIDTH, HEIGHT, SDL_WINDOW_SHOWN);
    if (!g_window) {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        goto cleanup_sdl;
    }

    g_renderer = SDL_CreateRenderer(g_window, -1,
                                    SDL_RENDERER_ACCELERATED |
                                    SDL_RENDERER_PRESENTVSYNC);
    if (!g_renderer) {
        fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
        goto cleanup_window;
    }

    /* 2. Load & centre the reference bitmap -------------------------------*/
    SDL_Surface *surf = load_and_resize_bmp(argv[1]);
    if (!surf) goto cleanup_renderer;

    g_fmt        = SDL_AllocFormat(surf->format->format);
    g_pitch      = IMAGE_W * (int)sizeof(Uint32);

    g_ref_pixels  = malloc(IMAGE_W * IMAGE_H * sizeof(Uint32));
    g_best_pixels = calloc(IMAGE_W * IMAGE_H, sizeof(Uint32));

    if (!g_ref_pixels || !g_best_pixels || !g_fmt) {
        fprintf(stderr, "Memory allocation failure\n");
        goto cleanup_surface;
    }

    /* Copy pixels from the surface (row‑aligned) --------------------------*/
    SDL_LockSurface(surf);
    for (int y = 0; y < IMAGE_H; ++y) {
        const Uint32 *sp = (const Uint32 *)((const Uint8 *)surf->pixels + y * surf->pitch);
        Uint32       *dp = &g_ref_pixels[y * IMAGE_W];
        for (int x = 0; x < IMAGE_W; ++x) dp[x] = sp[x];
    }
    SDL_UnlockSurface(surf);

    g_ref_tex = SDL_CreateTextureFromSurface(g_renderer, surf);
    SDL_FreeSurface(surf);
    surf = NULL;

    if (!g_ref_tex) {
        fprintf(stderr, "SDL_CreateTextureFromSurface: %s\n", SDL_GetError());
        goto cleanup_surface; /* surf already freed */
    }

    /* 3. Streaming texture that mirrors g_best_pixels ---------------------*/
    g_best_tex = SDL_CreateTexture(g_renderer, g_fmt->format,
                                   SDL_TEXTUREACCESS_STREAMING,
                                   IMAGE_W, IMAGE_H);
    if (!g_best_tex) {
        fprintf(stderr, "SDL_CreateTexture (best): %s\n", SDL_GetError());
        goto cleanup_surface;
    }

    /************************* GA thread ***********************************/
    GAContext ctx = {
        .src_pixels  = g_ref_pixels,
        .fmt         = g_fmt,
        .best_pixels = g_best_pixels,
        .pitch       = g_pitch,
        .best_mutex  = &g_best_mutex,
        .running     = &g_running
    };

    pthread_t ga_tid;
    if (pthread_create(&ga_tid, NULL, ga_thread_func, &ctx) != 0) {
        perror("pthread_create");
        goto cleanup_surface;
    }

    /** Main event loop ---------------------------------------------------*/
    int quit = 0;
    while (!quit) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) quit = 1;
        }

        /* Clear back‑buffer */
        SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 255);
        SDL_RenderClear(g_renderer);

        /* Draw reference (left half) */
        SDL_Rect dst_ref = {0, 0, IMAGE_W, IMAGE_H};
        SDL_RenderCopy(g_renderer, g_ref_tex, NULL, &dst_ref);

        /* Draw best candidate (right half) */
        pthread_mutex_lock(&g_best_mutex);
        SDL_UpdateTexture(g_best_tex, NULL, g_best_pixels, g_pitch);
        pthread_mutex_unlock(&g_best_mutex);

        SDL_Rect dst_best = {IMAGE_W, 0, IMAGE_W, IMAGE_H};
        SDL_RenderCopy(g_renderer, g_best_tex, NULL, &dst_best);

        SDL_RenderPresent(g_renderer);
    }

    /* Signal GA thread to stop & wait */
    g_running = 0;
    pthread_join(ga_tid, NULL);

    /************************* tidy up *************************************/
cleanup_surface:
    free(g_best_pixels);
    free(g_ref_pixels);
    if (g_best_tex) SDL_DestroyTexture(g_best_tex);
    if (g_ref_tex)  SDL_DestroyTexture(g_ref_tex);
    if (g_fmt)      SDL_FreeFormat(g_fmt);
cleanup_renderer:
    if (g_renderer) SDL_DestroyRenderer(g_renderer);
cleanup_window:
    if (g_window)   SDL_DestroyWindow(g_window);
cleanup_sdl:
    SDL_Quit();
    return EXIT_SUCCESS;
}

/* ------------------------------------------------------------------------
 * Helper: load 24/32‑bit BMP, letter‑box into IMAGE_W × IMAGE_H keeping
 * aspect ratio.  Always returns a 32‑bit ‘ARGB8888’ surface matching the
 * renderer’s pixel format, ready for @c SDL_CreateTextureFromSurface().
 * ----------------------------------------------------------------------*/
static SDL_Surface *load_and_resize_bmp(const char *filename)
{
    SDL_Surface *orig = SDL_LoadBMP(filename);
    if (!orig) {
        fprintf(stderr, "SDL_LoadBMP('%s'): %s\n", filename, SDL_GetError());
        return NULL;
    }

    /* If already at target size, just convert to 32‑bit */
    if (orig->w == IMAGE_W && orig->h == IMAGE_H) {
        SDL_Surface *conv = SDL_ConvertSurfaceFormat(orig, SDL_PIXELFORMAT_ARGB8888, 0);
        SDL_FreeSurface(orig);
        return conv;
    }

    /* letter‑box scaling ------------------------------------------------*/
    const float scale_w = (float)IMAGE_W / (float)orig->w;
    const float scale_h = (float)IMAGE_H / (float)orig->h;
    const float scale   = (scale_w < scale_h) ? scale_w : scale_h;

    const int new_w = (int)(orig->w * scale + 0.5f);
    const int new_h = (int)(orig->h * scale + 0.5f);

    SDL_Surface *tmp = SDL_CreateRGBSurfaceWithFormat(0, new_w, new_h, 32,
                                                      SDL_PIXELFORMAT_ARGB8888);
    SDL_BlitScaled(orig, NULL, tmp, NULL);

    SDL_Surface *final = SDL_CreateRGBSurfaceWithFormat(0, IMAGE_W, IMAGE_H, 32,
                                                        SDL_PIXELFORMAT_ARGB8888);
    SDL_FillRect(final, NULL, SDL_MapRGB(final->format, 0, 0, 0));

    SDL_Rect dst = {(IMAGE_W - new_w) / 2, (IMAGE_H - new_h) / 2, new_w, new_h};
    SDL_BlitSurface(tmp, NULL, final, &dst);

    SDL_FreeSurface(tmp);
    SDL_FreeSurface(orig);
    return final;
}

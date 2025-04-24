/**
 * @file main_runtime.c
 * @brief Runtime init and SDL/Nuklear integration for the Genetic Art demo.
 *
 * This file sets up SDL2, Nuklear (with font baking), and rendering dimensions.
 * It also provides reference image loading and the initial GAContext builder.
 */

// ---- Nuklear macros MUST precede nuklear.h --------------------------------
#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_COMMAND_USERDATA
#include "../includes/Nuklear/nuklear.h"

#include "../includes/software_rendering/main_runtime.h"
#include "../includes/fonts_as_header/embedded_font.h"
#include "../includes/software_rendering/nuklear_sdl_renderer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <SDL2/SDL.h>

// ---- Dimensions fallback ---------------------------------------------------
#ifndef WIDTH
#define WIDTH     1280
#endif
#ifndef HEIGHT
#define HEIGHT    960
#endif
#ifndef IMAGE_W
#define IMAGE_W   640
#endif
#ifndef IMAGE_H
#define IMAGE_H   480
#endif

// ---- External GUI log buffers ---------------------------------------------
extern pthread_mutex_t g_log_mutex;
extern char g_log_text[1024][512];
extern struct nk_color g_log_color[1024];
extern int g_log_count;

// ---- Local SDL handles -----------------------------------------------------
static SDL_Window   *g_window   = NULL;
static SDL_Renderer *g_renderer = NULL;
static struct nk_context *g_nk  = NULL;

// ----------------------------------------------------------------------------
// SDL2 Initialization
// ----------------------------------------------------------------------------
int init_sdl_and_window(SDL_Window **window, SDL_Renderer **renderer)
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return -1;
    }

    *window = SDL_CreateWindow("Genetic Art",
                               SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                               WIDTH, HEIGHT, SDL_WINDOW_SHOWN);
    if (!*window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        return -1;
    }

    *renderer = SDL_CreateRenderer(*window, -1,
                                   SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!*renderer) {
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        return -1;
    }

    return 0;
}

int init_nuklear_and_font(struct nk_context **nk_ctx, SDL_Window *window, SDL_Renderer *renderer)
{
    (void)window; // mark as unused for now

    *nk_ctx = nk_sdl_init(window, renderer);
    if (!*nk_ctx) return -1;

    struct nk_font_atlas *atlas;
    nk_sdl_font_stash_begin(&atlas);

    struct nk_font *font = nk_font_atlas_add_from_memory(
        atlas, (void *)amiga4ever_ttf, amiga4ever_ttf_len, 12.0f, NULL);

    if (!font)
        font = nk_font_atlas_add_default(atlas, 13.0f, NULL);

    nk_sdl_font_stash_end();
    if (font) nk_style_set_font(*nk_ctx, &font->handle);

    return 0;
}

SDL_Texture *load_reference_image(const char *filename, SDL_Renderer *renderer,
                                  SDL_PixelFormat **fmt, Uint32 **ref_pixels)
{
    SDL_Surface *orig = SDL_LoadBMP(filename);
    if (!orig) return NULL;

    SDL_Surface *final;
    if (orig->w == IMAGE_W && orig->h == IMAGE_H) {
        final = SDL_ConvertSurfaceFormat(orig, SDL_PIXELFORMAT_ARGB8888, 0);
        SDL_FreeSurface(orig);
    } else {
        float scale = fminf((float)IMAGE_W / orig->w, (float)IMAGE_H / orig->h);
        int new_w = (int)(orig->w * scale);
        int new_h = (int)(orig->h * scale);
        SDL_Surface *tmp = SDL_CreateRGBSurfaceWithFormat(0, new_w, new_h, 32, SDL_PIXELFORMAT_ARGB8888);
        SDL_BlitScaled(orig, NULL, tmp, NULL);

        final = SDL_CreateRGBSurfaceWithFormat(0, IMAGE_W, IMAGE_H, 32, SDL_PIXELFORMAT_ARGB8888);
        SDL_FillRect(final, NULL, SDL_MapRGB(final->format, 0, 0, 0));
        SDL_Rect dst = { (IMAGE_W - new_w)/2, (IMAGE_H - new_h)/2, new_w, new_h };
        SDL_BlitSurface(tmp, NULL, final, &dst);

        SDL_FreeSurface(tmp);
        SDL_FreeSurface(orig);
    }

    *fmt = SDL_AllocFormat(final->format->format);
    *ref_pixels = malloc(IMAGE_W * IMAGE_H * sizeof(Uint32));
    SDL_LockSurface(final);
    for (int y = 0; y < IMAGE_H; y++) {
        const Uint32 *sp = (const Uint32 *)((Uint8 *)final->pixels + y * final->pitch);
        memcpy(&(*ref_pixels)[y * IMAGE_W], sp, IMAGE_W * sizeof(Uint32));
    }
    SDL_UnlockSurface(final);

    SDL_Texture *tex = SDL_CreateTextureFromSurface(renderer, final);
    SDL_FreeSurface(final);
    return tex;
}

GAContext build_ga_context(Uint32 *ref_pixels, Uint32 *best_pixels,
                           SDL_PixelFormat *fmt, int pitch,
                           atomic_int *running)
{
    GAParams *params = malloc(sizeof(GAParams));
    *params = (GAParams){500, 100, 2, 0.05f, 0.70f, 1000000};

    GAFitnessParams *fp = malloc(sizeof(GAFitnessParams));
    fp->ref_pixels     = ref_pixels;
    fp->scratch_pixels = calloc(IMAGE_W * IMAGE_H, sizeof(Uint32));
    fp->fmt            = fmt;
    fp->pitch          = pitch;
    fp->width          = IMAGE_W;
    fp->height         = IMAGE_H;

    GAContext ctx = {
        .params           = params,
        .running          = running,
        .alloc_chromosome = chromosome_create,
        .free_chromosome  = chromosome_destroy,
        .best_mutex       = malloc(sizeof(pthread_mutex_t)),
        .best_snapshot    = chromosome_create(params->nb_shapes),
        .fitness_func     = ga_sdl_fitness_callback,
        .fitness_data     = fp,
        .log_func         = NULL,
        .log_user_data    = NULL
    };

    pthread_mutex_init(ctx.best_mutex, NULL);
    return ctx;
}

void run_main_loop(GAContext *ctx, struct nk_context *nk, SDL_Window *window,
                   SDL_Renderer *renderer, SDL_Texture *tex_ref,
                   SDL_Texture *tex_best, Uint32 *best_pixels, int pitch)
{
    (void)best_pixels; // silence unused parameter warning

    SDL_Event ev;
    while (atomic_load(ctx->running)) {
        nk_input_begin(nk);
        while (SDL_PollEvent(&ev)) {
            nk_sdl_handle_event(&ev);
            if (ev.type == SDL_QUIT)
                atomic_store(ctx->running, 0);
        }
        nk_input_end(nk);

        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, tex_ref, NULL, &(SDL_Rect){0, 0, IMAGE_W, IMAGE_H});

        pthread_mutex_lock(ctx->best_mutex);
        render_chrom(ctx->best_snapshot, best_pixels, pitch,
                     ((GAFitnessParams *)ctx->fitness_data)->fmt, IMAGE_W, IMAGE_H);
        pthread_mutex_unlock(ctx->best_mutex);

        SDL_UpdateTexture(tex_best, NULL, best_pixels, pitch);
        SDL_RenderCopy(renderer, tex_best, NULL, &(SDL_Rect){IMAGE_W, 0, IMAGE_W, IMAGE_H});

        if (nk_begin(nk, "Log", nk_rect(0,480,640,480), NK_WINDOW_BORDER|NK_WINDOW_SCROLL_AUTO_HIDE|NK_WINDOW_TITLE)) {
            nk_layout_row_dynamic(nk, 18, 1);
            pthread_mutex_lock(&g_log_mutex);
            for (int i = 0; i < g_log_count; i++)
                nk_text_colored(nk, g_log_text[i], strlen(g_log_text[i]), NK_TEXT_LEFT, g_log_color[i]);
            pthread_mutex_unlock(&g_log_mutex);
        }
        nk_end(nk);

        if (nk_begin(nk, "Future Widgets", nk_rect(640,480,640,480), NK_WINDOW_BORDER|NK_WINDOW_TITLE)) {
            nk_layout_row_dynamic(nk, 30, 1);
            nk_label(nk, "Put your GUI controls here.", NK_TEXT_LEFT);
        }
        nk_end(nk);

        nk_sdl_render(NK_ANTI_ALIASING_ON);
        SDL_RenderPresent(renderer);
        nk_clear(nk);
        SDL_Delay(10);
    }
}

void destroy_ga_context(GAContext *ctx)
{
    if (!ctx) return;
    GAFitnessParams *fp = (GAFitnessParams *)ctx->fitness_data;
    free(fp->scratch_pixels);
    free(fp);
    pthread_mutex_destroy(ctx->best_mutex);
    free(ctx->best_mutex);
    ctx->free_chromosome(ctx->best_snapshot);
    free((void *)ctx->params); // remove const cast warning
}

void cleanup_all(void)
{
    if (g_nk) nk_sdl_shutdown();
    if (g_renderer) SDL_DestroyRenderer(g_renderer);
    if (g_window) SDL_DestroyWindow(g_window);
    SDL_Quit();
}

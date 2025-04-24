#ifndef MAIN_RUNTIME_H
#define MAIN_RUNTIME_H



#include <SDL2/SDL.h>
#include <stdatomic.h>
#include "../genetic_algorythm/genetic_art.h"
#include "../genetic_algorythm/genetic_structs.h"
#include "ga_renderer.h"

/* Forward declaration of Nuklear context */
struct nk_context;

/**
 * @brief Initialize SDL, window, renderer.
 *
 * @return 0 on success, -1 on error.
 */
int init_sdl_and_window(SDL_Window **window, SDL_Renderer **renderer);

/**
 * @brief Initialize Nuklear and load the embedded font.
 *
 * @param nk_ctx Output pointer to Nuklear context.
 * @param window SDL window.
 * @param renderer SDL renderer.
 * @return 0 on success, -1 on failure.
 */
int init_nuklear_and_font(struct nk_context **nk_ctx, SDL_Window *window, SDL_Renderer *renderer);

/**
 * @brief Load and convert BMP into a format suitable for rendering and GA input.
 *
 * @param filename Path to the BMP image.
 * @param fmt Output SDL_PixelFormat (caller frees).
 * @param ref_pixels Output ARGB buffer (allocated).
 * @param tex Output texture to render on screen.
 * @return SDL_Texture* reference image texture or NULL on error.
 */
SDL_Texture *load_reference_image(const char *filename,
                                  SDL_Renderer *renderer,
                                  SDL_PixelFormat **fmt,
                                  Uint32 **ref_pixels);

/**
 * @brief Build and return a fully-initialized GAContext struct.
 *
 * @param ref_pixels Reference pixels for fitness.
 * @param fmt SDL pixel format.
 * @param best_pixels Output: memory to render best candidate.
 * @param pitch Pixel row pitch.
 * @param running Shared atomic flag for stopping.
 * @return Fully configured GAContext (must be freed manually).
 */
GAContext build_ga_context(Uint32 *ref_pixels,
                           Uint32 *best_pixels,
                           SDL_PixelFormat *fmt,
                           int pitch,
                           atomic_int *running);

/**
 * @brief Run the main application loop.
 *
 * @param ctx Pointer to initialized GAContext.
 * @param nk Pointer to Nuklear context.
 * @param window SDL_Window.
 * @param renderer SDL_Renderer.
 * @param tex_ref Reference texture.
 * @param tex_best Texture updated with best candidate.
 * @param best_pixels Pointer to best ARGB buffer.
 * @param pitch Pixel pitch for the buffer.
 */
void run_main_loop(GAContext *ctx,
                   struct nk_context *nk,
                   SDL_Window *window,
                   SDL_Renderer *renderer,
                   SDL_Texture *tex_ref,
                   SDL_Texture *tex_best,
                   Uint32 *best_pixels,
                   int pitch);

/**
 * @brief Free all SDL/Nuklear resources and shutdown cleanly.
 */
void cleanup_all(void);

#endif /* MAIN_RUNTIME_H */

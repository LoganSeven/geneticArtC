#ifndef MAIN_RUNTIME_H
#define MAIN_RUNTIME_H

/**
 * @file main_runtime.h
 * @brief Declarations for SDL2 and Nuklear initialization, reference image loading, and main loop management.
 * @details
 * This header defines the functions necessary to:
 * - Initialize SDL2 subsystems, window, and renderer.
 * - Initialize the Nuklear context and font management.
 * - Load and convert a BMP image for use as a reference target.
 * - Create and configure the genetic algorithm (GA) execution context.
 * - Run the main graphical application loop, combining SDL2 rendering and Nuklear GUI interaction.
 * - Cleanly destroy GA contexts and free SDL2/Nuklear resources during shutdown.
 *
 * @path includes/main_runtime.h
 */

#include <SDL2/SDL.h>            /**< SDL2 library for window, renderer, and event handling. */
#include <stdatomic.h>           /**< Atomic operations library used for concurrency control. */
#include "../genetic_algorithm/genetic_art.h" /**< Genetic Algorithm core structures and definitions. */

/* Forward declaration of Nuklear context */
struct nk_context;

/**
 * @brief Initialize SDL2, create a window, and create an SDL2 renderer.
 *
 * @param[out] window   Address of a pointer to an SDL_Window that will be initialized.
 * @param[out] renderer Address of a pointer to an SDL_Renderer that will be initialized.
 *
 * @return 0 on success, -1 on any initialization failure.
 *
 * @details
 * Initializes the SDL2 library subsystems required for window and 2D rendering operations.
 * If successful, returns initialized SDL_Window and SDL_Renderer pointers.
 *
 * @example
 * @code
 * SDL_Window *win = NULL;
 * SDL_Renderer *rend = NULL;
 * if (init_sdl_and_window(&win, &rend) < 0) {
 *     // Handle initialization error
 * }
 * @endcode
 */
int init_sdl_and_window(SDL_Window **window, SDL_Renderer **renderer);

/**
 * @brief Initialize the Nuklear GUI context and load an embedded or default font.
 *
 * @param[out] nk_ctx   Address of a pointer to a struct nk_context to initialize.
 * @param[in]  window   Pointer to the SDL_Window used for context setup.
 * @param[in]  renderer Pointer to the SDL_Renderer used for GUI rendering operations.
 *
 * @return 0 on success, -1 on failure (e.g., font loading or Nuklear initialization error).
 *
 * @details
 * Sets up a Nuklear context bound to the specified SDL_Window and SDL_Renderer.
 * Attempts to load an embedded TTF font at a configured size; defaults to a built-in font otherwise.
 */
int init_nuklear_and_font(struct nk_context **nk_ctx,
                          SDL_Window *window,
                          SDL_Renderer *renderer);

/**
 * @brief Load and convert a BMP image into a texture and reference pixel buffer.
 *
 * @param[in]  filename    Path to the BMP file to load.
 * @param[in]  renderer    Pointer to the SDL_Renderer used for texture creation.
 * @param[out] fmt         Address of a pointer to SDL_PixelFormat; must be manually freed with SDL_FreeFormat().
 * @param[out] ref_pixels  Address of a pointer to an ARGB8888 buffer representing the reference image.
 *
 * @return SDL_Texture* containing the processed and converted BMP image, or NULL on failure.
 *
 * @details
 * Loads a BMP image from disk. If dimensions differ from (IMAGE_W × IMAGE_H),
 * the image is resized and centered over a black background, then converted
 * to the ARGB8888 pixel format. A pixel buffer copy for fitness computation is also returned.
 */
SDL_Texture *load_reference_image(const char *filename,
                                  SDL_Renderer *renderer,
                                  SDL_PixelFormat **fmt,
                                  Uint32 **ref_pixels);

/**
 * @brief Build and return a fully initialized GAContext structure.
 *
 * @param[in] ref_pixels   Pointer to the loaded reference ARGB pixel buffer.
 * @param[in] best_pixels  Pointer to the best candidate ARGB buffer.
 * @param[in] fmt          Pointer to the SDL_PixelFormat describing the buffer layout.
 * @param[in] pitch        Row size in bytes for the ARGB pixel buffers.
 * @param[in] running      Pointer to an atomic integer flag used to control GA execution state.
 *
 * @return A configured GAContext structure ready for Genetic Algorithm operations.
 *
 * @details
 * Initializes all necessary internal structures:
 * - GAParams for evolutionary parameters.
 * - GAFitnessParams for fitness evaluation.
 * - Initial best_snapshot Chromosome.
 * The GAContext embeds function pointers for chromosome management and fitness computation.
 * 
 * The `destroy_ga_context()` function must be called to release internal allocations.
 */
GAContext build_ga_context(Uint32 *ref_pixels,
                           Uint32 *best_pixels,
                           SDL_PixelFormat *fmt,
                           int pitch,
                           atomic_int *running);

/**
 * @brief Run the main graphical and control loop of the application.
 *
 * @param[in,out] ctx         Pointer to the initialized GAContext.
 * @param[in,out] nk          Pointer to the Nuklear GUI context.
 * @param[in]     window      Pointer to the SDL_Window in use.
 * @param[in]     renderer    Pointer to the SDL_Renderer for drawing operations.
 * @param[in]     tex_ref     Texture representing the reference image.
 * @param[in]     tex_best    Texture representing the current best candidate solution.
 * @param[in,out] best_pixels Pointer to the current best candidate ARGB buffer.
 * @param[in]     pitch       Pixel pitch (row size in bytes) for the ARGB buffers.
 *
 * @details
 * Polls SDL2 events and updates Nuklear input state.
 * Renders the reference image and the evolving best candidate side-by-side.
 * Displays GUI elements including logs, statistics, and future controls.
 * Refreshes the window at each frame.
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
 * @brief Release internal allocations within a GAContext structure.
 *
 * @param[in,out] ctx Pointer to the GAContext to deallocate.
 *
 * @details
 * Deallocates memory used by chromosomes, parameter structures, and fitness buffers.
 * The GAContext structure itself is not freed (if it resides on the stack).
 */
void destroy_ga_context(GAContext *ctx);

/**
 * @brief Cleanly shut down the application, freeing all allocated resources.
 *
 * @details
 * Destroys the Nuklear GUI context, releases SDL_Renderer and SDL_Window,
 * and calls SDL_Quit() to properly shut down SDL2 subsystems.
 */
void cleanup_all(void);

#endif /* MAIN_RUNTIME_H */

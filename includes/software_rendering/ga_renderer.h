#ifndef GA_RENDERER_H
#define GA_RENDERER_H

#include "../genetic_algorithm/genetic_structs.h"
#include <SDL2/SDL.h>

/**
 * @brief Parameters used for calculating chromosome fitness with SDL rendering.
 *
 * This structure holds all necessary buffers and information required for 
 * fitness calculation and rendering of chromosomes.
 */
typedef struct {
    const Uint32 *ref_pixels;          /**< Pointer to reference pixel buffer (ARGB format). */
    Uint32       *scratch_pixels;      /**< Pointer to temporary rendering pixel buffer (ARGB). */
    const SDL_PixelFormat *fmt;        /**< SDL PixelFormat used for correct pixel manipulation. */
    int pitch;                         /**< Number of bytes per row (typically width * 4). */
    int width;                         /**< Width of the rendering area in pixels. */
    int height;                        /**< Height of the rendering area in pixels. */
} GAFitnessParams;

/**
 * @brief Computes fitness (Mean Squared Error) of a chromosome against a reference image.
 *
 * Renders the given chromosome to a temporary buffer, then compares it with a 
 * reference image pixel-by-pixel using Mean Squared Error on RGB channels.
 * Optionally optimized with AVX2 instructions if available.
 *
 * @param c         Pointer to the chromosome whose fitness is evaluated.
 * @param user_data Pointer to GAFitnessParams structure containing necessary buffers.
 *
 * @return The computed fitness as a double precision floating point (MSE value).
 *
 * @note Intended to be set as a callback in GAContext (`fitness_func`).
 *
 * Example:
 * @code
 * ctx.fitness_func = ga_sdl_fitness_callback;
 * ctx.fitness_user_data = &fitness_params;
 * @endcode
 */
double ga_sdl_fitness_callback(const Chromosome *c, void *user_data);

/**
 * @brief Renders a chromosome into an ARGB pixel buffer.
 *
 * Clears the provided output buffer and sequentially renders each gene (shape) 
 * in the chromosome, applying alpha blending according to the SDL pixel format.
 *
 * This function is utilized both for fitness calculations and visualizing chromosomes.
 *
 * @param c     Pointer to the chromosome to render.
 * @param out   Target ARGB pixel buffer to render into.
 * @param pitch Number of bytes per row of the output buffer (usually width * 4).
 * @param fmt   SDL_PixelFormat structure for correct RGBA color handling.
 * @param w     Width of the output rendering area (pixels).
 * @param h     Height of the output rendering area (pixels).
 *
 * Example:
 * @code
 * render_chrom(chromosome, buffer, width * 4, pixel_format, width, height);
 * @endcode
 */
void render_chrom(const Chromosome *c, Uint32 *out, int pitch,
                  const SDL_PixelFormat *fmt, int w, int h);

#endif // GA_RENDERER_H

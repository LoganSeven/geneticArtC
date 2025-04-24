#ifndef GA_RENDERER_H
#define GA_RENDERER_H

#include "../genetic_algorythm/genetic_structs.h"
#include <SDL2/SDL.h>

typedef struct {
    const Uint32 *ref_pixels;
    Uint32       *scratch_pixels;
    const SDL_PixelFormat *fmt;
    int pitch;
    int width;
    int height;
} GAFitnessParams;

double ga_sdl_fitness_callback(const Chromosome *c, void *user_data);

/**
 * @brief  Renders a Chromosome to a pixel buffer using SDL pixel format.
 *         This is used both for fitness and for visualization.
 *
 * @param c     The chromosome to render.
 * @param out   The target buffer to write ARGB pixels to.
 * @param pitch Bytes per row (usually IMAGE_W * 4).
 * @param fmt   SDL_PixelFormat for correct RGBA unpacking.
 * @param w     Image width.
 * @param h     Image height.
 */
void render_chrom(const Chromosome *c, Uint32 *out, int pitch,
                  const SDL_PixelFormat *fmt, int w, int h);

#endif // GA_RENDERER_H

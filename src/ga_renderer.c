/**
 * @file ga_renderer.c
 * @brief Implements SDL-based rendering and fitness calculation for shape-based chromosomes.
 *
 * This module provides functions for rendering chromosomes composed of geometric shapes
 * (circles and triangles) into a software ARGB buffer, as well as computing their fitness
 * using Mean Squared Error (MSE).
 */

 #include "../includes/software_rendering/ga_renderer.h"
 #include <stdlib.h>
 #include <string.h>
 #include <math.h>
 #include <stdio.h>
 #ifdef __AVX2__
 #include <immintrin.h>
 #endif
 
 /**
  * @brief Clamps integer v within [lo..hi].
  *
  * This function ensures that the value `v` is within the specified range `[lo..hi]`.
  * If `v` is less than `lo`, it returns `lo`. If `v` is greater than `hi`, it returns `hi`.
  * Otherwise, it returns `v`.
  *
  * @param v Value to clamp.
  * @param lo Lower bound.
  * @param hi Upper bound.
  * @return The clamped integer result.
  */
 static inline int clampi(int v, int lo, int hi)
 {
     return (v < lo) ? lo : (v > hi) ? hi : v;
 }
 
 /**
  * @brief Performs alpha blending of source color over destination color (both ARGB).
  *
  * This function blends the source color `src` over the destination color `dst` using alpha blending.
  * The resulting alpha is forced to 255 to maintain opaque final pixels.
  *
  * @param dst Destination color ARGB.
  * @param src Source color ARGB.
  * @param fmt SDL_PixelFormat pointer.
  * @return Blended ARGB result with alpha channel = 255.
  */
 static Uint32 alpha_blend(Uint32 dst, Uint32 src, const SDL_PixelFormat *fmt)
 {
     if (!fmt)
         return src;
 
     Uint8 sr, sg, sb, sa;
     SDL_GetRGBA(src, fmt, &sr, &sg, &sb, &sa);
 
     Uint8 dr, dg, db, da;
     SDL_GetRGBA(dst, fmt, &dr, &dg, &db, &da);
 
     float a = sa / 255.0f;
     Uint8 rr = (Uint8)(sr * a + dr * (1.0f - a));
     Uint8 rg = (Uint8)(sg * a + dg * (1.0f - a));
     Uint8 rb = (Uint8)(sb * a + db * (1.0f - a));
 
     return SDL_MapRGBA(fmt, rr, rg, rb, 255);
 }
 
 /**
  * @brief Draws a filled circle via alpha blending into pixel buffer.
  *
  * This function draws a filled circle with the specified center, radius, and color into the pixel buffer.
  * It uses alpha blending to combine the circle's color with the existing pixel colors.
  *
  * @param px Pointer to the pixel buffer (ARGB).
  * @param pitch The row size in bytes of the buffer.
  * @param fmt SDL_PixelFormat pointer.
  * @param cx Center x-coordinate.
  * @param cy Center y-coordinate.
  * @param r Circle radius.
  * @param col ARGB color to fill.
  * @param width Intended image width in pixels.
  * @param height Image height in pixels.
  */
 static void draw_circle(Uint32 *px, int pitch, const SDL_PixelFormat *fmt,
                          int cx, int cy, int r, Uint32 col,
                          int width, int height)
 {
     if (!px || !fmt || r <= 0 || width <= 0 || height <= 0)
         return;
 
     int row_len = pitch / 4; /* Number of pixels per row */
     int r2 = r * r;
 
     for (int dy = -r; dy <= r; dy++) {
         int y = cy + dy;
         if (y < 0 || y >= height)
             continue;
 
         int dx_max = (int)sqrtf((float)(r2 - dy * dy));
         for (int dx = -dx_max; dx <= dx_max; dx++) {
             int x = cx + dx;
             if (x < 0 || x >= row_len)
                 continue;
 
             int idx = y * row_len + x;
             px[idx] = alpha_blend(px[idx], col, fmt);
         }
     }
 }
 
 /**
  * @brief Linear interpolation helper for triangle edges.
  *
  * This function performs linear interpolation to find the x-coordinate on the edge
  * between two points (xa, ya) and (xb, yb) at a given y-coordinate.
  *
  * @param y Current y coordinate.
  * @param xa First edge x.
  * @param ya First edge y.
  * @param xb Second edge x.
  * @param yb Second edge y.
  * @return Interpolated x at line y.
  */
 static inline float edge(int y, int xa, int ya, int xb, int yb)
 {
     if (yb == ya)
         return (float)xa;
     return xa + (xb - xa) * ((float)(y - ya) / (float)(yb - ya));
 }
 
 /**
  * @brief Draws a filled triangle via alpha blending into pixel buffer.
  *
  * This function draws a filled triangle with the specified vertices and color into the pixel buffer.
  * It uses alpha blending to combine the triangle's color with the existing pixel colors.
  *
  * @param px Pointer to the pixel buffer (ARGB).
  * @param pitch Row size in bytes of the buffer.
  * @param fmt SDL_PixelFormat pointer.
  * @param x1 X of vertex1.
  * @param y1 Y of vertex1.
  * @param x2 X of vertex2.
  * @param y2 Y of vertex2.
  * @param x3 X of vertex3.
  * @param y3 Y of vertex3.
  * @param col ARGB color to fill.
  * @param width Intended image width in pixels.
  * @param height Image height in pixels.
  */
 static void draw_triangle(Uint32 *px, int pitch, const SDL_PixelFormat *fmt,
                            int x1, int y1, int x2, int y2, int x3, int y3,
                            Uint32 col, int width, int height)
 {
     if (!px || !fmt || width <= 0 || height <= 0)
         return;
 
     int row_len = pitch / 4;
     x1 = clampi(x1, 0, row_len - 1);
     x2 = clampi(x2, 0, row_len - 1);
     x3 = clampi(x3, 0, row_len - 1);
     y1 = clampi(y1, 0, height - 1);
     y2 = clampi(y2, 0, height - 1);
     y3 = clampi(y3, 0, height - 1);
 
     if (y1 > y2) { int tx=x1; x1=x2; x2=tx; int ty=y1; y1=y2; y2=ty; }
     if (y1 > y3) { int tx=x1; x1=x3; x3=tx; int ty=y1; y1=y3; y3=ty; }
     if (y2 > y3) { int tx=x2; x2=x3; x3=tx; int ty=y2; y2=y3; y3=ty; }
 
     for (int y = y1; y <= y3; y++) {
         float xa, xb;
         if (y < y2)
             xa = edge(y, x1, y1, x2, y2);
         else
             xa = edge(y, x2, y2, x3, y3);
 
         xb = edge(y, x1, y1, x3, y3);
 
         if (xa > xb) {
             float t = xa;
             xa = xb;
             xb = t;
         }
         int ix_a = clampi((int)xa, 0, row_len - 1);
         int ix_b = clampi((int)xb, 0, row_len - 1);
 
         for (int x = ix_a; x <= ix_b; x++) {
             int idx = y * row_len + x;
             px[idx] = alpha_blend(px[idx], col, fmt);
         }
     }
 }
 
 /**
  * @brief Renders chromosome into ARGB buffer by drawing its shapes.
  *
  * This function renders the chromosome into the ARGB buffer by drawing each of its shapes
  * (circles and triangles) using the specified pixel format and dimensions.
  *
  * @param c Pointer to the chromosome.
  * @param out ARGB output buffer.
  * @param pitch Row size in bytes of the buffer.
  * @param fmt SDL_PixelFormat pointer.
  * @param width Target width in pixels.
  * @param height Target height in pixels.
  */
 void render_chrom(const Chromosome *c, Uint32 *out, int pitch,
                    const SDL_PixelFormat *fmt, int width, int height)
 {
     if (!c || !out || !fmt)
         return;
 
     size_t buffer_size = (size_t)height * (size_t)pitch;
     if ((buffer_size / (size_t)pitch) != (size_t)height)
         return;
 
     int row_len = pitch / 4;
     if (width <= 0 || height <= 0 || width > row_len)
         return;
 
     memset(out, 0, buffer_size);
 
     for (size_t i = 0; i < c->n_shapes; i++) {
         const Gene *g = &c->shapes[i];
         Uint32 col = SDL_MapRGBA(fmt, g->r, g->g, g->b, g->a);
 
         if (g->type == SHAPE_CIRCLE) {
             draw_circle(out, pitch, fmt,
                          g->geom.circle.cx,
                          g->geom.circle.cy,
                          g->geom.circle.radius,
                          col, width, height);
         } else {
             draw_triangle(out, pitch, fmt,
                            g->geom.triangle.x1, g->geom.triangle.y1,
                            g->geom.triangle.x2, g->geom.triangle.y2,
                            g->geom.triangle.x3, g->geom.triangle.y3,
                            col, width, height);
         }
     }
 }
 
 /**
  * @brief Computes MSE fitness for the candidate vs. reference (RGB only).
  *
  * This function computes the Mean Squared Error (MSE) between the candidate image and the reference image.
  * It calculates the squared difference of the RGB channels for each pixel and averages the result.
  *
  * @param cand Pointer to candidate ARGB buffer.
  * @param ref Pointer to reference ARGB buffer.
  * @param count_px Number of pixels to process.
  * @return Mean Squared Error over RGB channels.
  */
 static inline double fitness_scalar(const Uint32 *cand, const Uint32 *ref, int count_px)
 {
     double err = 0.0;
     for (int i = 0; i < count_px; i++) {
         int dr = ((cand[i] >> 16) & 0xFF) - ((ref[i] >> 16) & 0xFF);
         int dg = ((cand[i] >> 8) & 0xFF) - ((ref[i] >> 8) & 0xFF);
         int db = (cand[i] & 0xFF) - (ref[i] & 0xFF);
         err += (double)(dr*dr + dg*dg + db*db);
     }
     return err / (double)count_px;
 }
 
 #ifdef __AVX2__
 /**
  * @brief AVX2 accelerated MSE (RGB channels only).
  *
  * This function computes the Mean Squared Error (MSE) between the candidate image and the reference image
  * using AVX2 instructions for acceleration. It calculates the squared difference of the RGB channels
  * for each pixel and averages the result.
  *
  * @param cand Pointer to candidate ARGB buffer.
  * @param ref Pointer to reference ARGB buffer.
  * @param count_px Number of pixels.
  * @return MSE over RGB channels.
  */
 /**
 * @brief AVX2 accelerated MSE (RGB channels only).
 *
 * This function computes the Mean Squared Error (MSE) between the candidate image and the reference image
 * using AVX2 instructions for acceleration. It calculates the squared difference of the RGB channels
 * for each pixel and averages the result.
 *
 * @param cand Pointer to candidate ARGB buffer.
 * @param ref Pointer to reference ARGB buffer.
 * @param count_px Number of pixels.
 * @return MSE over RGB channels.
 */
static inline double fitness_avx2(const Uint32 *cand, const Uint32 *ref, int count_px)

    // Define masks for extracting the RGB components from the ARGB values
    __m256i maskR = _mm256_set1_epi32(0x00FF0000); // Mask for the red component
    __m256i maskG = _mm256_set1_epi32(0x0000FF00); // Mask for the green component
    __m256i maskB = _mm256_set1_epi32(0x000000FF); // Mask for the blue component
    // Initialize the accumulator for the sum of squared differences to zero
    __m256d accum = _mm256_setzero_pd();
    // Calculate the limit for the main loop, processing 8 pixels at a time
    int limit = (count_px / 8) * 8;
    int i = 0;
    // Main loop: process 8 pixels at a time using AVX2 instructions
    for (; i < limit; i += 8) {
        // Load 8 candidate and reference pixels into AVX2 registers
        __m256i C = _mm256_loadu_si256((const __m256i*)(cand + i));
        __m256i R = _mm256_loadu_si256((const __m256i*)(ref + i));
        // Extract the RGB components from the candidate pixels
        __m256i cR = _mm256_srli_epi32(_mm256_and_si256(C, maskR), 16); // Red component
        __m256i cG = _mm256_srli_epi32(_mm256_and_si256(C, maskG), 8);  // Green component
        __m256i cB = _mm256_and_si256(C, maskB);                         // Blue component
        // Extract the RGB components from the reference pixels
        __m256i rR = _mm256_srli_epi32(_mm256_and_si256(R, maskR), 16); // Red component
        __m256i rG = _mm256_srli_epi32(_mm256_and_si256(R, maskG), 8);  // Green component
        __m256i rB = _mm256_and_si256(R, maskB);                         // Blue component
        // Calculate the differences between the candidate and reference RGB components
        __m256 dR = _mm256_cvtepi32_ps(_mm256_sub_epi32(cR, rR)); // Difference in red component
        __m256 dG = _mm256_cvtepi32_ps(_mm256_sub_epi32(cG, rG)); // Difference in green component
        __m256 dB = _mm256_cvtepi32_ps(_mm256_sub_epi32(cB, rB)); // Difference in blue component
        // Calculate the sum of squared differences for the RGB components
        __m256 sum = _mm256_fmadd_ps(dR, dR,                             // Sum of squared differences for red
                           _mm256_fmadd_ps(dG, dG,                      // Sum of squared differences for green
                                           _mm256_mul_ps(dB, dB)));      // Sum of squared differences for blue

        // Convert the sum of squared differences to double precision and accumulate the results
        __m256d lo = _mm256_cvtps_pd(_mm256_castps256_ps128(sum));       // Lower half of the sum
        __m256d hi = _mm256_cvtps_pd(_mm256_extractf128_ps(sum, 1));    // Upper half of the sum
        accum = _mm256_add_pd(accum, _mm256_add_pd(lo, hi));            // Accumulate the results
    }
    // Process any remaining pixels that were not handled in the main loop
    double leftover = 0.0;
    for (; i < count_px; i++) {
        // Calculate the differences between the candidate and reference RGB components
        int dr = ((cand[i] >> 16) & 0xFF) - ((ref[i] >> 16) & 0xFF); // Difference in red component
        int dg = ((cand[i] >> 8) & 0xFF) - ((ref[i] >> 8) & 0xFF);   // Difference in green component
        int db = (cand[i] & 0xFF) - (ref[i] & 0xFF);                 // Difference in blue component
        // Accumulate the sum of squared differences for the remaining pixels
        leftover += (double)(dr*dr + dg*dg + db*db);
    }
    // Store the accumulated sum of squared differences into a temporary array
    double tmp[4];
    _mm256_storeu_pd(tmp, accum);
    // Calculate the total sum of squared differences
    double sum_avx = tmp[0] + tmp[1] + tmp[2] + tmp[3];
    // Return the mean squared error (MSE)
    return (sum_avx + leftover) / (double)count_px;
}
#endif /* __AVX2__ */

 
 /**
  * @brief Renders chromosome into scratch buffer, then computes MSE (RGB).
  *
  * This function renders the chromosome into a scratch buffer and then computes the Mean Squared Error (MSE)
  * between the rendered image and the reference image. It uses the fitness_scalar function for the MSE calculation
  * unless AVX2 is available, in which case it uses the fitness_avx2 function for accelerated computation.
  *
  * @param c Chromosome pointer.
  * @param user_data Pointer to GAFitnessParams.
  * @return MSE score or large penalty on error.
  */
 /**
 * @brief Renders chromosome into scratch buffer, then computes MSE (RGB).
 *
 * This function renders the chromosome into a scratch buffer and then computes the Mean Squared Error (MSE)
 * between the rendered image and the reference image. It uses the fitness_scalar function for the MSE calculation
 * unless AVX2 is available, in which case it uses the fitness_avx2 function for accelerated computation.
 *
 * @param c Chromosome pointer.
 * @param user_data Pointer to GAFitnessParams.
 * @return MSE score or large penalty on error.
 */
double ga_sdl_fitness_callback(const Chromosome *c, void *user_data)
{
    // Check if the chromosome or user data is null, return a large penalty if true
    if (!c || !user_data)
        return 1.0e30;

    // Cast the user data to GAFitnessParams pointer
    GAFitnessParams *p = (GAFitnessParams*)user_data;
    // Check if the reference pixels, scratch pixels, or pixel format is null, return a large penalty if true
    if (!p->ref_pixels || !p->scratch_pixels || !p->fmt)
        return 1.0e30;

    // Calculate the number of pixels per row
    int row_len = p->pitch / 4;
    // Validate the width and height of the image
    // Return a large penalty if the width is less than or equal to 0,
    // the height is less than or equal to 0, or the width is greater than the number of pixels per row
    if (p->width <= 0 || p->height <= 0 || p->width > row_len)
        return 1.0e30;

    // Calculate the total buffer size
    size_t buffer_size = (size_t)p->height * (size_t)p->pitch;
    // Validate the buffer size
    // Return a large penalty if the buffer size is not consistent with the height and pitch
    if ((buffer_size / (size_t)p->pitch) != (size_t)p->height)
        return 1.0e30;

    // Calculate the total number of pixels
    int count_px = p->width * p->height;
    // Validate the number of pixels
    // Return a large penalty if the number of pixels is less than or equal to 0
    if (count_px <= 0)
        return 1.0e30;

    // Render the chromosome into the scratch buffer
    render_chrom(c, p->scratch_pixels, p->pitch, p->fmt, p->width, p->height);
    // Compute the MSE using AVX2 if available, otherwise use the scalar implementation
    #ifdef __AVX2__
        // Use the AVX2-accelerated MSE calculation
        return fitness_avx2(p->scratch_pixels, p->ref_pixels, count_px);
    #else
        // Use the scalar MSE calculation
        return fitness_scalar(p->scratch_pixels, p->ref_pixels, count_px);
    #endif
}

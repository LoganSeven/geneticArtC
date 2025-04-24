/**
 * @file ga_renderer.c
 * @brief Implements SDL-based rendering and fitness calculation for shape-based chromosomes.
 *
 * This code is 100% optional from the GA's perspective. 
 * The GA calls `ga_sdl_fitness_callback` only if set via GAContext.fitness_func.
 * 
 * I keep the logic for:
 *   - alpha_blend (ARGB)
 *   - draw_circle
 *   - draw_triangle
 *   - render_chrom
 *   - fitness computation (scalar or AVX2)
 *
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
  * @brief Helper: clamps an integer v into [lo..hi].
  */
 static inline int clampi(int v, int lo, int hi)
 {
     return (v < lo) ? lo : (v > hi) ? hi : v;
 }
 
 /**
  * @brief Performs alpha blending of 'src' over 'dst', both ARGB. Returns new ARGB color.
  */
 static Uint32 alpha_blend(Uint32 dst, Uint32 src, const SDL_PixelFormat *fmt)
 {
     if (!fmt) {
         /* If somehow there's no format, we skip blending. Production check. */
         return src;
     }
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
  * @brief Draws a circle with center (cx,cy) and radius r 
  *        by alpha-blending color col into scratch buffer.
  */
 static void draw_circle(Uint32 *px, int pitch, const SDL_PixelFormat *fmt,
                         int cx, int cy, int r, Uint32 col,
                         int width, int height)
 {
     if (!px || !fmt) return;
     if (r <= 0) return;
 
     int r2 = r * r;
     int row_len = pitch / 4; /* # of Uint32 per row */
 
     for (int dy = -r; dy <= r; dy++) {
         int y = cy + dy;
         if (y < 0 || y >= height) {
             continue;
         }
         int dx_max = (int)sqrtf((float)(r2 - dy * dy));
         for (int dx = -dx_max; dx <= dx_max; dx++) {
             int x = cx + dx;
             if (x < 0 || x >= width) {
                 continue;
             }
             int idx = y * row_len + x;
             px[idx] = alpha_blend(px[idx], col, fmt);
         }
     }
 }
 
 /**
  * @brief Helper for triangle filling: 
  *        linear interpolation of X for a given Y along edge (xa, ya) -> (xb, yb).
  */
 static inline float edge(int y, int xa, int ya, int xb, int yb)
 {
     if (yb == ya) {
         return (float)xa;
     }
     return xa + (xb - xa) * ((float)(y - ya) / (float)(yb - ya));
 }
 
 /**
  * @brief Draws a filled triangle for (x1,y1), (x2,y2), (x3,y3) 
  *        by alpha-blending color col into scratch buffer.
  */
 static void draw_triangle(Uint32 *px, int pitch, const SDL_PixelFormat *fmt,
                           int x1, int y1, int x2, int y2, int x3, int y3,
                           Uint32 col, int width, int height)
 {
     if (!px || !fmt) return;
 
     if (y1 > y2) { int tx=x1; x1=x2; x2=tx; int ty=y1; y1=y2; y2=ty; }
     if (y1 > y3) { int tx=x1; x1=x3; x3=tx; int ty=y1; y1=y3; y3=ty; }
     if (y2 > y3) { int tx=x2; x2=x3; x3=tx; int ty=y2; y2=y3; y3=ty; }
 
     int row_len = pitch / 4;
     for (int y = y1; y <= y3; y++) {
         if (y < 0 || y >= height) continue;
         float xa, xb;
         if (y < y2) {
             xa = edge(y, x1, y1, x2, y2);
             xb = edge(y, x1, y1, x3, y3);
         } else {
             xa = edge(y, x2, y2, x3, y3);
             xb = edge(y, x1, y1, x3, y3);
         }
         if (xa > xb) {
             float t = xa; xa = xb; xb = t;
         }
         int ix_a = clampi((int)xa, 0, width - 1);
         int ix_b = clampi((int)xb, 0, width - 1);
         for (int x = ix_a; x <= ix_b; x++) {
             int idx = y * row_len + x;
             px[idx] = alpha_blend(px[idx], col, fmt);
         }
     }
 }
 
 /**
  * @brief Renders Chromosome c into the out buffer (ARGB), 
  *        clearing it to black, then drawing each shape with alpha-blending.
  */
 void render_chrom(const Chromosome *c, Uint32 *out, int pitch,
                          const SDL_PixelFormat *fmt, int width, int height)
 {
     if (!c || !out || !fmt) return;
     /* clear to black */
     memset(out, 0, (size_t)(height * pitch));
 
     int row_len = pitch / 4;
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
 
 /* -----------------------------------------------------------------------
    Fitness computations
    We do MSE (mean squared error) over R,G,B only, ignoring alpha.
    We have a scalar fallback and an AVX2-accelerated version if __AVX2__ is defined.
    -----------------------------------------------------------------------*/
 static inline double fitness_scalar(const Uint32 *cand,
                                     const Uint32 *ref,
                                     int count_px)
 {
     double err = 0.0;
     for (int i = 0; i < count_px; i++) {
         Uint32 c = cand[i];
         Uint32 r = ref[i];
         int dr = (int)((c >> 16) & 0xFF) - (int)((r >> 16) & 0xFF);
         int dg = (int)((c >>  8) & 0xFF) - (int)((r >>  8) & 0xFF);
         int db = (int)( c        & 0xFF) - (int)( r        & 0xFF);
         err += (double)(dr*dr + dg*dg + db*db);
     }
     return err / (double)count_px;
 }
 
 #ifdef __AVX2__
 static inline double fitness_avx2(const Uint32 *cand,
                                   const Uint32 *ref,
                                   int count_px)
 {
     __m256i maskR = _mm256_set1_epi32(0x00FF0000);
     __m256i maskG = _mm256_set1_epi32(0x0000FF00);
     __m256i maskB = _mm256_set1_epi32(0x000000FF);
 
     __m256d accum = _mm256_setzero_pd();
 
     int limit = (count_px / 8) * 8;
     int i = 0;
     for (; i < limit; i += 8) {
         __m256i C = _mm256_loadu_si256((const __m256i*)(cand + i));
         __m256i R = _mm256_loadu_si256((const __m256i*)(ref  + i));
 
         __m256i cR = _mm256_srli_epi32(_mm256_and_si256(C, maskR), 16);
         __m256i cG = _mm256_srli_epi32(_mm256_and_si256(C, maskG),  8);
         __m256i cB = _mm256_and_si256(C, maskB);
 
         __m256i rR = _mm256_srli_epi32(_mm256_and_si256(R, maskR), 16);
         __m256i rG = _mm256_srli_epi32(_mm256_and_si256(R, maskG),  8);
         __m256i rB = _mm256_and_si256(R, maskB);
 
         __m256 dR = _mm256_cvtepi32_ps(_mm256_sub_epi32(cR, rR));
         __m256 dG = _mm256_cvtepi32_ps(_mm256_sub_epi32(cG, rG));
         __m256 dB = _mm256_cvtepi32_ps(_mm256_sub_epi32(cB, rB));
 
         __m256 sum = _mm256_fmadd_ps(dR, dR,
                            _mm256_fmadd_ps(dG, dG, _mm256_mul_ps(dB, dB)));
 
         __m256d lo = _mm256_cvtps_pd(_mm256_castps256_ps128(sum));
         __m256d hi = _mm256_cvtps_pd(_mm256_extractf128_ps(sum, 1));
         accum = _mm256_add_pd(accum, _mm256_add_pd(lo, hi));
     }
 
     double leftover = 0.0;
     for (; i < count_px; i++) {
         Uint32 c = cand[i];
         Uint32 r = ref[i];
         int dr = (int)((c >> 16) & 0xFF) - (int)((r >> 16) & 0xFF);
         int dg = (int)((c >>  8) & 0xFF) - (int)((r >>  8) & 0xFF);
         int db = (int)( c        & 0xFF) - (int)( r        & 0xFF);
         leftover += (double)(dr*dr + dg*dg + db*db);
     }
 
     double tmp[4];
     _mm256_storeu_pd(tmp, accum);
     double sum_avx = tmp[0] + tmp[1] + tmp[2] + tmp[3];
     return (sum_avx + leftover) / (double)count_px;
 }
 #endif /* __AVX2__ */
 
 
 /**
  * @brief Renders Chromosome c to scratch, then computes MSE vs ref. 
  *        This is suitable as a GAFitnessFunc callback.
  */
 double ga_sdl_fitness_callback(const Chromosome *c, void *user_data)
 {
     if (!c || !user_data) {
         /* fallback large fitness if something is invalid. */
         return 1.0e30;
     }
     GAFitnessParams *p = (GAFitnessParams*)user_data;
     if (!p->ref_pixels || !p->scratch_pixels || !p->fmt) {
         return 1.0e30; /* invalid config => huge fitness */
     }
 
     /* 1) Render Chromosome into scratch_pixels */
     render_chrom(c, p->scratch_pixels, p->pitch,
                  p->fmt, p->width, p->height);
 
     /* 2) Compute MSE comparing scratch_pixels with p->ref_pixels */
     int count_px = p->width * p->height;
 
 #ifdef __AVX2__
     return fitness_avx2(p->scratch_pixels, p->ref_pixels, count_px);
 #else
     return fitness_scalar(p->scratch_pixels, p->ref_pixels, count_px);
 #endif
 }
 
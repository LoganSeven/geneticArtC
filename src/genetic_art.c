/***************************************************************
 *  genetic_art.c
 *  GA core with a more optimized fitness function (AVX2 optional).
 *
 *  This file is part of the open-source "Genetic Art C11 + SDL2"
 *  demonstrator. Released under the MIT License.
 **************************************************************/

 #include "../includes/genetic_art.h"   // Must come first so GAParams, Chromosome, etc. are known
 #include <math.h>          // For sqrtf(), etc.
 #include <stdlib.h>        // For rand(), malloc(), free(), etc.
 #include <string.h>        // For memset(), memcpy(), etc.
 #include <stdio.h>         // For fprintf()
 #include <unistd.h>        // For sysconf() to get CPU count
 #include <time.h>          // For clock_gettime() to measure timing
 
 #ifdef __AVX2__            // Only if AVX2 compiler flag is set
 #include <immintrin.h>
 #endif
 
 /*************************** Local helpers *******************************/
 
 /**
  * @brief  Inline clamp function: ensures integer v is in [lo, hi].
  *
  * static inline to hint the compiler for possible inlining.
  *
  * @param v   The integer value to clamp.
  * @param lo  Lower bound.
  * @param hi  Upper bound.
  * @return    v clamped between lo and hi.
  */
 static inline int clampi(int v, int lo, int hi)  // function start
 {
     return (v < lo) ? lo : (v > hi) ? hi : v;    // return clamped value
 }                                               // function end
 
 /**
  * @brief  Puts one pixel (Uint32 color) at (x, y) in the output array.
  *
  *   px    = pointer to the start of the pixel buffer
  *   pitch = number of bytes per row
  *   x,y   = coordinates
  *   c     = pixel color (ARGB)
  *
  * Each pixel row is pitch bytes, but 4 bytes per Uint32 in ARGB8888.
  */
 static inline void put_px(Uint32 *px, int pitch, int x, int y, Uint32 c)  
 {
     px[y * (pitch / 4) + x] = c;  // store the color into the row-major buffer
 }                                                                      
 /**
  * @brief  Performs alpha blending of a source ARGB pixel onto a destination ARGB pixel.
  *
  * This blends 'src' over 'dst', returning the new blended color as a Uint32.
  *
  *   dst, src = ARGB colors
  *   fmt      = pointer to the SDL_PixelFormat (for extracting RGBA channels)
  */
 static Uint32 alpha_blend(Uint32 dst, Uint32 src, const SDL_PixelFormat *fmt) // function start
 {
     Uint8 sr, sg, sb, sa;                        // source channels
     SDL_GetRGBA(src, fmt, &sr, &sg, &sb, &sa);   // extract source RGBA
 
     Uint8 dr, dg, db, da;                        // destination channels
     SDL_GetRGBA(dst, fmt, &dr, &dg, &db, &da);   // extract destination RGBA
 
     const float a = sa / 255.0f;                 // alpha as [0..1]
     Uint8 rr = (Uint8)(sr * a + dr * (1.0f - a)); // blend R
     Uint8 rg = (Uint8)(sg * a + dg * (1.0f - a)); // blend G
     Uint8 rb = (Uint8)(sb * a + db * (1.0f - a)); // blend B
 
     // Return the new ARGB color with full alpha = 255
     return SDL_MapRGBA(fmt, rr, rg, rb, 255);
 }                                                                              // function end
 
 /**
  * @brief  Helper function used by draw_triangle() to interpolate an X coordinate
  *         given a Y coordinate along the edge from (xa, ya) to (xb, yb).
  *
  *   y       = the scanline Y
  *   xa, ya  = first point of the edge
  *   xb, yb  = second point of the edge
  * @return   the floating X position on that edge for the given Y.
  */
 static inline float edge(int y, int xa, int ya, int xb, int yb) // function start
 {
     if (yb == ya) {
         // If the edge is a horizontal line, just return xa
         return (float)xa;
     }
     // Else interpolate
     return xa + (xb - xa) * ((float)(y - ya) / (float)(yb - ya));
 } // function end
 
 /*======================================================================*/
 /*         Shape rasterizers (circle / filled triangle)                 */
 /*======================================================================*/
 
 /**
  * @brief  Draws a circle of color col at center (cx, cy) with radius r
  *         into the px buffer (pitch = bytes per row).
  *         Blends color via alpha_blend() for every pixel within the circle.
  *
  * @param px     Pointer to pixel buffer (ARGB8888).
  * @param pitch  Number of bytes per row in the px buffer.
  * @param fmt    SDL_PixelFormat for color extraction.
  * @param cx,cy  Center of the circle.
  * @param r      Radius of the circle.
  * @param col    The ARGB color to blend.
  *
  * NOTE: I rely on external definitions IMAGE_W / IMAGE_H or I can pass them in.
  *       For clarity, I keep them as in the original. Adjust as needed.
  */
 static void draw_circle(Uint32 *px, int pitch, const SDL_PixelFormat *fmt,
                         int cx, int cy, int r, Uint32 col) // function start
 {
     if (r <= 0) return;                // if radius <= 0, nothing to draw
 
     const int r2 = r * r;              // radius squared
     for (int dy = -r; dy <= r; ++dy) {
         const int y = cy + dy;         // current row
         if (y < 0 || y >= IMAGE_H) continue; // skip if out of bounds
 
         // For each scan line, figure out how far I can go horizontally
         const int dx_max = (int)sqrtf((float)(r2 - dy * dy)); // horizontal half-width
         for (int dx = -dx_max; dx <= dx_max; ++dx) {
             const int x = cx + dx;    // actual x coordinate
             if (x < 0 || x >= IMAGE_W) continue; // skip if out of bounds
 
             // Compute index into pixel buffer
             const int idx = y * (pitch / 4) + x; // row-major index
             // Blend the color
             px[idx] = alpha_blend(px[idx], col, fmt);
         }
     }
 } // function end
 
 /**
  * @brief  Draws a filled triangle with vertices (x1,y1), (x2,y2), (x3,y3)
  *         using color col in the px buffer.
  *         The triangle is drawn using a simple scan-line approach,
  *         alpha blending each covered pixel with alpha_blend().
  *
  * @param px     Pointer to pixel buffer (ARGB8888).
  * @param pitch  Number of bytes per row in the px buffer.
  * @param fmt    SDL_PixelFormat for color extraction.
  * @param x1,y1  First vertex.
  * @param x2,y2  Second vertex.
  * @param x3,y3  Third vertex.
  * @param col    The ARGB color to blend.
  *
  * NOTE: I rely on external definitions IMAGE_W / IMAGE_H or I can pass them in.
  */
 static void draw_triangle(Uint32 *px, int pitch, const SDL_PixelFormat *fmt,
                           int x1, int y1, int x2, int y2, int x3, int y3,
                           Uint32 col) // function start
 {
     // First, sort vertices by ascending y for simpler edge logic
     if (y1 > y2) { int tx = x1; x1 = x2; x2 = tx; int ty = y1; y1 = y2; y2 = ty; }
     if (y1 > y3) { int tx = x1; x1 = x3; x3 = tx; int ty = y1; y1 = y3; y3 = ty; }
     if (y2 > y3) { int tx = x2; x2 = x3; x3 = tx; int ty = y2; y2 = y3; y3 = ty; }
 
     // Loop from top vertex (y1) to bottom vertex (y3)
     for (int y = y1; y <= y3; ++y) {
         if (y < 0 || y >= IMAGE_H) continue;  // skip out-of-bounds rows
 
         float xa, xb;  // intersection points on left and right edges
 
         if (y < y2) {
             // If I are above the middle vertex, use edges (x1,y1)-(x2,y2)
             // and (x1,y1)-(x3,y3)
             xa = edge(y, x1, y1, x2, y2);
             xb = edge(y, x1, y1, x3, y3);
         } else {
             // If I are between the middle vertex and bottom,
             // use edges (x2,y2)-(x3,y3) and (x1,y1)-(x3,y3)
             xa = edge(y, x2, y2, x3, y3);
             xb = edge(y, x1, y1, x3, y3);
         }
 
         // Ensure xa <= xb
         if (xa > xb) {
             float t = xa; xa = xb; xb = t;
         }
 
         // Convert to integer range and clamp in [0, IMAGE_W - 1]
         int ix_a = clampi((int)xa, 0, IMAGE_W - 1);
         int ix_b = clampi((int)xb, 0, IMAGE_W - 1);
 
         // Fill the horizontal span from ix_a to ix_b
         for (int x = ix_a; x <= ix_b; ++x) {
             const int idx = y * (pitch / 4) + x; // row-major index
             px[idx] = alpha_blend(px[idx], col, fmt); // alpha blend
         }
     }
 } // function end
 
 /*======================================================================*/
 /*         Chromosome helpers                                           */
 /*======================================================================*/
 
 /**
  * @brief  Renders a single Chromosome c into the out buffer (pitch = row bytes).
  *         The buffer is cleared to black first, then each Gene is drawn in order.
  *
  * @param c     Pointer to the Chromosome to render.
  * @param out   Output pixel buffer.
  * @param pitch Number of bytes per row.
  * @param fmt   SDL_PixelFormat for color extraction.
  */
 static void render_chrom(const Chromosome *c, Uint32 *out, int pitch,
                          const SDL_PixelFormat *fmt) // function start
 {
     // Clear to opaque black
     memset(out, 0, IMAGE_H * pitch);
 
     // Loop over shapes in the chromosome
     for (size_t i = 0; i < c->n_shapes; ++i) {
         const Gene *g = &c->shapes[i];
         // Convert RGBA to a Uint32
         const Uint32 col = SDL_MapRGBA(fmt, g->r, g->g, g->b, g->a);
 
         // Choose the shape
         if (g->type == SHAPE_CIRCLE) {
             draw_circle(out, pitch, fmt,
                         g->geom.circle.cx,
                         g->geom.circle.cy,
                         g->geom.circle.radius,
                         col);
         } else {
             draw_triangle(out, pitch, fmt,
                           g->geom.triangle.x1, g->geom.triangle.y1,
                           g->geom.triangle.x2, g->geom.triangle.y2,
                           g->geom.triangle.x3, g->geom.triangle.y3,
                           col);
         }
     }
 } // function end
 
 /**
  * @brief  Creates and returns a random Gene, either a circle or a triangle,
  *         with random coordinates and random RGBA.
  *
  * 50% chance circle vs. triangle.
  */
 static Gene random_gene(void) // function start
 {
     Gene g;
     if (rand() & 1) {
         // 50% chance: circle
         g.type = SHAPE_CIRCLE;
         g.geom.circle.cx     = rand() % IMAGE_W;
         g.geom.circle.cy     = rand() % IMAGE_H;
         g.geom.circle.radius = (rand() % 50) + 1;
     } else {
         // shape is triangle
         g.type = SHAPE_TRIANGLE;
         g.geom.triangle.x1 = rand() % IMAGE_W;  g.geom.triangle.y1 = rand() % IMAGE_H;
         g.geom.triangle.x2 = rand() % IMAGE_W;  g.geom.triangle.y2 = rand() % IMAGE_H;
         g.geom.triangle.x3 = rand() % IMAGE_W;  g.geom.triangle.y3 = rand() % IMAGE_H;
     }
 
     // Random RGBA
     g.r = (unsigned char)(rand() % 256);
     g.g = (unsigned char)(rand() % 256);
     g.b = (unsigned char)(rand() % 256);
     g.a = (unsigned char)(rand() % 256);
 
     return g;
 } // function end
 
 /**
  * @brief  Mutates (in-place) a single Gene by randomly changing some aspect
  *         of its shape or color.
  *         The approach is intentionally naive: I pick one of several possible mutations.
  *
  * @param g  Pointer to the Gene to mutate.
  */
 static void mutate_gene(Gene *g) // function start
 {
     // I pick one of several possible mutations
     switch (rand() % 9) {
     case 0: // toggle type entirely (big change)
         *g = random_gene();
         break;
     case 1: // mutate circle.x or triangle.x1
         if (g->type == SHAPE_CIRCLE) {
             g->geom.circle.cx = rand() % IMAGE_W;
         } else {
             g->geom.triangle.x1 = rand() % IMAGE_W;
         }
         break;
     case 2: // mutate circle.y or triangle.y1
         if (g->type == SHAPE_CIRCLE) {
             g->geom.circle.cy = rand() % IMAGE_H;
         } else {
             g->geom.triangle.y1 = rand() % IMAGE_H;
         }
         break;
     case 3: // mutate circle radius or triangle.x2
         if (g->type == SHAPE_CIRCLE) {
             g->geom.circle.radius = (rand() % 50) + 1;
         } else {
             g->geom.triangle.x2 = rand() % IMAGE_W;
         }
         break;
     case 4: // triangle.y2
         if (g->type == SHAPE_TRIANGLE) {
             g->geom.triangle.y2 = rand() % IMAGE_H;
         }
         break;
     case 5: // triangle.x3
         if (g->type == SHAPE_TRIANGLE) {
             g->geom.triangle.x3 = rand() % IMAGE_W;
         }
         break;
     case 6: // triangle.y3
         if (g->type == SHAPE_TRIANGLE) {
             g->geom.triangle.y3 = rand() % IMAGE_H;
         }
         break;
     case 7: // mutate color (r,g,b)
         g->r = (unsigned char)(rand() % 256);
         g->g = (unsigned char)(rand() % 256);
         g->b = (unsigned char)(rand() % 256);
         break;
     case 8: // mutate alpha
         g->a = (unsigned char)(rand() % 256);
         break;
     }
 } // function end
 
 /**
  * @brief  Fills an existing Chromosome with random genes, and sets fitness=∞.
  *
  * @param c  Pointer to the Chromosome to initialize.
  */
 static void random_init_chrom(Chromosome *c) // function start
 {
     for (size_t i = 0; i < c->n_shapes; ++i) {
         c->shapes[i] = random_gene();
     }
     c->fitness = INFINITY;
 } // function end
 
 /**
  * @brief  Perform a single crossover (2-parent) for shapes. Half of the genes
  *         come from parent a, the other half from parent b.
  *
  * @param a  First parent chromosome.
  * @param b  Second parent chromosome.
  * @param o  Output child chromosome.
  *
  * I assume all three Chromosomes have the same n_shapes.
  */
 static void crossover(const Chromosome *a, const Chromosome *b, Chromosome *o) // function start
 {
     size_t cut = o->n_shapes / 2; // or a->n_shapes / 2, etc.
     memcpy(o->shapes,         a->shapes,         cut * sizeof(Gene));
     memcpy(o->shapes + cut, b->shapes + cut, (o->n_shapes - cut) * sizeof(Gene));
 } // function end
 
 /*======================================================================*/
 /*         Fitness (scalar fallback + AVX2 version)                     */
 /*======================================================================*/
 
 /**
  * @brief  Computes the sum of squared color differences (R,G,B) for all pixels,
  *         dividing by the total pixel count. This is a "mean squared error" measure.
  *
  * This is the portable scalar fallback version.
  *
  * @param cand      The candidate image buffer (ARGB8888).
  * @param ref       The reference (target) image buffer (ARGB8888).
  * @param count_px  Total number of pixels to compare.
  * @return          MSE over all pixels: sum of squared diffs / count_px.
  */
 static inline double fitness_scalar(const Uint32 *cand,
                                     const Uint32 *ref,
                                     int count_px) // function start
 {
     double err = 0.0;
     for (int i = 0; i < count_px; ++i) {
         Uint32 c = cand[i], r = ref[i];
         // Extract R,G,B from ARGB
         int dr = (int)((c >> 16) & 0xFF) - (int)((r >> 16) & 0xFF);
         int dg = (int)((c >>  8) & 0xFF) - (int)((r >>  8) & 0xFF);
         int db = (int)( c        & 0xFF) - (int)( r        & 0xFF);
         err += (double)(dr*dr + dg*dg + db*db);
     }
     return err / count_px;
 } // function end
 
 #ifdef __AVX2__
 /**
  * @brief  AVX2 + FMA version of fitness: processes 8 pixels per iteration.
  *         I sum the squares of (candidate - reference) for R,G,B channels,
  *         then divide by the total pixel count.
  *
  * This is faster on CPUs that support AVX2. I handle leftover pixels if
  * count_px is not a multiple of 8.
  *
  * @param cand      The candidate image buffer (ARGB8888).
  * @param ref       The reference (target) image buffer (ARGB8888).
  * @param count_px  Total number of pixels to compare.
  * @return          MSE over all pixels: sum of squared diffs / count_px.
  */
 static inline double fitness_avx2(const Uint32 *cand,
                                   const Uint32 *ref,
                                   int count_px) // function start
 {
     const __m256i mR = _mm256_set1_epi32(0x00FF0000); // mask for red channel
     const __m256i mG = _mm256_set1_epi32(0x0000FF00); // mask for green channel
     const __m256i mB = _mm256_set1_epi32(0x000000FF); // mask for blue channel
 
     __m256d acc = _mm256_setzero_pd(); // accumulates sum of squares
 
     int limit = (count_px / 8) * 8; // handle chunks of 8
     int i = 0;
     for (; i < limit; i += 8) {
         // Load 8 pixels from candidate and reference
         __m256i C = _mm256_loadu_si256((const __m256i *)(cand + i));
         __m256i R = _mm256_loadu_si256((const __m256i *)(ref  + i));
 
         // Extract channels: shift/mask for R, G, B
         __m256i cR = _mm256_srli_epi32(_mm256_and_si256(C, mR), 16);
         __m256i rR = _mm256_srli_epi32(_mm256_and_si256(R, mR), 16);
         __m256i cG = _mm256_srli_epi32(_mm256_and_si256(C, mG),  8);
         __m256i rG = _mm256_srli_epi32(_mm256_and_si256(R, mG),  8);
         __m256i cB = _mm256_and_si256(C, mB);
         __m256i rB = _mm256_and_si256(R, mB);
 
         // Convert to float and compute differences
         __m256 dR = _mm256_cvtepi32_ps(_mm256_sub_epi32(cR, rR));
         __m256 dG = _mm256_cvtepi32_ps(_mm256_sub_epi32(cG, rG));
         __m256 dB = _mm256_cvtepi32_ps(_mm256_sub_epi32(cB, rB));
 
         // sum = dR^2 + dG^2 + dB^2
         __m256 sum = _mm256_fmadd_ps(dR, dR,
                          _mm256_fmadd_ps(dG, dG, _mm256_mul_ps(dB, dB)));
 
         // Split the 256-bit float vector into two 128-bit lanes
         // and accumulate into the double accumulator 'acc'
         __m256d lo = _mm256_cvtps_pd(_mm256_castps256_ps128(sum));
         __m256d hi = _mm256_cvtps_pd(_mm256_extractf128_ps(sum, 1));
         acc = _mm256_add_pd(acc, _mm256_add_pd(lo, hi));
     }
 
     // Handle leftover pixels if count_px is not multiple of 8
     double leftover_sum = 0.0;
     for (; i < count_px; i++) {
         Uint32 c = cand[i], r = ref[i];
         int dr = (int)((c >> 16) & 0xFF) - (int)((r >> 16) & 0xFF);
         int dg = (int)((c >>  8) & 0xFF) - (int)((r >>  8) & 0xFF);
         int db = (int)( c        & 0xFF) - (int)( r        & 0xFF);
         leftover_sum += (double)(dr*dr + dg*dg + db*db);
     }
 
     // Extract accumulated sum from 'acc'
     double tmp[4];
     _mm256_storeu_pd(tmp, acc);
     double sum_avx = tmp[0] + tmp[1] + tmp[2] + tmp[3];
 
     return (sum_avx + leftover_sum) / count_px;
 } // function end
 #endif
 
 /**
  * @brief Thin wrapper that chooses the fastest path at compile-time.
  *
  * If __AVX2__ is defined, I use fitness_avx2(), otherwise fallback
  * to fitness_scalar().
  *
  * @param cand      The candidate image buffer (ARGB8888).
  * @param ref       The reference (target) image buffer (ARGB8888).
  * @param count_px  Total number of pixels.
  * @return          The mean squared error for R,G,B channels.
  */
 static inline double fitness_px(const Uint32 *cand,
                                 const Uint32 *ref,
                                 int count_px) // function start
 {
 #ifdef __AVX2__
     return fitness_avx2(cand, ref, count_px);
 #else
     return fitness_scalar(cand, ref, count_px);
 #endif
 } // function end
 
 /*======================================================================*/
 /*         Parallel fitness worker (thread-pool)                        */
 /*======================================================================*/
 
 /**
  * @brief Structure for parallel fitness tasks:
  *        A single worker processes indices [first, last) inside g_eval_pop.
  */
 typedef struct FitTask {
     int first, last;
     int island_id;               /* STEP 1: pass the island ID */
     const Uint32      *ref;
     SDL_PixelFormat   *fmt;
     int                pitch;
     pthread_barrier_t *bar;
     atomic_int        *running;
 } FitTask;
 
 /**
  * @brief Global pointer to the population currently under evaluation
  *        (updated every generation by the GA thread).
  *
  * Instead of an array of Chromosome, I now have an array of pointers
  * to Chromosome: g_eval_pop[i] is a Chromosome*.
  */
 static Chromosome *volatile *g_eval_pop = NULL;
 
 /**
  * @brief Worker thread function for fitness evaluation:
  *        - Wait for start barrier
  *        - If running is true, evaluate assigned slice of population
  *        - Signal completion barrier
  *        - Repeat until running is false
  *
  * @param arg  Pointer to FitTask with parameters for the worker.
  */
 static void *fit_worker(void *arg) // function start
 {
     FitTask *t = (FitTask *)arg;
     Uint32 *scratch = malloc(IMAGE_W * IMAGE_H * sizeof(Uint32)); // intermediate buffer
     if (!scratch) return NULL; // if out of memory, exit
 
     while (1)
     {
         // Wait for "GO" barrier from GA master
         pthread_barrier_wait(t->bar);
 
         // I must do the second barrier wait for "DONE" even if not running
         if (!atomic_load(t->running))
         {
             pthread_barrier_wait(t->bar);
             break;
         }
 
         // Evaluate assigned slice of population
         for (int i = t->first; i < t->last; ++i) {
             Chromosome *c = g_eval_pop[i];
             render_chrom(c, scratch, t->pitch, t->fmt);
             c->fitness = fitness_px(scratch, t->ref, IMAGE_W * IMAGE_H);
         }
 
         // Signal "DONE"
         pthread_barrier_wait(t->bar);
     }
 
     free(scratch);
     return NULL;
 } // function end
 
 /*======================================================================*/
 /*         Island Model Helpers (new)                                   */
 /*======================================================================*/
 
 /** 
  * @brief  Simple struct for an island's population slice.
  */
 typedef struct {
     int start, end;
 } IslandRange;
 
 /** 
  * @brief Find the best (lowest-fitness) chromosome in [start..end].
  */
 static Chromosome* find_best(Chromosome **pop, int start, int end)
 {
     Chromosome *best = pop[start];
     for (int i = start + 1; i <= end; i++) {
         if (pop[i]->fitness < best->fitness) {
             best = pop[i];
         }
     }
     return best;
 }
 
 /**
  * @brief Find the index of the worst (highest-fitness) chromosome in [start..end].
  */
 static int find_worst_index(Chromosome **pop, int start, int end)
 {
     int worst = start;
     for (int i = start + 1; i <= end; i++) {
         if (pop[i]->fitness > pop[worst]->fitness) {
             worst = i;
         }
     }
     return worst;
 }
 
 /**
  * @brief Tournament selection restricted to indices [a..b].
  *        For demonstration, I pick 2 random individuals in the slice
  *        and return the better (lowest fitness).
  */
 static inline Chromosome* tournament_in_range(Chromosome **arr, int a, int b)
 {
     // pick one random index
     int idx1 = a + rand() % (b - a + 1);
     // pick another
     int idx2 = a + rand() % (b - a + 1);
 
     Chromosome *c1 = arr[idx1];
     Chromosome *c2 = arr[idx2];
     // I want lower fitness => better
     return (c1->fitness <= c2->fitness) ? c1 : c2;
 }
 
 /**
  * @brief  Migrate MIGRANTS_PER_ISL best individuals in ring topology:
  *         Each island's worst is replaced by the best from the preceding island.
  *
  * @param isl  Array of IslandRange entries (ISLAND_COUNT in size).
  * @param pop  The array of Chromosome* (the current generation).
  */
 static void migrate(IslandRange isl[], Chromosome **pop)
 {
     Chromosome *migrants[ISLAND_COUNT];
     /* gather elites (best) from each island slice */
     for (int i = 0; i < ISLAND_COUNT; ++i) {
         migrants[i] = find_best(pop, isl[i].start, isl[i].end);
     }
     /* ring exchange: src -> dest = (i+1)%ISLAND_COUNT 
        so I do dest i, src = i-1 mod count  */
     for (int dest = 0; dest < ISLAND_COUNT; ++dest) {
         int src = (dest - 1 + ISLAND_COUNT) % ISLAND_COUNT;
         int widx = find_worst_index(pop, isl[dest].start, isl[dest].end);
         /* deep-copy genome from migrants[src] to pop[widx] */
         copy_chromosome(pop[widx], migrants[src]);
         pop[widx]->fitness = migrants[src]->fitness; /* optional direct copy of fitness */
     }
 }
 
 /*======================================================================*/
 /*         GA master thread                                             */
 /*======================================================================*/
 
 /**
  * @brief GA master thread function. Runs the entire GA:
  *        1) Builds a thread-pool for parallel fitness evaluation
  *        2) Initializes the population
  *        3) Iterates generation steps (reproduction per island, migration, etc.)
  *        4) Publishes improved best solutions
  *        5) Graceful shutdown
  *
  * Also prints the elapsed time in milliseconds for each 100-iteration block.
  *
  * @param arg  Pointer to a valid, fully-initialized GAContext.
  * @return     NULL when done (thread exit).
  */
 void *ga_thread_func(void *arg) // function start
 {
     GAContext *ctx = (GAContext *)arg;
     const GAParams *p = ctx->params;  // easy referencing
 
     // STEP 1: For simplicity, I fix #threads = #islands
     const int N = ISLAND_COUNT;
 
     pthread_barrier_t bar;
     pthread_barrier_init(&bar, NULL, N + 1);  // +1 for this GA thread
 
     FitTask tasks[N];
     pthread_t tids[N];
 
     // I'll store the population in arrays of Chromosome*:
     Chromosome **pop     = malloc(p->population_size * sizeof(Chromosome*));
     Chromosome **new_pop = malloc(p->population_size * sizeof(Chromosome*));
     if (!pop || !new_pop) {
         fprintf(stderr, "Out of memory for pop arrays\n");
         pthread_barrier_destroy(&bar);
         free(pop);
         free(new_pop);
         return NULL;
     }
 
     // Partition the population among islands
     const int ISL_SIZE = p->population_size / ISLAND_COUNT;
     IslandRange isl[ISLAND_COUNT];
     for (int i = 0; i < ISLAND_COUNT; ++i) {
         isl[i].start = i * ISL_SIZE;
         isl[i].end   = (i == ISLAND_COUNT - 1)
                         ? (p->population_size - 1)
                         : ((i + 1) * ISL_SIZE - 1);
     }
 
     // Build worker tasks & threads
     for (int k = 0; k < N; ++k) {
         tasks[k].island_id = k;
         tasks[k].first     = isl[k].start;
         tasks[k].last      = isl[k].end + 1;  // [start, end) => +1
         tasks[k].ref       = ctx->src_pixels;
         tasks[k].fmt       = ctx->fmt;
         tasks[k].pitch     = ctx->pitch;
         tasks[k].bar       = &bar;
         tasks[k].running   = ctx->running;
 
         pthread_create(&tids[k], NULL, fit_worker, &tasks[k]);
     }
 
     // Allocate scratch buffer for single-threaded usage
     Uint32 *scratch = malloc(IMAGE_W * IMAGE_H * sizeof(Uint32));
     if (!scratch) {
         fprintf(stderr, "Out of memory for scratch buffer\n");
         pthread_barrier_destroy(&bar);
         free(pop);
         free(new_pop);
         return NULL;
     }
 
     // -------------------- 2) Initial population -----------------------
     for (int i = 0; i < p->population_size; ++i) {
         // Create a new Chromosome with p->nb_shapes
         pop[i] = chromosome_create(p->nb_shapes);
         if (!pop[i]) {
             fprintf(stderr, "Out of memory creating chromosome\n");
             // Cleanup partial
             for (int j = 0; j < i; j++) { chromosome_destroy(pop[j]); }
             free(pop);
             free(new_pop);
             free(scratch);
             pthread_barrier_destroy(&bar);
             return NULL;
         }
         // Random initialization
         random_init_chrom(pop[i]);
 
         // Evaluate fitness once (sequentially, so I have something to start with)
         render_chrom(pop[i], scratch, ctx->pitch, ctx->fmt);
         pop[i]->fitness = fitness_px(scratch, ctx->src_pixels, IMAGE_W * IMAGE_H);
     }
 
    // Find the best individual in the initial population.
    // Originally: Chromosome *best = pop[0]; which is correct,
    // but GCC emits a false positive (-Wmaybe-uninitialized) despite prior allocation.
    // To work around this, I break the analysis chain:
     Chromosome *best = NULL; // initialized to NULL to silence static analysis
     // Tried to make GCC chill. Still nag, but I am doing everything right. Let's pargma 
     #pragma GCC diagnostic push
     #pragma GCC diagnostic ignored "-Wmaybe-uninitialized" 
     best = pop[0]; // assign explicitly after allocation.
     #pragma GCC diagnostic pop 
     for (int i = 1; i < p->population_size; ++i) {
         if (pop[i]->fitness < best->fitness) {
             best = pop[i];
         }
     }
 
     // Publish best to the shared buffer
     pthread_mutex_lock(ctx->best_mutex);
     render_chrom(best, ctx->best_pixels, ctx->pitch, ctx->fmt);
     pthread_mutex_unlock(ctx->best_mutex);
 
     // I'll track the elapsed time for every 100 iterations
     struct timespec start_ts;
     clock_gettime(CLOCK_MONOTONIC, &start_ts);
     long long prev_msec = (long long)start_ts.tv_sec * 1000
                         + (start_ts.tv_nsec / 1000000);
 
     // -------------------- 3) Main GA loop -----------------------------
     for (int iter = 1; atomic_load(ctx->running) && iter <= p->max_iterations; ++iter) {
 
         /*----------------------------------------------
          * MIGRATION step: after I have pop's fitness,
          * do ring exchange every MIGRATION_INTERVAL.
          * (I do it right before I produce new_pop.)
          *----------------------------------------------*/
         if ((iter % MIGRATION_INTERVAL == 0) && (iter > 0)) {
             migrate(isl, pop);
         }
 
         /*----------------------------------------------
          * REPRODUCTION per-island
          * I fill new_pop[...] from pop[...] island by island.
          *----------------------------------------------*/
         for (int isl_id = 0; isl_id < ISLAND_COUNT; ++isl_id) {
             // 1) copy 1 elite (best of that island) verbatim:
             Chromosome *best_isl = find_best(pop, isl[isl_id].start, isl[isl_id].end);
             new_pop[isl[isl_id].start] = best_isl; // pointer copy
 
             // 2) for the remaining slots in that island
             for (int i = isl[isl_id].start + 1; i <= isl[isl_id].end; i++) {
                 // pick two parents from the island range
                 Chromosome *pa = tournament_in_range(pop, isl[isl_id].start, isl[isl_id].end);
                 Chromosome *pb = tournament_in_range(pop, isl[isl_id].start, isl[isl_id].end);
                 // ensure pa is better
                 if (pb->fitness < pa->fitness) {
                     Chromosome *tmp = pa; pa = pb; pb = tmp;
                 }
 
                 // create new child
                 Chromosome *child = chromosome_create(p->nb_shapes);
                 if (!child) {
                     fprintf(stderr, "Out of memory creating child chromosome\n");
                     break;
                 }
 
                 // crossover or asexual
                 float r01 = (float)rand() / (float)RAND_MAX;
                 if (r01 < p->crossover_rate) {
                     crossover(pa, pb, child);
                 } else {
                     memcpy(child->shapes, pa->shapes, pa->n_shapes * sizeof(Gene));
                 }
 
                 // mutate
                 for (size_t g = 0; g < child->n_shapes; ++g) {
                     float mr = (float)rand() / (float)RAND_MAX;
                     if (mr < p->mutation_rate) {
                         mutate_gene(&child->shapes[g]);
                     }
                 }
                 new_pop[i] = child;
             }
         }
 
         // 3-B: Parallel fitness evaluation of new_pop
         g_eval_pop = new_pop;        // tell workers which population to evaluate
         pthread_barrier_wait(&bar);  // release workers ("GO")
         pthread_barrier_wait(&bar);  // wait until they complete ("DONE")
 
         // Now new_pop[] each has updated fitness
         // 3-C: Check if a new best was found
         for (int i = 0; i < p->population_size; ++i) {
             if (new_pop[i]->fitness < best->fitness) {
                 best = new_pop[i];
                 // Publish new best
                 pthread_mutex_lock(ctx->best_mutex);
                 render_chrom(best, ctx->best_pixels, ctx->pitch, ctx->fmt);
                 pthread_mutex_unlock(ctx->best_mutex);
             }
         }
 
         // Free old generation's chromosomes that were not re-used verbatim
         // (the ones that remain in pop[] but not in new_pop[]).
         // Here, each island's "elite" was pointer-copied. The rest are replaced:
         for (int isl_id = 0; isl_id < ISLAND_COUNT; ++isl_id) {
             Chromosome *kept = new_pop[isl[isl_id].start]; // the island's elite pointer
             for (int i = isl[isl_id].start; i <= isl[isl_id].end; ++i) {
                 if (pop[i] != kept) {
                     chromosome_destroy(pop[i]);
                 }
             }
         }
 
         // Now copy pointers: new_pop -> pop
         memcpy(pop, new_pop, p->population_size * sizeof(Chromosome*));
 
         // Every 100 iterations, measure time
         if (iter % 100 == 0) {
             struct timespec now_ts;
             clock_gettime(CLOCK_MONOTONIC, &now_ts);
             long long now_msec = (long long)now_ts.tv_sec * 1000
                                + (now_ts.tv_nsec / 1000000);
             long long elapsed_100 = now_msec - prev_msec;
             prev_msec = now_msec;
 
             fprintf(stdout,
                     "[%d] best MSE = %.2f, elapsed for last 100 iters: %lld ms\n",
                     iter, best->fitness, elapsed_100);
         }
     }
 
     // -------------------- 4) Graceful shutdown -------------------------
     atomic_store(ctx->running, 0);     // signal threads to exit
 
     // Do TWO barrier waits again (symmetric to the workers)
     pthread_barrier_wait(&bar);       // "GO" barrier
     pthread_barrier_wait(&bar);       // "DONE" barrier
 
     // Join all worker threads
     for (int k = 0; k < N; ++k) {
         pthread_join(tids[k], NULL);
     }
 
     // Clean up
     pthread_barrier_destroy(&bar);
     free(scratch);
 
     // Free the final population
     // (Because after last iteration, pop[] are the survivors)
     // I do not want to double-free the same pointer, so track if duplicates exist.
     // But with our approach, each Chromosome in pop[] is unique now.
     // Just free them all:
     for (int i = 0; i < p->population_size; ++i) {
         chromosome_destroy(pop[i]);
     }
 
     free(pop);
     free(new_pop);
 
     return NULL;
 } // function end

// =============================================================
// >>>>>>>>>>>>>>>>>>>>>>  genetic_art.c  <<<<<<<<<<<<<<<<<<<<<<
// =============================================================
/**
 * @file   genetic_art.c
 * @brief  GA core + simple software rasteriser for circles & triangles.
 *
 * (All original comments preserved below)
 */

#include "genetic_art.h"    //Include the header that declares our API and constants
#include <math.h>           // For sqrtf(), etc.
#include <stdlib.h>         // For rand(), malloc(), free(), etc.
#include <string.h>         // For memset(), memcpy(), etc.
#include <stdio.h>          // For fprintf()

/*************************** Local helpers *******************************/

// -----------------------------------------------------------------------
// Inline clamp function: ensures integer v is in [lo, hi]
// -----------------------------------------------------------------------
static inline int clampi(int v, int lo, int hi)  // static inline to hint the compiler for possible inlining
{                                               
    return (v < lo) ? lo : (v > hi) ? hi : v;    // return clamped value
}                                               //function end

// -----------------------------------------------------------------------
// Puts one pixel (Uint32 color) at (x, y) in the output array.
//   px    = pointer to the start of the pixel buffer
//   pitch = number of bytes per row
//   x,y   = coordinates
//   c     = pixel color (ARGB)
// -----------------------------------------------------------------------
static inline void put_px(Uint32 *px, int pitch, int x, int y, Uint32 c) 
{                                                                         
    px[y * (pitch / 4) + x] = c;  //each pixel row is pitch bytes, but 4 bytes per Uint32
}                                                                         //function end

// -----------------------------------------------------------------------
// Performs alpha blending of a source ARGB pixel onto a destination ARGB pixel.
// Returns the new blended color as a Uint32.
//
//   dst, src = ARGB colors
//   fmt      = pointer to the SDL_PixelFormat (for extracting RGBA channels)
// -----------------------------------------------------------------------
static Uint32 alpha_blend(Uint32 dst, Uint32 src, const SDL_PixelFormat *fmt)
{   
    
    Uint8 sr, sg, sb, sa;                    // source channels
    SDL_GetRGBA(src, fmt, &sr, &sg, &sb, &sa);  // extract source channels

    Uint8 dr, dg, db, da;                    // destination channels
    SDL_GetRGBA(dst, fmt, &dr, &dg, &db, &da);  // extract destination channels

    const float a = sa / 255.0f;             //alpha as [0..1]
    Uint8 rr = (Uint8)(sr * a + dr * (1.0f - a));  // blend R
    Uint8 rg = (Uint8)(sg * a + dg * (1.0f - a));  // blend G
    Uint8 rb = (Uint8)(sb * a + db * (1.0f - a));  // blend B

    // Return the new ARGB color with full alpha = 255
    return SDL_MapRGBA(fmt, rr, rg, rb, 255);
}   

// -----------------------------------------------------------------------
// Helper function used by draw_triangle() to interpolate an X coordinate
// given a Y coordinate along the edge from (xa, ya) to (xb, yb).
// 
// This was originally a nested function; we’ve moved it here to avoid 
// GNU nested-function extensions. 
// 
//   y       = the scanline Y
//   xa, ya  = first point of the edge
//   xb, yb  = second point of the edge
// Returns the floating X position on that edge for the given Y.
// -----------------------------------------------------------------------
static inline float edge(int y, int xa, int ya, int xb, int yb)
{   
    
    if (yb == ya) {           
        // If the edge is a horizontal line, just return xa
        return (float)xa;
    }
    // Else interpolate
    return xa + (xb - xa) * ((float)(y - ya) / (float)(yb - ya));
}  

/* ----------------------------- Shapes --------------------------------- */

// -----------------------------------------------------------------------
// Draws a circle of color col at center (cx, cy) with radius r
// into the px buffer (pitch = bytes per row).
// Blends color via alpha_blend() for every pixel within the circle.
// -----------------------------------------------------------------------
static void draw_circle(Uint32 *px, int pitch, const SDL_PixelFormat *fmt,
                        int cx, int cy, int r, Uint32 col)
{   //function start
    if (r <= 0) return;                // if radius <= 0, nothing to draw

    const int r2 = r * r;              // radius squared
    for (int dy = -r; dy <= r; ++dy) { // iterate over vertical range
        const int y = cy + dy;         // current row
        if (y < 0 || y >= IMAGE_H) continue; // skip if out of bounds

        // For each scan line, figure out how far we can go horizontally
        const int dx_max = (int)sqrtf((float)(r2 - dy * dy)); // horizontal half-width
        for (int dx = -dx_max; dx <= dx_max; ++dx) {           // loop horizontally
            const int x = cx + dx;    // actual x coordinate
            if (x < 0 || x >= IMAGE_W) continue; // skip if out of bounds

            // Compute index into pixel buffer
            const int idx = y * (pitch / 4) + x; // row-major index
            // Blend the color
            px[idx] = alpha_blend(px[idx], col, fmt);
        }
    }
}   

// -----------------------------------------------------------------------
// Draws a filled triangle with vertices (x1,y1), (x2,y2), (x3,y3)
// using color col in the px buffer. 
// The triangle is drawn using a simple scan-line approach,
// alpha blending each covered pixel with alpha_blend().
// -----------------------------------------------------------------------
static void draw_triangle(Uint32 *px, int pitch, const SDL_PixelFormat *fmt,
                          int x1, int y1, int x2, int y2, int x3, int y3,
                          Uint32 col)
{   
    // First, sort vertices by ascending y
    if (y1 > y2) { 
        int tx = x1, ty = y1; 
        x1 = x2; y1 = y2; x2 = tx; y2 = ty; 
    }
    if (y1 > y3) { 
        int tx = x1, ty = y1; 
        x1 = x3; y1 = y3; x3 = tx; y3 = ty; 
    }
    if (y2 > y3) { 
        int tx = x2, ty = y2; 
        x2 = x3; y2 = y3; x3 = tx; y3 = ty; 
    }

    // Loop from top vertex (y1) to bottom vertex (y3)
    for (int y = y1; y <= y3; ++y) { 
        if (y < 0 || y >= IMAGE_H) continue;  // skip out-of-bounds rows

        float xa, xb;  // intersection points on left and right edges

        // If we’re above the middle vertex, use the top edge (x1,y1)-(x2,y2)
        // and the top edge (x1,y1)-(x3,y3)
        if (y < y2) {
            xa = edge(y, x1, y1, x2, y2);
            xb = edge(y, x1, y1, x3, y3);
        } else {
            // If we’re between the middle vertex and bottom, 
            // use edges (x2,y2)-(x3,y3) and (x1,y1)-(x3,y3)
            xa = edge(y, x2, y2, x3, y3);
            xb = edge(y, x1, y1, x3, y3);
        }

        // Ensure xa <= xb
        if (xa > xb) {
            float t = xa; 
            xa = xb; 
            xb = t; 
        }

        // Convert to integer range and clamp in [0, IMAGE_W-1]
        int ix_a = clampi((int)xa, 0, IMAGE_W - 1);
        int ix_b = clampi((int)xb, 0, IMAGE_W - 1);

        // Fill the horizontal span from ix_a to ix_b
        for (int x = ix_a; x <= ix_b; ++x) {
            const int idx = y * (pitch / 4) + x; // row-major index
            px[idx] = alpha_blend(px[idx], col, fmt); // alpha blend
        }
    }
}   

/* ---------------------- Chromosome utilities -------------------------- */

// -----------------------------------------------------------------------
// Renders a single Chromosome c into the out buffer (pitch = row bytes).
// The buffer is cleared to black first, then each Gene is drawn in order.
// -----------------------------------------------------------------------
static void render_chrom(const Chromosome *c, Uint32 *out, int pitch,
                         const SDL_PixelFormat *fmt)
{   
    // Clear to opaque black
    memset(out, 0, IMAGE_H * pitch);            // each row is pitch bytes, total rows = IMAGE_H

    // Loop over shapes in the chromosome
    for (int i = 0; i < NB_SHAPES; ++i) {       
        const Gene *g = &c->shapes[i];          // pointer to current gene
        // Convert RGBA to a Uint32
        const Uint32 col = SDL_MapRGBA(fmt, g->r, g->g, g->b, g->a);

        // Choose the shape
        if (g->type == SHAPE_CIRCLE) {
            draw_circle(out, pitch, fmt, g->cx, g->cy, g->radius, col);
        } else {
            draw_triangle(out, pitch, fmt,
                          g->x1, g->y1, g->x2, g->y2, g->x3, g->y3, col);
        }
    }
}   

// -----------------------------------------------------------------------
// Computes fitness (mean squared error) of candidate cand vs. reference ref.
// Lower fitness => better match. We sum the squared differences per channel,
// then divide by the total number of pixels.
// -----------------------------------------------------------------------
static double fitness(const Uint32 *cand, const Uint32 *ref,
                      const SDL_PixelFormat *fmt)
{   
    const int n = IMAGE_W * IMAGE_H;  // total pixels
    double err = 0.0;                 // accumulates the sum of squared differences

    // Evaluate each pixel
    for (int i = 0; i < n; ++i) {
        Uint8 cr, cg, cb, ca; SDL_GetRGBA(cand[i], fmt, &cr, &cg, &cb, &ca); // candidate RGBA
        Uint8 rr, rg, rb, ra; SDL_GetRGBA(ref [i], fmt, &rr, &rg, &rb, &ra); // reference RGBA

        // Channel differences
        const double dr = (double)cr - rr;
        const double dg = (double)cg - rg;
        const double db = (double)cb - rb;

        err += dr*dr + dg*dg + db*db; // sum squares
    }

    // Return mean-squared error
    return err / n;
}   

// -----------------------------------------------------------------------
// Creates and returns a random Gene, either a circle or a triangle,
// with random coordinates and random RGBA.
// -----------------------------------------------------------------------
static Gene random_gene(void)
{   
    Gene g;                        // declare new Gene struct
    if (rand() & 1) {             // 50% chance
        g.type   = SHAPE_CIRCLE;  // shape is circle
        g.cx     = rand() % IMAGE_W;
        g.cy     = rand() % IMAGE_H;
        g.radius = (rand() % 50) + 1;
        // Triangle data fields are left unused
        g.x1 = g.y1 = g.x2 = g.y2 = g.x3 = g.y3 = 0;
    } else {
        g.type = SHAPE_TRIANGLE;  // shape is triangle
        g.x1 = rand() % IMAGE_W;  g.y1 = rand() % IMAGE_H;
        g.x2 = rand() % IMAGE_W;  g.y2 = rand() % IMAGE_H;
        g.x3 = rand() % IMAGE_W;  g.y3 = rand() % IMAGE_H;
        // Circle data fields are left unused
        g.cx = g.cy = g.radius = 0;
    }
    // Random RGBA
    g.r = rand() % 256; 
    g.g = rand() % 256; 
    g.b = rand() % 256; 
    g.a = rand() % 256;

    return g;
}   

// -----------------------------------------------------------------------
// Mutates (in-place) a single Gene by randomly changing some aspect
// of its shape or color. The "random" approach is intentionally naive.
// -----------------------------------------------------------------------
static void mutate_gene(Gene *g)
{   
    // We pick one of several possible mutations
    switch (rand() % 9) { 
    case 0: // toggle type entirely
        *g = random_gene();  
        break;
    case 1: // mutate circle.x or triangle.x1
        if (g->type == SHAPE_CIRCLE) g->cx = rand() % IMAGE_W; 
        else g->x1 = rand() % IMAGE_W; 
        break;
    case 2: // mutate circle.y or triangle.y1
        if (g->type == SHAPE_CIRCLE) g->cy = rand() % IMAGE_H; 
        else g->y1 = rand() % IMAGE_H; 
        break;
    case 3: // mutate circle radius or triangle.x2
        if (g->type == SHAPE_CIRCLE) g->radius = (rand() % 50) + 1; 
        else g->x2 = rand() % IMAGE_W; 
        break;
    case 4: // triangle.y2
        if (g->type == SHAPE_TRIANGLE) g->y2 = rand() % IMAGE_H; 
        break;
    case 5: // triangle.x3
        if (g->type == SHAPE_TRIANGLE) g->x3 = rand() % IMAGE_W; 
        break;
    case 6: // triangle.y3
        if (g->type == SHAPE_TRIANGLE) g->y3 = rand() % IMAGE_H; 
        break;
    case 7: // mutate color (r,g,b)
        g->r = rand() % 256; 
        g->g = rand() % 256; 
        g->b = rand() % 256; 
        break;
    case 8: // mutate alpha
        g->a = rand() % 256; 
        break;
    }
}   

// -----------------------------------------------------------------------
// Initialise a Chromosome with entirely random genes, and set fitness=∞.
// -----------------------------------------------------------------------
static void init_chrom(Chromosome *c)
{   
    for (int i = 0; i < NB_SHAPES; ++i) {
        c->shapes[i] = random_gene();   // fill each gene
    }
    c->fitness = INFINITY;             // sentinel initial fitness
}   

// -----------------------------------------------------------------------
// Perform a single crossover (2-parent) for shapes. Half genes from a, 
// half from b. 
// 
//  a, b = parent chromosomes
//  o    = output child
// -----------------------------------------------------------------------
static void crossover(const Chromosome *a, const Chromosome *b, Chromosome *o)
{   
    const int cut = NB_SHAPES / 2;                     // halfway
    memcpy(o->shapes, a->shapes, cut * sizeof(Gene));  // first half from a
    memcpy(o->shapes + cut, b->shapes + cut,
           (NB_SHAPES - cut) * sizeof(Gene));          // second half from b
}   

/************************ GA worker thread *******************************/
/**
 * @brief GA worker thread function. 
 * This runs the entire GA: population init, selection, mutation, etc.
 * It updates the shared best_pixels (under mutex) whenever a better fitness is found.
 *
 * @param arg pointer to a valid, fully-initialised @ref GAContext.
 */
void *ga_thread_func(void *arg)
{   

    // Cast the generic pointer to our GAContext
    GAContext *ctx = (GAContext *)arg;                     // store for easy reference

    // We'll use a scratch buffer for rendering candidate images
    Uint32 *scratch = malloc(IMAGE_W * IMAGE_H * sizeof(Uint32));  // intermediate
    if (!scratch) return NULL;                            // if out of memory, quit

    // -------------------- initial population ----------------------------
    Chromosome pop[POPULATION_SIZE];  // array of Chromosomes
    for (int i = 0; i < POPULATION_SIZE; ++i) {
        init_chrom(&pop[i]);                                    // random init
        render_chrom(&pop[i], scratch, ctx->pitch, ctx->fmt);   // render into scratch
        pop[i].fitness = fitness(scratch, ctx->src_pixels, ctx->fmt);  // compute MSE
    }

    // Find the best among the initial population
    Chromosome best = pop[0];  // start with 0-th as best
    for (int i = 1; i < POPULATION_SIZE; ++i) {
        if (pop[i].fitness < best.fitness) {
            best = pop[i];
        }
    }

    // Publish that best to the shared buffer
    pthread_mutex_lock(ctx->best_mutex);                                // lock before modifying shared data
    render_chrom(&best, ctx->best_pixels, ctx->pitch, ctx->fmt);        // re-render best into global best_pixels
    pthread_mutex_unlock(ctx->best_mutex);                              // unlock

    // This array will store the new generation
    Chromosome new_pop[POPULATION_SIZE];

    // Main GA loop
    for (int iter = 1; atomic_load(ctx->running) && iter <= MAX_ITERATIONS; ++iter) {
        // ---------------- produce next generation -----------------------
        for (int i = 0; i < POPULATION_SIZE; ++i) {
            // Pick two random parents
            const Chromosome *pa = &pop[rand() % POPULATION_SIZE];
            const Chromosome *pb = &pop[rand() % POPULATION_SIZE];

            // If pb is better than pa, swap them (ensures pa is better or same)
            if (pb->fitness < pa->fitness) {
                const Chromosome *tmp = pa; 
                pa = pb; 
                pb = tmp; 
            }

            Chromosome child;
            // Crossover or asexual copy
            if ((float)rand() / RAND_MAX < CROSSOVER_RATE) {
                crossover(pa, pb, &child);
            } else {
                child = *pa;  // asexual
            }

            // Mutate shapes
            for (int g = 0; g < NB_SHAPES; ++g) {
                if ((float)rand() / RAND_MAX < MUTATION_RATE) {
                    mutate_gene(&child.shapes[g]);
                }
            }

            new_pop[i] = child;  // place child in new population
        }

        // ---------------- evaluate new population -----------------------
        for (int i = 0; i < POPULATION_SIZE; ++i) {
            render_chrom(&new_pop[i], scratch, ctx->pitch, ctx->fmt);
            new_pop[i].fitness = fitness(scratch, ctx->src_pixels, ctx->fmt);
        }

        // Move new_pop -> pop
        memcpy(pop, new_pop, sizeof(pop));

        // Check if a new best was found
        for (int i = 0; i < POPULATION_SIZE; ++i) {
            if (pop[i].fitness < best.fitness) {
                best = pop[i];
                // Publish new best
                pthread_mutex_lock(ctx->best_mutex);
                render_chrom(&best, ctx->best_pixels, ctx->pitch, ctx->fmt);
                pthread_mutex_unlock(ctx->best_mutex);
            }
        }

        // Optional progress output
        if (iter % 100 == 0)
            fprintf(stdout, "Iter %6d  best MSE = %.2f\n", iter, best.fitness);
    }

    // Clean up and exit
    free(scratch);
    return NULL;
}   


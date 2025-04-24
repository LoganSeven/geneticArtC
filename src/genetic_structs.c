// =============================================================
// >>>>>>>>>>>>>>>>>>  genetic_structs.c  <<<<<<<<<<<<<<<<<<<<<<
// =============================================================
#include "../includes/genetic_algorythm/genetic_structs.h"
#include <stdlib.h>
#include <math.h>  // for INFINITY
#include <string.h> // for memcpy()

Chromosome *chromosome_create(size_t n_shapes)
{
    Chromosome *c = (Chromosome*)malloc(sizeof(Chromosome));
    if (!c) return NULL; /* out of memory */

    c->shapes = (Gene*)calloc(n_shapes, sizeof(Gene));
    if (!c->shapes) {
        free(c);
        return NULL;
    }
    c->n_shapes = n_shapes;
    c->fitness  = INFINITY; // from <math.h>
    return c;
}

void chromosome_destroy(Chromosome *c)
{
    if (!c) return;  // guard
    free(c->shapes);
    free(c);
}

/**
 * @brief  STEP 6: Deep‑copy chromosome genome (same n_shapes).
 *
 * Copies shape data from src to dst, assuming both have
 * the same n_shapes. Overwrites dst->shapes in place.
 */
void copy_chromosome(Chromosome *dst, const Chromosome *src)
{
    if (!dst || !src) return;
    if (dst->n_shapes != src->n_shapes) return; /* assume same length? */
    memcpy(dst->shapes, src->shapes, src->n_shapes * sizeof(Gene));
    // fitness not automatically copied here (the caller can do it if needed).
}

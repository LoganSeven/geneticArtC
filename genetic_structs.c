// genetic_structs.c
#include "genetic_structs.h"
#include <stdlib.h>
#include <math.h>  // for INFINITY

// A minimal implementation:
Chromosome *chromosome_create(size_t n_shapes)
{
    Chromosome *c = (Chromosome*)malloc(sizeof(Chromosome));
    if (!c) return NULL;

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

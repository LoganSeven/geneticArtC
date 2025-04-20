// =============================================================
// >>>>>>>>>>>>>>>>>>  genetic_structs.h  <<<<<<<<<<<<<<<<<<<<<<
// =============================================================
// Runtime‑configurable data structures shared by the GA core
// and any front‑end (SDL demo, headless bench, etc.).
// =============================================================
#ifndef GENETIC_STRUCTS_H
#define GENETIC_STRUCTS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

/* -------------------- GA tunable parameters -------------------- */
typedef struct {
    int   population_size; /* chromosomes per generation           */
    int   nb_shapes;       /* genes (shapes) per chromosome        */
    int   elite_count;     /* # individuals copied verbatim        */
    float mutation_rate;   /* probability a gene mutates [0‑1]     */
    float crossover_rate;  /* probability parents crossover        */
    int   max_iterations;  /* hard stop to avoid run‑aways         */
} GAParams;

/* -------------------------- Gene definitions -------------------------- */
typedef enum {
    SHAPE_CIRCLE = 0,
    SHAPE_TRIANGLE
} ShapeType;

/* Geometry is wrapped in a union so each Gene stores only what it needs. */
typedef struct {
    ShapeType type;
    union {
        struct { int cx, cy, radius; } circle;
        struct { int x1, y1, x2, y2, x3, y3; } triangle;
    } geom;
    /* RGBA colour */
    unsigned char r, g, b, a;
} Gene;

/* ---------------------------- Chromosome ------------------------------ */
/*
 * A Chromosome owns a dynamically‑sized array of Gene.
 * `n_shapes` MUST match the `nb_shapes` value stored in the GAParams used
 * to create it.
 */
typedef struct {
    Gene   *shapes;   /* heap array of length n_shapes          */
    size_t  n_shapes; /* cached count for tight inner loops     */
    double  fitness;  /* lower is better                        */
} Chromosome;

/* Convenience helpers (implemented in genetic_structs.c) -------------- */
Chromosome *chromosome_create(size_t n_shapes);
void        chromosome_destroy(Chromosome *c);

#ifdef __cplusplus
}
#endif

#endif /* GENETIC_STRUCTS_H */

#ifndef GENETIC_STRUCTS_H
#define GENETIC_STRUCTS_H

/**
 * @file genetic_structs.h
 * @brief Runtime-configurable data structures for the Genetic Algorithm core.
 * @details
 * This header defines the essential types and structures used by the Genetic Algorithm (GA) core system.
 * 
 * The module provides:
 * - Configuration parameters for GA execution (`GAParams`).
 * - Definition of genes as drawable primitive shapes (`Gene`).
 * - Representation of candidate solutions as chromosomes (`Chromosome`).
 * - Helper functions to manage chromosomes (allocation, destruction, copying).
 * 
 * The code is written in pure C for maximal portability and does not rely on C++ features.
 * 
 * @path includes/genetic_algorithm/genetic_structs.h
 */

#include <stddef.h> /**< Provides size_t type for portable size representation. */

/**
 * @brief Parameters controlling the behavior of the Genetic Algorithm.
 *
 * Encapsulates all user-tunable or runtime-configurable values affecting
 * population dynamics, mutation/crossover rates, and stopping conditions.
 */
typedef struct {
    int   population_size; /**< Number of chromosomes (candidate solutions) maintained in each generation. */
    int   nb_shapes;       /**< Number of genes (primitive shapes) composing each chromosome. */
    int   elite_count;     /**< Number of top-performing chromosomes directly carried to the next generation. */
    float mutation_rate;   /**< Probability [0, 1] of mutating a gene during evolution. */
    float crossover_rate;  /**< Probability [0, 1] that two parent chromosomes will crossover. */
    int   max_iterations;  /**< Maximum number of generations to run before termination. */
} GAParams;

/**
 * @brief Enumeration of supported geometric shape types for a gene.
 *
 * Specifies the type of drawable primitive associated with a gene.
 */
typedef enum {
    SHAPE_CIRCLE = 0, /**< Circular shape (center + radius). */
    SHAPE_TRIANGLE    /**< Triangular shape (three vertices). */
} ShapeType;

/**
 * @brief Representation of a single gene, corresponding to a colored geometric shape.
 *
 * A gene defines a drawable primitive (circle or triangle) with associated color information.
 * The shape-specific data is stored in a union to minimize memory usage.
 */
typedef struct {
    ShapeType type; /**< Type of geometric primitive (circle or triangle). */
    union {
        struct { int cx, cy, radius; } circle;          /**< Circle parameters: center coordinates and radius length. */
        struct { int x1, y1, x2, y2, x3, y3; } triangle; /**< Triangle parameters: coordinates of three vertices. */
    } geom; /**< Geometric data describing the shape depending on `type`. */
    unsigned char r; /**< Red color channel value (0–255). */
    unsigned char g; /**< Green color channel value (0–255). */
    unsigned char b; /**< Blue color channel value (0–255). */
    unsigned char a; /**< Alpha (opacity) channel value (0–255). */
} Gene;

/**
 * @brief Represents a candidate solution composed of multiple genes.
 *
 * A chromosome holds a dynamically allocated array of genes describing a candidate
 * graphical solution. Chromosomes are subject to evaluation, mutation, and crossover
 * operations during the Genetic Algorithm process.
 */
typedef struct {
    Gene   *shapes;   /**< Pointer to a contiguous array of genes composing the chromosome. */
    size_t  n_shapes; /**< Number of genes contained in the chromosome (matches GAParams.nb_shapes). */
    double  fitness;  /**< Fitness score indicating solution quality; lower values represent better fitness. */
} Chromosome;

/**
 * @brief Allocate and initialize a new Chromosome structure.
 *
 * Allocates memory for a chromosome with the specified number of genes.
 * The genes are zero-initialized; additional initialization (random generation, etc.)
 * must be performed by the caller after allocation.
 *
 * @param[in] n_shapes Number of genes (shapes) to allocate for the chromosome.
 *
 * @return Pointer to a newly allocated Chromosome structure on success,
 *         or NULL if memory allocation fails.
 *
 * @note The returned Chromosome must be freed using `chromosome_destroy()` to avoid memory leaks.
 */
Chromosome *chromosome_create(size_t n_shapes);

/**
 * @brief Free all memory associated with a Chromosome.
 *
 * Deallocates the dynamically allocated array of genes inside the chromosome.
 * It is safe to call this function with a NULL pointer.
 *
 * @param[in,out] c Pointer to the Chromosome to destroy.
 *
 * @details
 * After this function call, the Chromosome pointer itself remains valid,
 * but its internal data is released.
 */
void chromosome_destroy(Chromosome *c);

/**
 * @brief Copy the gene data from one Chromosome to another.
 *
 * Performs a deep copy of the gene array from `src` into `dst`.
 * Only the gene structures are copied; fitness values are not updated
 * and must be handled separately.
 *
 * @param[in,out] dst Pointer to the destination Chromosome (already allocated).
 * @param[in]     src Pointer to the source Chromosome to copy from.
 *
 * @note Both source and destination chromosomes must be pre-allocated
 *       and contain the same number of genes (`n_shapes` must match).
 */
void copy_chromosome(Chromosome *dst, const Chromosome *src);

#endif /* GENETIC_STRUCTS_H */

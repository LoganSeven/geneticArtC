/**
 * @file genetic_structs.c
 * @brief Implementation of genetic algorithm structures and functions.
 *
 * This file contains the implementation of functions for managing chromosomes,
 * which are fundamental structures in the genetic algorithm. It includes functions
 * for creating, destroying, and copying chromosomes.
 */

 #include "../includes/genetic_algorithm/genetic_structs.h"
 #include <stdlib.h>
 #include <math.h>    // for INFINITY
 #include <string.h>  // for memcpy()
 
 /**
  * @brief Allocate and initialize a new Chromosome structure.
  *
  * This function allocates memory for a Chromosome and its internal gene array.
  * It initializes the chromosome with a specified number of genes (shapes) and sets
  * the initial fitness to INFINITY, indicating an uncomputed state.
  *
  * @param n_shapes Number of genes (shapes) to allocate for the chromosome.
  * @return Pointer to the newly allocated Chromosome, or NULL if allocation fails.
  *
  * @note The caller is responsible for freeing the chromosome using `chromosome_destroy`.
  *
  * Example:
  * @code
  * Chromosome *c = chromosome_create(10);
  * if (c) {
  *     // Use the chromosome
  *     chromosome_destroy(c);
  * }
  * @endcode
  */
 Chromosome *chromosome_create(size_t n_shapes)
 {
     // Allocate memory for the Chromosome structure
     Chromosome *c = (Chromosome*)malloc(sizeof(Chromosome));
     if (!c) return NULL;  // Return NULL if memory allocation fails
     // Allocate memory for the gene array with zero-initialization
     c->shapes = (Gene*)calloc(n_shapes, sizeof(Gene));
     if (!c->shapes) {
         // If gene array allocation fails, free the chromosome structure and return NULL
         free(c);
         return NULL;
     }
 
     // Set the number of shapes (genes) in the chromosome
     c->n_shapes = n_shapes;
     // Initialize the fitness to INFINITY, indicating an uncomputed state
     c->fitness  = INFINITY;
     // Return the allocated and initialized chromosome
     return c;
 }
 
 /**
  * @brief Deallocate a Chromosome and its associated gene memory.
  *
  * This function frees the memory allocated for the chromosome structure and its gene array.
  * It is safe to call this function with a NULL pointer.
  *
  * @param c Pointer to the Chromosome to free.
  *
  * Example:
  * @code
  * chromosome_destroy(c);
  * @endcode
  */
 void chromosome_destroy(Chromosome *c)
 {
     if (!c) return;  // Do nothing if the chromosome pointer is NULL
 
     // Free the allocated memory for the gene array
     free(c->shapes);
 
     // Free the allocated memory for the chromosome structure
     free(c);
 }
 
 /**
  * @brief Copy the genome (genes) from one chromosome into another.
  *
  * This function performs a deep copy of the gene array (`shapes`) from the source
  * chromosome (`src`) into the destination chromosome (`dst`). It assumes both chromosomes
  * have an identical number of shapes (`n_shapes`). The fitness value is intentionally
  * not copied and must be managed separately by the caller.
  *
  * @param dst Pointer to the destination chromosome to receive the copied genes.
  * @param src Pointer to the source chromosome from which genes are copied.
  *
  * @note The caller must ensure both chromosomes are allocated and have matching `n_shapes`.
  * @note The fitness value is not copied by design and must be managed separately by the caller.
  *
  * Example:
  * @code
  * copy_chromosome(dst, src);
  * @endcode
  */
 void copy_chromosome(Chromosome *dst, const Chromosome *src)
 {
     if (!dst || !src) return;  // Validate input pointers
     // Ensure both chromosomes have the same number of shapes
     if (dst->n_shapes != src->n_shapes) return;
     // Perform a deep copy of the gene data from the source to the destination chromosome
     memcpy(dst->shapes, src->shapes, src->n_shapes * sizeof(Gene));
 }

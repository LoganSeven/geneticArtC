/**
 * @file genetic_art.c
 * @brief GA core with multi-threaded evaluation, island model, and shape-based
 *        chromosomes. All rendering/pixel code removed.
 *
 * The GA code is domain-aware in that it manipulates shape Genes (circles,
 * triangles) but it does NOT do pixel-based or SDL-based fitness.
 * Instead, it calls a user-supplied fitness_func callback.
 */

 #include "../includes/genetic_algorithm/genetic_art.h"
 #include <stdlib.h>
 #include <string.h>
 #include <stdio.h>
 #include <time.h>
 
 /**
  * @brief Logs a message at a specified log level using the context's log function.
  *
  * This function logs a message using the log function callback provided in the GAContext.
  * The log function is called with the specified log level and message string.
  *
  * @param ctx   Pointer to GAContext, which contains the log function callback.
  * @param level The logging level of the message.
  * @param msg   The message string to be logged.
  */
 static void ga_log(const GAContext *ctx, GALogLevel level, const char *msg)
 {
     if (ctx && ctx->log_func)
         ctx->log_func(level, msg, ctx->log_user_data);
 }
 
 /**
  * @brief Island Model parameters.
  *
  * The constants below define default values for the island-based GA.
  * Adjusting these (e.g., number of islands, migration interval, etc.)
  * can tailor the GA dynamics.
  */
 #define FIT_MAX_WORKERS    8
 #define ISLAND_COUNT       4    /**< Default number of islands (threads). */
 #define MIGRATION_INTERVAL 5    /**< Generations between migrations. */
 #define MIGRANTS_PER_ISL   1    /**< Number of elite copies exchanged. */
 
 /**
  * @brief Forward declarations for local helper functions performing standard GA operations.
  *
  * These operations (e.g., crossover, random init, mutation) do not rely on any SDL or pixel logic.
  */
 static void random_init_chrom(Chromosome *c);
 static void mutate_gene(Gene *g);
 static void crossover(const Chromosome *a, const Chromosome *b, Chromosome *o);
 
 /**
  * @brief Data structure used for each thread's fitness evaluation task.
  *
  * Contains a slice of the population indices, a pointer to the GAContext,
  * and a barrier for synchronizing thread operations.
  */
 typedef struct FitTask {
     int first;             /**< First index (inclusive) of the population slice. */
     int last;              /**< Last index (exclusive) of the population slice. */
     struct GAContext *ctx; /**< Shared GAContext pointer, provides fitness func and data. */
     pthread_barrier_t *bar;/**< Barrier for thread synchronization. */
 } FitTask;
 
 /**
  * @brief Global pointer allowing worker threads to access the population being evaluated.
  *
  * This points to the Chromosome* array of the generation currently under evaluation.
  * It is used only between barrier waits for parallel fitness calculation.
  */
 static Chromosome *volatile *g_eval_pop = NULL;
 
 /**
  * @brief Worker thread function that updates the .fitness of each Chromosome in [first..last)
  *        by calling ctx->fitness_func.
  *
  * The thread runs in a loop:
  *   - Waits for the "start" barrier.
  *   - Checks if the GA is still running. If not, it exits.
  *   - If running, calculates fitness for the assigned slice of the population.
  *   - Waits for the "done" barrier.
  *   - Breaks out if the GA has stopped.
  *
  * @param arg Pointer to a FitTask struct that defines the slice of population and the context.
  * @return Always returns NULL.
  */
 static void *fit_worker(void *arg)
 {
     FitTask *t = (FitTask*)arg;          /* Local pointer to the thread's task context. */
     GAContext *ctx = t->ctx;            /* Reference to the shared GAContext. */
 
     while (1) {
         /* Wait for "start" barrier before computing fitness. */
         pthread_barrier_wait(t->bar);
 
         /* Check if GA has been signaled to stop or if no valid fitness function is present. */
         if (!ctx->running || !ctx->fitness_func) {
             pthread_barrier_wait(t->bar);
             break;
         }
         if (ctx->running && (0 == *ctx->running)) {
             pthread_barrier_wait(t->bar);
             break;
         }
 
         /* Evaluate fitness for the assigned slice of population. */
         for (int i = t->first; i < t->last; i++) {
             Chromosome *c = g_eval_pop[i]; /* Local pointer to the i-th chromosome. */
             if (!c) continue;              /* Safety guard if pointer is invalid. */
             double f = ctx->fitness_func(c, ctx->fitness_data);
             c->fitness = f;
         }
 
         /* Wait for "done" barrier (main thread collects after fitness calculations). */
         pthread_barrier_wait(t->bar);
     }
     return NULL;
 }
 
 /**
  * @brief Island range structure indicating the start and end indices for each island.
  *
  * Each island receives a contiguous slice [start..end] of the population array.
  */
 typedef struct {
     int start; /**< Start index of the slice (inclusive). */
     int end;   /**< End index of the slice (inclusive). */
 } IslandRange;
 
 /**
  * @brief Finds the Chromosome with the lowest fitness in the given index range.
  *
  * This function iterates over the specified range of the population array and finds the
  * chromosome with the lowest fitness value.
  *
  * @param pop   Array of Chromosome pointers.
  * @param start Starting index (inclusive).
  * @param end   Ending index (inclusive).
  * @return Pointer to the best Chromosome (lowest fitness) in the specified range.
  */
 static Chromosome* find_best(Chromosome **pop, int start, int end)
 {
     Chromosome *best = pop[start]; /* Tracks the best chromosome found so far. */
     for (int i = start + 1; i <= end; i++) {
         if (pop[i]->fitness < best->fitness) {
             best = pop[i];
         }
     }
     return best;
 }
 
 /**
  * @brief Finds the index of the Chromosome with the highest fitness in [start..end].
  *
  * This function iterates over the specified range of the population array and finds the
  * index of the chromosome with the highest fitness value.
  *
  * @param pop   Array of Chromosome pointers.
  * @param start Starting index (inclusive).
  * @param end   Ending index (inclusive).
  * @return Index of the worst Chromosome (highest fitness) in the specified range.
  */
 static int find_worst_index(Chromosome **pop, int start, int end)
 {
     int worst = start; /* Tracks the index of the worst chromosome so far. */
     for (int i = start + 1; i <= end; i++) {
         if (pop[i]->fitness > pop[worst]->fitness) {
             worst = i;
         }
     }
     return worst;
 }
 
 /**
  * @brief Performs a tournament selection within [a..b] by picking two random individuals
  *        and returning the one with lower fitness.
  *
  * This function selects two random chromosomes within the specified range and returns
  * the chromosome with the lower fitness value.
  *
  * @param arr Array of Chromosome pointers.
  * @param a   Starting index (inclusive).
  * @param b   Ending index (inclusive).
  * @return Pointer to the Chromosome that wins the tournament (lower fitness).
  */
 static inline Chromosome* tournament_in_range(Chromosome **arr, int a, int b)
 {
     int idx1 = a + rand() % (b - a + 1); /* First random index in range. */
     int idx2 = a + rand() % (b - a + 1); /* Second random index in range. */
 
     Chromosome *c1 = arr[idx1];         /* First randomly chosen Chromosome. */
     Chromosome *c2 = arr[idx2];         /* Second randomly chosen Chromosome. */
     return (c1->fitness <= c2->fitness) ? c1 : c2;
 }
 
 /**
  * @brief Migrates top-performing Chromosomes among islands in a ring topology.
  *
  * This function copies the best individual from each island into the worst slot of the next island.
  * The migration follows a ring topology, where the last island migrates to the first island.
  *
  * @param isl Array of IslandRange structs defining each island's slice in the population.
  * @param pop Array of Chromosome pointers (entire population).
  */
 static void migrate(IslandRange isl[], Chromosome **pop)
 {
     Chromosome *migrants[ISLAND_COUNT]; /**< Temporary array of best Chromosomes from each island. */
     for (int i = 0; i < ISLAND_COUNT; i++) {
         migrants[i] = find_best(pop, isl[i].start, isl[i].end);
     }
 
     /* Place them into the "next" island’s worst slot (ring). */
     for (int dest = 0; dest < ISLAND_COUNT; dest++) {
         int src = (dest - 1 + ISLAND_COUNT) % ISLAND_COUNT; /* Ring-based source index. */
         int widx = find_worst_index(pop, isl[dest].start, isl[dest].end);
         copy_chromosome(pop[widx], migrants[src]);
         pop[widx]->fitness = migrants[src]->fitness;
     }
 }
 
 /**
  * @brief Creates a random Gene (either a circle or a triangle) with random position and color.
  *
  * This function does not perform any pixel-based logic. It simply assigns random geometry
  * (circle or triangle) and random RGBA color values.
  *
  * @return A randomly initialized Gene.
  */
 static Gene random_gene(void)
 {
     Gene g; /* A new gene with random geometry and color. */
     if (rand() & 1) {
         g.type = SHAPE_CIRCLE;
         g.geom.circle.cx     = rand() % 640;
         g.geom.circle.cy     = rand() % 480;
         g.geom.circle.radius = (rand() % 50) + 1;
     } else {
         g.type = SHAPE_TRIANGLE;
         g.geom.triangle.x1 = rand() % 640;
         g.geom.triangle.y1 = rand() % 480;
         g.geom.triangle.x2 = rand() % 640;
         g.geom.triangle.y2 = rand() % 480;
         g.geom.triangle.x3 = rand() % 640;
         g.geom.triangle.y3 = rand() % 480;
     }
 
     g.r = (unsigned char)(rand() % 256);
     g.g = (unsigned char)(rand() % 256);
     g.b = (unsigned char)(rand() % 256);
     g.a = (unsigned char)(rand() % 256);
 
     return g;
 }
 
 /**
  * @brief Fills an existing Chromosome with random genes.
  *
  * This function initializes each gene in the chromosome with random values using the `random_gene` function.
  * It also sets the fitness to a very large number, indicating an uncomputed state.
  *
  * @param c Pointer to the Chromosome to be randomized.
  */
 static void random_init_chrom(Chromosome *c)
 {
     for (size_t i = 0; i < c->n_shapes; i++) {
         c->shapes[i] = random_gene();
     }
     c->fitness = 1.0e30; /* Initialize fitness to a very large number. */
 }
 
 /**
  * @brief Performs a random mutation on a single Gene.
  *
  * Depending on a random choice, it may replace the gene entirely
  * with a new random gene or mutate one of its parameters (geometry or color).
  *
  * @param g Pointer to the Gene being mutated.
  */
 static void mutate_gene(Gene *g)
 {
     switch (rand() % 9) {
     case 0:
         /* Replace entire gene with a newly generated random gene. */
         *g = random_gene();
         break;
     case 1:
         /* Mutate circle.x or triangle.x1. */
         if (g->type == SHAPE_CIRCLE) {
             g->geom.circle.cx = rand() % 640;
         } else {
             g->geom.triangle.x1 = rand() % 640;
         }
         break;
     case 2:
         /* Mutate circle.y or triangle.y1. */
         if (g->type == SHAPE_CIRCLE) {
             g->geom.circle.cy = rand() % 480;
         } else {
             g->geom.triangle.y1 = rand() % 480;
         }
         break;
     case 3:
         /* Mutate circle radius or triangle.x2. */
         if (g->type == SHAPE_CIRCLE) {
             g->geom.circle.radius = (rand() % 50) + 1;
         } else {
             g->geom.triangle.x2 = rand() % 640;
         }
         break;
     case 4:
         /* Mutate triangle.y2 if shape is triangle. */
         if (g->type == SHAPE_TRIANGLE) {
             g->geom.triangle.y2 = rand() % 480;
         }
         break;
     case 5:
         /* Mutate triangle.x3 if shape is triangle. */
         if (g->type == SHAPE_TRIANGLE) {
             g->geom.triangle.x3 = rand() % 640;
         }
         break;
     case 6:
         /* Mutate triangle.y3 if shape is triangle. */
         if (g->type == SHAPE_TRIANGLE) {
             g->geom.triangle.y3 = rand() % 480;
         }
         break;
     case 7:
         /* Mutate color (r,g,b). */
         g->r = (unsigned char)(rand() % 256);
         g->g = (unsigned char)(rand() % 256);
         g->b = (unsigned char)(rand() % 256);
         break;
     case 8:
         /* Mutate alpha channel. */
         g->a = (unsigned char)(rand() % 256);
         break;
     }
 }
 
 /**
  * @brief Performs shape-level crossover from two parent Chromosomes into one offspring.
  *
  * The function splits the gene array at halfway: the first part is copied from parent A,
  * and the remaining part is copied from parent B.
  *
  * @param a Pointer to the first parent Chromosome.
  * @param b Pointer to the second parent Chromosome.
  * @param o Pointer to the offspring Chromosome (destination).
  */
 static void crossover(const Chromosome *a, const Chromosome *b, Chromosome *o)
 {
     if (!a || !b || !o) return;
     if (o->n_shapes != a->n_shapes || a->n_shapes != b->n_shapes) return;
 
     size_t cut = o->n_shapes / 2; /* Index at which to switch from parent A to parent B. */
     memcpy(o->shapes, a->shapes, cut * sizeof(Gene));
     memcpy(o->shapes + cut, b->shapes + cut, (o->n_shapes - cut) * sizeof(Gene));
 }
 
 /**
  * @brief Main Genetic Algorithm thread function.
  *
  * This function sets up worker threads, initializes the population,
  * executes the standard GA loop (selection, crossover, mutation, migration),
  * updates the global best solution, and terminates cleanly.
  *
  * Steps:
  *   1. Spawn thread workers and create a barrier.
  *   2. Initialize the population (random Chromosomes).
  *   3. Evaluate the population fitness in parallel.
  *   4. Main GA loop with selection, crossover, mutation, and ring-migration.
  *   5. Track the best solution and update ctx->best_snapshot.
  *   6. Shut down gracefully and free resources.
  *
  * @param arg Pointer to a GAContext structure that holds all GA parameters and references.
  * @return Always returns NULL.
  */
 void *ga_thread_func(void *arg)
 {
     GAContext *ctx = (GAContext*)arg; /* Pointer to GAContext, containing parameters and references. */
     if (!ctx) {
         return NULL;
     }
     const GAParams *p = ctx->params; /* Local pointer to GA parameters. */
     if (!p) {
         return NULL;
     }
 
     /* Build a barrier that includes N worker threads + the GA master thread => total N+1. */
     pthread_barrier_t bar;
     int N = ISLAND_COUNT;
     pthread_barrier_init(&bar, NULL, N + 1);
 
     /* Prepare tasks + threads. */
     FitTask tasks[N];    /* Array of FitTask structs, one per thread. */
     pthread_t tids[N];   /* Array of thread IDs. */
 
     /**
      * represent the population as an array of Chromosome
      * to enable replacement or copy individual Chromosomes easily.
      */
     Chromosome **pop     = (Chromosome**)malloc(p->population_size * sizeof(Chromosome*));
     Chromosome **new_pop = (Chromosome**)malloc(p->population_size * sizeof(Chromosome*));
     if (!pop || !new_pop) {
         fprintf(stderr, "[GA] Out of memory for population arrays.\n");
         pthread_barrier_destroy(&bar);
         if (pop) free(pop);
         if (new_pop) free(new_pop);
         return NULL;
     }
 
     /* Divide population among islands. */
     int isl_size = p->population_size / ISLAND_COUNT; /* Size of each island's slice. */
     IslandRange isl[ISLAND_COUNT];                    /* Array of island ranges. */
     for (int i = 0; i < ISLAND_COUNT; i++) {
         isl[i].start = i * isl_size;
         if (i == ISLAND_COUNT - 1) {
             isl[i].end = p->population_size - 1;
         } else {
             isl[i].end = (i + 1) * isl_size - 1;
         }
     }
 
     /* Create worker threads. Each worker receives a FitTask. */
     for (int k = 0; k < N; k++) {
         tasks[k].first = isl[k].start;
         tasks[k].last  = isl[k].end + 1; /* 'end' is exclusive in the worker loop. */
         tasks[k].ctx   = ctx;
         tasks[k].bar   = &bar;
 
         int ret = pthread_create(&tids[k], NULL, fit_worker, &tasks[k]);
         if (ret != 0) {
             fprintf(stderr, "[GA] pthread_create failed for worker %d.\n", k);
         }
     }
 
     /* If best_snapshot was not allocated, do so now (stores best solution). */
     if (!ctx->best_snapshot) {
         ctx->best_snapshot = ctx->alloc_chromosome(p->nb_shapes);
         if (!ctx->best_snapshot) {
             fprintf(stderr, "[GA] Failed to allocate best_snapshot.\n");
         }
     }
 
     /* -------------------- 2) Initialize population -------------------- */
     for (int i = 0; i < p->population_size; i++) {
         Chromosome *chr = ctx->alloc_chromosome(p->nb_shapes);
         if (!chr) {
             fprintf(stderr, "[GA] Out of memory creating chromosome.\n");
             for (int j = 0; j < i; j++) {
                 ctx->free_chromosome(pop[j]);
             }
             free(pop);
             free(new_pop);
             pthread_barrier_destroy(&bar);
             return NULL;
         }
         random_init_chrom(chr);
         pop[i] = chr;
     }
 
     /* Evaluate fitness of the initial population in parallel. */
     g_eval_pop = pop;
     pthread_barrier_wait(&bar); /* start */
     pthread_barrier_wait(&bar); /* done */
 
     Chromosome *best = pop[0]; /* Pointer to the best Chromosome found so far. */
     for (int i = 1; i < p->population_size; i++) {
         if (pop[i]->fitness < best->fitness) {
             best = pop[i];
         }
     }
 
     /* Update global best_snapshot if available. */
     if (ctx->best_snapshot && ctx->best_mutex) {
         pthread_mutex_lock(ctx->best_mutex);
         copy_chromosome(ctx->best_snapshot, best);
         ctx->best_snapshot->fitness = best->fitness;
         pthread_mutex_unlock(ctx->best_mutex);
     }
 
     /* Measure time between iteration blocks. */
     struct timespec start_ts;
     clock_gettime(CLOCK_MONOTONIC, &start_ts);
     long long prev_msec = (long long)start_ts.tv_sec * 1000 + (start_ts.tv_nsec / 1000000LL);
 
     /* -------------------- 3) Main GA loop -------------------- */
     for (int iter = 1; (ctx->running && (*ctx->running != 0)) && (iter <= p->max_iterations); iter++) {
 
         /* Perform ring-migration every MIGRATION_INTERVAL generations. */
         if ((iter % MIGRATION_INTERVAL) == 0 && iter > 0) {
             migrate(isl, pop);
         }
 
         /* Reproduction per island. */
         for (int isl_id = 0; isl_id < ISLAND_COUNT; isl_id++) {
             Chromosome *best_isl = find_best(pop, isl[isl_id].start, isl[isl_id].end);
             new_pop[isl[isl_id].start] = best_isl; /* Keep the island's best (elite) in new_pop. */
 
             /* Fill the rest of the island's slice. */
             for (int i = isl[isl_id].start + 1; i <= isl[isl_id].end; i++) {
                 Chromosome *pa = tournament_in_range(pop, isl[isl_id].start, isl[isl_id].end);
                 Chromosome *pb = tournament_in_range(pop, isl[isl_id].start, isl[isl_id].end);
 
                 /* Ensure pa is not worse than pb for consistent crossover. */
                 if (pb->fitness < pa->fitness) {
                     Chromosome *tmp = pa;
                     pa = pb;
                     pb = tmp;
                 }
 
                 Chromosome *child = ctx->alloc_chromosome(p->nb_shapes);
                 if (!child) {
                     fprintf(stderr, "[GA] Out of memory creating child.\n");
                     break;
                 }
 
                 float r01 = (float)rand() / (float)RAND_MAX; /* Random [0..1] for crossover test. */
                 if (r01 < p->crossover_rate) {
                     crossover(pa, pb, child);
                 } else {
                     /* No crossover => copy parent pa. */
                     memcpy(child->shapes, pa->shapes, pa->n_shapes * sizeof(Gene));
                 }
 
                 /* Mutation step. */
                 for (size_t g = 0; g < child->n_shapes; g++) {
                     float mr = (float)rand() / (float)RAND_MAX; /* Random [0..1] for mutation test. */
                     if (mr < p->mutation_rate) {
                         mutate_gene(&child->shapes[g]);
                     }
                 }
                 new_pop[i] = child;
             }
         }
 
         /* Evaluate new_pop in parallel. */
         g_eval_pop = new_pop;
         pthread_barrier_wait(&bar); /* start */
         pthread_barrier_wait(&bar); /* done */
 
         /* Find the best in new_pop, update global best if improved. */
         for (int i = 0; i < p->population_size; i++) {
             if (new_pop[i]->fitness < best->fitness) {
                 best = new_pop[i];
                 /* Lock best_snapshot and copy new best if available. */
                 if (ctx->best_snapshot && ctx->best_mutex) {
                     pthread_mutex_lock(ctx->best_mutex);
                     copy_chromosome(ctx->best_snapshot, best);
                     ctx->best_snapshot->fitness = best->fitness;
                     pthread_mutex_unlock(ctx->best_mutex);
                 }
             }
         }
 
         /* Free old generation, except for the elites they are directly reused in new_pop. */
         for (int isl_id = 0; isl_id < ISLAND_COUNT; isl_id++) {
             Chromosome *kept = new_pop[isl[isl_id].start];
             for (int i = isl[isl_id].start; i <= isl[isl_id].end; i++) {
                 if (pop[i] != kept) {
                     ctx->free_chromosome(pop[i]);
                 }
             }
         }
 
         /* Move new_pop => pop. */
         memcpy(pop, new_pop, p->population_size * sizeof(Chromosome*));
 
         /* Optionally measure performance every 100 iterations. */
         if ((iter % 100) == 0) {
             struct timespec now_ts;
             clock_gettime(CLOCK_MONOTONIC, &now_ts);
             long long now_msec = (long long)now_ts.tv_sec * 1000 + (now_ts.tv_nsec / 1000000LL);
             long long elapsed_100 = now_msec - prev_msec;
             prev_msec = now_msec;
             fprintf(stdout, "[GA %d] best fitness = %.4f, last 100 iters: %lld ms\n",
                     iter, best->fitness, elapsed_100);
         }
     }
 
     /* -------------------- 4) Graceful shutdown -------------------- */
     if (ctx->running) {
         *ctx->running = 0;
     }
     pthread_barrier_wait(&bar); /* start */
     pthread_barrier_wait(&bar); /* done */
 
     /* Join worker threads. */
     for (int k = 0; k < N; k++) {
         pthread_join(tids[k], NULL);
     }
     pthread_barrier_destroy(&bar);
 
     /* Free population memory. */
     for (int i = 0; i < p->population_size; i++) {
         ctx->free_chromosome(pop[i]);
     }
     free(pop);
     free(new_pop);
 
     return NULL;
 }

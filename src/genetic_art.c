/**
 * @file genetic_art.c
 * @brief GA core with multi-threaded evaluation, island model, and shape-based 
 *        chromosomes. All rendering/pixel code removed.
 *
 * The GA code is domain-aware in that it manipulates shape Genes (circles, 
 * triangles) but it does NOT do pixel-based or SDL-based fitness.
 * Instead, it calls a user-supplied fitness_func callback.
 */

 #include "../includes/genetic_algorythm/genetic_art.h"
 #include <stdlib.h>
 #include <string.h>
 #include <stdio.h>
 #include <time.h>

 /* Internal logger call back */
static void ga_log(const GAContext *ctx, GALogLevel level, const char *msg)
{
    if (ctx && ctx->log_func)
        ctx->log_func(level, msg, ctx->log_user_data);
}
 
 /* 
  * We keep an Island Model approach. 
  * The user can tune these or move them to the GAParams if desired.
  */
 #define FIT_MAX_WORKERS    8
 #define ISLAND_COUNT       4    /* default # of islands (threads) */
 #define MIGRATION_INTERVAL 5    /* generations between migrations */
 #define MIGRANTS_PER_ISL   1    /* #elite copies exchanged        */
 
 /* 
  * Forward declarations for local helper functions that do standard GA operations 
  * (crossover, random init, mutation, etc.) but do NOT use any SDL or pixel logic.
  */
 static void random_init_chrom(Chromosome *c);
 static void mutate_gene(Gene *g);
 static void crossover(const Chromosome *a, const Chromosome *b, Chromosome *o);
 
 /* -------------------------------------------------------------------------
    Per-thread "FitTask" and a global pointer to the population under 
    evaluation. The user-data for fitness is stored in GAContext. 
    This approach is exactly like the older code but no pixel references.
    -------------------------------------------------------------------------*/
 typedef struct FitTask {
     int first, last;            /* slice of population to evaluate */
     struct GAContext *ctx;      /* pointer to the shared GAContext */
     pthread_barrier_t *bar;     /* barrier for synchronizing start/finish */
 } FitTask;
 
 /* global pointer that the worker threads can see: 
    points to the Chromosome* array for the generation being evaluated. 
    Used only between barrier waits. 
 */
 static Chromosome *volatile *g_eval_pop = NULL;
 
 /**
  * @brief Worker thread function that updates the .fitness of each Chromosome 
  *        in [first..last) by calling ctx->fitness_func().
  *
  * The thread repeatedly:
  *  - waits for barrier "start"
  *  - if GA is still running => compute fitness for the assigned slice
  *  - wait for barrier "done"
  *  - exit if the GA has stopped
  */
 static void *fit_worker(void *arg)
 {
     FitTask *t = (FitTask*)arg;
     GAContext *ctx = t->ctx;
 
     while (1) {
         /* Wait for "start" barrier */
         pthread_barrier_wait(t->bar);
 
         /* Check if GA has been signaled to stop. If so, finalize. */
         if (!ctx->running || !ctx->fitness_func) {
             /* safety check: if context was invalid, we abort */
             pthread_barrier_wait(t->bar);
             break;
         }
         if (ctx->running && (0 == *ctx->running)) {
             /* If the atomic flag is set to 0 => stop */
             pthread_barrier_wait(t->bar);
             break;
         }
 
         /* Evaluate fitness for the assigned slice */
         for (int i = t->first; i < t->last; i++) {
             Chromosome *c = g_eval_pop[i];
             if (!c) continue; /* safety guard */
             double f = ctx->fitness_func(c, ctx->fitness_data);
             c->fitness = f;
         }
 
         /* Wait for "done" barrier */
         pthread_barrier_wait(t->bar);
     }
     return NULL;
 }
 
 /* -------------------------------------------------------------------------
    Helper: Island ranges
    Each island gets a sub-slice [start..end] of the population array.
    This struct is used for clarity.
    -------------------------------------------------------------------------*/
 typedef struct {
     int start, end;
 } IslandRange;
 
 /* 
  * find_best: returns the pointer to the best (lowest fitness) chromosome 
  * in pop for indices [start..end].
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
 
 /* 
  * find_worst_index: returns the index of the worst (highest fitness) 
  * in pop for [start..end].
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
 
 /* 
  * tournament_in_range: picks two random individuals in [a..b], 
  * returns the better one (lowest fitness).
  */
 static inline Chromosome* tournament_in_range(Chromosome **arr, int a, int b)
 {
     int idx1 = a + rand() % (b - a + 1);
     int idx2 = a + rand() % (b - a + 1);
 
     Chromosome *c1 = arr[idx1];
     Chromosome *c2 = arr[idx2];
     return (c1->fitness <= c2->fitness) ? c1 : c2;
 }
 
 /* 
  * migrate: ring-topology migration of MIGRANTS_PER_ISL best individuals 
  * to replace the worst individuals on the next island.
  */
 static void migrate(IslandRange isl[], Chromosome **pop)
 {
     /* collect best from each island */
     Chromosome *migrants[ISLAND_COUNT];
     for (int i = 0; i < ISLAND_COUNT; i++) {
         migrants[i] = find_best(pop, isl[i].start, isl[i].end);
     }
 
     /* place them into the "next" island’s worst slot (ring) */
     for (int dest = 0; dest < ISLAND_COUNT; dest++) {
         int src = (dest - 1 + ISLAND_COUNT) % ISLAND_COUNT;
         int widx = find_worst_index(pop, isl[dest].start, isl[dest].end);
         copy_chromosome(pop[widx], migrants[src]);
         pop[widx]->fitness = migrants[src]->fitness;
     }
 }
 
 
 /* 
  * random_gene: sets up one gene with random geometry & RGBA. 
  * This does NOT do pixel-based rendering. 
  * For demonstration, we fix ranges: x in [0..640], etc.
  * but we do NOT reference SDL. 
  * Adjust or parametrize as you wish to remove these numeric limits.
  */
 static Gene random_gene(void)
 {
     Gene g;
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
 
 /* 
  * random_init_chrom: fill an existing Chromosome with random genes
  */
 static void random_init_chrom(Chromosome *c)
 {
     for (size_t i = 0; i < c->n_shapes; i++) {
         c->shapes[i] = random_gene();
     }
     c->fitness = 1.0e30; /* large number instead of Infinity to avoid float issues */
 }
 
 /* 
  * mutate_gene: randomly changes aspects of a gene. 
  * This is domain logic but not pixel-based.
  */
 static void mutate_gene(Gene *g)
 {
     switch (rand() % 9) {
     case 0:
         /* big toggle: replace with a brand-new random gene */
         *g = random_gene();
         break;
     case 1:
         /* mutate circle.x or triangle.x1 */
         if (g->type == SHAPE_CIRCLE) {
             g->geom.circle.cx = rand() % 640;
         } else {
             g->geom.triangle.x1 = rand() % 640;
         }
         break;
     case 2:
         /* mutate circle.y or triangle.y1 */
         if (g->type == SHAPE_CIRCLE) {
             g->geom.circle.cy = rand() % 480;
         } else {
             g->geom.triangle.y1 = rand() % 480;
         }
         break;
     case 3:
         /* mutate circle radius or triangle.x2 */
         if (g->type == SHAPE_CIRCLE) {
             g->geom.circle.radius = (rand() % 50) + 1;
         } else {
             g->geom.triangle.x2 = rand() % 640;
         }
         break;
     case 4:
         /* triangle.y2 if shape is triangle */
         if (g->type == SHAPE_TRIANGLE) {
             g->geom.triangle.y2 = rand() % 480;
         }
         break;
     case 5:
         /* triangle.x3 */
         if (g->type == SHAPE_TRIANGLE) {
             g->geom.triangle.x3 = rand() % 640;
         }
         break;
     case 6:
         /* triangle.y3 */
         if (g->type == SHAPE_TRIANGLE) {
             g->geom.triangle.y3 = rand() % 480;
         }
         break;
     case 7:
         /* mutate color (r,g,b) */
         g->r = (unsigned char)(rand() % 256);
         g->g = (unsigned char)(rand() % 256);
         g->b = (unsigned char)(rand() % 256);
         break;
     case 8:
         /* mutate alpha */
         g->a = (unsigned char)(rand() % 256);
         break;
     }
 }
 
 /* 
  * crossover: 2-parent shape-level crossover. 
  * We copy half genes from parent A, half from B. 
  * Caller must ensure all 3 have same n_shapes. 
  */
 static void crossover(const Chromosome *a, const Chromosome *b, Chromosome *o)
 {
     if (!a || !b || !o) return;
     if (o->n_shapes != a->n_shapes || a->n_shapes != b->n_shapes) return;
 
     size_t cut = o->n_shapes / 2;
     memcpy(o->shapes, a->shapes, cut * sizeof(Gene));
     memcpy(o->shapes + cut, b->shapes + cut, (o->n_shapes - cut) * sizeof(Gene));
 }
 
 /* -------------------------------------------------------------------------
    GA main thread function
    This is the standard multi-threaded GA:
    1) spawn thread workers
    2) init population
    3) main loop with selection, crossover, mutation, migration
    4) track best => update ctx->best_snapshot
    5) shutdown
    -------------------------------------------------------------------------*/
 void *ga_thread_func(void *arg)
 {
     GAContext *ctx = (GAContext*)arg;
     if (!ctx) {
         return NULL; /* invalid pointer => cannot proceed */
     }
     const GAParams *p = ctx->params;
     if (!p) {
         return NULL; /* invalid pointer => cannot proceed */
     }
 
     /* Build a barrier that includes N workers + the GA master thread => total N+1 */
     pthread_barrier_t bar;
     int N = ISLAND_COUNT; 
     pthread_barrier_init(&bar, NULL, N + 1);
 
     /* Prepare tasks + threads */
     FitTask tasks[N];
     pthread_t tids[N];
 
     /* 
      * We represent the population as an array of Chromosome* 
      * Because we want to do "copy_chromosome" or replacement easily.
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
 
     /* divide population among islands */
     int isl_size = p->population_size / ISLAND_COUNT;
     IslandRange isl[ISLAND_COUNT];
     for (int i = 0; i < ISLAND_COUNT; i++) {
         isl[i].start = i * isl_size;
         if (i == ISLAND_COUNT - 1) {
             isl[i].end = p->population_size - 1;
         } else {
             isl[i].end = (i + 1) * isl_size - 1;
         }
     }
 
     /* Create worker threads */
     for (int k = 0; k < N; k++) {
         tasks[k].first = isl[k].start;
         tasks[k].last  = isl[k].end + 1; /* end is exclusive in our loop */
         tasks[k].ctx   = ctx;
         tasks[k].bar   = &bar;
 
         int ret = pthread_create(&tids[k], NULL, fit_worker, &tasks[k]);
         if (ret != 0) {
             fprintf(stderr, "[GA] pthread_create failed for worker %d.\n", k);
             /* we do not immediately exit: best-effort approach */
         }
     }
 
     /* 
      * If best_snapshot is not yet allocated, do so 
      * to store the best solution found so far 
      */
     if (!ctx->best_snapshot) {
         ctx->best_snapshot = ctx->alloc_chromosome(p->nb_shapes);
         if (!ctx->best_snapshot) {
             fprintf(stderr, "[GA] Failed to allocate best_snapshot.\n");
         }
     }
 
     /* --------------- 2) Initialize population ---------------*/
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
 
     /* Evaluate fitness of the initial population */
     g_eval_pop = pop;
     pthread_barrier_wait(&bar); /* start */
     pthread_barrier_wait(&bar); /* done */
 
     Chromosome *best = pop[0];
     for (int i = 1; i < p->population_size; i++) {
         if (pop[i]->fitness < best->fitness) {
             best = pop[i];
         }
     }
 
     /* update global best_snapshot if we can lock it */
     if (ctx->best_snapshot && ctx->best_mutex) {
         pthread_mutex_lock(ctx->best_mutex);
         copy_chromosome(ctx->best_snapshot, best);
         ctx->best_snapshot->fitness = best->fitness;
         pthread_mutex_unlock(ctx->best_mutex);
     }
 
     /* measure time between iteration blocks */
     struct timespec start_ts;
     clock_gettime(CLOCK_MONOTONIC, &start_ts);
     long long prev_msec = (long long)start_ts.tv_sec * 1000 + (start_ts.tv_nsec / 1000000LL);
 
     /* --------------- 3) Main GA loop ---------------*/
     for (int iter = 1; (ctx->running && (*ctx->running != 0)) && (iter <= p->max_iterations); iter++) {
         /* Possibly do ring-migration every MIGRATION_INTERVAL generations */
         if ((iter % MIGRATION_INTERVAL) == 0 && iter > 0) {
             migrate(isl, pop);
         }
 
         /* Reproduction per island */
         for (int isl_id = 0; isl_id < ISLAND_COUNT; isl_id++) {
             /* keep the island's best as "elite" in new_pop */
             Chromosome *best_isl = find_best(pop, isl[isl_id].start, isl[isl_id].end);
             new_pop[isl[isl_id].start] = best_isl;
 
             /* fill the rest of that island's slice */
             for (int i = isl[isl_id].start + 1; i <= isl[isl_id].end; i++) {
                 Chromosome *pa = tournament_in_range(pop, isl[isl_id].start, isl[isl_id].end);
                 Chromosome *pb = tournament_in_range(pop, isl[isl_id].start, isl[isl_id].end);
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
 
                 float r01 = (float)rand() / (float)RAND_MAX;
                 if (r01 < p->crossover_rate) {
                     crossover(pa, pb, child);
                 } else {
                     /* no crossover => copy parent pa */
                     memcpy(child->shapes, pa->shapes, pa->n_shapes * sizeof(Gene));
                 }
 
                 /* mutate */
                 for (size_t g = 0; g < child->n_shapes; g++) {
                     float mr = (float)rand() / (float)RAND_MAX;
                     if (mr < p->mutation_rate) {
                         mutate_gene(&child->shapes[g]);
                     }
                 }
                 new_pop[i] = child;
             }
         }
 
         /* Evaluate new_pop in parallel */
         g_eval_pop = new_pop;
         pthread_barrier_wait(&bar); /* start */
         pthread_barrier_wait(&bar); /* done */
 
         /* find best in new_pop, update global best if improved */
         for (int i = 0; i < p->population_size; i++) {
             if (new_pop[i]->fitness < best->fitness) {
                 best = new_pop[i];
                 /* lock best_snapshot and copy */
                 if (ctx->best_snapshot && ctx->best_mutex) {
                     pthread_mutex_lock(ctx->best_mutex);
                     copy_chromosome(ctx->best_snapshot, best);
                     ctx->best_snapshot->fitness = best->fitness;
                     pthread_mutex_unlock(ctx->best_mutex);
                 }
             }
         }
 
         /* free old generation, except the "elites" we re-used in new_pop */
         for (int isl_id = 0; isl_id < ISLAND_COUNT; isl_id++) {
             Chromosome *kept = new_pop[isl[isl_id].start];
             for (int i = isl[isl_id].start; i <= isl[isl_id].end; i++) {
                 if (pop[i] != kept) {
                     ctx->free_chromosome(pop[i]);
                 }
             }
         }
 
         /* move new_pop => pop */
         memcpy(pop, new_pop, p->population_size * sizeof(Chromosome*));
 
         /* optional: measure performance every 100 iterations */
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
 
     /* 4) Graceful shutdown: tell workers to exit */
     if (ctx->running) {
         *ctx->running = 0;
     }
     pthread_barrier_wait(&bar); /* start */
     pthread_barrier_wait(&bar); /* done */
 
     /* join worker threads */
     for (int k = 0; k < N; k++) {
         pthread_join(tids[k], NULL);
     }
     pthread_barrier_destroy(&bar);
 
     /* free population memory */
     for (int i = 0; i < p->population_size; i++) {
         /* the final generation is still in pop */
         ctx->free_chromosome(pop[i]);
     }
     free(pop);
     free(new_pop);
 
     return NULL;
 }

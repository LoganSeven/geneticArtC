/*
 * Genetic Algorithm Art Demo (SDL2, POSIX threads)
 * -----------------------------------------------
 *
 *  file main.c, relative path: root/src/main.c
 *
 * This project demonstrates how a very small genetic algorithm
 * (GA) can gradually approximate a reference bitmap using only
 * a handful of filled circles and triangles. It is **pure C**
 * (C11‑compatible) and builds on every modern desktop Linux with
 *   gcc main.c genetic_art.c -o genetic_art -lSDL2 -lm -pthread
 * 
 * The code base is split into three units:
 *   • main.c          — program entry, SDL2 window / texture logic
 *   • genetic_art.h   — public interface shared by main & GA core
 *   • genetic_art.c   — GA engine, shapes rasterizer & worker thread
 *
 * The goal of this code is not to demonstrate rasterization tricks nor
 * to build a graphic library. SDL is well known, the footprint is
 * reasonable, and it's cross‑platform and C-friendly.
 *
 * I plan to use G.A. in much more useful ways (e.g. system prompt
 * optimization, graph node optimization, etc.)
 */

 #define NK_INCLUDE_STANDARD_IO
 #define NK_INCLUDE_STANDARD_VARARGS
 #define NK_INCLUDE_DEFAULT_ALLOCATOR
 #define NK_INCLUDE_FONT_BAKING
 #define NK_INCLUDE_DEFAULT_FONT
 #include "../includes/Nuklear/nuklear.h"
 
 #include <SDL2/SDL.h>
 #include <signal.h>
 #include <pthread.h>
 #include <stdio.h>
 #include <stdlib.h>
 #include <time.h>
 #include <stdatomic.h>
 #include <stdint.h>  
 #include <unistd.h> /* for sysconf() and access() */
 
 #if defined(__APPLE__)
  #include <OpenCL/opencl.h>
 #elif defined(HAVE_OPENCL)
  #include <CL/cl.h>
 #endif
 
 #include "../includes/genetic_art.h"   /* Also includes "genetic_structs.h" internally */
 #include "../includes/nuklear_sdl_renderer.h"
 
 /* Added for embedded font data */
 #include "../includes/embedded_font.h"
 
 /*************************  Constants & Macros  *************************/
 
 #ifndef WIDTH
 #define WIDTH     1280
 #endif
 #ifndef HEIGHT
 #define HEIGHT    960  /* CHANGED to 960, top half=480 for images, bottom=480 for GUI */
 #endif
 #ifndef IMAGE_W
 #define IMAGE_W   640
 #endif
 #ifndef IMAGE_H
 #define IMAGE_H   480
 #endif
 
 /* Arbitrary maximum lines for the text log */
 #define LOG_MAX_LINES  1024
 #define LOG_LINE_LEN   512
 
 /************************* Globals *******************************/
 static SDL_Window         *g_window     = NULL;
 static SDL_Renderer       *g_renderer   = NULL;
 static SDL_Texture        *g_ref_tex    = NULL; /* immutable */
 static SDL_Texture        *g_best_tex   = NULL; /* updated every frame */
 
 /* shared GA data ------------------------------------------------*/
 static Uint32             *g_ref_pixels  = NULL;
 static Uint32             *g_best_pixels = NULL;
 static SDL_PixelFormat    *g_fmt         = NULL;
 static int                 g_pitch       = 0;   /* bytes‑per‑row for best/ref */
 static pthread_mutex_t     g_best_mutex  = PTHREAD_MUTEX_INITIALIZER;
 static atomic_int          g_running     = 1;   /* 0 -> stop GA thread */
 
 /* Nuklear GUI context pointer */
 static struct nk_context  *g_nk         = NULL;
 
 /* Log buffer + mutex for thread‑safe appends */
 static pthread_mutex_t     g_log_mutex   = PTHREAD_MUTEX_INITIALIZER;
 static char                g_log_text[LOG_MAX_LINES][LOG_LINE_LEN];
 static struct nk_color     g_log_color[LOG_MAX_LINES];
 static int                 g_log_count   = 0;
 
 /* ---------------------------------------------------------------------
  * Thread-safe logging function for Nuklear text panel.
  * Appends msg + '\n' to the log buffer, up to LOG_MAX_LINES lines.
  * Called from main or GA thread (or anywhere).
  * --------------------------------------------------------------------*/
 void logStr(const char *msg, struct nk_color col)
 {
      pthread_mutex_lock(&g_log_mutex);
      if (g_log_count < LOG_MAX_LINES) {
          snprintf(g_log_text[g_log_count], LOG_LINE_LEN, "%s", msg);
          g_log_color[g_log_count] = col;
          g_log_count++;
          printf("[logStr called] msg='%s' (count=%d)\n", msg, g_log_count);
      }
      pthread_mutex_unlock(&g_log_mutex);
 }
 
 /************************* Forward decls *************************/
 static SDL_Surface *load_and_resize_bmp(const char *filename);
 static void do_startup_selftest(void);
 
 /* Handle Ctrl+C properly */
 static void handle_sigint(int sig)
 {
     (void)sig;
     atomic_store(&g_running, 0);
     fprintf(stderr, "\n[Ctrl+C] SIGINT received. Stopping GA and exiting...\n");
 }
 

 int main(int argc, char *argv[])
 {
     /* Intercept Ctrl+C so I can gracefully exit */
     signal(SIGINT, handle_sigint);
 
     /* Check arguments for reference image path */
     if (argc < 2) {
         fprintf(stderr, "Usage: %s <image.bmp>\n", argv[0]);
         return EXIT_FAILURE;
     }
 
     srand((unsigned)time(NULL));
 
     if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
         fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
         return EXIT_FAILURE;
     }
 
     /* 1. Window & renderer ------------------------------------------------*/
     g_window = SDL_CreateWindow("Genetic Art (GA demo + Nuklear)",
                                 SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                 WIDTH, HEIGHT, SDL_WINDOW_SHOWN);
     if (!g_window) {
         fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
         goto cleanup_sdl;
     }
 
     g_renderer = SDL_CreateRenderer(g_window, -1,
                                     SDL_RENDERER_ACCELERATED |
                                     SDL_RENDERER_PRESENTVSYNC);
     if (!g_renderer) {
         fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
         goto cleanup_window;
     }
 
     /* 2. Nuklear init (no OpenGL) -----------------------------------------*/
     g_nk = nk_sdl_init(g_window, g_renderer);
     if (!g_nk) {
         fprintf(stderr, "Failed to init Nuklear.\n");
         goto cleanup_renderer;
     }
 
    ///* ---------------------------------------------------------------------
    //   Embedded font loading block:
    //   I load the TTF from memory using the embedded array from embedded_font.h.
    //   If that fails, I fall back to the default font.
    //   ---------------------------------------------------------------------*/
     //DO NOT REMOVE THIS COMMENTED CODE AS I PLAN TO USE IT
     //IF YOU DO IT I WILL DESTROY THE WORLD
    {
        struct nk_font_atlas *atlas;
        nk_sdl_font_stash_begin(&atlas);
 
        struct nk_font *my_font = nk_font_atlas_add_from_memory(
            atlas,
            (void*)amiga4ever_ttf,
            (nk_size)amiga4ever_ttf_len,
            14.0f,
            NULL
        );
 
        if (my_font) {
            /* If loaded OK, let’s set it as default */
            fprintf(stderr, "[DEBUG] Embedded TTF loaded successfully.\n");
            atlas->default_font = my_font;
        } else {
            fprintf(stderr, "[DEBUG] Embedded TTF load failed.\n");
        }
 
        if (!my_font) {
            /* Try fallback if embed fails */
            my_font = nk_font_atlas_add_default(atlas, 13.0f, NULL);
            if (my_font) {
                fprintf(stderr, "[DEBUG] Nuklear default font assigned.\n");
                atlas->default_font = my_font;
            } else {
                fprintf(stderr, "[DEBUG] Even default font failed!\n");
            }
        }
 
        /* Finalize the font atlas */
        nk_sdl_font_stash_end();
 
        /* 
         * [FIX] We re-introduce nk_style_set_font(...) here, referencing the
         * handle inside the font atlas, so we have a valid width callback.
         * This ensures that ctx->style.font->width is non-null.
         */
        if (my_font) {
            /* NEW: force usage of the baked font to avoid assertion. */
            nk_style_set_font(g_nk, &my_font->handle);
        }
 
        fprintf(stdout, "[DEBUG] we are after nk_sdl_font_stash_end\n");
    }
     /* End of embedded font loading block */

    // {
    //    struct nk_font_atlas *atlas;
    //    nk_sdl_font_stash_begin(&atlas);
    //
    //    // Force la font par défaut (celle compilée avec Nuklear)
    //    struct nk_font *my_font = nk_font_atlas_add_default(atlas, 13.0f, NULL);
    //    atlas->default_font = my_font;
    //
    //    nk_sdl_font_stash_end();
    //    nk_style_set_font(g_nk, &my_font->handle);
    //    fprintf(stderr, "[FONT] width callback pointer = %p\n", (void*)g_nk->style.font->width);
    //    fprintf(stderr, "[DEBUG] Default Nuklear font assigned.\n");
    // }
    
 
     /* 3. Load & center the reference bitmap -------------------------------*/
     SDL_Surface *surf = load_and_resize_bmp(argv[1]);
     if (!surf) goto cleanup_nuklear;
     fprintf(stdout, "[DEBUG] surf is ok\n");
     g_fmt   = SDL_AllocFormat(surf->format->format);
     g_pitch = IMAGE_W * (int)sizeof(Uint32);
 
     g_ref_pixels  = malloc(IMAGE_W * IMAGE_H * sizeof(Uint32));
     g_best_pixels = calloc(IMAGE_W * IMAGE_H, sizeof(Uint32));
     if (!g_ref_pixels || !g_best_pixels || !g_fmt) {
         fprintf(stderr, "Memory allocation failure\n");
         goto cleanup_surface;
     }
 
     /* Copy pixels from the surface (row‑aligned) --------------------------*/
     SDL_LockSurface(surf);
     for (int y = 0; y < IMAGE_H; ++y) {
         const Uint32 *sp = (const Uint32 *)((const Uint8 *)surf->pixels + y * surf->pitch);
         Uint32       *dp = &g_ref_pixels[y * IMAGE_W];
         for (int x = 0; x < IMAGE_W; ++x) {
             dp[x] = sp[x];
         }
     }
     SDL_UnlockSurface(surf);
 
     g_ref_tex = SDL_CreateTextureFromSurface(g_renderer, surf);
     SDL_FreeSurface(surf);
     surf = NULL;
     if (!g_ref_tex) {
         fprintf(stderr, "SDL_CreateTextureFromSurface: %s\n", SDL_GetError());
         goto cleanup_surface;
     }
 
     /* 4. Streaming texture that mirrors g_best_pixels ---------------------*/
     g_best_tex = SDL_CreateTexture(g_renderer, g_fmt->format,
                                    SDL_TEXTUREACCESS_STREAMING,
                                    IMAGE_W, IMAGE_H);
     if (!g_best_tex) {
         fprintf(stderr, "SDL_CreateTexture (best): %s\n", SDL_GetError());
         goto cleanup_surface;
     }
 
     /* 5. Build GA parameters & GAContext */
     GAParams params = {
         .population_size = 500,     /* chromosomes per generation        */
         .nb_shapes       = 100,     /* genes (shapes) per chromosome     */
         .elite_count     = 2,       /* # individuals copied verbatim     */
         .mutation_rate   = 0.05f,   /* probability gene mutates          */
         .crossover_rate  = 0.70f,   /* probability I do crossover        */
         .max_iterations  = 1000000  /* hard stop to avoid runaways       */
     };
 
     GAContext ctx = {
         .params      = &params,
         .src_pixels  = g_ref_pixels,
         .fmt         = g_fmt,
         .best_pixels = g_best_pixels,
         .pitch       = g_pitch,
         .best_mutex  = &g_best_mutex,
         .running     = &g_running
     };
     fprintf(stdout, "[DEBUG] we are before do_startup_selftest\n");
     /* 6. Start-up selftest (CPU SIMD, threads, openCL) ------------------*/
     do_startup_selftest();
     logStr("Welcome to GA Art!", nk_rgb(255,255,0));
 
     /* 7. Launch the GA thread --------------------------------------------*/
     pthread_t ga_tid;
     if (pthread_create(&ga_tid, NULL, ga_thread_func, &ctx) != 0) {
         perror("pthread_create");
         goto cleanup_surface;
     }
 
     /* --------------------------------------------------------------------
        8. Main event loop: draws top half = reference & best candidate,
           plus bottom half = Nuklear GUI (log + future widgets area).
        --------------------------------------------------------------------*/
     int quit = 0;
     fprintf(stdout, "[DEBUG] entering main loop\n");
 
     while (!quit && atomic_load(&g_running)) {
         SDL_Event ev;
        // FIX: re-bind font every frame
        if (g_nk->style.font)
            nk_style_set_font(g_nk, g_nk->style.font);
         /* Input begin for Nuklear */
         nk_input_begin(g_nk);
         while (SDL_PollEvent(&ev)) {
             /* Forward events to Nuklear for UI handling */
             nk_sdl_handle_event(&ev);
 
             if (ev.type == SDL_QUIT) {
                 atomic_store(&g_running, 0);  // stop GA if window closed
                 quit = 1;
             }
         }
         nk_input_end(g_nk);
         /* Clear screen */
         SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 255);
         SDL_RenderClear(g_renderer);
 
         /* Draw reference (top-left) */
         SDL_Rect dst_ref = { 0, 0, IMAGE_W, IMAGE_H };
         SDL_RenderCopy(g_renderer, g_ref_tex, NULL, &dst_ref);
 
         /* Draw best candidate (top-right) */
         pthread_mutex_lock(&g_best_mutex);
         SDL_UpdateTexture(g_best_tex, NULL, g_best_pixels, g_pitch);
         pthread_mutex_unlock(&g_best_mutex);
 
         SDL_Rect dst_best = { IMAGE_W, 0, IMAGE_W, IMAGE_H };
         SDL_RenderCopy(g_renderer, g_best_tex, NULL, &dst_best);
 
        /* ----------------------------------------------------------------
           Nuklear UI (bottom half)
           ----------------------------------------------------------------*/

        /* Patch: Rebind font every frame to avoid width=NULL issue */
        if (g_nk->style.font) {
            nk_style_set_font(g_nk, g_nk->style.font);
            //fprintf(stderr, "[font] width callback ptr = %p\n", (void*)g_nk->style.font->width); the call back is ok no need to show it
        }

        /* bottom-left: scrollable text log */
        {
            struct nk_rect bounds = nk_rect(0, 480, 640, 480);
            if (nk_begin(g_nk, "Log", bounds,
                         NK_WINDOW_BORDER|NK_WINDOW_SCROLL_AUTO_HIDE|NK_WINDOW_TITLE))
            {
                nk_layout_row_dynamic(g_nk, 18, 1);
                pthread_mutex_lock(&g_log_mutex);
                for (int i = 0; i < g_log_count; i++) {
                    nk_text_colored(g_nk, g_log_text[i],
                                    (int)strlen(g_log_text[i]),
                                    NK_TEXT_LEFT, g_log_color[i]);
                }
                pthread_mutex_unlock(&g_log_mutex);
            }
            nk_end(g_nk);
        }

 
         /* bottom-right: free area for future widgets */
         {
             struct nk_rect bounds = nk_rect(640, 480, 640, 480);
             if (nk_begin(g_nk, "Future Widgets", bounds,
                          NK_WINDOW_BORDER|NK_WINDOW_TITLE))
             {
                 nk_layout_row_dynamic(g_nk, 30, 1);
                 nk_label(g_nk, "Put your GUI controls here.", NK_TEXT_LEFT);
             }
             nk_end(g_nk);
         }
 
         /* Render Nuklear with a single-argument call */
         nk_sdl_render(NK_ANTI_ALIASING_ON);
 
         SDL_RenderPresent(g_renderer);
         nk_clear(g_nk);
         /* Some modest pacing to avoid busy-loop (optional) */
         SDL_Delay(10);
     }
 
     /* Signal GA thread to stop & wait for it to finish */
     atomic_store(&g_running, 0);
     pthread_join(ga_tid, NULL);
 
     /************************* tidy up *************************************/
 cleanup_surface:
     free(g_best_pixels);
     free(g_ref_pixels);
 
     if (g_best_tex) SDL_DestroyTexture(g_best_tex);
     if (g_ref_tex)  SDL_DestroyTexture(g_ref_tex);
     if (g_fmt)      SDL_FreeFormat(g_fmt);
 
 cleanup_nuklear:
     if (g_nk) nk_sdl_shutdown();
 
 cleanup_renderer:
     if (g_renderer) SDL_DestroyRenderer(g_renderer);
 
 cleanup_window:
     if (g_window)   SDL_DestroyWindow(g_window);
 
 cleanup_sdl:
     SDL_Quit();
     return EXIT_SUCCESS;
 }
 
 /* ------------------------------------------------------------------------
  * Helper: load 24/32‑bit BMP, letter‑box into IMAGE_W × IMAGE_H keeping
  * aspect ratio. Always returns a 32‑bit ‘ARGB8888’ surface matching the
  * renderer’s pixel format, ready for @c SDL_CreateTextureFromSurface().
  * ----------------------------------------------------------------------*/
 static SDL_Surface *load_and_resize_bmp(const char *filename)
 {
     SDL_Surface *orig = SDL_LoadBMP(filename);
     if (!orig) {
         fprintf(stderr, "SDL_LoadBMP('%s'): %s\n", filename, SDL_GetError());
         return NULL;
     }
 
     /* If already at target size, just convert to 32‑bit */
     if (orig->w == IMAGE_W && orig->h == IMAGE_H) {
         SDL_Surface *conv = SDL_ConvertSurfaceFormat(orig, SDL_PIXELFORMAT_ARGB8888, 0);
         SDL_FreeSurface(orig);
         return conv;
     }
 
     /* letter‑box scaling ------------------------------------------------*/
     const float scale_w = (float)IMAGE_W / (float)orig->w;
     const float scale_h = (float)IMAGE_H / (float)orig->h;
     const float scale   = (scale_w < scale_h) ? scale_w : scale_h;
 
     const int new_w = (int)(orig->w * scale + 0.5f);
     const int new_h = (int)(orig->h * scale + 0.5f);
 
     SDL_Surface *tmp = SDL_CreateRGBSurfaceWithFormat(
                            0, new_w, new_h, 32, SDL_PIXELFORMAT_ARGB8888);
     SDL_BlitScaled(orig, NULL, tmp, NULL);
 
     SDL_Surface *final = SDL_CreateRGBSurfaceWithFormat(
                             0, IMAGE_W, IMAGE_H, 32, SDL_PIXELFORMAT_ARGB8888);
     SDL_FillRect(final, NULL, SDL_MapRGB(final->format, 0, 0, 0));
 
     SDL_Rect dst = {
         (IMAGE_W - new_w) / 2,
         (IMAGE_H - new_h) / 2,
         new_w,
         new_h
     };
     SDL_BlitSurface(tmp, NULL, final, &dst);
 
     SDL_FreeSurface(tmp);
     SDL_FreeSurface(orig);
     return final;
 }
 
 /* ------------------------------------------------------------------------
  * Start‑up self‑test: log CPU SIMD detection, number of threads, 
  * and check if there's a GPU with OpenCL.
  * ----------------------------------------------------------------------*/
 static void do_startup_selftest(void)
 {
 #if defined(__GNUC__) || defined(__clang__)
     if (__builtin_cpu_supports("sse"))  logStr("SSE : ok", nk_rgb(180,255,180));
     else                                logStr("SSE : na", nk_rgb(255,200,200));
 
     if (__builtin_cpu_supports("avx"))  logStr("AVX : ok", nk_rgb(180,255,180));
     else                                logStr("AVX : na", nk_rgb(255,200,200));
 
     if (__builtin_cpu_supports("avx2")) logStr("AVX2: ok", nk_rgb(180,255,180));
     else                                logStr("AVX2: na", nk_rgb(255,200,200));
 #else
     logStr("SSE : unknown", nk_rgb(255,255,0));
     logStr("AVX : unknown", nk_rgb(255,255,0));
     logStr("AVX2: unknown", nk_rgb(255,255,0));
 #endif
 
     long nprocs = sysconf(_SC_NPROCESSORS_ONLN);
     {
         char tmp[64];
         snprintf(tmp, sizeof(tmp), "Threads: %ld", nprocs > 0 ? nprocs : 1);
         logStr(tmp, nk_rgb(180,255,180));
     }
 
 #if (defined(__APPLE__) || defined(HAVE_OPENCL))
     {
         cl_uint plat_count = 0;
         cl_int err = clGetPlatformIDs(0, NULL, &plat_count);
         if (err != CL_SUCCESS || plat_count == 0) {
             logStr("OpenCL GPU check: no platform found", nk_rgb(255,200,200));
         } else {
             cl_platform_id *plats = (cl_platform_id*)malloc(sizeof(cl_platform_id)*plat_count);
             clGetPlatformIDs(plat_count, plats, NULL);
             int found_gpu = 0;
             for (cl_uint i = 0; i < plat_count; i++) {
                 cl_uint dev_count = 0;
                 clGetDeviceIDs(plats[i], CL_DEVICE_TYPE_GPU, 0, NULL, &dev_count);
                 if (dev_count > 0) { found_gpu = 1; break; }
             }
             free(plats);
             if (found_gpu) logStr("OpenCL GPU: ok", nk_rgb(180,255,180));
             else           logStr("OpenCL GPU: none found", nk_rgb(255,200,200));
         }
     }
 #else
     logStr("OpenCL GPU check: not compiled in", nk_rgb(255,200,200));
 #endif
 }

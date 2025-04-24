/*
 * Genetic Algorithm Art Demo (SDL2, POSIX threads)
 * -----------------------------------------------
 *
 *  file main.c, relative path: root/src/main.c
 *
 * Refactored to use decoupled GA core via fitness callback.
 * Uses SDL2 + Nuklear GUI. Font is loaded from embedded TTF memory.
 *
 * GA logic lives in genetic_art.c/.h — which is rendering agnostic.
 * The ga_renderer.c/.h implements the MSE fitness + rasterization.
 * This main file wires all parts and handles the GUI.
 */

 #define NK_INCLUDE_FIXED_TYPES
 #define NK_INCLUDE_STANDARD_IO
 #define NK_INCLUDE_STANDARD_VARARGS
 #define NK_INCLUDE_DEFAULT_ALLOCATOR
 #define NK_INCLUDE_FONT_BAKING
 #define NK_INCLUDE_DEFAULT_FONT
 #define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
 #define NK_INCLUDE_COMMAND_USERDATA
 #include "../includes/Nuklear/nuklear.h"
 
 #include <SDL2/SDL.h>
 #include <signal.h>
 #include <pthread.h>
 #include <stdio.h>
 #include <stdlib.h>
 #include <time.h>
 #include <stdatomic.h>
 #include <stdint.h>
 #include <unistd.h>
 
 #if defined(__APPLE__)
  #include <OpenCL/opencl.h>
 #elif defined(HAVE_OPENCL)
  #include <CL/cl.h>
 #endif
 
 #include "../includes/genetic_art.h"
 #include "../includes/genetic_structs.h"
 #include "../includes/ga_renderer.h"
 #include "../includes/nuklear_sdl_renderer.h"
 #include "../includes/embedded_font.h"
 
 #ifndef WIDTH
 #define WIDTH     1280
 #endif
 #ifndef HEIGHT
 #define HEIGHT    960
 #endif
 #ifndef IMAGE_W
 #define IMAGE_W   640
 #endif
 #ifndef IMAGE_H
 #define IMAGE_H   480
 #endif
 
 #define LOG_MAX_LINES  1024
 #define LOG_LINE_LEN   512
 
 static SDL_Window         *g_window     = NULL;
 static SDL_Renderer       *g_renderer   = NULL;
 static SDL_Texture        *g_ref_tex    = NULL;
 static SDL_Texture        *g_best_tex   = NULL;
 
 static Uint32             *g_ref_pixels  = NULL;
 static Uint32             *g_best_pixels = NULL;
 static SDL_PixelFormat    *g_fmt         = NULL;
 static int                 g_pitch       = 0;
 static pthread_mutex_t     g_best_mutex  = PTHREAD_MUTEX_INITIALIZER;
 static atomic_int          g_running     = 1;
 
 static struct nk_context  *g_nk         = NULL;
 static pthread_mutex_t     g_log_mutex   = PTHREAD_MUTEX_INITIALIZER;
 static char                g_log_text[LOG_MAX_LINES][LOG_LINE_LEN];
 static struct nk_color     g_log_color[LOG_MAX_LINES];
 static int                 g_log_count   = 0;
 
 void logStr(const char *msg, struct nk_color col)
 {
     pthread_mutex_lock(&g_log_mutex);
     if (g_log_count < LOG_MAX_LINES) {
         snprintf(g_log_text[g_log_count], LOG_LINE_LEN, "%s", msg);
         g_log_color[g_log_count] = col;
         g_log_count++;
         printf("[logStr] %s\n", msg);
     }
     pthread_mutex_unlock(&g_log_mutex);
 }
 
 static void handle_sigint(int sig)
 {
     (void)sig;
     atomic_store(&g_running, 0);
     fprintf(stderr, "\n[Ctrl+C] SIGINT received. Exiting...\n");
 }
 
 static SDL_Surface *load_and_resize_bmp(const char *filename);
 static void do_startup_selftest(void);
 
 int main(int argc, char *argv[])
 {
     signal(SIGINT, handle_sigint);
     if (argc < 2) {
         fprintf(stderr, "Usage: %s <image.bmp>\n", argv[0]);
         return EXIT_FAILURE;
     }
     srand((unsigned)time(NULL));
 
     if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
         fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
         return EXIT_FAILURE;
     }
 
     g_window = SDL_CreateWindow("Genetic Art (Refactored)", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WIDTH, HEIGHT, SDL_WINDOW_SHOWN);
     if (!g_window) goto cleanup_sdl;
 
     g_renderer = SDL_CreateRenderer(g_window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
     if (!g_renderer) goto cleanup_window;
 
     g_nk = nk_sdl_init(g_window, g_renderer);
     if (!g_nk) goto cleanup_renderer;
 
     // Embedded font loading block
     {
         struct nk_font_atlas *atlas;
         nk_sdl_font_stash_begin(&atlas);
         struct nk_font *my_font = nk_font_atlas_add_from_memory(
            atlas, 
            (void*)amiga4ever_ttf,
             amiga4ever_ttf_len,
              10.0f,
               NULL);
         if (!my_font)
             my_font = nk_font_atlas_add_default(atlas, 13.0f, NULL);
         nk_sdl_font_stash_end();
         if (my_font)
             nk_style_set_font(g_nk, &my_font->handle);
     }
 
     SDL_Surface *surf = load_and_resize_bmp(argv[1]);
     if (!surf) goto cleanup_nuklear;
     g_fmt   = SDL_AllocFormat(surf->format->format);
     g_pitch = IMAGE_W * sizeof(Uint32);
 
     g_ref_pixels  = malloc(IMAGE_W * IMAGE_H * sizeof(Uint32));
     g_best_pixels = calloc(IMAGE_W * IMAGE_H, sizeof(Uint32));
     if (!g_ref_pixels || !g_best_pixels || !g_fmt) goto cleanup_surface;
 
     SDL_LockSurface(surf);
     for (int y = 0; y < IMAGE_H; y++) {
         const Uint32 *sp = (const Uint32*)((const Uint8*)surf->pixels + y * surf->pitch);
         memcpy(&g_ref_pixels[y * IMAGE_W], sp, IMAGE_W * sizeof(Uint32));
     }
     SDL_UnlockSurface(surf);
 
     g_ref_tex = SDL_CreateTextureFromSurface(g_renderer, surf);
     SDL_FreeSurface(surf);
     g_best_tex = SDL_CreateTexture(g_renderer, g_fmt->format, SDL_TEXTUREACCESS_STREAMING, IMAGE_W, IMAGE_H);
     if (!g_ref_tex || !g_best_tex) goto cleanup_surface;
 
     do_startup_selftest();
     logStr("Welcome to GA Art (Refactored)", nk_rgb(255,255,0));
 
     GAParams params = {500, 100, 2, 0.05f, 0.70f, 1000000};
 
     GAFitnessParams fparams = {
         .ref_pixels     = g_ref_pixels,
         .scratch_pixels = calloc(IMAGE_W * IMAGE_H, sizeof(Uint32)),
         .fmt            = g_fmt,
         .pitch          = g_pitch,
         .width          = IMAGE_W,
         .height         = IMAGE_H
     };
 
     GAContext ctx = {
         .params           = &params,
         .running          = &g_running,
         .alloc_chromosome = chromosome_create,
         .free_chromosome  = chromosome_destroy,
         .best_mutex       = &g_best_mutex,
         .best_snapshot    = chromosome_create(params.nb_shapes),
         .fitness_func     = ga_sdl_fitness_callback,
         .fitness_data     = &fparams
     };
 
     pthread_t ga_tid;
     if (pthread_create(&ga_tid, NULL, ga_thread_func, &ctx) != 0) goto cleanup_surface;
 
     while (atomic_load(&g_running)) {
         SDL_Event ev;
         nk_input_begin(g_nk);
         while (SDL_PollEvent(&ev)) {
             nk_sdl_handle_event(&ev);
             if (ev.type == SDL_QUIT) atomic_store(&g_running, 0);
         }
         nk_input_end(g_nk);
 
         SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 255);
         SDL_RenderClear(g_renderer);
         SDL_RenderCopy(g_renderer, g_ref_tex, NULL, &(SDL_Rect){0, 0, IMAGE_W, IMAGE_H});
 
         pthread_mutex_lock(&g_best_mutex);
         render_chrom(ctx.best_snapshot, g_best_pixels, g_pitch, g_fmt, IMAGE_W, IMAGE_H);
         pthread_mutex_unlock(&g_best_mutex);
 
         SDL_UpdateTexture(g_best_tex, NULL, g_best_pixels, g_pitch);
         SDL_RenderCopy(g_renderer, g_best_tex, NULL, &(SDL_Rect){IMAGE_W, 0, IMAGE_W, IMAGE_H});
 
         if (nk_begin(g_nk, "Log", nk_rect(0,480,640,480), NK_WINDOW_BORDER|NK_WINDOW_SCROLL_AUTO_HIDE|NK_WINDOW_TITLE)) {
             nk_layout_row_dynamic(g_nk, 18, 1);
             pthread_mutex_lock(&g_log_mutex);
             for (int i = 0; i < g_log_count; i++)
                 nk_text_colored(g_nk, g_log_text[i], strlen(g_log_text[i]), NK_TEXT_LEFT, g_log_color[i]);
             pthread_mutex_unlock(&g_log_mutex);
         }
         nk_end(g_nk);
 
         if (nk_begin(g_nk, "Future Widgets", nk_rect(640,480,640,480), NK_WINDOW_BORDER|NK_WINDOW_TITLE)) {
             nk_layout_row_dynamic(g_nk, 30, 1);
             nk_label(g_nk, "Put your GUI controls here.", NK_TEXT_LEFT);
         }
         nk_end(g_nk);
 
         nk_sdl_render(NK_ANTI_ALIASING_ON);
         SDL_RenderPresent(g_renderer);
         nk_clear(g_nk);
         SDL_Delay(10);
     }
 
     pthread_join(ga_tid, NULL);
 
 cleanup_surface:
     free(g_ref_pixels); free(g_best_pixels); free(fparams.scratch_pixels);
     if (g_ref_tex) SDL_DestroyTexture(g_ref_tex);
     if (g_best_tex) SDL_DestroyTexture(g_best_tex);
     if (g_fmt) SDL_FreeFormat(g_fmt);
 
 cleanup_nuklear:
     if (g_nk) nk_sdl_shutdown();
 
 cleanup_renderer:
     if (g_renderer) SDL_DestroyRenderer(g_renderer);
 
 cleanup_window:
     if (g_window) SDL_DestroyWindow(g_window);
 
 cleanup_sdl:
     SDL_Quit();
     return EXIT_SUCCESS;
 }
 
 static SDL_Surface *load_and_resize_bmp(const char *filename)
 {
     SDL_Surface *orig = SDL_LoadBMP(filename);
     if (!orig) return NULL;
     if (orig->w == IMAGE_W && orig->h == IMAGE_H)
         return SDL_ConvertSurfaceFormat(orig, SDL_PIXELFORMAT_ARGB8888, 0);
 
     float scale = fminf((float)IMAGE_W/orig->w, (float)IMAGE_H/orig->h);
     int new_w = (int)(orig->w * scale);
     int new_h = (int)(orig->h * scale);
 
     SDL_Surface *tmp = SDL_CreateRGBSurfaceWithFormat(0, new_w, new_h, 32, SDL_PIXELFORMAT_ARGB8888);
     SDL_BlitScaled(orig, NULL, tmp, NULL);
 
     SDL_Surface *final = SDL_CreateRGBSurfaceWithFormat(0, IMAGE_W, IMAGE_H, 32, SDL_PIXELFORMAT_ARGB8888);
     SDL_FillRect(final, NULL, SDL_MapRGB(final->format, 0, 0, 0));
     SDL_Rect dst = {(IMAGE_W - new_w)/2, (IMAGE_H - new_h)/2, new_w, new_h};
     SDL_BlitSurface(tmp, NULL, final, &dst);
 
     SDL_FreeSurface(tmp);
     SDL_FreeSurface(orig);
     return final;
 }
 
 static void do_startup_selftest(void)
 {
 #if defined(__GNUC__) || defined(__clang__)
     logStr(__builtin_cpu_supports("avx2") ? "AVX2: ok" : "AVX2: na", nk_rgb(180,255,180));
 #else
     logStr("AVX2: unknown", nk_rgb(255,255,0));
 #endif
     char tmp[64];
     snprintf(tmp, sizeof(tmp), "Threads: %ld", sysconf(_SC_NPROCESSORS_ONLN));
     logStr(tmp, nk_rgb(180,255,180));
 #if defined(__APPLE__) || defined(HAVE_OPENCL)
     cl_uint plat_count = 0;
     if (clGetPlatformIDs(0, NULL, &plat_count) != CL_SUCCESS || plat_count == 0) {
         logStr("OpenCL GPU: not found", nk_rgb(255,200,200));
     } else {
         logStr("OpenCL GPU: found", nk_rgb(180,255,180));
     }
 #else
     logStr("OpenCL GPU: check skipped", nk_rgb(255,255,0));
 #endif
 }
 
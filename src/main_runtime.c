/**
 * @file main_runtime.c
 * @brief Runtime init and SDL/Nuklear integration for the Genetic Art demo.
 *
 * This file sets up SDL2, Nuklear (with font baking), and rendering dimensions.
 * It also provides reference image loading and the initial GAContext builder.
 * It manages the main loop, rendering, and integration between the GA engine and
 * user interface.
 */

 #define NK_INCLUDE_FIXED_TYPES
 #define NK_INCLUDE_STANDARD_IO
 #define NK_INCLUDE_STANDARD_VARARGS
 #define NK_INCLUDE_DEFAULT_ALLOCATOR
 #define NK_INCLUDE_FONT_BAKING
 #define NK_INCLUDE_DEFAULT_FONT
 #define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
 #define NK_INCLUDE_COMMAND_USERDATA
 
 // Include necessary headers for Nuklear, configuration, and other modules.
 #include "../includes/Nuklear/nuklear.h"
 #include "../includes/config.h"
 #include "../includes/software_rendering/main_runtime.h"
 #include "../includes/fonts_as_header/embedded_font.h"
 #include "../includes/software_rendering/nuklear_sdl_renderer.h"
 #include "../includes/genetic_algorithm/genetic_structs.h"
 #include "../includes/software_rendering/ga_renderer.h"
 
 // Include standard libraries for I/O, memory management, and threading.
 #include <stdio.h>
 #include <stdlib.h>
 #include <string.h>
 #include <math.h>
 #include <pthread.h>
 #include <stdatomic.h>
 #include <SDL2/SDL.h>
 
 // Include additional tools and validators.
 #include "../includes/tools/system_tools.h"
 #include "../includes/validators/bmp_validator.h"  /**< New BMP validation header. */
 
 // External declarations for logging-related variables.
 extern pthread_mutex_t g_log_mutex;
 extern char g_log_text[1024][512];
 extern struct nk_color g_log_color[1024];
 extern int g_log_count;
 
 /** Global pointers for window/renderer references. */
 static SDL_Window   *g_window   = NULL;
 static SDL_Renderer *g_renderer = NULL;
 static struct nk_context *g_nk  = NULL;
 
 /**
  * @brief Initialize SDL, window, and renderer.
  *
  * This function initializes SDL with video and timer subsystems, creates a window with specified dimensions and position,
  * and creates a renderer for the window with accelerated rendering and vsync. It also stores global references to the window and renderer.
  *
  * @param[out] window Output pointer to an SDL_Window pointer.
  * @param[out] renderer Output pointer to an SDL_Renderer pointer.
  * @return 0 on success, -1 on error.
  */
 int init_sdl_and_window(SDL_Window **window, SDL_Renderer **renderer)
 {
     // Initialize SDL with video and timer subsystems.
     if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
         fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
         return -1;
     }
 
     // Create a window with specified dimensions and position.
     *window = SDL_CreateWindow("Genetic Art",
                                 SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                 WIDTH, HEIGHT, SDL_WINDOW_SHOWN);
     if (!*window) {
         fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
         return -1;
     }
 
     // Create a renderer for the window with accelerated rendering and vsync.
     *renderer = SDL_CreateRenderer(*window, -1,
                                     SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
     if (!*renderer) {
         fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
         return -1;
     }
 
     // Store global references to the window and renderer.
     g_window   = *window;
     g_renderer = *renderer;
     return 0;
 }
 
 /**
  * @brief Initialize Nuklear and load the embedded font.
  *
  * This function initializes the Nuklear context with SDL, stores a global reference to the Nuklear context,
  * begins font stash to manage font baking, attempts to load the embedded font from memory,
  * falls back to the default font if the embedded font fails to load, ends font stash, and applies the font to the Nuklear context.
  *
  * @param[out] nk_ctx Output pointer to Nuklear context.
  * @param window SDL window.
  * @param renderer SDL renderer.
  * @return 0 on success, -1 on failure.
  */
 int init_nuklear_and_font(struct nk_context **nk_ctx,
                             SDL_Window *window,
                             SDL_Renderer *renderer)
 {
     // Initialize Nuklear context with SDL.
     *nk_ctx = nk_sdl_init(window, renderer);
     if (!*nk_ctx) return -1;
 
     // Store global reference to the Nuklear context.
     g_nk = *nk_ctx;
 
     struct nk_font_atlas *atlas;
     // Begin font stash to manage font baking.
     nk_sdl_font_stash_begin(&atlas);
 
     // Attempt to load embedded font from memory.
     struct nk_font *font = nk_font_atlas_add_from_memory(
         atlas,
         (void *)amiga4ever_ttf,
         amiga4ever_ttf_len,
         8.0f,
         NULL
     );
     if (!font) {
         // Fallback to default font if embedded font fails to load.
         font = nk_font_atlas_add_default(atlas, 13.0f, NULL);
     }
 
     // End font stash and apply the font to the Nuklear context.
     nk_sdl_font_stash_end();
     if (font) {
         nk_style_set_font(*nk_ctx, &font->handle);
     }
 
     return 0;
 }
 
 /**
  * @brief Load and convert BMP into a format suitable for rendering and GA input.
  *
  * This function validates input parameters, validates BMP header/format with a separate function,
  * loads the original BMP surface, converts or scales to the final ARGB8888 surface of size IMAGE_W x IMAGE_H,
  * allocates and sets pixel format, allocates memory for reference pixels, copies pixel data from the final surface to reference pixels,
  * creates a texture from the final surface, and validates texture creation.
  *
  * @param filename Path to the BMP image.
  * @param renderer Valid SDL_Renderer.
  * @param fmt Output pointer to SDL_PixelFormat (caller must free with SDL_FreeFormat()).
  * @param ref_pixels Output pointer to newly allocated ARGB buffer for reference image.
  * @return SDL_Texture* reference image texture or NULL on error.
  */
 SDL_Texture *load_reference_image(const char *filename,
                                     SDL_Renderer *renderer,
                                     SDL_PixelFormat **fmt,
                                     Uint32 **ref_pixels)
 {
     // Validate input parameters.
     if (!filename || !renderer || !fmt || !ref_pixels) {
         fprintf(stderr, "load_reference_image: invalid parameter.\n");
         return NULL;
     }
 
     // Validate BMP header/format with separate function.
     if (!bmp_is_valid(filename)) {
         fprintf(stderr, "Error: The BMP file is invalid or corrupted.\n");
         return NULL;
     }
 
     // Load original BMP surface.
     SDL_Surface *orig = SDL_LoadBMP(filename);
     if (!orig) {
         fprintf(stderr, "SDL_LoadBMP failed: %s\n", SDL_GetError());
         return NULL;
     }
 
     // Convert or scale to the final ARGB8888 surface of size IMAGE_W x IMAGE_H.
     SDL_Surface *final = NULL;
     if (orig->w == IMAGE_W && orig->h == IMAGE_H) {
         final = SDL_ConvertSurfaceFormat(orig, SDL_PIXELFORMAT_ARGB8888, 0);
         SDL_FreeSurface(orig);
     } else {
         // Calculate scaling factors and dimensions.
         float scale = fminf((float)IMAGE_W / orig->w, (float)IMAGE_H / orig->h);
         int new_w = (int)(orig->w * scale);
         int new_h = (int)(orig->h * scale);
 
         // Create temporary surface for scaling.
         SDL_Surface *tmp = SDL_CreateRGBSurfaceWithFormat(0, new_w, new_h, 32, SDL_PIXELFORMAT_ARGB8888);
         if (!tmp) {
             fprintf(stderr, "Failed to create scaled surface: %s\n", SDL_GetError());
             SDL_FreeSurface(orig);
             return NULL;
         }
         SDL_BlitScaled(orig, NULL, tmp, NULL);
 
         // Create final surface with target dimensions.
         final = SDL_CreateRGBSurfaceWithFormat(0, IMAGE_W, IMAGE_H, 32, SDL_PIXELFORMAT_ARGB8888);
         if (!final) {
             fprintf(stderr, "Failed to create final surface: %s\n", SDL_GetError());
             SDL_FreeSurface(tmp);
             SDL_FreeSurface(orig);
             return NULL;
         }
         SDL_FillRect(final, NULL, SDL_MapRGB(final->format, 0, 0, 0));
         SDL_Rect dst = { (IMAGE_W - new_w)/2, (IMAGE_H - new_h)/2, new_w, new_h };
         SDL_BlitSurface(tmp, NULL, final, &dst);
 
         // Free temporary surfaces.
         SDL_FreeSurface(tmp);
         SDL_FreeSurface(orig);
     }
 
     // Validate final surface.
     if (!final) {
         fprintf(stderr, "Error: final surface is NULL after conversion.\n");
         return NULL;
     }
 
     // Allocate and set pixel format.
     *fmt = SDL_AllocFormat(final->format->format);
     if (!*fmt) {
         SDL_FreeSurface(final);
         fprintf(stderr, "Error: SDL_AllocFormat failed.\n");
         return NULL;
     }
 
     // Allocate memory for reference pixels.
     *ref_pixels = (Uint32 *)malloc(IMAGE_W * IMAGE_H * sizeof(Uint32));
     if (!*ref_pixels) {
         SDL_FreeSurface(final);
         SDL_FreeFormat(*fmt);
         *fmt = NULL;
         fprintf(stderr, "Error: Out of memory for ref_pixels.\n");
         return NULL;
     }
 
     // Copy pixel data from final surface to reference pixels.
     SDL_LockSurface(final);
     for (int y = 0; y < IMAGE_H; y++) {
         const Uint32 *sp = (const Uint32 *)((const Uint8 *)final->pixels + y * final->pitch);
         memcpy(&(*ref_pixels)[y * IMAGE_W], sp, IMAGE_W * sizeof(Uint32));
     }
     SDL_UnlockSurface(final);
 
     // Create texture from final surface.
     SDL_Texture *tex = SDL_CreateTextureFromSurface(renderer, final);
     SDL_FreeSurface(final);
 
     // Validate texture creation.
     if (!tex) {
         fprintf(stderr, "Error: SDL_CreateTextureFromSurface failed: %s\n", SDL_GetError());
     }
     return tex;
 }
 
 /**
  * @brief Build and return a fully-initialized GAContext struct.
  *
  * This function allocates and initializes GA parameters, allocates and initializes fitness parameters,
  * initializes the GAContext structure, and initializes the mutex for the best chromosome.
  *
  * @param ref_pixels Pointer to the reference pixels for fitness evaluation.
  * @param best_pixels Pointer to the buffer used for rendering the best candidate.
  * @param fmt SDL pixel format for the textures.
  * @param pitch The pitch (row size in bytes) for the ARGB buffers.
  * @param running Shared atomic flag for stopping.
  * @return A fully configured GAContext structure.
  */
 GAContext build_ga_context(Uint32 *ref_pixels,
                             Uint32 *best_pixels,
                             SDL_PixelFormat *fmt,
                             int pitch,
                             atomic_int *running)
 {
    // Allocate and initialize GA parameters.
     GAParams *params = (GAParams *)malloc(sizeof(GAParams));
     *params = (GAParams){ 500, 100, 2, 0.05f, 0.70f, 1000000 };
 
     // Allocate and initialize fitness parameters.
     GAFitnessParams *fp = (GAFitnessParams *)malloc(sizeof(GAFitnessParams));
     fp->ref_pixels     = ref_pixels;
     fp->scratch_pixels = (Uint32 *)calloc(IMAGE_W * IMAGE_H, sizeof(Uint32));
     fp->fmt            = fmt;
     fp->pitch          = pitch;
     fp->width          = IMAGE_W;
     fp->height         = IMAGE_H;
 
     // Initialize GAContext structure.
     GAContext ctx;
     ctx.params           = params;
     ctx.running          = running;
     ctx.alloc_chromosome = chromosome_create;
     ctx.free_chromosome  = chromosome_destroy;
     ctx.best_mutex       = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
     ctx.best_snapshot    = chromosome_create(params->nb_shapes);
     ctx.fitness_func     = ga_sdl_fitness_callback;
     ctx.fitness_data     = fp;
     ctx.log_func         = NULL;
     ctx.log_user_data    = NULL;
 
     // Initialize the mutex for best chromosome.
     pthread_mutex_init(ctx.best_mutex, NULL);
     return ctx;
 }
 
 /**
  * @brief Run the main rendering and GUI update for each frame.
  *
  * This function is responsible for:
  *  - Beginning and ending Nuklear input for the frame
  *  - Clearing the SDL renderer and drawing the reference and best images
  *  - Displaying a scrollable, auto‐scrolling log panel below the image area
  *  - Displaying a “Future Widgets” panel beside the log panel
  *  - Presenting the composed frame and throttling frame rate
  *
  * The log panel will auto‐scroll when new entries arrive, unless the user
  * has manually scrolled away from the bottom. Auto‐scroll resumes when the user scrolls back to the bottom.
  *
  * @param ctx Pointer to the GAContext containing algorithm state
  * @param nk Pointer to the Nuklear GUI context
  * @param window Pointer to the SDL_Window in use
  * @param renderer Pointer to the SDL_Renderer for drawing
  * @param tex_ref SDL_Texture holding the reference image
  * @param tex_best SDL_Texture holding the current best candidate
  * @param best_pixels Pointer to the pixel buffer for the best image
  * @param pitch Byte pitch (row size) of the pixel buffers
  */
 void run_main_loop(GAContext *ctx,
                     struct nk_context *nk,
                     SDL_Window *window,
                     SDL_Renderer *renderer,
                     SDL_Texture *tex_ref,
                     SDL_Texture *tex_best,
                     Uint32 *best_pixels,
                     int pitch)
 {
     (void)window;
 
     // Initialize variables for auto-scrolling and UI dimensions.
     static bool    auto_scroll_enabled = true;    /**< true while log auto‐scroll is active */
     static nk_uint scroll_x = 0, scroll_y = 0;    /**< stored scroll offsets between frames */
     static int     old_log_count = 0;             /**< previous frame’s log line count */
 
     int  window_w = 0, window_h = 0;
     SDL_GetWindowSize(window, &window_w, &window_h);
     float log_h = (float)window_h - (float)IMAGE_H;  /**< height in pixels below the image */
 
     // Retrieve the current log count.
     int local_log_count;
     pthread_mutex_lock(&g_log_mutex);
     local_log_count = g_log_count;
     pthread_mutex_unlock(&g_log_mutex);
 
     // Handle Nuklear input events.
     nk_input_begin(nk);
     nk_input_end(nk);
 
     // Clear the renderer with a black background.
     SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
     SDL_RenderClear(renderer);
 
     // Render the reference image.
     SDL_Rect ref_rect = { 0, 0, IMAGE_W, IMAGE_H };
     SDL_RenderCopy(renderer, tex_ref, NULL, &ref_rect);
 
     // Render the best chromosome image.
     pthread_mutex_lock(ctx->best_mutex);
     render_chrom(ctx->best_snapshot,
                   best_pixels,
                   pitch,
                   ((GAFitnessParams *)ctx->fitness_data)->fmt,
                   IMAGE_W,
                   IMAGE_H);
     pthread_mutex_unlock(ctx->best_mutex);
 
     SDL_UpdateTexture(tex_best, NULL, best_pixels, pitch);
     SDL_Rect best_rect = { IMAGE_W, 0, IMAGE_W, IMAGE_H };
     SDL_RenderCopy(renderer, tex_best, NULL, &best_rect);
 
     // Render the log window using Nuklear.
     {
         struct nk_rect log_rect = nk_rect(0.0f, (float)IMAGE_H, (float)window_w / 2.0f, log_h);
 
         if (nk_begin(nk, "Log", log_rect,
                       NK_WINDOW_BORDER | NK_WINDOW_SCROLL_AUTO_HIDE | NK_WINDOW_TITLE))
         {
             nk_layout_row_dynamic(nk, log_h, 1);
             nk_flags group_flags = NK_WINDOW_BORDER;
             if (nk_group_begin(nk, "LogContent", group_flags)) {
                 nk_uint cur_x, cur_y;
                 nk_group_get_scroll(nk, "LogContent", &cur_x, &cur_y);
 
                 // Render log entries.
                 pthread_mutex_lock(&g_log_mutex);
                 for (int i = 0; i < g_log_count; ++i) {
                     nk_layout_row_dynamic(nk, 18, 1);
                     nk_text_colored(nk,
                                     g_log_text[i],
                                     strlen(g_log_text[i]),
                                     NK_TEXT_LEFT,
                                     g_log_color[i]);
                 }
                 pthread_mutex_unlock(&g_log_mutex);
 
                 // Calculate content height and handle auto-scrolling.
                 float content_height = (float)local_log_count * 18.0f;
                 struct nk_rect bounds = nk_widget_bounds(nk);
                 float visible_height = bounds.h;
                 float max_scroll = content_height - visible_height;
                 if (max_scroll < 0.0f) max_scroll = 0.0f;
 
                 bool new_lines = (local_log_count > old_log_count);
                 bool at_bottom = (cur_y >= (nk_uint)(max_scroll - 1.0f));
 
                 if (!at_bottom) {
                     auto_scroll_enabled = false;
                 } else if (at_bottom) {
                     auto_scroll_enabled = true;
                 }
                 if (new_lines && auto_scroll_enabled) {
                     nk_group_set_scroll(nk, "LogContent", cur_x, (nk_uint)content_height);
                 }
 
                 old_log_count = local_log_count;
                 nk_group_get_scroll(nk, "LogContent", &scroll_x, &scroll_y);
 
                 nk_group_end(nk);
             }
         }
         nk_end(nk);
     }
 
     // Render the future widgets window using Nuklear.
     {
         struct nk_rect widget_rect = nk_rect((float)window_w / 2.0f,
                                               (float)IMAGE_H,
                                               (float)window_w / 2.0f,
                                               log_h);
 
         if (nk_begin(nk, "Future Widgets", widget_rect,
                       NK_WINDOW_BORDER | NK_WINDOW_TITLE))
         {
             nk_layout_row_dynamic(nk, 30, 1);
             nk_label(nk, "Work in progress...", NK_TEXT_LEFT);
             nk_label(nk, "r&d documentation available in pdf", NK_TEXT_LEFT);
         }
         nk_end(nk);
     }
 
     // Render Nuklear UI with anti-aliasing.
     nk_sdl_render(NK_ANTI_ALIASING_ON);
     SDL_RenderPresent(renderer);
     nk_clear(nk);
 
     // Delay to control the frame rate.
     SDL_Delay(10);
 }
 
 /**
  * @brief Frees all resources in the GAContext and its dependencies.
  *
  * This function frees fitness parameters and scratch pixels, frees the best chromosome snapshot,
  * destroys and frees the best chromosome mutex, and frees GA parameters.
  *
  * @param ctx Pointer to GAContext to destroy.
  */
 void destroy_ga_context(GAContext *ctx)
 {
     if (!ctx) return;
 
     // Free fitness parameters and scratch pixels.
     GAFitnessParams *fp = (GAFitnessParams *)ctx->fitness_data;
     if (fp) {
         free(fp->scratch_pixels);
         free(fp);
     }
     // Free the best chromosome snapshot.
     if (ctx->best_snapshot) {
         ctx->free_chromosome(ctx->best_snapshot);
     }
     // Destroy and free the best chromosome mutex.
     if (ctx->best_mutex) {
         pthread_mutex_destroy(ctx->best_mutex);
         free(ctx->best_mutex);
     }
     // Free GA parameters.
     if (ctx->params) {
         free((void *)ctx->params);
     }
 }
 
 /**
  * @brief Cleans up all allocated resources and shuts down SDL.
  *
  * This function shuts down Nuklear and SDL components.
  */
 void cleanup_all(void)
 {
     // Shutdown Nuklear and SDL components.
     if (g_nk) nk_sdl_shutdown();
     if (g_renderer) SDL_DestroyRenderer(g_renderer);
     if (g_window) SDL_DestroyWindow(g_window);
     SDL_Quit();
 }

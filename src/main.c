/**
 * @file main.c
 * @brief Genetic Algorithm Art Demo main entry point (SDL2 + POSIX threads).
 * @details
 * This file initializes and runs the main application for the Genetic Algorithm Art Demo.
 *
 * It integrates all components:
 * - SDL2 and Nuklear GUI for interactive display
 * - Embedded TTF font for rendering
 * - The GA engine (defined in `genetic_art.c/.h`) which is rendering agnostic
 * - A decoupled renderer (`ga_renderer.c/.h`) that implements fitness via MSE and shape rasterization
 *
 * Responsibilities of this file include:
 * - Setting up SDL and Nuklear
 * - Loading the reference image
 * - Creating and managing the GA context
 * - Running the main loop
 * - Handling system signals and graceful termination
 *
 * Path: `root/src/main.c`
 */

 #include "software_rendering/nuklear_sdl_renderer.h"
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
 
 #include "../includes/config.h"
 #include "../includes/software_rendering/main_runtime.h"
 #include "../includes/genetic_algorithm/genetic_art.h"
 #include "../includes/tools/system_tools.h"
 
 /* GUI log buffer sizes */
 #define LOG_MAX_LINES  1024  /**< Maximum number of log lines */
 #define LOG_LINE_LEN   512   /**< Maximum length of a log line */
 
 /* Shared global variables for logging */
 pthread_mutex_t g_log_mutex = PTHREAD_MUTEX_INITIALIZER;  /**< Mutex to protect concurrent log access */
 char g_log_text[LOG_MAX_LINES][LOG_LINE_LEN];  /**< Array of log text lines */
 struct nk_color g_log_color[LOG_MAX_LINES];    /**< Corresponding color for each log line */
 int g_log_count = 0;                           /**< Current number of logged lines */
 
 /**
  * @brief Global atomic flag indicating if the application is running.
  */
 static atomic_int g_running = 1;
 
 /**
  * @brief Thread-safe logging function.
  *
  * This function logs a message to the GUI log buffer. It uses a mutex to ensure thread safety.
  *
  * @param msg The message string to log.
  * @param col The color associated with the log line in the GUI.
  */
 void logStr(const char *msg, struct nk_color col)
 {
     // Lock the mutex to ensure thread-safe access to the log buffer
     pthread_mutex_lock(&g_log_mutex);
 
     // Check if the log buffer has space for a new message
     if (g_log_count < LOG_MAX_LINES) {
         // Copy the message to the log buffer
         snprintf(g_log_text[g_log_count], LOG_LINE_LEN, "%s", msg);
 
         // Set the color for the log message
         g_log_color[g_log_count] = col;
 
         // Increment the log count
         g_log_count++;
 
         // Print the log message to the console
         printf("[logStr] %s\n", msg);
     }
 
     // Unlock the mutex
     pthread_mutex_unlock(&g_log_mutex);
 }
 
 /**
  * @brief Callback used by the GA core to emit log messages to the GUI.
  *
  * This function is called by the GA core to log messages. It maps the log level to a color and then calls logStr to log the message.
  *
  * @param level Logging level (GA_LOG_INFO, GA_LOG_WARN, GA_LOG_ERROR)
  * @param msg Log message
  * @param user_data Unused pointer for user-defined data
  */
 void ga_log_to_gui(GALogLevel level, const char *msg, void *user_data)
 {
     (void)user_data;  // Unused parameter
 
     // Define the color for the log message based on the log level
     struct nk_color color;
     switch (level) {
         case GA_LOG_INFO:  color = nk_rgb(180, 255, 180); break;  /**< Green color for info messages */
         case GA_LOG_WARN:  color = nk_rgb(255, 255, 0);   break;  /**< Yellow color for warning messages */
         case GA_LOG_ERROR: color = nk_rgb(255, 100, 100); break;  /**< Red color for error messages */
         default:           color = nk_rgb(200, 200, 200); break;  /**< Gray color for other messages */
     }
 
     // Log the message with the determined color
     logStr(msg, color);
 }
 
 /**
  * @brief Signal handler for SIGINT (Ctrl+C).
  *
  * This function handles the SIGINT signal (Ctrl+C) by setting the global running flag to 0 and printing a message to the console.
  *
  * @param sig The signal number (unused)
  */
 static void handle_sigint(int sig)
 {
     (void)sig;  // Unused parameter
 
     // Set the global running flag to 0 to indicate that the application should exit
     atomic_store(&g_running, 0);
 
     // Print a message to the console indicating that SIGINT was received
     fprintf(stderr, "\n[Ctrl+C] SIGINT received. Exiting...\n");
 }
 
 /**
  * @brief Performs advanced system checks on CPU features, OpenGL, OpenCL, and thread count.
  *
  * This function performs various system checks and logs the results. It initializes a mutex to protect access to the system capabilities structure.
  */
 static void do_startup_selftest(void)
 {
     // Define a static variable to hold the system capabilities
     static SysCapabilities caps;
 
     // Initialize the mutex to protect access to the system capabilities structure
     pthread_mutex_init(&caps.mutex, NULL);
 
     // Detect the system capabilities
     detect_system_capabilities(&caps);
 
     // Log the system capabilities
     log_system_capabilities(&caps,
                             logStr,
                             nk_rgb(180, 255, 180),  /**< Green color for info messages */
                             nk_rgb(255, 255, 0));   /**< Yellow color for warning messages */
 
     // Destroy the mutex
     pthread_mutex_destroy(&caps.mutex);
 }
 
 /**
  * @brief Main entry point of the application.
  *
  * This function is the main entry point of the application. It initializes SDL and Nuklear, loads the reference image, creates the GA context, and runs the main loop.
  *
  * @param argc Argument count.
  * @param argv Argument vector. Expects a single image path as argument.
  * @return EXIT_SUCCESS on successful execution, EXIT_FAILURE otherwise.
  */
 int main(int argc, char *argv[])
 {
     // Set the signal handler for SIGINT (Ctrl+C)
     signal(SIGINT, handle_sigint);
 
     // Check if the correct number of arguments was provided
     if (argc < 2) {
         // Print the usage message and exit with failure
         fprintf(stderr, "Usage: %s <image.bmp>\n", argv[0]);
         return EXIT_FAILURE;
     }
 
     // Seed the random number generator
     srand((unsigned)time(NULL));
 
     // Initialize SDL and create the main window and renderer
     SDL_Window *window = NULL;  /**< Pointer to main SDL window */
     SDL_Renderer *renderer = NULL;  /**< Pointer to SDL renderer */
     if (init_sdl_and_window(&window, &renderer) != 0) {
         // Exit with failure if SDL initialization failed
         return EXIT_FAILURE;
     }
 
     // Initialize Nuklear and load the font
     struct nk_context *nk_ctx = NULL;  /**< Nuklear GUI context */
     if (init_nuklear_and_font(&nk_ctx, window, renderer) != 0) {
         // Clean up and exit with failure if Nuklear initialization failed
         cleanup_all();
         return EXIT_FAILURE;
     }
 
     // Load the reference BMP file from the command line argument
     SDL_PixelFormat *fmt = NULL;  /**< Pointer to the SDL pixel format */
     Uint32 *ref_pixels = NULL;  /**< Pointer to the reference image pixels */
     SDL_Texture *tex_ref = load_reference_image(argv[1], renderer, &fmt, &ref_pixels);
     if (!tex_ref || !fmt || !ref_pixels) {
         // Clean up and exit with failure if the reference image could not be loaded
         cleanup_all();
         return EXIT_FAILURE;
     }
 
     // Allocate memory for the best image pixels
     Uint32 *best_pixels = calloc(IMAGE_W * IMAGE_H, sizeof(Uint32));
     if (!best_pixels) {
         // Clean up and exit with failure if memory allocation failed
         cleanup_all();
         return EXIT_FAILURE;
     }
 
     // Create the SDL texture for the best image
     SDL_Texture *tex_best = SDL_CreateTexture(renderer, fmt->format, SDL_TEXTUREACCESS_STREAMING, IMAGE_W, IMAGE_H);
     if (!tex_best) {
         // Free the best image pixels and clean up and exit with failure if the texture could not be created
         free(best_pixels);
         cleanup_all();
         return EXIT_FAILURE;
     }
     // Run system checks and log them
     do_startup_selftest();
     // Log welcome messages
     logStr("Welcome to GA Art (a X-platform C boilerplate for genetic coding exploration)", nk_rgb(127, 255, 0));
     logStr("by LoganSeven, under MIT license (for now)", nk_rgb(127, 255, 0));
     // Build the GA context
     GAContext ctx = build_ga_context(ref_pixels, best_pixels, fmt, IMAGE_W * sizeof(Uint32), &g_running);
     ctx.log_func = ga_log_to_gui;  /**< Set the log function for the GA context */
 
     // Create the GA thread
     pthread_t ga_tid;
     if (pthread_create(&ga_tid, NULL, ga_thread_func, &ctx) != 0) {
         // Free the best image pixels and clean up and exit with failure if the GA thread could not be created
         free(best_pixels);
         cleanup_all();
         return EXIT_FAILURE;
     }
 
     // Main loop for event handling, Nuklear GUI, and rendering reference + best images
     SDL_Event ev;
     while (atomic_load(&g_running)) {
         // Poll for SDL events
         while (SDL_PollEvent(&ev)) {
             // Pass events to Nuklear input handler
             nk_sdl_handle_event(&ev);

             // Handle mouse wheel for scrolling in Nuklear
             if (ev.type == SDL_MOUSEWHEEL) {
                 nk_input_scroll(nk_ctx, nk_vec2(0.0f, (float)ev.wheel.y));
             }
             // Handle the SDL_QUIT event to exit the application
             if (ev.type == SDL_QUIT) {
                 atomic_store(&g_running, 0);
             }
         }
         // Run the main loop to update the GA context, Nuklear GUI, and render the images
         run_main_loop(&ctx, nk_ctx, window, renderer, tex_ref, tex_best, best_pixels, IMAGE_W * sizeof(Uint32));
     }
 
     // Wait for the GA thread to exit cleanly
     pthread_join(ga_tid, NULL);
     // Clean up the GA context resources
     destroy_ga_context(&ctx);
     // Free the reference and best image pixels
     free(ref_pixels);
     free(best_pixels);
     // Free the SDL pixel format
     if (fmt) SDL_FreeFormat(fmt);
     // Clean up all resources
     cleanup_all();
     // Exit with success
     return EXIT_SUCCESS;
 }

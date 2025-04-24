/*
 * Minimal SDL2-based Nuklear “backend” function prototypes.
 *
 * - nk_sdl_init: initialize the nk_context using the given Window/Renderer
 * - nk_sdl_font_stash_begin / end: handles loading fonts (TTF or default),
 *   then re-init the nk_context to point to that font
 * - nk_sdl_handle_event: feed SDL_Events into Nuklear (keyboard, mouse, etc.)
 * - nk_sdl_render: do final UI drawing
 * - nk_sdl_shutdown: free any allocated resources
 *
 * I keep the doc comment lines exactly as you originally had. 
 */

 #ifndef NUKLEAR_SDL_RENDERER_H
 #define NUKLEAR_SDL_RENDERER_H
 
 /* keep the same includes order that you used */
 #include <SDL2/SDL.h>
 #include "Nuklear/nuklear.h"
 
 /*
  * The function prototypes must match exactly the code in .c
  * Keep them unchanged to avoid link errors.
  */
 
 #ifdef __cplusplus
 extern "C" {
 #endif
 
 /* Initialize the Nuklear context with an SDL_Window and SDL_Renderer */
 struct nk_context* nk_sdl_init(SDL_Window *win, SDL_Renderer *sdl_renderer);
 
 /* Handle an SDL event (mouse, keyboard, etc.) – integrated from official doc,
    but we keep your signature which returns void and takes a const SDL_Event* */
 void nk_sdl_handle_event(const SDL_Event *evt);
 
 /* Start and end font stash block – used to load TTF or default fonts */
 void nk_sdl_font_stash_begin(struct nk_font_atlas **atlas);
 void nk_sdl_font_stash_end(void);
 
 /* Render all Nuklear draw commands using SDL (geometry-based approach) */
 void nk_sdl_render(enum nk_anti_aliasing AA);
 
 /* Shutdown and free all resources allocated by the Nuklear SDL renderer */
 void nk_sdl_shutdown(void);
 
 #ifdef __cplusplus
 }
 #endif
 
 #endif /* NUKLEAR_SDL_RENDERER_H */
 
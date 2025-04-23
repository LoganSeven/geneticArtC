#ifndef NUKLEAR_SDL_RENDERER_H
#define NUKLEAR_SDL_RENDERER_H

#include <SDL2/SDL.h>
#include "Nuklear/nuklear.h"

/*
 * Minimal SDL2-based Nuklear “backend” function prototypes.
 *
 * - nk_sdl_init: initialize the nk_context using the given Window/Renderer
 * - nk_sdl_font_stash_begin / end: handles loading fonts (TTF or default),
 *   then re-init the nk_context to point to that font
 * - nk_sdl_handle_event: feed SDL_Events into Nuklear (keyboard, mouse, etc.)
 * - nk_sdl_render: do final UI drawing (this minimal example is mostly a stub)
 * - nk_sdl_shutdown: free any allocated resources
 */

struct nk_context* nk_sdl_init(SDL_Window *win, SDL_Renderer *sdl_renderer);
void nk_sdl_handle_event(const SDL_Event *evt);

void nk_sdl_font_stash_begin(struct nk_font_atlas **atlas);
void nk_sdl_font_stash_end(void);

void nk_sdl_render(enum nk_anti_aliasing AA);
void nk_sdl_shutdown(void);

#endif /* NUKLEAR_SDL_RENDERER_H */

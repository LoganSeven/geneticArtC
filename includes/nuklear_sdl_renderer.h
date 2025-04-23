#ifndef NUKLEAR_SDL_RENDERER_H
#define NUKLEAR_SDL_RENDERER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <SDL2/SDL.h>
#include "./Nuklear/nuklear.h"  /* the full official single-header */

struct nk_context* nk_sdl_init(SDL_Window *win, SDL_Renderer *sdl_renderer);
void nk_sdl_handle_event(const SDL_Event *evt);
void nk_sdl_font_stash_begin(struct nk_font_atlas **atlas);
void nk_sdl_font_stash_end(void);
void nk_sdl_render(enum nk_anti_aliasing AA);
void nk_sdl_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* NUKLEAR_SDL_RENDERER_H */

/******************************************************************************
 * Minimal SDL2 backend for Nuklear, in pure C.
 *
 * This file provides:
 *   - nk_sdl_init / nk_sdl_shutdown
 *   - nk_sdl_font_stash_begin / nk_sdl_font_stash_end
 *   - nk_sdl_handle_event
 *   - nk_sdl_render
 ******************************************************************************/

/* 1) Define all the Nuklear macros so we get the full struct definitions */
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT

#include "../includes/nuklear_sdl_renderer.h"
#include <stdlib.h>

static struct {
    SDL_Window       *win;
    SDL_Renderer     *rend;
    struct nk_context ctx;
    struct nk_font_atlas atlas;
    int init_done;
} g_sdl_nk = {0};

/* ------------------------------------------------------------------------- */
struct nk_context* nk_sdl_init(SDL_Window *win, SDL_Renderer *renderer)
{
    if (!win || !renderer) {
        return NULL;
    }
    g_sdl_nk.win   = win;
    g_sdl_nk.rend  = renderer;
    g_sdl_nk.init_done = 1;

    /* Initialize a default Nuklear context with a null font (for now). */
    nk_init_default(&g_sdl_nk.ctx, NULL);
    return &g_sdl_nk.ctx;
}

/* ------------------------------------------------------------------------- */
void nk_sdl_handle_event(const SDL_Event *evt)
{
    if (!g_sdl_nk.init_done || !evt) return;

    /* Typically: parse SDL_Event -> call nk_input_* functions to feed Nuklear.
       For now, this is a placeholder. */
}

/* ------------------------------------------------------------------------- */
void nk_sdl_font_stash_begin(struct nk_font_atlas **atlas)
{
    if (!g_sdl_nk.init_done || !atlas) return;

    /* Initialize and begin collecting glyph data. */
    nk_font_atlas_init_default(&g_sdl_nk.atlas);
    nk_font_atlas_begin(&g_sdl_nk.atlas);
    *atlas = &g_sdl_nk.atlas; /* return the pointer to the user */
}

/* ------------------------------------------------------------------------- */
void nk_sdl_font_stash_end(void)
{
    if (!g_sdl_nk.init_done) return;

    /* Instead of passing '0', pass nk_handle_id(0) */
    nk_font_atlas_end(&g_sdl_nk.atlas, nk_handle_id(0), NULL);

    if (g_sdl_nk.atlas.default_font) {
        nk_init_default(&g_sdl_nk.ctx, &g_sdl_nk.atlas.default_font->handle);
    }
}

/* ------------------------------------------------------------------------- */
void nk_sdl_render(enum nk_anti_aliasing AA)
{
    if (!g_sdl_nk.init_done) return;

    /* 1) Normally, you'd iterate over the command buffer:
         const struct nk_command *cmd;
         nk_foreach(cmd, &g_sdl_nk.ctx) {
             // switch(cmd->type) { ... do SDL draws ... }
         }
       2) This sample does nothing. Just a placeholder. 
    */
    (void)AA; /* avoid unused-var warning */
}

/* ------------------------------------------------------------------------- */
void nk_sdl_shutdown(void)
{
    if (!g_sdl_nk.init_done) return;

    /* Cleanup the atlas & context. Freed memory includes TTF data. */
    nk_font_atlas_cleanup(&g_sdl_nk.atlas);
    nk_free(&g_sdl_nk.ctx);

    /* Reset everything to zero. */
    g_sdl_nk.win   = NULL;
    g_sdl_nk.rend  = NULL;
    g_sdl_nk.init_done = 0;
}

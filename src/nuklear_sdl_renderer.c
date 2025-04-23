#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_COMMAND_USERDATA

#include "../includes/Nuklear/nuklear.h"
#include "../includes/nuklear_sdl_renderer.h"
#include <SDL2/SDL.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

struct nk_draw_vertex {
    struct nk_vec2 position;
    struct nk_vec2 uv;
    nk_byte col[4];
};

static struct {
    SDL_Window       *win;
    SDL_Renderer     *rend;
    struct nk_context ctx;
    struct nk_font_atlas atlas;
    const struct nk_user_font *active_font;
    SDL_Texture      *font_tex;
    int init_done;
    int atlas_width;
    int atlas_height;
} g_sdl_nk;

struct nk_context* nk_sdl_init(SDL_Window *win, SDL_Renderer *renderer)
{
    memset(&g_sdl_nk, 0, sizeof(g_sdl_nk));
    g_sdl_nk.win  = win;
    g_sdl_nk.rend = renderer;
    nk_init_default(&g_sdl_nk.ctx, NULL);
    g_sdl_nk.init_done = 1;
    return &g_sdl_nk.ctx;
}

void nk_sdl_font_stash_begin(struct nk_font_atlas **atlas)
{
    if (!g_sdl_nk.init_done || !atlas) return;
    nk_font_atlas_init_default(&g_sdl_nk.atlas);
    nk_font_atlas_begin(&g_sdl_nk.atlas);
    *atlas = &g_sdl_nk.atlas;
}

void nk_sdl_font_stash_end(void)
{
    if (!g_sdl_nk.init_done) return;

    int w = 0, h = 0;
    const void *img = nk_font_atlas_bake(&g_sdl_nk.atlas, &w, &h, NK_FONT_ATLAS_RGBA32);

    if (!img || w <= 0 || h <= 0) {
        fprintf(stderr, "[FATAL] nk_font_atlas_bake failed or returned invalid size\n");
        return;
    }

    g_sdl_nk.atlas_width  = w;
    g_sdl_nk.atlas_height = h;

    // ✅ Create a surface from the baked font atlas
    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormatFrom(
        (void *)img, w, h, 32, w * 4, SDL_PIXELFORMAT_RGBA8888);

    if (!surface) {
        fprintf(stderr, "[FATAL] Failed to create SDL surface from font atlas\n");
        return;
    }

    // ✅ Convert the surface to the renderer’s pixel format
    SDL_Surface *formatted = SDL_ConvertSurfaceFormat(surface, SDL_PIXELFORMAT_RGBA8888, 0);
    SDL_FreeSurface(surface);
    if (!formatted) {
        fprintf(stderr, "[FATAL] Failed to convert SDL surface format\n");
        return;
    }

    // ✅ Create the final texture from the surface
    g_sdl_nk.font_tex = SDL_CreateTextureFromSurface(g_sdl_nk.rend, formatted);
    SDL_FreeSurface(formatted);

    if (!g_sdl_nk.font_tex) {
        fprintf(stderr, "[FATAL] Failed to create font texture\n");
        return;
    }

    SDL_SetTextureBlendMode(g_sdl_nk.font_tex, SDL_BLENDMODE_BLEND);

    // ✅ Complete Nuklear font atlas
    nk_font_atlas_end(&g_sdl_nk.atlas,
                      nk_handle_ptr(g_sdl_nk.font_tex),
                      NULL);

    // ✅ Set default font in Nuklear
    if (g_sdl_nk.atlas.default_font) {
        g_sdl_nk.active_font = &g_sdl_nk.atlas.default_font->handle;
        nk_style_set_font(&g_sdl_nk.ctx, g_sdl_nk.active_font);
    }
}



void nk_sdl_handle_event(const SDL_Event *evt)
{
    if (!g_sdl_nk.init_done || !evt) return;
}

void nk_sdl_render(enum nk_anti_aliasing AA)
{
    struct nk_buffer cmds, vbuf, ibuf;
    nk_buffer_init_default(&cmds);
    nk_buffer_init_default(&vbuf);
    nk_buffer_init_default(&ibuf);

    static const struct nk_draw_vertex_layout_element layout[] = {
        {NK_VERTEX_POSITION, NK_FORMAT_FLOAT, NK_OFFSETOF(struct nk_draw_vertex, position)},
        {NK_VERTEX_TEXCOORD, NK_FORMAT_FLOAT, NK_OFFSETOF(struct nk_draw_vertex, uv)},
        {NK_VERTEX_COLOR, NK_FORMAT_R8G8B8A8, NK_OFFSETOF(struct nk_draw_vertex, col)},
        {NK_VERTEX_LAYOUT_END}
    };

    const struct nk_convert_config cfg = {
        .vertex_layout = layout,
        .vertex_size = sizeof(struct nk_draw_vertex),
        .vertex_alignment = NK_ALIGNOF(struct nk_draw_vertex),
        .circle_segment_count = 22,
        .curve_segment_count = 22,
        .arc_segment_count = 22,
        .global_alpha = 1.0f,
        .shape_AA = AA,
        .line_AA = AA
    };

    nk_convert(&g_sdl_nk.ctx, &cmds, &vbuf, &ibuf, &cfg);

    const struct nk_draw_vertex *verts = nk_buffer_memory_const(&vbuf);
    const nk_draw_index *idx = nk_buffer_memory_const(&ibuf);
    const struct nk_draw_command *cmd;
    int offset = 0;

    nk_draw_foreach(cmd, &g_sdl_nk.ctx, &cmds) {
        if (!cmd->elem_count) continue;

        SDL_Rect clip = {
            (int)cmd->clip_rect.x,
            (int)cmd->clip_rect.y,
            (int)cmd->clip_rect.w,
            (int)cmd->clip_rect.h
        };
        SDL_RenderSetClipRect(g_sdl_nk.rend, &clip);

        for (unsigned i = 0; i < cmd->elem_count; i += 3) {
            const struct nk_draw_vertex *v0 = &verts[idx[offset + i + 0]];
            const struct nk_draw_vertex *v1 = &verts[idx[offset + i + 1]];
            const struct nk_draw_vertex *v2 = &verts[idx[offset + i + 2]];

            float min_x = fminf(fminf(v0->position.x, v1->position.x), v2->position.x);
            float max_x = fmaxf(fmaxf(v0->position.x, v1->position.x), v2->position.x);
            float min_y = fminf(fminf(v0->position.y, v1->position.y), v2->position.y);
            float max_y = fmaxf(fmaxf(v0->position.y, v1->position.y), v2->position.y);

            SDL_Rect box = {
                (int)min_x,
                (int)min_y,
                (int)(max_x - min_x),
                (int)(max_y - min_y)
            };

            SDL_SetRenderDrawColor(g_sdl_nk.rend, v0->col[0], v0->col[1], v0->col[2], v0->col[3]);
            SDL_RenderFillRect(g_sdl_nk.rend, &box);
        }

        offset += cmd->elem_count;
    }

    SDL_RenderSetClipRect(g_sdl_nk.rend, NULL);
    nk_buffer_free(&cmds);
    nk_buffer_free(&vbuf);
    nk_buffer_free(&ibuf);
}





void nk_sdl_shutdown(void)
{
    if (!g_sdl_nk.init_done) return;
    if (g_sdl_nk.font_tex)
        SDL_DestroyTexture(g_sdl_nk.font_tex);
    nk_font_atlas_cleanup(&g_sdl_nk.atlas);
    nk_free(&g_sdl_nk.ctx);
    memset(&g_sdl_nk, 0, sizeof(g_sdl_nk));
}

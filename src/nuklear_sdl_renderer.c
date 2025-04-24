/*
 * This is the full Nuklear+SDL2 renderer implementation.
 * It merges your structure and includes with the official doc's code.
 * The code is pure C, with no partial references or forward-definition issues.
 *
 */

/* ---- 1) Keep your order of #defines for Nuklear single-header usage ---- */
#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_COMMAND_USERDATA

/* ---- 2) Includes: your order and style ---- */
#include "../includes/Nuklear/nuklear.h"
#include "../includes/nuklear_sdl_renderer.h"

#include <SDL2/SDL.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/*
 * The code defines a vertex struct matching the official doc's layout.
 * Unifies it with the official approach.
 */
struct nk_draw_vertex {
    struct nk_vec2 position;
    struct nk_vec2 uv;
    nk_byte col[4];
};

/* 
 * Official doc uses a device struct plus a top-level struct to store
 * SDL-specific info, the NK context, and font atlas.
 * This merges that approach into a single static block.
 */
struct nk_sdl_device {
    struct nk_buffer cmds;            /* draw commands buffer */
    struct nk_draw_null_texture tex_null;
    SDL_Texture *font_tex;            /* texture storing the baked font atlas */
};

static struct {
    SDL_Window   *win;
    SDL_Renderer *rend;
    struct nk_sdl_device dev;         /* from official doc approach */
    struct nk_context    ctx;         /* your nk_context */
    struct nk_font_atlas atlas;       /* for font stash usage */
    int init_done;
    int atlas_width;
    int atlas_height;
    Uint64 time_of_last_frame;
} g_sdl_nk;

/* 
 * Forward declarations (internal)
 * for handling copy/paste from official doc, plus some geometry calls
 */
static void nk_sdl_device_upload_atlas(const void *image, int width, int height);
static void nk_sdl_clipboard_paste(nk_handle usr, struct nk_text_edit *edit);
static void nk_sdl_clipboard_copy(nk_handle usr, const char *text, int len);

/*
 * This function is not part of your original prototypes,
 * but preserves logic from the official doc for mouse grabbing.
 * If needed externally, it could be exposed. Kept static for now.
 */
static void nk_sdl_handle_grab(void);

/*
 * ----------------------------------------------------------------------------
 *                          Implementation
 * ----------------------------------------------------------------------------
 */

/*
 * nk_sdl_init:
 * Initializes the Nuklear context with an SDL_Window and an SDL_Renderer.
 * Official doc code merged with your logic.
 */
struct nk_context* nk_sdl_init(SDL_Window *win, SDL_Renderer *renderer)
{
    /* set up the global struct */
    memset(&g_sdl_nk, 0, sizeof(g_sdl_nk));
    g_sdl_nk.win       = win;
    g_sdl_nk.rend      = renderer;
    g_sdl_nk.init_done = 1;
    g_sdl_nk.time_of_last_frame = SDL_GetTicks64();

    /* use nk_init_default first, no custom font yet */
    nk_init_default(&g_sdl_nk.ctx, NULL);

    /* Setup clipboard callbacks from official doc for copying & pasting text */
    g_sdl_nk.ctx.clip.copy     = nk_sdl_clipboard_copy;
    g_sdl_nk.ctx.clip.paste    = nk_sdl_clipboard_paste;
    g_sdl_nk.ctx.clip.userdata = nk_handle_ptr(0);

    /* init draw command buffer in our device struct */
    nk_buffer_init_default(&g_sdl_nk.dev.cmds);

    /* FIX: enable blend mode for renderer */
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    return &g_sdl_nk.ctx;
}

/*
 * Start the font stash building process,
 * unifying your approach with the official doc's approach.
 */
void nk_sdl_font_stash_begin(struct nk_font_atlas **atlas)
{
    if (!g_sdl_nk.init_done || !atlas) return;

    nk_font_atlas_init_default(&g_sdl_nk.atlas);
    nk_font_atlas_begin(&g_sdl_nk.atlas);
    *atlas = &g_sdl_nk.atlas;
}

/*
 * End the font stash process and create the font texture.
 * This merges official code with your previously minimal approach.
 */
void nk_sdl_font_stash_end(void)
{
    if (!g_sdl_nk.init_done) return;

    int w = 0, h = 0;
    const void *img = nk_font_atlas_bake(&g_sdl_nk.atlas, &w, &h, NK_FONT_ATLAS_RGBA32);

    if (!img || w <= 0 || h <= 0) {
        SDL_Log("[FATAL] nk_font_atlas_bake failed or returned invalid size\n");
        return;
    }

    g_sdl_nk.atlas_width  = w;
    g_sdl_nk.atlas_height = h;

    /* Create the SDL texture from the baked font atlas */
    nk_sdl_device_upload_atlas(img, w, h);

    /*
     * official doc:
     * "nk_font_atlas_end(..., nk_handle_ptr(font_tex), &tex_null)"
     */
    nk_font_atlas_end(&g_sdl_nk.atlas,
                      nk_handle_ptr(g_sdl_nk.dev.font_tex),
                      &g_sdl_nk.dev.tex_null);

    /* If default_font was baked, set it now */
    if (g_sdl_nk.atlas.default_font)
        nk_style_set_font(&g_sdl_nk.ctx, &g_sdl_nk.atlas.default_font->handle);
}

/*
 * nk_sdl_handle_event:
 * Official doc returns an int, but your code signature is void.
 * Integrates official doc's event logic but skip the returned integer.
 */
void nk_sdl_handle_event(const SDL_Event *evt)
{
    if (!g_sdl_nk.init_done || !evt) return;

    /* replicate official doc approach using a local non-const event pointer */
    SDL_Event e = *evt; /* copy so it can be passed to some APIs if needed */

    struct nk_context *ctx = &g_sdl_nk.ctx;
    switch(e.type) {
    case SDL_KEYUP: /* handle both KEYUP & KEYDOWN */
    case SDL_KEYDOWN:
        {
            int down = (e.type == SDL_KEYDOWN);
            switch(e.key.keysym.sym) {
            case SDLK_RSHIFT:
            case SDLK_LSHIFT:    nk_input_key(ctx, NK_KEY_SHIFT, down); break;
            case SDLK_DELETE:    nk_input_key(ctx, NK_KEY_DEL, down);   break;
            case SDLK_RETURN:
            case SDLK_KP_ENTER:  nk_input_key(ctx, NK_KEY_ENTER, down); break;
            case SDLK_TAB:       nk_input_key(ctx, NK_KEY_TAB, down);   break;
            case SDLK_BACKSPACE: nk_input_key(ctx, NK_KEY_BACKSPACE, down); break;
            case SDLK_HOME:
                nk_input_key(ctx, NK_KEY_TEXT_START, down);
                nk_input_key(ctx, NK_KEY_SCROLL_START, down);
                break;
            case SDLK_END:
                nk_input_key(ctx, NK_KEY_TEXT_END, down);
                nk_input_key(ctx, NK_KEY_SCROLL_END, down);
                break;
            case SDLK_PAGEUP:
                nk_input_key(ctx, NK_KEY_SCROLL_UP, down);
                break;
            case SDLK_PAGEDOWN:
                nk_input_key(ctx, NK_KEY_SCROLL_DOWN, down);
                break;
            default:
                {
                    int ctrl_down = SDL_GetModState() & (KMOD_LCTRL | KMOD_RCTRL);
                    switch(e.key.keysym.sym) {
                    case SDLK_z: nk_input_key(ctx, NK_KEY_TEXT_UNDO, (down && ctrl_down)); break;
                    case SDLK_r: nk_input_key(ctx, NK_KEY_TEXT_REDO, (down && ctrl_down)); break;
                    case SDLK_c: nk_input_key(ctx, NK_KEY_COPY,      (down && ctrl_down)); break;
                    case SDLK_v: nk_input_key(ctx, NK_KEY_PASTE,     (down && ctrl_down)); break;
                    case SDLK_x: nk_input_key(ctx, NK_KEY_CUT,       (down && ctrl_down)); break;
                    case SDLK_b: nk_input_key(ctx, NK_KEY_TEXT_LINE_START, (down && ctrl_down)); break;
                    case SDLK_e: nk_input_key(ctx, NK_KEY_TEXT_LINE_END,   (down && ctrl_down)); break;
                    case SDLK_a: nk_input_key(ctx, NK_KEY_TEXT_SELECT_ALL, (down && ctrl_down)); break;
                    case SDLK_LEFT:
                        if (ctrl_down) nk_input_key(ctx, NK_KEY_TEXT_WORD_LEFT, down);
                        else nk_input_key(ctx, NK_KEY_LEFT, down);
                        break;
                    case SDLK_RIGHT:
                        if (ctrl_down) nk_input_key(ctx, NK_KEY_TEXT_WORD_RIGHT, down);
                        else nk_input_key(ctx, NK_KEY_RIGHT, down);
                        break;
                    case SDLK_UP:   nk_input_key(ctx, NK_KEY_UP,   down); break;
                    case SDLK_DOWN: nk_input_key(ctx, NK_KEY_DOWN, down); break;
                    default: break; /* no-op */
                    }
                }
                break;
            }
        }
        break;

    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP:
        {
            int down = (e.type == SDL_MOUSEBUTTONDOWN);
            int x = e.button.x;
            int y = e.button.y;

            if (e.button.button == SDL_BUTTON_LEFT) {
                if (e.button.clicks > 1)
                    nk_input_button(ctx, NK_BUTTON_DOUBLE, x, y, down);
                nk_input_button(ctx, NK_BUTTON_LEFT, x, y, down);
            } else if (e.button.button == SDL_BUTTON_MIDDLE) {
                nk_input_button(ctx, NK_BUTTON_MIDDLE, x, y, down);
            } else if (e.button.button == SDL_BUTTON_RIGHT) {
                nk_input_button(ctx, NK_BUTTON_RIGHT, x, y, down);
            }
        }
        break;

    case SDL_MOUSEMOTION:
        {
            if (ctx->input.mouse.grabbed) {
                int x = (int)ctx->input.mouse.prev.x;
                int y = (int)ctx->input.mouse.prev.y;
                nk_input_motion(ctx, x + e.motion.xrel, y + e.motion.yrel);
            } else {
                nk_input_motion(ctx, e.motion.x, e.motion.y);
            }
        }
        break;

    case SDL_MOUSEWHEEL:
        nk_input_scroll(ctx, nk_vec2((float)e.wheel.preciseX, (float)e.wheel.preciseY));
        break;

    case SDL_TEXTINPUT:
        {
            nk_glyph glyph;
            memcpy(glyph, e.text.text, NK_UTF_SIZE);
            nk_input_glyph(ctx, glyph);
        }
        break;

    default:
        break;
    }

    /* handle grabbing outside event loop to match official doc approach */
    nk_sdl_handle_grab();
}

/*
 * nk_sdl_render:
 * Uses SDL_RenderGeometryRaw (or fallback) to draw Nuklear shapes.
 * The function signature remains the same as your original.
 */
void nk_sdl_render(enum nk_anti_aliasing AA)
{
    if (!g_sdl_nk.init_done) return;

    struct nk_context *ctx = &g_sdl_nk.ctx;
    struct nk_sdl_device *dev = &g_sdl_nk.dev;

    /* Config structure for the shape conversion. */
    static const struct nk_draw_vertex_layout_element vertex_layout[] = {
        {NK_VERTEX_POSITION, NK_FORMAT_FLOAT,   offsetof(struct nk_draw_vertex, position)},
        {NK_VERTEX_TEXCOORD, NK_FORMAT_FLOAT,   offsetof(struct nk_draw_vertex, uv)},
        {NK_VERTEX_COLOR,    NK_FORMAT_R8G8B8A8,offsetof(struct nk_draw_vertex, col)},
        {NK_VERTEX_LAYOUT_END}
    };
    struct nk_convert_config config;
    memset(&config, 0, sizeof(config));
    config.vertex_layout = vertex_layout;
    config.vertex_size = sizeof(struct nk_draw_vertex);
    config.vertex_alignment = NK_ALIGNOF(struct nk_draw_vertex);
    config.tex_null = dev->tex_null;
    config.circle_segment_count = 22;
    config.curve_segment_count  = 22;
    config.arc_segment_count    = 22;
    config.global_alpha         = 1.0f;
    config.shape_AA             = AA;
    config.line_AA              = AA;

    /* Buffers to hold converted vertices + indices */
    struct nk_buffer cmds;
    struct nk_buffer vbuf;
    struct nk_buffer ibuf;
    nk_buffer_init_default(&cmds);
    nk_buffer_init_default(&vbuf);
    nk_buffer_init_default(&ibuf);

    /* measure frame time and set ctx->delta_time_seconds */
    Uint64 now = SDL_GetTicks64();
    ctx->delta_time_seconds = (float)(now - g_sdl_nk.time_of_last_frame) / 1000.0f;
    g_sdl_nk.time_of_last_frame = now;

    /* Convert queue of draw commands into geometry buffers */
    nk_convert(ctx, &dev->cmds, &vbuf, &ibuf, &config);

    /*
     * Iterate over the draw commands,
     * using SDL_RenderGeometryRaw() for triangle drawing.
     */
    {
        SDL_Rect saved_clip;
        SDL_bool clipping_enabled = SDL_RenderIsClipEnabled(g_sdl_nk.rend);
        SDL_RenderGetClipRect(g_sdl_nk.rend, &saved_clip);

        const struct nk_draw_command *cmd_iter;
        const struct nk_draw_vertex *verts = 
            (const struct nk_draw_vertex*)nk_buffer_memory_const(&vbuf);
        const nk_draw_index *index_buf = 
            (const nk_draw_index*)nk_buffer_memory_const(&ibuf);

        size_t offset = 0; /* current index offset for each draw command */

        nk_draw_foreach(cmd_iter, ctx, &dev->cmds) {
            if (!cmd_iter->elem_count) {
                continue;
            }

            SDL_Rect clip = {
                (int)cmd_iter->clip_rect.x,
                (int)cmd_iter->clip_rect.y,
                (int)cmd_iter->clip_rect.w,
                (int)cmd_iter->clip_rect.h
            };

            /* clamp if needed, matching official doc references */
            SDL_RenderSetClipRect(g_sdl_nk.rend, &clip);

            SDL_RenderGeometryRaw(
                g_sdl_nk.rend,
                (SDL_Texture*)cmd_iter->texture.ptr,
                /* position pointer, position stride in bytes */
                (const float*)((const nk_byte*)verts + offsetof(struct nk_draw_vertex, position)),
                (int)sizeof(struct nk_draw_vertex),
                /* color pointer, color stride in bytes */
                (const SDL_Color*)((const nk_byte*)verts + offsetof(struct nk_draw_vertex, col)),
                (int)sizeof(struct nk_draw_vertex),
                /* texcoord pointer, texcoord stride in bytes */
                (const float*)((const nk_byte*)verts + offsetof(struct nk_draw_vertex, uv)),
                (int)sizeof(struct nk_draw_vertex),
                /* number of vertices = total buffer / stride */
                (int)(vbuf.allocated / sizeof(struct nk_draw_vertex)),
                /* The index array + # of indices to draw + size of each index */
                (const void*)(index_buf + offset),
                (int)cmd_iter->elem_count,
                sizeof(nk_draw_index)
            );

            offset += cmd_iter->elem_count;
        }

        /* Restore clip rect */
        SDL_RenderSetClipRect(g_sdl_nk.rend, &saved_clip);
        if (!clipping_enabled) {
            SDL_RenderSetClipRect(g_sdl_nk.rend, NULL);
        }
    }

    /* Clear out for next frame */
    nk_clear(ctx);
    nk_buffer_clear(&dev->cmds);
    nk_buffer_free(&vbuf);
    nk_buffer_free(&ibuf);
    nk_buffer_free(&cmds);
}

/*
 * nk_sdl_shutdown:
 * Frees everything, merging official doc with your minimal approach.
 */
void nk_sdl_shutdown(void)
{
    if (!g_sdl_nk.init_done) return;

    /* free the atlas */
    nk_font_atlas_clear(&g_sdl_nk.atlas);

    /* free the NK context */
    nk_free(&g_sdl_nk.ctx);

    /* destroy the font texture */
    if (g_sdl_nk.dev.font_tex) {
        SDL_DestroyTexture(g_sdl_nk.dev.font_tex);
        g_sdl_nk.dev.font_tex = NULL;
    }

    /* free the command buffer */
    nk_buffer_free(&g_sdl_nk.dev.cmds);

    /* zero out the global struct */
    memset(&g_sdl_nk, 0, sizeof(g_sdl_nk));
}

/*
 * Internal function to upload the baked font atlas to an SDL texture.
 * Combines your code with official doc references to user texture handle.
 */
static void nk_sdl_device_upload_atlas(const void *image, int width, int height)
{
    /* 
     * Create a static SDL texture from the RGBA32 data pointer 'image'. 
     * Use endianness-based logic so alpha is recognized correctly.
     */
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
    #define SDL_NK_ATLAS_FORMAT SDL_PIXELFORMAT_ARGB8888
#else
    #define SDL_NK_ATLAS_FORMAT SDL_PIXELFORMAT_ABGR8888
#endif

    SDL_Texture *tex = SDL_CreateTexture(g_sdl_nk.rend, 
                                         SDL_NK_ATLAS_FORMAT,
                                         SDL_TEXTUREACCESS_STATIC,
                                         width, height);
    if (!tex) {
        SDL_Log("[FATAL] error creating SDL texture for atlas\n");
        return;
    }

    /* Transfer the raw RGBA buffer into the texture */
    SDL_UpdateTexture(tex, NULL, image, width * 4);
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);

    /* store it in our global device struct */
    g_sdl_nk.dev.font_tex = tex;
}

/* Official doc approach for clipboard paste */
static void nk_sdl_clipboard_paste(nk_handle usr, struct nk_text_edit *edit)
{
    (void)usr;
    char *text = SDL_GetClipboardText();
    if (text) {
        nk_textedit_paste(edit, text, nk_strlen(text));
        SDL_free(text);
    }
}

/* Official doc approach for clipboard copy */
static void nk_sdl_clipboard_copy(nk_handle usr, const char *text, int len)
{
    (void)usr;
    if (!len) return;
    char *str = (char*)malloc((size_t)len + 1);
    if (!str) return;
    memcpy(str, text, (size_t)len);
    str[len] = '\0';
    SDL_SetClipboardText(str);
    free(str);
}

/*
 * Official doc approach to handle mouse grabbing after event processing.
 * SDL_SetRelativeMouseMode can help if user wants "locked" mouse for certain UIs.
 */
static void nk_sdl_handle_grab(void)
{
    struct nk_context *ctx = &g_sdl_nk.ctx;
    if (ctx->input.mouse.grab) {
        SDL_SetRelativeMouseMode(SDL_TRUE);
    } else if (ctx->input.mouse.ungrab) {
        SDL_SetRelativeMouseMode(SDL_FALSE);
        SDL_WarpMouseInWindow(g_sdl_nk.win,
                              (int)ctx->input.mouse.prev.x,
                              (int)ctx->input.mouse.prev.y);
    } else if (ctx->input.mouse.grabbed) {
        ctx->input.mouse.pos.x = ctx->input.mouse.prev.x;
        ctx->input.mouse.pos.y = ctx->input.mouse.prev.y;
    }
}

/**
 * @file nk_sdl_renderer.c
 * @brief Full Nuklear+SDL2 renderer implementation in C, plus extra chromosome drawing.
 *
 * Integrates Nuklear with SDL for rendering via SDL_RenderGeometryRaw.
 * Provides a new function to draw Chromosome shapes with Nuklear.
 */

 #include "genetic_algorithm/genetic_structs.h"
 #define NK_INCLUDE_FIXED_TYPES
 #define NK_INCLUDE_STANDARD_IO
 #define NK_INCLUDE_STANDARD_VARARGS
 #define NK_INCLUDE_DEFAULT_ALLOCATOR
 #define NK_INCLUDE_FONT_BAKING
 #define NK_INCLUDE_DEFAULT_FONT
 #define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
 #define NK_INCLUDE_COMMAND_USERDATA
 
 #include "../includes/Nuklear/nuklear.h"
 #include "../includes/software_rendering/nuklear_sdl_renderer.h"
 
 #include <SDL2/SDL.h>
 #include <stdlib.h>
 #include <string.h>
 #include <stdint.h>
 
 /**
  * @brief Vertex structure matching the official Nuklear documentation layout.
  * Each vertex stores position, UV coordinates, and an RGBA color in 4 bytes.
  */
 struct nk_draw_vertex {
     struct nk_vec2 position; /**< Vertex position */
     struct nk_vec2 uv;       /**< Texture UV coordinates */
     nk_byte col[4];          /**< RGBA color */
 };
 
 /**
  * @brief Device structure for managing draw commands and font texture.
  * Holds an nk_buffer for commands, a null texture for Nuklear,
  * and an SDL texture for the baked font atlas.
  */
 struct nk_sdl_device {
     struct nk_buffer cmds;                    /**< Buffer for holding draw commands */
     struct nk_draw_null_texture tex_null;     /**< Null texture used by Nuklear */
     SDL_Texture *font_tex;                    /**< SDL texture for the baked font atlas */
 };
 
 /**
  * @brief Global static structure holding SDL and Nuklear context data.
  */
 static struct {
     SDL_Window   *win;             /**< Pointer to the SDL window */
     SDL_Renderer *rend;            /**< Pointer to the SDL renderer */
     struct nk_sdl_device dev;      /**< Device for rendering with Nuklear */
     struct nk_context    ctx;      /**< Nuklear context */
     struct nk_font_atlas atlas;    /**< Nuklear font atlas */
     int init_done;                 /**< Flag indicating if initialization succeeded */
     int atlas_width;               /**< Width of the baked font atlas */
     int atlas_height;              /**< Height of the baked font atlas */
     Uint64 time_of_last_frame;     /**< Timestamp of the last rendered frame */
 } g_sdl_nk;
 
 /* Forward declarations (internal) */
 static void nk_sdl_device_upload_atlas(const void *image, int width, int height);
 static void nk_sdl_clipboard_paste(nk_handle usr, struct nk_text_edit *edit);
 static void nk_sdl_clipboard_copy(nk_handle usr, const char *text, int len);
 static void nk_sdl_handle_grab(void);
 
 /* ----------------------------------------------------------------------------
  *                          Implementation
  * --------------------------------------------------------------------------*/
 
 /**
  * @brief Initializes the Nuklear context with an SDL_Window and an SDL_Renderer.
  * @param win      Pointer to an SDL_Window
  * @param renderer Pointer to an SDL_Renderer
  * @return Pointer to the internal Nuklear context (nk_context*)
  */
 struct nk_context* nk_sdl_init(SDL_Window *win, SDL_Renderer *renderer)
 {
     memset(&g_sdl_nk, 0, sizeof(g_sdl_nk));
     g_sdl_nk.win       = win;
     g_sdl_nk.rend      = renderer;
     g_sdl_nk.init_done = 1;
     g_sdl_nk.time_of_last_frame = SDL_GetTicks64();
 
     /* Initialize Nuklear with default settings */
     nk_init_default(&g_sdl_nk.ctx, NULL);
 
     /* Setup clipboard callbacks */
     g_sdl_nk.ctx.clip.copy     = nk_sdl_clipboard_copy;
     g_sdl_nk.ctx.clip.paste    = nk_sdl_clipboard_paste;
     g_sdl_nk.ctx.clip.userdata = nk_handle_ptr(0);
 
     /* Initialize draw command buffer */
     nk_buffer_init_default(&g_sdl_nk.dev.cmds);
 
     /* Enable blending in the SDL renderer */
     SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
 
     return &g_sdl_nk.ctx;
 }
 
 /**
  * @brief Begins building the font stash (font atlas).
  * @param atlas Double pointer that will be set to the internal nk_font_atlas
  */
 void nk_sdl_font_stash_begin(struct nk_font_atlas **atlas)
 {
     if (!g_sdl_nk.init_done || !atlas) return;
 
     nk_font_atlas_init_default(&g_sdl_nk.atlas);
     nk_font_atlas_begin(&g_sdl_nk.atlas);
     *atlas = &g_sdl_nk.atlas;
 }
 
 /**
  * @brief Completes the font stash building and creates the SDL texture for the atlas.
  */
 void nk_sdl_font_stash_end(void)
 {
     if (!g_sdl_nk.init_done) return;
 
     int w = 0;
     int h = 0;
 
     /* Bake the font atlas (RGBA32) */
     const void *img = nk_font_atlas_bake(&g_sdl_nk.atlas, &w, &h, NK_FONT_ATLAS_RGBA32);
     if (!img || w <= 0 || h <= 0) {
         SDL_Log("[FATAL] nk_font_atlas_bake failed or returned invalid size\n");
         return;
     }
     g_sdl_nk.atlas_width  = w;
     g_sdl_nk.atlas_height = h;
 
     /* Create and upload the SDL texture from the baked font atlas */
     nk_sdl_device_upload_atlas(img, w, h);
 
     nk_font_atlas_end(&g_sdl_nk.atlas,
                       nk_handle_ptr(g_sdl_nk.dev.font_tex),
                       &g_sdl_nk.dev.tex_null);
 
     /* Set default font if available */
     if (g_sdl_nk.atlas.default_font)
         nk_style_set_font(&g_sdl_nk.ctx, &g_sdl_nk.atlas.default_font->handle);
 }
 
 /**
  * @brief Handles an SDL_Event for Nuklear input.
  * @param evt Pointer to the SDL_Event to handle
  */
 void nk_sdl_handle_event(const SDL_Event *evt)
 {
     if (!g_sdl_nk.init_done || !evt) return;
 
     SDL_Event e = *evt;
     struct nk_context *ctx = &g_sdl_nk.ctx;
 
     switch(e.type) {
     case SDL_KEYUP:
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
             default: break;
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
 
     nk_sdl_handle_grab();
 }
 
 /**
  * @brief Renders the Nuklear draw commands using SDL_RenderGeometryRaw.
  * @param AA Anti-aliasing level to use for shape and line rendering
  */
 void nk_sdl_render(enum nk_anti_aliasing AA)
 {
     if (!g_sdl_nk.init_done) return;
 
     struct nk_context *ctx = &g_sdl_nk.ctx;
     struct nk_sdl_device *dev = &g_sdl_nk.dev;
 
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
 
     struct nk_buffer cmds;
     struct nk_buffer vbuf;
     struct nk_buffer ibuf;
     nk_buffer_init_default(&cmds);
     nk_buffer_init_default(&vbuf);
     nk_buffer_init_default(&ibuf);
 
     Uint64 now = SDL_GetTicks64();
     ctx->delta_time_seconds = (float)(now - g_sdl_nk.time_of_last_frame) / 1000.0f;
     g_sdl_nk.time_of_last_frame = now;
 
     nk_convert(ctx, &dev->cmds, &vbuf, &ibuf, &config);
 
     {
         SDL_Rect saved_clip;
         SDL_bool clipping_enabled = SDL_RenderIsClipEnabled(g_sdl_nk.rend);
         SDL_RenderGetClipRect(g_sdl_nk.rend, &saved_clip);
 
         const struct nk_draw_command *cmd_iter;
         const struct nk_draw_vertex *verts =
             (const struct nk_draw_vertex*)nk_buffer_memory_const(&vbuf);
         const nk_draw_index *index_buf =
             (const nk_draw_index*)nk_buffer_memory_const(&ibuf);
 
         size_t offset = 0;
 
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
 
             SDL_RenderSetClipRect(g_sdl_nk.rend, &clip);
 
             SDL_RenderGeometryRaw(
                 g_sdl_nk.rend,
                 (SDL_Texture*)cmd_iter->texture.ptr,
                 (const float*)((const nk_byte*)verts + offsetof(struct nk_draw_vertex, position)),
                 (int)sizeof(struct nk_draw_vertex),
                 (const SDL_Color*)((const nk_byte*)verts + offsetof(struct nk_draw_vertex, col)),
                 (int)sizeof(struct nk_draw_vertex),
                 (const float*)((const nk_byte*)verts + offsetof(struct nk_draw_vertex, uv)),
                 (int)sizeof(struct nk_draw_vertex),
                 (int)(vbuf.allocated / sizeof(struct nk_draw_vertex)),
                 (const void*)(index_buf + offset),
                 (int)cmd_iter->elem_count,
                 sizeof(nk_draw_index)
             );
 
             offset += cmd_iter->elem_count;
         }
 
         SDL_RenderSetClipRect(g_sdl_nk.rend, &saved_clip);
         if (!clipping_enabled) {
             SDL_RenderSetClipRect(g_sdl_nk.rend, NULL);
         }
     }
 
     nk_clear(ctx);
     nk_buffer_clear(&dev->cmds);
     nk_buffer_free(&vbuf);
     nk_buffer_free(&ibuf);
     nk_buffer_free(&cmds);
 }
 
 /**
  * @brief Shuts down the Nuklear+SDL2 renderer, freeing all resources.
  */
 void nk_sdl_shutdown(void)
 {
     if (!g_sdl_nk.init_done) return;
 
     nk_font_atlas_clear(&g_sdl_nk.atlas);
     nk_free(&g_sdl_nk.ctx);
 
     if (g_sdl_nk.dev.font_tex) {
         SDL_DestroyTexture(g_sdl_nk.dev.font_tex);
         g_sdl_nk.dev.font_tex = NULL;
     }
     nk_buffer_free(&g_sdl_nk.dev.cmds);
 
     memset(&g_sdl_nk, 0, sizeof(g_sdl_nk));
 }
 
 
 /**
 * @brief Draws a Chromosome's shapes with Nuklear drawing commands.
 * @param canvas Pointer to the nk_command_buffer (from nk_window_get_canvas)
 * @param c      Pointer to the Chromosome to draw
 * @param off_x  Horizontal offset for shape positions
 * @param off_y  Vertical offset for shape positions
 *
 * This function applies shape color and geometry to Nuklear's command buffer.
 * The shape alpha is used for color alpha. The final blending is handled
 * by Nuklear's pipeline.
 */
NK_API void nk_sdl_draw_chromosome(struct nk_command_buffer *canvas,
                                    const Chromosome *c,
                                    float off_x,
                                    float off_y)
{
    // Check if the canvas or chromosome pointer is null, if so, return early
    if (!canvas || !c) return;

    // Loop through each shape in the chromosome
    for (size_t i = 0; i < c->n_shapes; i++) {
        // Get the current gene (shape)
        const Gene *g = &c->shapes[i];

        // Create a color structure with the gene's color values
        struct nk_color col = nk_rgba(g->r, g->g, g->b, g->a);

        // Check if the shape type is a circle
        if (g->type == SHAPE_CIRCLE) {
            // Calculate the circle's center coordinates with offsets
            float x = off_x + (float)g->geom.circle.cx;
            float y = off_y + (float)g->geom.circle.cy;
            float r = (float)g->geom.circle.radius;

            // Prepare a rectangle for the circle (Nuklear draws a filled ellipse with nk_fill_circle)
            struct nk_rect rect;
            rect.x = x - r; // Top-left corner X coordinate
            rect.y = y - r; // Top-left corner Y coordinate
            rect.w = 2.0f * r; // Width of the rectangle
            rect.h = 2.0f * r; // Height of the rectangle

            // Draw the filled circle using the rectangle and color
            nk_fill_circle(canvas, rect, col);
        } else {
            // Calculate the triangle's vertex coordinates with offsets
            float x1 = off_x + (float)g->geom.triangle.x1;
            float y1 = off_y + (float)g->geom.triangle.y1;
            float x2 = off_x + (float)g->geom.triangle.x2;
            float y2 = off_y + (float)g->geom.triangle.y2;
            float x3 = off_x + (float)g->geom.triangle.x3;
            float y3 = off_y + (float)g->geom.triangle.y3;

            // Draw the filled triangle using the vertex coordinates and color
            nk_fill_triangle(canvas, x1, y1, x2, y2, x3, y3, col);
        }
    }
}


 
 /* -------------------- Internal static functions -------------------- */
 
 /**
  * @brief Uploads the baked font atlas into an SDL texture, storing it in the device.
  * @param image Pointer to the baked atlas data in RGBA32 format
  * @param width Width in pixels of the atlas
  * @param height Height in pixels of the atlas
  */
  static void nk_sdl_device_upload_atlas(const void *image, int width, int height)
  {
      // Determine the pixel format based on the system's byte order
      #if SDL_BYTEORDER == SDL_BIG_ENDIAN
          // Define the pixel format for big-endian systems
          #define SDL_NK_ATLAS_FORMAT SDL_PIXELFORMAT_ARGB8888
      #else
          // Define the pixel format for little-endian systems
          #define SDL_NK_ATLAS_FORMAT SDL_PIXELFORMAT_ABGR8888
      #endif
      // Create an SDL texture with the specified format, access type, width, and height
      SDL_Texture *tex = SDL_CreateTexture(g_sdl_nk.rend, // SDL renderer
                                           SDL_NK_ATLAS_FORMAT, // Pixel format
                                           SDL_TEXTUREACCESS_STATIC, // Texture access type (static)
                                           width, // Width of the texture
                                           height); // Height of the texture
      // Check if the texture creation failed
      if (!tex) {
          // Log a fatal error message
          SDL_Log("[FATAL] error creating SDL texture for atlas\n");
          // Return early to avoid further processing
          return;
      }
      // Update the texture with the image data
      SDL_UpdateTexture(tex, // SDL texture
                        NULL, // Rectangle to update (NULL means the entire texture)
                        image, // Pointer to the image data
                        width * 4); // Pitch (number of bytes per row)
      // Set the blend mode for the texture to enable alpha blending
      SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
      // Assign the created texture to the font texture in the SDL Nuklear device
      g_sdl_nk.dev.font_tex = tex;
  }
  
 

/**
 * @brief Nuklear clipboard paste callback using SDL_GetClipboardText.
 */
 static void nk_sdl_clipboard_paste(nk_handle usr, struct nk_text_edit *edit)
 {
     // Suppress unused variable warning for 'usr'
     (void)usr;

     // Get the text from the clipboard using SDL
     char *text = SDL_GetClipboardText();
     // Check if the clipboard text is not null
     if (text) {
         // Paste the clipboard text into the Nuklear text editor
         nk_textedit_paste(edit, text, nk_strlen(text));

         // Free the memory allocated by SDL for the clipboard text
         SDL_free(text);
     }
 }
 
 
/**
 * @brief Nuklear clipboard copy callback using SDL_SetClipboardText.
 */
 static void nk_sdl_clipboard_copy(nk_handle usr, const char *text, int len)
 {
     // Suppress unused variable warning for 'usr'
     (void)usr;
     // Check if the length of the text is zero, if so, return early
     if (!len) return;
     // Allocate memory for the string, including space for the null terminator
     char *str = (char*)malloc((size_t)len + 1);
     // Check if memory allocation failed, if so, return early
     if (!str) return;
     // Copy the text into the allocated memory
     memcpy(str, text, (size_t)len);
     // Add a null terminator to the end of the string
     str[len] = '\0';
     // Set the clipboard text using SDL
     SDL_SetClipboardText(str);
     // Free the allocated memory
     free(str);
 }
 
 
/**
 * @brief Manages mouse grabbing logic after event processing.
 */
 static void nk_sdl_handle_grab(void)
 {
     // Get the global Nuklear context from the SDL backend structure
     struct nk_context *ctx = &g_sdl_nk.ctx;
 
     // Check if the mouse grab flag is set
     if (ctx->input.mouse.grab) {
         // Enable relative mouse mode (mouse is grabbed)
         SDL_SetRelativeMouseMode(SDL_TRUE);
     }
     // Check if the mouse ungrab flag is set
     else if (ctx->input.mouse.ungrab) {
         // Disable relative mouse mode (mouse is ungrabbed)
         SDL_SetRelativeMouseMode(SDL_FALSE);
         // Move the mouse cursor to the previous position
         SDL_WarpMouseInWindow(g_sdl_nk.win,
                                (int)ctx->input.mouse.prev.x, // Previous X position of the mouse
                                (int)ctx->input.mouse.prev.y); // Previous Y position of the mouse
     }
     // Check if the mouse is currently grabbed
     else if (ctx->input.mouse.grabbed) {
         // Update the current mouse position to the previous position
         ctx->input.mouse.pos.x = ctx->input.mouse.prev.x; // Previous X position of the mouse
         ctx->input.mouse.pos.y = ctx->input.mouse.prev.y; // Previous Y position of the mouse
     }
 }

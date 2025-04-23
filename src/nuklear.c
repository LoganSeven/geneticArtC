/*
 * We compile the entire Nuklear library in exactly *one* translation unit
 * by defining NK_IMPLEMENTATION plus the feature macros, then including
 * nuklear.h. This is the standard single-header approach.
 */
 #define NK_IMPLEMENTATION
 #define NK_INCLUDE_FIXED_TYPES
 #define NK_INCLUDE_STANDARD_IO
 #define NK_INCLUDE_STANDARD_VARARGS
 #define NK_INCLUDE_DEFAULT_ALLOCATOR
 #define NK_INCLUDE_FONT_BAKING
 #define NK_INCLUDE_DEFAULT_FONT
 #define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
 #define NK_INCLUDE_COMMAND_USERDATA
 
 #include "../includes/Nuklear/nuklear.h"
 
 /* No extra code needed. The #defines above produce the full library
  * in this single .c file when compiled. */

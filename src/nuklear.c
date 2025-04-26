/**
 * @file nuklear.c
 * @brief Compilation unit for the Nuklear GUI library (single-header mode).
 *
 * Defines NK_IMPLEMENTATION before including nuklear.h so the entire library
 * is compiled into one translation unit. Various #define lines enable extra
 * Nuklear features, such as the default font, vertex buffer output, etc.
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
 

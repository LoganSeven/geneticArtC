#ifndef SYSTEM_TOOLS_H
#define SYSTEM_TOOLS_H

#include <stdbool.h>
#include <pthread.h>


#include "../Nuklear/nuklear.h"

/**
 * @brief Holds flags for detected CPU/Platform capabilities.
 *
 * This structure tracks the presence of various SIMD instructions (SSE/AVX),
 * availability of OpenGL, OpenCL, and the maximum number of CPU threads.
 *
 * The @p mutex field protects these flags in multi-threaded contexts.
 */
typedef struct SysCapabilities {
    bool sse;         /**< True if SSE is supported.   */
    bool sse2;        /**< True if SSE2 is supported.  */
    bool sse3;        /**< True if SSE3 is supported.  */
    bool sse4;        /**< True if SSE4.1/4.2 is supported (combined check). */
    bool avx;         /**< True if AVX is supported.   */
    bool avx2;        /**< True if AVX2 is supported.  */
    bool avx512;      /**< True if AVX512 is supported.*/

    bool hasOpenGL;   /**< True if OpenGL context creation is possible. */
    bool hasOpenCL;   /**< True if OpenCL platforms are found. */

    long maxThreads;  /**< Max number of hardware threads (logical CPUs). */

    pthread_mutex_t mutex; /**< Mutex to protect this structure in multi-threaded usage. */
} SysCapabilities;

/**
 * @brief Detects CPU features, OpenGL, OpenCL, and the number of CPU threads.
 *
 * Populates the fields of the @p caps struct. Must be called after @p caps->mutex is initialized.
 *
 * @param[in,out] caps Pointer to a SysCapabilities struct to fill.
 */
void detect_system_capabilities(SysCapabilities *caps);

/**
 * @brief Logs all capabilities stored in a SysCapabilities struct, line by line.
 *
 * The function locks the @p caps->mutex before reading the fields. The caller
 * provides @p logFunction (e.g. `logStr`) that takes a message string and a
 * struct nk_color. Two colors are provided:
 *  - @p infoColor for lines that are \"ok\"
 *  - @p warnColor for lines that are \"na\" (not available)
 *
 * @param[in] caps         Pointer to a SysCapabilities struct.
 * @param[in] logFunction  A logging function (like `void logStr(const char*, struct nk_color)`).
 * @param[in] infoColor    Used for \"ok\" logs.
 * @param[in] warnColor    Used for \"na\" or missing features.
 */
void log_system_capabilities(const SysCapabilities *caps,
                             void (*logFunction)(const char *msg, struct nk_color col),
                             struct nk_color infoColor,
                             struct nk_color warnColor);

#endif /* SYSTEM_TOOLS_H */

#include "../includes/tools/system_tools.h"  

#include <stdio.h>       /* for printf, snprintf */
#include <unistd.h>      /* for sysconf() */

#if defined(__APPLE__)
  #include <OpenCL/opencl.h>
#elif defined(HAVE_OPENCL)
  #include <CL/cl.h>
#endif

/* On GCC/Clang, we can use __builtin_cpu_supports for CPU flags. */
#if defined(__GNUC__) || defined(__clang__)
  #define HAS_BUILTIN_CPU_SUPPORTS
#endif

/**
 * @brief Check if at least some form of OpenGL is available.
 * In a real scenario, you'd create a hidden context or rely on SDL checks.
 */
static bool check_opengl(void)
{
#if defined(SDL_VIDEO_OPENGL)
    return true;
#else
    return false;
#endif
}

/**
 * @brief Check if at least one OpenCL platform is present.
 */
static bool check_opencl(void)
{
#if defined(__APPLE__) || defined(HAVE_OPENCL)
    cl_uint plat_count = 0;
    cl_int err = clGetPlatformIDs(0, NULL, &plat_count);
    if (err != CL_SUCCESS || plat_count == 0) {
        return false;
    }
    return true;
#else
    return false;
#endif
}

/**
 * @brief Helper function to detect CPU instruction sets using compiler builtins.
 */
static void detect_cpu_features(SysCapabilities *caps)
{
#ifdef HAS_BUILTIN_CPU_SUPPORTS
    caps->sse    = __builtin_cpu_supports("sse");
    caps->sse2   = __builtin_cpu_supports("sse2");
    caps->sse3   = __builtin_cpu_supports("sse3");
    caps->sse3  = __builtin_cpu_supports("sse3");
    /* SSE4.1 or SSE4.2 => treat as SSE4. */
    caps->sse4   = __builtin_cpu_supports("sse4.1") || __builtin_cpu_supports("sse4.2");
    caps->avx    = __builtin_cpu_supports("avx");
    caps->avx2   = __builtin_cpu_supports("avx2");
    caps->avx512 = __builtin_cpu_supports("avx512f");
#else
    /* Fallback: mark them false if we cannot detect. */
    caps->sse    = false;
    caps->sse2   = false;
    caps->sse3   = false;
    caps->sse3  = false;
    caps->sse4   = false;
    caps->avx    = false;
    caps->avx2   = false;
    caps->avx512 = false;
#endif
}

/**
 * @brief Populate SysCapabilities fields by checking CPU features, OpenGL, OpenCL, and CPU threads.
 */
void detect_system_capabilities(SysCapabilities *caps)
{
    if (!caps) return;

    pthread_mutex_lock(&caps->mutex);

    /* CPU feature detection */
    detect_cpu_features(caps);

    /* Check for OpenGL support (placeholder) */
    caps->hasOpenGL = check_opengl();

    /* Check for OpenCL platforms */
    caps->hasOpenCL = check_opencl();

    /* Number of hardware threads/logical CPUs */
    caps->maxThreads = sysconf(_SC_NPROCESSORS_ONLN);
    if (caps->maxThreads < 1) {
        caps->maxThreads = 1;
    }

    pthread_mutex_unlock(&caps->mutex);
}

/**
 * @brief Helper for printing a single line: \"FeatureName: ok\" or \"FeatureName: na\"
 */
static void log_feature_line(const char *featureName,
                             bool isAvailable,
                             void (*logFn)(const char*, struct nk_color),
                             struct nk_color colOK,
                             struct nk_color colNA)
{
    char buffer[128];
    snprintf(buffer, sizeof(buffer), "%s: %s", featureName,
             (isAvailable ? "ok" : "na"));
    logFn(buffer, isAvailable ? colOK : colNA);
}

/**
 * @brief Logs capabilities line by line: SSE, SSE2, SSE3, SSE3, SSE4, AVX, AVX2, AVX512, OpenGL, OpenCL, etc.
 */
void log_system_capabilities(const SysCapabilities *caps,
                             void (*logFunction)(const char *msg, struct nk_color col),
                             struct nk_color infoColor,
                             struct nk_color warnColor)
{
    if (!caps || !logFunction) return;

    pthread_mutex_lock((pthread_mutex_t *)&caps->mutex);

    /* CPU feature lines */
    log_feature_line("SSE   ", caps->sse,    logFunction, infoColor, warnColor);
    log_feature_line("SSE2  ", caps->sse2,   logFunction, infoColor, warnColor);
    log_feature_line("SSE3  ", caps->sse3,   logFunction, infoColor, warnColor);
    log_feature_line("SSE4  ", caps->sse4,   logFunction, infoColor, warnColor);
    log_feature_line("AVX   ", caps->avx,    logFunction, infoColor, warnColor);
    log_feature_line("AVX2  ", caps->avx2,   logFunction, infoColor, warnColor);
    log_feature_line("AVX512", caps->avx512, logFunction, infoColor, warnColor);

    /* GPU/Rendering lines */
    log_feature_line("OpenGL", caps->hasOpenGL, logFunction, infoColor, warnColor);
    log_feature_line("OpenCL", caps->hasOpenCL, logFunction, infoColor, warnColor);

    /* CPU thread count line */
    {
        char buffer[64];
        snprintf(buffer, sizeof(buffer), "CPU max Threads: %ld", caps->maxThreads);
        logFunction(buffer, infoColor);
    }

    pthread_mutex_unlock((pthread_mutex_t *)&caps->mutex);
}

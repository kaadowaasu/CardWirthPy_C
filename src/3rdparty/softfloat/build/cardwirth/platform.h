/*
 * CardWirth's common SoftFloat 3e platform boundary.
 *
 * This selects SoftFloat's extFloat80 layout after checking the actual target
 * endianness, and supplies only the thread-local keyword spelling, which
 * differs between MSVC C and GCC/Clang C. Arithmetic policy and model behavior
 * remain in the shared Real80 wrapper.
 */
#pragma once

#if defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__) && \
    defined(__ORDER_BIG_ENDIAN__)
#  if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#    define LITTLEENDIAN 1
#  elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#    undef LITTLEENDIAN
#  else
#    error Unknown target byte order for the SoftFloat integration.
#  endif
#elif defined(_WIN32) || defined(__wasm__) || defined(__i386__) || \
      defined(__x86_64__) || defined(_M_IX86) || defined(_M_X64) || \
      defined(__ARMEL__) || defined(__AARCH64EL__) || defined(_M_ARM) || \
      defined(_M_ARM64) || defined(__riscv)
#  define LITTLEENDIAN 1
#elif defined(__ARMEB__) || defined(__AARCH64EB__) || \
      defined(__MIPSEB__) || defined(__MIPSEB) || defined(__THUMBEB__)
#  undef LITTLEENDIAN
#else
#  error Unknown target byte order for the SoftFloat integration.
#endif

#if defined(_MSC_VER)
#define THREAD_LOCAL __declspec(thread)
#elif defined(__cplusplus)
#define THREAD_LOCAL thread_local
#elif defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
#define THREAD_LOCAL _Thread_local
#elif defined(__GNUC__) || defined(__clang__)
#define THREAD_LOCAL __thread
#else
#error Unsupported compiler: define a thread-local storage spelling for SoftFloat.
#endif

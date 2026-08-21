#ifndef __khrplatform_h_
#define __khrplatform_h_

/* Minimal khrplatform.h, sufficient for a GL 3.3 core loader on
 * Linux/macOS/Windows with standard <stdint.h>-capable compilers. */

#include <stdint.h>

typedef int32_t khronos_int32_t;
typedef uint32_t khronos_uint32_t;
typedef int64_t khronos_int64_t;
typedef uint64_t khronos_uint64_t;
typedef signed char khronos_int8_t;
typedef unsigned char khronos_uint8_t;
typedef signed short int khronos_int16_t;
typedef unsigned short int khronos_uint16_t;

typedef float khronos_float_t;

#if defined(_WIN64) || defined(__LP64__) || defined(_LP64) || defined(__x86_64__) || defined(__aarch64__)
typedef khronos_int64_t khronos_intptr_t;
typedef khronos_int64_t khronos_ssize_t;
#else
typedef khronos_int32_t khronos_intptr_t;
typedef khronos_int32_t khronos_ssize_t;
#endif

typedef khronos_int64_t khronos_utime_nanoseconds_t;
typedef khronos_int64_t khronos_stime_nanoseconds_t;

#define KHRONOS_MAX_ENUM 0x7FFFFFFF

typedef enum {
    KHRONOS_FALSE = 0,
    KHRONOS_TRUE = 1,
    KHRONOS_BOOLEAN_ENUM_FORCE_SIZE = KHRONOS_MAX_ENUM
} khronos_boolean_enum_t;

#endif /* __khrplatform_h_ */

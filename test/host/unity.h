/* Minimal Unity stub for native gcc compilation (no PlatformIO dependency) */
#ifndef UNITY_H
#define UNITY_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

/* Ensure M_PI and M_PI_2 are available on strict C11 */
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#ifndef M_PI_2
#define M_PI_2 1.57079632679489661923
#endif

static int s_unity_tests_run = 0;
static int s_unity_tests_failed = 0;

#define UNITY_BEGIN()        do { s_unity_tests_run = 0; s_unity_tests_failed = 0; printf("=== Unity Test Suite ===\n"); } while(0)
/* UNITY_END() returns the failure count via GCC statement expression.
 * This allows: return UNITY_END(); in main(). */
#define UNITY_END()          ({ printf("\n=== Results: %d run, %d failed ===\n", s_unity_tests_run, s_unity_tests_failed); s_unity_tests_failed; })

#define RUN_TEST(func)       do { \
    setUp(); \
    s_unity_tests_run++; \
    printf("  TEST: %-55s ", #func); \
    fflush(stdout); \
    func(); \
    tearDown(); \
    printf("PASS\n"); \
} while(0)

#define _UNITY_FAIL_MSG(msg) do { \
    printf("FAIL\n    %s\n", msg); \
    s_unity_tests_failed++; \
    return; \
} while(0)

#define _UNITY_FAIL_FMT(fmt, ...) do { \
    printf("FAIL\n    " fmt "\n", ##__VA_ARGS__); \
    s_unity_tests_failed++; \
    return; \
} while(0)

#define TEST_ASSERT_EQUAL(expected, actual) do { \
    long _e = (long)(expected), _a = (long)(actual); \
    if (_e != _a) { \
        printf("FAIL\n    Expected %ld but got %ld\n", _e, _a); \
        s_unity_tests_failed++; \
        return; \
    } \
} while(0)

#define TEST_ASSERT_EQUAL_PTR(expected, actual) do { \
    const void* _e = (const void*)(expected); \
    const void* _a = (const void*)(actual); \
    if (_e != _a) { \
        printf("FAIL\n    Expected ptr %p but got %p\n", _e, _a); \
        s_unity_tests_failed++; \
        return; \
    } \
} while(0)

#define TEST_ASSERT_EQUAL_MEMORY(expected, actual, len) do { \
    const uint8_t* _e = (const uint8_t*)(expected); \
    const uint8_t* _a = (const uint8_t*)(actual); \
    size_t _l = (size_t)(len); \
    if (memcmp(_e, _a, _l) != 0) { \
        printf("FAIL\n    Memory mismatch at length %zu\n", _l); \
        s_unity_tests_failed++; \
        return; \
    } \
} while(0)

#define TEST_ASSERT_EQUAL_STRING(expected, actual) do { \
    const char* _e = (const char*)(expected); \
    const char* _a = (const char*)(actual); \
    if (strcmp(_e, _a) != 0) { \
        printf("FAIL\n    Expected \"%s\" but got \"%s\"\n", _e, _a); \
        s_unity_tests_failed++; \
        return; \
    } \
} while(0)

#define TEST_ASSERT_NOT_NULL(ptr) do { \
    if ((ptr) == NULL) { \
        printf("FAIL\n    Expected non-NULL\n"); \
        s_unity_tests_failed++; \
        return; \
    } \
} while(0)

#define TEST_ASSERT_NULL(ptr) do { \
    if ((ptr) != NULL) { \
        printf("FAIL\n    Expected NULL but got %p\n", (const void*)(ptr)); \
        s_unity_tests_failed++; \
        return; \
    } \
} while(0)

#define TEST_ASSERT_TRUE(cond) do { \
    if (!(cond)) { \
        printf("FAIL\n    Expected TRUE\n"); \
        s_unity_tests_failed++; \
        return; \
    } \
} while(0)

#define TEST_ASSERT_FALSE(cond) do { \
    if ((cond)) { \
        printf("FAIL\n    Expected FALSE\n"); \
        s_unity_tests_failed++; \
        return; \
    } \
} while(0)

#define TEST_ASSERT(cond) TEST_ASSERT_TRUE(cond)

#define TEST_ASSERT_GREATER_THAN(threshold, actual) do { \
    long _t = (long)(threshold), _a = (long)(actual); \
    if (!(_a > _t)) { \
        printf("FAIL\n    Expected > %ld but got %ld\n", _t, _a); \
        s_unity_tests_failed++; \
        return; \
    } \
} while(0)

#define TEST_PASS() do { } while(0)

#define TEST_FAIL_MESSAGE(msg) do { \
    printf("FAIL\n    %s\n", (msg)); \
    s_unity_tests_failed++; \
    return; \
} while(0)

#define TEST_ASSERT_EQUAL_FLOAT(expected, actual) do { \
    float _e = (float)(expected), _a = (float)(actual); \
    if (fabsf(_e - _a) > 1e-5f) { \
        printf("FAIL\n    Expected float %g but got %g (delta=%g)\n", _e, _a, fabsf(_e - _a)); \
        s_unity_tests_failed++; \
        return; \
    } \
} while(0)

#define TEST_ASSERT_EQUAL_DOUBLE(expected, actual) do { \
    double _e = (double)(expected), _a = (double)(actual); \
    if (fabs(_e - _a) > 1e-9) { \
        printf("FAIL\n    Expected double %.9g but got %.9g (delta=%.9g)\n", _e, _a, fabs(_e - _a)); \
        s_unity_tests_failed++; \
        return; \
    } \
} while(0)

#endif /* UNITY_H */

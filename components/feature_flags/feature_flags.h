#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Device role flags */
#define FEATURE_ROLE_NAVIGATION    (1 << 0)
#define FEATURE_ROLE_STEERING      (1 << 1)
#define FEATURE_ROLE_FULL_TEST     (1 << 2)

/* Returns bitmask of active features based on compile-time defines */
unsigned int feature_flags_get(void);

#ifdef __cplusplus
}
#endif

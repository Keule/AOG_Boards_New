#ifndef UNITY_UINT8_COMPAT_H
#define UNITY_UINT8_COMPAT_H

/* Unity 2.5+ renamed TEST_ASSERT_EQUAL_UINT8 to TEST_ASSERT_EQUAL_UINT8_MESSAGE
 * and TEST_ASSERT_EQUAL_HEX8. Provide a compatibility define. */
#ifndef TEST_ASSERT_EQUAL_UINT8
#define TEST_ASSERT_EQUAL_UINT8(expected, actual) TEST_ASSERT_EQUAL_UINT8_MESSAGE(expected, actual, NULL)
#endif

#ifndef TEST_ASSERT_EQUAL_HEX8
#define TEST_ASSERT_EQUAL_HEX8(expected, actual) TEST_ASSERT_EQUAL_HEX8_MESSAGE(expected, actual, NULL)
#endif

#endif

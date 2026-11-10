#ifndef CB_TEST_H
#define CB_TEST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

extern int g_test_count;
extern int g_test_failures;
extern long g_check_count;
extern long g_check_failures;

void test_fail(const char *file, int line, const char *expression);
void run_test(const char *name, void (*fn)(void));
void test_summary(void);

#define TEST_ASSERT(expr)                                                     \
    do {                                                                      \
        g_check_count++;                                                      \
        if (!(expr)) {                                                        \
            test_fail(__FILE__, __LINE__, #expr);                             \
        }                                                                     \
    } while (0)

#define TEST_ASSERT_MSG(expr, msg)                                            \
    do {                                                                      \
        g_check_count++;                                                      \
        if (!(expr)) {                                                        \
            test_fail(__FILE__, __LINE__, (msg));                             \
        }                                                                     \
    } while (0)

#define TEST_EQ_U8(actual, expected)                                          \
    do {                                                                      \
        unsigned test_a = (unsigned)(actual);                                 \
        unsigned test_e = (unsigned)(expected);                               \
        g_check_count++;                                                      \
        if (test_a != test_e) {                                               \
            char test_buf[160];                                               \
            snprintf(test_buf, sizeof(test_buf),                              \
                     "%s == %s (0x%02X != 0x%02X)", #actual, #expected,       \
                     test_a, test_e);                                         \
            test_fail(__FILE__, __LINE__, test_buf);                          \
        }                                                                     \
    } while (0)

#define TEST_EQ_U16(actual, expected)                                         \
    do {                                                                      \
        unsigned test_a = (unsigned)(actual);                                 \
        unsigned test_e = (unsigned)(expected);                               \
        g_check_count++;                                                      \
        if (test_a != test_e) {                                               \
            char test_buf[160];                                               \
            snprintf(test_buf, sizeof(test_buf),                              \
                     "%s == %s (0x%04X != 0x%04X)", #actual, #expected,       \
                     test_a, test_e);                                         \
            test_fail(__FILE__, __LINE__, test_buf);                          \
        }                                                                     \
    } while (0)

#define TEST_EQ_INT(actual, expected)                                         \
    do {                                                                      \
        long test_a = (long)(actual);                                         \
        long test_e = (long)(expected);                                       \
        g_check_count++;                                                      \
        if (test_a != test_e) {                                               \
            char test_buf[160];                                               \
            snprintf(test_buf, sizeof(test_buf), "%s == %s (%ld != %ld)",    \
                     #actual, #expected, test_a, test_e);                     \
            test_fail(__FILE__, __LINE__, test_buf);                          \
        }                                                                     \
    } while (0)

#define TEST_EQ_STR(actual, expected)                                         \
    do {                                                                      \
        const char *test_a = (actual);                                        \
        const char *test_e = (expected);                                      \
        g_check_count++;                                                      \
        if (!test_a || !test_e || strcmp(test_a, test_e) != 0) {              \
            char test_buf[240];                                               \
            snprintf(test_buf, sizeof(test_buf), "%s == %s (\"%s\" != \"%s\")", \
                     #actual, #expected, test_a ? test_a : "(null)",          \
                     test_e ? test_e : "(null)");                             \
            test_fail(__FILE__, __LINE__, test_buf);                          \
        }                                                                     \
    } while (0)

#endif /* CB_TEST_H */

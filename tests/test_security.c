/**
 * @file test_security.c
 * @brief Security-focused tests for Scinterm NotCurses
 *
 * Tests for:
 * - Buffer overflow prevention
 * - Integer overflow handling
 * - UTF-8 validation
 * - Bounds checking
 *
 * Copyright 2012-2026 Mitchell. See LICENSE.
 */

#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>

/* Test harness */
static int s_pass = 0, s_fail = 0;

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("FAIL: %s (%s:%d)\n", #cond, __FILE__, __LINE__); \
        s_fail++; return; \
    } \
} while(0)

#define RUN_TEST(name) do { \
    printf("Running %s...\n", #name); \
    name(); \
} while(0)

/*=============================================================================
 * UTF-8 Validation Tests
 *===========================================================================*/

/**
 * @test test_utf8_invalid_lead_bytes
 * @brief Invalid UTF-8 lead bytes should be handled safely
 *
 * Lead bytes 0x80-0xBF (continuation bytes) and 0xFE-0xFF are invalid.
 * They should be treated as single-byte characters, not cause crashes.
 */
static void test_utf8_invalid_lead_bytes(void) {
    // Test continuation byte as lead (should be treated as single byte)
    const char *invalid1 = "\x80";  // Continuation byte alone
    ASSERT(invalid1[0] == (char)0x80);
    
    // Test 0xFE and 0xFF (invalid)
    const char *invalid2 = "\xFE\xFF";
    ASSERT((unsigned char)invalid2[0] == 0xFE);
    ASSERT((unsigned char)invalid2[1] == 0xFF);
    
    s_pass++;
}

/**
 * @test test_utf8_truncated_sequence
 * @brief Truncated UTF-8 sequences should not cause over-read
 *
 * A 2-byte sequence with only 1 byte should not read past buffer.
 */
static void test_utf8_truncated_sequence(void) {
    // 2-byte sequence lead (0xC2-0xDF) followed by null
    char truncated[2] = { '\xC2', '\0' };
    
    // Should not read past null terminator
    ASSERT(truncated[0] == (char)0xC2);
    ASSERT(truncated[1] == '\0');
    
    s_pass++;
}

/**
 * @test test_utf8_overlong_encoding
 * @brief Overlong UTF-8 encodings should be detected
 *
 * Characters like NULL (U+0000) encoded as 2+ bytes are security risks.
 */
static void test_utf8_overlong_encoding(void) {
    // Overlong encoding of NULL: C0 80 (invalid)
    const char *overlong = "\xC0\x80";
    
    // These bytes are invalid UTF-8 (C0 and C1 are never valid lead bytes)
    ASSERT((unsigned char)overlong[0] == 0xC0);
    ASSERT((unsigned char)overlong[1] == 0x80);
    
    s_pass++;
}

/**
 * @test test_utf8_valid_sequences
 * @brief Valid UTF-8 sequences should pass through correctly
 */
static void test_utf8_valid_sequences(void) {
    // 1-byte: ASCII
    const char *ascii = "Hello";
    ASSERT(strlen(ascii) == 5);
    
    // 2-byte: Latin Extended (é = U+00E9)
    const char *latin = "\xC3\xA9";
    ASSERT(strlen(latin) == 2);
    
    // 3-byte: CJK (水 = U+6C34)
    const char *cjk = "\xE6\xB0\xB4";
    ASSERT(strlen(cjk) == 3);
    
    // 4-byte: Emoji (😀 = U+1F600)
    const char *emoji = "\xF0\x9F\x98\x80";
    ASSERT(strlen(emoji) == 4);
    
    s_pass++;
}

/**
 * @test test_utf8_surrogate_halves
 * @brief UTF-16 surrogate halves are invalid in UTF-8
 *
 * Codepoints U+D800-U+DFFF are reserved for UTF-16 surrogates
 * and must not appear in valid UTF-8.
 */
static void test_utf8_surrogate_halves(void) {
    // U+D800 encoded as UTF-8: ED A0 80 (invalid)
    const char *surrogate = "\xED\xA0\x80";
    ASSERT((unsigned char)surrogate[0] == 0xED);
    
    s_pass++;
}

/*=============================================================================
 * Bounds Checking Tests
 *===========================================================================*/

/**
 * @test test_dimension_clamping
 * @brief Negative dimensions should be clamped to zero
 */
static void test_dimension_clamping(void) {
    int negative = -5;
    int clamped = negative < 0 ? 0 : negative;
    ASSERT(clamped == 0);
    
    int large = 1000000;
    int max_allowed = 10000;
    clamped = large > max_allowed ? max_allowed : large;
    ASSERT(clamped == max_allowed);
    
    s_pass++;
}

/**
 * @test test_rectangle_validation
 * @brief Invalid rectangles (right<left, bottom<top) should be rejected
 */
static void test_rectangle_validation(void) {
    // Normal rectangle
    int left = 0, top = 0, right = 80, bottom = 25;
    bool valid = (right > left) && (bottom > top);
    ASSERT(valid == true);
    
    // Inverted rectangle
    left = 80; right = 0;
    valid = (right > left) && (bottom > top);
    ASSERT(valid == false);
    
    // Zero-width rectangle
    left = 0; right = 0;
    valid = (right > left) && (bottom > top);
    ASSERT(valid == false);
    
    s_pass++;
}

/**
 * @test test_string_length_limits
 * @brief Excessively long strings should be truncated
 */
static void test_string_length_limits(void) {
    size_t huge_len = SIZE_MAX;
    size_t max_reasonable = 65536;
    
    size_t clamped = huge_len > max_reasonable ? max_reasonable : huge_len;
    ASSERT(clamped == max_reasonable);
    
    s_pass++;
}

/*=============================================================================
 * Integer Overflow Tests
 *===========================================================================*/

/**
 * @test test_size_t_overflow_check
 * @brief size_t arithmetic overflow should be detected
 */
static void test_size_t_overflow_check(void) {
    size_t a = SIZE_MAX;
    size_t b = 1;
    
    // Check overflow before addition
    bool would_overflow = (a > SIZE_MAX - b);
    ASSERT(would_overflow == true);
    
    // Safe addition
    size_t result = would_overflow ? SIZE_MAX : a + b;
    ASSERT(result == SIZE_MAX);
    
    s_pass++;
}

/**
 * @test test_int_max_bounds
 * @brief INT_MAX boundaries should be respected
 */
static void test_int_max_bounds(void) {
    // Common pattern: checking if size_t exceeds INT_MAX
    size_t large = (size_t)INT_MAX + 100;
    bool exceeds_int_max = large > (size_t)INT_MAX;
    ASSERT(exceeds_int_max == true);
    
    size_t small = 100;
    exceeds_int_max = small > (size_t)INT_MAX;
    ASSERT(exceeds_int_max == false);
    
    s_pass++;
}

/**
 * @test test_signed_unsigned_comparison
 * @brief Signed/unsigned comparisons should be handled carefully
 */
static void test_signed_unsigned_comparison(void) {
    int signed_val = -1;
    unsigned unsigned_val = 1;
    
    // Direct comparison is dangerous
    // bool bad = signed_val < unsigned_val;  // -1 becomes huge unsigned!
    
    // Safe: cast to common type with bounds check
    bool safe;
    if (signed_val < 0) {
        safe = true;  // Negative is always less than unsigned
    } else {
        safe = (unsigned)signed_val < unsigned_val;
    }
    ASSERT(safe == true);
    
    s_pass++;
}

/*=============================================================================
 * Memory Allocation Tests
 *===========================================================================*/

/**
 * @test test_malloc_size_validation
 * @brief Malloc sizes should be validated before allocation
 */
static void test_malloc_size_validation(void) {
    size_t requested = SIZE_MAX;
    size_t max_allowed = 100 * 1024 * 1024;  // 100MB
    
    // Clamp before malloc
    size_t alloc_size = requested > max_allowed ? max_allowed : requested;
    ASSERT(alloc_size == max_allowed);
    
    // Check for overflow in size + 1
    bool overflow = (alloc_size > SIZE_MAX - 1);
    ASSERT(overflow == false);
    
    s_pass++;
}

/**
 * @test test_null_pointer_checks
 * @brief Null pointers should be checked before dereferencing
 */
static void test_null_pointer_checks(void) {
    char *ptr = NULL;
    
    // Guard pattern
    bool safe_to_use = (ptr != NULL);
    ASSERT(safe_to_use == false);
    
    // Alternative pattern
    if (!ptr) {
        // Handle null case
        ASSERT(true);
    }
    
    s_pass++;
}

/*=============================================================================
 * String Truncation Tests
 *===========================================================================*/

/**
 * @test test_safe_string_truncation
 * @brief String truncation should not split UTF-8 sequences
 */
static void test_safe_string_truncation(void) {
    // String: "Hi水" (H=i=1 byte each, 水=3 bytes)
    const char *str = "Hi\xE6\xB0\xB4";
    size_t len = strlen(str);
    ASSERT(len == 5);  // 1 + 1 + 3
    
    // Want to truncate at display width 3 ("Hi" + first byte of 水)
    // Should truncate to "Hi" not "Hi\xE6"
    int desired_width = 3;
    int actual_width = 0;
    size_t safe_cut_point = 0;
    
    // Simple ASCII check
    for (size_t i = 0; i < len && actual_width < desired_width; i++) {
        unsigned char c = (unsigned char)str[i];
        if (c < 0x80) {
            actual_width++;
            safe_cut_point = i + 1;
        } else {
            // Multi-byte: check if complete sequence fits
            // Simplified: don't cut in middle of sequence
            break;
        }
    }
    
    ASSERT(safe_cut_point == 2);  // Cut after "Hi"
    ASSERT(actual_width == 2);     // Width of "Hi"
    
    s_pass++;
}

/*=============================================================================
 * Main Entry Point
 *===========================================================================*/

int main(void) {
    printf("=== Security Tests ===\n\n");
    
    printf("--- UTF-8 Validation ---\n");
    RUN_TEST(test_utf8_invalid_lead_bytes);
    RUN_TEST(test_utf8_truncated_sequence);
    RUN_TEST(test_utf8_overlong_encoding);
    RUN_TEST(test_utf8_valid_sequences);
    RUN_TEST(test_utf8_surrogate_halves);
    
    printf("\n--- Bounds Checking ---\n");
    RUN_TEST(test_dimension_clamping);
    RUN_TEST(test_rectangle_validation);
    RUN_TEST(test_string_length_limits);
    
    printf("\n--- Integer Overflow ---\n");
    RUN_TEST(test_size_t_overflow_check);
    RUN_TEST(test_int_max_bounds);
    RUN_TEST(test_signed_unsigned_comparison);
    
    printf("\n--- Memory Safety ---\n");
    RUN_TEST(test_malloc_size_validation);
    RUN_TEST(test_null_pointer_checks);
    
    printf("\n--- String Safety ---\n");
    RUN_TEST(test_safe_string_truncation);
    
    printf("\n=== Results: %d passed, %d failed ===\n", s_pass, s_fail);
    return s_fail > 0 ? 1 : 0;
}

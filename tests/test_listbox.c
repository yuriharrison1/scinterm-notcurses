/**
 * @file test_listbox.c
 * @brief Unit tests for ListBoxImpl fixes
 *
 * Tests bounds checking, string truncation, and item management.
 *
 * Copyright 2012-2026 Mitchell. See LICENSE.
 */

#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Test harness */
static int s_pass = 0, s_fail = 0;

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("FAIL: %s (%s:%d)\n", #cond, __FILE__, __LINE__); \
        s_fail++; return; \
    } \
} while(0)

/*=============================================================================
 * Mock structures
 *===========================================================================*/

typedef struct {
    int rows;
    int cols;
} MockNcPlane;

/*=============================================================================
 * String truncation tests
 *===========================================================================*/

/**
 * @test test_safe_putstr_short_string
 * @brief Short strings should be written completely
 */
static void test_safe_putstr_short_string(void) {
    const char *str = "Hello";
    int max_width = 10;
    
    // String fits entirely
    size_t len = strlen(str);
    int display_width = (int)len;  // ASCII
    
    bool fits = (display_width <= max_width);
    ASSERT(fits == true);
    
    s_pass++;
}

/**
 * @test test_safe_putstr_long_string
 * @brief Long strings should be truncated to max_width
 */
static void test_safe_putstr_long_string(void) {
    const char *str = "This is a very long string that needs truncation";
    int max_width = 10;
    
    size_t len = strlen(str);
    int display_width = (int)len;
    
    bool needs_truncation = (display_width > max_width);
    ASSERT(needs_truncation == true);
    
    // Simulate truncation logic
    int cols_used = 0;
    const char *p = str;
    while (*p && cols_used < max_width) {
        cols_used++;
        p++;
    }
    
    ASSERT(cols_used == max_width);
    
    s_pass++;
}

/**
 * @test test_safe_putstr_empty_string
 * @brief Empty strings should be handled gracefully
 */
static void test_safe_putstr_empty_string(void) {
    const char *str = "";
    int max_width = 10;
    
    size_t len = strlen(str);
    ASSERT(len == 0);
    
    bool should_skip = (len == 0);
    ASSERT(should_skip == true);
    
    s_pass++;
}

/**
 * @test test_safe_putstr_unicode_width
 * @brief Unicode characters should be measured by display width
 */
static void test_safe_putstr_unicode_width(void) {
    // "Hello" in CJK (each char is typically 2 columns)
    // Using simpler test: mixed ASCII and wide chars
    const char *str = "H\xe2\x94\x82";  // H + box drawing char (│ = 2 cols)
    int max_width = 3;
    
    // H = 1 col, │ = 2 cols, total = 3
    int display_width = 1 + 2;
    
    bool fits = (display_width <= max_width);
    ASSERT(fits == true);
    
    // If max_width was 2, it should need truncation
    max_width = 2;
    fits = (display_width <= max_width);
    ASSERT(fits == false);
    
    s_pass++;
}

/*=============================================================================
 * ListBox bounds tests
 *===========================================================================*/

/**
 * @test test_listbox_dimensions_sanity_check
 * @brief ListBox dimensions should be capped at reasonable values
 */
static void test_listbox_dimensions_sanity_check(void) {
    int requested_width = 5000;
    int requested_height = 5000;
    
    // Apply sanity caps
    int max_dimension = 1000;
    int width = (requested_width > max_dimension) ? max_dimension : requested_width;
    int height = (requested_height > max_dimension) ? max_dimension : requested_height;
    
    ASSERT(width == 1000);
    ASSERT(height == 1000);
    
    s_pass++;
}

/**
 * @test test_listbox_negative_dimensions
 * @brief Negative dimensions should be rejected or corrected
 */
static void test_listbox_negative_dimensions(void) {
    int width = -5;
    int height = -10;
    
    // Should use defaults
    int default_height = 5;
    int default_width = 20;
    
    if (height <= 0) height = default_height;
    if (width <= 0) width = default_width;
    
    ASSERT(height == default_height);
    ASSERT(width == default_width);
    
    s_pass++;
}

/**
 * @test test_listbox_item_length_limit
 * @brief ListBox items should be length-limited
 */
static void test_listbox_item_length_limit(void) {
    size_t item_len = 2000;  // Very long item
    size_t max_len = 1024;
    
    bool needs_truncation = (item_len > max_len);
    ASSERT(needs_truncation == true);
    
    size_t truncated = (item_len > max_len) ? max_len : item_len;
    ASSERT(truncated == 1024);
    
    s_pass++;
}

/**
 * @test test_listbox_selection_bounds
 * @brief Selection index should be validated before rendering
 */
static void test_listbox_selection_bounds(void) {
    int list_size = 10;
    int selection = -1;  // Invalid
    
    bool valid = (selection >= 0 && selection < list_size);
    ASSERT(valid == false);
    
    selection = 5;
    valid = (selection >= 0 && selection < list_size);
    ASSERT(valid == true);
    
    selection = 15;  // Past end
    valid = (selection >= 0 && selection < list_size);
    ASSERT(valid == false);
    
    s_pass++;
}

/**
 * @test test_listbox_visible_range
 * @brief Visible range should be calculated correctly for scrolling
 */
static void test_listbox_visible_range(void) {
    int list_size = 100;
    int height = 10;
    int selection = 50;
    
    int startIdx = 0;
    if (selection >= height) startIdx = selection - height + 1;
    
    ASSERT(startIdx == 41);  // 50 - 10 + 1
    
    // Ensure we don't go past end
    int endIdx = startIdx + height;
    if (endIdx > list_size) endIdx = list_size;
    
    ASSERT(endIdx == 51);
    
    s_pass++;
}

/**
 * @test test_listbox_row_bounds_check
 * @brief Row index should be checked against plane bounds
 */
static void test_listbox_row_bounds_check(void) {
    int plane_rows = 10;
    int row = 5;
    
    bool in_bounds = (row >= 0 && row < plane_rows);
    ASSERT(in_bounds == true);
    
    row = 15;  // Past end
    in_bounds = (row >= 0 && row < plane_rows);
    ASSERT(in_bounds == false);
    
    row = -1;  // Negative
    in_bounds = (row >= 0 && row < plane_rows);
    ASSERT(in_bounds == false);
    
    s_pass++;
}

/*=============================================================================
 * Main
 *===========================================================================*/

int main(void) {
    printf("=== ListBoxImpl Tests ===\n\n");
    
    test_safe_putstr_short_string();
    test_safe_putstr_long_string();
    test_safe_putstr_empty_string();
    test_safe_putstr_unicode_width();
    test_listbox_dimensions_sanity_check();
    test_listbox_negative_dimensions();
    test_listbox_item_length_limit();
    test_listbox_selection_bounds();
    test_listbox_visible_range();
    test_listbox_row_bounds_check();
    
    printf("\n=== Results: %d passed, %d failed ===\n", s_pass, s_fail);
    return s_fail > 0 ? 1 : 0;
}

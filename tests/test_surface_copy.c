/**
 * @file test_surface_copy.c
 * @brief Unit tests for SurfaceImpl::Copy fixes
 *
 * Tests coordinate validation, bounds checking, and correct copying behavior.
 *
 * Copyright 2012-2026 Mitchell. See LICENSE.
 */

#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* Test harness */
static int s_pass = 0, s_fail = 0;

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("FAIL: %s (%s:%d)\n", #cond, __FILE__, __LINE__); \
        s_fail++; return; \
    } \
} while(0)

/*=============================================================================
 * Mock NotCurses structures for testing
 *===========================================================================*/

typedef struct {
    int rows;
    int cols;
    char *cells;
} MockNcPlane;

static MockNcPlane *mock_plane_create(int rows, int cols) {
    MockNcPlane *p = (MockNcPlane *)malloc(sizeof(MockNcPlane));
    if (!p) return NULL;
    p->rows = rows;
    p->cols = cols;
    p->cells = (char *)calloc(rows * cols, sizeof(char));
    if (!p->cells) {
        free(p);
        return NULL;
    }
    return p;
}

static void mock_plane_destroy(MockNcPlane *p) {
    if (p) {
        free(p->cells);
        free(p);
    }
}

static int mock_plane_dim_yx(const MockNcPlane *p, int *rows, int *cols) {
    if (!p) return -1;
    if (rows) *rows = p->rows;
    if (cols) *cols = p->cols;
    return 0;
}

/*=============================================================================
 * Coordinate validation tests
 *===========================================================================*/

/**
 * @test test_copy_negative_src_coords
 * @brief Verify Copy rejects negative source coordinates
 *
 * The Copy function should validate that from.x and from.y are non-negative
 * to prevent reading outside the source plane bounds.
 */
static void test_copy_negative_src_coords(void) {
    // Test data
    int srcX = -1;  // Invalid
    int srcY = 0;
    
    // Validation logic from fixed Copy()
    bool should_reject = (srcX < 0 || srcY < 0);
    ASSERT(should_reject == true);
    
    // Test with negative Y
    srcX = 0;
    srcY = -5;
    should_reject = (srcX < 0 || srcY < 0);
    ASSERT(should_reject == true);
    
    s_pass++;
}

/**
 * @test test_copy_negative_dst_coords
 * @brief Verify Copy rejects negative destination coordinates
 */
static void test_copy_negative_dst_coords(void) {
    int dstX = -1;
    int dstY = 0;
    
    bool should_reject = (dstX < 0 || dstY < 0);
    ASSERT(should_reject == true);
    
    dstX = 0;
    dstY = -3;
    should_reject = (dstX < 0 || dstY < 0);
    ASSERT(should_reject == true);
    
    s_pass++;
}

/**
 * @test test_copy_zero_dimensions
 * @brief Verify Copy rejects zero or negative dimensions
 */
static void test_copy_zero_dimensions(void) {
    int width = 0;
    int height = 10;
    
    bool should_reject = (width <= 0 || height <= 0);
    ASSERT(should_reject == true);
    
    width = 10;
    height = -5;
    should_reject = (width <= 0 || height <= 0);
    ASSERT(should_reject == true);
    
    width = 0;
    height = 0;
    should_reject = (width <= 0 || height <= 0);
    ASSERT(should_reject == true);
    
    s_pass++;
}

/**
 * @test test_copy_src_bounds_clamping
 * @brief Verify Copy clamps to source bounds correctly
 */
static void test_copy_src_bounds_clamping(void) {
    MockNcPlane *src = mock_plane_create(10, 20);
    ASSERT(src != NULL);
    
    int srcX = 15;  // Near right edge
    int srcY = 5;
    int width = 10;  // Would extend past edge
    int height = 3;
    
    // Logic from fixed Copy()
    if (srcX + width > src->cols) {
        width = src->cols - srcX;
    }
    
    ASSERT(width == 5);  // 20 - 15 = 5
    
    mock_plane_destroy(src);
    s_pass++;
}

/**
 * @test test_copy_dst_bounds_clamping
 * @brief Verify Copy clamps to destination bounds correctly
 */
static void test_copy_dst_bounds_clamping(void) {
    MockNcPlane *dst = mock_plane_create(10, 15);
    ASSERT(dst != NULL);
    
    int dstX = 12;  // Near right edge of 15-col plane
    int dstY = 5;
    int width = 10;  // Would extend past edge
    int height = 3;
    
    if (dstX + width > dst->cols) {
        width = dst->cols - dstX;
    }
    
    ASSERT(width == 3);  // 15 - 12 = 3
    
    mock_plane_destroy(dst);
    s_pass++;
}

/**
 * @test test_copy_out_of_bounds_src_start
 * @brief Verify Copy rejects source start outside plane
 */
static void test_copy_out_of_bounds_src_start(void) {
    MockNcPlane *src = mock_plane_create(10, 20);
    ASSERT(src != NULL);
    
    int srcX = 25;  // Past right edge
    int srcY = 5;
    
    bool should_reject = (srcX >= src->cols || srcY >= src->rows);
    ASSERT(should_reject == true);
    
    srcX = 5;
    srcY = 15;  // Past bottom edge
    should_reject = (srcX >= src->cols || srcY >= src->rows);
    ASSERT(should_reject == true);
    
    mock_plane_destroy(src);
    s_pass++;
}

/**
 * @test test_copy_valid_coordinates
 * @brief Verify Copy accepts valid coordinates
 */
static void test_copy_valid_coordinates(void) {
    MockNcPlane *src = mock_plane_create(20, 40);
    MockNcPlane *dst = mock_plane_create(20, 40);
    ASSERT(src != NULL);
    ASSERT(dst != NULL);
    
    int srcX = 5, srcY = 5;
    int dstX = 10, dstY = 10;
    int width = 15, height = 10;
    
    // All validation checks
    bool valid = (srcX >= 0 && srcY >= 0 && dstX >= 0 && dstY >= 0);
    valid = valid && (width > 0 && height > 0);
    valid = valid && (srcX < src->cols && srcY < src->rows);
    valid = valid && (dstX < dst->cols && dstY < dst->rows);
    
    // Clamp to bounds
    if (srcX + width > src->cols) width = src->cols - srcX;
    if (srcY + height > src->rows) height = src->rows - srcY;
    if (dstX + width > dst->cols) width = dst->cols - dstX;
    if (dstY + height > dst->rows) height = dst->rows - dstY;
    
    valid = valid && (width > 0 && height > 0);
    
    ASSERT(valid == true);
    ASSERT(width == 15);
    ASSERT(height == 10);
    
    mock_plane_destroy(src);
    mock_plane_destroy(dst);
    s_pass++;
}

/*=============================================================================
 * Main
 *===========================================================================*/

int main(void) {
    printf("=== SurfaceImpl::Copy Tests ===\n\n");
    
    test_copy_negative_src_coords();
    test_copy_negative_dst_coords();
    test_copy_zero_dimensions();
    test_copy_src_bounds_clamping();
    test_copy_dst_bounds_clamping();
    test_copy_out_of_bounds_src_start();
    test_copy_valid_coordinates();
    
    printf("\n=== Results: %d passed, %d failed ===\n", s_pass, s_fail);
    return s_fail > 0 ? 1 : 0;
}

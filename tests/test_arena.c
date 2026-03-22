/**
 * @file test_arena.c
 * @brief Unit tests for the per-frame arena allocator
 *
 * Tests the Arena allocator implementation from scinterm_plat.h.
 * This is a pure C99 test that requires no external dependencies.
 *
 * Compilation:
 *   gcc -std=c99 -I../src/plat -o test_arena test_arena.c
 * Or with the build system:
 *   cmake --build build --target test_arena
 *
 * Copyright 2012-2026 Mitchell. See LICENSE.
 */

#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

/* Define a small arena size for testing to verify overflow behavior */
#define SCINTERM_ARENA_SIZE 1024

/* Mock the Arena struct and inline functions - we copy them here for pure C testing */
struct Arena {
    char  *buf;
    size_t pos;
    size_t cap;
};

/* Copied from scinterm_plat.h for testing without C++ dependencies */
static inline bool arena_init(struct Arena *a, size_t cap) {
    a->buf = (char *)__builtin_malloc(cap);
    a->pos = 0;
    a->cap = a->buf ? cap : 0u;
    return a->buf != NULL;
}

static inline void arena_free(struct Arena *a) {
    __builtin_free(a->buf);
    a->buf = NULL;
    a->pos = a->cap = 0u;
}

static inline void arena_reset(struct Arena *a) {
    a->pos = 0u;
}

static inline void *arena_alloc(struct Arena *a, size_t size) {
    const size_t kAlign = sizeof(void *);
    size = (size + kAlign - 1u) & ~(kAlign - 1u);   /* round up */
    if (!a->buf || a->pos + size > a->cap)
        return NULL;
    void *ptr = a->buf + a->pos;
    a->pos += size;
    return ptr;
}

/* Test harness */
static int s_pass = 0, s_fail = 0;

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("FAIL: %s (%s:%d)\n", #cond, __FILE__, __LINE__); \
        s_fail++; return; \
    } \
} while(0)

/*=============================================================================
 * Test Functions
 *===========================================================================*/

/**
 * @test test_arena_basic_alloc
 * @brief Verify that basic allocation returns non-NULL
 *
 * A newly initialized arena should successfully allocate memory from its
 * backing buffer. This is the fundamental happy-path test for the allocator.
 */
static void test_arena_basic_alloc(void) {
    struct Arena a = {0};
    ASSERT(arena_init(&a, 256));
    
    void *p = arena_alloc(&a, 64);
    ASSERT(p != NULL);
    
    arena_free(&a);
    s_pass++;
}

/**
 * @test test_arena_alignment
 * @brief Verify that allocations are 8-byte (pointer) aligned
 *
 * The arena allocator rounds up all allocation sizes to pointer alignment
 * to ensure proper memory alignment for any data type. This test verifies
 * that returned pointers are properly aligned.
 */
static void test_arena_alignment(void) {
    struct Arena a = {0};
    ASSERT(arena_init(&a, 256));
    
    /* Allocate 1 byte - should be rounded up to 8 bytes internally */
    void *p1 = arena_alloc(&a, 1);
    ASSERT(p1 != NULL);
    ASSERT(((uintptr_t)p1 & (sizeof(void *) - 1)) == 0);
    
    /* Allocate 7 bytes - should also be properly aligned */
    void *p2 = arena_alloc(&a, 7);
    ASSERT(p2 != NULL);
    ASSERT(((uintptr_t)p2 & (sizeof(void *) - 1)) == 0);
    
    /* The gap between p1 and p2 should be sizeof(void *) due to alignment */
    ASSERT((char *)p2 - (char *)p1 == (ptrdiff_t)sizeof(void *));
    
    arena_free(&a);
    s_pass++;
}

/**
 * @test test_arena_sequential_no_overlap
 * @brief Verify sequential allocations don't overlap
 *
 * When allocating sequentially from the arena, each allocation should
 * receive a distinct, non-overlapping region of memory.
 */
static void test_arena_sequential_no_overlap(void) {
    struct Arena a = {0};
    ASSERT(arena_init(&a, 256));
    
    void *p1 = arena_alloc(&a, 32);
    void *p2 = arena_alloc(&a, 32);
    void *p3 = arena_alloc(&a, 32);
    
    ASSERT(p1 != NULL && p2 != NULL && p3 != NULL);
    
    /* Verify distinct addresses */
    ASSERT(p1 != p2 && p2 != p3 && p1 != p3);
    
    /* Verify ordering - sequential allocations should be contiguous */
    ASSERT((char *)p2 > (char *)p1);
    ASSERT((char *)p3 > (char *)p2);
    
    /* Verify no overlap */
    ASSERT((char *)p1 + 32 <= (char *)p2);
    ASSERT((char *)p2 + 32 <= (char *)p3);
    
    arena_free(&a);
    s_pass++;
}

/**
 * @test test_arena_capacity_exceeded
 * @brief Verify alloc returns NULL when capacity is exceeded
 *
 * The arena should return NULL when attempting to allocate beyond its
 * capacity. This allows callers to detect exhaustion and fall back to
 * heap allocation.
 */
static void test_arena_capacity_exceeded(void) {
    struct Arena a = {0};
    /* Use small capacity for this test */
    ASSERT(arena_init(&a, 64));
    
    /* Allocate most of the capacity */
    void *p1 = arena_alloc(&a, 48);
    ASSERT(p1 != NULL);
    
    /* Try to allocate more than remaining (64 - 48 = 16, but rounded to 8 = 8 remaining) */
    void *p2 = arena_alloc(&a, 32);
    ASSERT(p2 == NULL);
    
    arena_free(&a);
    s_pass++;
}

/**
 * @test test_arena_reset
 * @brief Verify reset allows memory reuse from the start
 *
 * After resetting the arena, allocations should start from the beginning
 * of the buffer again, effectively "freeing" all previous allocations
 * in O(1) time.
 */
static void test_arena_reset(void) {
    struct Arena a = {0};
    ASSERT(arena_init(&a, 256));
    
    /* Allocate some memory */
    void *p1 = arena_alloc(&a, 64);
    ASSERT(p1 != NULL);
    size_t pos_after_first = a.pos;
    ASSERT(pos_after_first > 0);
    
    /* Reset the arena */
    arena_reset(&a);
    ASSERT(a.pos == 0);
    
    /* New allocation should get the same address as the first one */
    void *p2 = arena_alloc(&a, 64);
    ASSERT(p2 == p1);
    
    arena_free(&a);
    s_pass++;
}

/**
 * @test test_arena_zero_size_alloc
 * @brief Verify zero-size allocation is handled safely
 *
 * Zero-size allocations return the current pointer without advancing
 * (since 0 rounded up is still 0). The arena remains valid for subsequent
 * allocations.
 */
static void test_arena_zero_size_alloc(void) {
    struct Arena a = {0};
    ASSERT(arena_init(&a, 256));
    
    /* Zero-size alloc - returns current pointer, pos unchanged */
    void *p1 = arena_alloc(&a, 0);
    ASSERT(p1 != NULL);
    ASSERT(a.pos == 0); /* Position unchanged */
    
    /* Next allocation should work and get the same address */
    void *p2 = arena_alloc(&a, 32);
    ASSERT(p2 != NULL);
    ASSERT(p2 == p1); /* Same address since zero alloc didn't advance */
    ASSERT(a.pos >= 32); /* Now position advanced */
    
    arena_free(&a);
    s_pass++;
}

/**
 * @test test_arena_free_null
 * @brief Verify arena_free on NULL/zero'd arena is safe
 *
 * Calling arena_free on a zero-initialized arena should not crash.
 * This is important for cleanup paths where init might have failed.
 */
static void test_arena_free_null(void) {
    struct Arena a = {0};
    /* Don't initialize - arena is zeroed */
    ASSERT(a.buf == NULL);
    ASSERT(a.pos == 0);
    ASSERT(a.cap == 0);
    
    /* This should not crash */
    arena_free(&a);
    
    /* State should remain valid (zero) */
    ASSERT(a.buf == NULL);
    ASSERT(a.pos == 0);
    ASSERT(a.cap == 0);
    
    s_pass++;
}

/**
 * @test test_arena_large_alloc
 * @brief Verify large allocations work when capacity permits
 *
 * The arena should handle large allocations that still fit within capacity.
 */
static void test_arena_large_alloc(void) {
    struct Arena a = {0};
    ASSERT(arena_init(&a, 1024));
    
    /* Allocate most of the capacity in one go */
    void *p = arena_alloc(&a, 1000);
    ASSERT(p != NULL);
    
    /* Verify the data can be written to */
    memset(p, 0xAB, 1000);
    ASSERT(((unsigned char *)p)[0] == 0xAB);
    ASSERT(((unsigned char *)p)[999] == 0xAB);
    
    arena_free(&a);
    s_pass++;
}

/**
 * @test test_arena_exact_fit
 * @brief Verify allocation that exactly fits capacity
 *
 * An allocation that exactly matches remaining capacity (accounting for
 * alignment) should succeed, leaving the arena exhausted.
 */
static void test_arena_exact_fit(void) {
    struct Arena a = {0};
    /* Use 64-byte capacity */
    ASSERT(arena_init(&a, 64));
    
    /* Allocate exactly 64 bytes (with proper alignment, this fills the arena) */
    /* On 64-bit: sizeof(void*) = 8, so 64 is already aligned */
    /* On 32-bit: sizeof(void*) = 4, so 64 is already aligned */
    size_t alloc_size = 64;
    void *p = arena_alloc(&a, alloc_size);
    ASSERT(p != NULL);
    
    /* Arena should now be exhausted - any further allocation should fail */
    void *p2 = arena_alloc(&a, 1);
    ASSERT(p2 == NULL);
    
    arena_free(&a);
    s_pass++;
}

/**
 * @test test_arena_multiple_resets
 * @brief Verify multiple alloc/reset cycles work correctly
 *
 * The arena should support many reset cycles without degradation.
 */
static void test_arena_multiple_resets(void) {
    struct Arena a = {0};
    ASSERT(arena_init(&a, 256));
    
    for (int i = 0; i < 100; i++) {
        void *p1 = arena_alloc(&a, 64);
        ASSERT(p1 != NULL);
        
        void *p2 = arena_alloc(&a, 64);
        ASSERT(p2 != NULL);
        
        arena_reset(&a);
        
        /* After reset, should be able to allocate again */
        void *p3 = arena_alloc(&a, 128);
        ASSERT(p3 == p1); /* Should reuse same start address */
        
        arena_reset(&a);
    }
    
    arena_free(&a);
    s_pass++;
}

/*=============================================================================
 * Main Entry Point
 *===========================================================================*/

int main(void) {
    printf("=== Arena Allocator Tests ===\n\n");
    
    test_arena_basic_alloc();
    test_arena_alignment();
    test_arena_sequential_no_overlap();
    test_arena_capacity_exceeded();
    test_arena_reset();
    test_arena_zero_size_alloc();
    test_arena_free_null();
    test_arena_large_alloc();
    test_arena_exact_fit();
    test_arena_multiple_resets();
    
    printf("\n=== Results: %d passed, %d failed ===\n", s_pass, s_fail);
    return s_fail > 0 ? 1 : 0;
}

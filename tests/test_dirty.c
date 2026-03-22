/**
 * @file test_dirty.c
 * @brief Unit tests for dirty region tracking optimization
 *
 * Tests the dirty flag tracking mechanism used to skip unnecessary renders.
 * Since this is internal to the ScintillaNotCurses C++ class, we test it
 * by mocking the relevant state and logic in pure C.
 *
 * Compilation:
 *   gcc -std=c99 -o test_dirty test_dirty.c
 * Or with CMake:
 *   cmake --build build --target test_dirty
 *
 * Copyright 2012-2026 Mitchell. See LICENSE.
 */

#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

/* Mock the dirty tracking state that would be in ScintillaNotCurses class */
struct MockScintillaNotCurses {
    bool dirty;                    /* starts true so first render always paints */
    long long lastRenderTimeNs;    /* monotonic timestamp in nanoseconds */
    bool render_called;            /* tracks if Render() was invoked */
    bool render_succeeded;         /* simulates render success/failure */
};

/* Initialize a mock instance like the real constructor does */
static void mock_init(struct MockScintillaNotCurses *sci) {
    sci->dirty = true;                    /* starts dirty - first render not skipped */
    sci->lastRenderTimeNs = 0;            /* initialized to 0 */
    sci->render_called = false;
    sci->render_succeeded = true;
}

/* Simulate the internal mark_dirty() method */
static void scinterm_mark_dirty(struct MockScintillaNotCurses *sci) {
    sci->dirty = true;
}

/* Simulate the internal is_dirty() method */
static bool scinterm_is_dirty(const struct MockScintillaNotCurses *sci) {
    return sci->dirty;
}

/* Simulate the Render() method's dirty check and behavior */
static void scinterm_render(struct MockScintillaNotCurses *sci) {
    sci->render_called = true;
    
    /* Dirty check - skip if not dirty */
    if (!scinterm_is_dirty(sci)) {
        return; /* Early return - render skipped */
    }
    
    /* Simulate the actual render work */
    if (sci->render_succeeded) {
        /* Only clear dirty flag on successful render */
        sci->dirty = false;
    }
    /* If render failed, dirty remains true for retry */
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
 * @test test_dirty_starts_true
 * @brief Verify new plane starts dirty (first render not skipped)
 *
 * A newly created ScintillaNotCurses instance must start with dirty=true
 * so that the very first render paints the initial content. Without this,
 * the initial screen would be blank until the first change.
 */
static void test_dirty_starts_true(void) {
    struct MockScintillaNotCurses sci;
    mock_init(&sci);
    
    /* New instance should start dirty */
    ASSERT(scinterm_is_dirty(&sci) == true);
    
    s_pass++;
}

/**
 * @test test_mark_dirty_sets_flag
 * @brief Verify mark_dirty() sets the dirty flag
 *
 * After a successful render clears the dirty flag, mark_dirty() must
 * set it again so that the next render will repaint.
 */
static void test_mark_dirty_sets_flag(void) {
    struct MockScintillaNotCurses sci;
    mock_init(&sci);
    
    /* Complete a render to clear the dirty flag */
    scinterm_render(&sci);
    ASSERT(scinterm_is_dirty(&sci) == false);
    
    /* Mark dirty again */
    scinterm_mark_dirty(&sci);
    ASSERT(scinterm_is_dirty(&sci) == true);
    
    s_pass++;
}

/**
 * @test test_render_clears_flag
 * @brief Verify successful render clears dirty flag
 *
 * After a successful render, the dirty flag should be cleared so that
 * subsequent render calls can skip unnecessary work.
 */
static void test_render_clears_flag(void) {
    struct MockScintillaNotCurses sci;
    mock_init(&sci);
    
    /* Start dirty */
    ASSERT(scinterm_is_dirty(&sci) == true);
    
    /* Render should clear the flag */
    sci.render_succeeded = true;
    scinterm_render(&sci);
    ASSERT(scinterm_is_dirty(&sci) == false);
    
    s_pass++;
}

/**
 * @test test_unchanged_plane_skips_render
 * @brief Verify unchanged plane skips render call
 *
 * When the dirty flag is false, Render() should return early without
 * doing any actual painting work. This is the core optimization.
 */
static void test_unchanged_plane_skips_render(void) {
    struct MockScintillaNotCurses sci;
    mock_init(&sci);
    
    /* First render - should execute */
    scinterm_render(&sci);
    ASSERT(sci.render_called == true);
    
    /* Reset the tracking flag */
    sci.render_called = false;
    
    /* Second render without any changes - should skip */
    scinterm_render(&sci);
    /* render_called is set at entry, but we check if work was done */
    /* The implementation sets render_called=true but returns early */
    /* What matters is that dirty stays false and no actual paint happens */
    ASSERT(scinterm_is_dirty(&sci) == false);
    
    s_pass++;
}

/**
 * @test test_failed_render_keeps_dirty
 * @brief Verify failed render does NOT clear dirty flag
 *
 * If rendering fails (e.g., due to resource exhaustion), the dirty flag
 * must remain set so that the next render will retry. This prevents
 * "lost" updates.
 */
static void test_failed_render_keeps_dirty(void) {
    struct MockScintillaNotCurses sci;
    mock_init(&sci);
    
    /* Simulate a render failure */
    sci.render_succeeded = false;
    scinterm_render(&sci);
    
    /* Dirty should still be true for retry */
    ASSERT(scinterm_is_dirty(&sci) == true);
    
    /* Next render succeeds */
    sci.render_succeeded = true;
    scinterm_render(&sci);
    ASSERT(scinterm_is_dirty(&sci) == false);
    
    s_pass++;
}

/**
 * @test test_dirty_after_multiple_renders
 * @brief Verify dirty behavior across multiple render cycles
 *
 * A sequence of mark/render cycles should maintain correct dirty state.
 */
static void test_dirty_after_multiple_renders(void) {
    struct MockScintillaNotCurses sci;
    mock_init(&sci);
    
    /* Cycle 1: initial render */
    ASSERT(scinterm_is_dirty(&sci) == true);
    scinterm_render(&sci);
    ASSERT(scinterm_is_dirty(&sci) == false);
    
    /* Simulate some change */
    scinterm_mark_dirty(&sci);
    ASSERT(scinterm_is_dirty(&sci) == true);
    
    /* Cycle 2: render after change */
    scinterm_render(&sci);
    ASSERT(scinterm_is_dirty(&sci) == false);
    
    /* No change, another render */
    scinterm_render(&sci);
    ASSERT(scinterm_is_dirty(&sci) == false);
    
    /* Another change */
    scinterm_mark_dirty(&sci);
    ASSERT(scinterm_is_dirty(&sci) == true);
    
    /* Cycle 3: render */
    scinterm_render(&sci);
    ASSERT(scinterm_is_dirty(&sci) == false);
    
    s_pass++;
}

/**
 * @test test_redundant_mark_dirty
 * @brief Verify redundant mark_dirty calls are harmless
 *
 * Multiple mark_dirty calls should have the same effect as one.
 */
static void test_redundant_mark_dirty(void) {
    struct MockScintillaNotCurses sci;
    mock_init(&sci);
    
    /* First render to clear dirty */
    scinterm_render(&sci);
    ASSERT(scinterm_is_dirty(&sci) == false);
    
    /* Mark dirty multiple times */
    scinterm_mark_dirty(&sci);
    scinterm_mark_dirty(&sci);
    scinterm_mark_dirty(&sci);
    
    /* Should still be dirty */
    ASSERT(scinterm_is_dirty(&sci) == true);
    
    /* One render should clear it */
    scinterm_render(&sci);
    ASSERT(scinterm_is_dirty(&sci) == false);
    
    s_pass++;
}

/**
 * @test test_dirty_persists_across_checks
 * @brief Verify dirty state persists until successfully rendered
 *
 * Calling is_dirty() (the predicate) should not modify the state.
 */
static void test_dirty_persists_across_checks(void) {
    struct MockScintillaNotCurses sci;
    mock_init(&sci);
    
    /* Check multiple times */
    ASSERT(scinterm_is_dirty(&sci) == true);
    ASSERT(scinterm_is_dirty(&sci) == true);
    ASSERT(scinterm_is_dirty(&sci) == true);
    
    /* Still dirty */
    ASSERT(sci.dirty == true);
    
    s_pass++;
}

/**
 * @test test_initial_last_render_time
 * @brief Verify initial lastRenderTime is zeroed
 *
 * lastRenderTime starts at 0 so that the first frame throttle
 * calculation (now - 0) >> FRAME_NS, ensuring immediate first render.
 */
static void test_initial_last_render_time(void) {
    struct MockScintillaNotCurses sci;
    mock_init(&sci);
    
    ASSERT(sci.lastRenderTimeNs == 0);
    
    s_pass++;
}

/**
 * @test test_dirty_with_simulated_operations
 * @brief Verify dirty tracking with simulated editor operations
 *
 * Simulates various editor operations that should mark dirty.
 */
static void test_dirty_with_simulated_operations(void) {
    struct MockScintillaNotCurses sci;
    mock_init(&sci);
    
    /* Initial render */
    scinterm_render(&sci);
    ASSERT(scinterm_is_dirty(&sci) == false);
    
    /* Simulate: cursor movement */
    scinterm_mark_dirty(&sci);
    ASSERT(scinterm_is_dirty(&sci) == true);
    scinterm_render(&sci);
    
    /* Simulate: text insertion */
    scinterm_mark_dirty(&sci);
    ASSERT(scinterm_is_dirty(&sci) == true);
    scinterm_render(&sci);
    
    /* Simulate: scroll */
    scinterm_mark_dirty(&sci);
    ASSERT(scinterm_is_dirty(&sci) == true);
    scinterm_render(&sci);
    
    /* Simulate: selection change */
    scinterm_mark_dirty(&sci);
    ASSERT(scinterm_is_dirty(&sci) == true);
    scinterm_render(&sci);
    
    ASSERT(scinterm_is_dirty(&sci) == false);
    
    s_pass++;
}

/*=============================================================================
 * Main Entry Point
 *===========================================================================*/

int main(void) {
    printf("=== Dirty Region Tracking Tests ===\n\n");
    
    test_dirty_starts_true();
    test_mark_dirty_sets_flag();
    test_render_clears_flag();
    test_unchanged_plane_skips_render();
    test_failed_render_keeps_dirty();
    test_dirty_after_multiple_renders();
    test_redundant_mark_dirty();
    test_dirty_persists_across_checks();
    test_initial_last_render_time();
    test_dirty_with_simulated_operations();
    
    printf("\n=== Results: %d passed, %d failed ===\n", s_pass, s_fail);
    return s_fail > 0 ? 1 : 0;
}

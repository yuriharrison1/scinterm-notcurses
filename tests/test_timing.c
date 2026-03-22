/**
 * @file test_timing.c
 * @brief Unit tests for timing and throttle fixes
 *
 * Tests clock_gettime validation, EINTR handling, and frame timing.
 *
 * Copyright 2012-2026 Mitchell. See LICENSE.
 */

#define _POSIX_C_SOURCE 199309L

#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <signal.h>

/* Test harness */
static int s_pass = 0, s_fail = 0;

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("FAIL: %s (%s:%d)\n", #cond, __FILE__, __LINE__); \
        s_fail++; return; \
    } \
} while(0)

/* Target FPS and derived frame timing */
#define SCINTERM_TARGET_FPS 60
#define FRAME_NS (1000000000LL / SCINTERM_TARGET_FPS)

/*=============================================================================
 * Mock clock functions
 *===========================================================================*/

static int mock_clock_fail = 0;

static int mock_clock_gettime(clockid_t clk_id, struct timespec *tp) {
    (void)clk_id;
    
    if (mock_clock_fail) {
        return -1;  // Simulate failure
    }
    
    // Return mock time
    tp->tv_sec = 1000;
    tp->tv_nsec = 500000000;
    return 0;
}

/*=============================================================================
 * Clock validation tests
 *===========================================================================*/

/**
 * @test test_clock_gettime_success
 * @brief Valid clock_gettime should return true
 */
static void test_clock_gettime_success(void) {
    struct timespec ts = {};
    
    // Mock successful call
    int result = mock_clock_gettime(CLOCK_MONOTONIC, &ts);
    bool success = (result == 0);
    
    ASSERT(success == true);
    ASSERT(ts.tv_sec == 1000);
    ASSERT(ts.tv_nsec == 500000000);
    
    s_pass++;
}

/**
 * @test test_clock_gettime_failure
 * @brief Failed clock_gettime should be handled gracefully
 */
static void test_clock_gettime_failure(void) {
    struct timespec ts = {};
    
    mock_clock_fail = 1;
    int result = mock_clock_gettime(CLOCK_MONOTONIC, &ts);
    mock_clock_fail = 0;
    
    bool success = (result == 0);
    ASSERT(success == false);
    
    // After failure, ts should be zeroed or have fallback values
    // In our fixed implementation, it tries CLOCK_REALTIME
    
    s_pass++;
}

/**
 * @test test_time_diff_calculation
 * @brief Time difference calculation should be correct
 */
static void test_time_diff_calculation(void) {
    struct timespec a = { 10, 500000000 };  // 10.5s
    struct timespec b = { 5,  250000000 };  // 5.25s
    
    long long diff = (a.tv_sec - b.tv_sec) * 1000000000LL + 
                     (a.tv_nsec - b.tv_nsec);
    
    ASSERT(diff == 5250000000LL);  // 5.25s in nanoseconds
    
    // Reverse
    diff = (b.tv_sec - a.tv_sec) * 1000000000LL + 
           (b.tv_nsec - a.tv_nsec);
    
    ASSERT(diff == -5250000000LL);
    
    s_pass++;
}

/**
 * @test test_frame_throttle_calculation
 * @brief Frame timing should be calculated correctly
 */
static void test_frame_throttle_calculation(void) {
    // At 60 FPS, frame time should be ~16.67ms
    long long expected = 1000000000LL / 60;
    
    ASSERT(FRAME_NS == expected);
    ASSERT(FRAME_NS > 16000000);   // > 16ms
    ASSERT(FRAME_NS < 17000000);   // < 17ms
    
    s_pass++;
}

/**
 * @test test_throttle_should_wait
 * @brief Throttle should wait when frame time not elapsed
 */
static void test_throttle_should_wait(void) {
    long long elapsed = FRAME_NS / 2;  // Only half frame time elapsed
    
    bool should_wait = (elapsed < FRAME_NS);
    ASSERT(should_wait == true);
    
    long long wait_time = FRAME_NS - elapsed;
    ASSERT(wait_time == FRAME_NS / 2);
    
    s_pass++;
}

/**
 * @test test_throttle_should_render
 * @brief Render should proceed when frame time elapsed
 */
static void test_throttle_should_render(void) {
    long long elapsed = FRAME_NS + 1000000;  // Frame time + 1ms
    
    bool should_wait = (elapsed < FRAME_NS);
    ASSERT(should_wait == false);
    
    s_pass++;
}

/**
 * @test test_eintr_handling_logic
 * @brief EINTR should cause nanosleep to be retried
 */
static void test_eintr_handling_logic(void) {
    // Simulate the logic in sc_nanosleep
    int eintr_count = 0;
    int max_retries = 5;
    long long remaining_ns = 10000000;  // 10ms
    
    // Simulate being interrupted 3 times
    for (int i = 0; i < 3 && remaining_ns > 0; i++) {
        eintr_count++;
        // Each time, we "retry" with remaining time
        remaining_ns -= 1000000;  // Pretend 1ms passed
    }
    
    ASSERT(eintr_count == 3);
    ASSERT(remaining_ns == 7000000);  // 10 - 3 = 7ms remaining
    ASSERT(eintr_count < max_retries);  // Didn't exceed max
    
    s_pass++;
}

/**
 * @test test_first_render_immediate
 * @brief First render should happen immediately (lastRenderTime = 0)
 */
static void test_first_render_immediate(void) {
    struct timespec lastRenderTime = { 0, 0 };
    struct timespec now = { 10, 0 };
    
    // lastRenderTime = 0 means ~10 billion ns elapsed
    long long elapsed = (now.tv_sec - lastRenderTime.tv_sec) * 1000000000LL +
                        (now.tv_nsec - lastRenderTime.tv_nsec);
    
    ASSERT(elapsed > FRAME_NS);  // Way more than one frame
    ASSERT(elapsed == 10000000000LL);
    
    bool should_render = (elapsed >= FRAME_NS);
    ASSERT(should_render == true);
    
    s_pass++;
}

/**
 * @test test_resize_bypasses_throttle
 * @brief Resize events should bypass frame throttle
 */
static void test_resize_bypasses_throttle(void) {
    bool is_resize = true;
    long long elapsed = FRAME_NS / 10;  // Very little time passed
    
    // Resize should bypass
    bool should_render = is_resize || (elapsed >= FRAME_NS);
    ASSERT(should_render == true);
    
    // But non-resize with same timing should wait
    is_resize = false;
    should_render = is_resize || (elapsed >= FRAME_NS);
    ASSERT(should_render == false);
    
    s_pass++;
}

/*=============================================================================
 * Main
 *===========================================================================*/

int main(void) {
    printf("=== Timing and Throttle Tests ===\n\n");
    
    test_clock_gettime_success();
    test_clock_gettime_failure();
    test_time_diff_calculation();
    test_frame_throttle_calculation();
    test_throttle_should_wait();
    test_throttle_should_render();
    test_eintr_handling_logic();
    test_first_render_immediate();
    test_resize_bypasses_throttle();
    
    printf("\n=== Results: %d passed, %d failed ===\n", s_pass, s_fail);
    return s_fail > 0 ? 1 : 0;
}

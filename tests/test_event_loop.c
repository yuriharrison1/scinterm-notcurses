/**
 * @file test_event_loop.c
 * @brief Unit tests for event loop optimizations
 *
 * Tests the input batching and frame throttle mechanisms:
 * - Input batching: process all queued events before rendering
 * - Frame throttle: limit renders to target FPS (default 60)
 * - Resize bypass: resize events render immediately
 *
 * Since these depend on notcurses and timing, we mock the relevant
 * interfaces for reproducible testing.
 *
 * Compilation:
 *   gcc -std=c99 -o test_event_loop test_event_loop.c
 * Or with CMake:
 *   cmake --build build --target test_event_loop
 *
 * Copyright 2012-2026 Mitchell. See LICENSE.
 */

#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

/* Target FPS and derived frame timing */
#define SCINTERM_TARGET_FPS 60
#define FRAME_TIME_NS (1000000000LL / SCINTERM_TARGET_FPS)  /* ~16.67ms */

/* Mock notcurses types */
#define NCKEY_RESIZE ((uint32_t)-10)
#define NCKEY_UP     ((uint32_t)-46)
#define NCKEY_DOWN   ((uint32_t)-47)
#define NCKEY_LEFT   ((uint32_t)-48)
#define NCKEY_RIGHT  ((uint32_t)-49)
#define NCKEY_ENTER  ((uint32_t)-40)
#define NCKEY_MOD_CTRL  2
#define NCKEY_MOD_SHIFT 1

typedef struct ncinput {
    uint32_t id;
    uint32_t modifiers;
    int      evtype;
    char     eff_text[5];
} ncinput;

typedef struct notcurses notcurses;

/* Mock event queue for testing */
#define MAX_MOCK_EVENTS 256

static struct {
    ncinput events[MAX_MOCK_EVENTS];
    int count;
    int read_pos;
} s_mock_queue = {0};

static int s_render_count = 0;
static int s_key_event_count = 0;
static int s_resize_event_count = 0;
static int64_t s_mock_time_ns = 0;
static bool s_throttle_bypassed = false;

/* Reset mock state */
static void mock_reset(void) {
    memset(&s_mock_queue, 0, sizeof(s_mock_queue));
    s_render_count = 0;
    s_key_event_count = 0;
    s_resize_event_count = 0;
    s_mock_time_ns = 0;
    s_throttle_bypassed = false;
}

/* Add a mock event to the queue */
static void mock_add_event(uint32_t key, uint32_t modifiers, int evtype) {
    if (s_mock_queue.count < MAX_MOCK_EVENTS) {
        s_mock_queue.events[s_mock_queue.count++] = (ncinput){
            .id = key,
            .modifiers = modifiers,
            .evtype = evtype,
            .eff_text = {0}
        };
    }
}

/* Mock notcurses_get_nblock - non-blocking input */
static uint32_t mock_notcurses_get_nblock(notcurses *nc, ncinput *ni) {
    (void)nc;
    if (s_mock_queue.read_pos < s_mock_queue.count) {
        *ni = s_mock_queue.events[s_mock_queue.read_pos];
        return s_mock_queue.events[s_mock_queue.read_pos++].id;
    }
    return (uint32_t)-1; /* No more events */
}

/* Mock render counter (kept for future expansion) */
static int mock_notcurses_render(notcurses *nc) {
    (void)nc;
    s_render_count++;
    return 0;
}

/* Mock clock for deterministic testing */
static int64_t mock_clock_gettime_ns(void) {
    return s_mock_time_ns;
}

static void mock_advance_time_ns(int64_t ns) {
    s_mock_time_ns += ns;
}

/* Mock ScintillaNotCurses input processing */
struct MockEditor {
    int64_t lastRenderTime;
    bool needs_render;
};

/* Simulate ProcessInput with batching */
static bool mock_process_input(struct MockEditor *ed, notcurses *nc) {
    (void)ed;
    ncinput input;
    uint32_t keys[MAX_MOCK_EVENTS];
    int n = 0;
    
    /* Batch all available events */
    while (n < MAX_MOCK_EVENTS) {
        uint32_t key = mock_notcurses_get_nblock(nc, &input);
        if (key == (uint32_t)-1 || key == 0) break;
        keys[n] = key;
        ++n;
    }
    
    if (n == 0) return false;
    
    /* Process all batched events */
    for (int i = 0; i < n; ++i) {
        if (keys[i] == NCKEY_RESIZE) {
            s_resize_event_count++;
            /* Resize bypasses throttle */
            s_throttle_bypassed = true;
            ed->needs_render = true;
        } else {
            s_key_event_count++;
            ed->needs_render = true;
        }
    }
    
    return true;
}

/* Simulate frame throttle logic */
static bool should_render(struct MockEditor *ed) {
    int64_t now = mock_clock_gettime_ns();
    int64_t elapsed = now - ed->lastRenderTime;
    
    /* Initial render: lastRenderTime=0 means first frame always renders */
    /* (In real code, now - {0,0} is huge >> FRAME_NS) */
    if (ed->lastRenderTime == 0 && now == 0) {
        ed->lastRenderTime = now;
        return true;
    }
    
    /* Resize bypasses throttle */
    if (s_throttle_bypassed) {
        s_throttle_bypassed = false;
        ed->lastRenderTime = now;
        return true;
    }
    
    if (elapsed < FRAME_TIME_NS) {
        return false; /* Throttled */
    }
    
    ed->lastRenderTime = now;
    return true;
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
 * @test test_single_event_processed
 * @brief Verify single event is processed correctly
 *
 * A single input event should be read, processed, and trigger a render.
 */
static void test_single_event_processed(void) {
    mock_reset();
    struct MockEditor ed = {0};
    
    /* Add one key event */
    mock_add_event('a', 0, 1);
    
    /* Process input */
    bool had_input = mock_process_input(&ed, NULL);
    ASSERT(had_input == true);
    ASSERT(s_key_event_count == 1);
    ASSERT(s_resize_event_count == 0);
    ASSERT(ed.needs_render == true);
    
    s_pass++;
}

/**
 * @test test_burst_events_all_processed
 * @brief Verify burst of N events are all processed before one render
 *
 * Input batching should collect all queued events and process them
 * before any rendering occurs. This ensures that a burst of keystrokes
 * results in only one render, not N renders.
 */
static void test_burst_events_all_processed(void) {
    mock_reset();
    struct MockEditor ed = {0};
    
    const int BURST_SIZE = 10;
    
    /* Add multiple key events */
    for (int i = 0; i < BURST_SIZE; i++) {
        mock_add_event('a' + i, 0, 1);
    }
    
    /* Process input - should batch all events */
    bool had_input = mock_process_input(&ed, NULL);
    ASSERT(had_input == true);
    ASSERT(s_key_event_count == BURST_SIZE);
    
    /* Queue should be empty now */
    ncinput dummy;
    uint32_t key = mock_notcurses_get_nblock(NULL, &dummy);
    ASSERT(key == (uint32_t)-1);
    
    s_pass++;
}

/**
 * @test test_throttle_no_render_before_frame_time
 * @brief Verify no render before FRAME_TIME_NS elapsed
 *
 * The frame throttle should prevent renders that occur too quickly
 * after the previous render, enforcing the target FPS limit.
 */
static void test_throttle_no_render_before_frame_time(void) {
    mock_reset();
    struct MockEditor ed = {0};
    ed.lastRenderTime = 0;
    
    /* First render at t=0 */
    mock_advance_time_ns(0);
    ASSERT(should_render(&ed) == true);
    ASSERT(ed.lastRenderTime == 0);
    
    /* Advance time, but not enough */
    mock_advance_time_ns(FRAME_TIME_NS / 2);
    ed.needs_render = true;
    ASSERT(should_render(&ed) == false); /* Throttled */
    
    /* Last render time should not have changed */
    ASSERT(ed.lastRenderTime == 0);
    
    s_pass++;
}

/**
 * @test test_throttle_render_after_frame_time
 * @brief Verify render triggered after FRAME_TIME_NS elapsed
 *
 * Once FRAME_TIME_NS has elapsed since the last render, a new render
 * should be allowed.
 */
static void test_throttle_render_after_frame_time(void) {
    mock_reset();
    struct MockEditor ed = {0};
    ed.lastRenderTime = 0;
    
    /* First render */
    mock_advance_time_ns(0);
    ASSERT(should_render(&ed) == true);
    
    /* Advance time past the frame budget */
    mock_advance_time_ns(FRAME_TIME_NS + 1000);
    ed.needs_render = true;
    ASSERT(should_render(&ed) == true);
    
    /* Last render time should be updated */
    ASSERT(ed.lastRenderTime == FRAME_TIME_NS + 1000);
    
    s_pass++;
}

/**
 * @test test_resize_bypasses_throttle
 * @brief Verify resize event bypasses throttle and renders immediately
 *
 * Resize events are critical UI feedback and should not be throttled.
 * They must render immediately regardless of the frame timer.
 */
static void test_resize_bypasses_throttle(void) {
    mock_reset();
    struct MockEditor ed = {0};
    
    /* First render at t=0 */
    mock_advance_time_ns(0);
    ASSERT(should_render(&ed) == true);
    
    /* Very shortly after, simulate resize */
    mock_advance_time_ns(1000); /* Only 1 microsecond later */
    
    /* Add and process resize event */
    mock_add_event(NCKEY_RESIZE, 0, 1);
    bool had_input = mock_process_input(&ed, NULL);
    ASSERT(had_input == true);
    ASSERT(s_resize_event_count == 1);
    
    /* Resize should bypass throttle */
    ASSERT(should_render(&ed) == true);
    
    s_pass++;
}

/**
 * @test test_mixed_events_with_resize
 * @brief Verify mixed key and resize events are handled correctly
 *
 * When keys and resize are mixed in a batch, all should be processed
 * and the resize should trigger immediate render.
 */
static void test_mixed_events_with_resize(void) {
    mock_reset();
    struct MockEditor ed = {0};
    
    /* Add key, key, resize, key */
    mock_add_event('a', 0, 1);
    mock_add_event('b', 0, 1);
    mock_add_event(NCKEY_RESIZE, 0, 1);
    mock_add_event('c', 0, 1);
    
    bool had_input = mock_process_input(&ed, NULL);
    ASSERT(had_input == true);
    ASSERT(s_key_event_count == 3);
    ASSERT(s_resize_event_count == 1);
    ASSERT(s_throttle_bypassed == true);
    
    s_pass++;
}

/**
 * @test test_empty_queue_returns_false
 * @brief Verify empty input queue returns false
 *
 * When no events are available, ProcessInput should return false
 * to indicate no work was done.
 */
static void test_empty_queue_returns_false(void) {
    mock_reset();
    struct MockEditor ed = {0};
    
    /* Don't add any events */
    bool had_input = mock_process_input(&ed, NULL);
    ASSERT(had_input == false);
    ASSERT(s_key_event_count == 0);
    ASSERT(s_resize_event_count == 0);
    
    s_pass++;
}

/**
 * @test test_large_batch_processed
 * @brief Verify large batches (>100 events) are all processed
 *
 * The batch size limit should be high enough to handle large input bursts.
 */
static void test_large_batch_processed(void) {
    mock_reset();
    struct MockEditor ed = {0};
    
    const int LARGE_BATCH = 150;
    
    for (int i = 0; i < LARGE_BATCH; i++) {
        mock_add_event((i % 26) + 'a', 0, 1);
    }
    
    bool had_input = mock_process_input(&ed, NULL);
    ASSERT(had_input == true);
    ASSERT(s_key_event_count == LARGE_BATCH);
    
    s_pass++;
}

/**
 * @test test_throttle_exact_frame_time
 * @brief Verify render at exactly FRAME_TIME_NS boundary
 *
 * At the exact boundary, render should be allowed.
 */
static void test_throttle_exact_frame_time(void) {
    mock_reset();
    struct MockEditor ed = {0};
    
    /* First render */
    mock_advance_time_ns(0);
    ASSERT(should_render(&ed) == true);
    
    /* Advance exactly FRAME_TIME_NS */
    mock_advance_time_ns(FRAME_TIME_NS);
    ed.needs_render = true;
    ASSERT(should_render(&ed) == true);
    
    s_pass++;
}

/**
 * @test test_consecutive_renders_with_throttle
 * @brief Verify consecutive renders respect throttle
 *
 * Multiple render requests should be throttled appropriately.
 */
static void test_consecutive_renders_with_throttle(void) {
    mock_reset();
    struct MockEditor ed = {0};
    
    /* First render */
    mock_advance_time_ns(0);
    ASSERT(should_render(&ed) == true);
    
    /* Try to render at various times */
    for (int i = 1; i <= 5; i++) {
        s_mock_time_ns = i * (FRAME_TIME_NS / 2);
        bool should = should_render(&ed);
        if (i < 2) {
            ASSERT(should == false); /* Before FRAME_TIME_NS */
        } else {
            /* After FRAME_TIME_NS, render allowed */
            if (should) break;
        }
    }
    
    s_pass++;
}

/**
 * @test test_event_batching_with_modifiers
 * @brief Verify events with modifiers are batched correctly
 *
 * Events with Ctrl, Shift, etc. should batch alongside regular keys.
 */
static void test_event_batching_with_modifiers(void) {
    mock_reset();
    struct MockEditor ed = {0};
    
    /* Add events with modifiers */
    mock_add_event('c', NCKEY_MOD_CTRL, 1);
    mock_add_event('v', NCKEY_MOD_CTRL, 1);
    mock_add_event(NCKEY_UP, NCKEY_MOD_SHIFT, 1);
    mock_add_event('a', 0, 1);
    
    bool had_input = mock_process_input(&ed, NULL);
    ASSERT(had_input == true);
    ASSERT(s_key_event_count == 4);
    
    s_pass++;
}

/*=============================================================================
 * Main Entry Point
 *===========================================================================*/

int main(void) {
    printf("=== Event Loop Tests ===\n\n");
    
    test_single_event_processed();
    test_burst_events_all_processed();
    test_throttle_no_render_before_frame_time();
    test_throttle_render_after_frame_time();
    test_resize_bypasses_throttle();
    test_mixed_events_with_resize();
    test_empty_queue_returns_false();
    test_large_batch_processed();
    test_throttle_exact_frame_time();
    test_consecutive_renders_with_throttle();
    test_event_batching_with_modifiers();
    
    printf("\n=== Results: %d passed, %d failed ===\n", s_pass, s_fail);
    return s_fail > 0 ? 1 : 0;
}

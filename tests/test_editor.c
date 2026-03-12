/**
 * @file test_editor.c
 * @brief Basic tests for Scinterm NotCurses
 * Copyright 2012-2026 Mitchell. See LICENSE.
 */

#include "scinterm_notcurses.h"
#include <notcurses/notcurses.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name, expr) do { \
    if (expr) { tests_passed++; printf("PASS: %s\n", name); } \
    else { tests_failed++; printf("FAIL: %s\n", name); } \
} while(0)

static void dummy_callback(void *sci, int msg, SCNotification *n, void *ud) {
    (void)sci; (void)msg; (void)n; (void)ud;
}

int main(void) {
    printf("=== Scinterm NotCurses Tests ===\n\n");

    /* Test init */
    bool ok = scintilla_notcurses_init();
    TEST("init", ok);

    if (!ok) {
        printf("Cannot continue without init\n");
        return 1;
    }

    /* Test create */
    void *sci = scintilla_new(dummy_callback, NULL);
    TEST("create editor", sci != NULL);

    if (!sci) {
        printf("Cannot continue without editor\n");
        scintilla_notcurses_shutdown();
        return 1;
    }

    /* Test get plane */
    struct ncplane *plane = scintilla_get_plane(sci);
    TEST("get plane", plane != NULL);

    /* Test send message - set text */
    scintilla_send_message(sci, SCI_SETTEXT, 0, (sptr_t)"Hello, World!");
    int len = (int)scintilla_send_message(sci, SCI_GETTEXTLENGTH, 0, 0);
    TEST("set/get text length", len == 13);

    /* Test get text */
    char buf[64] = {0};
    scintilla_send_message(sci, SCI_GETTEXT, (uptr_t)sizeof(buf), (sptr_t)buf);
    TEST("get text content", strcmp(buf, "Hello, World!") == 0);

    /* Test render (should not crash) */
    scintilla_render(sci);
    TEST("render no crash", 1);

    /* Test focus */
    scintilla_set_focus(sci, true);
    scintilla_set_focus(sci, false);
    TEST("focus toggle", 1);

    /* Test resize */
    scintilla_resize(sci);
    TEST("resize no crash", 1);

    /* Test cursor update */
    scintilla_update_cursor(sci);
    TEST("update cursor no crash", 1);

    /* Test key sending */
    scintilla_set_focus(sci, true);
    scintilla_send_message(sci, SCI_DOCUMENTEND, 0, 0);
    scintilla_send_key(sci, 'A', 0);
    TEST("send key no crash", 1);

    /* Cleanup */
    scintilla_delete(sci);
    TEST("delete no crash", 1);

    scintilla_notcurses_shutdown();
    TEST("shutdown no crash", 1);

    printf("\n=== Results: %d passed, %d failed ===\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}

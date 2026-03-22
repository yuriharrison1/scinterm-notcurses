/**
 * @file multi_window.c
 * @brief Scinterm NotCurses multi-window example (two editors side by side)
 * Copyright 2012-2026 Mitchell. See LICENSE.
 */

#include "scinterm_notcurses.h"
#include <notcurses/notcurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

static int active_editor = 0;  /* 0 = left, 1 = right */

static void on_notify(ScintillaHandle *sci, int msg, SCNotification *n, void *ud) {
    (void)sci; (void)msg; (void)n; (void)ud;
}

int main(void) {
    if (!scintilla_notcurses_init()) {
        fprintf(stderr, "Failed to initialize NotCurses\n");
        return 1;
    }

    /* Get nc context from the first editor's plane */
    ScintillaHandle *editor_left = scintilla_new(on_notify, NULL);
    if (!editor_left) {
        fprintf(stderr, "Failed to create left editor\n");
        scintilla_notcurses_shutdown();
        return 1;
    }

    struct ncplane *left_plane = scintilla_get_plane(editor_left);
    struct notcurses *nc = ncplane_notcurses(left_plane);

    /* Get terminal dimensions */
    unsigned rows = 0, cols = 0;
    ncplane_dim_yx(notcurses_stdplane(nc), &rows, &cols);

    /* Resize left editor to fill left half */
    unsigned half = cols / 2;
    ncplane_resize_simple(left_plane, rows, half);
    ncplane_move_yx(left_plane, 0, 0);

    /* Create right editor */
    ScintillaHandle *editor_right = scintilla_new(on_notify, NULL);
    if (!editor_right) {
        fprintf(stderr, "Failed to create right editor\n");
        scintilla_delete(editor_left);
        scintilla_notcurses_shutdown();
        return 1;
    }

    struct ncplane *right_plane = scintilla_get_plane(editor_right);
    ncplane_resize_simple(right_plane, rows, cols - half);
    ncplane_move_yx(right_plane, 0, (int)half);

    /* Set content */
    scintilla_send_message(editor_left, SCI_SETTEXT, 0,
        (sptr_t)"Left Editor\n\nPress Tab to switch\nCtrl+Q to quit\n");
    scintilla_send_message(editor_right, SCI_SETTEXT, 0,
        (sptr_t)"Right Editor\n\nPress Tab to switch\nCtrl+Q to quit\n");

    /* Focus left editor */
    scintilla_set_focus(editor_left, true);
    scintilla_set_focus(editor_right, false);

    notcurses_mice_enable(nc, NCMICE_ALL_EVENTS);
    notcurses_linesigs_disable(nc);

    bool running = true;
    while (running) {
        scintilla_render(editor_left);
        scintilla_render(editor_right);
        if (active_editor == 0) {
            scintilla_update_cursor(editor_left);
        } else {
            scintilla_update_cursor(editor_right);
        }
        notcurses_render(nc);

        ncinput input = {};
        uint32_t key = notcurses_get_blocking(nc, &input);
        if (key == (uint32_t)-1) break;

        /* Skip key release events (Kitty protocol sends press+release pairs) */
        if (input.evtype == NCTYPE_RELEASE) continue;

        /* Ctrl+Q: notcurses delivers Ctrl+letter as uppercase with ctrl=1 */
        bool ctrl = input.ctrl || (input.modifiers & NCKEY_MOD_CTRL);
        if (key == 0x11 || ((key == 'q' || key == 'Q') && ctrl)) {
            running = false;
        } else if (key == NCKEY_RESIZE) {
            notcurses_refresh(nc, NULL, NULL);
            ncplane_dim_yx(notcurses_stdplane(nc), &rows, &cols);
            half = cols / 2;
            ncplane_resize_simple(left_plane, rows, half);
            ncplane_move_yx(left_plane, 0, 0);
            ncplane_resize_simple(right_plane, rows, cols - half);
            ncplane_move_yx(right_plane, 0, (int)half);
            scintilla_resize(editor_left);
            scintilla_resize(editor_right);
        } else if (key == NCKEY_TAB && !(input.modifiers & NCKEY_MOD_CTRL)) {
            /* Switch active editor */
            if (active_editor == 0) {
                scintilla_set_focus(editor_left, false);
                scintilla_set_focus(editor_right, true);
                active_editor = 1;
            } else {
                scintilla_set_focus(editor_right, false);
                scintilla_set_focus(editor_left, true);
                active_editor = 0;
            }
        } else {
            ScintillaHandle *active = (active_editor == 0) ? editor_left : editor_right;
            scintilla_send_key(active, (int)key, (int)input.modifiers);
        }
    }

    scintilla_delete(editor_left);
    scintilla_delete(editor_right);
    scintilla_notcurses_shutdown();
    return 0;
}

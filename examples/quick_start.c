/**
 * @file quick_start.c
 * @brief Quick start example for Scinterm NotCurses
 * Copyright 2012-2026 Mitchell. See LICENSE.
 */

#include "scinterm_notcurses.h"
#include <notcurses/notcurses.h>
#include <stdbool.h>

/* Notification callback */
static void callback(ScintillaHandle *sci, int iMessage, SCNotification *n, void *userdata) {
    (void)sci; (void)iMessage; (void)n; (void)userdata;
    /* Handle Scintilla notifications */
}

int main(void) {
    /* Initialize NotCurses */
    if (!scintilla_notcurses_init()) return 1;

    /* Create editor */
    ScintillaHandle *editor = scintilla_new(callback, NULL);
    if (!editor) {
        scintilla_notcurses_shutdown();
        return 1;
    }

    /* Set initial text */
    scintilla_send_message(editor, SCI_SETTEXT, 0,
        (sptr_t)"Hello, Scinterm World!\n");

    /* Get nc from the editor plane */
    struct ncplane *plane = scintilla_get_plane(editor);
    struct notcurses *nc = ncplane_notcurses(plane);

    scintilla_set_focus(editor, true);

    /* Main loop */
    bool running = true;
    while (running) {
        scintilla_render(editor);
        notcurses_render(nc);

        ncinput input = {};
        uint32_t key = notcurses_get_blocking(nc, &input);
        if (key == (uint32_t)-1) break;

        /* Skip key release events (Kitty protocol sends press+release pairs) */
        if (input.evtype == NCTYPE_RELEASE) continue;

        if (key == NCKEY_ESC) {
            running = false;
        } else if (key == NCKEY_RESIZE) {
            notcurses_refresh(nc, NULL, NULL);
            scintilla_resize(editor);
        } else {
            scintilla_send_key(editor, (int)key, (int)input.modifiers);
        }
    }

    /* Cleanup */
    scintilla_delete(editor);
    scintilla_notcurses_shutdown();

    return 0;
}

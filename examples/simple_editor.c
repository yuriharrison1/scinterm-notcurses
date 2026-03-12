/**
 * @file simple_editor.c
 * @brief Simple Scinterm NotCurses editor example
 * Copyright 2012-2026 Mitchell. See LICENSE.
 */

#include "scinterm_notcurses.h"
#include <notcurses/notcurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

static void on_notify(void *sci, int msg, SCNotification *n, void *ud) {
    (void)sci; (void)msg; (void)n; (void)ud;
}

int main(int argc, char *argv[]) {
    if (!scintilla_notcurses_init()) {
        fprintf(stderr, "Failed to initialize NotCurses\n");
        return 1;
    }

    void *editor = scintilla_new(on_notify, NULL);
    if (!editor) {
        fprintf(stderr, "Failed to create editor\n");
        scintilla_notcurses_shutdown();
        return 1;
    }

    /* Load file if provided */
    if (argc > 1) {
        FILE *f = fopen(argv[1], "r");
        if (f) {
            fseek(f, 0, SEEK_END);
            long size = ftell(f);
            fseek(f, 0, SEEK_SET);
            char *buf = malloc((size_t)size + 1);
            if (buf) {
                fread(buf, 1, (size_t)size, f);
                buf[size] = '\0';
                scintilla_send_message(editor, SCI_SETTEXT, 0, (sptr_t)buf);
                free(buf);
            }
            fclose(f);
        } else {
            fprintf(stderr, "Cannot open file: %s\n", argv[1]);
        }
    } else {
        scintilla_send_message(editor, SCI_SETTEXT, 0,
            (sptr_t)"Welcome to Scinterm NotCurses!\n\nPress Ctrl+Q to quit.\n");
    }

    scintilla_set_focus(editor, true);

    /* Get the nc context from the plane */
    struct ncplane *plane = scintilla_get_plane(editor);
    struct notcurses *nc = ncplane_notcurses(plane);

    /* Enable mouse */
    notcurses_mice_enable(nc, NCMICE_ALL_EVENTS);

    bool running = true;
    while (running) {
        scintilla_render(editor);
        notcurses_render(nc);
        scintilla_update_cursor(editor);

        ncinput input = {};
        uint32_t key = notcurses_get_blocking(nc, &input);
        if (key == (uint32_t)-1) break;

        /* Ctrl+Q to quit */
        if (key == 'q' && (input.modifiers & NCKEY_MOD_CTRL)) {
            running = false;
        } else if (key == NCKEY_RESIZE) {
            notcurses_refresh(nc, NULL, NULL);
            scintilla_resize(editor);
        } else {
            scintilla_send_key(editor, (int)key, (int)input.modifiers);
        }
    }

    scintilla_delete(editor);
    scintilla_notcurses_shutdown();
    return 0;
}

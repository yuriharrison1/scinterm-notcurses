/**
 * @file syntax_highlighting.c
 * @brief Scinterm NotCurses syntax highlighting example (C language)
 * Copyright 2012-2026 Mitchell. See LICENSE.
 */

#include "scinterm_notcurses.h"
#include <notcurses/notcurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

/* Style IDs for C/C++ lexer (SCE_C_*) */
#define SCE_C_DEFAULT       0
#define SCE_C_COMMENT       1
#define SCE_C_COMMENTLINE   2
#define SCE_C_NUMBER        4
#define SCE_C_WORD          5
#define SCE_C_STRING        6
#define SCE_C_PREPROCESSOR  9
#define SCE_C_OPERATOR      10
#define SCE_C_IDENTIFIER    11

/* Modern Scintilla uses SCI_SETILEXER (4033) and ILexer5 interface.
 * SCI_SETLEXERLANGUAGE (4006) can set by name when lexers are compiled in. */
#ifndef SCI_SETLEXERLANGUAGE
#define SCI_SETLEXERLANGUAGE 4006
#endif

static void on_notify(void *sci, int msg, SCNotification *n, void *ud) {
    (void)sci; (void)msg; (void)n; (void)ud;
}

static void setup_c_highlighting(void *editor) {
    /* Set the lexer to C/C++ by language name (requires lexers compiled in) */
    scintilla_send_message(editor, SCI_SETLEXERLANGUAGE, 0, (sptr_t)"cpp");

    /* Default style */
    scintilla_send_message(editor, SCI_STYLESETFORE, STYLE_DEFAULT,
                           0xD4D4D4);  /* light grey */
    scintilla_send_message(editor, SCI_STYLESETBACK, STYLE_DEFAULT,
                           0x1E1E1E);  /* dark background */

    /* Copy default to all styles */
    scintilla_send_message(editor, SCI_STYLECLEARALL, 0, 0);

    /* Comments: green */
    scintilla_send_message(editor, SCI_STYLESETFORE, SCE_C_COMMENT,      0x6A9955);
    scintilla_send_message(editor, SCI_STYLESETFORE, SCE_C_COMMENTLINE,  0x6A9955);

    /* Numbers: light green */
    scintilla_send_message(editor, SCI_STYLESETFORE, SCE_C_NUMBER, 0xB5CEA8);

    /* Keywords: blue */
    scintilla_send_message(editor, SCI_STYLESETFORE, SCE_C_WORD, 0x569CD6);
    scintilla_send_message(editor, SCI_STYLESETBOLD, SCE_C_WORD, 1);

    /* Strings: orange */
    scintilla_send_message(editor, SCI_STYLESETFORE, SCE_C_STRING, 0xCE9178);

    /* Preprocessor: purple */
    scintilla_send_message(editor, SCI_STYLESETFORE, SCE_C_PREPROCESSOR, 0xC586C0);

    /* Operators: white */
    scintilla_send_message(editor, SCI_STYLESETFORE, SCE_C_OPERATOR, 0xD4D4D4);

    /* Set keywords */
    scintilla_send_message(editor, SCI_SETKEYWORDS, 0,
        (sptr_t)"auto break case char const continue default do double else "
                "enum extern float for goto if inline int long register return "
                "short signed sizeof static struct switch typedef union unsigned "
                "void volatile while");
}

static const char *sample_code =
    "#include <stdio.h>\n"
    "#include <stdlib.h>\n"
    "\n"
    "/* A simple example program */\n"
    "int main(int argc, char *argv[]) {\n"
    "    int i;\n"
    "    const char *msg = \"Hello, World!\";\n"
    "\n"
    "    // Print a greeting\n"
    "    for (i = 0; i < 10; i++) {\n"
    "        printf(\"%s (%d)\\n\", msg, i);\n"
    "    }\n"
    "\n"
    "    if (argc > 1) {\n"
    "        printf(\"Arg: %s\\n\", argv[1]);\n"
    "    }\n"
    "\n"
    "    return 0;\n"
    "}\n";

int main(void) {
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

    setup_c_highlighting(editor);

    scintilla_send_message(editor, SCI_SETTEXT, 0, (sptr_t)sample_code);
    scintilla_set_focus(editor, true);

    struct ncplane *plane = scintilla_get_plane(editor);
    struct notcurses *nc = ncplane_notcurses(plane);

    notcurses_mice_enable(nc, NCMICE_ALL_EVENTS);
    notcurses_linesigs_disable(nc);

    bool running = true;
    while (running) {
        scintilla_render(editor);
        scintilla_update_cursor(editor);
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
            scintilla_resize(editor);
        } else {
            scintilla_send_key(editor, (int)key, (int)input.modifiers);
        }
    }

    scintilla_delete(editor);
    scintilla_notcurses_shutdown();
    return 0;
}

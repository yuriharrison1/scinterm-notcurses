/**
 * @file syntax_highlighting.c
 * @brief Scinterm NotCurses syntax highlighting example.
 *
 * When built with -DENABLE_SCINTILLUA=ON, uses Scintillua (Lua/LPeg-based
 * lexers, same as textadept) for real syntax highlighting. Otherwise falls
 * back to manual C/C++ keyword styling without a real lexer.
 *
 * Copyright 2012-2026 Mitchell. See LICENSE.
 */

#include "scinterm_notcurses.h"
#include <notcurses/notcurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

/* SCE_C_* style IDs for the built-in C/C++ lexer (non-Scintillua path) */
#define SCE_C_COMMENT       1
#define SCE_C_COMMENTLINE   2
#define SCE_C_NUMBER        4
#define SCE_C_WORD          5
#define SCE_C_STRING        6
#define SCE_C_PREPROCESSOR  9
#define SCE_C_OPERATOR      10

#ifndef SCI_SETLEXERLANGUAGE
#define SCI_SETLEXERLANGUAGE 4006
#endif

static void on_notify(ScintillaHandle *sci, int msg, SCNotification *n, void *ud) {
    (void)sci; (void)msg; (void)n; (void)ud;
}

/* -----------------------------------------------------------------------
 * VS Code Dark+ color palette used for both code paths.
 * ----------------------------------------------------------------------- */
#define COLOR_DEFAULT    0xD4D4D4  /* light grey  */
#define COLOR_BG         0x1E1E1E  /* dark bg     */
#define COLOR_COMMENT    0x6A9955  /* green       */
#define COLOR_STRING     0xCE9178  /* orange      */
#define COLOR_NUMBER     0xB5CEA8  /* light green */
#define COLOR_KEYWORD    0x569CD6  /* blue        */
#define COLOR_TYPE       0x4EC9B0  /* teal        */
#define COLOR_FUNCTION   0xDCDCAA  /* yellow      */
#define COLOR_PREPROC    0xC586C0  /* purple      */
#define COLOR_CONSTANT   0x4FC1FF  /* light blue  */
#define COLOR_VARIABLE   0x9CDCFE  /* sky blue    */

/* -----------------------------------------------------------------------
 * Scintillua path: enumerate named styles and assign colors.
 * ----------------------------------------------------------------------- */
#ifdef ENABLE_SCINTILLUA
static struct { const char *name; int color; bool bold; } scintillua_styles[] = {
    /* Token name             Foreground color   Bold */
    { "annotation",           COLOR_PREPROC,     false },
    { "attribute",            0x9CDCFE,          false },
    { "class",                0x4EC9B0,          false },
    { "comment",              COLOR_COMMENT,     false },
    { "constant",             COLOR_CONSTANT,    false },
    { "constant.builtin",     COLOR_CONSTANT,    false },
    { "embedded",             COLOR_PREPROC,     false },
    { "error",                0xFF0000,          false },
    { "function",             COLOR_FUNCTION,    false },
    { "function.builtin",     COLOR_FUNCTION,    false },
    { "function.method",      COLOR_FUNCTION,    false },
    { "identifier",           COLOR_DEFAULT,     false },
    { "keyword",              COLOR_KEYWORD,     true  },
    { "label",                COLOR_PREPROC,     false },
    { "number",               COLOR_NUMBER,      false },
    { "operator",             COLOR_DEFAULT,     false },
    { "preprocessor",         COLOR_PREPROC,     false },
    { "regex",                0xD16969,          false },
    { "string",               COLOR_STRING,      false },
    { "type",                 COLOR_TYPE,        false },
    { "variable",             COLOR_VARIABLE,    false },
    { "variable.builtin",     COLOR_VARIABLE,    false },
    { NULL, 0, false }
};

static void apply_scintillua_theme(void *editor) {
    scintilla_send_message(editor, SCI_STYLESETFORE, STYLE_DEFAULT, COLOR_DEFAULT);
    scintilla_send_message(editor, SCI_STYLESETBACK, STYLE_DEFAULT, COLOR_BG);
    scintilla_send_message(editor, SCI_STYLECLEARALL, 0, 0);

    int n = (int)scintilla_send_message(editor, SCI_GETNAMEDSTYLES, 0, 0);
    char name[128];
    for (int i = 0; i < n; i++) {
        name[0] = '\0';
        scintilla_send_message(editor, SCI_NAMEOFSTYLE, (uptr_t)i, (sptr_t)name);
        if (!name[0]) continue;
        for (int j = 0; scintillua_styles[j].name; j++) {
            if (strcmp(name, scintillua_styles[j].name) == 0) {
                scintilla_send_message(editor, SCI_STYLESETFORE, i,
                                       scintillua_styles[j].color);
                if (scintillua_styles[j].bold)
                    scintilla_send_message(editor, SCI_STYLESETBOLD, i, 1);
                break;
            }
        }
    }
}
#endif /* ENABLE_SCINTILLUA */

/* -----------------------------------------------------------------------
 * Fallback path: manual keyword/color setup without a real lexer.
 * ----------------------------------------------------------------------- */
#ifndef ENABLE_SCINTILLUA
static void apply_builtin_c_theme(void *editor) {
    scintilla_send_message(editor, SCI_SETLEXERLANGUAGE, 0, (sptr_t)"cpp");

    scintilla_send_message(editor, SCI_STYLESETFORE, STYLE_DEFAULT, COLOR_DEFAULT);
    scintilla_send_message(editor, SCI_STYLESETBACK, STYLE_DEFAULT, COLOR_BG);
    scintilla_send_message(editor, SCI_STYLECLEARALL, 0, 0);

    scintilla_send_message(editor, SCI_STYLESETFORE, SCE_C_COMMENT,     COLOR_COMMENT);
    scintilla_send_message(editor, SCI_STYLESETFORE, SCE_C_COMMENTLINE, COLOR_COMMENT);
    scintilla_send_message(editor, SCI_STYLESETFORE, SCE_C_NUMBER,      COLOR_NUMBER);
    scintilla_send_message(editor, SCI_STYLESETFORE, SCE_C_WORD,        COLOR_KEYWORD);
    scintilla_send_message(editor, SCI_STYLESETBOLD, SCE_C_WORD,        1);
    scintilla_send_message(editor, SCI_STYLESETFORE, SCE_C_STRING,      COLOR_STRING);
    scintilla_send_message(editor, SCI_STYLESETFORE, SCE_C_PREPROCESSOR,COLOR_PREPROC);
    scintilla_send_message(editor, SCI_STYLESETFORE, SCE_C_OPERATOR,    COLOR_DEFAULT);

    scintilla_send_message(editor, SCI_SETKEYWORDS, 0,
        (sptr_t)"auto break case char const continue default do double else "
                "enum extern float for goto if inline int long register return "
                "short signed sizeof static struct switch typedef union unsigned "
                "void volatile while");
}
#endif /* !ENABLE_SCINTILLUA */

/* -----------------------------------------------------------------------
 * Sample C code to display.
 * ----------------------------------------------------------------------- */
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

/* -----------------------------------------------------------------------
 * Entry point.
 * ----------------------------------------------------------------------- */
int main(void) {
    if (!scintilla_notcurses_init()) {
        fprintf(stderr, "Failed to initialize NotCurses\n");
        return 1;
    }

    ScintillaHandle *editor = scintilla_new(on_notify, NULL);
    if (!editor) {
        fprintf(stderr, "Failed to create editor\n");
        scintilla_notcurses_shutdown();
        return 1;
    }

    /* Load lexer and apply theme. */
#ifdef ENABLE_SCINTILLUA
#  ifndef SCINTILLUA_LEXERS_DIR
#    define SCINTILLUA_LEXERS_DIR NULL
#  endif
    if (!scintilla_set_lexer(editor, "c", SCINTILLUA_LEXERS_DIR)) {
        fprintf(stderr, "Warning: Scintillua 'c' lexer not loaded\n");
    }
    apply_scintillua_theme(editor);
#else
    apply_builtin_c_theme(editor);
#endif

    scintilla_send_message(editor, SCI_SETTEXT, 0, (sptr_t)sample_code);
    scintilla_set_focus(editor, true);

    struct ncplane *plane = scintilla_get_plane(editor);
    struct notcurses *nc = ncplane_notcurses(plane);

    notcurses_mice_enable(nc, NCMICE_ALL_EVENTS);

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

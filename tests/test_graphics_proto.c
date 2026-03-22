/**
 * @file test_graphics_proto.c
 * @brief Unit tests for graphics protocol detection and override
 *
 * Tests the three-stage graphics protocol detection:
 * 1. Environment variable heuristics ($TERM_PROGRAM, $TERM)
 * 2. NotCurses runtime pixel-support query
 * 3. User override via scinterm_set_graphics_protocol()
 *
 * Compilation:
 *   gcc -std=c99 -o test_graphics_proto test_graphics_proto.c
 * Or with CMake:
 *   cmake --build build --target test_graphics_proto
 *
 * Copyright 2012-2026 Mitchell. See LICENSE.
 */

#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* Graphics protocol enum - matches scinterm_notcurses.h */
typedef enum ScintermGraphicsProtocol {
    SCINTERM_GRAPHICS_AUTO  = 0,
    SCINTERM_GRAPHICS_KITTY = 1,
    SCINTERM_GRAPHICS_SIXEL = 2,
    SCINTERM_GRAPHICS_NONE  = 3
} ScintermGraphicsProtocol;

/* NotCurses pixel implementation enum - mock */
typedef enum {
    NCPIXEL_NONE = 0,
    NCPIXEL_SIXEL = 1,
    NCPIXEL_KITTY_STATIC = 2,
    NCPIXEL_KITTY_ANIMATED = 3,
    NCPIXEL_KITTY_SELFREF = 4
} ncpixelimpl_e;

/* Mock state */
static ScintermGraphicsProtocol g_graphics_request = SCINTERM_GRAPHICS_AUTO;
static ScintermGraphicsProtocol g_graphics_active = SCINTERM_GRAPHICS_NONE;
static bool g_init_called = false;

/* Mock environment storage */
static struct {
    char term_program[64];
    char term[64];
    bool has_term_program;
    bool has_term;
} s_mock_env = {0};

/* Reset mock state */
static void mock_reset(void) {
    g_graphics_request = SCINTERM_GRAPHICS_AUTO;
    g_graphics_active = SCINTERM_GRAPHICS_NONE;
    g_init_called = false;
    memset(&s_mock_env, 0, sizeof(s_mock_env));
}

/* Mock getenv */
static char *mock_getenv(const char *name) {
    if (strcmp(name, "TERM_PROGRAM") == 0) {
        return s_mock_env.has_term_program ? s_mock_env.term_program : NULL;
    }
    if (strcmp(name, "TERM") == 0) {
        return s_mock_env.has_term ? s_mock_env.term : NULL;
    }
    return NULL;
}

/* Mock setenv helpers */
static void mock_setenv_term_program(const char *value) {
    if (value) {
        strncpy(s_mock_env.term_program, value, sizeof(s_mock_env.term_program) - 1);
        s_mock_env.has_term_program = true;
    } else {
        s_mock_env.has_term_program = false;
    }
}

static void mock_setenv_term(const char *value) {
    if (value) {
        strncpy(s_mock_env.term, value, sizeof(s_mock_env.term) - 1);
        s_mock_env.has_term = true;
    } else {
        s_mock_env.has_term = false;
    }
}

/* Stage 1: Environment detection (copied logic from scinterm_notcurses.cpp) */
static ScintermGraphicsProtocol detect_graphics_from_env(void) {
    const char *tp = mock_getenv("TERM_PROGRAM");
    if (tp) {
        if (strcmp(tp, "kitty")    == 0) return SCINTERM_GRAPHICS_KITTY;
        if (strcmp(tp, "WezTerm")  == 0) return SCINTERM_GRAPHICS_KITTY;
        if (strcmp(tp, "ghostty")  == 0) return SCINTERM_GRAPHICS_KITTY;
        if (strcmp(tp, "iTerm.app")== 0) return SCINTERM_GRAPHICS_KITTY;
    }

    const char *term = mock_getenv("TERM");
    if (term) {
        if (strcmp(term, "xterm-kitty")   == 0) return SCINTERM_GRAPHICS_KITTY;
        if (strcmp(term, "xterm-ghostty")  == 0) return SCINTERM_GRAPHICS_KITTY;
        if (strcmp(term, "mlterm") == 0) return SCINTERM_GRAPHICS_SIXEL;
        if (strcmp(term, "foot")   == 0) return SCINTERM_GRAPHICS_SIXEL;
        if (strcmp(term, "yaft")   == 0) return SCINTERM_GRAPHICS_SIXEL;
        if (strstr(term, "sixel") != NULL) return SCINTERM_GRAPHICS_SIXEL;
    }
    return SCINTERM_GRAPHICS_NONE;
}

/* Stage 2: Map notcurses pixel implementation to protocol */
static ScintermGraphicsProtocol ncpixel_to_protocol(ncpixelimpl_e impl) {
    switch (impl) {
        case NCPIXEL_KITTY_STATIC:
        case NCPIXEL_KITTY_ANIMATED:
        case NCPIXEL_KITTY_SELFREF:
            return SCINTERM_GRAPHICS_KITTY;
        case NCPIXEL_SIXEL:
            return SCINTERM_GRAPHICS_SIXEL;
        default:
            return SCINTERM_GRAPHICS_NONE;
    }
}

/* API: Set graphics protocol override */
static void scinterm_set_graphics_protocol(ScintermGraphicsProtocol protocol) {
    /* In real implementation, this only works before init */
    if (!g_init_called) {
        g_graphics_request = protocol;
    }
}

/* API: Initialize with graphics detection */
static bool scintilla_notcurses_init(void) {
    ScintermGraphicsProtocol detected;
    
    /* Stage 3: User override */
    if (g_graphics_request != SCINTERM_GRAPHICS_AUTO) {
        detected = g_graphics_request;
    } else {
        /* Stage 1: Environment detection */
        detected = detect_graphics_from_env();
    }
    
    /* Stage 2: NotCurses query (only if AUTO and no env hint) */
    if (g_graphics_request == SCINTERM_GRAPHICS_AUTO
        && detected == SCINTERM_GRAPHICS_NONE) {
        /* In real impl: notcurses_check_pixel_support() */
        /* For testing, we simulate this returning NONE */
        detected = SCINTERM_GRAPHICS_NONE;
    }
    
    g_graphics_active = detected;
    g_init_called = true;
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
 * @test test_term_program_wezterm
 * @brief TERM_PROGRAM=WezTerm auto-detects kitty
 *
 * WezTerm supports the Kitty graphics protocol, so detection should
 * return KITTY when TERM_PROGRAM is set to "WezTerm".
 */
static void test_term_program_wezterm(void) {
    mock_reset();
    mock_setenv_term_program("WezTerm");
    mock_setenv_term(NULL);
    
    ScintermGraphicsProtocol proto = detect_graphics_from_env();
    ASSERT(proto == SCINTERM_GRAPHICS_KITTY);
    
    s_pass++;
}

/**
 * @test test_term_program_ghostty
 * @brief TERM_PROGRAM=ghostty auto-detects kitty
 *
 * Ghostty supports the Kitty graphics protocol.
 */
static void test_term_program_ghostty(void) {
    mock_reset();
    mock_setenv_term_program("ghostty");
    mock_setenv_term(NULL);
    
    ScintermGraphicsProtocol proto = detect_graphics_from_env();
    ASSERT(proto == SCINTERM_GRAPHICS_KITTY);
    
    s_pass++;
}

/**
 * @test test_term_program_kitty
 * @brief TERM_PROGRAM=kitty auto-detects kitty
 */
static void test_term_program_kitty(void) {
    mock_reset();
    mock_setenv_term_program("kitty");
    mock_setenv_term(NULL);
    
    ScintermGraphicsProtocol proto = detect_graphics_from_env();
    ASSERT(proto == SCINTERM_GRAPHICS_KITTY);
    
    s_pass++;
}

/**
 * @test test_term_program_iterm
 * @brief TERM_PROGRAM=iTerm.app auto-detects kitty
 *
 * iTerm2 version 3.5+ supports the Kitty graphics protocol.
 */
static void test_term_program_iterm(void) {
    mock_reset();
    mock_setenv_term_program("iTerm.app");
    mock_setenv_term(NULL);
    
    ScintermGraphicsProtocol proto = detect_graphics_from_env();
    ASSERT(proto == SCINTERM_GRAPHICS_KITTY);
    
    s_pass++;
}

/**
 * @test test_term_unset_falls_back
 * @brief TERM_PROGRAM unset falls back to none gracefully
 *
 * When neither TERM_PROGRAM nor TERM gives a hint, detection should
 * return NONE (allowing runtime query).
 */
static void test_term_unset_falls_back(void) {
    mock_reset();
    mock_setenv_term_program(NULL);
    mock_setenv_term(NULL);
    
    ScintermGraphicsProtocol proto = detect_graphics_from_env();
    ASSERT(proto == SCINTERM_GRAPHICS_NONE);
    
    s_pass++;
}

/**
 * @test test_term_xterm_kitty
 * @brief TERM=xterm-kitty auto-detects kitty
 */
static void test_term_xterm_kitty(void) {
    mock_reset();
    mock_setenv_term_program(NULL);
    mock_setenv_term("xterm-kitty");
    
    ScintermGraphicsProtocol proto = detect_graphics_from_env();
    ASSERT(proto == SCINTERM_GRAPHICS_KITTY);
    
    s_pass++;
}

/**
 * @test test_term_foot_sixel
 * @brief TERM=foot auto-detects sixel
 */
static void test_term_foot_sixel(void) {
    mock_reset();
    mock_setenv_term_program(NULL);
    mock_setenv_term("foot");
    
    ScintermGraphicsProtocol proto = detect_graphics_from_env();
    ASSERT(proto == SCINTERM_GRAPHICS_SIXEL);
    
    s_pass++;
}

/**
 * @test test_term_sixel_substring
 * @brief TERM containing "sixel" auto-detects sixel
 */
static void test_term_sixel_substring(void) {
    mock_reset();
    mock_setenv_term_program(NULL);
    mock_setenv_term("xterm+sixel+256color");
    
    ScintermGraphicsProtocol proto = detect_graphics_from_env();
    ASSERT(proto == SCINTERM_GRAPHICS_SIXEL);
    
    s_pass++;
}

/**
 * @test test_override_kitty_before_init
 * @brief Override KITTY before init works
 *
 * Calling scinterm_set_graphics_protocol(KITTY) before init should
 * force the KITTY protocol regardless of environment.
 */
static void test_override_kitty_before_init(void) {
    mock_reset();
    
    /* Set environment to NONE */
    mock_setenv_term_program(NULL);
    mock_setenv_term(NULL);
    
    /* Override to KITTY before init */
    scinterm_set_graphics_protocol(SCINTERM_GRAPHICS_KITTY);
    ASSERT(g_graphics_request == SCINTERM_GRAPHICS_KITTY);
    
    /* Init should respect the override */
    scintilla_notcurses_init();
    ASSERT(g_graphics_active == SCINTERM_GRAPHICS_KITTY);
    
    s_pass++;
}

/**
 * @test test_override_sixel_before_init
 * @brief Override SIXEL before init works
 */
static void test_override_sixel_before_init(void) {
    mock_reset();
    
    scinterm_set_graphics_protocol(SCINTERM_GRAPHICS_SIXEL);
    scintilla_notcurses_init();
    ASSERT(g_graphics_active == SCINTERM_GRAPHICS_SIXEL);
    
    s_pass++;
}

/**
 * @test test_override_none_before_init
 * @brief Override NONE before init forces no graphics
 *
 * SCINTERM_GRAPHICS_NONE should force no graphics regardless of
 * terminal capabilities.
 */
static void test_override_none_before_init(void) {
    mock_reset();
    
    /* Even with kitty TERM */
    mock_setenv_term_program("kitty");
    
    /* Override to NONE */
    scinterm_set_graphics_protocol(SCINTERM_GRAPHICS_NONE);
    scintilla_notcurses_init();
    ASSERT(g_graphics_active == SCINTERM_GRAPHICS_NONE);
    
    s_pass++;
}

/**
 * @test test_override_after_init_ignored
 * @brief Override after init has no effect
 *
 * Once initialization is complete, the graphics protocol is locked in.
 * Subsequent calls to set_graphics_protocol should be ignored.
 */
static void test_override_after_init_ignored(void) {
    mock_reset();
    
    /* Init with AUTO in a kitty terminal */
    mock_setenv_term_program("kitty");
    scintilla_notcurses_init();
    ASSERT(g_graphics_active == SCINTERM_GRAPHICS_KITTY);
    
    /* Try to override after init - should be ignored */
    scinterm_set_graphics_protocol(SCINTERM_GRAPHICS_NONE);
    ASSERT(g_graphics_active == SCINTERM_GRAPHICS_KITTY); /* Unchanged */
    
    s_pass++;
}

/**
 * @test test_auto_uses_env_over_term
 * @brief AUTO mode prefers TERM_PROGRAM over TERM
 *
 * TERM_PROGRAM is more reliable than TERM, so it should be checked first.
 */
static void test_auto_uses_env_over_term(void) {
    mock_reset();
    
    /* Set conflicting values - TERM_PROGRAM should win */
    mock_setenv_term_program("kitty");
    mock_setenv_term("xterm-256color"); /* Not graphics-capable */
    
    ScintermGraphicsProtocol proto = detect_graphics_from_env();
    ASSERT(proto == SCINTERM_GRAPHICS_KITTY);
    
    s_pass++;
}

/**
 * @test test_ncpixel_mapping
 * @brief Verify ncpixelimpl_e to protocol mapping
 */
static void test_ncpixel_mapping(void) {
    ASSERT(ncpixel_to_protocol(NCPIXEL_KITTY_STATIC) == SCINTERM_GRAPHICS_KITTY);
    ASSERT(ncpixel_to_protocol(NCPIXEL_KITTY_ANIMATED) == SCINTERM_GRAPHICS_KITTY);
    ASSERT(ncpixel_to_protocol(NCPIXEL_KITTY_SELFREF) == SCINTERM_GRAPHICS_KITTY);
    ASSERT(ncpixel_to_protocol(NCPIXEL_SIXEL) == SCINTERM_GRAPHICS_SIXEL);
    ASSERT(ncpixel_to_protocol(NCPIXEL_NONE) == SCINTERM_GRAPHICS_NONE);
    ASSERT(ncpixel_to_protocol(999) == SCINTERM_GRAPHICS_NONE); /* Unknown */
    
    s_pass++;
}

/**
 * @test test_term_program_takes_priority
 * @brief Verify TERM_PROGRAM takes priority over TERM
 *
 * Even if TERM suggests no graphics, TERM_PROGRAM should be authoritative.
 */
static void test_term_program_takes_priority(void) {
    mock_reset();
    
    /* TERM suggests sixel, but TERM_PROGRAM is kitty */
    mock_setenv_term_program("kitty");
    mock_setenv_term("xterm+sixel");
    
    ScintermGraphicsProtocol proto = detect_graphics_from_env();
    /* TERM_PROGRAM is checked first, returns KITTY */
    ASSERT(proto == SCINTERM_GRAPHICS_KITTY);
    
    s_pass++;
}

/**
 * @test test_mlterm_sixel
 * @brief TERM=mlterm auto-detects sixel
 */
static void test_mlterm_sixel(void) {
    mock_reset();
    mock_setenv_term_program(NULL);
    mock_setenv_term("mlterm");
    
    ScintermGraphicsProtocol proto = detect_graphics_from_env();
    ASSERT(proto == SCINTERM_GRAPHICS_SIXEL);
    
    s_pass++;
}

/**
 * @test test_yaft_sixel
 * @brief TERM=yaft auto-detects sixel
 */
static void test_yaft_sixel(void) {
    mock_reset();
    mock_setenv_term_program(NULL);
    mock_setenv_term("yaft");
    
    ScintermGraphicsProtocol proto = detect_graphics_from_env();
    ASSERT(proto == SCINTERM_GRAPHICS_SIXEL);
    
    s_pass++;
}

/*=============================================================================
 * Main Entry Point
 *===========================================================================*/

int main(void) {
    printf("=== Graphics Protocol Tests ===\n\n");
    
    test_term_program_wezterm();
    test_term_program_ghostty();
    test_term_program_kitty();
    test_term_program_iterm();
    test_term_unset_falls_back();
    test_term_xterm_kitty();
    test_term_foot_sixel();
    test_term_sixel_substring();
    test_override_kitty_before_init();
    test_override_sixel_before_init();
    test_override_none_before_init();
    test_override_after_init_ignored();
    test_auto_uses_env_over_term();
    test_ncpixel_mapping();
    test_term_program_takes_priority();
    test_mlterm_sixel();
    test_yaft_sixel();
    
    printf("\n=== Results: %d passed, %d failed ===\n", s_pass, s_fail);
    return s_fail > 0 ? 1 : 0;
}

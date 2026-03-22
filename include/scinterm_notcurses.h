/**
 * @file scinterm_notcurses.h
 * @brief Scinterm NotCurses - Main public API header
 * 
 * Scinterm is a Scintilla-based text editor widget for terminal environments.
 * This version uses NotCurses as the backend, providing true color support,
 * Unicode handling, and advanced terminal features.
 * 
 * Copyright 2012-2026 Mitchell. See LICENSE.
 */

#ifndef SCINTERM_NOTCURSES_H
#define SCINTERM_NOTCURSES_H

#include <notcurses/notcurses.h>
#include <stdbool.h>
#include <stddef.h>
#include "Scintilla.h"

/*=============================================================================
 * Graphics protocol
 *
 * Defined outside extern "C" so it is a plain C typedef enum that compiles
 * identically in both C and C++ translation units.
 *===========================================================================*/

/**
 * @brief Terminal pixel-graphics capability level.
 *
 * Scinterm itself renders only text, but the protocol it reports to NotCurses
 * influences the NCOPTION flags passed to notcurses_init() and therefore
 * how NotCurses manages pixel-graphics content already on screen (e.g. inline
 * images drawn by the shell before the editor launched).
 *
 * Detection order (AUTO):
 *   1. $TERM_PROGRAM: "kitty" / "WezTerm" / "ghostty" / "iTerm.app" → KITTY
 *   2. $TERM: "xterm-kitty" / "xterm-ghostty" → KITTY
 *             "mlterm" / "foot" / "yaft" / contains "sixel" → SIXEL
 *   3. notcurses_check_pixel_support() at runtime for final resolution.
 *
 * Call scinterm_set_graphics_protocol() BEFORE scintilla_notcurses_init() to
 * override auto-detection.  If not called, SCINTERM_GRAPHICS_AUTO is used.
 */
typedef enum ScintermGraphicsProtocol {
    /** Detect from $TERM / $TERM_PROGRAM and notcurses capabilities (default). */
    SCINTERM_GRAPHICS_AUTO  = 0,
    /** Kitty Graphics Protocol (kitty, WezTerm, Ghostty, iTerm2 ≥ 3.5).      */
    SCINTERM_GRAPHICS_KITTY = 1,
    /** Sixel pixel graphics (mlterm, foot, xterm+sixel, yaft).                */
    SCINTERM_GRAPHICS_SIXEL = 2,
    /** No pixel graphics; plain text / ANSI only.                             */
    SCINTERM_GRAPHICS_NONE  = 3
} ScintermGraphicsProtocol;

#ifdef __cplusplus
extern "C" {
#endif

/*=============================================================================
 * Constants
 *===========================================================================*/

/** Maximum number of registered images for autocomplete */
#define IMAGE_MAX 31

/** Mouse button press event */
#define SCM_PRESS   1

/** Mouse drag event */
#define SCM_DRAG    2

/** Mouse button release event */
#define SCM_RELEASE 3

/*=============================================================================
 * Graphics protocol override
 *===========================================================================*/

/**
 * @brief Override the graphics protocol used during NotCurses initialization.
 *
 * Must be called BEFORE scintilla_notcurses_init().  After initialization the
 * value is locked in; calling this function afterwards has no effect.
 *
 * @param protocol  Desired protocol, or SCINTERM_GRAPHICS_AUTO (default) to
 *                  let Scinterm detect it from environment variables and the
 *                  notcurses pixel-support query.
 */
void scinterm_set_graphics_protocol(ScintermGraphicsProtocol protocol);

/*=============================================================================
 * Initialization and cleanup
 *===========================================================================*/

/**
 * @brief Initialize the NotCurses library for Scinterm.
 * 
 * This function must be called before creating any Scinterm windows.
 * It sets up the locale, initializes NotCurses, and prepares the environment.
 * 
 * @return true if initialization succeeded, false otherwise.
 */
bool scintilla_notcurses_init(void);

/**
 * @brief Shut down the NotCurses library.
 * 
 * This function should be called after all Scinterm windows are deleted,
 * before the program exits.
 */
void scintilla_notcurses_shutdown(void);

/*=============================================================================
 * Scinterm instance management
 *===========================================================================*/

/**
 * @brief Opaque handle to a Scinterm editor instance
 * 
 * ARCHITECTURE: Provides type safety over raw void* pointers.
 * The actual implementation is internal and subject to change.
 */
typedef struct ScintillaNotCurses ScintillaHandle;

/**
 * @brief Notification callback signature
 * @param sci Editor instance that sent the notification
 * @param iMessage Message code (see ScintillaMessages.h)
 * @param n Notification data structure
 * @param userdata User data passed during creation
 */
typedef void (*ScintillaCallback)(ScintillaHandle *sci, int iMessage, 
                                   SCNotification *n, void *userdata);

/**
 * @brief Create a new Scinterm editor instance.
 * 
 * @param callback Notification callback function. Called for various Scintilla events.
 * @param userdata User data to pass to the callback.
 * @return Handle to the Scinterm instance, or NULL on failure.
 */
ScintillaHandle *scintilla_new(ScintillaCallback callback, void *userdata);

/**
 * @brief Get the NotCurses plane associated with a Scinterm instance.
 * 
 * @param sci Scinterm instance handle.
 * @return NotCurses plane, or NULL if not created yet.
 */
struct ncplane *scintilla_get_plane(ScintillaHandle *sci);

/**
 * @brief Delete a Scinterm instance and free all resources.
 * 
 * @param sci Scinterm instance handle to delete.
 */
void scintilla_delete(ScintillaHandle *sci);

/*=============================================================================
 * Scintilla message passing
 *===========================================================================*/

/**
 * @brief Send a message to the Scintilla core.
 * 
 * This is the main communication channel with Scintilla. All operations
 * (setting text, styling, cursor movement, etc.) are performed via messages.
 * 
 * @param sci Scinterm instance.
 * @param iMessage Message ID (see ScintillaMessages.h).
 * @param wParam First message parameter.
 * @param lParam Second message parameter.
 * @return Message-specific return value.
 */
sptr_t scintilla_send_message(ScintillaHandle *sci, unsigned int iMessage, uptr_t wParam, sptr_t lParam);

/*=============================================================================
 * Input handling
 *===========================================================================*/

/**
 * @brief Send a key event to Scinterm.
 * 
 * @param sci Scinterm instance.
 * @param key Key code (using NotCurses key definitions from <notcurses/notcurses.h>).
 * @param modifiers Bitmask of SCMOD_* modifiers (Ctrl, Alt, Shift).
 */
void scintilla_send_key(ScintillaHandle *sci, int key, int modifiers);

/**
 * @brief Send a mouse event to Scinterm.
 * 
 * @param sci Scinterm instance.
 * @param event Mouse event type: SCM_PRESS, SCM_DRAG, or SCM_RELEASE.
 * @param button Button number (1 for left, 2 for middle, 3 for right, 4-5 for scroll).
 * @param modifiers Bitmask of SCMOD_* modifiers.
 * @param y Absolute Y coordinate (screen coordinates).
 * @param x Absolute X coordinate (screen coordinates).
 * @return true if the event was handled, false otherwise.
 */
bool scintilla_send_mouse(ScintillaHandle *sci, int event, int button, int modifiers, int y, int x);

/**
 * @brief Process input from NotCurses and dispatch to Scinterm.
 * 
 * This is a convenience function that reads input from NotCurses and
 * automatically converts it to the appropriate Scinterm events.
 * 
 * @param sci Scinterm instance.
 * @param nc NotCurses context.
 * @return true if the input was handled, false otherwise.
 */
bool scintilla_process_input(ScintillaHandle *sci, struct notcurses *nc);

/*=============================================================================
 * Rendering and display
 *===========================================================================*/

/**
 * @brief Render the Scinterm content to its NotCurses plane.
 * 
 * This function should be called in the main loop whenever the content
 * might have changed. It updates the plane's content but does not call
 * notcurses_render() - that must be done separately.
 * 
 * @param sci Scinterm instance.
 */
void scintilla_render(ScintillaHandle *sci);

/**
 * @brief Update the cursor position.
 * 
 * Ensures the terminal cursor is at the correct position for the current
 * caret location. Should be called after rendering if the cursor might
 * have moved.
 * 
 * @param sci Scinterm instance.
 */
void scintilla_update_cursor(ScintillaHandle *sci);

/**
 * @brief Handle terminal resize events.
 * 
 * Should be called when the terminal is resized (e.g., on NCKEY_RESIZE).
 * Updates the Scinterm internal state to match the new dimensions.
 * 
 * @param sci Scinterm instance.
 */
void scintilla_resize(ScintillaHandle *sci);

/**
 * @brief Set focus state for the Scinterm instance.
 * 
 * Affects cursor visibility and selection rendering.
 * 
 * @param sci Scinterm instance.
 * @param focus true if the window has focus, false otherwise.
 */
void scintilla_set_focus(ScintillaHandle *sci, bool focus);

/*=============================================================================
 * Clipboard operations
 *===========================================================================*/

/**
 * @brief Get the current clipboard text from Scinterm.
 * 
 * The caller is responsible for freeing the returned string with free().
 * 
 * @param sci Scinterm instance.
 * @param len Optional pointer to store the length of the text.
 * @return Allocated string containing the clipboard text, or NULL on error.
 */
char *scintilla_get_clipboard(ScintillaHandle *sci, int *len);

/*=============================================================================
 * Compatibility functions
 *===========================================================================*/

/**
 * @brief Set color offsets (compatibility with curses version).
 * 
 * NotCurses supports true color, so this function does nothing but is
 * provided for compatibility with existing Scinterm applications.
 * 
 * @param color_offset Ignored.
 * @param pair_offset Ignored.
 */
void scintilla_set_color_offsets(int color_offset, int pair_offset);

/**
 * @brief Set the background transparency level for all Scintilla rendering.
 *
 * Controls how Scintilla's background cells blend with the terminal background:
 *   0        - fully opaque (theme background colour shown)
 *   1 .. 99  - blend mode (50/50 mix with the plane below)
 *   100      - fully transparent (terminal background shows through)
 *
 * @param pct Transparency percentage [0, 100].
 */
void scintilla_set_bg_alpha(int pct);

/*=============================================================================
 * Syntax highlighting via Scintillua
 *===========================================================================*/

/**
 * @brief Set a Scintillua Lua-based lexer for syntax highlighting.
 *
 * Requires the library to be built with -DENABLE_SCINTILLUA=ON.
 *
 * @param sci        Scinterm instance.
 * @param name       Language name (e.g. "c", "python", "bash"). Must match a
 *                   .lua file in the lexers directory.
 * @param lexers_dir Path to the Scintillua lexers/ directory, or NULL to
 *                   reuse the last path set.
 * @return true if the lexer was set successfully, false otherwise.
 */
bool scintilla_set_lexer(ScintillaHandle *sci, const char *name, const char *lexers_dir);

#ifdef __cplusplus
}
#endif

#endif /* SCINTERM_NOTCURSES_H */

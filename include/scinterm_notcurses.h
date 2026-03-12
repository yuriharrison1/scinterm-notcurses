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
 * @brief Create a new Scinterm editor instance.
 * 
 * @param callback Notification callback function. Called for various Scintilla events.
 * @param userdata User data to pass to the callback.
 * @return Pointer to the Scinterm instance, or NULL on failure.
 */
void *scintilla_new(
    void (*callback)(void *sci, int iMessage, SCNotification *n, void *userdata), 
    void *userdata);

/**
 * @brief Get the NotCurses plane associated with a Scinterm instance.
 * 
 * @param sci Scinterm instance.
 * @return NotCurses plane, or NULL if not created yet.
 */
struct ncplane *scintilla_get_plane(void *sci);

/**
 * @brief Delete a Scinterm instance and free all resources.
 * 
 * @param sci Scinterm instance to delete.
 */
void scintilla_delete(void *sci);

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
sptr_t scintilla_send_message(void *sci, unsigned int iMessage, uptr_t wParam, sptr_t lParam);

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
void scintilla_send_key(void *sci, int key, int modifiers);

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
bool scintilla_send_mouse(void *sci, int event, int button, int modifiers, int y, int x);

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
bool scintilla_process_input(void *sci, struct notcurses *nc);

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
void scintilla_render(void *sci);

/**
 * @brief Update the cursor position.
 * 
 * Ensures the terminal cursor is at the correct position for the current
 * caret location. Should be called after rendering if the cursor might
 * have moved.
 * 
 * @param sci Scinterm instance.
 */
void scintilla_update_cursor(void *sci);

/**
 * @brief Handle terminal resize events.
 * 
 * Should be called when the terminal is resized (e.g., on NCKEY_RESIZE).
 * Updates the Scinterm internal state to match the new dimensions.
 * 
 * @param sci Scinterm instance.
 */
void scintilla_resize(void *sci);

/**
 * @brief Set focus state for the Scinterm instance.
 * 
 * Affects cursor visibility and selection rendering.
 * 
 * @param sci Scinterm instance.
 * @param focus true if the window has focus, false otherwise.
 */
void scintilla_set_focus(void *sci, bool focus);

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
char *scintilla_get_clipboard(void *sci, int *len);

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

#ifdef __cplusplus
}
#endif

#endif /* SCINTERM_NOTCURSES_H */

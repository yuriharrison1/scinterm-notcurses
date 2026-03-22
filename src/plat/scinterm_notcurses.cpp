/**
 * @file scinterm_notcurses.cpp
 * @brief Main ScintillaNotCurses implementation
 *
 * Implements the ScintillaNotCurses class which bridges Scintilla's editor
 * core with the NotCurses terminal rendering library.
 * Copyright 2012-2026 Mitchell. See LICENSE.
 */

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <cassert>
#include <cctype>
#include <ctime>        /* clock_gettime, nanosleep, struct timespec, CLOCK_MONOTONIC */
#include <cerrno>       /* errno, EINTR */

#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include <map>
#include <set>
#include <forward_list>
#include <optional>
#include <algorithm>
#include <iterator>
#include <memory>
#include <chrono>
#include <atomic>
#include <mutex>

#include <notcurses/notcurses.h>

#ifdef ENABLE_SCINTILLUA
#include "Scintillua.h"
#endif

#include "ScintillaTypes.h"
#include "ScintillaMessages.h"
#include "ScintillaStructures.h"
#include "ILoader.h"
#include "ILexer.h"

#include "Debugging.h"
#include "Geometry.h"
#include "Platform.h"

#include "CharacterType.h"
#include "CharacterCategoryMap.h"
#include "Position.h"
#include "UniqueString.h"
#include "SplitVector.h"
#include "Partitioning.h"
#include "RunStyles.h"
#include "ContractionState.h"
#include "CellBuffer.h"
#include "PerLine.h"
#include "CallTip.h"
#include "KeyMap.h"
#include "Indicator.h"
#include "LineMarker.h"
#include "Style.h"
#include "ViewStyle.h"
#include "CharClassify.h"
#include "Decoration.h"
#include "CaseFolder.h"
#include "Document.h"
#include "UniConversion.h"
#include "DBCS.h"
#include "Selection.h"
#include "PositionCache.h"
#include "EditModel.h"
#include "MarginView.h"
#include "EditView.h"
#include "Editor.h"
#include "AutoComplete.h"
#include "ScintillaBase.h"
#include "XPM.h"

#include "scinterm_plat.h"
#include "scinterm_wcwidth.h"
#include "scinterm_notcurses.h"

using namespace Scintilla;
using namespace Scintilla::Internal;

/*=============================================================================
 * Thread-safe clipboard implementation
 *===========================================================================*/

class ThreadSafeClipboard {
private:
    std::string data;
    std::mutex mtx;

public:
    void set(const std::string_view &text) {
        std::lock_guard<std::mutex> lock(mtx);
        data = std::string(text);
    }
    
    std::string get() {
        std::lock_guard<std::mutex> lock(mtx);
        return data;
    }
    
    bool empty() {
        std::lock_guard<std::mutex> lock(mtx);
        return data.empty();
    }
    
    void clear() {
        std::lock_guard<std::mutex> lock(mtx);
        data.clear();
    }
};

static ThreadSafeClipboard g_clipboard;

/*=============================================================================
 * Frame-rate throttle helpers with robust timing
 *
 * Uses CLOCK_MONOTONIC (no leap-second jumps, no NTP steps) for stable
 * inter-frame interval measurement.  All arithmetic is in nanoseconds to
 * stay in integer math.
 *===========================================================================*/

/* Target inter-frame interval: e.g. 16 666 667 ns at 60 fps. */
static constexpr long long FRAME_NS = 1'000'000'000LL / SCINTERM_TARGET_FPS;

/* Read the monotonic clock. Returns true on success, false on failure. */
static inline bool sc_now(struct timespec *ts) noexcept {
    if (clock_gettime(CLOCK_MONOTONIC, ts) != 0) {
        // Fallback to CLOCK_REALTIME if MONOTONIC unavailable
        if (clock_gettime(CLOCK_REALTIME, ts) != 0) {
            ts->tv_sec = 0;
            ts->tv_nsec = 0;
            return false;
        }
    }
    return true;
}

/* Signed difference (a − b) in nanoseconds.  Negative when b is later. */
static inline long long sc_diff_ns(const struct timespec &a,
                                   const struct timespec &b) noexcept {
    return (static_cast<long long>(a.tv_sec)  - static_cast<long long>(b.tv_sec))
           * 1'000'000'000LL
         + (static_cast<long long>(a.tv_nsec) - static_cast<long long>(b.tv_nsec));
}

/* Robust nanosleep that handles EINTR correctly */
static inline void sc_nanosleep(long long ns) noexcept {
    struct timespec req = { 0L, static_cast<long>(ns) };
    struct timespec rem;
    
    while (nanosleep(&req, &rem) == -1) {
        if (errno == EINTR) {
            req = rem;
        } else {
            break;
        }
    }
}

/*=============================================================================
 * Fold marker drawing callback
 *===========================================================================*/

/* Called by Scintilla's LineMarker::Draw() when customDraw is set.
 * Casts the surface to SurfaceImpl and delegates to DrawLineMarker()
 * which renders an ASCII/Unicode character in the 1-cell fold margin. */
static void DrawFoldMarkerCallback(Surface *surface, const PRectangle &rcWhole,
                                   const Font *fontForCharacter, int tFold,
                                   Scintilla::MarginType /*marginStyle*/,
                                   const void *lineMarker) {
    auto *surf = static_cast<SurfaceImpl *>(surface);
    if (surf)
        surf->DrawLineMarker(rcWhole, fontForCharacter, tFold, lineMarker);
}

/*=============================================================================
 * ScintillaNotCurses class
 *===========================================================================*/

class ScintillaNotCurses : public ScintillaBase {
    struct ncplane *ncp;
    bool hasFocus;
    bool capturedMouse;
    /* Dirty flag: true when the plane needs a Scintilla repaint.
     * Starts true so the very first Render() always paints.
     * Internal only — never exposed through the public C API header. */
    bool dirty;
    /* Monotonic timestamp of the last completed render frame.
     * Initialised to {0,0} so the first Render() call always fires
     * immediately: sc_diff_ns(now, {0,0}) >> FRAME_NS.              */
    struct timespec lastRenderTime;
    void (*notifyCallback)(void *sci, int iMessage, SCNotification *n, void *userdata);
    void *notifyUserdata;

    /*=========================================================================
     * Dirty-tracking helpers (internal, not in public API)
     *
     * scinterm_mark_dirty() — set the dirty flag; called from every code path
     *   that changes visible state.
     * scinterm_is_dirty()   — predicate used by Render() to decide whether
     *   the Scintilla paint pass should be skipped.
     *=======================================================================*/
    void scinterm_mark_dirty() noexcept { dirty = true; }
    bool scinterm_is_dirty() const noexcept { return dirty; }

public:
    ScintillaNotCurses(void (*cb)(void *, int, SCNotification *, void *), void *ud);
    ~ScintillaNotCurses() override;

    struct ncplane *GetPlane() { return ncp; }

    /* Scintilla Editor virtuals */
    void Initialise() override;
    void Finalise() noexcept override;

    bool FineTickerRunning(TickReason /*reason*/) override { return false; }
    void FineTickerStart(TickReason /*reason*/, int /*millis*/, int /*tolerance*/) override {}
    void FineTickerCancel(TickReason /*reason*/) override {}
    bool SetIdle(bool /*on*/) override { return false; }
    void SetMouseCapture(bool on) override { capturedMouse = on; }
    bool HaveMouseCapture() override { return capturedMouse; }

    std::string UTF8FromEncoded(std::string_view encoded) const override {
        return std::string(encoded);
    }
    std::string EncodedFromUTF8(std::string_view utf8) const override {
        return std::string(utf8);
    }

    void SetVerticalScrollPos() override {}
    void SetHorizontalScrollPos() override {}
    bool ModifyScrollBars(Sci::Line /*nMax*/, Sci::Line /*nPage*/) override { return false; }
    void ReconfigureScrollBars() override {}

    /* Caret moved (cursor-key navigation, mouse click, programmatic move).
     * Marks dirty so Render() updates the visible cursor position.        */
    void UpdateSystemCaret() override { scinterm_mark_dirty(); }

    /* Document content changed (insert, delete, paste, undo/redo).        */
    void NotifyChange() override { scinterm_mark_dirty(); }

    /* Override Redraw() to catch all other visual-state changes:
     *   • Style changes:  SCI_STYLESETFORE/BACK → InvalidateStyleRedraw() → Redraw()
     *   • Scroll changes: ScrollTo() → Redraw()
     *   • Selection:      InvalidateSelection() paths that reach Redraw()
     * The base is called first so redrawPendingText is maintained correctly
     * for Scintilla's own Paint() internals.                               */
    void Redraw() override {
        Editor::Redraw();       /* sets redrawPendingText; no-op InvalidateRectangle */
        scinterm_mark_dirty();
    }

    void NotifyParent(Scintilla::NotificationData scn) override;

    void CopyToClipboard(const SelectionText &selectedText) override;
    void Copy() override;
    void Paste() override;
    void ClaimSelection() override {}

    void CreateCallTipWindow(PRectangle /*rc*/) override {}
    void AddToPopUp(const char * /*label*/, int /*cmd*/, bool /*enabled*/) override {}
    void StartDrag() override {}

    Scintilla::sptr_t DefWndProc(Scintilla::Message iMessage, Scintilla::uptr_t wParam,
                                 Scintilla::sptr_t lParam) override {
        (void)iMessage; (void)wParam; (void)lParam;
        return 0;
    }

    /* Public rendering/input methods */
    void Render();
    void UpdateCursor();
    void Resize();
    void SetFocus(bool focus);
    void SendKey(int key, int modifiers);
    bool SendMouse(int event, int button, int modifiers, int y, int x);
    bool ProcessInput(struct notcurses *nc);
    char *GetClipboard(int *len);
};

ScintillaNotCurses::ScintillaNotCurses(void (*cb)(void *, int, SCNotification *, void *),
                                       void *ud)
    : ncp(nullptr), hasFocus(false), capturedMouse(false), dirty(true),
      lastRenderTime{}, notifyCallback(cb), notifyUserdata(ud) {
    lastRenderTime.tv_sec = 0;
    lastRenderTime.tv_nsec = 0;
    Initialise();
}

ScintillaNotCurses::~ScintillaNotCurses() {
    Finalise();
}

void ScintillaNotCurses::Initialise() {
    struct ncplane *stdplane = GetStdPlane();
    if (!stdplane) return;

    unsigned rows = 0, cols = 0;
    ncplane_dim_yx(stdplane, &rows, &cols);

    struct ncplane_options opts = {};
    opts.y = 0;
    opts.x = 0;
    opts.rows = rows > 0 ? rows : 24;
    opts.cols = cols > 0 ? cols : 80;
    ncp = ncplane_create(stdplane, &opts);
    if (!ncp) return;

    /* Set the Scintilla main window to our plane */
    wMain = static_cast<WindowID>(ncp);

    /* Draw directly to the surface — no off-screen pixmap double-buffering.
     * This matches the original scinterm (curses) behaviour and avoids the
     * overhead of Copy() for every rendered line. */
    view.bufferedDraw = false;

    /* Force lineHeight=1: ViewStyle::Refresh always initialises maxDescent=1
     * before inspecting font metrics, so lineHeight = lround(1+1) = 2 even
     * when Descent() returns 0.  Setting extraDescent=-1 compensates:
     *   maxDescent = max(0.0, 1 + (-1)) = 0  →  lineHeight = 1            */
    vs.extraDescent = -1;

    /* Terminal rendering is single-pass: DrawTextNoClip receives both fg and
     * bg colours in one call, which is the only way to preserve selection
     * background in NotCurses.  With the default PhasesDraw::Two, the
     * foreground phase uses DrawTextTransparent which calls
     * ncplane_set_bg_default() and erases whatever background FillRectangle
     * painted in phase 1, making selections invisible.                    */
    WndProc(Message::SetPhasesDraw, 0, 0); /* 0 = SC_PHASES_ONE */

    /* Do not let Scintilla draw its own caret block — it would paint a white
     * rectangle on the terminal.  The terminal cursor is positioned via
     * notcurses_cursor_enable() in UpdateCursor() instead.               */
    WndProc(Message::SetCaretStyle, 0, 0); /* 0 = CARETSTYLE_INVISIBLE */

    /* Disable caret line highlight — it paints the entire current line with
     * a light background colour, which looks like a white band on terminal. */
    WndProc(Message::SetCaretLineVisible, 0, 0);

    /* Configure default colours: dark theme, then propagate to all styles */
    vs.styles[STYLE_DEFAULT].fore = ColourRGBA(0xD4, 0xD4, 0xD4);
    vs.styles[STYLE_DEFAULT].back = ColourRGBA(0x1E, 0x1E, 0x1E);
    WndProc(Message::StyleClearAll, 0, 0);

    /* Disable all margins — in terminal mode every column is precious */
    for (int m = 0; m < 5; m++)
        WndProc(Message::SetMarginWidthN, static_cast<uptr_t>(m), 0);

    /* In terminal mode 1 unit = 1 character cell.  The default
     * marginNumberPadding=3 (GUI pixels) would consume 3 character columns on
     * the right of the line-number margin, causing numbers to start at a
     * negative x for anything beyond single digits.  Set it to 0 so that line
     * numbers are right-justified flush to the margin edge. */
    vs.marginNumberPadding = 0;

    /* Register terminal fold marker renderer.
     * Scintilla's built-in DrawFoldingMark() uses pixel-level geometry that
     * is meaningless in a terminal (1 cell = 1 unit).  Setting customDraw on
     * the 7 fold-marker slots redirects rendering to DrawFoldMarkerCallback,
     * which draws a single ASCII/Unicode character per cell instead.
     * This survives subsequent SCI_MARKERDEFINE calls because MarkerDefine
     * only updates markType, leaving customDraw untouched.                  */
    /* Marker numbers 25-31 are the 7 fold-related slots (see Scintilla.h).
     * SC_MARKNUM_FOLDEREND=25 … SC_MARKNUM_FOLDEROPEN=31                  */
    /* Ensure markers vector is large enough */
    if (vs.markers.size() < 32) {
        vs.markers.resize(32);
    }
    for (int m = 25; m <= 31; ++m) {
        if (m < static_cast<int>(vs.markers.size()))
            vs.markers[m].customDraw = DrawFoldMarkerCallback;
    }

    InvalidateStyleRedraw();
}

void ScintillaNotCurses::Finalise() noexcept {
    if (ncp) {
        ncplane_destroy(ncp);
        ncp = nullptr;
    }
}

void ScintillaNotCurses::NotifyParent(Scintilla::NotificationData scn) {
    if (notifyCallback) {
        scn.nmhdr.hwndFrom = this;
        /* Cast to SCNotification* — NotificationData and SCNotification are
           layout-compatible (both are the same struct in different namespaces) */
        notifyCallback(this,
                       static_cast<int>(scn.nmhdr.code),
                       reinterpret_cast<SCNotification *>(&scn),
                       notifyUserdata);
    }
}

void ScintillaNotCurses::CopyToClipboard(const SelectionText &selectedText) {
    g_clipboard.set(std::string_view(selectedText.Data(), selectedText.Length()));
}

void ScintillaNotCurses::Copy() {
    SelectionText st;
    CopySelectionRange(&st);
    CopyToClipboard(st);
}

void ScintillaNotCurses::Paste() {
    if (g_clipboard.empty()) return;
    std::string clipdata = g_clipboard.get();
    ClearSelection(false);
    InsertPaste(clipdata.c_str(), static_cast<Sci::Position>(clipdata.size()));
}

void ScintillaNotCurses::Render() {
    if (!ncp) return;

    /* ── Dirty check ────────────────────────────────────────────────────────
     * Skip the Scintilla paint pass entirely when nothing has changed.      */
    if (!scinterm_is_dirty()) return;

    /* ── Frame-rate throttle ────────────────────────────────────────────────
     *
     * Uses robust nanosleep that handles EINTR correctly. The sleep time
     * is adjusted to maintain consistent frame timing.                     */
    struct timespec now = {};
    if (sc_now(&now)) {
        const long long elapsed_ns = sc_diff_ns(now, lastRenderTime);
        if (elapsed_ns < FRAME_NS) {
            sc_nanosleep(FRAME_NS - elapsed_ns);
            // Re-read time after sleep for accurate timing
            sc_now(&now);
        }
    }
    lastRenderTime = now;

    dirty = false;

    /* Reclaim all per-frame scratch memory in O(1) before the paint pass. */
    {
        std::lock_guard<std::mutex> lock(g_arena_mutex);
        arena_reset(&g_render_arena);
    }

    /* Fill the entire plane with the default background colour before painting
     * so that any cell not explicitly drawn by Scintilla shows the editor
     * background instead of the terminal's default (usually white). */
    ColourRGBA bg = vs.styles[STYLE_DEFAULT].back;
    uint64_t channels = 0;
    ncchannels_set_bg_rgb8(&channels,
        static_cast<unsigned>(bg.GetRed()),
        static_cast<unsigned>(bg.GetGreen()),
        static_cast<unsigned>(bg.GetBlue()));
    ncchannels_set_fg_rgb8(&channels,
        static_cast<unsigned>(bg.GetRed()),
        static_cast<unsigned>(bg.GetGreen()),
        static_cast<unsigned>(bg.GetBlue()));
    ncplane_set_base(ncp, " ", 0, channels);
    ncplane_erase(ncp);

    /* Create a surface backed by our ncplane and pass it to Scintilla's paint path */
    auto surface = Surface::Allocate(Technology::Default);
    surface->Init(static_cast<WindowID>(ncp));
    Paint(surface.get(), GetClientRectangle());
}

void ScintillaNotCurses::UpdateCursor() {
    if (!ncp) return;
    struct notcurses *nc = ncplane_notcurses(ncp);
    if (!nc) return;
    if (!hasFocus) {
        notcurses_cursor_disable(nc);
        return;
    }
    Point pt = PointMainCaret();
    int row = static_cast<int>(pt.y);
    int col = static_cast<int>(pt.x);
    /* Convert plane-relative coordinates to screen coordinates */
    int py = 0, px = 0;
    ncplane_yx(ncp, &py, &px);
    notcurses_cursor_enable(nc, py + row, px + col);
}

void ScintillaNotCurses::Resize() {
    if (!ncp) return;
    /* Do NOT resize the plane here.  The caller (scintilla_resize()) already
     * set the plane to the desired dimensions (e.g. rows-2 to leave room for
     * tab bar and status bar).  Just inform Scintilla of the new size so it
     * can update scroll bars and layout. */
    scinterm_mark_dirty();   /* geometry changed → full repaint required */
    ChangeSize();
}

void ScintillaNotCurses::SetFocus(bool focus) {
    hasFocus = focus;
    SetFocusState(focus);
    scinterm_mark_dirty();   /* focus change affects cursor visibility */
}

/*=============================================================================
 * Key handling helper functions
 * ARCHITECTURE: Extracted functions for better maintainability
 *===========================================================================*/

/**
 * @brief Encode Unicode codepoint to UTF-8
 * SECURITY: Validates codepoint range before encoding
 */
static int encode_utf8(uint32_t ucs, char out[5]) {
    // Reject surrogates and out-of-range values
    if (ucs > 0x10FFFF || (ucs >= 0xD800 && ucs <= 0xDFFF)) {
        return 0;
    }
    
    if (ucs < 0x80) {
        out[0] = static_cast<char>(ucs);
        return 1;
    } else if (ucs < 0x800) {
        out[0] = static_cast<char>(0xC0 | (ucs >> 6));
        out[1] = static_cast<char>(0x80 | (ucs & 0x3F));
        return 2;
    } else if (ucs < 0x10000) {
        out[0] = static_cast<char>(0xE0 | (ucs >> 12));
        out[1] = static_cast<char>(0x80 | ((ucs >> 6) & 0x3F));
        out[2] = static_cast<char>(0x80 | (ucs & 0x3F));
        return 3;
    } else {
        out[0] = static_cast<char>(0xF0 | (ucs >> 18));
        out[1] = static_cast<char>(0x80 | ((ucs >> 12) & 0x3F));
        out[2] = static_cast<char>(0x80 | ((ucs >> 6) & 0x3F));
        out[3] = static_cast<char>(0x80 | (ucs & 0x3F));
        return 4;
    }
}

void ScintillaNotCurses::SendKey(int key, int modifiers) {
    int sciMods = 0;
    if (modifiers & NCKEY_MOD_SHIFT) sciMods |= static_cast<int>(KeyMod::Shift);
    if (modifiers & NCKEY_MOD_CTRL)  sciMods |= static_cast<int>(KeyMod::Ctrl);
    if (modifiers & NCKEY_MOD_ALT)   sciMods |= static_cast<int>(KeyMod::Alt);
    if (modifiers & NCKEY_MOD_META)  sciMods |= static_cast<int>(KeyMod::Meta);
    if (modifiers & NCKEY_MOD_SUPER) sciMods |= static_cast<int>(KeyMod::Super);

    int sciKey = key;
    switch (static_cast<uint32_t>(key)) {
        case NCKEY_UP:     sciKey = static_cast<int>(Keys::Up); break;
        case NCKEY_DOWN:   sciKey = static_cast<int>(Keys::Down); break;
        case NCKEY_LEFT:   sciKey = static_cast<int>(Keys::Left); break;
        case NCKEY_RIGHT:  sciKey = static_cast<int>(Keys::Right); break;
        case NCKEY_HOME:   sciKey = static_cast<int>(Keys::Home); break;
        case NCKEY_END:    sciKey = static_cast<int>(Keys::End); break;
        case NCKEY_PGUP:   sciKey = static_cast<int>(Keys::Prior); break;
        case NCKEY_PGDOWN: sciKey = static_cast<int>(Keys::Next); break;
        case NCKEY_DEL:    sciKey = static_cast<int>(Keys::Delete); break;
        case NCKEY_INS:    sciKey = static_cast<int>(Keys::Insert); break;
        case NCKEY_BACKSPACE: sciKey = static_cast<int>(Keys::Back); break;
        /* NCKEY_TAB = 0x09, handled in printable char range below */
        case NCKEY_ENTER:  sciKey = static_cast<int>(Keys::Return); break;
        /* NCKEY_ESC = 0x1b, handled in printable char range below */
        default:
            if (key == 0x09) {
                /* Tab */
                KeyDownWithModifiers(Keys::Tab, static_cast<KeyMod>(sciMods), nullptr);
                return;
            } else if (key == 0x1b) {
                /* Escape */
                KeyDownWithModifiers(Keys::Escape, static_cast<KeyMod>(sciMods), nullptr);
                return;
            } else if (key == 0x0d || key == 0x0a) {
                /* Enter/Return */
                KeyDownWithModifiers(Keys::Return, static_cast<KeyMod>(sciMods), nullptr);
                return;
            } else if (key == 0x08 || key == 0x7f) {
                /* Backspace */
                KeyDownWithModifiers(Keys::Back, static_cast<KeyMod>(sciMods), nullptr);
                return;
            } else if (key >= 0x20 && key < 0x7F) {
                if (sciMods & (static_cast<int>(KeyMod::Ctrl) | static_cast<int>(KeyMod::Alt))) {
                    /* Ctrl/Alt+key — pass as command, not text insertion. */
                    int cmdKey = (key >= 'a' && key <= 'z') ? (key - 32) : key;
                    KeyDownWithModifiers(static_cast<Keys>(cmdKey),
                                        static_cast<KeyMod>(sciMods), nullptr);
                } else {
                    /* Regular printable ASCII character */
                    char ch[2] = { static_cast<char>(key), '\0' };
                    InsertCharacter(std::string_view(ch, 1), CharacterSource::DirectInput);
                }
                return;
            } else if (key > 0x7F && key <= 0x10FFFF &&
                       !(key >= 0xD800 && key <= 0xDFFF)) {
                /* Unicode codepoint — use extracted encode_utf8 function */
                char utf8[5] = {};
                int len = encode_utf8(static_cast<uint32_t>(key), utf8);
                if (len > 0) {
                    InsertCharacter(std::string_view(utf8, static_cast<size_t>(len)),
                                    CharacterSource::DirectInput);
                }
                return;
            }
            break;
    }
    KeyDownWithModifiers(static_cast<Keys>(sciKey),
                         static_cast<KeyMod>(sciMods), nullptr);
}

bool ScintillaNotCurses::SendMouse(int event, int button, int modifiers, int y, int x) {
    if (!ncp) return false;

    int py = 0, px = 0;
    ncplane_yx(ncp, &py, &px);
    int relY = y - py;
    int relX = x - px;
    if (relX < 0 || relY < 0) return false;

    Point pt(static_cast<XYPOSITION>(relX), static_cast<XYPOSITION>(relY));
    int sciMods = 0;
    if (modifiers & NCKEY_MOD_SHIFT) sciMods |= static_cast<int>(KeyMod::Shift);
    if (modifiers & NCKEY_MOD_CTRL)  sciMods |= static_cast<int>(KeyMod::Ctrl);
    if (modifiers & NCKEY_MOD_ALT)   sciMods |= static_cast<int>(KeyMod::Alt);
    if (modifiers & NCKEY_MOD_META)  sciMods |= static_cast<int>(KeyMod::Meta);
    if (modifiers & NCKEY_MOD_SUPER) sciMods |= static_cast<int>(KeyMod::Super);

    unsigned int time = 0;

    if (event == SCM_PRESS) {
        if (button == 4) {
            WndProc(Message::LineScrollUp, 3, 0);
            return true;
        }
        if (button == 5) {
            WndProc(Message::LineScrollDown, 3, 0);
            return true;
        }
        ButtonDownWithModifiers(pt, time, static_cast<KeyMod>(sciMods));
        return true;
    } else if (event == SCM_DRAG) {
        ButtonMoveWithModifiers(pt, time, static_cast<KeyMod>(sciMods));
        return true;
    } else if (event == SCM_RELEASE) {
        ButtonUpWithModifiers(pt, time, static_cast<KeyMod>(sciMods));
        return true;
    }
    return false;
}

bool ScintillaNotCurses::ProcessInput(struct notcurses *nc) {
    /* ── Input batching with resize coalescing ─────────────────────────────
     *
     * Collects all available events and processes them before rendering.
     * Multiple resize events are coalesced into a single resize operation
     * to prevent O(N²) work during resize storms.                          */
    static constexpr int BATCH_MAX = 256;
    ncinput  inputs[BATCH_MAX];
    uint32_t keys[BATCH_MAX];
    int n = 0;
    bool has_resize = false;

    while (n < BATCH_MAX) {
        ncinput in = {};
        const uint32_t key = notcurses_get_nblock(nc, &in);
        if (key == static_cast<uint32_t>(-1) || key == 0) break;
        
        // Coalesce multiple resize events
        if (key == NCKEY_RESIZE) {
            if (has_resize) {
                // Skip duplicate resize events - keep only the last one
                continue;
            }
            has_resize = true;
        }
        
        inputs[n] = in;
        keys[n]   = key;
        ++n;
    }

    if (n == 0) return false;

    for (int i = 0; i < n; ++i) {
        const ncinput  &input = inputs[i];
        const uint32_t  key   = keys[i];

        /* Resize: update Scintilla geometry immediately; no key dispatch. */
        if (key == NCKEY_RESIZE) {
            Resize();
            continue;
        }

        /* Kitty protocol delivers press+release pairs for every keystroke.
         * Discard releases for keyboard events — only PRESS/REPEAT reach
         * Scintilla.  Mouse release is handled by the SCM_RELEASE path below
         * and must NOT be filtered here. */
        if (!nckey_mouse_p(key) && input.evtype == NCTYPE_RELEASE)
            continue;

        if (nckey_mouse_p(key)) {
            int evt = (input.evtype == NCTYPE_PRESS)   ? SCM_PRESS :
                      (input.evtype == NCTYPE_RELEASE) ? SCM_RELEASE : SCM_DRAG;
            int mods = 0;
            if (input.modifiers & NCKEY_MOD_SHIFT) mods |= NCKEY_MOD_SHIFT;
            if (input.modifiers & NCKEY_MOD_CTRL)  mods |= NCKEY_MOD_CTRL;
            if (input.modifiers & NCKEY_MOD_ALT)   mods |= NCKEY_MOD_ALT;
            if (input.modifiers & NCKEY_MOD_META)  mods |= NCKEY_MOD_META;
            if (input.modifiers & NCKEY_MOD_SUPER) mods |= NCKEY_MOD_SUPER;
            
            // Extract button from notcurses input.id field (more reliable than eff_text)
            int btn = 1;  // Default to left button
            if (input.id >= 1 && input.id <= 5) {
                btn = static_cast<int>(input.id);
            }
            
            SendMouse(evt, btn, mods, input.y, input.x);
        } else {
            SendKey(static_cast<int>(key), static_cast<int>(input.modifiers));
        }
    }

    return true;
}

char *ScintillaNotCurses::GetClipboard(int *len) {
    if (g_clipboard.empty()) {
        if (len) *len = 0;
        return nullptr;
    }
    std::string clipdata = g_clipboard.get();
    size_t sz = clipdata.size();
    
    /* SECURITY: Guard against multiple overflow scenarios:
     * 1. Truncation: cap at INT_MAX for return value
     * 2. Overflow: sz + 1 must not overflow
     * 3. Allocation size: must be reasonable
     */
    constexpr size_t MAX_CLIPBOARD_SIZE = static_cast<size_t>(INT_MAX) - 1;
    constexpr size_t REASONABLE_MAX = 100 * 1024 * 1024;  // 100MB sanity limit
    
    if (sz > REASONABLE_MAX) {
        sz = REASONABLE_MAX;
    }
    if (sz > MAX_CLIPBOARD_SIZE) {
        sz = MAX_CLIPBOARD_SIZE;
    }
    
    /* Now sz + 1 is guaranteed safe */
    char *result = static_cast<char *>(malloc(sz + 1));
    if (!result) {
        if (len) *len = 0;
        return nullptr;
    }
    
    memcpy(result, clipdata.data(), sz);
    result[sz] = '\0';
    if (len) *len = static_cast<int>(sz);
    return result;
}

/*=============================================================================
 * Graphics protocol detection
 *
 * Detection is performed in scintilla_notcurses_init() in three stages:
 *
 *  Stage 1 – env-var heuristics (cheap, before notcurses_init):
 *    $TERM_PROGRAM  kitty / WezTerm / ghostty / iTerm.app  → KITTY
 *    $TERM          xterm-kitty / xterm-ghostty             → KITTY
 *                   mlterm / foot / yaft / *sixel*          → SIXEL
 *
 *  Stage 2 – notcurses runtime query (after notcurses_init):
 *    notcurses_check_pixel_support() returns an ncpixelimpl_e value that
 *    is mapped to KITTY / SIXEL / NONE.  This overrides stage 1 only when
 *    stage 1 returned NONE (i.e. env vars gave no hint).
 *
 *  Stage 3 – user override:
 *    scinterm_set_graphics_protocol() stores a value in g_graphics_request.
 *    Any value other than AUTO short-circuits both stages above.
 *
 * The resolved protocol is stored in g_graphics_active after init.
 *===========================================================================*/

/** Protocol requested by the caller (default: auto-detect). */
static ScintermGraphicsProtocol g_graphics_request = SCINTERM_GRAPHICS_AUTO;

/** Protocol actually in use after init (resolved from request + detection). */
static ScintermGraphicsProtocol g_graphics_active  = SCINTERM_GRAPHICS_NONE;

/** Whether init has been called (locks graphics_request). */
static std::atomic<bool> g_init_completed{false};

/**
 * Stage 1: probe $TERM_PROGRAM and $TERM for well-known pixel-capable
 * terminals.  Returns KITTY, SIXEL, or NONE.  Never returns AUTO.
 */
static ScintermGraphicsProtocol detect_graphics_from_env() noexcept {
    /* $TERM_PROGRAM takes priority — it is set by the terminal emulator itself
     * and is more reliable than $TERM which can be overridden by the user. */
    const char *tp = std::getenv("TERM_PROGRAM");
    if (tp) {
        if (std::strcmp(tp, "kitty")    == 0) return SCINTERM_GRAPHICS_KITTY;
        if (std::strcmp(tp, "WezTerm")  == 0) return SCINTERM_GRAPHICS_KITTY;
        if (std::strcmp(tp, "ghostty")  == 0) return SCINTERM_GRAPHICS_KITTY;
        if (std::strcmp(tp, "iTerm.app")== 0) return SCINTERM_GRAPHICS_KITTY;
    }
    /* $COLORTERM=truecolor/24bit is not graphics-specific; skip it. */

    const char *term = std::getenv("TERM");
    if (term) {
        /* Kitty-family TERM strings */
        if (std::strcmp(term, "xterm-kitty")   == 0) return SCINTERM_GRAPHICS_KITTY;
        if (std::strcmp(term, "xterm-ghostty")  == 0) return SCINTERM_GRAPHICS_KITTY;
        /* Sixel-capable terminals */
        if (std::strcmp(term, "mlterm") == 0) return SCINTERM_GRAPHICS_SIXEL;
        if (std::strcmp(term, "foot")   == 0) return SCINTERM_GRAPHICS_SIXEL;
        if (std::strcmp(term, "yaft")   == 0) return SCINTERM_GRAPHICS_SIXEL;
        /* Generic sixel marker anywhere in $TERM (e.g. "xterm+sixel") */
        if (std::strstr(term, "sixel") != nullptr) return SCINTERM_GRAPHICS_SIXEL;
    }
    return SCINTERM_GRAPHICS_NONE;
}

/**
 * Stage 2: map a notcurses ncpixelimpl_e value to our enum.
 * NCPIXEL_NONE / unknown → NONE; kitty variants → KITTY; sixel → SIXEL.
 */
static ScintermGraphicsProtocol ncpixel_to_protocol(ncpixelimpl_e impl) noexcept {
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

/**
 * Translate a resolved protocol to the NCOPTION_* flags that should be
 * OR-ed into notcurses_init().
 *
 * NCOPTION_NO_CLEAR_BITMAPS tells NotCurses not to erase pixel-graphics
 * content (kitty/sixel images) when it refreshes planes.  This preserves
 * inline images already present on screen before the editor was launched.
 */
static uint64_t graphics_to_ncoption_flags(ScintermGraphicsProtocol proto) noexcept {
    switch (proto) {
        case SCINTERM_GRAPHICS_KITTY:
        case SCINTERM_GRAPHICS_SIXEL:
            return NCOPTION_NO_CLEAR_BITMAPS;
        default:
            return 0;
    }
}

#ifdef SCINTERM_DEBUG
/** Return a human-readable name for a protocol value (debug builds only). */
static const char *protocol_name(ScintermGraphicsProtocol proto) noexcept {
    switch (proto) {
        case SCINTERM_GRAPHICS_AUTO:  return "AUTO";
        case SCINTERM_GRAPHICS_KITTY: return "KITTY";
        case SCINTERM_GRAPHICS_SIXEL: return "SIXEL";
        case SCINTERM_GRAPHICS_NONE:  return "NONE";
        default:                      return "UNKNOWN";
    }
}
#endif /* SCINTERM_DEBUG */

/*=============================================================================
 * C API implementation
 *===========================================================================*/

extern "C" {

bool scintilla_notcurses_init(void) {
    using namespace Scintilla::Internal;
    
    if (g_init_completed.load()) {
        // Already initialized
        return true;
    }

    /* --- Stage 3: if caller already overrode, skip env detection --- */
    ScintermGraphicsProtocol detected;
    if (g_graphics_request != SCINTERM_GRAPHICS_AUTO) {
        detected = g_graphics_request;
#ifdef SCINTERM_DEBUG
        std::fprintf(stderr, "[scinterm] graphics override: %s\n",
                     protocol_name(detected));
#endif
    } else {
        /* --- Stage 1: cheap env-var heuristics --- */
        detected = detect_graphics_from_env();
#ifdef SCINTERM_DEBUG
        std::fprintf(stderr, "[scinterm] graphics env detection: %s\n",
                     protocol_name(detected));
#endif
    }

    /* Compute NCOPTION flags from what we know before init.
     * When AUTO/NONE from env, we pass 0 now and refine after init via
     * notcurses_check_pixel_support(). */
    const uint64_t extra_flags = (detected != SCINTERM_GRAPHICS_NONE)
                                 ? graphics_to_ncoption_flags(detected)
                                 : 0;

    if (!InitNotCurses(extra_flags))
        return false;

    /* --- Stage 2: runtime pixel-support query (only if not user-overridden
     *              and env gave no hint) --- */
    if (g_graphics_request == SCINTERM_GRAPHICS_AUTO
        && detected == SCINTERM_GRAPHICS_NONE) {
        const ncpixelimpl_e impl = notcurses_check_pixel_support(GetNotCurses());
        detected = ncpixel_to_protocol(impl);
#ifdef SCINTERM_DEBUG
        std::fprintf(stderr, "[scinterm] graphics notcurses query: impl=%d → %s\n",
                     static_cast<int>(impl), protocol_name(detected));
#endif
    }

    g_graphics_active = detected;
    g_init_completed.store(true);

#ifdef SCINTERM_DEBUG
    std::fprintf(stderr, "[scinterm] graphics active: %s\n",
                 protocol_name(g_graphics_active));
#endif
    return true;
}

void scinterm_set_graphics_protocol(ScintermGraphicsProtocol protocol) {
    if (!g_init_completed.load()) {
        g_graphics_request = protocol;
    }
    // Silently ignore if init already completed - protocol is locked
}

void scintilla_notcurses_shutdown(void) {
    Scintilla::Internal::ShutdownNotCurses();
    g_init_completed.store(false);
}

ScintillaHandle *scintilla_new(ScintillaCallback cb, void *ud) {
    auto *sci = new(std::nothrow) ScintillaNotCurses(
        reinterpret_cast<void (*)(void *, int, SCNotification *, void *)>(cb), ud);
    return reinterpret_cast<ScintillaHandle *>(sci);
}

void scintilla_delete(ScintillaHandle *sci) {
    delete reinterpret_cast<ScintillaNotCurses *>(sci);
}

struct ncplane *scintilla_get_plane(ScintillaHandle *sci) {
    if (!sci) return nullptr;
    return reinterpret_cast<ScintillaNotCurses *>(sci)->GetPlane();
}

sptr_t scintilla_send_message(ScintillaHandle *sci, unsigned int iMessage, uptr_t wParam, sptr_t lParam) {
    if (!sci) return 0;
    return reinterpret_cast<ScintillaNotCurses *>(sci)->WndProc(
        static_cast<Scintilla::Message>(iMessage), wParam, lParam);
}

void scintilla_send_key(ScintillaHandle *sci, int key, int modifiers) {
    if (!sci) return;
    reinterpret_cast<ScintillaNotCurses *>(sci)->SendKey(key, modifiers);
}

bool scintilla_send_mouse(ScintillaHandle *sci, int event, int button, int modifiers, int y, int x) {
    if (!sci) return false;
    return reinterpret_cast<ScintillaNotCurses *>(sci)->SendMouse(event, button, modifiers, y, x);
}

bool scintilla_process_input(ScintillaHandle *sci, struct notcurses *nc) {
    if (!sci || !nc) return false;
    return reinterpret_cast<ScintillaNotCurses *>(sci)->ProcessInput(nc);
}

void scintilla_render(ScintillaHandle *sci) {
    if (!sci) return;
    reinterpret_cast<ScintillaNotCurses *>(sci)->Render();
}

void scintilla_update_cursor(ScintillaHandle *sci) {
    if (!sci) return;
    reinterpret_cast<ScintillaNotCurses *>(sci)->UpdateCursor();
}

void scintilla_resize(ScintillaHandle *sci) {
    if (!sci) return;
    reinterpret_cast<ScintillaNotCurses *>(sci)->Resize();
}

void scintilla_set_focus(ScintillaHandle *sci, bool focus) {
    if (!sci) return;
    reinterpret_cast<ScintillaNotCurses *>(sci)->SetFocus(focus);
}

char *scintilla_get_clipboard(ScintillaHandle *sci, int *len) {
    if (!sci) {
        if (len) *len = 0;
        return nullptr;
    }
    return reinterpret_cast<ScintillaNotCurses *>(sci)->GetClipboard(len);
}

void scintilla_set_color_offsets(int color_offset, int pair_offset) {
    (void)color_offset;
    (void)pair_offset;
    /* No-op: NotCurses uses direct true color */
}

bool scintilla_set_lexer(ScintillaHandle *sci, const char *name, const char *lexers_dir) {
#ifdef ENABLE_SCINTILLUA
    if (!sci || !name) return false;
    if (lexers_dir)
        SetLibraryProperty("scintillua.lexers", lexers_dir);
    Scintilla::ILexer5 *lexer = CreateLexer(name);
    if (!lexer) return false;
    scintilla_send_message(sci, SCI_SETILEXER, 0,
                           reinterpret_cast<sptr_t>(lexer));
    return true;
#else
    (void)sci; (void)name; (void)lexers_dir;
    return false;
#endif
}

} // extern "C"

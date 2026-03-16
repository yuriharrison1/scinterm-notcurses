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
 * Internal clipboard
 *===========================================================================*/

static std::string g_clipboard;

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
    void (*notifyCallback)(void *sci, int iMessage, SCNotification *n, void *userdata);
    void *notifyUserdata;
    std::string internalClipboard;

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
    void UpdateSystemCaret() override {}

    void NotifyChange() override {}
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
    : ncp(nullptr), hasFocus(false), capturedMouse(false),
      notifyCallback(cb), notifyUserdata(ud) {
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
    internalClipboard = std::string(selectedText.Data(), selectedText.Length());
    g_clipboard = internalClipboard;
}

void ScintillaNotCurses::Copy() {
    SelectionText st;
    CopySelectionRange(&st);
    CopyToClipboard(st);
}

void ScintillaNotCurses::Paste() {
    if (g_clipboard.empty()) return;
    ClearSelection(false);
    InsertPaste(g_clipboard.c_str(), static_cast<Sci::Position>(g_clipboard.size()));
}

void ScintillaNotCurses::Render() {
    if (!ncp) return;

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
    ChangeSize();
}

void ScintillaNotCurses::SetFocus(bool focus) {
    hasFocus = focus;
    SetFocusState(focus);
}

void ScintillaNotCurses::SendKey(int key, int modifiers) {
    int sciMods = 0;
    if (modifiers & NCKEY_MOD_SHIFT) sciMods |= static_cast<int>(KeyMod::Shift);
    if (modifiers & NCKEY_MOD_CTRL)  sciMods |= static_cast<int>(KeyMod::Ctrl);
    if (modifiers & NCKEY_MOD_ALT)   sciMods |= static_cast<int>(KeyMod::Alt);

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
                    /* Ctrl/Alt+key — pass as command, not text insertion.
                     * Scintilla's keymap uses uppercase letters for commands. */
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
                /* Unicode codepoint — encode as UTF-8.
                 * Reject surrogates (D800-DFFF) and values beyond U+10FFFF. */
                char utf8[5] = {};
                uint32_t ucs = static_cast<uint32_t>(key);
                int len = 0;
                if (ucs < 0x80) {
                    utf8[0] = static_cast<char>(ucs);
                    len = 1;
                } else if (ucs < 0x800) {
                    utf8[0] = static_cast<char>(0xC0 | (ucs >> 6));
                    utf8[1] = static_cast<char>(0x80 | (ucs & 0x3F));
                    len = 2;
                } else if (ucs < 0x10000) {
                    utf8[0] = static_cast<char>(0xE0 | (ucs >> 12));
                    utf8[1] = static_cast<char>(0x80 | ((ucs >> 6) & 0x3F));
                    utf8[2] = static_cast<char>(0x80 | (ucs & 0x3F));
                    len = 3;
                } else {
                    utf8[0] = static_cast<char>(0xF0 | (ucs >> 18));
                    utf8[1] = static_cast<char>(0x80 | ((ucs >> 12) & 0x3F));
                    utf8[2] = static_cast<char>(0x80 | ((ucs >> 6) & 0x3F));
                    utf8[3] = static_cast<char>(0x80 | (ucs & 0x3F));
                    len = 4;
                }
                InsertCharacter(std::string_view(utf8, static_cast<size_t>(len)),
                                CharacterSource::DirectInput);
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
    ncinput input = {};
    uint32_t key = notcurses_get_nblock(nc, &input);
    if (key == static_cast<uint32_t>(-1) || key == 0) return false;

    if (nckey_mouse_p(key)) {
        int evt = (input.evtype == NCTYPE_PRESS)   ? SCM_PRESS :
                  (input.evtype == NCTYPE_RELEASE) ? SCM_RELEASE : SCM_DRAG;
        int mods = 0;
        if (input.modifiers & NCKEY_MOD_SHIFT) mods |= NCKEY_MOD_SHIFT;
        if (input.modifiers & NCKEY_MOD_CTRL)  mods |= NCKEY_MOD_CTRL;
        int btn = input.eff_text[0] ? static_cast<int>(input.eff_text[0]) : 1;
        return SendMouse(evt, btn, mods, input.y, input.x);
    }

    SendKey(static_cast<int>(key), static_cast<int>(input.modifiers));
    return true;
}

char *ScintillaNotCurses::GetClipboard(int *len) {
    if (g_clipboard.empty()) {
        if (len) *len = 0;
        return nullptr;
    }
    size_t sz = g_clipboard.size();
    /* Guard against truncation: cap at INT_MAX bytes */
    if (sz > static_cast<size_t>(INT_MAX))
        sz = static_cast<size_t>(INT_MAX);
    char *result = static_cast<char *>(malloc(sz + 1));
    if (!result) {
        if (len) *len = 0;
        return nullptr;
    }
    memcpy(result, g_clipboard.data(), sz);
    result[sz] = '\0';
    if (len) *len = static_cast<int>(sz);
    return result;
}

/*=============================================================================
 * C API implementation
 *===========================================================================*/

extern "C" {

bool scintilla_notcurses_init(void) {
    return Scintilla::Internal::InitNotCurses();
}

void scintilla_notcurses_shutdown(void) {
    Scintilla::Internal::ShutdownNotCurses();
}

void *scintilla_new(void (*cb)(void *, int, SCNotification *, void *), void *ud) {
    auto *sci = new(std::nothrow) ScintillaNotCurses(cb, ud);
    return sci;
}

void scintilla_delete(void *sci) {
    delete static_cast<ScintillaNotCurses *>(sci);
}

struct ncplane *scintilla_get_plane(void *sci) {
    if (!sci) return nullptr;
    return static_cast<ScintillaNotCurses *>(sci)->GetPlane();
}

sptr_t scintilla_send_message(void *sci, unsigned int iMessage, uptr_t wParam, sptr_t lParam) {
    if (!sci) return 0;
    return static_cast<ScintillaNotCurses *>(sci)->WndProc(
        static_cast<Scintilla::Message>(iMessage), wParam, lParam);
}

void scintilla_send_key(void *sci, int key, int modifiers) {
    if (!sci) return;
    static_cast<ScintillaNotCurses *>(sci)->SendKey(key, modifiers);
}

bool scintilla_send_mouse(void *sci, int event, int button, int modifiers, int y, int x) {
    if (!sci) return false;
    return static_cast<ScintillaNotCurses *>(sci)->SendMouse(event, button, modifiers, y, x);
}

bool scintilla_process_input(void *sci, struct notcurses *nc) {
    if (!sci || !nc) return false;
    return static_cast<ScintillaNotCurses *>(sci)->ProcessInput(nc);
}

void scintilla_render(void *sci) {
    if (!sci) return;
    static_cast<ScintillaNotCurses *>(sci)->Render();
}

void scintilla_update_cursor(void *sci) {
    if (!sci) return;
    static_cast<ScintillaNotCurses *>(sci)->UpdateCursor();
}

void scintilla_resize(void *sci) {
    if (!sci) return;
    static_cast<ScintillaNotCurses *>(sci)->Resize();
}

void scintilla_set_focus(void *sci, bool focus) {
    if (!sci) return;
    static_cast<ScintillaNotCurses *>(sci)->SetFocus(focus);
}

char *scintilla_get_clipboard(void *sci, int *len) {
    if (!sci) {
        if (len) *len = 0;
        return nullptr;
    }
    return static_cast<ScintillaNotCurses *>(sci)->GetClipboard(len);
}

void scintilla_set_color_offsets(int color_offset, int pair_offset) {
    (void)color_offset;
    (void)pair_offset;
    /* No-op: NotCurses uses direct true color */
}

bool scintilla_set_lexer(void *sci, const char *name, const char *lexers_dir) {
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

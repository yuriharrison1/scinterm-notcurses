/**
 * @file scinterm_plat.cpp
 * @brief Platform abstraction layer implementation for Scinterm NotCurses
 *
 * Implements the Scintilla Platform interface using NotCurses as the backend.
 * Copyright 2012-2026 Mitchell. See LICENSE.
 */

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <cassert>
#include <cstdarg>
#include <cmath>

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

namespace Scintilla::Internal {

/*=============================================================================
 * Global NotCurses state
 *===========================================================================*/

static struct notcurses *g_nc = nullptr;
static struct ncplane *g_stdplane = nullptr;

bool InitNotCurses() {
    struct notcurses_options opts = {};
    opts.flags = NCOPTION_SUPPRESS_BANNERS;
    g_nc = notcurses_init(&opts, nullptr);
    if (!g_nc) return false;
    g_stdplane = notcurses_stdplane(g_nc);
    return true;
}

void ShutdownNotCurses() {
    if (g_nc) {
        notcurses_stop(g_nc);
        g_nc = nullptr;
        g_stdplane = nullptr;
    }
}

struct notcurses* GetNotCurses() { return g_nc; }
struct ncplane* GetStdPlane() { return g_stdplane; }

/*=============================================================================
 * Helper utilities
 *===========================================================================*/

static inline void SetNCColour(struct ncplane *ncp, ColourRGBA c, bool fg) {
    uint8_t r = c.GetRed(), g = c.GetGreen(), b = c.GetBlue();
    if (fg) ncplane_set_fg_rgb8(ncp, r, g, b);
    else    ncplane_set_bg_rgb8(ncp, r, g, b);
}

/*=============================================================================
 * FontImpl implementation
 *===========================================================================*/

FontImpl::FontImpl(const FontParameters &fp) {
    attrs = 0;
    if (static_cast<int>(fp.weight) >= 700) attrs |= NCSTYLE_BOLD;
    if (fp.italic) attrs |= NCSTYLE_ITALIC;
}

std::shared_ptr<Font> Font::Allocate(const FontParameters &fp) {
    return std::make_shared<FontImpl>(fp);
}

/*=============================================================================
 * SurfaceImpl implementation
 *===========================================================================*/

SurfaceImpl::SurfaceImpl() : ncp(nullptr), isOwned(false), pixmapColor(0, 0, 0),
    isIndentGuideHighlight(false), isCallTip(false) {
}

SurfaceImpl::~SurfaceImpl() noexcept {
    Release();
}

void SurfaceImpl::Init(WindowID wid) {
    Release();
    if (wid) {
        ncp = static_cast<struct ncplane *>(wid);
    }
}

void SurfaceImpl::Init(SurfaceID sid, WindowID wid) {
    Release();
    if (wid) {
        ncp = static_cast<struct ncplane *>(wid);
    } else if (sid) {
        ncp = static_cast<struct ncplane *>(sid);
    }
}

std::unique_ptr<Surface> SurfaceImpl::AllocatePixMap(int width, int height) {
    auto surf = std::make_unique<SurfaceImpl>();
    /* Use the stdplane as parent so the pixmap is NOT a child of the editor
     * plane (avoids it appearing on top of the editor content).
     * Position it far off-screen so it is never rendered to the terminal. */
    struct ncplane *parent = GetStdPlane();
    if (!parent) parent = ncp;
    if (parent) {
        struct ncplane_options opts = {};
        opts.y = -30000;
        opts.x = -30000;
        opts.rows = static_cast<unsigned>(height > 0 ? height : 1);
        opts.cols = static_cast<unsigned>(width > 0 ? width : 1);
        struct ncplane *plane = ncplane_create(parent, &opts);
        if (plane) {
            surf->ncp = plane;
            surf->isOwned = true;
        }
    }
    return surf;
}

void SurfaceImpl::SetMode(SurfaceMode mode) {
    (void)mode;
}

void SurfaceImpl::Release() noexcept {
    if (ncp && isOwned) {
        ncplane_destroy(ncp);
    }
    ncp = nullptr;
    isOwned = false;
}

int SurfaceImpl::SupportsFeature(Scintilla::Supports feature) noexcept {
    (void)feature;
    return 0;
}

bool SurfaceImpl::Initialised() {
    return ncp != nullptr;
}

int SurfaceImpl::LogPixelsY() { return 72; }
int SurfaceImpl::PixelDivisions() { return 1; }
int SurfaceImpl::DeviceHeightFont(int points) {
    (void)points;
    return 1;
}

void SurfaceImpl::LineDraw(Point start, Point end, Stroke stroke) {
    (void)start; (void)end; (void)stroke;
    /* No-op: terminal doesn't support arbitrary line drawing */
}

void SurfaceImpl::PolyLine(const Point *pts, size_t npts, Stroke stroke) {
    (void)pts; (void)npts; (void)stroke;
}

void SurfaceImpl::Polygon(const Point *pts, size_t npts, FillStroke fillStroke) {
    (void)pts; (void)npts; (void)fillStroke;
}

void SurfaceImpl::RectangleDraw(PRectangle rc, FillStroke fillStroke) {
    FillRectangle(rc, fillStroke.fill);
}

void SurfaceImpl::RectangleFrame(PRectangle rc, Stroke stroke) {
    FillRectangle(rc, Fill(stroke.colour));
}

void SurfaceImpl::FillRectangle(PRectangle rc, Fill fill) {
    if (!ncp) return;

    int left = static_cast<int>(std::floor(rc.left));
    int top = static_cast<int>(std::floor(rc.top));
    int right = static_cast<int>(std::ceil(rc.right));
    int bottom = static_cast<int>(std::ceil(rc.bottom));

    if (right <= left || bottom <= top) return;

    SetNCColour(ncp, fill.colour, false);
    ncplane_set_fg_default(ncp);
    ncplane_set_styles(ncp, NCSTYLE_NONE);

    for (int row = top; row < bottom; row++) {
        for (int col = left; col < right; col++) {
            ncplane_putchar_yx(ncp, row, col, ' ');
        }
    }
}

void SurfaceImpl::FillRectangleAligned(PRectangle rc, Fill fill) {
    FillRectangle(rc, fill);
}

void SurfaceImpl::FillRectangle(PRectangle rc, Surface &surfacePattern) {
    (void)rc; (void)surfacePattern;
}

void SurfaceImpl::RoundedRectangle(PRectangle rc, FillStroke fillStroke) {
    FillRectangle(rc, fillStroke.fill);
}

void SurfaceImpl::AlphaRectangle(PRectangle rc, XYPOSITION cornerSize, FillStroke fillStroke) {
    (void)cornerSize;
    FillRectangle(rc, fillStroke.fill);
}

void SurfaceImpl::GradientRectangle(PRectangle rc, const std::vector<ColourStop> &stops,
                                     GradientOptions options) {
    (void)options;
    if (!stops.empty()) {
        FillRectangle(rc, Fill(stops.front().colour));
    }
}

void SurfaceImpl::DrawRGBAImage(PRectangle rc, int width, int height,
                                const unsigned char *pixelsImage) {
    (void)rc; (void)width; (void)height; (void)pixelsImage;
}

void SurfaceImpl::Ellipse(PRectangle rc, FillStroke fillStroke) {
    FillRectangle(rc, fillStroke.fill);
}

void SurfaceImpl::Stadium(PRectangle rc, FillStroke fillStroke, Ends ends) {
    (void)ends;
    FillRectangle(rc, fillStroke.fill);
}

void SurfaceImpl::Copy(PRectangle rc, Point from, Surface &surfaceSource) {
    SurfaceImpl &src = static_cast<SurfaceImpl &>(surfaceSource);
    if (!ncp || !src.ncp || src.ncp == ncp) return;

    int srcY = static_cast<int>(from.y);
    int srcX = static_cast<int>(from.x);
    int dstY = static_cast<int>(std::floor(rc.top));
    int dstX = static_cast<int>(std::floor(rc.left));
    int height = static_cast<int>(std::ceil(rc.bottom) - std::floor(rc.top));
    int width  = static_cast<int>(std::ceil(rc.right)  - std::floor(rc.left));

    for (int row = 0; row < height; row++) {
        for (int col = 0; col < width; col++) {
            nccell cell = NCCELL_TRIVIAL_INITIALIZER;
            if (ncplane_at_yx_cell(src.ncp, srcY + row, srcX + col, &cell) >= 0) {
                if (ncplane_cursor_move_yx(ncp, dstY + row, dstX + col) == 0) {
                    ncplane_putc(ncp, &cell);
                }
                nccell_release(src.ncp, &cell);
            }
        }
    }
}

std::unique_ptr<IScreenLineLayout> SurfaceImpl::Layout(const IScreenLine *screenLine) {
    (void)screenLine;
    return nullptr;
}

void SurfaceImpl::DrawTextNoClip(PRectangle rc, const Font *font_, XYPOSITION ybase,
                                  std::string_view text, ColourRGBA fore, ColourRGBA back) {
    if (!ncp || text.empty()) return;

    int row = static_cast<int>(rc.top);
    int col = static_cast<int>(rc.left);

    SetNCColour(ncp, fore, true);
    SetNCColour(ncp, back, false);

    const FontImpl *fi = static_cast<const FontImpl *>(font_);
    if (fi) {
        ncplane_set_styles(ncp, fi->attrs);
    } else {
        ncplane_set_styles(ncp, NCSTYLE_NONE);
    }

    /* Build a null-terminated copy for ncplane_putstr_yx */
    std::string buf(text);
    ncplane_putstr_yx(ncp, row, col, buf.c_str());

    (void)ybase;
}

void SurfaceImpl::DrawTextClipped(PRectangle rc, const Font *font_, XYPOSITION ybase,
                                   std::string_view text, ColourRGBA fore, ColourRGBA back) {
    DrawTextNoClip(rc, font_, ybase, text, fore, back);
}

void SurfaceImpl::DrawTextTransparent(PRectangle rc, const Font *font_, XYPOSITION ybase,
                                       std::string_view text, ColourRGBA fore) {
    if (!ncp || text.empty()) return;

    int row = static_cast<int>(rc.top);
    int col = static_cast<int>(rc.left);

    SetNCColour(ncp, fore, true);
    ncplane_set_bg_default(ncp);

    const FontImpl *fi = static_cast<const FontImpl *>(font_);
    if (fi) {
        ncplane_set_styles(ncp, fi->attrs);
    } else {
        ncplane_set_styles(ncp, NCSTYLE_NONE);
    }

    std::string buf(text);
    ncplane_putstr_yx(ncp, row, col, buf.c_str());

    (void)ybase;
}

void SurfaceImpl::MeasureWidths(const Font *font_, std::string_view text,
                                 XYPOSITION *positions) {
    (void)font_;
    if (text.empty()) return;

    XYPOSITION pos = 0;
    size_t i = 0;
    const char *str = text.data();
    size_t len = text.size();

    while (i < len) {
        unsigned char c = static_cast<unsigned char>(str[i]);
        uint32_t ucs;
        int bytes;

        if (c < 0x80) {
            ucs = c;
            bytes = 1;
        } else if ((c & 0xE0) == 0xC0) {
            ucs = c & 0x1F;
            bytes = 2;
        } else if ((c & 0xF0) == 0xE0) {
            ucs = c & 0x0F;
            bytes = 3;
        } else if ((c & 0xF8) == 0xF0) {
            ucs = c & 0x07;
            bytes = 4;
        } else {
            /* Invalid byte: treat as single-width replacement */
            positions[i] = pos + 1;
            i++;
            pos += 1;
            continue;
        }

        /* Clamp bytes to remaining length to avoid reading past the buffer */
        if (i + static_cast<size_t>(bytes) > len)
            bytes = static_cast<int>(len - i);

        /* Decode continuation bytes, validating format */
        for (int b = 1; b < bytes; b++) {
            unsigned char cb = static_cast<unsigned char>(str[i + b]);
            if ((cb & 0xC0) != 0x80) {
                /* Truncated sequence — treat remaining as single-width */
                bytes = b;
                break;
            }
            ucs = (ucs << 6) | (cb & 0x3F);
        }

        int w = scinterm_wcwidth(ucs);
        if (w < 0) w = 1;  /* treat control chars as 1 column wide */
        pos += static_cast<XYPOSITION>(w);

        /* Assign the cumulative position to all bytes of this codepoint.
         * i + b < len is guaranteed because bytes was clamped above. */
        for (int b = 0; b < bytes; b++) {
            positions[i + static_cast<size_t>(b)] = pos;
        }
        i += static_cast<size_t>(bytes);
    }
}

XYPOSITION SurfaceImpl::WidthText(const Font *font_, std::string_view text) {
    (void)font_;
    return static_cast<XYPOSITION>(scinterm_wcswidth_utf8(text.data(), static_cast<int>(text.size())));
}

void SurfaceImpl::DrawTextNoClipUTF8(PRectangle rc, const Font *font_, XYPOSITION ybase,
                                      std::string_view text, ColourRGBA fore, ColourRGBA back) {
    DrawTextNoClip(rc, font_, ybase, text, fore, back);
}

void SurfaceImpl::DrawTextClippedUTF8(PRectangle rc, const Font *font_, XYPOSITION ybase,
                                       std::string_view text, ColourRGBA fore, ColourRGBA back) {
    DrawTextNoClip(rc, font_, ybase, text, fore, back);
}

void SurfaceImpl::DrawTextTransparentUTF8(PRectangle rc, const Font *font_, XYPOSITION ybase,
                                           std::string_view text, ColourRGBA fore) {
    DrawTextTransparent(rc, font_, ybase, text, fore);
}

void SurfaceImpl::MeasureWidthsUTF8(const Font *font_, std::string_view text,
                                     XYPOSITION *positions) {
    MeasureWidths(font_, text, positions);
}

XYPOSITION SurfaceImpl::WidthTextUTF8(const Font *font_, std::string_view text) {
    return WidthText(font_, text);
}

XYPOSITION SurfaceImpl::Ascent(const Font *font_) { (void)font_; return 1; }
XYPOSITION SurfaceImpl::Descent(const Font *font_) { (void)font_; return 0; }
XYPOSITION SurfaceImpl::InternalLeading(const Font *font_) { (void)font_; return 0; }
XYPOSITION SurfaceImpl::Height(const Font *font_) { (void)font_; return 1; }
XYPOSITION SurfaceImpl::AverageCharWidth(const Font *font_) { (void)font_; return 1; }

void SurfaceImpl::SetClip(PRectangle rc) { (void)rc; }
void SurfaceImpl::PopClip() {}
void SurfaceImpl::FlushCachedState() {}
void SurfaceImpl::FlushDrawing() {}

void SurfaceImpl::DrawLineMarker(const PRectangle &rcWhole, const Font *fontForCharacter,
                                  int tFold, const void *data) {
    (void)rcWhole; (void)fontForCharacter; (void)tFold; (void)data;
}

void SurfaceImpl::DrawWrapMarker(PRectangle rcPlace, bool isEndMarker, ColourRGBA wrapColour) {
    (void)rcPlace; (void)isEndMarker; (void)wrapColour;
}

void SurfaceImpl::DrawTabArrow(PRectangle rcTab, const ViewStyle &vsDraw) {
    (void)rcTab; (void)vsDraw;
}

/* Factory function required by Scintilla */
std::unique_ptr<Surface> Surface::Allocate(Scintilla::Technology technology) {
    (void)technology;
    return std::make_unique<SurfaceImpl>();
}

/*=============================================================================
 * Window implementation
 *===========================================================================*/

Window::~Window() noexcept {}

void Window::Destroy() noexcept {
    wid = nullptr;
}

PRectangle Window::GetPosition() const {
    if (!wid) return PRectangle(0, 0, 0, 0);
    auto ncp = static_cast<struct ncplane *>(wid);
    int y = 0, x = 0;
    unsigned rows = 0, cols = 0;
    ncplane_yx(ncp, &y, &x);
    ncplane_dim_yx(ncp, &rows, &cols);
    return PRectangle(static_cast<XYPOSITION>(x), static_cast<XYPOSITION>(y),
                      static_cast<XYPOSITION>(x + static_cast<int>(cols)),
                      static_cast<XYPOSITION>(y + static_cast<int>(rows)));
}

void Window::SetPosition(PRectangle rc) {
    if (!wid) return;
    auto ncp = static_cast<struct ncplane *>(wid);
    ncplane_resize_simple(ncp, static_cast<unsigned>(rc.Height()),
                          static_cast<unsigned>(rc.Width()));
    ncplane_move_yx(ncp, static_cast<int>(rc.top), static_cast<int>(rc.left));
}

void Window::SetPositionRelative(PRectangle rc, const Window *relativeTo) {
    (void)relativeTo;
    SetPosition(rc);
}

PRectangle Window::GetClientPosition() const {
    return GetPosition();
}

void Window::Show(bool show) { (void)show; }
void Window::InvalidateAll() {}
void Window::InvalidateRectangle(PRectangle rc) { (void)rc; }
void Window::SetCursor(Cursor curs) { (void)curs; }

PRectangle Window::GetMonitorRect(Point pt) {
    (void)pt;
    if (!GetStdPlane()) return PRectangle(0, 0, 0, 0);
    unsigned rows = 0, cols = 0;
    ncplane_dim_yx(GetStdPlane(), &rows, &cols);
    return PRectangle(0, 0, static_cast<XYPOSITION>(cols), static_cast<XYPOSITION>(rows));
}

/*=============================================================================
 * ListBox implementation
 *===========================================================================*/

ListBox::ListBox() noexcept {}
ListBox::~ListBox() noexcept {}

std::unique_ptr<ListBox> ListBox::Allocate() {
    return std::make_unique<ListBoxImpl>();
}

ListBoxImpl::ListBoxImpl() : ncp(nullptr), height(5), width(20), selection(0),
    lastDimY(0), lastDimX(0), delegate(nullptr) {
    memset(types, 0, sizeof(types));
}

ListBoxImpl::~ListBoxImpl() {
    if (ncp) {
        ncplane_destroy(ncp);
        ncp = nullptr;
    }
}

void ListBoxImpl::SetFont(const Font *font) {
    (void)font;
}

void ListBoxImpl::Create(Window &parent, int ctrlID, Point location_,
                          int lineHeight_, bool unicodeMode_,
                          Scintilla::Technology technology_) {
    (void)ctrlID; (void)lineHeight_; (void)unicodeMode_; (void)technology_;

    /* Destroy any pre-existing plane to avoid resource leak on re-creation */
    if (ncp) {
        ncplane_destroy(ncp);
        ncp = nullptr;
        wid = nullptr;
    }

    struct ncplane *parentPlane = static_cast<struct ncplane *>(parent.GetID());
    if (!parentPlane) return;

    struct ncplane_options opts = {};
    opts.y = static_cast<int>(location_.y);
    opts.x = static_cast<int>(location_.x);
    opts.rows = static_cast<unsigned>(height > 0 ? height : 5);
    opts.cols = static_cast<unsigned>(width > 0 ? width : 20);
    ncp = ncplane_create(parentPlane, &opts);
    if (ncp) {
        wid = ncp;
    }
}

void ListBoxImpl::SetAverageCharWidth(int width_) {
    (void)width_;
}

void ListBoxImpl::SetVisibleRows(int rows) {
    if (rows > 0) height = rows;
}

int ListBoxImpl::GetVisibleRows() const {
    return height;
}

PRectangle ListBoxImpl::GetDesiredRect() {
    return PRectangle(0, 0,
                      static_cast<XYPOSITION>(width),
                      static_cast<XYPOSITION>(height));
}

int ListBoxImpl::CaretFromEdge() {
    return 4;
}

void ListBoxImpl::Clear() noexcept {
    list.clear();
    selection = 0;
}

void ListBoxImpl::Append(char *s, int type) {
    (void)type;
    if (s) list.emplace_back(s);
}

int ListBoxImpl::Length() {
    return static_cast<int>(list.size());
}

void ListBoxImpl::Select(int n) {
    selection = n;
    if (!ncp) return;

    /* Render the listbox */
    ncplane_erase(ncp);
    int rows = static_cast<int>(list.size());
    int visRows = height;
    int startIdx = 0;
    if (n >= visRows) startIdx = n - visRows + 1;
    if (startIdx < 0) startIdx = 0;

    for (int i = 0; i < visRows && (startIdx + i) < rows; i++) {
        int itemIdx = startIdx + i;
        if (itemIdx == n) {
            /* Highlight selected */
            ncplane_set_fg_rgb8(ncp, 0, 0, 0);
            ncplane_set_bg_rgb8(ncp, 0xAA, 0xAA, 0xFF);
        } else {
            ncplane_set_fg_rgb8(ncp, 0xFF, 0xFF, 0xFF);
            ncplane_set_bg_rgb8(ncp, 0x20, 0x20, 0x20);
        }
        ncplane_putstr_yx(ncp, i, 0, list[static_cast<size_t>(itemIdx)].c_str());
    }
}

int ListBoxImpl::GetSelection() {
    return selection;
}

int ListBoxImpl::Find(const char *prefix) {
    if (!prefix) return -1;
    std::string pfx(prefix);
    for (size_t i = 0; i < list.size(); i++) {
        if (list[i].find(pfx) == 0)
            return static_cast<int>(i);
    }
    return -1;
}

std::string ListBoxImpl::GetValue(int n) {
    if (n < 0 || n >= static_cast<int>(list.size())) return {};
    return list[static_cast<size_t>(n)];
}

void ListBoxImpl::RegisterImage(int type, const char *xpm_data) {
    (void)type; (void)xpm_data;
}

void ListBoxImpl::RegisterRGBAImage(int type, int width_, int height_,
                                     const unsigned char *pixelsImage) {
    (void)type; (void)width_; (void)height_; (void)pixelsImage;
}

void ListBoxImpl::ClearRegisteredImages() {}

void ListBoxImpl::SetDelegate(IListBoxDelegate *lbDelegate) {
    delegate = lbDelegate;
}

void ListBoxImpl::SetList(const char *listStr, char separator, char typesep) {
    Clear();
    if (!listStr) return;

    std::string s;
    for (const char *p = listStr; *p; p++) {
        if (*p == separator) {
            /* Find typesep if present */
            auto pos = s.find(typesep);
            if (pos != std::string::npos) s = s.substr(0, pos);
            if (!s.empty()) {
                list.push_back(s);
                s.clear();
            }
        } else {
            s += *p;
        }
    }
    if (!s.empty()) {
        auto pos = s.find(typesep);
        if (pos != std::string::npos) s = s.substr(0, pos);
        if (!s.empty()) list.push_back(s);
    }
}

void ListBoxImpl::SetOptions(ListOptions options_) {
    (void)options_;
}

/*=============================================================================
 * Menu implementation
 *===========================================================================*/

Menu::Menu() noexcept : mid(nullptr) {}

void Menu::CreatePopUp() {}

void Menu::Destroy() noexcept {
    mid = nullptr;
}

void Menu::Show(Point pt, const Window &w) {
    (void)pt; (void)w;
}

/*=============================================================================
 * Platform namespace implementation
 *===========================================================================*/

namespace Platform {

ColourRGBA Chrome() { return ColourRGBA(0xC0, 0xC0, 0xC0); }
ColourRGBA ChromeHighlight() { return ColourRGBA(0xFF, 0xFF, 0xFF); }
const char *DefaultFont() { return "terminal"; }
int DefaultFontSize() { return 10; }
unsigned int DoubleClickTime() { return 500; }

void DebugDisplay(const char *s) noexcept {
    fprintf(stderr, "%s", s);
}

void DebugPrintf(const char *format, ...) noexcept {
    va_list ap;
    va_start(ap, format);
    vfprintf(stderr, format, ap);
    va_end(ap);
}

bool ShowAssertionPopUps(bool /*assertionPopUps*/) noexcept {
    return false;
}

void Assert(const char *c, const char *file, int line) noexcept {
    fprintf(stderr, "Assertion [%s] failed at %s %d\n", c, file, line);
    abort();
}

} // namespace Platform

} // namespace Scintilla::Internal

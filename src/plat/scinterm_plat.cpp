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
#include <climits>

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

/*=============================================================================
 * Global render-frame arena with thread-safety
 *===========================================================================*/

Arena g_render_arena = {};
std::mutex g_arena_mutex;

/**
 * @brief Thread-safe arena reset
 */
static inline void arena_reset_safe(Arena *a) {
    std::lock_guard<std::mutex> lock(g_arena_mutex);
    arena_reset(a);
}

/**
 * @brief Thread-safe arena allocation
 */
static inline void *arena_alloc_safe(Arena *a, size_t size) {
    std::lock_guard<std::mutex> lock(g_arena_mutex);
    return arena_alloc(a, size);
}

bool InitNotCurses(uint64_t extra_ncopts) {
    struct notcurses_options opts = {};
    opts.flags = NCOPTION_SUPPRESS_BANNERS | extra_ncopts;
    g_nc = notcurses_init(&opts, nullptr);
    if (!g_nc) return false;
    g_stdplane = notcurses_stdplane(g_nc);
    if (!g_stdplane) {
        notcurses_stop(g_nc);
        g_nc = nullptr;
        return false;
    }
    if (!arena_init(&g_render_arena, SCINTERM_ARENA_SIZE)) {
        notcurses_stop(g_nc);
        g_nc = nullptr;
        g_stdplane = nullptr;
        return false;
    }
    return true;
}

void ShutdownNotCurses() {
    if (g_nc) {
        notcurses_stop(g_nc);
        g_nc = nullptr;
        g_stdplane = nullptr;
    }
    arena_free(&g_render_arena);
}

struct notcurses* GetNotCurses() { return g_nc; }
struct ncplane* GetStdPlane() { return g_stdplane; }

/*=============================================================================
 * Background alpha
 *===========================================================================*/

static unsigned g_bg_alpha_nc = NCALPHA_OPAQUE;

void SetBgAlpha(int pct) {
    /* NCALPHA_BLEND only mixes between notcurses planes; since the stdplane is
     * transparent there is nothing below to blend with, so BLEND produces no
     * visible change.  Any non-zero percentage maps to NCALPHA_TRANSPARENT so
     * that cells use the terminal's own default background, which respects the
     * terminal's window-level transparency (e.g. WezTerm window_background_opacity). */
    g_bg_alpha_nc = (pct > 0) ? NCALPHA_TRANSPARENT : NCALPHA_OPAQUE;
}

unsigned GetBgAlphaNc() { return g_bg_alpha_nc; }

/*=============================================================================
 * Helper utilities
 *===========================================================================*/

static inline void SetNCColour(struct ncplane *ncp, ColourRGBA c, bool fg) {
    uint8_t r = c.GetRed(), g = c.GetGreen(), b = c.GetBlue();
    if (fg) ncplane_set_fg_rgb8(ncp, r, g, b);
    else    ncplane_set_bg_rgb8(ncp, r, g, b);
}

/*=============================================================================
 * Safe string output with bounds checking
 * Security-hardened version with proper UTF-8 validation
 *===========================================================================*/

/**
 * @brief Validate UTF-8 continuation byte
 * @param c Byte to check
 * @return true if valid continuation byte (0x80-0xBF)
 */
static inline bool is_utf8_continuation(unsigned char c) {
    return (c & 0xC0) == 0x80;
}

/**
 * @brief Get UTF-8 sequence length from lead byte (with validation)
 * @param c Lead byte
 * @return Sequence length (1-4), or 0 if invalid
 */
static inline int utf8_sequence_length(unsigned char c) {
    if (c < 0x80) return 1;           // ASCII
    if ((c & 0xE0) == 0xC0) return 2; // 2-byte
    if ((c & 0xF0) == 0xE0) return 3; // 3-byte
    if ((c & 0xF8) == 0xF0) return 4; // 4-byte
    return 0;                         // Invalid lead byte
}

/**
 * @brief Safely put a string to a plane, truncating to plane width
 * 
 * SECURITY FIXES:
 * - Proper UTF-8 validation to prevent over-read
 * - Bounds checking for all buffer accesses
 * - Overflow-safe arithmetic
 * - Invalid UTF-8 sequences treated as single bytes
 * 
 * @param ncp Target plane
 * @param row Row coordinate
 * @param col Column coordinate  
 * @param str String to write (null-terminated)
 * @param max_width Maximum width to write (0 = use plane width)
 * @return int Number of columns written, or -1 on error
 */
static int safe_putstr_yx(struct ncplane *ncp, int row, int col, const char *str, int max_width = 0) {
    if (!ncp || !str) return -1;
    if (row < 0 || col < 0) return -1;
    
    // Get plane dimensions for bounds checking
    unsigned plane_rows = 0, plane_cols = 0;
    ncplane_dim_yx(ncp, &plane_rows, &plane_cols);
    
    // Validate coordinates
    if (static_cast<unsigned>(row) >= plane_rows || static_cast<unsigned>(col) >= plane_cols) {
        return -1;
    }
    
    if (max_width <= 0) {
        max_width = static_cast<int>(plane_cols) - col;
        if (max_width <= 0) return 0;
    }
    
    // Clamp max_width to available space
    int available_cols = static_cast<int>(plane_cols) - col;
    if (max_width > available_cols) {
        max_width = available_cols;
    }
    
    size_t len = strlen(str);
    if (len == 0) return 0;
    
    // Limit string length to prevent excessive processing
    constexpr size_t MAX_PROCESS_LEN = 65536;
    if (len > MAX_PROCESS_LEN) {
        len = MAX_PROCESS_LEN;
    }
    
    // Fast path: check if entire string fits
    int total_width = scinterm_wcswidth_utf8(str, static_cast<int>(len));
    if (total_width < 0) total_width = static_cast<int>(len);
    
    if (total_width <= max_width) {
        // Fits entirely - use ncplane_putstr_yx directly
        return ncplane_putstr_yx(ncp, row, col, str);
    }
    
    // Need to truncate - iterate carefully with bounds checking
    int cols_used = 0;
    size_t byte_pos = 0;
    const char *p = str;
    const char *const end = str + len;
    
    while (p < end && cols_used < max_width) {
        unsigned char c = static_cast<unsigned char>(*p);
        
        // ASCII fast path
        if (c < 0x80) {
            int char_width = (c >= 0x20 && c != 0x7F) ? 1 : 0;
            if (cols_used + char_width > max_width) {
                break;
            }
            cols_used += char_width;
            byte_pos++;
            p++;
            continue;
        }
        
        // Multi-byte UTF-8
        int seq_len = utf8_sequence_length(c);
        
        // Invalid lead byte - treat as single byte replacement
        if (seq_len == 0 || seq_len > 4) {
            if (cols_used + 1 > max_width) break;
            cols_used += 1;
            byte_pos++;
            p++;
            continue;
        }
        
        // Check if we have enough bytes for the full sequence
        if (p + seq_len > end) {
            // Truncated sequence at end - treat as single bytes
            if (cols_used + 1 > max_width) break;
            cols_used += 1;
            byte_pos++;
            p++;
            continue;
        }
        
        // Validate continuation bytes
        bool valid = true;
        for (int i = 1; i < seq_len; i++) {
            if (!is_utf8_continuation(static_cast<unsigned char>(p[i]))) {
                valid = false;
                break;
            }
        }
        
        if (!valid) {
            // Invalid sequence - treat as single byte
            if (cols_used + 1 > max_width) break;
            cols_used += 1;
            byte_pos++;
            p++;
            continue;
        }
        
        // Decode and get width
        uint32_t ucs = 0;
        switch (seq_len) {
            case 2: ucs = c & 0x1F; break;
            case 3: ucs = c & 0x0F; break;
            case 4: ucs = c & 0x07; break;
        }
        
        for (int i = 1; i < seq_len; i++) {
            ucs = (ucs << 6) | (static_cast<unsigned char>(p[i]) & 0x3F);
        }
        
        // Validate decoded codepoint
        if (ucs > 0x10FFFF || (ucs >= 0xD800 && ucs <= 0xDFFF)) {
            // Invalid Unicode - treat as single byte
            if (cols_used + 1 > max_width) break;
            cols_used += 1;
            byte_pos++;
            p++;
            continue;
        }
        
        int char_width = scinterm_wcwidth(ucs);
        if (char_width < 0) char_width = 1;  // Control chars as width 1
        
        if (cols_used + char_width > max_width) {
            break;
        }
        
        cols_used += char_width;
        byte_pos += static_cast<size_t>(seq_len);
        p += seq_len;
    }
    
    // Overflow check for byte_pos
    if (byte_pos > len || byte_pos > MAX_PROCESS_LEN) {
        byte_pos = len;
    }
    
    // Stack buffer for small truncations, heap for large
    constexpr size_t STACK_BUF_SIZE = 512;
    char stack_buf[STACK_BUF_SIZE];
    char *truncated = nullptr;
    bool used_stack = false;
    
    if (byte_pos < STACK_BUF_SIZE - 1) {
        truncated = stack_buf;
        used_stack = true;
    } else {
        // Overflow check before malloc
        if (byte_pos > SIZE_MAX / 2) return -1;
        truncated = static_cast<char *>(malloc(byte_pos + 1));
        if (!truncated) return -1;
    }
    
    memcpy(truncated, str, byte_pos);
    truncated[byte_pos] = '\0';
    int result = ncplane_putstr_yx(ncp, row, col, truncated);
    
    if (!used_stack) {
        free(truncated);
    }
    
    return result;
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
     * Position it at (0,0) but with zero size to hide it. */
    struct ncplane *parent = GetStdPlane();
    if (!parent) parent = ncp;
    if (parent) {
        // Validate dimensions
        if (width <= 0) width = 1;
        if (height <= 0) height = 1;
        // Cap at reasonable maximum to prevent memory issues
        if (width > 10000) width = 10000;
        if (height > 10000) height = 10000;
        
        struct ncplane_options opts = {};
        opts.y = 0;
        opts.x = 0;
        opts.rows = static_cast<unsigned>(height);
        opts.cols = static_cast<unsigned>(width);
        struct ncplane *plane = ncplane_create(parent, &opts);
        if (plane) {
            // Move off-screen after creation
            ncplane_move_yx(plane, -height, 0);
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

/**
 * @brief Clamp integer value to [min, max] range safely
 * Security: Prevents overflow/underflow
 */
template<typename T>
static inline T safe_clamp(T value, T min_val, T max_val) {
    if (value < min_val) return min_val;
    if (value > max_val) return max_val;
    return value;
}

/**
 * @brief Convert unsigned dimension to signed safely
 * Security: Returns -1 if value exceeds int max
 */
static inline int safe_dim_to_int(unsigned dim) {
    if (dim > static_cast<unsigned>(std::numeric_limits<int>::max())) {
        return std::numeric_limits<int>::max();
    }
    return static_cast<int>(dim);
}

void SurfaceImpl::FillRectangle(PRectangle rc, Fill fill) {
    if (!ncp) return;

    // Convert with bounds checking
    int left = static_cast<int>(std::floor(rc.left));
    int top = static_cast<int>(std::floor(rc.top));
    int right = static_cast<int>(std::ceil(rc.right));
    int bottom = static_cast<int>(std::ceil(rc.bottom));

    // Early rejection of invalid rectangles
    // SECURITY: Check for overflow in coordinate calculations
    if (right < left || bottom < top) return;
    if (right - left > 10000 || bottom - top > 10000) return;  // Sanity limit
    
    // Clamp to valid range
    left = safe_clamp(left, 0, std::numeric_limits<int>::max());
    top = safe_clamp(top, 0, std::numeric_limits<int>::max());
    
    // Get plane dimensions
    unsigned plane_rows_u = 0, plane_cols_u = 0;
    ncplane_dim_yx(ncp, &plane_rows_u, &plane_cols_u);
    
    // Safe conversion to signed
    int plane_rows = safe_dim_to_int(plane_rows_u);
    int plane_cols = safe_dim_to_int(plane_cols_u);
    
    // Clamp right/bottom to plane dimensions
    right = safe_clamp(right, left, plane_cols);
    bottom = safe_clamp(bottom, top, plane_rows);
    
    if (right <= left || bottom <= top) return;

    SetNCColour(ncp, fill.colour, false);
    ncplane_set_fg_default(ncp);
    ncplane_set_styles(ncp, NCSTYLE_NONE);

    // OPTIMIZATION: Use line-based fill instead of cell-by-cell
    const int width = right - left;
    if (width <= 0) return;
    
    // For small widths, cell-by-cell is fine
    // For larger widths, create a fill line
    constexpr int FILL_LINE_THRESHOLD = 16;
    
    if (width >= FILL_LINE_THRESHOLD) {
        // Allocate fill line on stack for common cases
        constexpr int STACK_FILL_SIZE = 256;
        char stack_fill[STACK_FILL_SIZE];
        char *fill_line = nullptr;
        bool use_heap = false;
        
        if (width < STACK_FILL_SIZE) {
            fill_line = stack_fill;
        } else {
            // SECURITY: Limit heap allocation size
            constexpr int MAX_FILL_SIZE = 4096;
            if (width > MAX_FILL_SIZE) return;
            fill_line = static_cast<char *>(malloc(static_cast<size_t>(width) + 1));
            if (!fill_line) return;
            use_heap = true;
        }
        
        memset(fill_line, ' ', static_cast<size_t>(width));
        fill_line[width] = '\0';
        
        for (int row = top; row < bottom; row++) {
            ncplane_putstr_yx(ncp, row, left, fill_line);
        }
        
        if (use_heap) {
            free(fill_line);
        }
    } else {
        // Cell-by-cell for small rectangles
        for (int row = top; row < bottom; row++) {
            for (int col = left; col < right; col++) {
                ncplane_putchar_yx(ncp, row, col, ' ');
            }
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

    // Validate all coordinates
    if (srcX < 0 || srcY < 0 || dstX < 0 || dstY < 0) return;
    if (width <= 0 || height <= 0) return;
    
    // Get source and destination dimensions
    unsigned src_rows = 0, src_cols = 0;
    unsigned dst_rows = 0, dst_cols = 0;
    ncplane_dim_yx(src.ncp, &src_rows, &src_cols);
    ncplane_dim_yx(ncp, &dst_rows, &dst_cols);
    
    // Clamp to source bounds
    if (static_cast<unsigned>(srcX) >= src_cols || static_cast<unsigned>(srcY) >= src_rows) return;
    if (static_cast<unsigned>(srcX + width) > src_cols) width = static_cast<int>(src_cols) - srcX;
    if (static_cast<unsigned>(srcY + height) > src_rows) height = static_cast<int>(src_rows) - srcY;
    
    // Clamp to destination bounds
    if (static_cast<unsigned>(dstX) >= dst_cols || static_cast<unsigned>(dstY) >= dst_rows) return;
    if (static_cast<unsigned>(dstX + width) > dst_cols) width = static_cast<int>(dst_cols) - dstX;
    if (static_cast<unsigned>(dstY + height) > dst_rows) height = static_cast<int>(dst_rows) - dstY;
    
    if (width <= 0 || height <= 0) return;

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

/*=============================================================================
 * Thread-local text buffer for DrawText operations
 * Eliminates repeated heap allocations during rendering
 *===========================================================================*/

/**
 * @brief Thread-local buffer for text rendering
 * 
 * PERFORMANCE: This buffer is reused across DrawText calls, eliminating
 * malloc/free overhead in the render hot path. It grows as needed but
 * never shrinks, amortizing allocation cost over many frames.
 */
static thread_local std::vector<char> g_tls_text_buffer;

/**
 * @brief Get thread-local buffer with at least min_size capacity
 * @param min_size Minimum required capacity
 * @return Pointer to buffer data
 */
static char* get_tls_buffer(size_t min_size) {
    if (g_tls_text_buffer.size() < min_size) {
        // Grow with some headroom to avoid frequent reallocations
        size_t new_size = min_size + 256;
        // Cap at reasonable maximum to prevent runaway growth
        constexpr size_t MAX_BUFFER_SIZE = 65536;
        if (new_size > MAX_BUFFER_SIZE) {
            new_size = MAX_BUFFER_SIZE;
        }
        g_tls_text_buffer.resize(new_size);
    }
    return g_tls_text_buffer.data();
}

void SurfaceImpl::DrawTextNoClip(PRectangle rc, const Font *font_, XYPOSITION ybase,
                                  std::string_view text, ColourRGBA fore, ColourRGBA back) {
    if (!ncp || text.empty()) return;

    int row = static_cast<int>(rc.top);
    int col = static_cast<int>(rc.left);
    
    // Validate coordinates
    if (row < 0 || col < 0) return;

    SetNCColour(ncp, fore, true);
    SetNCColour(ncp, back, false);

    const FontImpl *fi = static_cast<const FontImpl *>(font_);
    if (fi) {
        ncplane_set_styles(ncp, fi->attrs);
    } else {
        ncplane_set_styles(ncp, NCSTYLE_NONE);
    }

    size_t tlen = text.size();
    
    // Use thread-local buffer (no heap allocation in steady state)
    char *buf = get_tls_buffer(tlen + 1);
    std::memcpy(buf, text.data(), tlen);
    buf[tlen] = '\0';
    
    // Get plane width for truncation
    unsigned plane_cols = ncplane_dim_x(ncp);
    if (static_cast<unsigned>(col) < plane_cols) {
        safe_putstr_yx(ncp, row, col, buf, static_cast<int>(plane_cols) - col);
    }

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
    
    // Validate coordinates
    if (row < 0 || col < 0) return;

    SetNCColour(ncp, fore, true);
    ncplane_set_bg_default(ncp);

    const FontImpl *fi = static_cast<const FontImpl *>(font_);
    if (fi) {
        ncplane_set_styles(ncp, fi->attrs);
    } else {
        ncplane_set_styles(ncp, NCSTYLE_NONE);
    }

    // PERFORMANCE: Use thread-local buffer (no heap allocation)
    size_t tlen = text.size();
    char *buf = get_tls_buffer(tlen + 1);
    std::memcpy(buf, text.data(), tlen);
    buf[tlen] = '\0';
    
    unsigned plane_cols = ncplane_dim_x(ncp);
    if (static_cast<unsigned>(col) < plane_cols) {
        safe_putstr_yx(ncp, row, col, buf, static_cast<int>(plane_cols) - col);
    }

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
    if (!ncp || !data) return;
    (void)fontForCharacter; (void)tFold;

    const auto *lm = static_cast<const LineMarker *>(data);

    /* Map Scintilla marker types to terminal characters.
     * The fold margin is 1 cell wide, so we use single ASCII/Unicode chars. */
    const char *ch = nullptr;
    using MS = MarkerSymbol;
    switch (lm->markType) {
        case MS::BoxPlus:              ch = "+"; break;
        case MS::BoxMinus:             ch = "-"; break;
        case MS::BoxPlusConnected:     ch = "+"; break;
        case MS::BoxMinusConnected:    ch = "-"; break;
        case MS::VLine:                ch = "\xe2\x94\x82"; break;
        case MS::LCorner:              ch = "\xe2\x94\x94"; break;
        case MS::LCornerCurve:         ch = "\xe2\x95\xb0"; break;
        case MS::TCorner:              ch = "\xe2\x94\x9c"; break;
        case MS::TCornerCurve:         ch = "\xe2\x94\x9c"; break;
        case MS::Plus:                 ch = "+"; break;
        case MS::Minus:                ch = "-"; break;
        case MS::Arrow:                ch = ">"; break;
        case MS::ArrowDown:            ch = "v"; break;
        case MS::CirclePlus:           ch = "+"; break;
        case MS::CircleMinus:          ch = "-"; break;
        case MS::CirclePlusConnected:  ch = "+"; break;
        case MS::CircleMinusConnected: ch = "-"; break;
        default:                       return;
    }

    int row = static_cast<int>(rcWhole.top);
    int col = static_cast<int>(rcWhole.left);
    
    // Validate coordinates
    if (row < 0 || col < 0) return;
    
    // Check plane bounds
    unsigned plane_rows = 0, plane_cols = 0;
    ncplane_dim_yx(ncp, &plane_rows, &plane_cols);
    if (static_cast<unsigned>(row) >= plane_rows || static_cast<unsigned>(col) >= plane_cols) {
        return;
    }

    SetNCColour(ncp, lm->fore, true);
    SetNCColour(ncp, lm->back, false);
    ncplane_set_styles(ncp, NCSTYLE_NONE);
    ncplane_putstr_yx(ncp, row, col, ch);
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
    unsigned rows = 0, cols = 0;
    ncplane_dim_yx(ncp, &rows, &cols);
    return PRectangle(0, 0,
                      static_cast<XYPOSITION>(cols),
                      static_cast<XYPOSITION>(rows));
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

    // Validate dimensions
    if (height <= 0) height = 5;
    if (width <= 0) width = 20;
    if (height > 1000) height = 1000;  // Sanity cap
    if (width > 1000) width = 1000;
    
    // Check parent dimensions
    unsigned parent_rows = 0, parent_cols = 0;
    ncplane_dim_yx(parentPlane, &parent_rows, &parent_cols);
    
    // Ensure listbox fits within parent
    if (static_cast<int>(location_.y) + height > static_cast<int>(parent_rows)) {
        height = static_cast<int>(parent_rows) - static_cast<int>(location_.y);
        if (height <= 0) height = static_cast<int>(parent_rows) / 2;
    }
    if (static_cast<int>(location_.x) + width > static_cast<int>(parent_cols)) {
        width = static_cast<int>(parent_cols) - static_cast<int>(location_.x);
        if (width <= 0) width = static_cast<int>(parent_cols) / 2;
    }

    struct ncplane_options opts = {};
    opts.y = static_cast<int>(location_.y);
    opts.x = static_cast<int>(location_.x);
    opts.rows = static_cast<unsigned>(height);
    opts.cols = static_cast<unsigned>(width);
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
    if (s) {
        // Limit string length to prevent excessive memory usage
        size_t len = strlen(s);
        if (len > 1024) {
            // Truncate very long strings
            std::string truncated(s, 1024);
            list.push_back(truncated);
        } else {
            list.emplace_back(s);
        }
    }
}

int ListBoxImpl::Length() {
    return static_cast<int>(list.size());
}

void ListBoxImpl::Select(int n) {
    selection = n;
    if (!ncp) return;

    // Validate selection index
    if (n < 0 || n >= static_cast<int>(list.size())) {
        // Still valid to set selection, but render nothing selected
        if (n < 0) return;
    }

    /* Render the listbox */
    ncplane_erase(ncp);
    int rows = static_cast<int>(list.size());
    int visRows = height;
    int startIdx = 0;
    if (n >= visRows) startIdx = n - visRows + 1;
    if (startIdx < 0) startIdx = 0;
    
    // Get actual plane dimensions
    unsigned plane_rows = 0, plane_cols = 0;
    ncplane_dim_yx(ncp, &plane_rows, &plane_cols);
    int max_width = static_cast<int>(plane_cols);

    for (int i = 0; i < visRows && (startIdx + i) < rows; i++) {
        int itemIdx = startIdx + i;
        if (itemIdx < 0 || itemIdx >= static_cast<int>(list.size())) continue;
        
        // Skip if row is outside plane bounds
        if (static_cast<unsigned>(i) >= plane_rows) continue;
        
        if (itemIdx == n) {
            /* Highlight selected */
            ncplane_set_fg_rgb8(ncp, 0, 0, 0);
            ncplane_set_bg_rgb8(ncp, 0xAA, 0xAA, 0xFF);
        } else {
            ncplane_set_fg_rgb8(ncp, 0xFF, 0xFF, 0xFF);
            ncplane_set_bg_rgb8(ncp, 0x20, 0x20, 0x20);
        }
        
        // Use safe string output with truncation
        const char *str = list[static_cast<size_t>(itemIdx)].c_str();
        safe_putstr_yx(ncp, i, 0, str, max_width);
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
                // Limit size of individual items
                if (s.length() > 1024) {
                    s.resize(1024);
                }
                list.push_back(s);
                s.clear();
            }
        } else {
            s += *p;
            // Prevent excessive accumulation
            if (s.length() > 2048) {
                s.resize(1024);  // Truncate and add
                list.push_back(s);
                s.clear();
            }
        }
    }
    if (!s.empty()) {
        auto pos = s.find(typesep);
        if (pos != std::string::npos) s = s.substr(0, pos);
        if (!s.empty()) {
            if (s.length() > 1024) {
                s.resize(1024);
            }
            list.push_back(s);
        }
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
    (void)s; /* no-op: stderr writes corrupt notcurses display */
}

void DebugPrintf(const char *format, ...) noexcept {
    (void)format; /* no-op: stderr writes corrupt notcurses display */
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

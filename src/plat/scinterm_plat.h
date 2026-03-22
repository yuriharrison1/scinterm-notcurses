/**
 * @file scinterm_plat.h
 * @brief Platform abstraction layer for Scinterm NotCurses
 *
 * This header defines the platform-specific classes that implement
 * the Scintilla platform interface using NotCurses.
 *
 * Copyright 2012-2026 Mitchell. See LICENSE.
 */

#ifndef SCINTERM_PLAT_H
#define SCINTERM_PLAT_H

/* Maximum number of registered images for autocomplete */
#ifndef IMAGE_MAX
#define IMAGE_MAX 31
#endif

/*=============================================================================
 * Per-frame arena allocator
 *
 * A bump-pointer allocator that eliminates malloc/free overhead in the render
 * hot path.  The arena is reset once at the top of each Render() call, so all
 * per-frame scratch memory is reclaimed in O(1) without touching the heap.
 *
 * Default capacity is 1 MB.  Override before including this header:
 *   #define SCINTERM_ARENA_SIZE (2 * 1024 * 1024)
 *===========================================================================*/

/*=============================================================================
 * Configurable compile-time constants
 *
 * Override any of these by defining them before including this header or via
 * a compiler flag (e.g. -DSCINTERM_TARGET_FPS=30).
 *===========================================================================*/

#ifndef SCINTERM_ARENA_SIZE
#define SCINTERM_ARENA_SIZE (1024u * 1024u)   /* 1 MB per-frame scratch arena */
#endif

#ifndef SCINTERM_TARGET_FPS
#define SCINTERM_TARGET_FPS 60                /* maximum rendered frames per second */
#endif

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <mutex>

struct Arena {
    char  *buf;
    size_t pos;
    size_t cap;
};

/** One-time initialisation.  Allocates the backing buffer from the heap.
 *  Returns true on success; the arena is left inert (cap==0) on failure. */
static inline bool arena_init(Arena *a, size_t cap) {
    a->buf = static_cast<char *>(std::malloc(cap));
    a->pos = 0;
    a->cap = a->buf ? cap : 0u;
    return a->buf != nullptr;
}

/** Free the backing buffer and zero the arena descriptor. */
static inline void arena_free(Arena *a) {
    std::free(a->buf);
    a->buf = nullptr;
    a->pos = a->cap = 0u;
}

/** Reset the arena to empty — O(1), no heap traffic. */
static inline void arena_reset(Arena *a) {
    a->pos = 0u;
}

/** Bump-allocate `size` bytes (pointer-aligned).
 *  Returns nullptr when the arena is exhausted; callers must handle this. */
static inline void *arena_alloc(Arena *a, size_t size) {
    constexpr size_t kAlign = sizeof(void *);
    size = (size + kAlign - 1u) & ~(kAlign - 1u);   /* round up */
    if (!a->buf || a->pos + size > a->cap)
        return nullptr;
    void *ptr = a->buf + a->pos;
    a->pos += size;
    return ptr;
}

#include <notcurses/notcurses.h>
#include "Platform.h"
#include "Geometry.h"
#include <vector>
#include <map>
#include <memory>
#include <string>

namespace Scintilla::Internal {

/*=============================================================================
 * Global render-frame arena
 *
 * Defined in scinterm_plat.cpp.  Reset by ScintillaNotCurses::Render() once
 * per frame before the Scintilla paint pass begins.
 * 
 * THREAD SAFETY: The arena is protected by an internal mutex. All access
 * from ScintillaNotCurses::Render() is thread-safe. Direct access to
 * g_render_arena should only be done through arena_alloc_safe() and
 * arena_reset_safe() functions defined in the .cpp file.
 *===========================================================================*/

extern Arena g_render_arena;
extern std::mutex g_arena_mutex;

/*=============================================================================
 * Forward declarations
 *===========================================================================*/

class FontImpl;
class SurfaceImpl;
class ListBoxImpl;

/*=============================================================================
 * Global NotCurses management
 *===========================================================================*/

/**
 * @brief Initialize the global NotCurses context.
 *
 * @param extra_ncopts  Additional NCOPTION_* flags OR-ed into the base flags.
 *                      Pass 0 for default behaviour.
 * @return true if successful, false otherwise.
 */
bool InitNotCurses(uint64_t extra_ncopts = 0);

/**
 * @brief Shut down the global NotCurses context.
 */
void ShutdownNotCurses();

/**
 * @brief Get the global NotCurses context.
 * @return NotCurses context pointer.
 */
struct notcurses* GetNotCurses();

/**
 * @brief Get the standard plane (root plane).
 * @return Standard NotCurses plane.
 */
struct ncplane* GetStdPlane();

/**
 * @brief Set the background alpha level for all Scintilla rendering.
 * @param pct Transparency percentage: 0 = opaque, 1-99 = blend, 100 = fully transparent.
 */
void SetBgAlpha(int pct);

/** Returns the current background notcurses alpha constant. */
unsigned GetBgAlphaNc();

/*=============================================================================
 * Font implementation
 *===========================================================================*/

/**
 * @brief NotCurses font implementation.
 *
 * Since NotCurses doesn't use fonts directly, this class simply stores
 * style attributes (bold, italic, underline) for text rendering.
 */
class FontImpl : public Font {
public:
    explicit FontImpl(const FontParameters &fp);
    ~FontImpl() noexcept override = default;

    unsigned int attrs;  /**< NCSTYLE_* attributes (bold, italic, underline) */
};

/*=============================================================================
 * Surface implementation
 *===========================================================================*/

/**
 * @brief NotCurses surface for drawing.
 *
 * This class implements the Scintilla Surface interface using NotCurses
 * primitives. It handles all drawing operations: text, rectangles, lines,
 * markers, etc.
 */
class SurfaceImpl : public Surface {
    struct ncplane* ncp;
    bool isOwned;   /**< true if this surface owns ncp (must destroy it) */
    PRectangle clip;
    ColourRGBA pixmapColor;
    bool isIndentGuideHighlight;
    bool isCallTip;

public:
    SurfaceImpl();
    ~SurfaceImpl() noexcept override;

    void Init(WindowID wid) override;
    void Init(SurfaceID sid, WindowID wid) override;
    std::unique_ptr<Surface> AllocatePixMap(int width, int height) override;
    void SetMode(SurfaceMode mode) override;
    void Release() noexcept override;
    int SupportsFeature(Scintilla::Supports feature) noexcept override;
    bool Initialised() override;
    int LogPixelsY() override;
    int PixelDivisions() override;
    int DeviceHeightFont(int points) override;

    void LineDraw(Point start, Point end, Stroke stroke) override;
    void PolyLine(const Point *pts, size_t npts, Stroke stroke) override;
    void Polygon(const Point *pts, size_t npts, FillStroke fillStroke) override;
    void RectangleDraw(PRectangle rc, FillStroke fillStroke) override;
    void RectangleFrame(PRectangle rc, Stroke stroke) override;
    void FillRectangle(PRectangle rc, Fill fill) override;
    void FillRectangleAligned(PRectangle rc, Fill fill) override;
    void FillRectangle(PRectangle rc, Surface &surfacePattern) override;
    void RoundedRectangle(PRectangle rc, FillStroke fillStroke) override;
    void AlphaRectangle(PRectangle rc, XYPOSITION cornerSize, FillStroke fillStroke) override;
    void GradientRectangle(PRectangle rc, const std::vector<ColourStop> &stops,
                           GradientOptions options) override;
    void DrawRGBAImage(PRectangle rc, int width, int height,
                       const unsigned char *pixelsImage) override;
    void Ellipse(PRectangle rc, FillStroke fillStroke) override;
    void Stadium(PRectangle rc, FillStroke fillStroke, Ends ends) override;
    void Copy(PRectangle rc, Point from, Surface &surfaceSource) override;
    std::unique_ptr<IScreenLineLayout> Layout(const IScreenLine *screenLine) override;

    void DrawTextNoClip(PRectangle rc, const Font *font_, XYPOSITION ybase,
                        std::string_view text, ColourRGBA fore, ColourRGBA back) override;
    void DrawTextClipped(PRectangle rc, const Font *font_, XYPOSITION ybase,
                         std::string_view text, ColourRGBA fore, ColourRGBA back) override;
    void DrawTextTransparent(PRectangle rc, const Font *font_, XYPOSITION ybase,
                             std::string_view text, ColourRGBA fore) override;
    void MeasureWidths(const Font *font_, std::string_view text, XYPOSITION *positions) override;
    XYPOSITION WidthText(const Font *font_, std::string_view text) override;

    void DrawTextNoClipUTF8(PRectangle rc, const Font *font_, XYPOSITION ybase,
                            std::string_view text, ColourRGBA fore, ColourRGBA back) override;
    void DrawTextClippedUTF8(PRectangle rc, const Font *font_, XYPOSITION ybase,
                             std::string_view text, ColourRGBA fore, ColourRGBA back) override;
    void DrawTextTransparentUTF8(PRectangle rc, const Font *font_, XYPOSITION ybase,
                                 std::string_view text, ColourRGBA fore) override;
    void MeasureWidthsUTF8(const Font *font_, std::string_view text, XYPOSITION *positions) override;
    XYPOSITION WidthTextUTF8(const Font *font_, std::string_view text) override;

    XYPOSITION Ascent(const Font *font_) override;
    XYPOSITION Descent(const Font *font_) override;
    XYPOSITION InternalLeading(const Font *font_) override;
    XYPOSITION Height(const Font *font_) override;
    XYPOSITION AverageCharWidth(const Font *font_) override;

    void SetClip(PRectangle rc) override;
    void PopClip() override;
    void FlushCachedState() override;
    void FlushDrawing() override;

    void DrawLineMarker(const PRectangle &rcWhole, const Font *fontForCharacter,
                        int tFold, const void *data);
    void DrawWrapMarker(PRectangle rcPlace, bool isEndMarker, ColourRGBA wrapColour);
    void DrawTabArrow(PRectangle rcTab, const ViewStyle &vsDraw);
};

/*=============================================================================
 * ListBox implementation (for autocompletion)
 *===========================================================================*/

/**
 * @brief NotCurses list box implementation for autocompletion.
 *
 * This class implements a dropdown list box for autocompletion suggestions,
 * using a separate NotCurses plane.
 */
class ListBoxImpl : public ListBox {
    struct ncplane* ncp;
    int height;
    int width;
    std::vector<std::string> list;
    char types[IMAGE_MAX + 1][5];
    int selection;
    int lastDimY;
    int lastDimX;

public:
    IListBoxDelegate *delegate;

    ListBoxImpl();
    ~ListBoxImpl() override;

    void SetFont(const Font *font) override;
    void Create(Window &parent, int ctrlID, Point location_, int lineHeight_,
                bool unicodeMode_, Scintilla::Technology technology_) override;
    void SetAverageCharWidth(int width) override;
    void SetVisibleRows(int rows) override;
    int GetVisibleRows() const override;
    PRectangle GetDesiredRect() override;
    int CaretFromEdge() override;
    void Clear() noexcept override;
    void Append(char *s, int type = -1) override;
    int Length() override;
    void Select(int n) override;
    int GetSelection() override;
    int Find(const char *prefix) override;
    std::string GetValue(int n) override;
    void RegisterImage(int type, const char *xpm_data) override;
    void RegisterRGBAImage(int type, int width, int height,
                           const unsigned char *pixelsImage) override;
    void ClearRegisteredImages() override;
    void SetDelegate(IListBoxDelegate *lbDelegate) override;
    void SetList(const char *list, char separator, char typesep) override;
    void SetOptions(ListOptions options_) override;
};

} // namespace Scintilla::Internal

#endif /* SCINTERM_PLAT_H */

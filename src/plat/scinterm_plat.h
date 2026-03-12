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

#include <notcurses/notcurses.h>
#include "Platform.h"
#include "Geometry.h"
#include <vector>
#include <map>
#include <memory>
#include <string>

namespace Scintilla::Internal {

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
 * @return true if successful, false otherwise.
 */
bool InitNotCurses();

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

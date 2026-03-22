/**
 * @file scinterm_parallel_render.h
 * @brief Parallel tile-based rendering for Scintilla
 * 
 * ARCHITECTURE: Divide screen into horizontal tiles, render in parallel
 * COMPOSITING: Single-threaded blit of tiles to output surface
 * 
 * Copyright 2012-2026 Mitchell. See LICENSE.
 */

#ifndef SCINTERM_PARALLEL_RENDER_H
#define SCINTERM_PARALLEL_RENDER_H

#include <memory>
#include <vector>
#include <atomic>
#include <future>

#include "scinterm_thread_pool.h"

namespace Scintilla::Internal {

/**
 * @brief Configuration for parallel rendering
 */
struct ParallelRenderConfig {
    int num_threads = 0;           // 0 = auto-detect
    int min_lines_per_tile = 5;    // Minimum tile height
    int max_tiles = 16;            // Maximum number of tiles
    bool enabled = true;           // Can disable at runtime
    
    static ParallelRenderConfig Default();
    static ParallelRenderConfig Auto();
};

/**
 * @brief Render tile - unit of parallel work
 * 
 * Each tile renders a horizontal slice of the document.
 * Tiles are independent except for shared read-only Document.
 */
/**
 * @brief Render tile - unit of parallel work (structural)
 * Full implementation requires Scintilla types
 */
struct RenderTile {
    int tile_id = 0;
    int start_line = 0;            // First line in document
    int end_line = 0;              // One past last line
    
    // Completion flag
    std::atomic<bool> completed{false};
    std::atomic<bool> has_error{false};
    
    // Default constructor
    RenderTile() = default;
    
    // Move constructor
    RenderTile(RenderTile&& other) noexcept
        : tile_id(other.tile_id),
          start_line(other.start_line),
          end_line(other.end_line),
          completed(other.completed.load()),
          has_error(other.has_error.load()) {}
    
    // Move assignment
    RenderTile& operator=(RenderTile&& other) noexcept {
        if (this != &other) {
            tile_id = other.tile_id;
            start_line = other.start_line;
            end_line = other.end_line;
            completed.store(other.completed.load());
            has_error.store(other.has_error.load());
        }
        return *this;
    }
    
    // Disable copy
    RenderTile(const RenderTile&) = delete;
    RenderTile& operator=(const RenderTile&) = delete;
};

/**
 * @brief Parallel renderer for Scintilla
 * 
 * PERFORMANCE: Renders document tiles in parallel using thread pool.
 * SAFETY: Document is read-only during render; surfaces are per-tile.
 */
class ParallelRenderer {
public:
    explicit ParallelRenderer(const ParallelRenderConfig& config = ParallelRenderConfig::Default());
    ~ParallelRenderer();
    
    // Non-copyable
    ParallelRenderer(const ParallelRenderer&) = delete;
    ParallelRenderer& operator=(const ParallelRenderer&) = delete;
    
    /**
     * @brief Render editor content in parallel
     * 
     * PLACEHOLDER: Full implementation requires Scintilla paint integration.
     * 
     * @param editor Editor to render (opaque pointer)
     * @param target Output surface
     * @param full_rect Full rendering rectangle
     */
    void render(void* editor, void* target, const void* full_rect);
    
    /**
     * @brief Check if parallel rendering should be used
     */
    bool should_use_parallel(int total_lines) const;
    
    /**
     * @brief Enable/disable parallel rendering at runtime
     */
    void set_enabled(bool enabled) { config_.enabled = enabled; }
    bool is_enabled() const { return config_.enabled; }
    
    struct Stats {
        double last_render_time_ms;
        int num_tiles;
        int cache_hits;
    };
    Stats get_stats() const { return stats_; }
    
private:
    // Placeholder methods - full implementation requires Scintilla integration
    std::vector<RenderTile> create_tiles(const void* full_rect, int total_lines);
    void render_tile(void* editor, RenderTile& tile);
    void composite_tiles(void* target, const std::vector<RenderTile>& tiles);
    
    ParallelRenderConfig config_;
    std::unique_ptr<ThreadPool> thread_pool_;
    Stats stats_;
};

/**
 * @brief Global parallel renderer instance
 */
ParallelRenderer& get_parallel_renderer();

} // namespace Scintilla::Internal

#endif // SCINTERM_PARALLEL_RENDER_H

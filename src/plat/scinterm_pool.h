/**
 * @file scinterm_pool.h
 * @brief Object pool for efficient Surface reuse
 * 
 * ARCHITECTURE: Pool pattern reduces allocation overhead by reusing
 * Surface objects across frames. Thread-safe for concurrent access.
 * 
 * Copyright 2012-2026 Mitchell. See LICENSE.
 */

#ifndef SCINTERM_POOL_H
#define SCINTERM_POOL_H

#include <memory>
#include <vector>
#include <mutex>
#include <stack>
#include <atomic>

// Forward declaration - avoids Platform.h include cycle
namespace Scintilla { namespace Internal { class Surface; }}

namespace Scintilla::Internal {

/**
 * @brief Object pool for Surface instances
 * 
 * PERFORMANCE: Eliminates repeated heap allocations for temporary surfaces.
 * Typical usage pattern allocates once and reuses for many frames.
 */
class SurfacePool {
public:
    explicit SurfacePool(size_t initial_capacity = 8);
    ~SurfacePool();

    // Non-copyable, non-movable
    SurfacePool(const SurfacePool&) = delete;
    SurfacePool& operator=(const SurfacePool&) = delete;

    /**
     * @brief Acquire a Surface from the pool
     * @return Surface pointer (must be released via return_to_pool)
     */
    std::unique_ptr<Surface> acquire();

    /**
     * @brief Return a Surface to the pool for reuse
     * @param surface Surface to return (will be reset but kept allocated)
     */
    void release(std::unique_ptr<Surface> surface);

    /**
     * @brief Pre-allocate surfaces up to capacity
     * @param count Number of surfaces to pre-allocate
     */
    void reserve(size_t count);

    /**
     * @brief Get current pool statistics
     */
    struct Stats {
        size_t available;      // Surfaces ready for reuse
        size_t in_use;         // Surfaces currently acquired
        size_t total_created;  // Total surfaces ever created
        size_t total_hits;     // Successful pool acquisitions
        size_t total_misses;   // Allocations due to empty pool
    };
    Stats get_stats() const;

    /**
     * @brief Clear all pooled surfaces (force deallocation)
     */
    void clear();

private:
    mutable std::mutex mutex_;
    std::vector<std::unique_ptr<Surface>> available_;
    std::atomic<size_t> in_use_{0};
    std::atomic<size_t> total_created_{0};
    std::atomic<size_t> total_hits_{0};
    std::atomic<size_t> total_misses_{0};
};

/**
 * @brief RAII wrapper for pooled surfaces
 * Automatically returns surface to pool on destruction
 */
class PooledSurface {
public:
    PooledSurface(SurfacePool& pool, std::unique_ptr<Surface> surface);
    ~PooledSurface();

    // Movable but not copyable
    PooledSurface(PooledSurface&& other) noexcept;
    PooledSurface& operator=(PooledSurface&& other) noexcept;
    PooledSurface(const PooledSurface&) = delete;
    PooledSurface& operator=(const PooledSurface&) = delete;

    Surface* get() const { return surface_.get(); }
    Surface* operator->() const { return surface_.get(); }
    Surface& operator*() const { return *surface_; }
    explicit operator bool() const { return surface_ != nullptr; }

private:
    SurfacePool& pool_;
    std::unique_ptr<Surface> surface_;
};

/**
 * @brief Global surface pool instance
 * Lazy-initialized on first use
 */
SurfacePool& get_surface_pool();

/**
 * @brief RAII guard for temporary surface acquisition
 * Usage: auto surface = acquire_pooled_surface();
 */
PooledSurface acquire_pooled_surface();

} // namespace Scintilla::Internal

#endif // SCINTERM_POOL_H

/**
 * @file scinterm_thread_pool.h
 * @brief High-performance thread pool for parallel rendering
 * 
 * ARCHITECTURE: Work-stealing queue for load balancing
 * PERFORMANCE: Lock-free operations where possible, minimal contention
 * 
 * Copyright 2012-2026 Mitchell. See LICENSE.
 */

#ifndef SCINTERM_THREAD_POOL_H
#define SCINTERM_THREAD_POOL_H

#include <thread>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <future>
#include <atomic>
#include <deque>
#include <optional>

namespace Scintilla::Internal {

/**
 * @brief Work-stealing task queue for each worker thread
 * 
 * Each worker has its own queue to minimize contention.
 * When empty, workers steal from other queues (randomly).
 */
class WorkStealingQueue {
public:
    WorkStealingQueue() = default;
    
    // Push task to this worker's queue (LIFO - for own tasks)
    void push(std::function<void()> task);
    
    // Pop from this worker's queue (LIFO)
    std::optional<std::function<void()>> pop();
    
    // Steal from this queue (FIFO - for other workers)
    std::optional<std::function<void()>> steal();
    
    size_t size() const;
    bool empty() const;
    
private:
    mutable std::mutex mutex_;
    std::deque<std::function<void()>> tasks_;
};

/**
 * @brief High-performance thread pool with work stealing
 * 
 * Use this pool for CPU-bound tasks like rendering tiles.
 * Automatic load balancing via work stealing.
 */
class ThreadPool {
public:
    explicit ThreadPool(size_t num_threads = 0);  // 0 = auto-detect
    ~ThreadPool();
    
    // Non-copyable, movable
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&) = default;
    ThreadPool& operator=(ThreadPool&&) = default;
    
    /**
     * @brief Submit a task and get a future for the result
     */
    template<typename F, typename... Args>
    auto submit(F&& f, Args&&... args) -> std::future<decltype(f(args...))> {
        using return_type = decltype(f(args...));
        
        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...));
        
        std::future<return_type> result = task->get_future();
        
        // Get current worker index (or random if not a worker thread)
        size_t worker_idx = get_current_worker_index();
        
        // Push to this thread's queue (LIFO for cache locality)
        local_queues_[worker_idx]->push([task]() { (*task)(); });
        
        condition_.notify_one();
        return result;
    }
    
    /**
     * @brief Submit a task without waiting for result (fire-and-forget)
     */
    void submit_detached(std::function<void()> task);
    
    /**
     * @brief Parallel for loop - distribute iterations across threads
     * 
     * @param start Start index (inclusive)
     * @param end End index (exclusive)
     * @param f Function to call for each index: f(i)
     */
    void parallel_for(size_t start, size_t end, std::function<void(size_t)> f);
    
    /**
     * @brief Wait for all submitted tasks to complete
     */
    void wait_all();
    
    /**
     * @brief Get number of worker threads
     */
    size_t size() const { return workers_.size(); }
    
    /**
     * @brief Get current active task count
     */
    size_t active_tasks() const { return active_tasks_.load(); }
    
    struct Stats {
        size_t tasks_submitted;
        size_t tasks_completed;
        size_t steals_performed;  // Work stealing events
    };
    Stats get_stats() const;
    
private:
    void worker_loop(size_t worker_id);
    std::optional<std::function<void()>> get_task(size_t worker_id);
    size_t get_current_worker_index() const;
    
    std::vector<std::thread> workers_;
    std::vector<std::unique_ptr<WorkStealingQueue>> local_queues_;
    
    // Fallback queue for non-worker threads
    std::mutex global_mutex_;
    std::queue<std::function<void()>> global_queue_;
    
    std::condition_variable condition_;
    std::atomic<bool> stop_{false};
    std::atomic<size_t> active_tasks_{0};
    std::atomic<size_t> steals_{0};
    
    thread_local static size_t tls_worker_id_;
};

/**
 * @brief Global thread pool instance for rendering
 */
ThreadPool& get_render_thread_pool();

} // namespace Scintilla::Internal

#endif // SCINTERM_THREAD_POOL_H

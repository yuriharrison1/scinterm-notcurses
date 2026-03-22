/**
 * @file scinterm_thread_pool.cpp
 * @brief Work-stealing thread pool implementation
 * Copyright 2012-2026 Mitchell. See LICENSE.
 */

#include <random>
#include <algorithm>
#include <cstddef>
#include "scinterm_thread_pool.h"

namespace Scintilla::Internal {

/*=============================================================================
 * WorkStealingQueue Implementation
 *===========================================================================*/

void WorkStealingQueue::push(std::function<void()> task) {
    std::lock_guard<std::mutex> lock(mutex_);
    tasks_.push_back(std::move(task));
}

std::optional<std::function<void()>> WorkStealingQueue::pop() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (tasks_.empty()) return std::nullopt;
    
    // LIFO - take from back (most recent, likely hot in cache)
    auto task = std::move(tasks_.back());
    tasks_.pop_back();
    return task;
}

std::optional<std::function<void()>> WorkStealingQueue::steal() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (tasks_.empty()) return std::nullopt;
    
    // FIFO - take from front (oldest, less likely contended)
    auto task = std::move(tasks_.front());
    tasks_.pop_front();
    return task;
}

size_t WorkStealingQueue::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return tasks_.size();
}

bool WorkStealingQueue::empty() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return tasks_.empty();
}

/*=============================================================================
 * ThreadPool Implementation
 *===========================================================================*/

thread_local size_t ThreadPool::tls_worker_id_ = static_cast<size_t>(-1);

ThreadPool::ThreadPool(size_t num_threads) {
    if (num_threads == 0) {
        num_threads = std::max(2u, std::thread::hardware_concurrency());
    }
    
    for (size_t i = 0; i < num_threads; ++i) {
        local_queues_.push_back(std::make_unique<WorkStealingQueue>());
    }
    
    for (size_t i = 0; i < num_threads; ++i) {
        workers_.emplace_back(&ThreadPool::worker_loop, this, i);
    }
}

ThreadPool::~ThreadPool() {
    stop_.store(true);
    condition_.notify_all();
    
    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

void ThreadPool::submit_detached(std::function<void()> task) {
    // Determine which queue to use
    size_t worker_idx = get_current_worker_index();
    
    if (worker_idx < workers_.size()) {
        // We're on a worker thread - push to local queue
        local_queues_[worker_idx]->push(std::move(task));
    } else {
        // External thread - push to global queue
        {
            std::lock_guard<std::mutex> lock(global_mutex_);
            global_queue_.push(std::move(task));
        }
    }
    
    condition_.notify_one();
}

void ThreadPool::parallel_for(size_t start, size_t end, std::function<void(size_t)> f) {
    const size_t total = end - start;
    if (total == 0) return;
    
    const size_t num_threads = workers_.size();
    
    // For small ranges, just do it inline
    if (total < num_threads * 4) {
        for (size_t i = start; i < end; ++i) {
            f(i);
        }
        return;
    }
    
    // Divide work among threads
    const size_t chunk_size = (total + num_threads - 1) / num_threads;
    std::vector<std::future<void>> futures;
    
    for (size_t t = 0; t < num_threads; ++t) {
        size_t chunk_start = start + t * chunk_size;
        size_t chunk_end = std::min(chunk_start + chunk_size, end);
        
        if (chunk_start >= chunk_end) break;
        
        futures.push_back(submit([f, chunk_start, chunk_end]() {
            for (size_t i = chunk_start; i < chunk_end; ++i) {
                f(i);
            }
        }));
    }
    
    // Wait for all to complete
    for (auto& fut : futures) {
        fut.wait();
    }
}

void ThreadPool::wait_all() {
    // Simple implementation: wait until all queues are empty and no active tasks
    while (active_tasks_.load() > 0) {
        bool all_empty = true;
        for (const auto& queue : local_queues_) {
            if (!queue->empty()) {
                all_empty = false;
                break;
            }
        }
        
        {
            std::lock_guard<std::mutex> lock(global_mutex_);
            if (!global_queue_.empty()) {
                all_empty = false;
            }
        }
        
        if (all_empty && active_tasks_.load() == 0) {
            break;
        }
        
        std::this_thread::yield();
    }
}

ThreadPool::Stats ThreadPool::get_stats() const {
    return Stats{0, 0, steals_.load()};  // TODO: Track more stats
}

void ThreadPool::worker_loop(size_t worker_id) {
    tls_worker_id_ = worker_id;
    
    // Random number generator for stealing
    std::random_device rd;
    std::mt19937 rng(rd());
    std::uniform_int_distribution<size_t> dist(0, workers_.size() - 2);
    
    while (!stop_.load()) {
        auto task = get_task(worker_id);
        
        if (task) {
            active_tasks_++;
            (*task)();
            active_tasks_--;
        } else {
            // No work available - wait
            std::unique_lock<std::mutex> lock(global_mutex_);
            condition_.wait_for(lock, std::chrono::milliseconds(10), 
                [this]() { return stop_.load() || !global_queue_.empty(); });
        }
    }
}

std::optional<std::function<void()>> ThreadPool::get_task(size_t worker_id) {
    // 1. Try own queue (LIFO - hot cache)
    auto task = local_queues_[worker_id]->pop();
    if (task) return task;
    
    // 2. Try global queue
    {
        std::lock_guard<std::mutex> lock(global_mutex_);
        if (!global_queue_.empty()) {
            task = std::move(global_queue_.front());
            global_queue_.pop();
            return task;
        }
    }
    
    // 3. Try stealing from other workers (FIFO - cold cache)
    static thread_local std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<size_t> dist(0, workers_.size() - 1);
    
    size_t victim = dist(rng);
    if (victim == worker_id) {
        victim = (victim + 1) % workers_.size();
    }
    
    task = local_queues_[victim]->steal();
    if (task) {
        steals_++;
        return task;
    }
    
    return std::nullopt;
}

size_t ThreadPool::get_current_worker_index() const {
    return tls_worker_id_;
}

/*=============================================================================
 * Global Instance
 *===========================================================================*/

ThreadPool& get_render_thread_pool() {
    static ThreadPool instance(0);  // Auto-detect threads
    return instance;
}

} // namespace Scintilla::Internal

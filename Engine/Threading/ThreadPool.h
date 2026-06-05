#pragma once
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <atomic>
#include <algorithm>

// ---------------------------------------------------------------------------
// ThreadPool (page 9) — a small fixed-size worker pool for the CPU rasterizer's
// tile/pixel parallelism. Submit() queues work; WaitAll() blocks until the
// queue drains. ParallelFor splits a [0,count) range into chunks across the
// workers (so 1M pixels don't become 1M tasks). Worker threads never touch GL.
// ---------------------------------------------------------------------------
class ThreadPool
{
public:
    explicit ThreadPool(int n = 0)
    {
        if (n <= 0) n = static_cast<int>(std::thread::hardware_concurrency());
        if (n <  1) n = 1;
        for (int i = 0; i < n; ++i) workers_.emplace_back([this] { workLoop(); });
    }

    ~ThreadPool()
    {
        { std::unique_lock<std::mutex> lk(m_); stop_ = true; }
        cv_.notify_all();
        for (std::thread& t : workers_) if (t.joinable()) t.join();
    }

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    int size() const { return static_cast<int>(workers_.size()); }

    void Submit(std::function<void()> f)
    {
        { std::unique_lock<std::mutex> lk(m_); tasks_.push(std::move(f)); ++pending_; }
        cv_.notify_one();
    }

    void WaitAll()
    {
        std::unique_lock<std::mutex> lk(doneM_);
        doneCv_.wait(lk, [this] { return pending_.load() == 0; });
    }

    // Split [0,count) into ~size()*4 contiguous chunks; fn(begin,end) per chunk.
    // Small ranges run inline on the caller (avoids enqueue overhead).
    void ParallelForChunks(int count, const std::function<void(int, int)>& fn)
    {
        if (count <= 0) return;
        const int chunks = std::max(1, size() * 4);
        const int per = (count + chunks - 1) / chunks;
        if (count <= per || size() <= 1) { fn(0, count); return; }   // tiny -> inline
        for (int c = 0; c < count; c += per)
        {
            const int begin = c, end = std::min(c + per, count);
            Submit([&fn, begin, end] { fn(begin, end); });
        }
        WaitAll();
    }

private:
    void workLoop()
    {
        for (;;)
        {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lk(m_);
                cv_.wait(lk, [this] { return stop_ || !tasks_.empty(); });
                if (stop_ && tasks_.empty()) return;
                task = std::move(tasks_.front());
                tasks_.pop();
            }
            task();
            if (--pending_ == 0)
            {
                std::unique_lock<std::mutex> lk(doneM_);
                doneCv_.notify_all();
            }
        }
    }

    std::vector<std::thread>          workers_;
    std::queue<std::function<void()>> tasks_;
    std::mutex                        m_;
    std::condition_variable           cv_;
    std::mutex                        doneM_;
    std::condition_variable           doneCv_;
    std::atomic<int>                  pending_{ 0 };
    bool                              stop_ = false;
};

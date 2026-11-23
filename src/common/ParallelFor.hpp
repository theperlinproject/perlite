#pragma once
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace perlite {
    // A small pool of threads for splitting one job of the tick (the rays of many explosions) over
    // the cores. ParallelFor returns when every index has been handled; the calling thread works too.
    class ParallelPool {
    public:
        static ParallelPool& Instance() {
            static ParallelPool pool;
            return pool;
        }

        void For(size_t count, const std::function<void(size_t)>& fn) {
            if (count == 0)
                return;
            if (count == 1 || workers.empty()) {
                for (size_t i = 0; i < count; i++)
                    fn(i);
                return;
            }
            std::unique_lock run(jobMutex); // one job at a time
            {
                std::lock_guard lock(mutex);
                job = &fn;
                total = count;
                next.store(0, std::memory_order_relaxed);
                finished = 0;
                generation++;
            }
            wake.notify_all();
            Work(fn, count);
            std::unique_lock lock(mutex);
            done.wait(lock, [&] { return finished == workers.size(); });
            job = nullptr;
        }

        ~ParallelPool() {
            {
                std::lock_guard lock(mutex);
                stopping = true;
                generation++;
            }
            wake.notify_all();
            for (std::thread& t : workers)
                t.join();
        }

    private:
        ParallelPool() {
            const unsigned hw = std::max(2u, std::thread::hardware_concurrency());
            for (unsigned i = 0; i + 1 < hw; i++)
                workers.emplace_back([this] { Loop(); });
        }

        void Work(const std::function<void(size_t)>& fn, size_t count) {
            for (;;) {
                const size_t i = next.fetch_add(1, std::memory_order_relaxed);
                if (i >= count)
                    return;
                fn(i);
            }
        }

        void Loop() {
            uint64_t seen = 0;
            for (;;) {
                const std::function<void(size_t)>* fn;
                size_t count;
                {
                    std::unique_lock lock(mutex);
                    wake.wait(lock, [&] { return generation != seen; });
                    seen = generation;
                    if (stopping)
                        return;
                    fn = job;
                    count = total;
                }
                if (fn)
                    Work(*fn, count);
                {
                    std::lock_guard lock(mutex);
                    finished++;
                }
                done.notify_all();
            }
        }

        std::vector<std::thread> workers;
        std::mutex jobMutex, mutex;
        std::condition_variable wake, done;
        const std::function<void(size_t)>* job = nullptr;
        size_t total = 0;
        std::atomic<size_t> next{0};
        size_t finished = 0;
        uint64_t generation = 0;
        bool stopping = false;
    };
}

#pragma once

#include "event_bus.hpp"
#include "handler_registry.hpp"
#include <vector>
#include <thread>
#include <atomic>

class ThreadPool {
public:
    // Constructor. Takes references to the EventBus and HandlerRegistry.
    // Does not spawn worker threads automatically to allow the user to register handlers first.
    ThreadPool(size_t num_threads, EventBus& event_bus, const HandlerRegistry& handler_registry);

    // Destructor. Safely stops the pool and joins all threads to prevent std::terminate.
    ~ThreadPool();

    // Spawns the worker threads and starts the dispatch loop.
    // Thread-safe: ensures threads are only started once.
    void start();

    // Requests all threads to exit, wakes up blocked workers, and joins them.
    // Thread-safe: can be called multiple times safely.
    void stop();

    // Returns the size of the thread pool.
    size_t get_num_threads() const { return num_threads_; }

private:
    // The loop executed by each worker thread.
    void worker_loop();

    size_t num_threads_;
    EventBus& event_bus_;
    const HandlerRegistry& handler_registry_;
    std::vector<std::thread> workers_;
    std::atomic<bool> running_{false};
};

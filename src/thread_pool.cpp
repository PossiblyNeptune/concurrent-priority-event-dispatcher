#include "thread_pool.hpp"
#include <iostream>

ThreadPool::ThreadPool(size_t num_threads, EventBus& event_bus, const HandlerRegistry& handler_registry)
    : num_threads_(num_threads), event_bus_(event_bus), handler_registry_(handler_registry) {}

ThreadPool::~ThreadPool() {
    stop();
}

void ThreadPool::start() {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) {
        return; // Already running
    }

    workers_.reserve(num_threads_);
    for (size_t i = 0; i < num_threads_; ++i) {
        workers_.emplace_back(&ThreadPool::worker_loop, this);
    }
}

void ThreadPool::stop() {
    bool expected = true;
    if (!running_.compare_exchange_strong(expected, false)) {
        return; // Already stopped or not running
    }

    // Shut down the event bus to unblock any waiting worker threads
    event_bus_.shutdown();

    // Join all workers to cleanly stop thread execution and prevent std::terminate()
    for (auto& thread : workers_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    workers_.clear();
}

void ThreadPool::worker_loop() {
    // Loop until stopped AND the event queues have been completely drained.
    while (running_ || !event_bus_.empty()) {
        try {
            // 1. Retrieve the next highest priority event (blocks if queue is empty)
            Event event = event_bus_.pop();

            // 2. Dispatch event to registered handlers.
            // Wrap in an inner try-catch so that a buggy callback exception does NOT crash the worker thread!
            try {
                handler_registry_.dispatch(event);
            } catch (const std::exception& ex) {
                std::cerr << "[ThreadPool Worker Exception] Unhandled callback exception: " 
                          << ex.what() << " for event type: " 
                          << eventTypeToString(event.type) << std::endl;
            } catch (...) {
                std::cerr << "[ThreadPool Worker Exception] Unknown callback exception for event type: " 
                          << eventTypeToString(event.type) << std::endl;
            }

        } catch (const std::exception&) {
            // This outer catch catches the shutdown exception thrown by event_bus_.pop()
            // once the bus is closed and all queues have run dry.
            break;
        }
    }
}

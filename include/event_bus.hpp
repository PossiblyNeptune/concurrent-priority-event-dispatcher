#pragma once

#include "event.hpp"
#include <queue>
#include <mutex>
#include <condition_variable>
#include <chrono>

#include <vector>

struct QueueSizes {
    size_t high;
    size_t med;
    size_t low;
};

struct EventTypeStats {
    uint64_t processed_count{0};
    uint64_t total_latency_ns{0};
    uint64_t missed_deadlines{0};
};

class EventBus {
public:
    // Constructor defining the max combined queue capacity.
    explicit EventBus(size_t max_capacity);

    // Thread-safe method to push an event into the bus.
    // If the bus is full:
    // - If there are LOW events, the oldest LOW event is dropped to free space.
    // - If there are no LOW events, the calling thread blocks until space is available.
    void push(Event event);

    // Thread-safe blocking method to pop the highest-priority available event.
    // Prioritizes HIGH > MEDIUM > LOW, and runs the aging logic before popping.
    // Throws a runtime error or returns dummy event if popped after shutdown.
    Event pop();

    // Thread-safe non-blocking method to pop an event.
    // Returns true and populates `event` if successful, false otherwise.
    // Does not block if empty.
    bool pop_non_blocking(Event& event);

    // Utility methods
    size_t size() const;
    bool empty() const;
    void shutdown();

    // Stats methods
    QueueSizes get_queue_sizes() const;
    std::vector<EventTypeStats> get_and_reset_type_stats();

private:
    // Internal helper to promote stale events to prevent starvation.
    // Must be called with `mutex_` already acquired.
    void apply_aging_under_lock(uint64_t now_ns);

    size_t max_capacity_;
    bool shutdown_{false};

    // Three separate queues to preserve FIFO order within each priority band.
    std::queue<Event> high_queue_;
    std::queue<Event> med_queue_;
    std::queue<Event> low_queue_;

    mutable std::mutex mutex_;
    std::condition_variable not_empty_cv_;
    std::condition_variable not_full_cv_;

    // Starvation threshold: 50 milliseconds in nanoseconds
    static constexpr uint64_t AGING_THRESHOLD_NS = 50'000'000;

    // Internal metrics storage (index maps to EventType cast to int)
    std::vector<EventTypeStats> type_stats_;
};

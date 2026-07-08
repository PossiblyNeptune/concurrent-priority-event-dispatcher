#include "event_bus.hpp"
#include <stdexcept>

EventBus::EventBus(size_t max_capacity) : max_capacity_(max_capacity) {
    type_stats_.resize(4);
}

void EventBus::push(Event event) {
    std::unique_lock<std::mutex> lock(mutex_);

    // Enforce backpressure and bounded capacity
    while (high_queue_.size() + med_queue_.size() + low_queue_.size() >= max_capacity_) {
        if (shutdown_) {
            return;
        }

        // Bounded queue policy: drop oldest LOW priority event to make space
        if (!low_queue_.empty()) {
            low_queue_.pop();
            break; // Dropping one event frees exactly one slot
        } else {
            // If there are no LOW events to drop, we must block the producer
            not_full_cv_.wait(lock, [this]() {
                return shutdown_ || (high_queue_.size() + med_queue_.size() + low_queue_.size() < max_capacity_);
            });
            if (shutdown_) {
                return;
            }
        }
    }

    // Push the event to the appropriate FIFO queue
    switch (event.priority) {
        case Priority::HIGH:
            high_queue_.push(event);
            break;
        case Priority::MEDIUM:
            med_queue_.push(event);
            break;
        case Priority::LOW:
            low_queue_.push(event);
            break;
    }

    // Wake up one blocked consumer thread (worker)
    not_empty_cv_.notify_one();
}

Event EventBus::pop() {
    std::unique_lock<std::mutex> lock(mutex_);

    // Wait until there is an event to pop, or the system is shutting down
    not_empty_cv_.wait(lock, [this]() {
        return shutdown_ || !high_queue_.empty() || !med_queue_.empty() || !low_queue_.empty();
    });

    // If shutdown occurred and no events remain, throw to signal workers to exit
    if (shutdown_ && high_queue_.empty() && med_queue_.empty() && low_queue_.empty()) {
        throw std::runtime_error("EventBus has been shut down");
    }

    // Apply aging logic dynamically based on current time
    auto now = std::chrono::steady_clock::now();
    uint64_t now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
    apply_aging_under_lock(now_ns);

    Event event;
    if (!high_queue_.empty()) {
        event = high_queue_.front();
        high_queue_.pop();
    } else if (!med_queue_.empty()) {
        event = med_queue_.front();
        med_queue_.pop();
    } else if (!low_queue_.empty()) {
        event = low_queue_.front();
        low_queue_.pop();
    } else {
        throw std::runtime_error("EventBus empty during pop");
    }

    // Record statistics under lock
    uint64_t latency = now_ns - event.timestamp_ns;
    int type_idx = static_cast<int>(event.type);
    if (type_idx >= 0 && type_idx < 4) {
        type_stats_[type_idx].processed_count++;
        type_stats_[type_idx].total_latency_ns += latency;
        if (now_ns > event.deadline_ns) {
            type_stats_[type_idx].missed_deadlines++;
        }
    }

    // Notify any waiting producers that space has opened up
    not_full_cv_.notify_one();
    return event;
}

bool EventBus::pop_non_blocking(Event& event) {
    std::unique_lock<std::mutex> lock(mutex_);

    if (high_queue_.empty() && med_queue_.empty() && low_queue_.empty()) {
        return false;
    }

    // Apply aging logic dynamically based on current time
    auto now = std::chrono::steady_clock::now();
    uint64_t now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
    apply_aging_under_lock(now_ns);

    if (!high_queue_.empty()) {
        event = high_queue_.front();
        high_queue_.pop();
    } else if (!med_queue_.empty()) {
        event = med_queue_.front();
        med_queue_.pop();
    } else if (!low_queue_.empty()) {
        event = low_queue_.front();
        low_queue_.pop();
    } else {
        return false;
    }

    // Record statistics under lock
    uint64_t latency = now_ns - event.timestamp_ns;
    int type_idx = static_cast<int>(event.type);
    if (type_idx >= 0 && type_idx < 4) {
        type_stats_[type_idx].processed_count++;
        type_stats_[type_idx].total_latency_ns += latency;
        if (now_ns > event.deadline_ns) {
            type_stats_[type_idx].missed_deadlines++;
        }
    }

    // Notify any waiting producers
    not_full_cv_.notify_one();
    return true;
}

size_t EventBus::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return high_queue_.size() + med_queue_.size() + low_queue_.size();
}

bool EventBus::empty() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return high_queue_.empty() && med_queue_.empty() && low_queue_.empty();
}

void EventBus::shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);
    shutdown_ = true;
    not_empty_cv_.notify_all();
    not_full_cv_.notify_all();
}

void EventBus::apply_aging_under_lock(uint64_t now_ns) {
    // 1. Promote eligible MEDIUM events to HIGH priority.
    // Check elements starting from the front of the queue.
    while (!med_queue_.empty()) {
        if (now_ns - med_queue_.front().last_promoted_ns > AGING_THRESHOLD_NS) {
            Event ev = med_queue_.front();
            med_queue_.pop();
            ev.priority = Priority::HIGH;
            ev.last_promoted_ns = now_ns; // Track when it was promoted
            high_queue_.push(ev);
        } else {
            break; // Stop because subsequent events were queued/promoted later
        }
    }

    // 2. Promote eligible LOW events to MEDIUM priority.
    while (!low_queue_.empty()) {
        if (now_ns - low_queue_.front().last_promoted_ns > AGING_THRESHOLD_NS) {
            Event ev = low_queue_.front();
            low_queue_.pop();
            ev.priority = Priority::MEDIUM;
            ev.last_promoted_ns = now_ns; // Track when it was promoted
            med_queue_.push(ev);
        } else {
            break;
        }
    }
}

QueueSizes EventBus::get_queue_sizes() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return QueueSizes{high_queue_.size(), med_queue_.size(), low_queue_.size()};
}

std::vector<EventTypeStats> EventBus::get_and_reset_type_stats() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<EventTypeStats> stats = type_stats_;
    type_stats_ = std::vector<EventTypeStats>(4);
    return stats;
}

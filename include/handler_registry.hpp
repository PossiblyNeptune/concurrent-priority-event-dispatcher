#pragma once

#include "event.hpp"
#include <vector>
#include <unordered_map>
#include <shared_mutex>
#include <functional>

// Signature for event subscriber callback functions.
// We pass Event by const-reference so handlers can read type, priority, timestamps, and payload
// without copying the payload container.
using EventHandler = std::function<void(const Event&)>;

class HandlerRegistry {
public:
    HandlerRegistry() = default;

    // Registers a callback handler for a specific EventType.
    // Thread-safe (Writer): Acquires an exclusive lock since it modifies the map.
    void register_handler(EventType type, EventHandler handler);

    // Dispatches the event to all registered callbacks for its EventType.
    // Thread-safe (Reader): Acquires a shared lock to read the callbacks, copies them,
    // and invokes them outside the lock to prevent potential deadlocks.
    void dispatch(const Event& event) const;

    // Thread-safe utility to clear all subscriptions.
    // Thread-safe (Writer): Acquires an exclusive lock.
    void clear();

private:
    std::unordered_map<EventType, std::vector<EventHandler>> registry_;
    mutable std::shared_mutex mutex_;
};

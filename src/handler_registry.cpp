#include "handler_registry.hpp"
#include <mutex>

void HandlerRegistry::register_handler(EventType type, EventHandler handler) {
    // Writers require exclusive access to prevent concurrent modification of the map.
    std::unique_lock<std::shared_mutex> lock(mutex_);
    registry_[type].push_back(handler);
}

void HandlerRegistry::dispatch(const Event& event) const {
    std::vector<EventHandler> handlers_to_call;

    // 1. Acquire a shared read lock to retrieve the list of subscribers.
    // Multiple threads can call dispatch concurrently as long as no writer is modifying the map.
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto it = registry_.find(event.type);
        if (it != registry_.end()) {
            handlers_to_call = it->second; // Deep copy of the function pointers/objects
        }
    } // Lock is released here as the unique_lock scope ends

    // 2. Invoke the handlers outside the lock.
    // This allows handlers to perform arbitrary operations, including registering new handlers
    // or pausing, without causing deadlocks or blocking other threads accessing the registry.
    for (const auto& handler : handlers_to_call) {
        handler(event);
    }
}

void HandlerRegistry::clear() {
    // Requires exclusive lock to clear the underlying map.
    std::unique_lock<std::shared_mutex> lock(mutex_);
    registry_.clear();
}

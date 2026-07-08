#include "event_bus.hpp"
#include "handler_registry.hpp"
#include "thread_pool.hpp"
#include <iostream>
#include <cassert>
#include <thread>
#include <atomic>
#include <chrono>

int main() {
    std::cout << "=== Running EventBus Starvation Prevention Test ===" << std::endl;

    // Small capacity bus (20) to establish a busy pipeline easily
    EventBus bus(20);
    HandlerRegistry registry;

    std::atomic<int> high_processed{0};
    std::atomic<int> low_processed{0};

    // Register callback handlers
    registry.register_handler(EventType::INPUT, [&](const Event&) {
        high_processed++;
    });

    registry.register_handler(EventType::AI, [&](const Event&) {
        low_processed++;
    });

    // Start ThreadPool with 4 workers to process concurrently
    ThreadPool pool(4, bus, registry);
    pool.start();

    std::atomic<bool> flood_active{true};

    // Background thread that floods the bus with HIGH priority events
    std::thread flood_producer([&]() {
        int id = 0;
        while (flood_active) {
            auto now = std::chrono::steady_clock::now();
            uint64_t timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
            uint64_t deadline = timestamp + 100'000'000; // 100ms deadline offset

            Event ev{EventType::INPUT, Priority::HIGH, timestamp, timestamp, deadline, ++id};
            bus.push(ev);

            // Throttle slightly to keep the HIGH queue occupied without locking the physical CPU cores completely
            std::this_thread::sleep_for(std::chrono::microseconds(200));
        }
    });

    // Let the high-priority flood establish itself
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Push a single LOW priority event
    auto now = std::chrono::steady_clock::now();
    uint64_t timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
    uint64_t deadline = timestamp + 1'000'000'000; // 1 second deadline offset

    std::cout << "Pushing LOW priority AI event under HIGH priority flood..." << std::endl;
    Event low_event{EventType::AI, Priority::LOW, timestamp, timestamp, deadline, 9999};
    bus.push(low_event);

    // Sleep for 150ms. Since aging threshold is 50ms:
    // - At T + 50ms: LOW (AI) is promoted to MEDIUM.
    // - At T + 100ms: MEDIUM (AI) is promoted to HIGH.
    // - Once HIGH, it gets popped FIFO in the high queue.
    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    // Stop the flood producer
    flood_active = false;
    if (flood_producer.joinable()) {
        flood_producer.join();
    }

    std::cout << "Flood stopped. Total HIGH events processed: " << high_processed.load() << std::endl;
    std::cout << "LOW events processed: " << low_processed.load() << std::endl;

    // Verify that the LOW priority event bypassed starvation and was processed!
    assert(low_processed.load() == 1);
    std::cout << "Starvation prevented successfully! LOW event bypassed the HIGH flood." << std::endl;

    // Stop thread pool cleanly
    pool.stop();

    std::cout << "=== Starvation Prevention Test Passed! ===" << std::endl;
    return 0;
}

#include "event_bus.hpp"
#include "handler_registry.hpp"
#include "thread_pool.hpp"
#include <iostream>
#include <cassert>
#include <thread>
#include <atomic>
#include <chrono>
#include <vector>

int main() {
    std::cout << "=== Running Latency Under Load & Deadline Miss Test ===" << std::endl;

    // Small capacity bus (30) to establish queue congestion easily
    EventBus bus(30);
    HandlerRegistry registry;

    std::atomic<int> input_processed{0};
    std::atomic<int> physics_processed{0};
    std::atomic<int> ai_processed{0};

    // Register handlers with a 2ms delay to simulate actual CPU workload computation
    registry.register_handler(EventType::INPUT, [&](const Event&) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        input_processed++;
    });

    registry.register_handler(EventType::PHYSICS, [&](const Event&) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        physics_processed++;
    });

    registry.register_handler(EventType::AI, [&](const Event&) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        ai_processed++;
    });

    // Start ThreadPool with 3 worker threads.
    // Since workers take 2ms per event, and we have multiple fast producers,
    // we are guaranteed to overload the consumers and build a backlog.
    ThreadPool pool(3, bus, registry);
    pool.start();

    std::atomic<bool> producing{true};
    std::vector<std::thread> producers;

    // Producer 1: Floods INPUT events with a tight 5ms deadline offset (no throttling sleep)
    producers.emplace_back([&]() {
        int id = 0;
        while (producing) {
            auto now = std::chrono::steady_clock::now();
            uint64_t timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
            uint64_t deadline = timestamp + 5'000'000; // 5ms deadline

            Event ev{EventType::INPUT, Priority::HIGH, timestamp, timestamp, deadline, ++id};
            bus.push(ev);
        }
    });

    // Producer 2: Floods PHYSICS events with a 10ms deadline offset (no throttling sleep)
    producers.emplace_back([&]() {
        int id = 0;
        while (producing) {
            auto now = std::chrono::steady_clock::now();
            uint64_t timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
            uint64_t deadline = timestamp + 10'000'000; // 10ms deadline

            Event ev{EventType::PHYSICS, Priority::MEDIUM, timestamp, timestamp, deadline, ++id};
            bus.push(ev);
        }
    });

    // Producer 3: Floods AI events with a 30ms deadline offset (no throttling sleep)
    producers.emplace_back([&]() {
        int id = 0;
        while (producing) {
            auto now = std::chrono::steady_clock::now();
            uint64_t timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
            uint64_t deadline = timestamp + 30'000'000; // 30ms deadline

            Event ev{EventType::AI, Priority::LOW, timestamp, timestamp, deadline, ++id};
            bus.push(ev);
        }
    });

    // Run stress overload simulation for 500 milliseconds
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // Stop producers first
    producing = false;
    for (auto& producer : producers) {
        if (producer.joinable()) {
            producer.join();
        }
    }

    std::cout << "Producers stopped. Stopping ThreadPool workers..." << std::endl;
    pool.stop();

    // Query statistics accumulated in the EventBus
    std::vector<EventTypeStats> stats = bus.get_and_reset_type_stats();
    if (stats.size() < 4) {
        stats.resize(4);
    }

    uint64_t total_processed = input_processed.load() + physics_processed.load() + ai_processed.load();
    uint64_t total_missed = stats[static_cast<int>(EventType::INPUT)].missed_deadlines +
                            stats[static_cast<int>(EventType::PHYSICS)].missed_deadlines +
                            stats[static_cast<int>(EventType::AI)].missed_deadlines;

    std::cout << "=== Stress Test Summary ===" << std::endl;
    std::cout << "INPUT Events Processed  : " << input_processed.load()
              << " | Missed Deadlines: " << stats[static_cast<int>(EventType::INPUT)].missed_deadlines << std::endl;
    std::cout << "PHYSICS Events Processed: " << physics_processed.load()
              << " | Missed Deadlines: " << stats[static_cast<int>(EventType::PHYSICS)].missed_deadlines << std::endl;
    std::cout << "AI Events Processed     : " << ai_processed.load()
              << " | Missed Deadlines: " << stats[static_cast<int>(EventType::AI)].missed_deadlines << std::endl;
    std::cout << "Total Processed Events  : " << total_processed << std::endl;
    std::cout << "Total Missed Deadlines  : " << total_missed << std::endl;

    // Verify that the system successfully tracked deadline violations
    // Overloaded threads with tight deadlines are guaranteed to miss deadlines.
    assert(total_processed > 0);
    assert(total_missed > 0);

    std::cout << "Deadline violations successfully tracked and logged!" << std::endl;
    std::cout << "=== Latency Under Load & Deadline Miss Test Passed! ===" << std::endl;
    return 0;
}

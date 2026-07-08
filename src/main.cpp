#include "event_bus.hpp"
#include "handler_registry.hpp"
#include "thread_pool.hpp"
#include "stats_sampler.hpp"
#include <iostream>
#include <thread>
#include <atomic>
#include <chrono>
#include <mutex>
#include <string>

static std::mutex g_cout_mutex;

// Thread-safe print utility to prevent console logging interleaving across worker threads
void log_safe(const std::string& message) {
    std::lock_guard<std::mutex> lock(g_cout_mutex);
    std::cout << message << std::endl;
}

class TickSource {
public:
    explicit TickSource(EventBus& bus) : bus_(bus) {}
    ~TickSource() { stop(); }

    // Spawns a background thread to generate events at precise timing intervals
    void start() {
        bool expected = false;
        if (!running_.compare_exchange_strong(expected, true)) {
            return;
        }
        producer_thread_ = std::thread(&TickSource::run, this);
    }

    // Stops event generation and joins the producer thread
    void stop() {
        bool expected = true;
        if (!running_.compare_exchange_strong(expected, false)) {
            return;
        }
        if (producer_thread_.joinable()) {
            producer_thread_.join();
        }
    }

private:
    void run() {
        using namespace std::chrono;
        auto start_time = steady_clock::now();

        auto next_input = start_time;
        auto next_physics = start_time;
        auto next_ai = start_time;

        // Frequencies in nanoseconds
        const nanoseconds input_interval(1'000'000'000 / 60);    // 60 Hz -> ~16.67ms
        const nanoseconds physics_interval(1'000'000'000 / 50);  // 50 Hz -> 20ms
        const nanoseconds ai_interval(1'000'000'000 / 10);       // 10 Hz -> 100ms

        int event_id = 0;

        while (running_) {
            auto now = steady_clock::now();
            auto next_wakeup = now + hours(1); // Default long wakeup time

            // Check if it is time to push an INPUT event (60/s)
            if (now >= next_input) {
                push_event(EventType::INPUT, Priority::HIGH, ++event_id);
                next_input += input_interval;
            }
            if (next_input < next_wakeup) {
                next_wakeup = next_input;
            }

            // Check if it is time to push a PHYSICS event (50/s)
            if (now >= next_physics) {
                push_event(EventType::PHYSICS, Priority::MEDIUM, ++event_id);
                next_physics += physics_interval;
            }
            if (next_physics < next_wakeup) {
                next_wakeup = next_physics;
            }

            // Check if it is time to push an AI event (10/s)
            if (now >= next_ai) {
                push_event(EventType::AI, Priority::LOW, ++event_id);
                next_ai += ai_interval;
            }
            if (next_ai < next_wakeup) {
                next_wakeup = next_ai;
            }

            // High-precision sleep until the next scheduled event deadline
            std::this_thread::sleep_until(next_wakeup);
        }
    }

    void push_event(EventType type, Priority priority, int id) {
        auto now = std::chrono::steady_clock::now();
        uint64_t timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
        
        // Define deadline: INPUT needs response in 30ms, PHYSICS in 40ms, AI in 200ms
        uint64_t deadline = timestamp;
        if (type == EventType::INPUT) {
            deadline += 30'000'000;
        } else if (type == EventType::PHYSICS) {
            deadline += 40'000'000;
        } else {
            deadline += 200'000'000;
        }

        Event ev{type, priority, timestamp, timestamp, deadline, id};
        bus_.push(ev);
    }

    EventBus& bus_;
    std::thread producer_thread_;
    std::atomic<bool> running_{false};
};

int main() {
    log_safe("=== Starting Concurrent Event Dispatcher Simulation ===");

    // Bounded capacity of 50 events
    EventBus bus(50);
    HandlerRegistry registry;

    std::atomic<int> input_processed{0};
    std::atomic<int> physics_processed{0};
    std::atomic<int> ai_processed{0};

    // Register callback handlers
    registry.register_handler(EventType::INPUT, [&](const Event& e) {
        int id = std::any_cast<int>(e.payload);
        log_safe("[INPUT Handler] Thread " + 
                 std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id()) % 100) +
                 " processed event ID: " + std::to_string(id));
        input_processed++;
    });

    registry.register_handler(EventType::PHYSICS, [&](const Event& e) {
        int id = std::any_cast<int>(e.payload);
        log_safe("[PHYSICS Handler] Thread " + 
                 std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id()) % 100) +
                 " processed event ID: " + std::to_string(id));
        physics_processed++;
    });

    registry.register_handler(EventType::AI, [&](const Event& e) {
        int id = std::any_cast<int>(e.payload);
        log_safe("[AI Handler] Thread " + 
                 std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id()) % 100) +
                 " processed event ID: " + std::to_string(id));
        ai_processed++;
    });

    // Detect CPU cores and initialize ThreadPool
    size_t num_threads = std::thread::hardware_concurrency();
    if (num_threads == 0) num_threads = 4;
    log_safe("System cores detected. Spawning ThreadPool with " + std::to_string(num_threads) + " worker threads.");

    ThreadPool pool(num_threads, bus, registry);
    pool.start();

    StatsSampler sampler(bus, "stats.csv", 100);
    log_safe("Starting statistics sampling (StatsSampler)...");
    sampler.start();

    TickSource tick_source(bus);
    log_safe("Starting event generation (TickSource)...");
    tick_source.start();

    // Run simulation for 2 seconds
    std::this_thread::sleep_for(std::chrono::seconds(2));

    log_safe("\nStopping simulation...");
    tick_source.stop(); // Stop producers first so no new events enter
    pool.stop();        // Unblock and join workers after queues empty
    sampler.stop();     // Stop stats sampling thread

    log_safe("=== Simulation Results ===");
    log_safe("INPUT events processed  : " + std::to_string(input_processed.load()));
    log_safe("PHYSICS events processed: " + std::to_string(physics_processed.load()));
    log_safe("AI events processed     : " + std::to_string(ai_processed.load()));
    log_safe("Total events processed   : " + std::to_string(input_processed.load() + physics_processed.load() + ai_processed.load()));
    log_safe("=========================");

    return 0;
}

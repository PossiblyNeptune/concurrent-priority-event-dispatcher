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
    explicit TickSource(EventBus& bus, double input_hz = 60.0, double physics_hz = 50.0, double ai_hz = 10.0) 
        : bus_(bus), input_hz_(input_hz), physics_hz_(physics_hz), ai_hz_(ai_hz) {}
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

        // Frequencies in nanoseconds (or large value if disabled / <= 0)
        const nanoseconds input_interval = (input_hz_ > 0) ? nanoseconds(static_cast<uint64_t>(1'000'000'000.0 / input_hz_)) : hours(24);
        const nanoseconds physics_interval = (physics_hz_ > 0) ? nanoseconds(static_cast<uint64_t>(1'000'000'000.0 / physics_hz_)) : hours(24);
        const nanoseconds ai_interval = (ai_hz_ > 0) ? nanoseconds(static_cast<uint64_t>(1'000'000'000.0 / ai_hz_)) : hours(24);

        int event_id = 0;

        while (running_) {
            auto now = steady_clock::now();
            auto next_wakeup = now + hours(1); // Default long wakeup time

            // Check if it is time to push an INPUT event (60/s)
            if (input_hz_ > 0) {
                if (now >= next_input) {
                    push_event(EventType::INPUT, Priority::HIGH, ++event_id);
                    next_input += input_interval;
                }
                if (next_input < next_wakeup) {
                    next_wakeup = next_input;
                }
            }

            // Check if it is time to push a PHYSICS event (50/s)
            if (physics_hz_ > 0) {
                if (now >= next_physics) {
                    push_event(EventType::PHYSICS, Priority::MEDIUM, ++event_id);
                    next_physics += physics_interval;
                }
                if (next_physics < next_wakeup) {
                    next_wakeup = next_physics;
                }
            }

            // Check if it is time to push an AI event (10/s)
            if (ai_hz_ > 0) {
                if (now >= next_ai) {
                    push_event(EventType::AI, Priority::LOW, ++event_id);
                    next_ai += ai_interval;
                }
                if (next_ai < next_wakeup) {
                    next_wakeup = next_ai;
                }
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
    double input_hz_;
    double physics_hz_;
    double ai_hz_;
    std::thread producer_thread_;
    std::atomic<bool> running_{false};
};

int main(int argc, char* argv[]) {
    log_safe("=== Starting Concurrent Event Dispatcher Simulation ===");

    // Default configuration parameters
    double duration_s = 2.0;
    size_t num_threads = std::thread::hardware_concurrency();
    if (num_threads == 0) num_threads = 4;
    double input_hz = 60.0;
    double physics_hz = 50.0;
    double ai_hz = 10.0;
    size_t bus_capacity = 50;

    try {
        if (argc > 1) duration_s = std::stod(argv[1]);
        if (argc > 2) num_threads = std::stoul(argv[2]);
        if (argc > 3) input_hz = std::stod(argv[3]);
        if (argc > 4) physics_hz = std::stod(argv[4]);
        if (argc > 5) ai_hz = std::stod(argv[5]);
        if (argc > 6) bus_capacity = std::stoul(argv[6]);
    } catch (const std::exception& ex) {
        std::cerr << "Error parsing command line parameters: " << ex.what() << "\n";
        std::cerr << "Usage: " << argv[0] << " [duration_seconds] [num_threads] [input_hz] [physics_hz] [ai_hz] [bus_capacity]\n";
        return 1;
    }

    log_safe("Simulation Configuration:");
    log_safe("  Duration        : " + std::to_string(duration_s) + " seconds");
    log_safe("  Worker Threads  : " + std::to_string(num_threads));
    log_safe("  INPUT Rate      : " + std::to_string(input_hz) + " Hz");
    log_safe("  PHYSICS Rate    : " + std::to_string(physics_hz) + " Hz");
    log_safe("  AI Rate         : " + std::to_string(ai_hz) + " Hz");
    log_safe("  Bus Capacity    : " + std::to_string(bus_capacity));

    // Initialize EventBus with configured capacity
    EventBus bus(bus_capacity);
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

    ThreadPool pool(num_threads, bus, registry);
    pool.start();

    StatsSampler sampler(bus, "stats.csv", 100);
    log_safe("Starting statistics sampling (StatsSampler)...");
    sampler.start();

    TickSource tick_source(bus, input_hz, physics_hz, ai_hz);
    log_safe("Starting event generation (TickSource)...");
    tick_source.start();

    // Sleep for the configured duration
    std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<uint64_t>(duration_s * 1000.0)));

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

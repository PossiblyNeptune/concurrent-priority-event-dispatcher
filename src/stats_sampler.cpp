#include "stats_sampler.hpp"
#include <fstream>
#include <iostream>
#include <chrono>

StatsSampler::StatsSampler(EventBus& bus, const std::string& csv_path, uint64_t interval_ms)
    : bus_(bus), csv_path_(csv_path), interval_ms_(interval_ms) {}

StatsSampler::~StatsSampler() {
    stop();
}

void StatsSampler::start() {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) {
        return; // Already running
    }

    // Write header lines to establish the CSV layout
    std::ofstream file(csv_path_, std::ios::out);
    if (file.is_open()) {
        file << "Time_MS,High_Size,Med_Size,Low_Size,"
             << "Input_Latency_MS,Physics_Latency_MS,AI_Latency_MS,Render_Latency_MS,"
             << "Input_Missed,Physics_Missed,AI_Missed,Render_Missed\n";
    } else {
        std::cerr << "[StatsSampler Error] Could not initialize CSV file: " << csv_path_ << std::endl;
    }

    sampler_thread_ = std::thread(&StatsSampler::run, this);
}

void StatsSampler::stop() {
    bool expected = true;
    if (!running_.compare_exchange_strong(expected, false)) {
        return; // Already stopped or not running
    }

    if (sampler_thread_.joinable()) {
        sampler_thread_.join();
    }
}

void StatsSampler::run() {
    using namespace std::chrono;
    auto start_time = steady_clock::now();

    while (running_) {
        std::this_thread::sleep_for(milliseconds(interval_ms_));

        auto now = steady_clock::now();
        uint64_t elapsed_ms = duration_cast<milliseconds>(now - start_time).count();

        // 1. Query queue sizes across all lanes
        QueueSizes q_sizes = bus_.get_queue_sizes();

        // 2. Query event-type metrics and reset them for the next interval
        std::vector<EventTypeStats> type_stats = bus_.get_and_reset_type_stats();
        if (type_stats.size() < 4) {
            type_stats.resize(4);
        }

        // 3. Convert accumulated wait latency from nanoseconds to milliseconds
        double avg_latencies_ms[4] = {0.0, 0.0, 0.0, 0.0};
        for (int i = 0; i < 4; ++i) {
            if (type_stats[i].processed_count > 0) {
                avg_latencies_ms[i] = static_cast<double>(type_stats[i].total_latency_ns) / 
                                      (type_stats[i].processed_count * 1'000'000.0);
            }
        }

        // 4. Open and write line to CSV file
        std::ofstream file(csv_path_, std::ios::app);
        if (file.is_open()) {
            file << elapsed_ms << ","
                 << q_sizes.high << "," << q_sizes.med << "," << q_sizes.low << ","
                 << avg_latencies_ms[static_cast<int>(EventType::INPUT)] << ","
                 << avg_latencies_ms[static_cast<int>(EventType::PHYSICS)] << ","
                 << avg_latencies_ms[static_cast<int>(EventType::AI)] << ","
                 << avg_latencies_ms[static_cast<int>(EventType::RENDER)] << ","
                 << type_stats[static_cast<int>(EventType::INPUT)].missed_deadlines << ","
                 << type_stats[static_cast<int>(EventType::PHYSICS)].missed_deadlines << ","
                 << type_stats[static_cast<int>(EventType::AI)].missed_deadlines << ","
                 << type_stats[static_cast<int>(EventType::RENDER)].missed_deadlines << "\n";
            file.flush(); // Ensure records are written to disk immediately to prevent loss on abrupt shutdown
        }
    }
}

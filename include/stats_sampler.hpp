#pragma once

#include "event_bus.hpp"
#include <string>
#include <thread>
#include <atomic>

class StatsSampler {
public:
    // Constructor. Requires EventBus handle, filepath to write CSV, and polling interval.
    StatsSampler(EventBus& bus, const std::string& csv_path, uint64_t interval_ms = 100);

    // Destructor. Stops and joins the sampling thread safely.
    ~StatsSampler();

    // Starts the statistics sampling thread.
    void start();

    // Stops the sampling thread.
    void stop();

private:
    // Loop executed by the sampler thread.
    void run();

    EventBus& bus_;
    std::string csv_path_;
    uint64_t interval_ms_;
    std::thread sampler_thread_;
    std::atomic<bool> running_{false};
};

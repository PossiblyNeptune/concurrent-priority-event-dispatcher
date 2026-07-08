# Concurrent Priority Event Dispatcher

A high-performance, thread-safe, concurrent priority event dispatcher implemented in C++17. Designed as a real-time task manager optimized for latency-critical systems, such as game engines or real-time simulation pipelines.

Features a multi-priority queue structure with bounded backpressure policies, lock-free callback dispatching, dynamic event aging (starvation prevention), thread pool execution safety, and background telemetry recording.

---

## Architecture Overview

The system uses a concurrent **Producer-Consumer** architecture:

1. **Production (`TickSource`)**: Single-threaded scheduler that generates events at specified rates (INPUT at 60Hz, PHYSICS at 50Hz, AI at 10Hz) using absolute waking timers to prevent accumulative timing drift.
2. **Buffering (`EventBus`)**: A thread-safe, bounded priority queue. It uses three separate FIFO queues to maintain temporal execution order within priority levels.
   - **Hybrid Backpressure**: Drops the oldest low-priority AI event when capacity is exceeded to keep paths open, or suspends producers if only critical events are queued.
   - **Dynamic Aging**: Runs an O(1) front-of-queue scan to promote stale events (waiting > 50ms) to prevent low-priority starvation.
3. **Distribution (`ThreadPool`)**: Worker pool sized to match CPU core availability (`std::thread::hardware_concurrency()`). Executes tasks inside exception-handling wrappers to protect worker threads.
4. **Routing (`HandlerRegistry`)**: Associates event types with subscriber callback handlers. Utilizes a shared reader-writer lock (`std::shared_mutex`) and copy-and-release execution to prevent recursive deadlocks.
5. **Telemetry (`StatsSampler`)**: Periodically copies and resets metrics under a brief lock, executing long calculations and CSV writes (`stats.csv`) on a background thread.

---

## Directory Structure

```text
├── include/
│   ├── event.hpp            # Event and EventType definitions (std::any payload)
│   ├── event_bus.hpp        # Bounded priority EventBus class
│   ├── handler_registry.hpp # Shared-lock subscription router
│   ├── thread_pool.hpp      # CPU hardware-concurrency worker pool
│   └── stats_sampler.hpp    # Telemetry logging thread
├── src/
│   ├── event_bus.cpp        # EventBus queue, backpressure, and aging logic
│   ├── handler_registry.cpp # Shared-lock and copy-and-release implementation
│   ├── thread_pool.cpp      # Thread loops, draining, and exception isolations
│   ├── stats_sampler.cpp    # Telemetry writing and interval reset patterns
│   └── main.cpp             # Main simulation and TickSource scheduler loop
├── tests/
│   ├── test_priority_ordering.cpp # FIFO and priority order validation
│   ├── test_no_starvation.cpp     # Starvation-prevention validation
│   └── test_latency_under_load.cpp# Concurrency stress and deadline miss validation
├── visualizer/              # Interactive dashboard files
│   ├── index.html           # Live visualization UI structure
│   ├── style.css            # Telemetry styling and layout
│   └── app.js               # Event-aging and queue-load animations
├── CMakeLists.txt           # Build file
├── build.bat                # Visual Studio MSVC compilation helper
├── .gitignore               # Ignored build artifacts
├── SUMMARY.md               # Project development status
├── USER_MANUAL.md           # Systems workflow user guide
├── INTERVIEW_PREP.md        # Technical systems engineering prep sheet
├── pipeline_and_logic.md    # Step-by-step pipeline workflow breakdown
└── theory_and_code.md       # OS theory and C++ implementation guide
```

---

## Getting Started

### Prerequisites
* A C++17 compliant compiler (MSVC, GCC, or Clang)
* CMake 3.12 or newer

### Building the Project
You can build the project using CMake or the MSVC helper script on Windows:

#### Option A: Using the Windows Build Script
Run the build script to set up MSVC and build the binaries under the `build/` directory:
```cmd
.\build.bat
```

#### Option B: Using CMake
```bash
mkdir build
cd build
cmake ..
cmake --build . --config Release
```

### Running the Executables
Once compiled, you can run the primary simulation or execute the test suite:

```bash
# Run the simulation
.\build\event_dispatcher

# Run test suites
.\build\test_priority_ordering
.\build\test_no_starvation
.\build\test_latency_under_load
```

---

## Telemetry & Visualization
The project outputs system metrics to a file named `stats.csv` during execution. 

You can view a live simulation of the queue lanes, dynamic aging promotions, worker threads, and live telemetry graphs by opening the [visualizer/index.html](file:///d:/projects/event_dispatcher/visualizer/index.html) file in a web browser.

# Concurrent Priority Event Dispatcher

A high-performance, thread-safe, concurrent priority event dispatcher implemented in C++17. This project acts as a real-time task manager optimized for latency-critical systems, such as game engines, multimedia streams, or financial simulation pipelines.
---

## 🎯 The Purpose & Problem Definition

In real-time multi-threaded architectures, thousands of tasks occur concurrently at different rates and execution urgencies. For instance, in a multiplayer game engine:
* **INPUT (HIGH priority)**: Keyboard and mouse movements must process immediately (< 16ms) to keep controls responsive.
* **PHYSICS (MEDIUM priority)**: Collision checks and spatial state updates run at 50Hz.
* **AI (LOW priority)**: Enemy pathfinding and state ticks can wait a few frames if execution paths are congested.

If these tasks are handled sequentially on a single thread, a long-running, low-priority AI operation can block the execution pipeline, leading to input lag. Conversely, if threads execute tasks at random, input inputs could be delayed behind irrelevant background ticks.

This project solves this bottleneck by providing a **Concurrent Priority Event Dispatcher** that:
1. Coordinates multi-threaded producers and consumer workers safely.
2. Prioritizes critical execution categories while maintaining strict FIFO order within each priority level.
3. Automatically promotes stale tasks using an **Aging Algorithm** to prevent low-priority task starvation.
4. Enforces capacity-bounding and backpressure policies to prevent out-of-memory errors during workload spikes.
5. Employs shared locking mechanics to guarantee thread safety during subscription changes.

---

## ⚙️ How It Works (Pipeline Workflow)

The dispatcher is built on a concurrent **Producer-Consumer** architecture:

```text
1. PRODUCTION (TickSource Scheduler)
   [Unified timing grid] ──> [Rigid absolute sleep] ──> [Determine event deadlines] ──> [Construct Event]
                                                                                              │
                                                                                              ▼
2. INGESTION (EventBus push)                                                                  │
   [Lock Mutex] <─────────────────────────────────────────────────────────────────────────────+
        │
        ├──> Capacity full?
        │         ├──> Yes (LOW events exist) ──> Pop & Drop oldest LOW task
        │         └──> Yes (No LOW events)    ──> Block producer thread on condition variable
        │
        └──> Enqueue Event onto priority queue (HIGH, MEDIUM, or LOW FIFO)
        └──> Signal waiting worker thread & Release lock
                                                                                              │
                                                                                              ▼
3. RETRIEVAL & DEQUEUING (EventBus pop)                                                       │
   [Worker wakes on signal] ──> [Lock Mutex]                                                   │
        │
        ├──> Dynamic Aging check: Promote MEDIUM/LOW events waiting > 50ms to next queue lane
        └──> Pop event from front of the highest occupied queue (FIFO preservation)
        └──> Calculate waiting latency & check soft real-time deadline status
        └──> Notify waiting producers & Release lock
                                                                                              │
                                                                                              ▼
4. ROUTING & CALLBACK DISPATCH (HandlerRegistry)                                              │
   [Acquire Registry shared read lock]                                                        │
        │
        └──> Deep copy callback handles matching EventType to local stack
        └──> Release shared read lock (Avoids lock holding during callback execution)
        └──> Execute callbacks outside registry locks inside exception-handling wrappers
```

---

## 🛠️ Deep Dive: Core Engineering Features

### 1. Bounded Capacity & Hybrid Backpressure
To prevent memory leaks under massive workload spikes, the [EventBus](file:///d:/projects/event_dispatcher/include/event_bus.hpp) enforces a capacity-bounding threshold (configured to 50 events in C++):
* **Lossy Dropping (Telemetry/Background)**: If the bus is full and a `LOW` priority event (AI pathfinding) is sitting in the queue, the dispatcher discards the oldest `LOW` task immediately to free a slot for incoming telemetry.
* **Lossless Blocking (Backpressure)**: If the queue is occupied entirely by critical `HIGH` and `MEDIUM` tasks, the producer thread suspends execution (`std::condition_variable::wait`) and yields CPU cycles to workers until events are popped.

### 2. O(1) Starvation Prevention (Dynamic Aging)
Traditional priority queues implemented as binary heaps are unstable (they do not guarantee First-In, First-Out execution for items of equal priority) and require $O(\log N)$ bubble-down cycles when modifying priorities.
Our [EventBus](file:///d:/projects/event_dispatcher/src/event_bus.cpp) uses three separate FIFO queues. Since items in a FIFO queue are sorted by age, the oldest event is always at the front. During pops, workers inspect the front event of the `LOW` and `MEDIUM` queues. If they have waited past 50ms, they are promoted ($O(1)$ lookup and enqueue operations).

### 3. Reader-Writer Lock Separation (`std::shared_mutex`)
Subsystems register callbacks mostly during application boot (rare writes), but worker threads query those callbacks constantly during runtime (constant concurrent reads).
The [HandlerRegistry](file:///d:/projects/event_dispatcher/include/handler_registry.hpp) uses a **Shared Mutex** (`std::shared_mutex`). Multiple worker threads can query and dispatch callbacks concurrently via shared read locks (`std::shared_lock`), while subscriptions are updated under exclusive locks (`std::unique_lock`).

### 4. Deadlock-Free Callback Dispatching
Executing external user-defined code while holding synchronization locks can cause deadlocks if a callback tries to register a new handler recursively. 
To prevent this, the registry employs a **copy-and-release** pattern: it locks the registry, copies the handler array to the worker thread's local stack, releases the lock immediately, and then executes the callbacks outside the lock.

---

## 📂 Codebase File Map

* **[include/event.hpp](file:///d:/projects/event_dispatcher/include/event.hpp)**: Structure of `Event` packets, priority enums, and the type-erased payload container (`std::any`).
* **[include/event_bus.hpp](file:///d:/projects/event_dispatcher/include/event_bus.hpp) / [src/event_bus.cpp](file:///d:/projects/event_dispatcher/src/event_bus.cpp)**: Thread-safe priority queues with dynamic aging and hybrid backpressure.
* **[include/handler_registry.hpp](file:///d:/projects/event_dispatcher/include/handler_registry.hpp) / [src/handler_registry.cpp](file:///d:/projects/event_dispatcher/src/handler_registry.cpp)**: Multi-threaded subscriber routing using reader-writer locks and copy-and-release dispatching.
* **[include/thread_pool.hpp](file:///d:/projects/event_dispatcher/include/thread_pool.hpp) / [src/thread_pool.cpp](file:///d:/projects/event_dispatcher/src/thread_pool.cpp)**: System core worker pool with thread exception isolation and safe queue draining.
* **[include/stats_sampler.hpp](file:///d:/projects/event_dispatcher/include/stats_sampler.hpp) / [src/stats_sampler.cpp](file:///d:/projects/event_dispatcher/src/stats_sampler.cpp)**: Non-invasive background metrics logging thread writing telemetry to `stats.csv`.
* **[src/main.cpp](file:///d:/projects/event_dispatcher/src/main.cpp)**: TickSource scheduler and simulation driver.
* **[tests/](file:///d:/projects/event_dispatcher/tests/)**: Automated test suite (priority ordering, starvation verification, latency profiling).
* **[visualizer/](file:///d:/projects/event_dispatcher/visualizer/)**: HTML dashboard showing queue load animations, worker thread activities, and real-time graphs.

---

## 🚀 Building & Running

### Build Setup
You can build the dispatcher statically along with all tests and simulation executables:

#### Build on Windows (Visual Studio / MSVC)
```cmd
.\build.bat
```

#### Build with CMake (Cross-platform)
```bash
mkdir build && cd build
cmake ..
cmake --build . --config Release
```

### Running Binaries

Run the primary simulation driver:
```bash
.\build\event_dispatcher
```

Run the automated test suites:
```bash
# Verify priority execution and bounded queue drops
.\build\test_priority_ordering

# Verify starvation prevention (LOW events promoted under heavy HIGH floods)
.\build\test_no_starvation

# Stress test queues under concurrency and verify deadline tracking
.\build\test_latency_under_load
```

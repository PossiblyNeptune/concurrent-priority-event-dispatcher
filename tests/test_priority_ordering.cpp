#include "event_bus.hpp"
#include "handler_registry.hpp"
#include "thread_pool.hpp"
#include <iostream>
#include <atomic>
#include <cassert>
#include <thread>
#include <chrono>

// Helper to create an Event
Event createEvent(EventType type, Priority priority, int id) {
    auto now = std::chrono::steady_clock::now();
    uint64_t timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
    // Use an arbitrary deadline in the future (e.g. now + 1 second)
    uint64_t deadline = timestamp + 1'000'000'000;
    return Event{type, priority, timestamp, timestamp, deadline, id};
}

void testPriorityAndFIFO() {
    std::cout << "Running testPriorityAndFIFO..." << std::endl;
    EventBus bus(10);

    // Push events in mixed order: LOW(1) -> MED(2) -> HIGH(3) -> HIGH(4) -> LOW(5) -> MED(6)
    bus.push(createEvent(EventType::AI, Priority::LOW, 1));
    bus.push(createEvent(EventType::PHYSICS, Priority::MEDIUM, 2));
    bus.push(createEvent(EventType::INPUT, Priority::HIGH, 3));
    bus.push(createEvent(EventType::INPUT, Priority::HIGH, 4));
    bus.push(createEvent(EventType::AI, Priority::LOW, 5));
    bus.push(createEvent(EventType::PHYSICS, Priority::MEDIUM, 6));

    assert(bus.size() == 6);

    // Expected Pop Order:
    // 1. HIGH(3)
    // 2. HIGH(4)  (Verifies FIFO for HIGH priority)
    // 3. MED(2)
    // 4. MED(6)   (Verifies FIFO for MEDIUM priority)
    // 5. LOW(1)
    // 6. LOW(5)   (Verifies FIFO for LOW priority)

    Event e = bus.pop();
    assert(e.priority == Priority::HIGH && std::any_cast<int>(e.payload) == 3);

    e = bus.pop();
    assert(e.priority == Priority::HIGH && std::any_cast<int>(e.payload) == 4);

    e = bus.pop();
    assert(e.priority == Priority::MEDIUM && std::any_cast<int>(e.payload) == 2);

    e = bus.pop();
    assert(e.priority == Priority::MEDIUM && std::any_cast<int>(e.payload) == 6);

    e = bus.pop();
    assert(e.priority == Priority::LOW && std::any_cast<int>(e.payload) == 1);

    e = bus.pop();
    assert(e.priority == Priority::LOW && std::any_cast<int>(e.payload) == 5);

    assert(bus.empty());
    std::cout << "testPriorityAndFIFO passed!\n" << std::endl;
}

void testBoundedCapacityAndDrop() {
    std::cout << "Running testBoundedCapacityAndDrop..." << std::endl;
    EventBus bus(3); // Small capacity to test dropping

    // Push 3 LOW events
    bus.push(createEvent(EventType::AI, Priority::LOW, 1));
    bus.push(createEvent(EventType::AI, Priority::LOW, 2));
    bus.push(createEvent(EventType::AI, Priority::LOW, 3));

    assert(bus.size() == 3);

    // Push 4th LOW event. Since queue is full, and we have LOW events,
    // it should drop the oldest LOW event: LOW(1).
    bus.push(createEvent(EventType::AI, Priority::LOW, 4));

    assert(bus.size() == 3);

    // We expect to pop: LOW(2) -> LOW(3) -> LOW(4). LOW(1) was dropped.
    Event e = bus.pop();
    assert(std::any_cast<int>(e.payload) == 2);

    e = bus.pop();
    assert(std::any_cast<int>(e.payload) == 3);

    e = bus.pop();
    assert(std::any_cast<int>(e.payload) == 4);

    assert(bus.empty());

    // Test dropping LOW when pushing a MEDIUM event
    EventBus bus2(2);
    bus2.push(createEvent(EventType::INPUT, Priority::HIGH, 1));
    bus2.push(createEvent(EventType::AI, Priority::LOW, 2)); // Capacity reached: 1 HIGH, 1 LOW
    assert(bus2.size() == 2);

    // Push MEDIUM(3). Since queue is full and LOW(2) exists, it should drop LOW(2) and accept MEDIUM(3)
    bus2.push(createEvent(EventType::PHYSICS, Priority::MEDIUM, 3));
    assert(bus2.size() == 2);

    // Pop order should be: HIGH(1) -> MEDIUM(3). LOW(2) was dropped.
    e = bus2.pop();
    assert(std::any_cast<int>(e.payload) == 1 && e.priority == Priority::HIGH);

    e = bus2.pop();
    assert(std::any_cast<int>(e.payload) == 3 && e.priority == Priority::MEDIUM);

    assert(bus2.empty());

    std::cout << "testBoundedCapacityAndDrop passed!\n" << std::endl;
}

void testAging() {
    std::cout << "Running testAging..." << std::endl;
    EventBus bus(10);

    // Push a LOW event (ID: 1) and MEDIUM event (ID: 2)
    bus.push(createEvent(EventType::AI, Priority::LOW, 1));
    bus.push(createEvent(EventType::PHYSICS, Priority::MEDIUM, 2));

    // Wait 60ms so both events age past the 50ms threshold
    std::this_thread::sleep_for(std::chrono::milliseconds(60));

    // Push a brand new HIGH event (ID: 3)
    bus.push(createEvent(EventType::INPUT, Priority::HIGH, 3));

    // At this point:
    // - MEDIUM(2) has aged > 50ms, so it should be promoted to HIGH.
    // - LOW(1) has aged > 50ms, so it should be promoted to MEDIUM.
    //
    // Note: Since MEDIUM(2) was pushed before HIGH(3), and it gets promoted to HIGH,
    // its original timestamp is older than HIGH(3)'s timestamp.
    // Wait! Let's see: during pop(), apply_aging_under_lock() promotes:
    // - MEDIUM(2) -> HIGH. It is pushed to the back of high_queue_.
    // - LOW(1) -> MEDIUM. It is pushed to the back of med_queue_.
    //
    // High queue now has:
    //   - HIGH(3) (first, pushed at creation)
    //   - MEDIUM(2) (now HIGH, pushed during promotion)
    // Wait, let's verify if that's the order!
    // Since HIGH(3) was pushed normally to high_queue_, it is at the front.
    // MEDIUM(2) gets promoted to HIGH, so it is pushed to the back of high_queue_.
    // So popping should yield:
    // 1. HIGH(3) (priority HIGH, payload 3)
    // 2. MEDIUM(2) (promoted to HIGH, priority HIGH, payload 2)
    // 3. LOW(1) (promoted to MEDIUM, priority MEDIUM, payload 1)
    
    Event e = bus.pop();
    assert(std::any_cast<int>(e.payload) == 3 && e.priority == Priority::HIGH);

    e = bus.pop();
    assert(std::any_cast<int>(e.payload) == 2 && e.priority == Priority::HIGH); // Promoted to HIGH!

    e = bus.pop();
    assert(std::any_cast<int>(e.payload) == 1 && e.priority == Priority::MEDIUM); // Promoted to MEDIUM!

    assert(bus.empty());
    std::cout << "testAging passed!\n" << std::endl;
}

void testHandlerRegistry() {
    std::cout << "Running testHandlerRegistry..." << std::endl;
    HandlerRegistry registry;

    int inputCount = 0;
    int physicsCount = 0;

    registry.register_handler(EventType::INPUT, [&](const Event& e) {
        assert(e.type == EventType::INPUT);
        inputCount += std::any_cast<int>(e.payload);
    });

    registry.register_handler(EventType::PHYSICS, [&](const Event& e) {
        assert(e.type == EventType::PHYSICS);
        physicsCount += std::any_cast<int>(e.payload);
    });

    // Dispatch INPUT event with payload 10
    registry.dispatch(createEvent(EventType::INPUT, Priority::HIGH, 10));
    assert(inputCount == 10);
    assert(physicsCount == 0);

    // Dispatch PHYSICS event with payload 5
    registry.dispatch(createEvent(EventType::PHYSICS, Priority::MEDIUM, 5));
    assert(inputCount == 10);
    assert(physicsCount == 5);

    // Clear registry
    registry.clear();
    registry.dispatch(createEvent(EventType::INPUT, Priority::HIGH, 100));
    assert(inputCount == 10); // remains unchanged

    std::cout << "testHandlerRegistry passed!\n" << std::endl;
}

void testThreadPool() {
    std::cout << "Running testThreadPool..." << std::endl;
    EventBus bus(50);
    HandlerRegistry registry;

    std::atomic<int> inputCount{0};
    std::atomic<int> physicsCount{0};

    // Register handlers
    registry.register_handler(EventType::INPUT, [&](const Event& e) {
        inputCount += std::any_cast<int>(e.payload);
    });
    registry.register_handler(EventType::PHYSICS, [&](const Event& e) {
        physicsCount += std::any_cast<int>(e.payload);
    });

    size_t numThreads = std::thread::hardware_concurrency();
    if (numThreads == 0) numThreads = 2; // Fallback
    std::cout << "Spawning " << numThreads << " worker threads..." << std::endl;

    ThreadPool pool(numThreads, bus, registry);
    pool.start();

    // Push 20 INPUT and 20 PHYSICS events
    for (int i = 0; i < 20; ++i) {
        bus.push(createEvent(EventType::INPUT, Priority::HIGH, 1));
        bus.push(createEvent(EventType::PHYSICS, Priority::MEDIUM, 2));
    }

    // Wait for the threads to consume and dispatch everything
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    pool.stop();

    // Verify all events were processed and queues are empty
    assert(bus.empty());
    assert(inputCount == 20);
    assert(physicsCount == 40);

    std::cout << "testThreadPool passed!\n" << std::endl;
}

int main() {
    std::cout << "=== Running EventBus Single-Threaded Tests ===" << std::endl;
    testPriorityAndFIFO();
    testBoundedCapacityAndDrop();
    testAging();
    testHandlerRegistry();
    testThreadPool();
    std::cout << "=== All EventBus Tests Passed! ===" << std::endl;
    return 0;
}

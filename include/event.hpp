#pragma once

#include <any>
#include <cstdint>
#include <string>

// Enum representing different categories of events in our game engine simulation.
enum class EventType {
    INPUT,      // HIGH priority by default (e.g. keyboard click, mouse move)
    PHYSICS,    // MEDIUM/HIGH priority (e.g. collision detection, movement updates)
    AI,         // LOW/MEDIUM priority (e.g. pathfinding ticks, state machine updates)
    RENDER      // HIGH/MEDIUM priority (e.g. frame trigger, buffer swap)
};

// Enum representing the dispatch priority levels.
enum class Priority {
    HIGH,
    MEDIUM,
    LOW
};

// Converts EventType to a string representation for debugging/logging.
inline std::string eventTypeToString(EventType type) {
    switch (type) {
        case EventType::INPUT:   return "INPUT";
        case EventType::PHYSICS: return "PHYSICS";
        case EventType::AI:      return "AI";
        case EventType::RENDER:  return "RENDER";
    }
    return "UNKNOWN";
}

// Converts Priority to a string representation for debugging/logging.
inline std::string priorityToString(Priority priority) {
    switch (priority) {
        case Priority::HIGH:   return "HIGH";
        case Priority::MEDIUM: return "MEDIUM";
        case Priority::LOW:    return "LOW";
    }
    return "UNKNOWN";
}

// Core Event structure passed through the dispatch pipeline.
struct Event {
    EventType type;             // Category of the event
    Priority  priority;         // Current priority (may change due to aging)
    uint64_t  timestamp_ns;     // Nanoseconds timestamp when the event was generated
    uint64_t  last_promoted_ns; // Nanoseconds timestamp when the event was last promoted (for aging)
    uint64_t  deadline_ns;      // Absolute nanoseconds timestamp by which the event should be processed
    std::any  payload;          // Type-safe container for arbitrary event data
};


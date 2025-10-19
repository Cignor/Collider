#pragma once
#include <juce_core/juce_core.h>

// Very lightweight RT logger: audio-thread posts fixed-size C strings into a lock-free ring;
// message thread flushes to juce::Logger periodically.
namespace RtLogger {

void init (int capacity = 1024, int lineBytes = 256);
void shutdown();

// RT-safe: formats into a local buffer then copies into preallocated slot.
void postf (const char* fmt, ...) noexcept;

// Must be called from message thread to flush queued lines to juce::Logger
void flushToFileLogger();

}



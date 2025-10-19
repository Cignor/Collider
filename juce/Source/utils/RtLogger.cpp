#include "RtLogger.h"
#include <atomic>
#include <cstdarg>

namespace {
struct Ring {
    juce::HeapBlock<char> storage;
    int lineBytes { 256 };
    int capacity { 0 };
    std::atomic<int> writeIdx { 0 };
    std::atomic<int> readIdx { 0 };
} ring;
}

namespace RtLogger {

void init (int capacity, int lineBytes)
{
    ring.lineBytes = juce::jmax (64, lineBytes);
    ring.capacity  = juce::jmax (128, capacity);
    ring.storage.allocate ((size_t) (ring.capacity * ring.lineBytes), true);
    ring.writeIdx = 0; ring.readIdx = 0;
}

void shutdown()
{
    ring.storage.free();
    ring.capacity = 0; ring.lineBytes = 0; ring.writeIdx = 0; ring.readIdx = 0;
}

void postf (const char* fmt, ...) noexcept
{
    if (ring.capacity <= 0) return;
    const int wi = ring.writeIdx.load (std::memory_order_relaxed);
    const int ri = ring.readIdx.load (std::memory_order_relaxed);
    if (((wi + 1) % ring.capacity) == ri) return; // full, drop
    char stackBuf[512];
    va_list args; va_start (args, fmt);
   #if defined(_MSC_VER)
    _vsnprintf_s (stackBuf, (size_t) sizeof(stackBuf), _TRUNCATE, fmt, args);
   #else
    vsnprintf (stackBuf, sizeof(stackBuf), fmt, args);
   #endif
    va_end (args);
    const int slot = wi % ring.capacity;
    char* dest = ring.storage.getData() + slot * ring.lineBytes;
   #if defined(_MSC_VER)
    strncpy_s (dest, (size_t) ring.lineBytes, stackBuf, _TRUNCATE);
   #else
    std::strncpy (dest, stackBuf, (size_t) ring.lineBytes - 1);
    dest[ring.lineBytes - 1] = '\0';
   #endif
    ring.writeIdx.store ((wi + 1) % ring.capacity, std::memory_order_release);
}

void flushToFileLogger()
{
    if (ring.capacity <= 0) return;
    while (ring.readIdx.load (std::memory_order_acquire) != ring.writeIdx.load (std::memory_order_acquire))
    {
        const int ri = ring.readIdx.load (std::memory_order_relaxed);
        const int slot = ri % ring.capacity;
        const char* src = ring.storage.getData() + slot * ring.lineBytes;
        juce::Logger::writeToLog (src);
        ring.readIdx.store ((ri + 1) % ring.capacity, std::memory_order_release);
    }
}

}



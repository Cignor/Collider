#pragma once

#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>
#include <deque>

class NotificationManager
{
public:
    enum class Type { Status, Success, Error, Warning, Info };

    // Call this from any thread to post a new notification
    static void post(Type type, const juce::String& message, float duration = 5.0f);

    // Call this once per frame inside your main ImGui render loop
    static void render();

private:
    struct Notification
    {
        uint32_t id;
        Type type;
        juce::String message;
        double startTime;
        float lifetime;
        float alpha = 0.0f; // For fade animation
    };

    // Singleton access
    static NotificationManager& getInstance();

    NotificationManager() = default;
    ~NotificationManager() = default;
    NotificationManager(const NotificationManager&) = delete;
    NotificationManager& operator=(const NotificationManager&) = delete;

    void postImpl(Type type, const juce::String& message, float duration);
    void renderImpl();

    std::deque<Notification> m_notifications;
    juce::CriticalSection m_lock;
    uint32_t m_nextId = 0;
};


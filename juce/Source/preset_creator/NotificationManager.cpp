#include "NotificationManager.h"

#include <imgui.h>
#include <juce_events/juce_events.h>
#include <algorithm>

void NotificationManager::post(Type type, const juce::String& message, float duration)
{
    // Use MessageManager to ensure the post happens on the UI thread,
    // which simplifies thread safety as only the render loop modifies the queue.
    juce::MessageManager::callAsync([=]() {
        getInstance().postImpl(type, message, duration);
    });
}

void NotificationManager::render()
{
    getInstance().renderImpl();
}

NotificationManager& NotificationManager::getInstance()
{
    static NotificationManager instance;
    return instance;
}

void NotificationManager::postImpl(Type type, const juce::String& message, float duration)
{
    const juce::ScopedLock lock(m_lock);
    m_notifications.push_back({
        ++m_nextId,
        type,
        message,
        ImGui::GetTime(),
        duration
    });
}

void NotificationManager::renderImpl()
{
    const juce::ScopedLock lock(m_lock);
    if (m_notifications.empty()) return;

    const float now = (float)ImGui::GetTime();
    const float padding = 10.0f;
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImVec2 work_pos = viewport->WorkPos;
    float currentY = work_pos.y + padding;

    const float fadeInTime = 0.3f;
    const float fadeOutTime = 0.5f;

    for (auto& notif : m_notifications)
    {
        float age = now - (float)notif.startTime;
        bool isDismissed = false;

        // Fade In
        if (age < fadeInTime) {
            notif.alpha = juce::jmap(age, 0.0f, fadeInTime, 0.0f, 1.0f);
        }
        // Fade Out (triggered by time or click)
        else if (age > notif.lifetime) {
            float fadeAge = age - notif.lifetime;
            notif.alpha = juce::jmap(fadeAge, 0.0f, fadeOutTime, 1.0f, 0.0f);
            if (fadeAge > fadeOutTime) isDismissed = true;
        }
        // Fully visible
        else {
            notif.alpha = 1.0f;
        }

        float windowWidth = 350.0f;
        float windowHeight = 60.0f;

        float slideInOffset = juce::jmap(std::min(age, fadeInTime), 0.0f, fadeInTime, windowWidth, 0.0f);
        ImVec2 windowPos = ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - windowWidth - padding + slideInOffset, currentY);

        ImGui::SetNextWindowPos(windowPos);
        ImGui::SetNextWindowSize(ImVec2(windowWidth, windowHeight));
        ImGui::SetNextWindowBgAlpha(0.8f * notif.alpha);

        char windowName[32];
        snprintf(windowName, 32, "Notification##%u", notif.id);

        ImGui::Begin(windowName, nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
        
        // Check if window was clicked (using hover + mouse button check)
        if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            notif.startTime = now - notif.lifetime; // Trigger fade out immediately
        }

        ImVec4 iconColor;
        const char* iconText = nullptr;

        switch (notif.type) {
            case Type::Success: 
                iconColor = ImVec4(0.2f, 1.0f, 0.2f, 1.0f); 
                iconText = "\xE2\x9C\x85"; // ✅
                break;
            case Type::Error:   
                iconColor = ImVec4(1.0f, 0.2f, 0.2f, 1.0f); 
                iconText = "\xE2\x9D\x8C"; // ❌
                break;
            case Type::Warning: 
                iconColor = ImVec4(1.0f, 0.8f, 0.2f, 1.0f); 
                iconText = "\xE2\x9A\xA0\xEF\xB8\x8F"; // ⚠️
                break;
            case Type::Info:    
                iconColor = ImVec4(0.2f, 0.6f, 1.0f, 1.0f); 
                iconText = "\xE2\x84\xB9\xEF\xB8\x8F"; // ℹ️
                break;
            case Type::Status:  
                iconColor = ImVec4(0.8f, 0.8f, 0.8f, 1.0f); 
                iconText = nullptr; // Spinner
                break;
        }
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertFloat4ToU32(ImVec4(iconColor.x, iconColor.y, iconColor.z, notif.alpha)));
        
        if (notif.type == Type::Status) {
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            ImVec2 spinnerCenter = ImVec2(ImGui::GetCursorScreenPos().x + 12, ImGui::GetCursorScreenPos().y + 12);
            drawList->PathArcTo(spinnerCenter, 8.0f, now * 4.0f, now * 4.0f + 4.0f, 32);
            drawList->PathStroke(ImGui::GetColorU32(ImGuiCol_Text), 0, 2.0f);
        } else if (iconText != nullptr) {
            ImGui::TextUnformatted(iconText);
        }
        ImGui::PopStyleColor();

        ImGui::SameLine(35.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 1.0f, 1.0f, notif.alpha)));
        ImGui::TextWrapped("%s", notif.message.toRawUTF8());
        ImGui::PopStyleColor();

        // Progress bar for auto-dismissal
        if (notif.type != Type::Error && notif.type != Type::Status) {
            float progress = juce::jlimit(0.0f, 1.0f, age / notif.lifetime);
            ImVec2 p_min = ImVec2(ImGui::GetWindowPos().x, ImGui::GetWindowPos().y + windowHeight - 3.0f);
            ImVec2 p_max = ImVec2(p_min.x + windowWidth * (1.0f - progress), p_min.y + 3.0f);
            ImGui::GetWindowDrawList()->AddRectFilled(p_min, p_max, ImGui::GetColorU32(iconColor));
        }

        ImGui::End();

        currentY += windowHeight + padding;
    }

    // Remove dismissed notifications
    m_notifications.erase(std::remove_if(m_notifications.begin(), m_notifications.end(), 
        [&](const Notification& n) {
            return (now - n.startTime) > (n.lifetime + fadeOutTime);
    }), m_notifications.end());
}


#include "NotificationManager.h"

#include <juce_core/juce_core.h>
#include <imgui.h>
#include <algorithm>

void NotificationManager::post(Type type, const juce::String& message, float duration)
{
    // Directly call postImpl since we're already on the UI thread
    getInstance().postImpl(type, message, duration);
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
    
    // When posting Success or Error, automatically dismiss any existing Status notifications
    // This allows the "Saving..." status to be replaced by "Saved!" or "Failed to save!"
    if (type == Type::Success || type == Type::Error)
    {
        m_notifications.erase(
            std::remove_if(m_notifications.begin(), m_notifications.end(),
                [](const Notification& n) { return n.type == Type::Status; }),
            m_notifications.end()
        );
    }
    
    // For Status messages, make them persist until replaced or dismissed
    if (type == Type::Status)
        duration = 3600.0f; // A very long time, effectively persistent until replaced
    // For Error messages, make them persist until clicked
    if (type == Type::Error)
        duration = 3600.0f; 
    
    m_notifications.push_back({
        ++m_nextId,
        type,
        message,
        ImGui::GetTime(),
        duration
    });

    // Debug log to verify notifications are being posted
    juce::String typeStr;
    switch (type) {
        case Type::Success: typeStr = "Success"; break;
        case Type::Error:   typeStr = "Error"; break;
        case Type::Warning: typeStr = "Warning"; break;
        case Type::Info:    typeStr = "Info"; break;
        case Type::Status:  typeStr = "Status"; break;
    }
    juce::Logger::writeToLog("[Toast] " + typeStr + ": " + message);
}

void NotificationManager::renderImpl()
{
    const juce::ScopedLock lock(m_lock);
    if (m_notifications.empty()) return;

    const float now = (float)ImGui::GetTime();
    const float padding = 10.0f;
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImVec2 work_pos = viewport->WorkPos;
    // Adjust Y position to be below the main menu bar
    float currentY = work_pos.y + padding + ImGui::GetFrameHeight();

    // Hard-pinned toast style: no animations, consistent visibility
    const float fadeInTime = 0.0f;
    const float fadeOutTime = 0.0f;

    // Use an iterator to safely remove items while looping
    for (auto it = m_notifications.begin(); it != m_notifications.end(); )
    {
        auto& notif = *it;
        float age = now - (float)notif.startTime;
        // No fade animation; always fully visible until lifetime expires
        notif.alpha = 1.0f;

        float windowWidth = 350.0f;
        float windowHeight = 60.0f;

        // Fixed top-right position (no animation)
        ImVec2 windowPos = ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - windowWidth - padding, currentY);

        // Clamp inside visible work area to avoid off-screen placement
        const float minX = viewport->WorkPos.x + padding;
        const float maxX = viewport->WorkPos.x + viewport->WorkSize.x - windowWidth - padding;
        const float minY = viewport->WorkPos.y + padding + ImGui::GetFrameHeight();
        const float maxY = viewport->WorkPos.y + viewport->WorkSize.y - windowHeight - padding;
        if (maxX > minX) windowPos.x = juce::jlimit(minX, maxX, windowPos.x); else windowPos.x = minX;
        if (maxY > minY) windowPos.y = juce::jlimit(minY, maxY, windowPos.y); else windowPos.y = minY;

        // Ensure window is in the visible work area
        ImGui::SetNextWindowPos(windowPos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(windowWidth, windowHeight));
        ImGui::SetNextWindowBgAlpha(0.92f);

        char windowName[32];
        snprintf(windowName, 32, "Notification##%u", notif.id);
        
        // (No extra overlay; single source of truth to avoid duplicate text)

        ImGui::Begin(windowName, nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_AlwaysAutoResize);
        
        if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            if (age <= notif.lifetime) {
                 notif.startTime = now - notif.lifetime; // Trigger fade-out
            }
        }

        ImVec4 iconColor;
        const char* iconText = nullptr;

        switch (notif.type) {
            case Type::Success: iconColor = ImVec4(0.2f, 1.0f, 0.2f, 1.0f); iconText = "\xE2\x9C\x85"; break;
            case Type::Error:   iconColor = ImVec4(1.0f, 0.2f, 0.2f, 1.0f); iconText = "\xE2\x9D\x8C"; break;
            case Type::Warning: iconColor = ImVec4(1.0f, 0.8f, 0.2f, 1.0f); iconText = "\xE2\x9A\xA0\xEF\xB8\x8F"; break;
            case Type::Info:    iconColor = ImVec4(0.2f, 0.6f, 1.0f, 1.0f); iconText = "\xE2\x84\xB9\xEF\xB8\x8F"; break;
            case Type::Status:  iconColor = ImVec4(0.8f, 0.8f, 0.8f, 1.0f); iconText = nullptr; break;
        }

        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, notif.alpha);

        ImGui::PushStyleColor(ImGuiCol_Text, iconColor);
        if (notif.type == Type::Status) {
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            ImVec2 spinnerCenter = ImVec2(ImGui::GetCursorScreenPos().x + 12, ImGui::GetCursorScreenPos().y + ImGui::GetTextLineHeight() / 2 + 10);
            drawList->PathArcTo(spinnerCenter, 8.0f, now * 4.0f, now * 4.0f + 4.0f, 32);
            drawList->PathStroke(ImGui::GetColorU32(iconColor), 0, 2.0f);
        } else if (iconText) {
            ImGui::TextUnformatted(iconText);
        }
        ImGui::PopStyleColor();

        ImGui::SameLine(35.0f);
        ImGui::TextWrapped("%s", notif.message.toRawUTF8());

        if (notif.type != Type::Error && notif.type != Type::Status) {
            float progress = juce::jlimit(0.0f, 1.0f, age / notif.lifetime);
            ImVec2 p_min = ImVec2(ImGui::GetWindowPos().x, ImGui::GetWindowPos().y + windowHeight - 3.0f);
            ImVec2 p_max = ImVec2(p_min.x + windowWidth * (1.0f - progress), p_min.y + 3.0f);
            ImGui::GetWindowDrawList()->AddRectFilled(p_min, p_max, ImGui::GetColorU32(iconColor));
        }
        
        ImGui::PopStyleVar();
        ImGui::End();

        bool shouldDismiss = (notif.type != Type::Error && (now - notif.startTime) > notif.lifetime);
        if (shouldDismiss) {
            it = m_notifications.erase(it);
        } else {
            currentY += windowHeight + padding;
            ++it;
        }
    }
}


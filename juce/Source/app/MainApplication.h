#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

namespace juce { class ApplicationProperties; }

class MainApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "Collider Audio Engine"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise (const juce::String& commandLine) override;
    void shutdown() override;
    void systemRequestedQuit() override;
    void anotherInstanceStarted (const juce::String& commandLine) override;

    struct StartupPolicy
    {
        enum class Mode
        {
            Default,
            LastUsed,
            CustomSize,
            Maximized,
            Fullscreen
        };

        Mode mode = Mode::LastUsed;
        int width = 0;
        int height = 0;
        bool forcePolicy = false;
    };

private:
    class MainWindow : public juce::DocumentWindow
    {
    public:
        MainWindow (juce::String name,
                    juce::ApplicationProperties& props,
                    const StartupPolicy& policy);
        void closeButtonPressed() override;
        bool keyPressed (const juce::KeyPress& key) override;

    private:
        // Track a manual "maximized-like" state (covers work area) and last normal bounds
        bool isMaximizedLike = false;
        juce::Rectangle<int> lastNormalBounds;

        void applyMaximizeLike();
        void restoreFromMaximizeLike();
    };

    std::unique_ptr<MainWindow> mainWindow;
    std::unique_ptr<juce::ApplicationProperties> appProperties;
    StartupPolicy m_startupPolicy;
};



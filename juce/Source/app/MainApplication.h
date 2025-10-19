#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

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

private:
    class MainWindow : public juce::DocumentWindow
    {
    public:
        MainWindow (juce::String name);
        void closeButtonPressed() override;
    };

    std::unique_ptr<MainWindow> mainWindow;
};



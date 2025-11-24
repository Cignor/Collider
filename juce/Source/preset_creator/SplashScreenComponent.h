#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../utils/VersionInfo.h"

/**
 * Splash screen component displayed on application startup.
 * 
 * Shows version information and can be dismissed with any
 * key press or mouse click.
 */
class SplashScreenComponent : public juce::Component,
                               public juce::Timer
{
public:
    SplashScreenComponent();
    ~SplashScreenComponent() override = default;
    
    void paint(juce::Graphics& g) override;
    void resized() override;
    
    // Dismiss the splash screen
    void dismiss();
    
    // Check if splash should be shown based on user preference
    static bool shouldShowSplashScreen(juce::PropertiesFile* properties);
    
    // Save preference to not show splash on next launch
    static void setShowSplashOnStartup(juce::PropertiesFile* properties, bool show);
    
    // Callback for when splash is dismissed
    std::function<void()> onDismiss;
    
    // Handle keyboard and mouse events (dismiss on any interaction except mouse move)
    bool keyPressed(const juce::KeyPress& key) override;
    bool keyStateChanged(bool isKeyDown) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    
    // Timer checks for keyboard/mouse activity and auto-dismiss
    void timerCallback() override;
    
private:
    static constexpr int AUTO_DISMISS_MS = 8000; // 8 seconds
    
    // Splash screen image
    juce::Image splashImage;
    
    // Load the splash screen image
    void loadSplashImage();
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SplashScreenComponent)
};


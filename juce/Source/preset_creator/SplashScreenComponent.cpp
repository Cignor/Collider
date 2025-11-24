#include "SplashScreenComponent.h"
#include <juce_graphics/juce_graphics.h>
#include <juce_gui_basics/juce_gui_basics.h>

SplashScreenComponent::SplashScreenComponent()
{
    // Load the splash screen image
    loadSplashImage();
    
    // Set size based on image, or default if image failed to load
    if (splashImage.isValid())
    {
        setSize(splashImage.getWidth(), splashImage.getHeight());
    }
    else
    {
        setSize(600, 400);
    }
    
    // Set up timer to check for user input (keyboard or mouse)
    // Check frequently (every 50ms) for any user activity
    startTimer(50);
    
    // Make component visible and able to receive focus
    setVisible(true);
    setWantsKeyboardFocus(true);
    setOpaque(false); // Transparent to respect PNG alpha channel
    setInterceptsMouseClicks(true, true); // Enable mouse clicks on this component and its children
    
    juce::Logger::writeToLog("[Splash] Component initialized, size: " + juce::String(getWidth()) + "x" + juce::String(getHeight()) + 
                              ", image valid: " + juce::String(splashImage.isValid() ? "yes" : "no"));
}

void SplashScreenComponent::loadSplashImage()
{
    // Try to load from icons folder next to executable (where CMake copies it)
    juce::File exeDir = juce::File::getSpecialLocation(juce::File::currentExecutableFile).getParentDirectory();
    juce::File splashFile = exeDir.getChildFile("icons/splash clean.png");
    
    if (splashFile.existsAsFile())
    {
        juce::Image loadedImage = juce::ImageFileFormat::loadFrom(splashFile);
        if (loadedImage.isValid())
        {
            splashImage = loadedImage;
            juce::Logger::writeToLog("[Splash] Loaded image from: " + splashFile.getFullPathName());
            return;
        }
    }
    
    // Fallback: Try project root icons folder (for development)
    juce::File currentDir = juce::File::getCurrentWorkingDirectory();
    juce::File projectRoot = exeDir.getParentDirectory().getParentDirectory();
    juce::File fallbackFile = projectRoot.getChildFile("icons/splash clean.png");
    
    if (fallbackFile.existsAsFile())
    {
        juce::Image loadedImage = juce::ImageFileFormat::loadFrom(fallbackFile);
        if (loadedImage.isValid())
        {
            splashImage = loadedImage;
            juce::Logger::writeToLog("[Splash] Loaded image from project root: " + fallbackFile.getFullPathName());
            return;
        }
    }
    
    juce::Logger::writeToLog("[Splash] Warning: Could not load splash screen image from " + splashFile.getFullPathName() + " or " + fallbackFile.getFullPathName());
}

void SplashScreenComponent::paint(juce::Graphics& g)
{
    // Draw splash image with transparency support - don't fill background
    if (splashImage.isValid())
    {
        // Draw image at its natural size, respecting alpha channel
        g.drawImage(splashImage, getLocalBounds().toFloat(), juce::RectanglePlacement::centred);
    }
    else
    {
        // Fallback to black background if image not loaded
        g.fillAll(juce::Colours::black);
    }
    
    // Position text on the right side to avoid overlapping with the bee on the left
    // The bee is typically on the left side, so we'll align text to the right
    const int padding = 40;
    const int leftMargin = getWidth() / 2; // Start text area from middle (right side)
    const int topPadding = 80; // Top padding for text area
    auto bounds = juce::Rectangle<int>(leftMargin, topPadding, getWidth() - leftMargin - padding, getHeight() - topPadding - padding);
    
    // Title - Application name (positioned on right side)
    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(48.0f, juce::Font::bold));
    auto titleBounds = bounds.removeFromTop(60);
    g.drawFittedText(
        VersionInfo::APPLICATION_NAME,
        titleBounds,
        juce::Justification::centredLeft,
        1
    );
    
    bounds.removeFromTop(2); // Very tight spacing - version close to title
    
    // Version
    g.setColour(juce::Colour(0xffcccccc));
    g.setFont(juce::Font(24.0f));
    auto versionBounds = bounds.removeFromTop(28);
    g.drawFittedText(
        "Version " + VersionInfo::getFullVersionString(),
        versionBounds,
        juce::Justification::centredLeft,
        1
    );
    
    bounds.removeFromTop(2); // Very tight spacing - author close to version
    
    // Author - aligned with version text
    g.setColour(juce::Colour(0xff888888));
    g.setFont(juce::Font(16.0f));
    auto authorBounds = bounds.removeFromTop(20);
    g.drawFittedText(
        "By " + juce::String(VersionInfo::AUTHOR),
        authorBounds,
        juce::Justification::centredLeft,
        1
    );
    
    // Bottom hints with better spacing and styling
    const int bottomPadding = 60; // More space from bottom to avoid touching drawing
    const int hintSpacing = 8;
    
    // Dismiss hint at bottom - with subtle styling
    g.setColour(juce::Colour(0xff888888));
    g.setFont(juce::Font(13.0f, juce::Font::plain));
    auto hintBounds = juce::Rectangle<int>(padding, getHeight() - bottomPadding, getWidth() - padding * 2, 18);
    g.drawFittedText(
        "Press any key or click to continue",
        hintBounds,
        juce::Justification::centredBottom,
        1
    );
    
    // Help hint below dismiss hint - smaller and more subtle
    g.setColour(juce::Colour(0xff666666));
    g.setFont(juce::Font(11.0f, juce::Font::plain));
    auto helpBounds = juce::Rectangle<int>(padding, getHeight() - bottomPadding + hintSpacing + 18, getWidth() - padding * 2, 16);
    g.drawFittedText(
        "Press F1 for Help",
        helpBounds,
        juce::Justification::centredBottom,
        1
    );
}

void SplashScreenComponent::resized()
{
    // Component size is fixed, but we can handle resizing if needed
    // Request focus when resized to ensure we can receive keyboard events
    grabKeyboardFocus();
}

bool SplashScreenComponent::keyPressed(const juce::KeyPress& key)
{
    juce::ignoreUnused(key);
    dismiss();
    return true;
}

bool SplashScreenComponent::keyStateChanged(bool isKeyDown)
{
    // Dismiss on any key state change (catches keys that might not trigger keyPressed)
    if (isKeyDown)
    {
        dismiss();
        return true;
    }
    return false;
}

void SplashScreenComponent::mouseDown(const juce::MouseEvent& e)
{
    juce::ignoreUnused(e);
    dismiss();
}

void SplashScreenComponent::mouseUp(const juce::MouseEvent& e)
{
    juce::ignoreUnused(e);
    dismiss();
}

void SplashScreenComponent::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    juce::ignoreUnused(e, wheel);
    dismiss();
}

void SplashScreenComponent::mouseDoubleClick(const juce::MouseEvent& e)
{
    juce::ignoreUnused(e);
    dismiss();
}

void SplashScreenComponent::timerCallback()
{
    // Check if any mouse buttons are currently down
    auto& desktop = juce::Desktop::getInstance();
    auto mods = desktop.getMainMouseSource().getCurrentModifiers();
    if (mods.isAnyMouseButtonDown())
    {
        dismiss();
        return;
    }
    
    // Check if any keys are currently pressed (check common keys)
    // This is a simple approach - check modifier keys and a few common keys
    if (mods.isAnyModifierKeyDown() || 
        juce::KeyPress::isKeyCurrentlyDown(juce::KeyPress::spaceKey) ||
        juce::KeyPress::isKeyCurrentlyDown(juce::KeyPress::returnKey) ||
        juce::KeyPress::isKeyCurrentlyDown(juce::KeyPress::escapeKey))
    {
        dismiss();
        return;
    }
    
    // Auto-dismiss after timeout (check if we've been showing for 8 seconds)
    static auto startTime = juce::Time::getMillisecondCounterHiRes();
    auto elapsed = juce::Time::getMillisecondCounterHiRes() - startTime;
    if (elapsed >= AUTO_DISMISS_MS)
    {
        dismiss();
    }
}

void SplashScreenComponent::dismiss()
{
    stopTimer();
    
    if (onDismiss)
    {
        onDismiss();
    }
}

bool SplashScreenComponent::shouldShowSplashScreen(juce::PropertiesFile* properties)
{
    if (properties == nullptr)
        return true; // Default to showing on first launch
    
    // Check if user has disabled splash screen
    // Default to true if not set (first launch)
    return properties->getBoolValue("showSplashOnStartup", true);
}

void SplashScreenComponent::setShowSplashOnStartup(juce::PropertiesFile* properties, bool show)
{
    if (properties != nullptr)
    {
        properties->setValue("showSplashOnStartup", show);
    }
}


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
    
    // Set up auto-dismiss timer
    if (autoDismissEnabled)
    {
        startTimer(AUTO_DISMISS_MS);
    }
    
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
    auto bounds = juce::Rectangle<int>(leftMargin, padding, getWidth() - leftMargin - padding, getHeight() - padding * 2);
    
    // Title - Application name (positioned on right side)
    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(48.0f, juce::Font::bold));
    auto titleBounds = bounds.removeFromTop(80);
    g.drawFittedText(
        VersionInfo::APPLICATION_NAME,
        titleBounds,
        juce::Justification::centredLeft,
        1
    );
    
    bounds.removeFromTop(20);
    
    // Version
    g.setColour(juce::Colour(0xffcccccc));
    g.setFont(juce::Font(24.0f));
    auto versionBounds = bounds.removeFromTop(40);
    g.drawFittedText(
        "Version " + VersionInfo::getFullVersionString(),
        versionBounds,
        juce::Justification::centredLeft,
        1
    );
    
    bounds.removeFromTop(20);
    
    // Build type
    g.setColour(juce::Colour(0xffaaaaaa));
    g.setFont(juce::Font(18.0f));
    auto buildBounds = bounds.removeFromTop(30);
    g.drawFittedText(
        VersionInfo::BUILD_TYPE,
        buildBounds,
        juce::Justification::centredLeft,
        1
    );
    
    bounds.removeFromTop(40);
    
    // Author
    g.setColour(juce::Colour(0xff888888));
    g.setFont(juce::Font(16.0f));
    auto authorBounds = bounds.removeFromTop(25);
    g.drawFittedText(
        "By " + juce::String(VersionInfo::AUTHOR),
        authorBounds,
        juce::Justification::centredLeft,
        1
    );
    
    // Dismiss hint at bottom right
    g.setColour(juce::Colour(0xff666666));
    g.setFont(juce::Font(14.0f));
    auto hintBounds = juce::Rectangle<int>(padding, getHeight() - 30, getWidth() - padding * 2, 20);
    g.drawFittedText(
        "Press any key or click to continue",
        hintBounds,
        juce::Justification::centredBottom,
        1
    );
}

void SplashScreenComponent::resized()
{
    // Component size is fixed, but we can handle resizing if needed
}

bool SplashScreenComponent::keyPressed(const juce::KeyPress& key)
{
    dismiss();
    juce::ignoreUnused(key);
    return true;
}

void SplashScreenComponent::mouseDown(const juce::MouseEvent& e)
{
    juce::ignoreUnused(e);
    dismiss();
}

void SplashScreenComponent::timerCallback()
{
    stopTimer();
    dismiss();
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


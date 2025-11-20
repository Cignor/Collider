#include "SplashScreenComponent.h"
#include <juce_graphics/juce_graphics.h>

SplashScreenComponent::SplashScreenComponent()
{
    setSize(600, 400);
    
    // Set up auto-dismiss timer
    if (autoDismissEnabled)
    {
        startTimer(AUTO_DISMISS_MS);
    }
    
    // Make component visible and able to receive focus
    setVisible(true);
    setWantsKeyboardFocus(true);
}

void SplashScreenComponent::paint(juce::Graphics& g)
{
    // Background gradient
    juce::ColourGradient gradient(
        juce::Colour(0xff1a1a1a), 0, 0,
        juce::Colour(0xff2a2a2a), 0, (float)getHeight(),
        false
    );
    g.setGradientFill(gradient);
    g.fillAll();
    
    // Outer border
    g.setColour(juce::Colour(0xff444444));
    g.drawRect(getLocalBounds(), 2);
    
    // Inner border
    g.setColour(juce::Colour(0xff666666));
    g.drawRect(getLocalBounds().reduced(4), 1);
    
    const int padding = 40;
    auto bounds = getLocalBounds().reduced(padding);
    
    // Title - Application name
    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(48.0f, juce::Font::bold));
    auto titleBounds = bounds.removeFromTop(80);
    g.drawFittedText(
        VersionInfo::APPLICATION_NAME,
        titleBounds,
        juce::Justification::centred,
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
        juce::Justification::centred,
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
        juce::Justification::centred,
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
        juce::Justification::centred,
        1
    );
    
    bounds.removeFromTop(40);
    
    // Dismiss hint
    g.setColour(juce::Colour(0xff666666));
    g.setFont(juce::Font(14.0f));
    auto hintBounds = bounds.removeFromTop(20);
    g.drawFittedText(
        "Press any key or click to continue",
        hintBounds,
        juce::Justification::centred,
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


#include "MainApplication.h"
#include "../ui/MainComponent.h"
#include <juce_data_structures/juce_data_structures.h>

void MainApplication::initialise (const juce::String& commandLine)
{
    // Install a file logger so logs are visible on disk
    auto logDir = juce::File::getCurrentWorkingDirectory().getChildFile ("juce").getChildFile ("logs");
    logDir.createDirectory();
    auto* fileLogger = juce::FileLogger::createDateStampedLogger (logDir.getFullPathName(), "engine", ".log", "[JUCE] Logger started");
    juce::Logger::setCurrentLogger (fileLogger);
    juce::Logger::writeToLog ("[JUCE] Log file: " + (fileLogger != nullptr ? fileLogger->getLogFile().getFullPathName() : juce::String("<none>")));

    // Initialize ApplicationProperties
    appProperties = std::make_unique<juce::ApplicationProperties>();
    juce::PropertiesFile::Options opts;
    opts.applicationName     = getApplicationName();
    opts.filenameSuffix      = ".settings";
    opts.osxLibrarySubFolder = "Application Support";
    opts.storageFormat       = juce::PropertiesFile::storeAsXML;

    auto appDataDir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                                .getChildFile(getApplicationName());
    appDataDir.createDirectory();
    opts.folderName = appDataDir.getFullPathName();
    appProperties->setStorageParameters (opts);

    // Parse command line for startup policy
    {
        juce::StringArray args;
        args.addTokens (commandLine, " ", "\"");
        args.removeEmptyStrings();
        m_startupPolicy.mode = StartupPolicy::Mode::LastUsed;

        for (int i = 0; i < args.size(); ++i)
        {
            if (args[i] == "--window" && i + 1 < args.size())
            {
                auto parts = juce::StringArray::fromTokens (args[++i], "x", "");
                if (parts.size() == 2)
                {
                    m_startupPolicy.mode = StartupPolicy::Mode::CustomSize;
                    m_startupPolicy.width = parts[0].getIntValue();
                    m_startupPolicy.height = parts[1].getIntValue();
                    m_startupPolicy.forcePolicy = true;
                }
            }
            else if (args[i] == "--preset" && i + 1 < args.size())
            {
                auto preset = args[++i].toLowerCase();
                if (preset == "small") { m_startupPolicy.mode = StartupPolicy::Mode::CustomSize; m_startupPolicy.width = 1280; m_startupPolicy.height = 800; }
                else if (preset == "medium") { m_startupPolicy.mode = StartupPolicy::Mode::CustomSize; m_startupPolicy.width = 1600; m_startupPolicy.height = 900; }
                else if (preset == "large") { m_startupPolicy.mode = StartupPolicy::Mode::CustomSize; m_startupPolicy.width = 1920; m_startupPolicy.height = 1080; }
                m_startupPolicy.forcePolicy = true;
            }
            else if (args[i] == "--maximized")
            {
                m_startupPolicy.mode = StartupPolicy::Mode::Maximized;
                m_startupPolicy.forcePolicy = true;
            }
            else if (args[i] == "--fullscreen")
            {
                m_startupPolicy.mode = StartupPolicy::Mode::Fullscreen;
                m_startupPolicy.forcePolicy = true;
            }
            else if (args[i] == "--ignore-last")
            {
                m_startupPolicy.forcePolicy = true;
                if (m_startupPolicy.mode == StartupPolicy::Mode::LastUsed)
                    m_startupPolicy.mode = StartupPolicy::Mode::Default;
            }
        }
    }

    // Create main window with properties and policy
    mainWindow = std::make_unique<MainWindow> (getApplicationName(), *appProperties, m_startupPolicy);
}

void MainApplication::shutdown()
{
    juce::Logger::writeToLog ("[JUCE] Shutting down");

    if (mainWindow != nullptr && appProperties != nullptr)
    {
        if (auto* props = appProperties->getUserSettings())
        {
            if (! mainWindow->isFullScreen())
            {
                props->setValue ("mainWindowState", mainWindow->getWindowStateAsString());
                appProperties->saveIfNeeded();
                juce::Logger::writeToLog ("[JUCE] Saved window state.");
            }
        }
    }

    juce::Logger::setCurrentLogger (nullptr);
    mainWindow.reset();
    appProperties.reset();
}

void MainApplication::systemRequestedQuit()
{
    quit();
}

void MainApplication::anotherInstanceStarted (const juce::String&)
{
}

MainApplication::MainWindow::MainWindow (juce::String name,
                                         juce::ApplicationProperties& props,
                                         const StartupPolicy& policy)
    : DocumentWindow (name,
                      juce::Desktop::getInstance().getDefaultLookAndFeel()
                        .findColour (juce::ResizableWindow::backgroundColourId),
                      DocumentWindow::allButtons)
{
    setUsingNativeTitleBar (true);
    setContentOwned (new MainComponent(), true);
    setResizable (true, true);
    setResizeLimits (900, 600, 8192, 8192);

    bool restored = false;
    if (policy.mode == StartupPolicy::Mode::LastUsed && ! policy.forcePolicy)
    {
        if (auto* userSettings = props.getUserSettings())
        {
            auto savedState = userSettings->getValue ("mainWindowState");
            if (savedState.isNotEmpty())
            {
                restored = restoreWindowStateFromString (savedState);
                juce::Logger::writeToLog ("[JUCE] Restored window state from settings.");
            }
        }
    }

    if (! restored)
    {
        juce::Logger::writeToLog ("[JUCE] No saved state found or policy forced. Applying startup policy.");
        switch (policy.mode)
        {
            case StartupPolicy::Mode::CustomSize:
                if (policy.width > 0 && policy.height > 0)
                    centreWithSize (policy.width, policy.height);
                else
                    centreWithSize (2600, 1800);
                break;
            case StartupPolicy::Mode::Maximized:
                centreWithSize (2600, 1800);
                break;
            case StartupPolicy::Mode::Fullscreen:
                centreWithSize (2600, 1800);
                break;
            case StartupPolicy::Mode::Default:
            case StartupPolicy::Mode::LastUsed:
            default:
                centreWithSize (2600, 1800);
                break;
        }
    }

    setVisible (true);

    if (! restored)
    {
        if (policy.mode == StartupPolicy::Mode::Maximized)
            applyMaximizeLike();
        else if (policy.mode == StartupPolicy::Mode::Fullscreen)
            setFullScreen (true);
    }

    auto& displays = juce::Desktop::getInstance().getDisplays();
    auto* display = displays.getDisplayForRect (getBounds());
    if (display == nullptr)
        display = displays.getPrimaryDisplay();
    if (display != nullptr)
        setBounds (getBounds().constrainedWithin (display->userArea));
}

void MainApplication::MainWindow::closeButtonPressed()
{
    juce::JUCEApplication::getInstance()->systemRequestedQuit();
}

bool MainApplication::MainWindow::keyPressed (const juce::KeyPress& key)
{
    // F11 toggles fullscreen
    if (key.getKeyCode() == juce::KeyPress::F11Key)
    {
        setFullScreen (! isFullScreen());
        return true;
    }

    // Alt+Enter toggles maximized-like state (avoid interfering when in fullscreen)
    if (key.getKeyCode() == juce::KeyPress::returnKey && key.getModifiers().isAltDown())
    {
        if (isFullScreen())
            setFullScreen (false);
        else if (! isMaximizedLike)
            applyMaximizeLike();
        else
            restoreFromMaximizeLike();
        return true;
    }

    return DocumentWindow::keyPressed (key);
}

void MainApplication::MainWindow::applyMaximizeLike()
{
    if (! isMaximizedLike)
    {
        lastNormalBounds = getBounds();
        auto& displays = juce::Desktop::getInstance().getDisplays();
        if (auto* display = displays.getDisplayForRect (getBounds()))
            setBounds (display->userArea);
        else if (auto* primary = displays.getPrimaryDisplay())
            setBounds (primary->userArea);
        isMaximizedLike = true;
    }
}

void MainApplication::MainWindow::restoreFromMaximizeLike()
{
    if (isMaximizedLike)
    {
        if (! lastNormalBounds.isEmpty())
            setBounds (lastNormalBounds);
        isMaximizedLike = false;
    }
}



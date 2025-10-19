#include "MainApplication.h"
#include "../ui/MainComponent.h"

void MainApplication::initialise (const juce::String&)
{
    // Install a file logger so logs are visible on disk
    auto logDir = juce::File::getCurrentWorkingDirectory().getChildFile ("juce").getChildFile ("logs");
    logDir.createDirectory();
    auto* fileLogger = juce::FileLogger::createDateStampedLogger (logDir.getFullPathName(), "engine", ".log", "[JUCE] Logger started");
    juce::Logger::setCurrentLogger (fileLogger);
    juce::Logger::writeToLog ("[JUCE] Log file: " + (fileLogger != nullptr ? fileLogger->getLogFile().getFullPathName() : juce::String("<none>")));
    mainWindow = std::make_unique<MainWindow> (getApplicationName());
}

void MainApplication::shutdown()
{
    juce::Logger::writeToLog ("[JUCE] Shutting down");
    juce::Logger::setCurrentLogger (nullptr);
    mainWindow.reset();
}

void MainApplication::systemRequestedQuit()
{
    quit();
}

void MainApplication::anotherInstanceStarted (const juce::String&)
{
}

MainApplication::MainWindow::MainWindow (juce::String name)
    : DocumentWindow (name,
                      juce::Desktop::getInstance().getDefaultLookAndFeel()
                        .findColour (juce::ResizableWindow::backgroundColourId),
                      DocumentWindow::allButtons)
{
    setUsingNativeTitleBar (true);
    // IT MUST BE THIS:
    setContentOwned (new MainComponent(), true);
    setResizable (true, true);
    centreWithSize (2600, 1800);
    setVisible (true);
}

void MainApplication::MainWindow::closeButtonPressed()
{
    juce::JUCEApplication::getInstance()->systemRequestedQuit();
}



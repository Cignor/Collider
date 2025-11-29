#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "../UpdaterTypes.h"

namespace Updater
{

/**
 * Dialog showing download progress for update files
 * Displays progress bar, speed, file count, and allows cancellation
 */
class DownloadProgressDialog : public juce::Component, public juce::Timer
{
public:
    DownloadProgressDialog();
    ~DownloadProgressDialog() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void timerCallback() override;

    // Update progress
    void setProgress(const DownloadProgress& progress);

    // Callbacks
    std::function<void()> onCancel;
    std::function<void()> onRestartNow;
    std::function<void()> onRestartLater;

    // Show completion state
    void showCompleted(bool requiresRestart);

private:
    DownloadProgress currentProgress;
    double           progressValue;
    bool             isCompleted = false;
    bool             needsRestart = false;

    juce::Label       titleLabel;
    juce::Label       statusLabel;
    juce::Label       speedLabel;
    juce::Label       filesLabel;
    juce::ProgressBar progressBar;

    juce::TextButton cancelButton;
    juce::TextButton restartNowButton;
    juce::TextButton restartLaterButton;

    void         setupComponents();
    juce::String formatSpeed(double bytesPerSec);
    juce::String formatFileSize(juce::int64 bytes);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DownloadProgressDialog)
};

} // namespace Updater

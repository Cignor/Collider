#include "DownloadProgressDialog.h"

namespace Updater
{

DownloadProgressDialog::DownloadProgressDialog() : progressValue(0.0), progressBar(progressValue)
{
    setupComponents();
    startTimer(100); // Update UI every 100ms
}

void DownloadProgressDialog::setupComponents()
{
    // Title
    addAndMakeVisible(titleLabel);
    titleLabel.setText("Downloading Update", juce::dontSendNotification);
    titleLabel.setFont(juce::Font(20.0f, juce::Font::bold));
    titleLabel.setJustificationType(juce::Justification::centred);

    // Status (current file)
    addAndMakeVisible(statusLabel);
    statusLabel.setText("Preparing download...", juce::dontSendNotification);
    statusLabel.setFont(juce::Font(13.0f));

    // Speed
    addAndMakeVisible(speedLabel);
    speedLabel.setText("Speed: --", juce::dontSendNotification);
    speedLabel.setFont(juce::Font(12.0f));

    // Files count
    addAndMakeVisible(filesLabel);
    filesLabel.setText("Files: 0 / 0", juce::dontSendNotification);
    filesLabel.setFont(juce::Font(12.0f));

    // Progress bar
    addAndMakeVisible(progressBar);

    // Cancel button
    addAndMakeVisible(cancelButton);
    cancelButton.setButtonText("Cancel");
    cancelButton.onClick = [this]() {
        if (onCancel)
            onCancel();
    };

    // Restart buttons (hidden initially)
    addChildComponent(restartNowButton);
    restartNowButton.setButtonText("Restart Now");
    restartNowButton.onClick = [this]() {
        if (onRestartNow)
            onRestartNow();
    };

    addChildComponent(restartLaterButton);
    restartLaterButton.setButtonText("Restart Later");
    restartLaterButton.onClick = [this]() {
        if (onRestartLater)
            onRestartLater();
    };

    setSize(450, 220);
}

void DownloadProgressDialog::paint(juce::Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
}

void DownloadProgressDialog::resized()
{
    auto bounds = getLocalBounds().reduced(20);

    titleLabel.setBounds(bounds.removeFromTop(35));
    bounds.removeFromTop(15);

    statusLabel.setBounds(bounds.removeFromTop(25));
    bounds.removeFromTop(10);

    progressBar.setBounds(bounds.removeFromTop(25));
    bounds.removeFromTop(15);

    auto infoRow = bounds.removeFromTop(20);
    speedLabel.setBounds(infoRow.removeFromLeft(getWidth() / 2 - 20));
    filesLabel.setBounds(infoRow);

    bounds.removeFromTop(20);

    // Buttons at bottom
    auto buttonArea = bounds.removeFromBottom(30);
    auto buttonWidth = 130;
    auto spacing = 10;

    if (isCompleted && needsRestart)
    {
        restartLaterButton.setBounds(buttonArea.removeFromLeft(buttonWidth));
        buttonArea.removeFromLeft(spacing);
        restartNowButton.setBounds(buttonArea.removeFromRight(buttonWidth));
    }
    else if (!isCompleted)
    {
        cancelButton.setBounds(buttonArea.removeFromRight(buttonWidth));
    }
}

void DownloadProgressDialog::timerCallback()
{
    // Update progress value (ProgressBar will automatically repaint)
    progressValue = currentProgress.getProgress();
}

void DownloadProgressDialog::setProgress(const DownloadProgress& progress)
{
    currentProgress = progress;

    // Update status
    if (progress.currentFile.isNotEmpty())
    {
        statusLabel.setText("Downloading: " + progress.currentFile, juce::dontSendNotification);
    }

    // Update speed
    speedLabel.setText(
        "Speed: " + formatSpeed(progress.speedBytesPerSec), juce::dontSendNotification);

    // Update files count
    filesLabel.setText(
        "Files: " + juce::String(progress.filesCompleted) + " / " +
            juce::String(progress.totalFiles),
        juce::dontSendNotification);
}

void DownloadProgressDialog::showCompleted(bool requiresRestart)
{
    isCompleted = true;
    needsRestart = requiresRestart;

    titleLabel.setText("Update Complete", juce::dontSendNotification);

    if (requiresRestart)
    {
        statusLabel.setText(
            "Critical files updated. Please restart the application.", juce::dontSendNotification);
        cancelButton.setVisible(false);
        restartNowButton.setVisible(true);
        restartLaterButton.setVisible(true);
    }
    else
    {
        statusLabel.setText("Update installed successfully!", juce::dontSendNotification);
        cancelButton.setButtonText("Close");
    }

    progressValue = 1.0;
    resized();
}

juce::String DownloadProgressDialog::formatSpeed(double bytesPerSec)
{
    if (bytesPerSec < 1024)
        return juce::String(bytesPerSec, 0) + " B/s";
    else if (bytesPerSec < 1024 * 1024)
        return juce::String(bytesPerSec / 1024.0, 1) + " KB/s";
    else
        return juce::String(bytesPerSec / (1024.0 * 1024.0), 2) + " MB/s";
}

juce::String DownloadProgressDialog::formatFileSize(juce::int64 bytes)
{
    if (bytes < 1024)
        return juce::String(bytes) + " B";
    else if (bytes < 1024 * 1024)
        return juce::String(bytes / 1024.0, 1) + " KB";
    else if (bytes < 1024 * 1024 * 1024)
        return juce::String(bytes / (1024.0 * 1024.0), 1) + " MB";
    else
        return juce::String(bytes / (1024.0 * 1024.0 * 1024.0), 2) + " GB";
}

} // namespace Updater

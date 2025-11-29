#include "UpdateAvailableDialog.h"

namespace Updater
{

UpdateAvailableDialog::UpdateAvailableDialog(const UpdateInfo& updateInfo) : info(updateInfo)
{
    setupComponents();
}

void UpdateAvailableDialog::setupComponents()
{
    // Title
    addAndMakeVisible(titleLabel);
    titleLabel.setText("Update Available", juce::dontSendNotification);
    titleLabel.setFont(juce::Font(24.0f, juce::Font::bold));
    titleLabel.setJustificationType(juce::Justification::centred);

    // Version info
    addAndMakeVisible(currentVersionLabel);
    currentVersionLabel.setText(
        "Current Version: " + info.currentVersion, juce::dontSendNotification);
    currentVersionLabel.setFont(juce::Font(14.0f));

    addAndMakeVisible(newVersionLabel);
    newVersionLabel.setText("New Version: " + info.newVersion, juce::dontSendNotification);
    newVersionLabel.setFont(juce::Font(14.0f, juce::Font::bold));

    // Download size
    addAndMakeVisible(downloadSizeLabel);
    downloadSizeLabel.setText(
        "Download Size: " + formatFileSize(info.totalDownloadSize) + " (" +
            juce::String(info.filesToDownload.size()) + " files)",
        juce::dontSendNotification);
    downloadSizeLabel.setFont(juce::Font(12.0f));

    // Changelog
    addAndMakeVisible(changelogEditor);
    changelogEditor.setMultiLine(true);
    changelogEditor.setReadOnly(true);
    changelogEditor.setScrollbarsShown(true);
    changelogEditor.setText(
        info.changelogSummary.isNotEmpty() ? info.changelogSummary : "No changelog available");

    // Changelog link
    if (info.changelogUrl.isNotEmpty())
    {
        addAndMakeVisible(changelogLink);
        changelogLink.setButtonText("View Full Changelog");
        changelogLink.setURL(juce::URL(info.changelogUrl));
    }

    // Buttons
    addAndMakeVisible(updateButton);
    updateButton.setButtonText("Update Now");
    updateButton.onClick = [this]() {
        if (onUpdateNow)
            onUpdateNow();
    };

    addAndMakeVisible(remindLaterButton);
    remindLaterButton.setButtonText("Remind Me Later");
    remindLaterButton.onClick = [this]() {
        if (onRemindLater)
            onRemindLater();
    };

    addAndMakeVisible(skipButton);
    skipButton.setButtonText("Skip This Version");
    skipButton.onClick = [this]() {
        if (onSkipVersion)
            onSkipVersion();
    };

    // Auto-check toggle
    addAndMakeVisible(autoCheckToggle);
    autoCheckToggle.setButtonText("Automatically check for updates on startup");
    autoCheckToggle.setToggleState(true, juce::dontSendNotification);

    setSize(500, 400);
}

void UpdateAvailableDialog::paint(juce::Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
}

void UpdateAvailableDialog::resized()
{
    auto bounds = getLocalBounds().reduced(20);

    titleLabel.setBounds(bounds.removeFromTop(40));
    bounds.removeFromTop(10);

    currentVersionLabel.setBounds(bounds.removeFromTop(25));
    newVersionLabel.setBounds(bounds.removeFromTop(25));
    bounds.removeFromTop(5);
    downloadSizeLabel.setBounds(bounds.removeFromTop(20));
    bounds.removeFromTop(15);

    // Changelog
    auto changelogBounds = bounds.removeFromTop(150);
    changelogEditor.setBounds(changelogBounds);
    bounds.removeFromTop(5);

    if (changelogLink.isVisible())
    {
        changelogLink.setBounds(bounds.removeFromTop(25));
        bounds.removeFromTop(10);
    }

    // Auto-check toggle
    autoCheckToggle.setBounds(bounds.removeFromTop(25));
    bounds.removeFromTop(15);

    // Buttons at bottom
    auto buttonArea = bounds.removeFromBottom(30);
    auto buttonWidth = 140;
    auto spacing = 10;

    skipButton.setBounds(buttonArea.removeFromLeft(buttonWidth));
    buttonArea.removeFromLeft(spacing);
    remindLaterButton.setBounds(buttonArea.removeFromLeft(buttonWidth));
    buttonArea.removeFromLeft(spacing);
    updateButton.setBounds(buttonArea.removeFromRight(buttonWidth));
}

juce::String UpdateAvailableDialog::formatFileSize(juce::int64 bytes)
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

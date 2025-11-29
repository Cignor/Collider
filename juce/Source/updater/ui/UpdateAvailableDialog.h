#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "../UpdaterTypes.h"

namespace Updater
{

/**
 * Dialog shown when an update is available
 * Displays version info, changelog, and download options
 */
class UpdateAvailableDialog : public juce::Component
{
public:
    UpdateAvailableDialog(const UpdateInfo& updateInfo);
    ~UpdateAvailableDialog() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // Callbacks
    std::function<void()> onUpdateNow;
    std::function<void()> onRemindLater;
    std::function<void()> onSkipVersion;

private:
    UpdateInfo info;

    juce::Label           titleLabel;
    juce::Label           currentVersionLabel;
    juce::Label           newVersionLabel;
    juce::Label           downloadSizeLabel;
    juce::TextEditor      changelogEditor;
    juce::HyperlinkButton changelogLink;

    juce::TextButton updateButton;
    juce::TextButton remindLaterButton;
    juce::TextButton skipButton;

    juce::ToggleButton autoCheckToggle;

    void         setupComponents();
    juce::String formatFileSize(juce::int64 bytes);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UpdateAvailableDialog)
};

} // namespace Updater

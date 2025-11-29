#include "UpdateDownloadDialog.h"
#include <algorithm>

namespace Updater
{

UpdateDownloadDialog::UpdateDownloadDialog() {}

void UpdateDownloadDialog::open(const UpdateInfo& info)
{
    updateInfo = info;
    isOpen = true;
    isDownloading = false;
    // Reset filter
    searchFilter[0] = 0;
}

void UpdateDownloadDialog::setDownloadProgress(const DownloadProgress& progress)
{
    currentProgress = progress;
}

void UpdateDownloadDialog::render()
{
    if (!isOpen)
        return;

    // Set window size and position
    ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin("Software Update Available", &isOpen, ImGuiWindowFlags_None))
    {
        ImGui::End();
        return;
    }

    // Header info
    // Header info
    if (updateInfo.updateAvailable)
    {
        ImGui::TextColored(
            ImVec4(0.4f, 1.0f, 0.4f, 1.0f),
            "New Version Available: %s",
            updateInfo.newVersion.toRawUTF8());
        ImGui::SameLine();
        ImGui::TextDisabled("(Current: %s)", updateInfo.currentVersion.toRawUTF8());

        if (updateInfo.requiresRestart)
        {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "[Requires Restart]");
        }
    }
    else
    {
        ImGui::TextColored(
            ImVec4(0.4f, 0.8f, 1.0f, 1.0f),
            "You are up to date! (Version %s)",
            updateInfo.currentVersion.toRawUTF8());
    }

    ImGui::Separator();

    // Search filter
    ImGui::Text("Search Files:");
    ImGui::SameLine();
    ImGui::PushItemWidth(300.0f);
    ImGui::InputText("##search", searchFilter, sizeof(searchFilter));
    ImGui::PopItemWidth();

    ImGui::Separator();

    // File list and controls split
    float footerHeight = 150.0f;
    ImGui::BeginChild("FileList", ImVec2(0, -footerHeight), false);
    renderFileList();
    ImGui::EndChild();

    ImGui::Separator();

    ImGui::BeginChild("Controls", ImVec2(0, 0), false);
    renderControls();
    ImGui::EndChild();

    ImGui::End();
}

void UpdateDownloadDialog::renderFileList()
{
    const auto& filesToShow = updateInfo.allRemoteFiles;

    if (filesToShow.isEmpty())
    {
        ImGui::Text("No files found on server.");
        return;
    }

    // Filter logic
    std::vector<int> filteredIndices;
    juce::String     search(searchFilter);
    search = search.toLowerCase();

    for (int i = 0; i < filesToShow.size(); ++i)
    {
        const auto& file = filesToShow.getReference(i);
        if (search.isEmpty() || file.relativePath.toLowerCase().contains(search))
        {
            filteredIndices.push_back(i);
        }
    }

    if (ImGui::BeginTable(
            "UpdateFilesTable",
            4,
            ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Borders |
                ImGuiTableFlags_RowBg))
    {
        ImGui::TableSetupColumn("File Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();

        for (int idx : filteredIndices)
        {
            const auto& file = filesToShow.getReference(idx);

            ImGui::TableNextRow();

            // File Name
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%s", file.relativePath.toRawUTF8());
            if (file.critical)
            {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "(Critical)");
            }

            // Type
            ImGui::TableSetColumnIndex(1);
            juce::String ext = file.relativePath.fromLastOccurrenceOf(".", false, false);
            ImGui::Text("%s", ext.toRawUTF8());

            // Size
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%s", getFormattedFileSize(file.size).toRawUTF8());

            // Status
            ImGui::TableSetColumnIndex(3);

            bool needsUpdate = false;
            for (const auto& f : updateInfo.filesToDownload)
            {
                if (f.relativePath == file.relativePath)
                {
                    needsUpdate = true;
                    break;
                }
            }

            if (isDownloading && currentProgress.currentFile == file.relativePath)
            {
                ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Downloading...");
            }
            else if (needsUpdate)
            {
                ImGui::Text("Pending");
            }
            else
            {
                ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "Installed");
            }
        }
        ImGui::EndTable();
    }
}

void UpdateDownloadDialog::renderControls()
{
    // Summary stats
    juce::int64 totalSize = updateInfo.totalDownloadSize;
    int         fileCount = updateInfo.filesToDownload.size();

    if (updateInfo.updateAvailable)
    {
        ImGui::Text("Summary: %d files to update", fileCount);
        ImGui::SameLine();
        ImGui::Text("| Total Download Size: %s", getFormattedFileSize(totalSize).toRawUTF8());
    }
    else
    {
        ImGui::Text("Summary: %d files verified", updateInfo.allRemoteFiles.size());
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "| System is up to date");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (isDownloading)
    {
        // Progress Bar
        float progress = (float)currentProgress.getProgress();
        ImGui::ProgressBar(progress, ImVec2(-1, 0));

        ImGui::Text("Downloading: %s", currentProgress.currentFile.toRawUTF8());
        ImGui::Text("Speed: %.2f MB/s", currentProgress.speedBytesPerSec / (1024.0 * 1024.0));
        ImGui::SameLine();
        ImGui::Text(
            "| Downloaded: %s / %s",
            getFormattedFileSize(currentProgress.bytesDownloaded).toRawUTF8(),
            getFormattedFileSize(currentProgress.totalBytes).toRawUTF8());

        if (ImGui::Button("Cancel", ImVec2(120, 30)))
        {
            if (onCancelDownload)
                onCancelDownload();
        }
    }
    else
    {
        // Action Buttons
        if (!updateInfo.updateAvailable)
            ImGui::BeginDisabled();

        if (ImGui::Button("Update Now", ImVec2(150, 40)))
        {
            if (onStartDownload)
                onStartDownload();
        }

        if (!updateInfo.updateAvailable)
            ImGui::EndDisabled();

        ImGui::SameLine();
        if (ImGui::Button("Remind Me Later", ImVec2(150, 40)))
        {
            close();
        }

        ImGui::SameLine();
        if (ImGui::Button("Skip This Version", ImVec2(150, 40)))
        {
            if (onSkipVersion)
                onSkipVersion();
            close();
        }
    }

    // Changelog link/summary
    ImGui::Spacing();
    if (updateInfo.changelogSummary.isNotEmpty())
    {
        ImGui::TextWrapped("What's New: %s", updateInfo.changelogSummary.toRawUTF8());
    }
}

juce::String UpdateDownloadDialog::getFormattedFileSize(juce::int64 size) const
{
    if (size >= 1024 * 1024 * 1024)
        return juce::String::formatted("%.2f GB", size / (1024.0 * 1024.0 * 1024.0));
    else if (size >= 1024 * 1024)
        return juce::String::formatted("%.1f MB", size / (1024.0 * 1024.0));
    else if (size >= 1024)
        return juce::String::formatted("%.1f KB", size / 1024.0);
    else
        return juce::String::formatted("%d B", (int)size);
}

} // namespace Updater

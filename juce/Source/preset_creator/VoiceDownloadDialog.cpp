#include "VoiceDownloadDialog.h"
#include "../audio/voices/VoiceDownloadHelper.h"
#include "../audio/voices/VoiceDownloadThread.h"
#include <set>
#include <algorithm>

VoiceDownloadDialog::VoiceDownloadDialog()
{
    downloadThread.startThread();
    availableVoices = VoiceDownloadHelper::getAllAvailableVoices();
    voiceSelected.resize(availableVoices.size(), false);
    buildLanguageList();
    refreshVoiceStatuses();
    wasDownloading = false;
}

VoiceDownloadDialog::~VoiceDownloadDialog()
{
    downloadThread.stopThread(5000);
}

void VoiceDownloadDialog::render()
{
    if (!isOpen)
        return;
    
    // Auto-refresh status when download completes
    bool isDownloading = downloadThread.isDownloading();
    if (wasDownloading && !isDownloading)
    {
        // Download just finished - refresh statuses to show newly installed voices
        refreshVoiceStatuses();
    }
    wasDownloading = isDownloading;
    
    // Set window size and position
    ImGui::SetNextWindowSize(ImVec2(900, 600), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(100, 100), ImGuiCond_FirstUseEver);
    
    if (!ImGui::Begin("Download Piper Voices", &isOpen, ImGuiWindowFlags_None))
    {
        ImGui::End();
        return;
    }
    
    // Header text
    ImGui::Text("Download additional Piper TTS voices for use in TTS Performer nodes.");
    ImGui::Separator();
    
    // Search and filter controls
    ImGui::PushItemWidth(300.0f);
    ImGui::InputText("Search", searchFilter, sizeof(searchFilter));
    ImGui::PopItemWidth();
    
    ImGui::SameLine();
    ImGui::PushItemWidth(200.0f);
    if (ImGui::Combo("Language", &selectedLanguageFilter, [](void* data, int idx, const char** out_text)
    {
        auto* list = static_cast<juce::StringArray*>(data);
        if (idx >= 0 && idx < list->size())
        {
            *out_text = list->getReference(idx).toRawUTF8();
            return true;
        }
        return false;
    }, &languageList, languageList.size()))
    {
        // Language filter changed
    }
    ImGui::PopItemWidth();
    
    ImGui::SameLine();
    if (ImGui::Button("Refresh Status"))
    {
        refreshVoiceStatuses();
    }
    
    ImGui::Separator();
    
    // Voice list and download controls side by side
    ImGui::BeginChild("VoiceList", ImVec2(ImGui::GetContentRegionAvail().x * 0.7f, 0), false);
    renderVoiceList();
    ImGui::EndChild();
    
    ImGui::SameLine();
    
    ImGui::BeginChild("Controls", ImVec2(0, 0), false);
    renderDownloadControls();
    ImGui::EndChild();
    
    ImGui::End();
}

void VoiceDownloadDialog::refreshVoiceStatuses()
{
    voiceStatuses = VoiceDownloadHelper::checkAllVoiceStatuses();
    
    // Update file sizes for installed voices
    voiceFileSizes.clear();
    juce::File modelsDir = VoiceDownloadHelper::resolveModelsBaseDir();
    juce::File piperVoicesDir = modelsDir.getChildFile("piper-voices");
    
    for (const auto& voice : availableVoices)
    {
        auto status = voiceStatuses[voice.name];
        if (status == VoiceDownloadHelper::VoiceStatus::Installed)
        {
            // Calculate file path (same logic as checkVoiceStatus)
            int lastDash = voice.name.lastIndexOfChar('-');
            if (lastDash >= 0)
            {
                juce::String beforeLastDash = voice.name.substring(0, lastDash);
                int secondLastDash = beforeLastDash.lastIndexOfChar('-');
                if (secondLastDash >= 0)
                {
                    juce::String locale = voice.name.substring(0, secondLastDash);
                    juce::String voiceName = voice.name.substring(secondLastDash + 1, lastDash);
                    juce::String quality = voice.name.substring(lastDash + 1);
                    juce::String lang = locale.substring(0, locale.indexOfChar('_'));
                    if (lang.isEmpty()) lang = locale;
                    
                    juce::File onnxFile = piperVoicesDir.getChildFile(lang)
                                             .getChildFile(locale)
                                             .getChildFile(voiceName)
                                             .getChildFile(quality)
                                             .getChildFile(voice.name + ".onnx");
                    
                    if (onnxFile.existsAsFile())
                    {
                        voiceFileSizes[voice.name] = onnxFile.getSize();
                    }
                }
            }
        }
    }
}

void VoiceDownloadDialog::renderVoiceList()
{
    auto filtered = getFilteredVoices();
    
    if (filtered.empty())
    {
        ImGui::Text("No voices match the current filter.");
        return;
    }
    
    // Table header
    if (ImGui::BeginTable("VoicesTable", 7, ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Borders))
    {
        ImGui::TableSetupColumn("Select", ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Language", ImGuiTableColumnFlags_WidthFixed, 120.0f);
        ImGui::TableSetupColumn("Gender", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("Quality", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 120.0f);
        ImGui::TableSetupScrollFreeze(1, 1);
        ImGui::TableHeadersRow();
        
        for (size_t i = 0; i < filtered.size(); ++i)
        {
            const auto& voice = filtered[i];
            
            // Find index in full availableVoices list
            size_t fullIndex = 0;
            for (size_t j = 0; j < availableVoices.size(); ++j)
            {
                if (availableVoices[j].name == voice.name)
                {
                    fullIndex = j;
                    break;
                }
            }
            
            ImGui::TableNextRow();
            
            // Select checkbox
            ImGui::TableSetColumnIndex(0);
            bool isSelected = voiceSelected[fullIndex];
            auto status = voiceStatuses[voice.name];
            bool isInstalled = (status == VoiceDownloadHelper::VoiceStatus::Installed);
            bool isError = (status == VoiceDownloadHelper::VoiceStatus::Error);
            bool isDownloading = downloadThread.isDownloading() && downloadThread.getCurrentVoice() == voice.name;
            
            // Allow selection if: not installed, has error (corrupted), or is partial
            bool canSelect = !isInstalled || isError || (status == VoiceDownloadHelper::VoiceStatus::Partial);
            
            if (!canSelect || isDownloading)
            {
                ImGui::BeginDisabled();
                ImGui::Checkbox(("##select" + juce::String((int)i)).toRawUTF8(), &isSelected);
                ImGui::EndDisabled();
            }
            else
            {
                ImGui::Checkbox(("##select" + juce::String((int)i)).toRawUTF8(), &isSelected);
                voiceSelected[fullIndex] = isSelected;
            }
            
            // Name
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%s", voice.name.toRawUTF8());
            
            // Language
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%s", voice.language.toRawUTF8());
            
            // Gender
            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%s", voice.gender.toRawUTF8());
            
            // Quality
            ImGui::TableSetColumnIndex(4);
            ImGui::Text("%s", voice.quality.toRawUTF8());
            
            // Size
            ImGui::TableSetColumnIndex(5);
            juce::String sizeStr = getFormattedFileSize(voice.name);
            ImGui::Text("%s", sizeStr.toRawUTF8());
            
            // Status (status already declared above)
            ImGui::TableSetColumnIndex(6);
            if (isDownloading)
            {
                ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Downloading...");
            }
            else if (status == VoiceDownloadHelper::VoiceStatus::Installed)
            {
                ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Installed");
            }
            else if (status == VoiceDownloadHelper::VoiceStatus::Partial)
            {
                ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "Partial");
            }
            else if (status == VoiceDownloadHelper::VoiceStatus::Error)
            {
                ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Error (Corrupted)");
                if (ImGui::IsItemHovered())
                {
                    ImGui::BeginTooltip();
                    ImGui::Text("This voice file is corrupted or incomplete.\nPlease re-download it.");
                    ImGui::EndTooltip();
                }
            }
            else if (voice.isIncluded)
            {
                ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "Included");
            }
            else
            {
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Not Installed");
            }
        }
        
        ImGui::EndTable();
    }
}

void VoiceDownloadDialog::renderDownloadControls()
{
    ImGui::Text("Download Controls");
    ImGui::Separator();
    
    // Get selected voices (allow re-downloading corrupted/error voices)
    std::vector<juce::String> selectedVoices;
    for (size_t i = 0; i < availableVoices.size(); ++i)
    {
        if (voiceSelected[i])
        {
            auto status = voiceStatuses[availableVoices[i].name];
            // Allow download if: not installed, has error (corrupted), or is partial
            if (status != VoiceDownloadHelper::VoiceStatus::Installed || 
                status == VoiceDownloadHelper::VoiceStatus::Error ||
                status == VoiceDownloadHelper::VoiceStatus::Partial)
            {
                selectedVoices.push_back(availableVoices[i].name);
            }
        }
    }
    
    // Download button
    if (downloadThread.isDownloading())
    {
        ImGui::BeginDisabled();
        ImGui::Button("Download Selected", ImVec2(-1, 0));
        ImGui::EndDisabled();
    }
    else
    {
        if (ImGui::Button("Download Selected", ImVec2(-1, 0)))
        {
            if (!selectedVoices.empty())
            {
                downloadThread.downloadBatch(selectedVoices);
            }
        }
    }
    
    // Cancel button
    if (downloadThread.isDownloading())
    {
        if (ImGui::Button("Cancel Download", ImVec2(-1, 0)))
        {
            downloadThread.cancelCurrentDownload();
        }
    }
    
    ImGui::Separator();
    
    // Progress display
    if (downloadThread.isDownloading())
    {
        ImGui::Text("Downloading: %s", downloadThread.getCurrentVoice().toRawUTF8());
        float progress = downloadThread.getProgress();
        if (progress >= 0.0f)
        {
            ImGui::ProgressBar(progress, ImVec2(-1, 0));
            ImGui::Text("%.0f%%", progress * 100.0f);
        }
        else
        {
            ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Error occurred");
        }
        
        juce::String statusMsg = downloadThread.getStatusMessage();
        if (statusMsg.contains("failed") || statusMsg.contains("error") || statusMsg.contains("Error") || 
            statusMsg.contains("corrupted") || statusMsg.contains("too small"))
        {
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%s", statusMsg.toRawUTF8());
        }
        else
        {
            ImGui::Text("%s", statusMsg.toRawUTF8());
        }
        
        // Show helpful tips on errors
        if (statusMsg.containsIgnoreCase("corrupted") || statusMsg.containsIgnoreCase("too small"))
        {
            ImGui::Spacing();
            ImGui::TextWrapped("The download appears to be corrupted. This can happen due to:");
            ImGui::BulletText("Network interruption");
            ImGui::BulletText("Server issues");
            ImGui::BulletText("Disk space problems");
            ImGui::TextWrapped("Please try downloading again. The corrupted file has been removed.");
        }
        else if (statusMsg.containsIgnoreCase("connection") || statusMsg.containsIgnoreCase("server"))
        {
            ImGui::Spacing();
            ImGui::TextWrapped("Connection problem detected. Please check your internet connection and try again.");
        }
    }
    else
    {
        ImGui::Text("Ready");
        if (!selectedVoices.empty())
        {
            ImGui::Text("%d voice(s) selected", (int)selectedVoices.size());
        }
    }
    
    ImGui::Separator();
    
    // Quick actions
    if (ImGui::Button("Select All Missing", ImVec2(-1, 0)))
    {
        for (size_t i = 0; i < availableVoices.size(); ++i)
        {
            auto status = voiceStatuses[availableVoices[i].name];
            voiceSelected[i] = (status != VoiceDownloadHelper::VoiceStatus::Installed);
        }
    }
    
    if (ImGui::Button("Deselect All", ImVec2(-1, 0)))
    {
        for (size_t i = 0; i < voiceSelected.size(); ++i)
        {
            voiceSelected[i] = false;
        }
    }
    
    ImGui::Separator();
    
    // Statistics
    int installedCount = 0;
    int missingCount = 0;
    for (const auto& [name, status] : voiceStatuses)
    {
        if (status == VoiceDownloadHelper::VoiceStatus::Installed)
            installedCount++;
        else if (status == VoiceDownloadHelper::VoiceStatus::NotInstalled)
            missingCount++;
    }
    
    ImGui::Text("Statistics:");
    ImGui::BulletText("Total voices: %d", (int)availableVoices.size());
    ImGui::BulletText("Installed: %d", installedCount);
    ImGui::BulletText("Missing: %d", missingCount);
}

std::vector<VoiceDownloadHelper::VoiceEntry> VoiceDownloadDialog::getFilteredVoices() const
{
    std::vector<VoiceDownloadHelper::VoiceEntry> filtered;
    juce::String search(searchFilter);
    search = search.toLowerCase();
    
    juce::String selectedLanguage = (selectedLanguageFilter > 0 && selectedLanguageFilter <= languageList.size()) 
                                   ? languageList[selectedLanguageFilter - 1] 
                                   : juce::String();
    
    for (const auto& voice : availableVoices)
    {
        // Search filter
        if (!search.isEmpty())
        {
            if (!voice.name.toLowerCase().contains(search) &&
                !voice.language.toLowerCase().contains(search) &&
                !voice.accent.toLowerCase().contains(search))
            {
                continue;
            }
        }
        
        // Language filter
        if (selectedLanguage.isNotEmpty() && voice.language != selectedLanguage)
        {
            continue;
        }
        
        filtered.push_back(voice);
    }
    
    return filtered;
}

void VoiceDownloadDialog::buildLanguageList()
{
    languageList.clear();
    languageList.add("All Languages");
    
    std::set<juce::String> uniqueLanguages;
    for (const auto& voice : availableVoices)
    {
        uniqueLanguages.insert(voice.language);
    }
    
    for (const auto& lang : uniqueLanguages)
    {
        languageList.add(lang);
    }
}

juce::String VoiceDownloadDialog::getFormattedFileSize(const juce::String& voiceName) const
{
    auto it = voiceFileSizes.find(voiceName);
    if (it != voiceFileSizes.end())
    {
        juce::int64 size = it->second;
        if (size >= 1024 * 1024 * 1024)  // GB
        {
            return juce::String::formatted("%.2f GB", size / (1024.0 * 1024.0 * 1024.0));
        }
        else if (size >= 1024 * 1024)  // MB
        {
            return juce::String::formatted("%.1f MB", size / (1024.0 * 1024.0));
        }
        else if (size >= 1024)  // KB
        {
            return juce::String::formatted("%.1f KB", size / 1024.0);
        }
        else
        {
            return juce::String::formatted("%d B", (int)size);
        }
    }
    return "-";
}


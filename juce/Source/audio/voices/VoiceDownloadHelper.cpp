#include "VoiceDownloadHelper.h"
#include "../modules/TTSPerformerModuleProcessor.h"
#include "../graph/ModularSynthProcessor.h"

std::vector<VoiceDownloadHelper::VoiceEntry> VoiceDownloadHelper::getAllAvailableVoices()
{
    // Call the static method directly - no instance needed
    return TTSPerformerModuleProcessor::getAllAvailableVoices();
}

juce::File VoiceDownloadHelper::resolveModelsBaseDir()
{
    // Create a temporary processor instance to access the path resolution method
    // This is safe because:
    // 1. Constructor is lightweight (mostly just parameter setup)
    // 2. We only call const methods that don't modify state
    // 3. No audio processing happens
    // 4. Instance is destroyed immediately after use
    auto tempProcessor = createTempProcessor();
    if (tempProcessor)
        return tempProcessor->resolveModelsBaseDir();
    
    // Fallback if processor creation fails
    auto exeDir = juce::File::getSpecialLocation(juce::File::currentExecutableFile).getParentDirectory();
    return exeDir.getChildFile("models");
}

VoiceDownloadHelper::VoiceStatus VoiceDownloadHelper::checkVoiceStatus(const juce::String& voiceName)
{
    auto tempProcessor = createTempProcessor();
    if (tempProcessor)
        return tempProcessor->checkVoiceStatus(voiceName);
    
    return VoiceStatus::Error;
}

std::map<juce::String, VoiceDownloadHelper::VoiceStatus> VoiceDownloadHelper::checkAllVoiceStatuses()
{
    auto tempProcessor = createTempProcessor();
    if (tempProcessor)
        return tempProcessor->checkAllVoiceStatuses();
    
    return std::map<juce::String, VoiceStatus>();
}

const VoiceDownloadHelper::VoiceEntry* VoiceDownloadHelper::getVoiceEntry(const juce::String& voiceName)
{
    auto voices = getAllAvailableVoices();
    for (const auto& voice : voices)
    {
        if (voice.name == voiceName)
            return &voice;
    }
    return nullptr;
}

std::unique_ptr<TTSPerformerModuleProcessor> VoiceDownloadHelper::createTempProcessor()
{
    try
    {
        // Create a temporary processor instance for path resolution and status checking.
        // The constructor initializes buses internally, so we don't need to pass anything.
        // Note: This will start a synthesis thread in the constructor, but it's lightweight
        // and the thread won't do anything without work queued. The destructor will clean it up.
        return std::make_unique<TTSPerformerModuleProcessor>();
    }
    catch (...)
    {
        // If creation fails, return nullptr
        // Callers should handle this gracefully
        juce::Logger::writeToLog("[VoiceDownloadHelper] Failed to create temporary processor instance");
        return nullptr;
    }
}


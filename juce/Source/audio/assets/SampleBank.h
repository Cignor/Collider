#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <unordered_map>
#include <memory>
#include <vector>

class SampleBank
{
public:
    struct Sample
    {
        juce::AudioBuffer<float> buffer; // mono mixdown
        juce::AudioBuffer<float> stereo; // stereo view (2ch)
        double sampleRate { 48000.0 };
    };

    SampleBank()
    {
        formatManager.registerBasicFormats();
    }

    void loadSamplesFromDirectory (const juce::File& rootDir);

    // Convenience: get any loaded sample (rough selection)
    Sample* getRandomSample()
    {
        if (! owned.empty())
            return owned.front().get();
        for (auto it = cache.begin(); it != cache.end(); ++it)
            if (auto sp = it->second.lock()) return sp.get();
        return nullptr;
    }

    // Shared version for processors that need lifetime safety
    std::shared_ptr<Sample> getRandomSharedSample()
    {
        if (! owned.empty())
        {
            auto& rng = juce::Random::getSystemRandom();
            const int idx = rng.nextInt ((int) owned.size());
            lastRandomFileName = findFileNameForSample (owned[(size_t) idx]);
            return owned[(size_t) idx];
        }
        std::vector<std::shared_ptr<Sample>> tmp;
        for (auto it = cache.begin(); it != cache.end(); ++it)
            if (auto sp = it->second.lock()) tmp.push_back (sp);
        if (! tmp.empty())
        {
            auto& rng = juce::Random::getSystemRandom();
            const int idx = rng.nextInt ((int) tmp.size());
            lastRandomFileName = findFileNameForSample (tmp[(size_t) idx]);
            return tmp[(size_t) idx];
        }
        return nullptr;
    }

    juce::String getLastRandomFileName() const { return lastRandomFileName; }

    std::shared_ptr<Sample> getOrLoad (const juce::File& file)
    {
        const auto key = file.getFullPathName().toStdString();
        if (auto it = cache.find (key); it != cache.end())
            if (auto sp = it->second.lock()) return sp;

        if (! file.existsAsFile())
            return nullptr;

        std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));
        if (! reader)
            return nullptr;

        auto sample = std::make_shared<Sample>();
        const int numSamples = (int) reader->lengthInSamples;
        juce::AudioBuffer<float> temp ((int) reader->numChannels, numSamples);
        reader->read (&temp, 0, numSamples, 0, true, true);

        // Mixdown to mono
        sample->buffer.setSize (1, numSamples);
        sample->buffer.clear();
        const int srcCh = (int) temp.getNumChannels();
        for (int ch = 0; ch < srcCh; ++ch)
            sample->buffer.addFrom (0, 0, temp, ch, 0, numSamples, 1.0f / (float) juce::jmax (1, srcCh));

        // Ensure a stereo buffer is available (copy or duplicate mono)
        sample->stereo.setSize (2, numSamples);
        if (srcCh >= 2)
        {
            sample->stereo.copyFrom (0, 0, temp, 0, 0, numSamples);
            sample->stereo.copyFrom (1, 0, temp, 1, 0, numSamples);
        }
        else
        {
            sample->stereo.copyFrom (0, 0, sample->buffer, 0, 0, numSamples);
            sample->stereo.copyFrom (1, 0, sample->buffer, 0, 0, numSamples);
        }

        sample->sampleRate = reader->sampleRate;

        cache[key] = sample;
        return sample;
    }
    
    std::shared_ptr<Sample> generateSineWaveFailsafe (double sampleRate, double durationSeconds)
    {
        auto sample = std::make_shared<Sample>();
        const int numSamples = (int)(sampleRate * durationSeconds);
        const float frequency = 440.0f; // A4 note
        
        // Generate mono sine wave
        sample->buffer.setSize(1, numSamples);
        for (int i = 0; i < numSamples; ++i)
        {
            float t = (float)i / (float)sampleRate;
            sample->buffer.setSample(0, i, std::sin(2.0f * juce::MathConstants<float>::pi * frequency * t) * 0.5f);
        }
        
        // Create stereo version (duplicate mono to both channels)
        sample->stereo.setSize(2, numSamples);
        sample->stereo.copyFrom(0, 0, sample->buffer, 0, 0, numSamples);
        sample->stereo.copyFrom(1, 0, sample->buffer, 0, 0, numSamples);
        
        sample->sampleRate = sampleRate;
        return sample;
    }

private:
    juce::AudioFormatManager formatManager;
    std::unordered_map<std::string, std::weak_ptr<Sample>> cache;
    std::vector<std::shared_ptr<Sample>> owned;
    juce::String lastRandomFileName;

    juce::String findFileNameForSample (const std::shared_ptr<Sample>& sample) const
    {
        for (const auto& [path, weak] : cache)
            if (auto sp = weak.lock())
                if (sp.get() == sample.get())
                    return juce::File (juce::String (path)).getFileName();
        return {};
    }
};



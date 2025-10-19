#pragma once

#include <juce_core/juce_core.h>
#include <vector>

struct PhonemeTiming
{
	juce::String phoneme;
	double startTimeSeconds{};
	double endTimeSeconds{};
	double durationSeconds{};

	PhonemeTiming() = default;
	PhonemeTiming(const juce::String& p, double start, double end)
		: phoneme(p), startTimeSeconds(start), endTimeSeconds(end), durationSeconds(end - start) {}
};

struct WordTiming
{
	juce::String word;
	double startTimeSeconds{};
	double endTimeSeconds{};
	double durationSeconds{};
	std::vector<PhonemeTiming> phonemes;

	WordTiming() = default;
	WordTiming(const juce::String& w, double start, double end)
		: word(w), startTimeSeconds(start), endTimeSeconds(end), durationSeconds(end - start) {}
};

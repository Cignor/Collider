#pragma once
#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include <vector>

class PresetValidator
{
public:
	struct Issue {
		enum Severity { Warning, Error };
		Severity severity;
		juce::String message;
	};

	std::vector<Issue> validate(const juce::ValueTree& preset);
};



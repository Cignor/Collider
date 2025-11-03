#pragma once
#include <juce_data_structures/juce_data_structures.h>
#include <vector>

class PresetAutoHealer
{
public:
	PresetAutoHealer() = default;

	// Attempts to heal module names based on naming convention, deriving valid names from PinDatabase
	std::vector<juce::String> heal(juce::ValueTree& presetToHeal);
};



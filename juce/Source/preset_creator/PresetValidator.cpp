#include "PresetValidator.h"
#include "PinDatabase.h" // We need this for validation

std::vector<PresetValidator::Issue> PresetValidator::validate(const juce::ValueTree& preset)
{
	std::vector<Issue> issues;
	auto modulesVT = preset.getChildWithName("modules");
	auto connsVT = preset.getChildWithName("connections");
	
	if (!modulesVT.isValid()) {
		issues.push_back({Issue::Error, "Preset is missing <modules> block."});
		return issues;
	}

	auto& pinDb = getModulePinDatabase();
	std::map<juce::uint32, juce::String> logicalIdToType;

	// Rule 1: Check if all module types exist
	for (const auto& moduleNode : modulesVT)
	{
		if (!moduleNode.hasType("module")) continue;
		
		juce::String type = moduleNode.getProperty("type").toString();
		juce::uint32 logicalId = (juce::uint32)(int)moduleNode.getProperty("logicalId", 0);
		logicalIdToType[logicalId] = type;
		
		if (pinDb.find(type.toLowerCase()) == pinDb.end())
		{
			issues.push_back({Issue::Error, "Unknown module type: '" + type + "'"});
		}
	}
	
	// Rule 2: Check if connection channels are valid
	if (connsVT.isValid())
	{
		for (const auto& connNode : connsVT)
		{
			if (!connNode.hasType("connection")) continue;
			
			juce::uint32 srcId = (juce::uint32)(int)connNode.getProperty("srcId", 0);
			int srcChan = (int)connNode.getProperty("srcChan", 0);
			
			if (logicalIdToType.count(srcId))
			{
				juce::String srcType = logicalIdToType.at(srcId);
				if (pinDb.count(srcType.toLowerCase()))
				{
					const auto& outputs = pinDb.at(srcType.toLowerCase()).audioOuts;
                        bool isValid = std::any_of(outputs.begin(), outputs.end(),
                            [srcChan](const AudioPin& pin) { return pin.channel == srcChan; });
					
					if (!isValid)
					{
						issues.push_back({Issue::Warning, "Source channel " + juce::String(srcChan) + " is invalid for module '" + srcType + "'"});
					}
				}
			}
		}
	}
	
	return issues;
}



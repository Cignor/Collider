#include "PresetAutoHealer.h"
#include "PinDatabase.h"

std::vector<juce::String> PresetAutoHealer::heal(juce::ValueTree& presetToHeal)
{
	std::vector<juce::String> healingMessages;
	auto modulesVT = presetToHeal.getChildWithName("modules");
	if (!modulesVT.isValid()) return healingMessages;

    // Gather valid names from PinDatabase
    std::set<juce::String> validModuleNames;
    std::unordered_map<juce::String, juce::String> collapsedToCanonical; // e.g., "trackmixer" -> "track_mixer"
    for (const auto& pair : getModulePinDatabase())
    {
        validModuleNames.insert(pair.first);
        juce::String collapsed;
        for (int i = 0; i < pair.first.length(); ++i)
        {
            auto ch = pair.first[i];
            if (ch != '_' && ch != ' ')
                collapsed += ch;
        }
        collapsedToCanonical[collapsed] = pair.first;
    }

	for (auto moduleNode : modulesVT)
	{
		if (!moduleNode.hasType("module")) continue;
		juce::String currentType = moduleNode.getProperty("type").toString();

		// Already valid
		if (validModuleNames.count(currentType) > 0)
			continue;

		// Rule A: lowercase and replace spaces with underscores
		juce::String normalized = currentType.toLowerCase().replaceCharacter(' ', '_');

		// Rule B: insert underscores before camelCase/PascalCase transitions and lowercase
		juce::String caseFixed;
		caseFixed.preallocateBytes((int)currentType.length() * 2);
		for (int i = 0; i < currentType.length(); ++i)
		{
			juce::juce_wchar c = currentType[i];
			bool prevIsLower = (i > 0) && juce::CharacterFunctions::isLowerCase(currentType[i - 1]);
			bool isUpper = juce::CharacterFunctions::isUpperCase(c);
			if (i > 0 && isUpper && prevIsLower)
				caseFixed += '_';
			caseFixed += juce::CharacterFunctions::toLowerCase(c);
		}
		caseFixed = caseFixed.replace(" ", "_");

        // Try fixes in order
		if (validModuleNames.count(normalized) > 0)
		{
			moduleNode.setProperty("type", normalized, nullptr);
			healingMessages.push_back("Healed: Renamed '" + currentType + "' to '" + normalized + "'.");
		}
		else if (validModuleNames.count(caseFixed) > 0)
		{
			moduleNode.setProperty("type", caseFixed, nullptr);
			healingMessages.push_back("Healed: Renamed '" + currentType + "' to '" + caseFixed + "'.");
		}
        else
        {
            // Rule C: collapse candidate and look up canonical name by collapsed form
            juce::String collapsedCurrent;
            for (int i = 0; i < currentType.length(); ++i)
            {
                auto ch = currentType[i];
                if (ch != '_' && ch != ' ')
                    collapsedCurrent += juce::CharacterFunctions::toLowerCase(ch);
            }
            auto it = collapsedToCanonical.find(collapsedCurrent);
            if (it != collapsedToCanonical.end())
            {
                const juce::String& canonical = it->second;
                moduleNode.setProperty("type", canonical, nullptr);
                healingMessages.push_back("Healed: Renamed '" + currentType + "' to '" + canonical + "'.");
            }
        }
	}

	return healingMessages;
}



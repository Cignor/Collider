#pragma once

#include "../audio/modules/ModuleProcessor.h"
#include <map>

// Getter functions that return references to static local variables
// This ensures safe initialization after JUCE is ready (construct on first use idiom)

// Returns the pin database for all module types
inline std::map<juce::String, ModulePinInfo>& getModulePinDatabase()
{
    // By declaring the map as static inside a function, we ensure it's
    // initialized safely on its first use, after JUCE is ready.
    static std::map<juce::String, ModulePinInfo> modulePinDatabase;
    return modulePinDatabase;
}

// Returns the module descriptions database
inline std::map<juce::String, const char*>& getModuleDescriptions()
{
    static std::map<juce::String, const char*> moduleDescriptions;
    return moduleDescriptions;
}

// Function to populate both databases - must be called before first use
void populatePinDatabase();


#include "ModuleProcessor.h"
#include "../graph/ModularSynthProcessor.h"
#include <unordered_set>


bool ModuleProcessor::getParamRouting(const juce::String& /*paramId*/, int& /*outBusIndex*/, int& /*outChannelIndexInBus*/) const
{
    return false;
}

bool ModuleProcessor::isParamInputConnected(const juce::String& paramId) const
{
    auto* synth = parentSynth;
    if (synth == nullptr)
        return false;

#if JUCE_DEBUG
    if (synth->isGraphMutationPending())
    {
        juce::Logger::writeToLog("[Graph][WARN] isParamInputConnected invoked during graph mutation for param '"
                                 + paramId + "' on module LID " + juce::String((int)storedLogicalId));
    }
#endif

    juce::uint32 myLogicalId = storedLogicalId;
    if (myLogicalId == 0)
    {
        for (const auto& info : synth->getModulesInfo())
        {
            if (synth->getModuleForLogical (info.first) == this)
            {
                myLogicalId = info.first;
                break;
            }
        }
        if (myLogicalId == 0)
            return false;
    }

    int busIndex = -1;
    int chanInBus = -1;
    if (!getParamRouting(paramId, busIndex, chanInBus))
        return false;

    int absoluteChannel = chanInBus;
    if (busIndex > 0)
    {
        int sum = 0;
        const int numInputBuses = getBusCount(true);
        for (int b = 0; b < numInputBuses && b < busIndex; ++b)
            sum += getChannelCountOfBus(true, b);
        absoluteChannel = sum + chanInBus;
    }

    auto connectionsSnapshot = synth->getConnectionSnapshot();
    if (! connectionsSnapshot)
        return false;

    for (const auto& c : *connectionsSnapshot)
        if (c.dstLogicalId == myLogicalId && c.dstChan == absoluteChannel)
            return true;

    // Fallback: if stored logicalId yields no match, re-resolve by pointer and retry once.
    {
        juce::uint32 ptrResolvedId = 0;
        for (const auto& info : synth->getModulesInfo())
        {
            if (synth->getModuleForLogical(info.first) == this)
            {
                ptrResolvedId = info.first;
                break;
            }
        }
        if (ptrResolvedId != 0 && ptrResolvedId != myLogicalId)
        {
            for (const auto& c : *connectionsSnapshot)
            {
                if (c.dstLogicalId == ptrResolvedId && c.dstChan == absoluteChannel)
                {
                    const_cast<ModuleProcessor*>(this)->setLogicalId(ptrResolvedId);
                    return true;
                }
            }
        }
    }

    return false;
}

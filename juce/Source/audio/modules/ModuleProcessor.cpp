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

    // Prefer stored logical ID; fall back to pointer-based lookup if not set
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

    // Convert bus+channelInBus to absolute input-channel index by summing
    int absoluteChannel = chanInBus;
    if (busIndex > 0)
    {
        int sum = 0;
        const int numInputBuses = getBusCount(true);
        for (int b = 0; b < numInputBuses && b < busIndex; ++b)
            sum += getChannelCountOfBus(true, b);
        absoluteChannel = sum + chanInBus;
    }

    for (const auto& c : synth->getConnectionsInfo())
        if (c.dstLogicalId == myLogicalId && c.dstChan == absoluteChannel)
            return true;

    // Fallback: if stored logicalId yields no match, re-resolve by pointer and retry once.
    // This self-heals cases where a module instance was swapped and not re-assigned.
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
            // Retry with pointer-resolved ID and update stored ID to keep future checks consistent
            for (const auto& c : synth->getConnectionsInfo())
            {
                if (c.dstLogicalId == ptrResolvedId && c.dstChan == absoluteChannel)
                {
                    const juce::String msg = "[ModuleProcessor][isParamInputConnected] Rebound logicalId from " + juce::String((int)myLogicalId) +
                                              " to " + juce::String((int)ptrResolvedId) + " for paramId='" + paramId + "'";
                    DBG(msg); juce::Logger::writeToLog(msg);
                    const_cast<ModuleProcessor*>(this)->setLogicalId(ptrResolvedId);
                    return true;
                }
            }
        }
    }

    // Diagnostics once per (paramId@logicalId)
    {
        static std::unordered_set<std::string> onceKeys;
        const std::string key = (paramId + "@" + juce::String(myLogicalId)).toStdString();
        if (onceKeys.insert(key).second)
        {
            const juce::String msg = "[ModuleProcessor][isParamInputConnected] NO CONNECTION for paramId='" + paramId +
                "' absChan=" + juce::String(absoluteChannel) + " busIndex=" + juce::String(busIndex) +
                " chanInBus=" + juce::String(chanInBus) + " myLogicalId=" + juce::String((int) myLogicalId);
            DBG(msg);
            juce::Logger::writeToLog(msg);
            juce::String chans; int count = 0;
            for (const auto& c : synth->getConnectionsInfo())
                if (c.dstLogicalId == myLogicalId) { chans += juce::String(c.dstChan) + ", "; ++count; }
            const juce::String msg2 = "[ModuleProcessor][isParamInputConnected] Connections to this module: count=" + juce::String(count) +
                " chans=[" + chans + "]";
            DBG(msg2);
            juce::Logger::writeToLog(msg2);
        }
    }
    return false;
}

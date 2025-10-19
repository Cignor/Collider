#include <JuceHeader.h>
#include "../audio/modules/ModuleProcessor.h"
#include "../audio/modules/VCOModuleProcessor.h"
#include "../audio/modules/LFOModuleProcessor.h"
#include "../audio/graph/ModularSynthProcessor.h"

/**
    Unit tests for the ModuleProcessor base class and derived modules.
    
    This test suite validates the core functionality of the new bus-based
    modulation system, including parameter routing and connection detection.
*/
class ModuleProcessorTests : public juce::UnitTest
{
public:
    ModuleProcessorTests() : UnitTest("ModuleProcessor Tests", "Audio") {}
    
    void runTest() override
    {
        runTestParameterRouting();
        runTestConnectionDetection();
        runTestBusConfiguration();
        runTestParameterMapping();
    }

private:
    void runTestParameterRouting()
    {
        beginTest("Parameter Routing");
        
        // Test VCO parameter routing
        VCOModuleProcessor vco;
        int busIndex, channelInBus;
        
        expect(vco.getParamRouting("frequency", busIndex, channelInBus));
        expectEquals(busIndex, 1);
        expectEquals(channelInBus, 0);
        
        expect(vco.getParamRouting("waveform", busIndex, channelInBus));
        expectEquals(busIndex, 2);
        expectEquals(channelInBus, 0);
        
        // Test invalid parameter
        expect(!vco.getParamRouting("invalid_param", busIndex, channelInBus));
    }
    
    void runTestConnectionDetection()
    {
        beginTest("Connection Detection");
        
        // Create a minimal test environment
        auto synth = std::make_unique<ModularSynthProcessor>();
        
        // Add test modules
        auto vcoNodeId = synth->addModule("VCO");
        auto lfoNodeId = synth->addModule("LFO");
        
        // Get module processors
        auto vco = synth->getModuleForLogical(vcoNodeId);
        auto lfo = synth->getModuleForLogical(lfoNodeId);
        
        expect(vco != nullptr);
        expect(lfo != nullptr);
        
        // Initially no connections
        expect(!vco->isParamInputConnected("frequency"));
        
        // Connect LFO to VCO frequency
        synth->connect(lfoNodeId, 0, vcoNodeId, 1);
        
        // Now frequency should be connected
        expect(vco->isParamInputConnected("frequency"));
        expect(!vco->isParamInputConnected("waveform"));
    }
    
    void runTestBusConfiguration()
    {
        beginTest("Bus Configuration");
        
        VCOModuleProcessor vco;
        LFOModuleProcessor lfo;
        
        // Verify bus counts
        expectEquals(vco.getBusCount(true), 3);   // Audio + 2 modulation buses
        expectEquals(vco.getBusCount(false), 1);  // 1 output bus
        
        expectEquals(lfo.getBusCount(true), 4);   // Audio + 3 modulation buses
        expectEquals(lfo.getBusCount(false), 1);  // 1 output bus
        
        // Verify channel counts per bus
        expectEquals(vco.getChannelCountOfBus(true, 0), 1);   // Audio input
        expectEquals(vco.getChannelCountOfBus(true, 1), 1);   // Frequency mod
        expectEquals(vco.getChannelCountOfBus(true, 2), 1);   // Waveform mod
    }
    
    void runTestParameterMapping()
    {
        beginTest("Parameter Mapping");
        
        // Test that parameter IDs match between routing and actual parameters
        VCOModuleProcessor vco;
        auto params = vco.getParameters();
        
        // Find frequency parameter
        bool foundFrequency = false;
        for (auto* param : params)
        {
            if (auto* paramWithId = dynamic_cast<juce::AudioProcessorParameterWithID*>(param))
            {
                if (paramWithId->paramID == "frequency")
                {
                    foundFrequency = true;
                    break;
                }
            }
        }
        expect(foundFrequency);
        
        // Test parameter routing consistency
        int busIndex, channelInBus;
        expect(vco.getParamRouting("frequency", busIndex, channelInBus));
        
        // Verify the bus exists
        expect(busIndex < vco.getBusCount(true));
        expect(channelInBus < vco.getChannelCountOfBus(true, busIndex));
    }
};

// Register the test
static ModuleProcessorTests moduleProcessorTests;

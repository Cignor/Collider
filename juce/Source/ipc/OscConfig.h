#pragma once

namespace OscConfig
{
    // Host/port for Python OSC receiver (client from JUCE to Python)
    inline constexpr const char* kPythonHost = "127.0.0.1";
    inline constexpr int kPythonPort = 9002;

    // Port for JUCE OSC server (Python -> JUCE)
    inline constexpr int kJuceServerPort = 9001;
}



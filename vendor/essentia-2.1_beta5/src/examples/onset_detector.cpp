/*
 * Copyright (C) 2006-2016  Music Technology Group - Universitat Pompeu Fabra
 *
 * This file is part of Essentia
 *
 * Essentia is free software: you can redistribute it and/or modify it under
 * the terms of the GNU Affero General Public License as published by the Free
 * Software Foundation (FSF), either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the Affero GNU General Public License
 * version 3 along with this program.  If not, see http://www.gnu.org/licenses/
 */

/*detects onsets and logs them.
 by Piotr Holonowicz, MTG, UPF
 Modified to use writetolog
*/
#include <iostream>
#include <fstream>
#include <sstream>
#include "algorithmfactory.h"
#include "credit_libav.h"

using namespace std;
using namespace essentia;
using namespace standard;

// Helper function to simulate logging
void writetolog(const std::string& message)
{
    std::cout << "[LOG] " << message << std::endl;
    // If on Windows and debugging, output to debug string as well
#ifdef _WIN32
#include <windows.h>
    OutputDebugStringA(("[LOG] " + message + "\n").c_str());
#endif
}

int log_onsets(const vector<Real>& onsets)
{
    writetolog("--- Onset Detection Results ---");
    std::stringstream ss;
    ss << "Total Onsets Found: " << onsets.size();
    writetolog(ss.str());

    for (size_t i = 0; i < onsets.size(); ++i)
    {
        std::stringstream valSS;
        valSS << "Onset " << i << ": " << onsets[i] << " s";
        writetolog(valSS.str());
    }
    writetolog("-------------------------------");
    return EXIT_SUCCESS;
}

int main(int argc, char* argv[])
{
    writetolog("Essentia onset detector (weighted Complex and HFC detection functions)");

    if (argc != 2)
    {
        writetolog("Error: wrong number of arguments");
        writetolog("Usage: " + std::string(argv[0]) + " input_audiofile");
        // creditLibAV(); // Commented out to avoid linker error if missing
        return 1;
    }

    essentia::init();

    Real         onsetRate;
    vector<Real> onsets;

    vector<Real> audio;
    std::string  fileName = argv[1];

    writetolog("Processing file: " + fileName);

    try
    {
        // File Input
        Algorithm* audiofile =
            AlgorithmFactory::create("MonoLoader", "filename", argv[1], "sampleRate", 44100);

        Algorithm* extractoronsetrate = AlgorithmFactory::create("OnsetRate");

        audiofile->output("audio").set(audio);

        extractoronsetrate->input("signal").set(audio);
        extractoronsetrate->output("onsets").set(onsets);
        extractoronsetrate->output("onsetRate").set(onsetRate);

        audiofile->compute();
        extractoronsetrate->compute();

        std::stringstream rateSS;
        rateSS << "onsetRate: " << onsetRate;
        writetolog(rateSS.str());

        log_onsets(onsets);

        delete extractoronsetrate;
        delete audiofile;
    }
    catch (const std::exception& e)
    {
        writetolog("Exception occurred: " + std::string(e.what()));
    }

    essentia::shutdown();

    return 0;
}

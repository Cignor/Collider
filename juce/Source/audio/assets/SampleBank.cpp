#include "SampleBank.h"

void SampleBank::loadSamplesFromDirectory (const juce::File& rootDir)
{
    if (! rootDir.isDirectory())
        return;

    juce::Array<juce::File> files;
    rootDir.findChildFiles (files, juce::File::findFiles, true);

    juce::StringArray loaded;
    for (auto& f : files)
    {
        const juce::String ext = f.getFileExtension().toLowerCase();
        if (ext != ".wav" && ext != ".aiff" && ext != ".aif" && ext != ".flac" && ext != ".mp3")
            continue;
        auto s = getOrLoad (f);
        if (s)
        {
            // Retain a strong ref so getRandomSample() can always return something
            bool pushed = false;
            for (auto& o : owned) { if (o.get() == s.get()) { pushed = true; break; } }
            if (! pushed) owned.push_back (s);
            loaded.add (f.getFileName());
            const int ch = s->stereo.getNumChannels();
            const int ns = s->stereo.getNumSamples();
            float peak = 0.0f;
            if (ch > 0 && ns > 0)
                peak = s->stereo.getMagnitude (0, 0, ns);
            juce::Logger::writeToLog ("[SampleBank] Loaded '" + f.getFileName() + "' ch=" + juce::String (ch) + " samples=" + juce::String (ns) + " peak=" + juce::String (peak));
        }
    }

    if (loaded.isEmpty())
    {
        DBG ("[SampleBank] No samples loaded from: " + rootDir.getFullPathName());
    }
    else
    {
        loaded.sort (true);
        DBG ("[SampleBank] Loaded samples (" + juce::String (loaded.size()) + ") from " + rootDir.getFullPathName() + ":\n  " + loaded.joinIntoString (", "));
    }
}



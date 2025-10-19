#include "TestHarnessComponent.h"
#include "MainComponent.h"
#include "../audio/voices/NoiseVoiceProcessor.h"
#include <juce_dsp/juce_dsp.h>
#include "../audio/voices/SynthVoiceProcessor.h"
#include "../audio/voices/SampleVoiceProcessor.h"
#include "../audio/utils/VoiceDeletionUtils.h"

static juce::TextEditor* gLoggerEditor = nullptr;

void OnScreenLogger::attach (juce::TextEditor* editor)
{
    gLoggerEditor = editor;
}

void OnScreenLogger::log (const juce::String& msg)
{
    if (gLoggerEditor != nullptr)
    {
        gLoggerEditor->moveCaretToEnd();
        gLoggerEditor->insertTextAtCaret (msg + "\n");
    }
    juce::Logger::writeToLog (msg);
}

TestHarnessComponent::TestHarnessComponent()
{
    OnScreenLogger::attach (&logView);
    OnScreenLogger::log ("Harness: Constructor starting...");

    setAudioChannels (0, 2);
    if (auto* dev = deviceManager.getCurrentAudioDevice())
    {
        OnScreenLogger::log (" -> Device open SUCCESS: " + dev->getName()
            + " | SR: " + juce::String (dev->getCurrentSampleRate())
            + " | BS: " + juce::String (dev->getCurrentBufferSizeSamples()));
    }
    else
    {
        OnScreenLogger::log (" -> FATAL ERROR: Audio device failed to open. Pointer is null.");
    }

    addAndMakeVisible (btnCreateSynth);
    addAndMakeVisible (btnCreateSample);
    addAndMakeVisible (btnCreateNoise);
    addAndMakeVisible (btnDestroy);
    addAndMakeVisible (btnDestroyRandom);
    addAndMakeVisible (btnChaos);
    addAndMakeVisible (sliderGain);
    addAndMakeVisible (sliderPan);
    addAndMakeVisible (lblGain);
    addAndMakeVisible (lblPan);
    addAndMakeVisible (lblStatus);
    addAndMakeVisible (lblDevice);
    addAndMakeVisible (lblVoices);
    addAndMakeVisible (lblPeak);
    addAndMakeVisible (logView);

    btnCreateSynth.addListener (this);
    btnCreateSample.addListener (this);
    btnCreateNoise.addListener (this);
    btnDestroy.addListener (this);
    btnDestroyRandom.addListener (this);

    sliderGain.setRange (0.0, 1.0, 0.001);
    sliderGain.setSkewFactor (0.7);
    sliderGain.setValue (0.7);
    sliderGain.addListener (this);

    sliderPan.setRange (-1.0, 1.0, 0.001);
    sliderPan.setValue (0.0);
    sliderPan.addListener (this);

    lblStatus.setText ("Controlling Voice ID: -", juce::dontSendNotification);
    logView.setMultiLine (true); logView.setReadOnly (true); logView.setScrollbarsShown (true); logView.setCaretVisible (false);
    logView.setText ("[Harness] Ready\n");
    startTimerHz (15);

    // Load samples recursively from the provided absolute path
    juce::File root ("H:/0000_CODE/01_collider_pyo/audio/samples");
    sampleBank.loadSamplesFromDirectory (root);

    OnScreenLogger::log ("Harness: Constructor finished.");
}

TestHarnessComponent::~TestHarnessComponent()
{
    shutdownAudio();
}

void TestHarnessComponent::prepareToPlay (int samplesPerBlockExpected, double sampleRate)
{
    juce::ignoreUnused (samplesPerBlockExpected, sampleRate);
    OnScreenLogger::log ("Harness: prepareToPlay called.");
    harnessStartMs = juce::Time::getMillisecondCounterHiRes();
}

void TestHarnessComponent::getNextAudioBlock (const juce::AudioSourceChannelInfo& bufferToFill)
{
    // 1. Clear the main output buffer to start fresh.
    bufferToFill.clearActiveBufferRegion();

    if (bufferToFill.buffer == nullptr) return;

    // 2. Ensure our temporary mixing buffer is the correct size.
    auto numSamples = bufferToFill.numSamples;
    auto numChannels = bufferToFill.buffer->getNumChannels();
    tempMixBuffer.setSize (numChannels, numSamples, false, true, true);

    // 3. Loop through all active voices in our array.
    {
        const juce::SpinLock::ScopedLockType guard (voicesLock);
        for (auto* voice : activeVoices)
        {
            if (voice != nullptr)
            {
                tempMixBuffer.clear();
                juce::MidiBuffer emptyMidi;
                voice->processBlock (tempMixBuffer, emptyMidi);

                for (int channel = 0; channel < numChannels; ++channel)
                {
                    bufferToFill.buffer->addFrom (channel,
                                                 bufferToFill.startSample,
                                                 tempMixBuffer,
                                                 channel,
                                                 0,
                                                 numSamples);
                }
            }
        }
    }
}

void TestHarnessComponent::releaseResources()
{
    // Release resources for all voices
    for (auto* v : activeVoices) if (v) v->releaseResources();
}

void TestHarnessComponent::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);
    g.setColour (juce::Colours::white);
    g.setFont (16.0f);
    g.drawFittedText ("Collider Audio Test Harness", getLocalBounds(), juce::Justification::centredTop, 1);
}

void TestHarnessComponent::resized()
{
    int x = 10, y = 40, w = juce::jmin (240, getWidth() - 20), h = 28, gap = 8;
    btnCreateSynth.setBounds (x, y, w, h); y += h + gap;
    btnCreateSample.setBounds (x, y, w, h); y += h + gap;
    btnCreateNoise.setBounds (x, y, w, h); y += h + gap;
    btnDestroy.setBounds (x, y, w, h); y += h + gap;
    btnDestroyRandom.setBounds (x, y, w, h); y += h + gap * 2;

    lblGain.setBounds (x, y, 50, h); sliderGain.setBounds (x + 60, y, w, h); y += h + gap;
    lblPan.setBounds (x, y, 50, h); sliderPan.setBounds (x + 60, y, w, h); y += h + gap;
    btnChaos.setBounds (x, y, w, h); y += h + gap;
    lblStatus.setBounds (x, y, w, h); y += h + gap;
    lblDevice.setBounds (x, y, w, h); y += h + gap;
    lblVoices.setBounds (x, y, w, h); y += h + gap;
    lblPeak.setBounds (x, y, w, h); y += h + gap;
    logView.setBounds (x + w + 20, 40, getWidth() - (x + w + 30), getHeight() - 50);
}

void TestHarnessComponent::buttonClicked (juce::Button* b)
{
    double sampleRate = deviceManager.getCurrentAudioDevice() ? deviceManager.getCurrentAudioDevice()->getCurrentSampleRate() : 48000.0;
    int blockSize = deviceManager.getCurrentAudioDevice() ? deviceManager.getCurrentAudioDevice()->getCurrentBufferSizeSamples() : 512;
    juce::uint64 newId = juce::Time::getMillisecondCounterHiRes();

    VoiceProcessor* newVoice = nullptr;

    if (b == &btnCreateSynth)
    {
        OnScreenLogger::log("[UI] Creating Synth voice...");
        newVoice = activeVoices.add(new SynthVoiceProcessor());
    }
    else if (b == &btnCreateNoise)
    {
        OnScreenLogger::log("[UI] Creating Noise voice...");
        newVoice = activeVoices.add(new NoiseVoiceProcessor());
    }
    else if (b == &btnCreateSample)
    {
        OnScreenLogger::log("[UI] Creating Sample voice...");
        if (auto sharedSample = sampleBank.getRandomSharedSample()) // Use the method that returns a shared_ptr
        {
            auto* vp = new SampleVoiceProcessor(sharedSample);
            vp->setLooping(true);
            newVoice = activeVoices.add(vp);
            OnScreenLogger::log(" -> Using random sample (length): " + juce::String (sharedSample->stereo.getNumSamples()));
        }
        else
        {
            OnScreenLogger::log("[UI][WARN] No samples found in bank! Cannot create sample voice.");
        }
    }
    else if (b == &btnDestroy)
    {
        if (lastControlledVoice != nullptr)
        {
            OnScreenLogger::log("[UI] Destroying voice ID: " + juce::String((juce::int64)lastControlledVoice->uniqueId));
            {
                const juce::SpinLock::ScopedLockType guard (voicesLock);
                activeVoices.removeObject(lastControlledVoice, true);
                if (activeVoices.isEmpty())
                    lastControlledVoice = nullptr;
                else
                    lastControlledVoice = activeVoices.getLast();
            }
        }
        else
        {
            OnScreenLogger::log("[UI] No voice to destroy.");
        }
    }
    else if (b == &btnDestroyRandom)
    {
        if (!activeVoices.isEmpty())
        {
            auto& rng = juce::Random::getSystemRandom();
            {
                const juce::SpinLock::ScopedLockType guard (voicesLock);
                const int index = rng.nextInt (activeVoices.size());
                if (auto* v = activeVoices[index])
            {
                OnScreenLogger::log("[UI] Destroying RANDOM voice ID: " + juce::String((juce::int64)v->uniqueId) + " at index " + juce::String(index));
                const bool isLastControlled = (v == lastControlledVoice);
                activeVoices.remove (index, true);
                if (isLastControlled)
                {
                    if (activeVoices.isEmpty())
                        lastControlledVoice = nullptr;
                    else
                        lastControlledVoice = activeVoices.getLast();
                }
            }
            }
        }
        else
        {
            OnScreenLogger::log("[UI] No voices to destroy at random.");
        }
    }

    if (newVoice != nullptr)
    {
        newVoice->uniqueId = newId;
        newVoice->prepareToPlay(sampleRate, blockSize);
        lastControlledVoice = newVoice;
    }

    lblStatus.setText("Controlling Voice ID: " + (lastControlledVoice ? juce::String((juce::int64)lastControlledVoice->uniqueId) : "-"), juce::dontSendNotification);
    lblVoices.setText("Voices: " + juce::String(activeVoices.size()), juce::dontSendNotification);
}

void TestHarnessComponent::sliderValueChanged (juce::Slider* s)
{
    if (s == &sliderGain)
    {
        if (lastControlledVoice)
        {
            if (auto* p = lastControlledVoice->getAPVTS().getParameter ("gain"))
                p->setValueNotifyingHost ((float) sliderGain.getValue());
        }
    }
    else if (s == &sliderPan)
    {
        if (lastControlledVoice)
        {
            if (auto* p = lastControlledVoice->getAPVTS().getParameter ("pan"))
                p->setValueNotifyingHost ((float) sliderPan.getValue());
        }
    }
}

void TestHarnessComponent::timerCallback()
{
    // Minimal device info for purity test
    if (auto* dev = deviceManager.getCurrentAudioDevice())
    {
        juce::String devStr = "SR=" + juce::String (dev->getCurrentSampleRate()) + "  BS=" + juce::String (dev->getCurrentBufferSizeSamples());
        lblDevice.setText (devStr, juce::dontSendNotification);
    }

    // Drive a 6s looped sweep 30Hz -> 600Hz for all synth voices
    const double nowMs = juce::Time::getMillisecondCounterHiRes();
    const double elapsed = (nowMs - harnessStartMs) * 0.001; // seconds
    const double period = 6.0; // seconds
    const double t = std::fmod (elapsed, period) / period; // 0..1
    const float freq = (float) (30.0 + t * (600.0 - 30.0));
    {
        const juce::SpinLock::ScopedLockType guard (voicesLock);
        for (auto* v : activeVoices)
        {
            if (v != nullptr)
            {
                if (auto* p = v->getAPVTS().getParameter ("frequency"))
                {
                    auto* asFloat = dynamic_cast<juce::RangedAudioParameter*> (p);
                    if (asFloat != nullptr)
                    {
                        const float norm = asFloat->getNormalisableRange().convertTo0to1 (freq);
                        asFloat->setValueNotifyingHost (norm);
                    }
                }
            }
        }
    }

    // Chaos mode: randomize params on a random active voice
    if (btnChaos.getToggleState())
    {
        // MASTER GUARD CLAUSE: Do nothing if there are no voices to control.
        {
            const juce::SpinLock::ScopedLockType guard (voicesLock);
            if (activeVoices.isEmpty())
                return; // This is the fix.
        }
        auto& rng = juce::Random::getSystemRandom();
        if (rng.nextFloat() < 0.10f)
        {
            const juce::SpinLock::ScopedLockType guard2 (voicesLock);
            auto index = rng.nextInt (activeVoices.size());
            if (auto* v = activeVoices[index])
            {
                static const juce::StringArray effectIds {
                    "frequency",
                    "filterCutoff", "filterResonance",
                    "chorusRate", "chorusDepth", "chorusMix"
                };
                const auto pid = effectIds[(int) rng.nextInt (effectIds.size())];
                if (auto* rp = v->getAPVTS().getParameter (pid))
                {
                    if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (rp))
                    {
                        const float rand01 = rng.nextFloat();
                        ranged->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, rand01));
                        OnScreenLogger::log ("[CHAOS] Set " + pid + " on voice to norm=" + juce::String (rand01));
                    }
                }
            }
        }
    }
}



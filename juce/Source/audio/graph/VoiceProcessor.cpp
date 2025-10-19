#include "VoiceProcessor.h"

VoiceProcessor::VoiceProcessor()
    : AudioProcessor(BusesProperties()
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "Parameters", createParameterLayout())
{}

void VoiceProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) samplesPerBlock, 2 };
    currentSampleRate = sampleRate;
    filter.reset();
    filter.prepare (spec);
    filter.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
    chorus.reset();
    chorus.prepare (spec);
    phaser.reset();
    phaser.prepare (spec);
    reverb.reset();
    reverb.prepare (spec);
    compressor.reset();
    compressor.prepare (spec);
    limiter.reset();
    limiter.prepare (spec);
    waveshaper.reset();
    delayL.reset(); delayR.reset();
    {
        juce::dsp::ProcessSpec specMono { sampleRate, (juce::uint32) samplesPerBlock, 1 };
        delayL.prepare (specMono);
        delayR.prepare (specMono);
    }
    fxPrepared = true;
    preparedChannels = spec.numChannels;
}

void VoiceProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    // Generate raw sound directly into the provided buffer.
    buffer.clear();
    renderBlock(buffer, midi);

    // No logging on audio thread

    // Load all parameters at the top
    const float cutoff        = apvts.getRawParameterValue("filterCutoff")    ? apvts.getRawParameterValue("filterCutoff")->load()    : 20000.0f;
    const float resonance     = apvts.getRawParameterValue("filterResonance") ? apvts.getRawParameterValue("filterResonance")->load() : 1.0f;
    const float chorusRate    = apvts.getRawParameterValue("chorusRate")      ? apvts.getRawParameterValue("chorusRate")->load()      : 1.0f;
    const float chorusDepth   = apvts.getRawParameterValue("chorusDepth")     ? apvts.getRawParameterValue("chorusDepth")->load()     : 0.25f;
    const float chorusMix     = apvts.getRawParameterValue("chorusMix")       ? apvts.getRawParameterValue("chorusMix")->load()       : 0.5f;
    const float phaserRate    = apvts.getRawParameterValue("phaserRate")      ? apvts.getRawParameterValue("phaserRate")->load()      : 0.5f;
    const float phaserDepth   = apvts.getRawParameterValue("phaserDepth")     ? apvts.getRawParameterValue("phaserDepth")->load()     : 0.5f;
    const float phaserCentre  = apvts.getRawParameterValue("phaserCentre")    ? apvts.getRawParameterValue("phaserCentre")->load()    : 1000.0f;
    const float phaserFB      = apvts.getRawParameterValue("phaserFeedback")  ? apvts.getRawParameterValue("phaserFeedback")->load()  : 0.0f;
    const float phaserMix     = apvts.getRawParameterValue("phaserMix")       ? apvts.getRawParameterValue("phaserMix")->load()       : 0.0f;
    const float revRoom       = apvts.getRawParameterValue("reverbRoom")      ? apvts.getRawParameterValue("reverbRoom")->load()      : 0.0f;
    const float revDamp       = apvts.getRawParameterValue("reverbDamp")      ? apvts.getRawParameterValue("reverbDamp")->load()      : 0.5f;
    const float revWidth      = apvts.getRawParameterValue("reverbWidth")     ? apvts.getRawParameterValue("reverbWidth")->load()     : 1.0f;
    const float revMix        = apvts.getRawParameterValue("reverbMix")       ? apvts.getRawParameterValue("reverbMix")->load()       : 0.0f;
    const float delayMs       = apvts.getRawParameterValue("delayTimeMs")     ? apvts.getRawParameterValue("delayTimeMs")->load()     : 0.0f;
    const float delayFB       = apvts.getRawParameterValue("delayFeedback")   ? apvts.getRawParameterValue("delayFeedback")->load()   : 0.0f;
    const float delayMix      = apvts.getRawParameterValue("delayMix")        ? apvts.getRawParameterValue("delayMix")->load()        : 0.0f;
    const float compThresh    = apvts.getRawParameterValue("compThreshold")   ? apvts.getRawParameterValue("compThreshold")->load()   : 0.0f;
    const float compRatio     = apvts.getRawParameterValue("compRatio")       ? apvts.getRawParameterValue("compRatio")->load()       : 1.0f;
    const float compAttack    = apvts.getRawParameterValue("compAttackMs")    ? apvts.getRawParameterValue("compAttackMs")->load()    : 10.0f;
    const float compRelease   = apvts.getRawParameterValue("compReleaseMs")   ? apvts.getRawParameterValue("compReleaseMs")->load()   : 100.0f;
    const float compMakeup    = apvts.getRawParameterValue("compMakeup")      ? apvts.getRawParameterValue("compMakeup")->load()      : 0.0f;
    const float limitThresh   = apvts.getRawParameterValue("limitThreshold")  ? apvts.getRawParameterValue("limitThreshold")->load()  : 0.0f;
    const float limitRelease  = apvts.getRawParameterValue("limitReleaseMs")  ? apvts.getRawParameterValue("limitReleaseMs")->load()  : 10.0f;
    const float drive         = apvts.getRawParameterValue("driveAmount")     ? apvts.getRawParameterValue("driveAmount")->load()     : 0.0f;
    const float driveMix      = apvts.getRawParameterValue("driveMix")        ? apvts.getRawParameterValue("driveMix")->load()        : 0.0f;
    const float gateThresh    = apvts.getRawParameterValue("gateThreshold")   ? apvts.getRawParameterValue("gateThreshold")->load()   : -100.0f;
    const float gateAttack    = apvts.getRawParameterValue("gateAttackMs")    ? apvts.getRawParameterValue("gateAttackMs")->load()    : 1.0f;
    const float gateRelease   = apvts.getRawParameterValue("gateReleaseMs")   ? apvts.getRawParameterValue("gateReleaseMs")->load()   : 50.0f;

    // --- 2. Process FX: Filter then Chorus ---
    // Guard against unprepared FX or channel mismatch
    juce::dsp::AudioBlock<float> block (buffer);
    if (fxPrepared && preparedChannels > 0 && block.getNumChannels() > 0 && block.getNumChannels() == preparedChannels)
    {
        juce::dsp::ProcessContextReplacing<float> context (block);

        // Apply Filter only if non-neutral
        if (cutoff < 19900.0f || resonance > 1.0f)
        {
            filter.setCutoffFrequency (juce::jlimit (20.0f, 20000.0f, cutoff));
            filter.setResonance       (juce::jlimit (0.1f, 20.0f,   resonance));
            filter.process (context);
        }

        // Apply Chorus only if it has depth
        if (chorusDepth > 0.001f)
        {
            chorus.setRate  (juce::jlimit (0.05f, 5.0f, chorusRate));
            chorus.setDepth (juce::jlimit (0.0f, 1.0f,  chorusDepth));
            chorus.setMix   (juce::jlimit (0.0f, 1.0f,  chorusMix));
            chorus.process (context);
        }

        // Phaser
        if (phaserDepth > 0.001f || std::abs (phaserFB) > 0.001f)
        {
            phaser.setRate (juce::jlimit (0.01f, 10.0f, phaserRate));
            phaser.setDepth (juce::jlimit (0.0f, 1.0f, phaserDepth));
            phaser.setCentreFrequency (juce::jlimit (20.0f, 20000.0f, phaserCentre));
            phaser.setFeedback (juce::jlimit (-0.99f, 0.99f, phaserFB));
            if (phaserMix > 0.0f)
            {
                tempBuffer.makeCopyOf (buffer, true);
                juce::dsp::AudioBlock<float> tmp (tempBuffer);
                juce::dsp::ProcessContextReplacing<float> ctx (tmp);
                phaser.process (ctx);
                const float dry = 1.0f - phaserMix;
                const float wet = phaserMix;
                for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                    buffer.addFrom (ch, 0, tempBuffer, ch, 0, buffer.getNumSamples(), wet - dry);
                buffer.applyGain (dry);
            }
            else
            {
                phaser.process (context);
            }
        }

        // Delay (simple stereo): process using dsp::DelayLine processSample to avoid state issues
        if (delayMix > 0.0f && delayMs > 0.0f)
        {
            const float maxDelaySamples = 4.0f * (float) currentSampleRate; // 4s safety
            const float delaySamples = juce::jlimit (1.0f, maxDelaySamples, delayMs * (float) currentSampleRate * 0.001f);
            const float fb = juce::jlimit (0.0f, 0.95f, delayFB);

            delayL.setDelay (delaySamples);
            delayR.setDelay (delaySamples);
            auto* l = buffer.getWritePointer (0);
            auto* r = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : l;

            const float dry = 1.0f - delayMix;
            const float wet = delayMix;
            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                const float yl = delayL.popSample (0);
                const float yr = delayR.popSample (0);
                const float dlIn = juce::jlimit (-1.0f, 1.0f, l[i] + yl * fb);
                const float drIn = juce::jlimit (-1.0f, 1.0f, r[i] + yr * fb);
                delayL.pushSample (0, dlIn);
                delayR.pushSample (0, drIn);
                l[i] = l[i] * dry + yl * wet;
                r[i] = r[i] * dry + yr * wet;
            }
        }

        // Reverb
        if (revMix > 0.0f)
        {
            juce::dsp::Reverb::Parameters rp;
            rp.roomSize = juce::jlimit (0.0f, 1.0f, revRoom);
            rp.damping  = juce::jlimit (0.0f, 1.0f, revDamp);
            rp.width    = juce::jlimit (0.0f, 1.0f, revWidth);
            rp.wetLevel = juce::jlimit (0.0f, 1.0f, revMix);
            rp.dryLevel = 1.0f - rp.wetLevel;
            reverb.setParameters (rp);
            reverb.process (context);
        }

        // Drive (tanh) with dry/wet
        if (drive > 0.001f)
        {
            const float k = juce::jlimit (0.0f, 10.0f, drive) * 5.0f;
            tempBuffer.makeCopyOf (buffer, true);
            for (int ch = 0; ch < tempBuffer.getNumChannels(); ++ch)
            {
                auto* d = tempBuffer.getWritePointer (ch);
                for (int i = 0; i < tempBuffer.getNumSamples(); ++i)
                    d[i] = std::tanh (k * d[i]);
            }
            const float dry = juce::jlimit (0.0f, 1.0f, 1.0f - driveMix);
            const float wet = juce::jlimit (0.0f, 1.0f, driveMix);
            buffer.applyGain (dry);
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                buffer.addFrom (ch, 0, tempBuffer, ch, 0, buffer.getNumSamples(), wet);
        }

        // Compressor
        if (compRatio > 1.0f)
        {
            compressor.setThreshold (juce::jlimit (-60.0f, 0.0f, compThresh));
            compressor.setRatio (juce::jmax (1.0f, compRatio));
            compressor.setAttack (juce::jlimit (0.1f, 200.0f, compAttack));
            compressor.setRelease (juce::jlimit (5.0f, 1000.0f, compRelease));
            juce::dsp::AudioBlock<float> b (buffer);
            juce::dsp::ProcessContextReplacing<float> c (b);
            compressor.process (c);
            if (std::abs (compMakeup) > 0.001f)
                buffer.applyGain (juce::Decibels::decibelsToGain (compMakeup));
        }

        // Limiter
        if (limitThresh < 0.0f)
        {
            limiter.setThreshold (juce::jlimit (-20.0f, 0.0f, limitThresh));
            limiter.setRelease (juce::jlimit (1.0f, 200.0f, limitRelease));
            juce::dsp::AudioBlock<float> b (buffer);
            juce::dsp::ProcessContextReplacing<float> c (b);
            limiter.process (c);
        }

        // Simple noise gate (post FX, pre gain) using envelope follower against dB threshold
        if (gateThresh > -90.0f)
        {
            const float thrLin = juce::Decibels::decibelsToGain (gateThresh);
            const float atk = juce::jlimit (0.001f, 0.5f, gateAttack * 0.001f);
            const float rel = juce::jlimit (0.001f, 2.0f, gateRelease * 0.001f);
            auto* l = buffer.getWritePointer (0);
            auto* r = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : l;
            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                const float mag = std::max (std::abs (l[i]), std::abs (r[i]));
                const float target = (mag >= thrLin) ? 1.0f : 0.0f;
                gateEnvL += (target - gateEnvL) * (target > gateEnvL ? atk : rel);
                gateEnvR = gateEnvL;
                l[i] *= gateEnvL;
                r[i] *= gateEnvR;
            }
        }
    }

    // --- Apply final Gain and Pan. ---
    const float gain = apvts.getRawParameterValue("gain") ? apvts.getRawParameterValue("gain")->load() : 0.7f;
    const float pan  = apvts.getRawParameterValue("pan")  ? apvts.getRawParameterValue("pan")->load()  : 0.0f;

    // Calculate left and right channel multipliers based on pan law
    const float panL = std::cos((pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f);
    const float panR = std::sin((pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f);

    // Apply the master gain and the pan law separately for clarity and correctness.
    // First, apply the overall gain to all channels.
    buffer.applyGain(gain);

    // Then, apply the panning multipliers to their respective channels.
    buffer.applyGain(0, 0, buffer.getNumSamples(), panL);
    if (buffer.getNumChannels() > 1)
        buffer.applyGain(1, 0, buffer.getNumSamples(), panR);

    // Release diagnostics: append once in a while when silent
    {
        static int diagCounter = 0;
        if ((++diagCounter % 600) == 0)
        {
            const float mag = buffer.getMagnitude (0, buffer.getNumSamples());
            if (mag < 1.0e-6f)
            {
                juce::Logger::writeToLog ("[VoiceProcessor] silent block, gain=" + juce::String (gain) +
                                          " pan=" + juce::String (pan) +
                                          " ch=" + juce::String (buffer.getNumChannels()));
            }
        }
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout VoiceProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // Core Voice Params
    params.push_back(std::make_unique<juce::AudioParameterFloat>("gain", "Gain", 0.0f, 1.0f, 0.7f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("pan", "Pan", -1.0f, 1.0f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("frequency", "Frequency", 20.0f, 20000.0f, 440.0f));

    // --- FX PARAMS ---
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "filterCutoff", "Filter Cutoff",
        juce::NormalisableRange<float>(20.0f, 20000.0f, 1.0f, 0.25f), 20000.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "filterResonance", "Filter Resonance",
        1.0f, 20.0f, 1.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "chorusRate", "Chorus Rate",
        0.1f, 10.0f, 1.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "chorusDepth", "Chorus Depth",
        0.0f, 1.0f, 0.25f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "chorusMix", "Chorus Mix",
        0.0f, 1.0f, 0.5f));

    // Phaser
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "phaserRate", "Phaser Rate",
        0.01f, 10.0f, 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "phaserDepth", "Phaser Depth",
        0.0f, 1.0f, 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "phaserCentre", "Phaser Centre",
        20.0f, 20000.0f, 1000.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "phaserFeedback", "Phaser Feedback",
        -0.99f, 0.99f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "phaserMix", "Phaser Mix",
        0.0f, 1.0f, 0.0f));

    // Reverb
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "reverbRoom", "Reverb Room",
        0.0f, 1.0f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "reverbDamp", "Reverb Damping",
        0.0f, 1.0f, 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "reverbWidth", "Reverb Width",
        0.0f, 1.0f, 1.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "reverbMix", "Reverb Mix",
        0.0f, 1.0f, 0.0f));

    // Delay
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "delayTimeMs", "Delay Time (ms)",
        1.0f, 2000.0f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "delayFeedback", "Delay Feedback",
        0.0f, 0.95f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "delayMix", "Delay Mix",
        0.0f, 1.0f, 0.0f));

    // Compressor
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "compThreshold", "Comp Threshold (dB)",
        -60.0f, 0.0f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "compRatio", "Comp Ratio",
        1.0f, 20.0f, 1.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "compAttackMs", "Comp Attack (ms)",
        0.1f, 200.0f, 10.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "compReleaseMs", "Comp Release (ms)",
        5.0f, 1000.0f, 100.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "compMakeup", "Comp Makeup (dB)",
        -12.0f, 12.0f, 0.0f));

    // Limiter
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "limitThreshold", "Limiter Threshold (dB)",
        -20.0f, 0.0f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "limitReleaseMs", "Limiter Release (ms)",
        1.0f, 200.0f, 10.0f));

    // Distortion / Drive
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "driveAmount", "Drive Amount",
        0.0f, 2.0f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "driveMix", "Drive Mix",
        0.0f, 1.0f, 0.0f));

    // Time/Pitch
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "timeStretchRatio", "Time Stretch Ratio",
        0.25f, 6.0f, 1.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "pitchSemitones", "Pitch Shift (Semitones)",
        -24.0f, 24.0f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "pitchRatio", "Pitch Ratio",
        0.5f, 2.0f, 1.0f));

    // Noise Gate
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "gateThreshold", "Gate Threshold (dB)",
        -80.0f, -20.0f, -100.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "gateAttackMs", "Gate Attack (ms)",
        0.1f, 50.0f, 1.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "gateReleaseMs", "Gate Release (ms)",
        5.0f, 500.0f, 50.0f));

    return { params.begin(), params.end() };
}
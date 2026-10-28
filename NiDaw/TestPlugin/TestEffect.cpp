#include <juce_audio_processors/juce_audio_processors.h>

/** A deliberately tiny VST3 effect, built only so HM can test its insert chains
    without depending on what happens to be installed.

    It is stereo in, stereo out, takes no MIDI, and does one thing: it scales what
    comes in by its gain parameter (0.5 by default). That makes "the effect really
    processed this track" checkable - render the same project with and without it
    and the amplitude is half - and its state is a small chunk the host can store
    and restore.
*/
class HMTestEffect : public juce::AudioProcessor
{
public:
    HMTestEffect()
        : juce::AudioProcessor (BusesProperties()
                                    .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                    .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
    {
        addParameter (gain = new juce::AudioParameterFloat ("gain", "Gain",
                                                            juce::NormalisableRange<float> (0.0f, 1.0f),
                                                            0.5f));
    }

    //==============================================================================
    const juce::String getName() const override { return "HM Test Effect"; }

    void prepareToPlay (double, int) override {}
    void releaseResources() override {}

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override
    {
        const auto supported = [] (const juce::AudioChannelSet& set)
        {
            return set == juce::AudioChannelSet::stereo()
                       || set == juce::AudioChannelSet::mono();
        };

        return supported (layouts.getMainInputChannelSet())
                   && supported (layouts.getMainOutputChannelSet());
    }

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        juce::ScopedNoDenormals noDenormals;

        // The buffer holds the input in its first channels and the output is written
        // over those same channels, so this is an in-place gain.
        const auto numSamples = buffer.getNumSamples();
        const auto value = gain->get();
        const auto channels = juce::jmin (getTotalNumOutputChannels(), buffer.getNumChannels());

        for (int channel = 0; channel < channels; ++channel)
            buffer.applyGain (channel, 0, numSamples, value);
    }

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }

    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }

    /** No look-ahead, nothing to report: HM shows 0 ms for this one. */
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destination) override
    {
        const auto value = gain->get();
        destination.setSize (sizeof (kMagic) + sizeof (value), true);
        destination.copyFrom (&kMagic, 0, sizeof (kMagic));
        destination.copyFrom (&value, sizeof (kMagic), sizeof (value));
    }

    void setStateInformation (const void* data, int sizeInBytes) override
    {
        if (sizeInBytes != (int) (sizeof (kMagic) + sizeof (float)))
            return;

        float value = 0.0f;
        std::memcpy (&value, static_cast<const char*> (data) + sizeof (kMagic), sizeof (value));
        *gain = juce::jlimit (0.0f, 1.0f, value);
    }

    static constexpr juce::uint32 kMagic = 0x484d4658;   // "HMFX"

private:
    juce::AudioParameterFloat* gain = nullptr;
};

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new HMTestEffect();
}

#include "TestInstrumentReport.h"

#include <juce_audio_processors/juce_audio_processors.h>

/** A deliberately tiny VST3 instrument, built only so HM can test its plugin
    hosting end to end without depending on what happens to be installed.

    It plays a sine at the note it is told to play and follows the pitch wheel
    with the two semitone range the host programs into it. That is exactly what
    makes the host's frequency-to-MIDI mapping checkable: write a note at 660 Hz,
    render, and 660 Hz has to come out.

    Its interface is deliberately much bigger than the host's default window, which
    is what makes "the window is at least as big as the plugin" checkable: a host
    that keeps its own 640x420 leaves most of a real plugin outside the window.
*/
class HMTestInstrumentEditor : public juce::AudioProcessorEditor
{
public:
    explicit HMTestInstrumentEditor (juce::AudioProcessor& owner)
        : juce::AudioProcessorEditor (owner)
    {
        // Big, and resizable by the host, like the instruments this exists to test.
        setResizable (true, false);
        setSize (kPreferredWidth, kPreferredHeight);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff202028));
        g.setColour (juce::Colour (0xffe6e6ee));
        g.setFont (juce::FontOptions (18.0f));
        g.drawText ("HM Test Instrument " + juce::String (getWidth()) + "x" + juce::String (getHeight()),
                    getLocalBounds(), juce::Justification::centred);
    }

    static constexpr int kPreferredWidth = 900;
    static constexpr int kPreferredHeight = 600;
};

class HMTestInstrument : public juce::AudioProcessor
{
public:
    HMTestInstrument()
        : juce::AudioProcessor (BusesProperties().withOutput ("Output",
                                                             juce::AudioChannelSet::stereo(),
                                                             true))
    {
        addParameter (gain = new juce::AudioParameterFloat ("gain", "Gain",
                                                            juce::NormalisableRange<float> (0.0f, 1.0f),
                                                            0.5f));
        addParameter (receipts = new juce::AudioParameterInt (hmtests::kReceiptsParameterName,
                                                              hmtests::kReceiptsParameterName,
                                                              0, hmtests::kReceiptsMax, 0));
        addParameter (conflicts = new juce::AudioParameterInt (hmtests::kConflictsParameterName,
                                                               hmtests::kConflictsParameterName,
                                                               0, hmtests::kReceiptsMax, 0));
        addParameter (channelsUsed = new juce::AudioParameterInt (hmtests::kChannelsUsedParameterName,
                                                                  hmtests::kChannelsUsedParameterName,
                                                                  0, 16, 0));
        addParameter (pedalChannels = new juce::AudioParameterInt (hmtests::kPedalChannelsParameterName,
                                                                   hmtests::kPedalChannelsParameterName,
                                                                   0, 16, 0));
        addParameter (peakVoices = new juce::AudioParameterInt (hmtests::kPeakVoicesParameterName,
                                                                hmtests::kPeakVoicesParameterName,
                                                                0, 16, 0));
        addParameter (resetCounters = new juce::AudioParameterInt (hmtests::kResetCountersParameterName,
                                                                   hmtests::kResetCountersParameterName,
                                                                   0, 1, 0));
    }

    //==============================================================================
    const juce::String getName() const override { return "HM Test Instrument"; }

    void prepareToPlay (double sampleRate, int) override
    {
        currentSampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;

        for (auto& voice : voices)
            voice = Voice{};

        for (auto& bend : bendCents)
            bend = 0.0;

        channelSeen.fill (false);
        pedalSeen.fill (false);
        *conflicts = 0;
        *channelsUsed = 0;
        *pedalChannels = 0;
        *peakVoices = 0;
        *resetCounters = 0;

        // Emulates the samplers that re-initialise their instrument when the host
        // re-prepares them (a device restart with a different rate or block size does
        // exactly that, and Kontakt comes back with its volume at the default). The
        // host is expected to notice and put the state back - that is what the
        // self test checks.
        *gain = 0.5f;
    }

    void releaseResources() override {}

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override
    {
        const auto output = layouts.getMainOutputChannelSet();
        return output == juce::AudioChannelSet::stereo()
                   || output == juce::AudioChannelSet::mono();
    }

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
    {
        juce::ScopedNoDenormals noDenormals;

        if (resetCounters->get() > 0)
        {
            channelSeen.fill (false);
            pedalSeen.fill (false);
            *conflicts = 0;
            *channelsUsed = 0;
            *pedalChannels = 0;
            *peakVoices = 0;
            resetCounters->setValueNotifyingHost (0.0f);
        }

        buffer.clear();

        const auto numSamples = buffer.getNumSamples();

        for (const auto metadata : midi)
        {
            const auto message = metadata.getMessage();
            const auto position = metadata.samplePosition;

            if (position >= numSamples)
                continue;

            if (message.isNoteOn())
            {
                const auto channel = juce::jlimit (1, 16, message.getChannel());
                const auto semitones = message.getNoteNumber() + bendCents[(size_t) (channel - 1)] / 100.0;

                // The thing this instrument exists to catch: a new note landing on a
                // channel that is still holding a note at another pitch. One pitch
                // bend cannot serve both, so the two notes pull each other around.
                for (const auto& other : voices)
                    if (other.active && ! other.released && other.channel == channel
                        && std::abs (other.soundingSemitones - semitones) > 0.005)
                        *conflicts = juce::jmin (hmtests::kReceiptsMax, conflicts->get() + 1);

                if (! channelSeen[(size_t) (channel - 1)])
                {
                    channelSeen[(size_t) (channel - 1)] = true;
                    *channelsUsed = juce::jmin (16, channelsUsed->get() + 1);
                }

                auto& voice = voices[(size_t) nextVoice++ % voices.size()];
                voice = Voice{};
                voice.active    = true;
                voice.note      = message.getNoteNumber();
                voice.channel   = message.getChannel();
                voice.velocity  = message.getFloatVelocity();
                voice.phase     = 0.0;
                voice.startSample = position;
                voice.soundingSemitones = semitones;
                noteReceipt (hmtests::kSawNoteOn);
            }
            else if (message.isNoteOff())
            {
                for (auto& voice : voices)
                    if (voice.active && ! voice.released && voice.note == message.getNoteNumber())
                        voice.released = true;
            }
            else if (message.isPitchWheel())
            {
                noteReceipt (hmtests::kSawPitchBend);

                const auto channel = juce::jlimit (1, 16, message.getChannel()) - 1;
                bendCents[(size_t) channel] = (message.getPitchWheelValue() - 8192) / 8192.0 * 200.0;
            }
            else if (message.isController())
            {
                if (message.getControllerNumber() == 64)
                {
                    const auto channel = juce::jlimit (1, 16, message.getChannel());

                    if (! pedalSeen[(size_t) (channel - 1)])
                    {
                        pedalSeen[(size_t) (channel - 1)] = true;
                        *pedalChannels = juce::jmin (16, pedalChannels->get() + 1);
                    }
                }

                // The self test reads this back to prove which controllers the host
                // ever sends: the answer has to be "none that touch a sound" (no CC 7,
                // no CC 10, no CC 11) or an instrument's own volume can move behind
                // the user's back.
                noteReceipt (hmtests::receiptBitForController (message.getControllerNumber()));
            }
            else if (message.isAllNotesOff())
            {
                for (auto& voice : voices)
                    if (voice.active)
                        voice.released = true;
            }
        }

        for (auto& voice : voices)
        {
            if (! voice.active)
                continue;

            const auto frequency = 440.0
                                       * std::pow (2.0, (voice.note - 69 + bendCents[(size_t) voice.channel - 1] / 100.0) / 12.0);
            const auto delta = juce::MathConstants<double>::twoPi * frequency / currentSampleRate;

            auto* left  = buffer.getWritePointer (0);
            auto* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;

            for (int i = 0; i < numSamples; ++i)
            {
                if (voice.released)
                    voice.release += 1.0 / (kReleaseSeconds * currentSampleRate);
                else
                    voice.attack = juce::jmin (1.0, voice.attack + 1.0 / (kAttackSeconds * currentSampleRate));

                const auto envelope = voice.attack * juce::jmax (0.0, 1.0 - voice.release);
                const auto sample = (float) (std::sin (voice.phase) * envelope * voice.velocity
                                                 * (double) gain->get());

                left[i] += sample;

                if (right != nullptr)
                    right[i] += sample;

                voice.phase += delta;
            }

            if (voice.release >= 1.0)
                voice.active = false;
        }

        auto sounding = 0;

        for (const auto& voice : voices)
            if (voice.active && ! voice.released)
                ++sounding;

        if (sounding > peakVoices->get())
            *peakVoices = juce::jmin (16, sounding);
    }

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override { return new HMTestInstrumentEditor (*this); }
    bool hasEditor() const override { return true; }

    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }

    double getTailLengthSeconds() const override { return kReleaseSeconds; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    /** A small state chunk: the magic makes "was the state really restored" a
        question the host can answer. */
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

    static constexpr juce::uint32 kMagic = 0x484d5631;   // "HMV1"

private:
    /** Adds one bit to the report the host can read back (see the header). */
    void noteReceipt (int bit) noexcept
    {
        if (bit == 0)
            return;

        const auto current = receipts->get();

        if ((current & bit) == 0)
            *receipts = current | bit;
    }

    struct Voice
    {
        bool   active   = false;
        bool   released = false;
        int    note     = 60;
        int    channel  = 1;
        float  velocity = 1.0f;
        double phase    = 0.0;
        double attack   = 0.0;
        double release  = 0.0;
        int    startSample = 0;
        /** The pitch this voice was started at, so "two pitches in one channel" is
            answerable even after the channel's bend has moved on. */
        double soundingSemitones = 0.0;
    };

    static constexpr double kAttackSeconds  = 0.002;
    static constexpr double kReleaseSeconds = 0.050;
    static constexpr int    kVoices         = 16;

    std::array<Voice, kVoices> voices {};
    std::array<double, 16> bendCents {};
    std::array<bool, 16> channelSeen {};
    std::array<bool, 16> pedalSeen {};
    juce::AudioParameterFloat* gain = nullptr;
    /** What the host has sent this instance: bits, readable through the parameter. */
    juce::AudioParameterInt* receipts = nullptr;
    juce::AudioParameterInt* conflicts = nullptr;
    juce::AudioParameterInt* channelsUsed = nullptr;
    juce::AudioParameterInt* pedalChannels = nullptr;
    juce::AudioParameterInt* peakVoices = nullptr;
    juce::AudioParameterInt* resetCounters = nullptr;
    double currentSampleRate = 48000.0;
    int nextVoice = 0;
};

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new HMTestInstrument();
}

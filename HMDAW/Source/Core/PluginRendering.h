#pragma once

#include "Types.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <functional>
#include <memory>

namespace hmdaw
{

/** A hosted external instrument, seen from the render loop's point of view.

    Deliberately shaped like the built-in instruments: the engine calls the same
    four notes-then-render sequence it calls on the FM synth, so a VST track takes
    exactly the same path through the transport, the loop and the export.

    The real implementation lives in the app (it needs the JUCE plugin classes);
    the unit tests never construct one, which is why this header must stay free of
    any plugin dependency.
*/
class HostedInstrument
{
public:
    virtual ~HostedInstrument() = default;

    /** Called on the message thread before the instance is published. */
    virtual void prepare (double sampleRate, int maxBlockSize) = 0;

    /** Drops every sounding note without making a sound of its own. */
    virtual void reset() = 0;

    virtual void noteOn (juce::int64 noteId, float frequencyHz, float velocity) = 0;
    virtual void noteOff (juce::int64 noteId) = 0;
    virtual void allNotesOff() = 0;

    /** Lets go of every held note the way a player lifting their hands would: a
        real note-off for each of them, so the release (and anything the plugin
        has ringing, such as its own reverb) decays instead of being cut.

        `allNotesOff` stays the hard one - it is what "this must be silent now"
        means (changing project, unloading, swapping the instrument in a slot).
        The default does nothing, so a test double only implements what it checks.
    */
    virtual void releaseHeldNotes()
    {
    }

    /** One controller value the user drew in the editor's controller lane.

        The engine forwards these on a VST track and leaves the instrument alone on a
        built-in one (where it handles expression and the sustain pedal itself). The
        default does nothing, so a test double only has to implement what it checks.
    */
    virtual void sendController (int controllerNumber, int value)
    {
        juce::ignoreUnused (controllerNumber, value);
    }

    /** Adds the instrument's output into the first two channels of `buffer`. */
    virtual void renderAdding (juce::AudioBuffer<float>& buffer,
                               int startSample, int numSamples,
                               float gainLeft, float gainRight) = 0;

    /** True while the instrument may still be making sound - a held note or a
        release that has not died away yet. */
    virtual bool isActive() const noexcept = 0;

    /** The plugin's own state, for storing it in the project. Empty when the
        instrument has nothing to say. */
    virtual juce::MemoryBlock captureState() const = 0;
};

/** Makes a fresh, already prepared instrument for one VST track.

    The offline export uses this so a file is rendered by a private instance
    instead of by the one the audio device is playing through. Returns nullptr
    when the plugin cannot be loaded - the track then simply renders silence.
*/
using HostedInstrumentFactory =
    std::function<std::shared_ptr<HostedInstrument> (juce::int64 trackId, const VstParams& params)>;

/** One effect in a track's insert chain, seen from the render loop.

    It processes the track's audio in place: whatever is in `buffer` when it is
    called is what it works on, and the result is expected to be there afterwards.
    A bypassed effect does nothing at all (the caller still calls it, so the
    instance keeps its position and its state).
*/
class HostedEffect
{
public:
    virtual ~HostedEffect() = default;

    virtual void prepare (double sampleRate, int maxBlockSize) = 0;
    virtual void reset() = 0;

    /** Processes `numSamples` of the first two channels, in place. */
    virtual void process (juce::AudioBuffer<float>& buffer, int startSample, int numSamples) = 0;

    virtual bool isBypassed() const noexcept = 0;

    /** What the plugin says its own latency is; shown in the effect's row. */
    virtual int getLatencySamples() const noexcept = 0;

    /** The plugin's own state, for storing it in the project. */
    virtual juce::MemoryBlock captureState() const = 0;
};

/** Makes a fresh, already prepared effect for one slot of one track. Used by the
    offline export; returns nullptr when the plugin is missing. */
using HostedEffectFactory =
    std::function<std::shared_ptr<HostedEffect> (juce::int64 trackId, int slotIndex,
                                                 const EffectSlot& slot)>;

/** The app's plugin host, as the audio engine sees it.

    Live instances are created on the message thread (that is what plugins
    expect); the audio thread only looks them up. The unit tests leave this null,
    which means a VST track makes no sound and nothing crashes.
*/
class LiveInstrumentProvider
{
public:
    virtual ~LiveInstrumentProvider() = default;

    /** Prepares every live instance for a new device rate / block size. */
    virtual void prepareAll (double sampleRate, int maxBlockSize) = 0;

    /** The instance for a track, or nullptr while it is still loading (or when
        the plugin is missing). */
    virtual std::shared_ptr<HostedInstrument> instrumentFor (juce::int64 trackId,
                                                             const VstParams& params) = 0;

    /** The instance for one slot of a track's insert chain, or nullptr while it is
        still loading (or when the plugin is missing). */
    virtual std::shared_ptr<HostedEffect> effectFor (juce::int64 trackId, int slotIndex,
                                                     const EffectSlot& slot) = 0;
};

} // namespace hmdaw

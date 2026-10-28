#pragma once

#include "../Core/ProjectDocument.h"
#include "SubtractiveSynth.h"
#include "WavetableSynth.h"
#include "DrumSynth.h"
#include "GranularSynth.h"
#include "HybridSynth.h"
#include "SynthEngine.h"

#include <juce_audio_devices/juce_audio_devices.h>

#include <atomic>
#include <memory>

namespace nidaw
{

/** Live notes (a MIDI keyboard, the on-screen keys, a written note's audition) are
    numbered in a space of their own: **far below** anything a project can hold.

    Scheduled notes get their ids from the track's event list - 1, 2, 3… - and a live
    note that happened to carry one of those numbers would be taken for "the same
    note" by the instrument (and by the MIDI channel it owns): the two notes would
    share one channel, one pitch bend, and one note-off. Keeping the two spaces
    apart is what makes a live note and a playing project note independent, and it
    also keeps the three live sources apart from each other (they start at 1,
    -1000 and -100000 respectively).
*/
constexpr juce::int64 kLiveNoteIdBase = -(juce::int64) (1LL << 40);

/** The id an instrument sees for a live note the caller named `callerId`. */
inline juce::int64 liveNoteIdFor (juce::int64 callerId) noexcept
{
    return kLiveNoteIdBase - callerId;
}

/** Realtime-safe playback of an immutable RenderSchedule.

    The same class drives both the audio device callback and the offline export,
    which keeps the exported file identical to what you hear.
*/
class PlaybackEngine
{
public:
    PlaybackEngine();

    void prepare (double newSampleRate, int maxBlockSize);

    /** Called from the message thread whenever the project changes. */
    void setSchedule (std::shared_ptr<const RenderSchedule> newSchedule);

    /** Tempo map for the metronome. Set from the message thread alongside the
        schedule; the audio thread only reads it. */
    void setTempoMap (std::shared_ptr<const music::TempoMap> newTempoMap);

    /** Monitoring aids. Both default to off, which is what keeps an offline
        export free of clicks and of loop wraps. */
    void setMetronomeEnabled (bool shouldClick) noexcept;
    bool isMetronomeEnabled() const noexcept { return metronomeOn.load (std::memory_order_relaxed); }

    /** Loop (cycle) range in seconds. An empty or inverted range is ignored. */
    void setLoopRegion (double startSeconds, double endSeconds, bool shouldLoop) noexcept;
    bool isLoopEnabled() const noexcept { return loopOn.load (std::memory_order_relaxed); }

    void setPlaying (bool shouldPlay);
    bool isPlaying() const noexcept { return playing.load (std::memory_order_relaxed); }

    void setPositionSeconds (double seconds);
    /** The same, marked as 停止's own rewind: notes are released, but the insert
        chains are left alone so their tails ring out. */
    void setStopRewindSeconds (double seconds);
    /** Puts an end to whatever the last stop left ringing: every voice is cut hard
        and every insert chain is flushed. 播放 calls this when it starts from a
        stopped transport, so the new pass begins from silence instead of over the
        top of the previous one's reverb or delay tail. */
    void cutSoundingTails();
    double getPositionSeconds() const noexcept { return positionSeconds.load (std::memory_order_relaxed); }

    void allNotesOff();

    // ---- live notes ---------------------------------------------------------
    /** A note played *now* rather than scheduled: a MIDI keyboard, the on-screen
        keys of the MIDI input window, or the little audition that confirms a note
        you just wrote with the mouse.

        It goes to the track's own instrument and then through the track's chain,
        fader and pan exactly like a scheduled note does, it sounds whether or not
        the transport is running, and it never reaches an offline export (that uses
        its own engine). `pad` >= 0 hits that drum on a drum track.

        `noteId` only has to be unique among *this* caller's own live notes: it is
        translated into the private live space (`kLiveNoteIdBase`) on the way in, so
        it can never collide with a scheduled note's id. The id is rejected when it
        is 0.
    */
    void noteOnTrack (juce::int64 trackId, juce::int64 noteId, double frequencyHz,
                      float velocity, int pad = -1);
    void noteOffTrack (juce::int64 trackId, juce::int64 noteId);

    /** A controller value played *now*: the sustain pedal, the expression pedal or
        the mod wheel of the MIDI keyboard (and of the virtual keyboard's own
        controls). It follows the same rule as a controller curve drawn in the
        editor: a **built-in** instrument has the value applied by the engine
        (expression is a gain multiplier on the track, the pedal holds note-offs
        back), and a **VST** track has it forwarded to the plugin, which applies it
        itself - never both.

        `controller` outside 0..127, and `value` outside 0..127, are clamped.
    */
    void sendControllerToTrack (juce::int64 trackId, int controller, int value);
    /** How many live events had to be dropped because the queue was full. A
        diagnostic: it should stay at 0. */
    int getLiveQueueOverflowCountForTest() const noexcept
    {
        return liveOverflow.load (std::memory_order_relaxed);
    }

    /** How many tail cuts the audio thread has applied. The self test uses it to
        check that starting the transport cuts the tail exactly once and that
        pressing play while it is already running cuts nothing. */
    int getTailCutCountForTest() const noexcept
    {
        return tailCutCount.load (std::memory_order_relaxed);
    }

    /** Audition voice used by the instrument editor's mini keyboard. It is mixed
        into the output independently of the transport and never takes part in an
        offline export. */
    void triggerPreviewNote (const SynthParams& params, double frequencyHz, float velocity);
    void releasePreviewNote();
    void updatePreviewParams (const SynthParams& params);

    /** The same, for a sampled instrument. */
    void triggerPreviewSampleNote (const SamplerParams& params,
                                   std::shared_ptr<const LoadedSample> sample,
                                   double frequencyHz, float velocity);
    void updatePreviewSampleParams (const SamplerParams& params);

    /** Same, for the subtractive instrument. */
    void triggerPreviewSubNote (const SubtractiveParams& params, double frequencyHz, float velocity);
    void updatePreviewSubParams (const SubtractiveParams& params);

    /** Same, for the wavetable instrument. */
    void triggerPreviewWaveNote (const WavetableParams& params, double frequencyHz, float velocity);
    void updatePreviewWaveParams (const WavetableParams& params);
    /** The project's own tables for the previewed instrument (null = built-in).
        The sound window and the wavetable editor push these, so the on-screen
        keyboard auditions exactly the table that is open. */
    void updatePreviewWaveTables (std::shared_ptr<const wavetable::Wavetable> table0,
                                  std::shared_ptr<const wavetable::Wavetable> table1);

    /** Same, for the drum kit: `pad` picks the drum. */
    void triggerPreviewDrumNote (const DrumKitParams& params, int pad, float velocity);
    void updatePreviewDrumParams (const DrumKitParams& params);

    /** Same, for the granular instrument (the file it cuts grains out of). */
    void triggerPreviewGranularNote (const GranularParams& params,
                                     std::shared_ptr<const LoadedSample> sample,
                                     double frequencyHz, float velocity);
    void updatePreviewGranularParams (const GranularParams& params);

    /** Renders the next block into the internal output buffer. */
    void renderNextBlock (int numSamples);

    /** Self test only: forget every queued live event and stop every voice.

        The offscreen run renders blocks by hand and has no audio device, so nothing
        drains the live queue between scenes. Without this, a note auditioned in an
        earlier scene fires (with its old id) at the start of the next measurement -
        which showed up as "9 voices from one key". Call it from the message thread
        while no device is running.
    */
    void resetLiveStateForTest();

    const juce::AudioBuffer<float>& getOutputBuffer() const noexcept { return output; }
    juce::AudioBuffer<float>& getOutputBuffer() noexcept { return output; }

    double getSampleRate() const noexcept { return sampleRate; }

    /** Diagnostics / tests. */
    int getActiveVoiceCount() const noexcept;

    // ---- level metering -----------------------------------------------------
    /** One meter's reading: the loudest sample each channel has contributed since
        the last call, and whether that channel went past full scale.

        Reading it *clears* it: the audio thread only ever publishes numbers, and all
        the falling, holding and dots are done by whoever draws the meter.
    */
    struct MeterReading
    {
        float peakLeft = 0.0f;
        float peakRight = 0.0f;
        bool  clipLeft = false;
        bool  clipRight = false;
    };

    MeterReading readTrackMeter (juce::int64 trackId) noexcept;
    MeterReading readMasterMeter() noexcept;

    // ---- what a wavetable track is playing right now -------------------------
    /** The table position the newest sounding voice of this track computed (position
        slider + position envelope + LFO), or the last value when nothing sounds.
        `osc` is 0 or 1. The editor's waterfall view draws its cursor from this: the
        picture follows the sound rather than a second guess at the maths. */
    float getWavetablePosition (juce::int64 trackId, int osc) const noexcept;
    /** How many voices of this track's wavetable instrument are sounding. */
    int getWavetableActiveVoices (juce::int64 trackId) const noexcept;
    /** The same two, for the audition voice the instrument windows' keyboards use. */
    float getPreviewWavetablePosition (int osc) const noexcept;
    int getPreviewWavetableActiveVoices() const noexcept;

    // ---- what a granular track is playing right now ---------------------------
    /** How many grains of this track are in the air, and where each one is reading
        (0..1 through the file) and how far through its own life it is (0..1). The
        editor's scatter view draws exactly these, so the picture follows the sound
        rather than a second guess at the maths. */
    int   getGranularGrainCount (juce::int64 trackId) const noexcept;
    int   getHybridGranularGrainCount (juce::int64 trackId, int layer) const noexcept;
    float getHybridGranularGrainPosition (juce::int64 trackId, int layer,
                                          int index) const noexcept;
    float getHybridGranularGrainAge (juce::int64 trackId, int layer,
                                     int index) const noexcept;
    float getHybridEnvelopePosition (juce::int64 trackId, int index) const noexcept;
    float getHybridEnvelopeValue (juce::int64 trackId, int index) const noexcept;
    float getHybridLfoPhase (juce::int64 trackId, int index) const noexcept;
    float getHybridLfoValue (juce::int64 trackId, int index) const noexcept;
    int getHybridActiveVoices (juce::int64 trackId) const noexcept;
    /** The hybrid layer's own wavetable oscillator reading. */
    float getHybridWavetablePosition (juce::int64 trackId, int layer) const noexcept;
    float getGranularGrainPosition (juce::int64 trackId, int index) const noexcept;
    float getGranularGrainAge (juce::int64 trackId, int index) const noexcept;
    /** The same three, for the audition voice the instrument windows use. */
    int   getPreviewGranularGrainCount() const noexcept;
    float getPreviewGranularGrainPosition (int index) const noexcept;
    float getPreviewGranularGrainAge (int index) const noexcept;

private:
    std::shared_ptr<const RenderSchedule> getScheduleSnapshot();
    std::shared_ptr<const music::TempoMap> getTempoMapSnapshot();
    void matchTracks (const std::shared_ptr<const RenderSchedule>& schedule);
    /** Which slot a track is being rendered in, or -1 (the meters work by track). */
    int slotOfTrack (juce::int64 trackId) const noexcept;
    void processEvents (const RenderSchedule& schedule, double upToSeconds);
    /** Walks the drawn controller curves up to a moment: forwards them to an
        external instrument, or folds expression / the sustain pedal into this
        slot's own state for a built-in one. Runs before the notes of the same
        instant, so a pedal pressed exactly on a note's end still holds it. */
    void processControllers (const RenderSchedule& schedule, double upToSeconds);
    /** Folds one controller value into a slot's own state (the built-in rule):
        CC11 multiplies the track's output, CC64 holds note-offs back until it is
        lifted. Shared by the drawn curves and the live controller messages. */
    void applyControllerToSlot (int slot, int controller, int value);
    /** Releases everything a held sustain pedal was keeping alive. */
    void releaseSustained();
    /** Lets go of every sounding voice, of every kind of instrument.

        Used when the transport wraps or jumps: a note whose note-off lies beyond
        the loop end (or behind the jump) is never told to stop by the event stream,
        so this is the only thing that can end it. **Every** instrument pool has to
        be asked - this is exactly the list that used to miss the subtractive, the
        wavetable and the drum voices, which left notes ringing for ever at a loop.
    */
    void releaseAllVoices (bool immediately);
    /** Starts or stops one note on one slot. */
    /** `honourSustain` is false when the pedal itself is what is releasing the note:
        a note held by the pedal must go even though the pedal is still down. */
    void triggerEvent (int slot, TrackKind kind, const SynthEvent& event,
                       bool honourSustain = true, bool equalTemperament = false);
    /** Puts the controller cursors back so the state at `positionSeconds` is the one
        the curves describe: the last value at or before it is re-applied. */
    void rewindControllerCursors (double atSeconds);
    double nextEventTime (const RenderSchedule& schedule) const;
    void resetClickCursor (const music::TempoMap& tempoMap, double startSeconds);
    void processClicks (const music::TempoMap& tempoMap, double upToSeconds);
    void triggerClick (bool accent);
    void renderClicks (int offset, int numSamples);
    /** Jumps back to the loop start: releases the voices, rewinds every track
        cursor and re-arms the metronome. Runs on the audio thread. */
    void wrapToLoopStart (double startSeconds);
    void renderVoices (const RenderSchedule& schedule, int offset, int numSamples,
                       double bpm);
    /** Runs one track's insert chain over whatever `trackBuffer` holds. */
    void runEffectChain (const TrackRenderInfo& info, int numSamples);
    /** Adds `trackBuffer` to the output with the track's volume and pan. */
    void mixTrackIntoOutput (const TrackRenderInfo& info, int offset, int numSamples);
    /** Mixes the placed audio clips of every audio track for one chunk. */
    void renderAudioTracks (const RenderSchedule& schedule, double startSeconds,
                            int offset, int numSamples);
    void applyPendingRequests();
    void applyPreview();
    void renderPreviewVoices (int numSamples);

    /** One live event on its way from the message thread to the audio thread: a
        note going on or off, or one controller value from the keyboard. The queue
        is the only thing the two threads share. */
    enum class LiveEventKind { noteOn, noteOff, controller };

    struct LiveEvent
    {
        LiveEventKind kind       = LiveEventKind::noteOn;
        juce::int64 trackId   = 0;
        juce::int64 noteId    = 0;
        float       frequency = 440.0f;
        float       velocity  = 100.0f;
        int         pad       = -1;
        /** kind == controller: which controller, and the value it now has. */
        int         controller = 0;
        int         value      = 0;
    };

    static constexpr int kLiveQueueSize = 512;
    juce::AbstractFifo liveFifo { kLiveQueueSize };
    std::array<LiveEvent, (size_t) kLiveQueueSize> liveEvents {};
    std::atomic<int> liveOverflow { 0 };

    /** Message thread: hand one event to the audio thread (dropped, and counted,
        when the queue is full - the audio thread is never made to wait). */
    void pushLiveEvent (const LiveEvent& event);
    /** Audio thread: applies everything that arrived to the track's instrument. */
    void applyLiveEvents (const RenderSchedule& schedule);
    void applyLiveEvent (const RenderSchedule& schedule, const LiveEvent& event);

    juce::SpinLock scheduleLock;
    std::shared_ptr<const RenderSchedule> schedule;
    std::shared_ptr<const RenderSchedule> lastMatched;
    std::shared_ptr<const music::TempoMap> tempoMap;
    std::shared_ptr<const music::TempoMap> lastTempoMap;

    /** One synth per track, on the heap: 64 tracks of six-operator voices is far
        too much to keep inside a component that may itself live on the stack. */
    std::vector<TrackSynth> synths;
    std::vector<SamplePlayer> samplePlayers;
    std::vector<SubtractiveSynth> subtractiveSynths;
    std::vector<WavetableSynth> wavetableSynths;
    std::vector<DrumSynth> drumKits;
    std::vector<GranularSynth> granularSynths;
    std::vector<HybridSynth> hybridSynths;
    /** The hosted instruments the current schedule points at, kept so a track that
        changes its plugin (or goes away) can be told to stop. Owned by the app's
        plugin host; this is only a reference. */
    std::vector<std::shared_ptr<HostedInstrument>> hostedInstruments;
    /** The insert-chain instances of the current schedule, kept for the same
        reason as the instruments above: a track that goes away (or changes its
        chain) has to be told to stop, and the audio thread must never be the one
        that destroys the last reference. */
    std::vector<std::vector<std::shared_ptr<HostedEffect>>> hostedEffects;
    std::array<juce::int64, kMaxTracks> trackIds {};
    std::array<size_t, kMaxTracks> cursors {};
    /** How far along each track's own controller list the render has walked. */
    std::array<size_t, kMaxTracks> controllerCursors {};

    /** What a drawn curve does to a **built-in** instrument: expression multiplies
        the track's output (CC11), and the sustain pedal (CC64) holds note-offs back
        until it is lifted. External instruments get the controller messages
        themselves and are left alone here, so nothing is ever applied twice. */
    struct ControllerState
    {
        float expression = 1.0f;
        bool  sustainDown = false;
        /** The notes whose note-off the pedal is holding back. */
        std::vector<juce::int64> sustainedNotes;
        /** Which instrument this slot is playing, so a pedal release can hand the
            held note-offs to the right voice pool. */
        TrackKind kind = TrackKind::synth;
    };

    std::array<ControllerState, kMaxTracks> controllerStates {};
    int activeTrackCount = 0;

    /** What the meters read: each channel's peak for each slot, and the same for the
        mix as a whole. The audio thread only writes; a reader takes the value and
        clears it, so a meter never shows a stale peak twice. */
    std::array<std::atomic<juce::int64>, kMaxTracks> publishedTrackIds {};
    std::array<std::atomic<float>, kMaxTracks> trackPeakLeft {}, trackPeakRight {};
    std::array<std::atomic<bool>, kMaxTracks> trackClipLeft {}, trackClipRight {};
    std::atomic<float> masterPeakLeft { 0.0f }, masterPeakRight { 0.0f };
    std::atomic<bool> masterClipLeft { false }, masterClipRight { false };

    juce::AudioBuffer<float> output;
    /** One track's signal, used while its insert chain runs: the track is rendered
        here at unity, the chain processes it, and only then is it added to the
        output with the track's volume and pan (inserts are pre-fader). */
    juce::AudioBuffer<float> trackBuffer;
    std::atomic<double> positionSeconds { 0.0 };
    std::atomic<bool> playing { false };

    std::atomic<bool> metronomeOn { false };
    std::atomic<bool> loopOn { false };
    std::atomic<double> loopStartSeconds { 0.0 };
    std::atomic<double> loopEndSeconds { 0.0 };

    /** One click: a decaying sine, so a beat is a short "tick" and the downbeat
        is a brighter, louder one. Audio thread only. */
    struct ClickVoice
    {
        bool   active = false;
        double phase  = 0.0;
        double delta  = 0.0;
        double gain   = 0.0;
        double decay  = 1.0;
    };

    static constexpr int kMaxClickVoices = 8;
    std::array<ClickVoice, kMaxClickVoices> clicks;
    juce::int64 nextClickTick = 0;
    /** Written from both threads: the message thread re-arms it on play/stop. */
    std::atomic<bool> clickCursorValid { false };

    // Requests handed from the message thread to the audio thread. The message
    // thread must never touch voice state or the schedule directly.
    std::atomic<double> pendingSeekSeconds { -1.0 };
    std::atomic<bool> pendingReleaseVoices { false };
    /** True while the pending seek is the one 停止 does (rewinding to where the
        transport started): the notes are released and the cursors are rewound just
        the same, but the insert chains are **not** reset, so a reverb or a delay
        keeps ringing out instead of being cut. A real jump (dragging the playhead,
        Home, opening a project) still resets them. */
    std::atomic<bool> pendingStopRewind { false };

    /** True when the message thread asked for the tails to be cut because the
        transport is starting again (see `cutSoundingTails`). Consumed by the audio
        thread, which is the only place voices and chains may be touched. */
    std::atomic<bool> pendingTailCut { false };
    /** Bumped every time that cut is actually applied. */
    std::atomic<int> tailCutCount { 0 };

    /** How many blocks in a row a track whose instrument has gone quiet has
        produced nothing at all, so its inserts can stop being rendered. */
    std::array<int, kMaxTracks> chainSilenceBlocks {};

    /** Which instrument the audition voice is using. */
    enum class PreviewKind { fm, sampler, subtractive, wavetable, drums, granular };

    TrackSynth previewSynth;
    SamplePlayer previewSampler;
    SubtractiveSynth previewSubtractive;
    WavetableSynth previewWavetable;
    DrumSynth previewDrums;
    GranularSynth previewGranular;
    juce::SpinLock previewLock;
    SynthParams previewParams;
    SamplerParams previewSamplerParams;
    SubtractiveParams previewSubParams;
    WavetableParams previewWaveParams;
    std::array<std::shared_ptr<const wavetable::Wavetable>, 2> previewWaveTables;
    DrumKitParams previewDrumParams;
    GranularParams previewGranularParams;
    int previewDrumPad = 0;
    std::shared_ptr<const LoadedSample> previewSample;
    PreviewKind previewKind = PreviewKind::fm;
    double previewFrequency = 440.0;
    float previewVelocity = 100.0f;
    std::atomic<bool> previewGate { false };
    std::atomic<int> previewSerial { 0 };
    int lastPreviewSerial = -1;
    bool previewRunning = false;

    double sampleRate = 48000.0;
    int maxBlock = 1024;
};

//==============================================================================
/** Owns the audio device and exposes the transport. */
class AudioEngine : public juce::AudioIODeviceCallback
{
public:
    explicit AudioEngine (ProjectDocument& documentToUse);
    ~AudioEngine() override;

    juce::String initialise();
    void shutdown();

    juce::AudioDeviceManager& getDeviceManager() noexcept { return deviceManager; }

    /** Rebuilds the schedule from the document; call from the message thread. */
    void rebuildSchedule();

    /** The external instruments the audio thread may render. The app sets this to
        its plugin host; the unit tests leave it null and VST tracks stay silent. */
    void setLiveProvider (LiveInstrumentProvider* provider) noexcept { liveProvider = provider; }

    /** Pushes the metronome switch and the loop range to the playback engine.
        Cheap: use it when only those changed, instead of rebuilding the schedule. */
    void refreshTransportAids();

    void play();
    void stop();
    void togglePlay();
    void returnToZero();

    /** Live notes on one track: a MIDI keyboard, the on-screen keys of the MIDI
        input window, or the audition that confirms a note you just wrote. They go
        through the track's own instrument, chain and fader like any other note,
        sound whether or not the transport is running, and never reach an export. */
    void noteOnTrack (juce::int64 trackId, juce::int64 noteId, double frequencyHz,
                      float velocity, int pad = -1)
    {
        playback.noteOnTrack (trackId, noteId, frequencyHz, velocity, pad);
    }

    void noteOffTrack (juce::int64 trackId, juce::int64 noteId)
    {
        playback.noteOffTrack (trackId, noteId);
    }

    /** Live controller values (pedal / expression / mod wheel) from the keyboard:
        see `PlaybackEngine::sendControllerToTrack`. */
    void sendControllerToTrack (juce::int64 trackId, int controller, int value)
    {
        playback.sendControllerToTrack (trackId, controller, value);
    }

    int getLiveQueueOverflowCountForTest() const noexcept
    {
        return playback.getLiveQueueOverflowCountForTest();
    }

    /** How many tail cuts have been applied (see `PlaybackEngine::cutSoundingTails`):
        starting the transport cuts the tail once, pressing play while it runs cuts
        nothing. */
    int getTailCutCountForTest() const noexcept
    {
        return playback.getTailCutCountForTest();
    }

    /** Builds the schedule the generator plays: the project's tracks with their own
        instrument, insert chain and fader, but carrying only the generator's notes.
        `anchorTick` is where the performance's tick 0 sits on the project's grid, so
        the tempo map (and its tempo changes) line up with what the recorder will
        write into the clips. */
    bool isPlaying() const noexcept { return playback.isPlaying(); }
    double getPositionSeconds() const noexcept { return playback.getPositionSeconds(); }
    void setPositionSeconds (double seconds);

    double getSampleRate() const noexcept;
    int getBlockSize() const noexcept;
    juce::String getDeviceDescription() const;

    void triggerPreviewNote (const SynthParams& params, double frequencyHz, float velocity)
    {
        playback.triggerPreviewNote (params, frequencyHz, velocity);
    }

    void releasePreviewNote() { playback.releasePreviewNote(); }
    void updatePreviewParams (const SynthParams& params) { playback.updatePreviewParams (params); }

    void triggerPreviewSampleNote (const SamplerParams& params,
                                   std::shared_ptr<const LoadedSample> sample,
                                   double frequencyHz, float velocity)
    {
        playback.triggerPreviewSampleNote (params, std::move (sample), frequencyHz, velocity);
    }

    void updatePreviewSampleParams (const SamplerParams& params)
    {
        playback.updatePreviewSampleParams (params);
    }

    void triggerPreviewSubNote (const SubtractiveParams& params, double frequencyHz, float velocity)
    {
        playback.triggerPreviewSubNote (params, frequencyHz, velocity);
    }

    void updatePreviewSubParams (const SubtractiveParams& params)
    {
        playback.updatePreviewSubParams (params);
    }

    void triggerPreviewWaveNote (const WavetableParams& params, double frequencyHz, float velocity)
    {
        playback.triggerPreviewWaveNote (params, frequencyHz, velocity);
    }

    void updatePreviewWaveParams (const WavetableParams& params)
    {
        playback.updatePreviewWaveParams (params);
    }

    void updatePreviewWaveTables (std::shared_ptr<const wavetable::Wavetable> table0,
                                  std::shared_ptr<const wavetable::Wavetable> table1)
    {
        playback.updatePreviewWaveTables (std::move (table0), std::move (table1));
    }

    void triggerPreviewDrumNote (const DrumKitParams& params, int pad, float velocity)
    {
        playback.triggerPreviewDrumNote (params, pad, velocity);
    }

    void updatePreviewDrumParams (const DrumKitParams& params)
    {
        playback.updatePreviewDrumParams (params);
    }

    void triggerPreviewGranularNote (const GranularParams& params,
                                     std::shared_ptr<const LoadedSample> sample,
                                     double frequencyHz, float velocity)
    {
        playback.triggerPreviewGranularNote (params, std::move (sample), frequencyHz, velocity);
    }

    void updatePreviewGranularParams (const GranularParams& params)
    {
        playback.updatePreviewGranularParams (params);
    }

    int getActiveVoiceCount() const noexcept { return playback.getActiveVoiceCount(); }

    /** Level metering, per channel: the peak since the last read (reading clears it)
        and the flags that say a channel went past full scale. */
    PlaybackEngine::MeterReading readTrackMeter (juce::int64 trackId) noexcept
    {
        return playback.readTrackMeter (trackId);
    }

    PlaybackEngine::MeterReading readMasterMeter() noexcept
    {
        return playback.readMasterMeter();
    }

    /** The wavetable cursor's reading: see `PlaybackEngine::getWavetablePosition`. */
    float getWavetablePosition (juce::int64 trackId, int osc) const noexcept
    {
        return playback.getWavetablePosition (trackId, osc);
    }

    int getWavetableActiveVoices (juce::int64 trackId) const noexcept
    {
        return playback.getWavetableActiveVoices (trackId);
    }

    float getPreviewWavetablePosition (int osc) const noexcept
    {
        return playback.getPreviewWavetablePosition (osc);
    }

    int getPreviewWavetableActiveVoices() const noexcept
    {
        return playback.getPreviewWavetableActiveVoices();
    }

    /** The grains a granular track is sounding: see `PlaybackEngine`'s own four. */
    int getGranularGrainCount (juce::int64 trackId) const noexcept
    {
        return playback.getGranularGrainCount (trackId);
    }

    int getHybridGranularGrainCount (juce::int64 trackId, int layer) const noexcept
    {
        return playback.getHybridGranularGrainCount (trackId, layer);
    }

    float getHybridGranularGrainPosition (juce::int64 trackId, int layer,
                                          int index) const noexcept
    {
        return playback.getHybridGranularGrainPosition (trackId, layer, index);
    }

    float getHybridGranularGrainAge (juce::int64 trackId, int layer,
                                     int index) const noexcept
    {
        return playback.getHybridGranularGrainAge (trackId, layer, index);
    }

    float getHybridEnvelopePosition (juce::int64 trackId, int index) const noexcept
    {
        return playback.getHybridEnvelopePosition (trackId, index);
    }

    float getHybridEnvelopeValue (juce::int64 trackId, int index) const noexcept
    {
        return playback.getHybridEnvelopeValue (trackId, index);
    }

    float getHybridLfoPhase (juce::int64 trackId, int index) const noexcept
    {
        return playback.getHybridLfoPhase (trackId, index);
    }

    float getHybridLfoValue (juce::int64 trackId, int index) const noexcept
    {
        return playback.getHybridLfoValue (trackId, index);
    }

    int getHybridActiveVoices (juce::int64 trackId) const noexcept
    {
        return playback.getHybridActiveVoices (trackId);
    }

    float getHybridWavetablePosition (juce::int64 trackId, int layer) const noexcept
    {
        return playback.getHybridWavetablePosition (trackId, layer);
    }

    float getGranularGrainPosition (juce::int64 trackId, int index) const noexcept
    {
        return playback.getGranularGrainPosition (trackId, index);
    }

    float getGranularGrainAge (juce::int64 trackId, int index) const noexcept
    {
        return playback.getGranularGrainAge (trackId, index);
    }

    int getPreviewGranularGrainCount() const noexcept
    {
        return playback.getPreviewGranularGrainCount();
    }

    float getPreviewGranularGrainPosition (int index) const noexcept
    {
        return playback.getPreviewGranularGrainPosition (index);
    }

    float getPreviewGranularGrainAge (int index) const noexcept
    {
        return playback.getPreviewGranularGrainAge (index);
    }
    /** Renders one block down the same path the device callback uses.

        The self test drives the transport and the meters with this when it cannot
        count on an audio device being open (the meters would otherwise never move).
    */
    void renderBlockForTest (int numSamples) { playback.renderNextBlock (numSamples); }

    /** Self test only: empty the live-event queue and stop every voice. */
    void resetLiveStateForTest() { playback.resetLiveStateForTest(); }

    void audioDeviceIOCallbackWithContext (const float* const* inputChannelData,
                                           int numInputChannels,
                                           float* const* outputChannelData,
                                           int numOutputChannels,
                                           int numSamples,
                                           const juce::AudioIODeviceCallbackContext& context) override;
    void audioDeviceAboutToStart (juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;

    static std::shared_ptr<const RenderSchedule> makeSchedule (const Project& project);

private:
    ProjectDocument& document;
    juce::AudioDeviceManager deviceManager;
    PlaybackEngine playback;
    LiveInstrumentProvider* liveProvider = nullptr;
    double playbackStartSeconds = 0.0;
    bool deviceRunning = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioEngine)
};

} // namespace nidaw

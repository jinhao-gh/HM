#include "AudioEngine.h"

#include "../Core/MeterMath.h"
#include "../Core/VstSupport.h"

#include <limits>

namespace hmdaw
{

namespace
{
    constexpr double kInfinity = std::numeric_limits<double>::infinity();

    /** The audition voice always uses this note id. */
    constexpr juce::int64 kPreviewNoteId = -1;

    /** A track whose instrument has gone quiet keeps running its insert chain
        while the chain still has something to say: below this peak (about
        -100 dBFS) a block counts as silence, and after
        `kChainSilenceBlocksBeforeStop` of them in a row the chain is left alone
        until the instrument plays again. At 512 frames / 48 kHz that is ~85 ms -
        long enough not to chop a reverb tail, short enough to cost nothing. */
    constexpr float kChainSilenceThreshold = 1.0e-5f;
    constexpr int   kChainSilenceBlocksBeforeStop = 8;

    /** The frequency a track's instrument should be handed: exactly what the schedule
        says, or the nearest semitone on a VST track set to play equal temperament.

        The host's normal way of playing an exact frequency is to give the note its own
        MIDI channel and put the leftover cents on that channel's pitch bend, which
        needs a plugin that treats a bend as per channel. Instruments that bend the
        whole patch (Serum 2 and Kontakt among them) drag every sounding note onto
        whichever bend arrived last; this mode plays them semitones instead, with no
        bend at all, so a chord stays in tune. */
    float frequencyForTrack (const RenderSchedule& target, size_t trackIndex, float hz)
    {
        if (trackIndex >= target.tracks.size() || ! target.tracks[trackIndex].equalTemperament)
            return hz;

        return (float) vst::snapToSemitone ((double) hz);
    }
}

//==============================================================================
PlaybackEngine::PlaybackEngine()
{
    prepare (48000.0, 1024);
}

void PlaybackEngine::prepare (double newSampleRate, int maxBlockSize)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
    maxBlock   = juce::jlimit (32, 16384, maxBlockSize);

    output.setSize (2, maxBlock, false, true, true);
    output.clear();
    studioRenderer.prepare (sampleRate, maxBlock);
    trackBuffer.setSize (2, maxBlock, false, true, true);
    trackBuffer.clear();

    synths.resize ((size_t) kMaxTracks);
    samplePlayers.resize ((size_t) kMaxTracks);
    subtractiveSynths.resize ((size_t) kMaxTracks);
    minimoogSynths.resize ((size_t) kMaxTracks);
    gmSynths.resize ((size_t) kMaxTracks);
    wavetableSynths.resize ((size_t) kMaxTracks);
    drumKits.resize ((size_t) kMaxTracks);
    granularSynths.resize ((size_t) kMaxTracks);
    hybridSynths.resize ((size_t) kMaxTracks);
    hostedInstruments.assign ((size_t) kMaxTracks, nullptr);
    hostedEffects.assign ((size_t) kMaxTracks, {});

    for (auto& s : synths)
        s.prepare (sampleRate);

    for (auto& s : samplePlayers)
        s.prepare (sampleRate);

    for (auto& s : subtractiveSynths)
        s.prepare (sampleRate);

    for (auto& s : minimoogSynths)
        s.prepare (sampleRate);

    for (auto& s : gmSynths)
        s.prepare (sampleRate);

    for (auto& s : wavetableSynths)
        s.prepare (sampleRate);

    for (auto& s : drumKits)
        s.prepare (sampleRate);

    for (auto& s : granularSynths)
        s.prepare (sampleRate);

    for (auto& s : hybridSynths)
        s.prepare (sampleRate, maxBlock);

    trackIds.fill (0);
    cursors.fill (0);
    activeTrackCount = 0;
    lastMatched.reset();

    previewSynth.prepare (sampleRate);
    previewSampler.prepare (sampleRate);
    previewSubtractive.prepare (sampleRate);
    previewMinimoog.prepare (sampleRate);
    previewGm.prepare (sampleRate);
    previewWavetable.prepare (sampleRate);
    previewDrums.prepare (sampleRate);
    previewGranular.prepare (sampleRate);
    previewRunning = false;
    lastPreviewSerial = -1;
}

std::shared_ptr<const RenderSchedule> PlaybackEngine::getScheduleSnapshot()
{
    const juce::SpinLock::ScopedLockType lock (scheduleLock);
    return schedule;
}

void PlaybackEngine::setSchedule (std::shared_ptr<const RenderSchedule> newSchedule)
{
    const juce::SpinLock::ScopedLockType lock (scheduleLock);
    schedule = std::move (newSchedule);
}







std::shared_ptr<const music::TempoMap> PlaybackEngine::getTempoMapSnapshot()
{
    const juce::SpinLock::ScopedLockType lock (scheduleLock);
    return tempoMap;
}

void PlaybackEngine::setTempoMap (std::shared_ptr<const music::TempoMap> newTempoMap)
{
    const juce::SpinLock::ScopedLockType lock (scheduleLock);
    tempoMap = std::move (newTempoMap);
}

void PlaybackEngine::setMetronomeEnabled (bool shouldClick) noexcept
{
    metronomeOn.store (shouldClick, std::memory_order_relaxed);
}

void PlaybackEngine::setLoopRegion (double startSeconds, double endSeconds,
                                    bool shouldLoop) noexcept
{
    loopStartSeconds.store (juce::jmax (0.0, startSeconds), std::memory_order_relaxed);
    loopEndSeconds.store (juce::jmax (0.0, endSeconds), std::memory_order_relaxed);
    loopOn.store (shouldLoop, std::memory_order_relaxed);
}

void PlaybackEngine::setPlaying (bool shouldPlay)
{
    if (playing.load() == shouldPlay)
        return;

    playing.store (shouldPlay);

    // A fresh start always clicks from where the transport is.
    clickCursorValid.store (false, std::memory_order_relaxed);

    if (! shouldPlay)
        pendingReleaseVoices.store (true);
}

void PlaybackEngine::setPositionSeconds (double seconds)
{
    const auto target = juce::jmax (0.0, seconds);

    // Update the readout immediately, and ask the audio thread to re-seek.
    positionSeconds.store (target);
    pendingSeekSeconds.store (target);
}

void PlaybackEngine::setStopRewindSeconds (double seconds)
{
    // Same seek, but flagged as 停止's rewind: the audio thread skips the insert
    // chain reset for this one, so a reverb or delay tail keeps ringing out.
    pendingStopRewind.store (true);
    setPositionSeconds (seconds);
}

void PlaybackEngine::cutSoundingTails()
{
    // Starting the transport again is the one place the tail is *not* welcome: what
    // the last stop left ringing would otherwise play on top of the new pass. The
    // message thread only raises the flag - cutting voices and flushing chains has
    // to happen on the audio thread.
    pendingTailCut.store (true);
}

void PlaybackEngine::pushLiveEvent (const LiveEvent& event)
{
    const auto scope = liveFifo.write (1);

    if (scope.blockSize1 + scope.blockSize2 < 1)
    {
        // The audio thread has not caught up: dropping one note of an audition is
        // better than making the message thread wait for the audio thread.
        liveOverflow.fetch_add (1, std::memory_order_relaxed);
        return;
    }

    if (scope.blockSize1 > 0)
        liveEvents[(size_t) scope.startIndex1] = event;
    else
        liveEvents[(size_t) scope.startIndex2] = event;
}

void PlaybackEngine::noteOnTrack (juce::int64 trackId, juce::int64 noteId, double frequencyHz,
                                  float velocity, int pad)
{
    if (trackId == 0 || noteId == 0 || frequencyHz <= 0.0)
        return;

    // Into the live space, so this note can never be mistaken for a scheduled one
    // (see `kLiveNoteIdBase`): two notes that share an id share a MIDI channel, and
    // that is exactly what pulls one note's pitch around with another's.
    LiveEvent event;
    event.kind = LiveEventKind::noteOn;
    event.trackId = trackId;
    event.noteId = liveNoteIdFor (noteId);
    event.frequency = (float) frequencyHz;
    event.velocity = velocity;
    event.pad = pad;
    pushLiveEvent (event);
}

void PlaybackEngine::noteOffTrack (juce::int64 trackId, juce::int64 noteId)
{
    if (trackId == 0 || noteId == 0)
        return;

    LiveEvent event;
    event.kind = LiveEventKind::noteOff;
    event.trackId = trackId;
    event.noteId = liveNoteIdFor (noteId);
    pushLiveEvent (event);
}

void PlaybackEngine::sendControllerToTrack (juce::int64 trackId, int controller, int value)
{
    if (trackId == 0)
        return;

    LiveEvent event;
    event.kind = LiveEventKind::controller;
    event.trackId = trackId;
    event.controller = juce::jlimit (0, 127, controller);
    event.value = juce::jlimit (0, 127, value);
    pushLiveEvent (event);
}

void PlaybackEngine::applyLiveEvents (const RenderSchedule& target)
{
    const auto ready = liveFifo.getNumReady();

    if (ready <= 0)
        return;

    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    liveFifo.prepareToRead (ready, start1, size1, start2, size2);

    for (int i = 0; i < size1; ++i)
        applyLiveEvent (target, liveEvents[(size_t) (start1 + i)]);

    for (int i = 0; i < size2; ++i)
        applyLiveEvent (target, liveEvents[(size_t) (start2 + i)]);

    liveFifo.finishedRead (size1 + size2);
}

void PlaybackEngine::applyLiveEvent (const RenderSchedule& target, const LiveEvent& event)
{
    // The audio thread's own table, not the published one: a slot's instrument is
    // only ever touched from here.
    auto slot = -1;

    for (int i = 0; i < kMaxTracks; ++i)
        if (trackIds[(size_t) i] == event.trackId)
        {
            slot = i;
            break;
        }

    if (slot < 0)
        return;   // that track is gone

    const auto index = (size_t) slot;
    const auto kind = slot < (int) target.tracks.size() ? target.tracks[index].kind
                                                        : TrackKind::synth;

    // A controller message from the keyboard: the same split the drawn curves use.
    // An external instrument gets the value and applies it itself; a built-in one
    // has the engine fold it into the slot's expression / pedal state.
    if (event.kind == LiveEventKind::controller)
    {
        if (auto& hosted = hostedInstruments[index]; hosted != nullptr)
            hosted->sendController (event.controller, event.value);
        else
        {
            controllerStates[index].kind = kind;
            applyControllerToSlot (slot, event.controller, event.value);
        }

        return;
    }

    const auto isNoteOn = event.kind == LiveEventKind::noteOn;

    if (auto& hosted = hostedInstruments[index]; hosted != nullptr)
    {
        if (isNoteOn)
            hosted->noteOn (event.noteId,
                            frequencyForTrack (target, index, event.frequency),
                            event.velocity);
        else
            hosted->noteOff (event.noteId);

        return;
    }

    // A held sustain pedal swallows a live note-off exactly as it swallows a
    // scheduled one: the voice keeps ringing until the pedal comes up, which is
    // what makes the pedal audible on the built-in instruments at all.
    controllerStates[index].kind = kind;

    if (! isNoteOn && controllerStates[index].sustainDown)
    {
        controllerStates[index].sustainedNotes.push_back (event.noteId);
        return;
    }

    switch (kind)
    {
        case TrackKind::sampler:
            if (isNoteOn)
                samplePlayers[index].noteOn (event.noteId, event.frequency, event.velocity);
            else
                samplePlayers[index].noteOff (event.noteId);
            break;

        case TrackKind::subtractive:
            if (isNoteOn)
                subtractiveSynths[index].noteOn (event.noteId, event.frequency, event.velocity);
            else
                subtractiveSynths[index].noteOff (event.noteId);
            break;

        case TrackKind::minimoog:
            if (isNoteOn)
                minimoogSynths[index].noteOn (event.noteId, event.frequency, event.velocity);
            else
                minimoogSynths[index].noteOff (event.noteId);
            break;

        case TrackKind::gm:
            if (isNoteOn)
                gmSynths[index].noteOn (event.noteId, event.frequency, event.velocity);
            else
                gmSynths[index].noteOff (event.noteId);
            break;

        case TrackKind::wavetable:
            if (isNoteOn)
                wavetableSynths[index].noteOn (event.noteId, event.frequency, event.velocity);
            else
                wavetableSynths[index].noteOff (event.noteId);
            break;

        case TrackKind::drums:
            if (isNoteOn)
                drumKits[index].noteOn (event.noteId, juce::jmax (0, event.pad), event.velocity);
            else
                drumKits[index].noteOff (event.noteId);
            break;

        case TrackKind::granular:
            if (isNoteOn)
                granularSynths[index].noteOn (event.noteId, event.frequency, event.velocity);
            else
                granularSynths[index].noteOff (event.noteId);
            break;

        case TrackKind::hybrid:
            if (isNoteOn)
                hybridSynths[index].noteOn (event.noteId, event.frequency, event.velocity);
            else
                hybridSynths[index].noteOff (event.noteId);
            break;

        case TrackKind::audio:
            break;   // nothing on an audio track to play

        case TrackKind::vst:
            break;   // a VST track with no instance up has nothing to play either

        case TrackKind::synth:
            if (isNoteOn)
                synths[index].noteOn (event.noteId, event.frequency, event.velocity);
            else
                synths[index].noteOff (event.noteId);
            break;
    }
}

void PlaybackEngine::allNotesOff()
{
    for (auto& s : synths)
        s.allNotesOff (true);

    for (auto& s : samplePlayers)
        s.allNotesOff (true);

    for (auto& s : subtractiveSynths)
        s.allNotesOff (true);

    for (auto& s : wavetableSynths)
        s.allNotesOff (true);

    for (auto& s : drumKits)
        s.allNotesOff (true);

    for (auto& s : granularSynths)
        s.allNotesOff (true);

    for (auto& s : hybridSynths)
        s.allNotesOff (true);

    for (auto& s : gmSynths)
        s.allNotesOff();

    for (auto& h : hostedInstruments)
        if (h != nullptr)
            h->allNotesOff();

    for (auto& chain : hostedEffects)
        for (auto& effect : chain)
            if (effect != nullptr)
                effect->reset();

    previewSynth.allNotesOff (true);
    previewSampler.allNotesOff (true);
    previewSubtractive.allNotesOff (true);
    previewMinimoog.allNotesOff (true);
    previewGm.allNotesOff();
    previewWavetable.allNotesOff (true);
    previewDrums.allNotesOff (true);
    previewGranular.allNotesOff (true);
    previewRunning = false;
}

int PlaybackEngine::getActiveVoiceCount() const noexcept
{
    const auto preview = previewSynth.activeVoiceCount() + previewSampler.activeVoiceCount()
                             + previewSubtractive.activeVoiceCount()
                             + previewMinimoog.activeVoiceCount()
                             + previewGm.getActiveVoiceCount()
                             + previewWavetable.activeVoiceCount()
                             + previewDrums.activeVoiceCount()
                             + previewGranular.activeVoiceCount();
    int count = preview;
    int fm = 0, sampler = 0, sub = 0, mini = 0, wave = 0, drums = 0, gran = 0, hyb = 0, gm = 0;

    for (const auto& s : synths)
        fm += s.activeVoiceCount();

    for (const auto& s : samplePlayers)
        sampler += s.activeVoiceCount();

    for (const auto& s : subtractiveSynths)
        sub += s.activeVoiceCount();

    for (const auto& s : minimoogSynths)
        mini += s.activeVoiceCount();

    for (const auto& s : gmSynths)
        gm += s.getActiveVoiceCount();

    for (const auto& s : wavetableSynths)
        wave += s.activeVoiceCount();

    for (const auto& s : drumKits)
        drums += s.activeVoiceCount();

    for (const auto& s : granularSynths)
        gran += s.activeVoiceCount();

    for (const auto& s : hybridSynths)
        hyb += s.activeVoiceCount();

    count += fm + sampler + sub + mini + wave + drums + gran + hyb + gm;

    return count;
}

void PlaybackEngine::triggerPreviewNote (const SynthParams& params, double frequencyHz, float velocity)
{
    {
        const juce::SpinLock::ScopedLockType lock (previewLock);
        previewParams = params;
        previewKind = PreviewKind::fm;
        previewFrequency = music::VerticalAxis::clampHz (frequencyHz);
        previewVelocity = juce::jlimit (0.0f, 127.0f, velocity);
    }

    previewGate.store (true, std::memory_order_relaxed);
    previewSerial.fetch_add (1, std::memory_order_relaxed);
}

void PlaybackEngine::releasePreviewNote()
{
    previewGate.store (false, std::memory_order_relaxed);
}

void PlaybackEngine::updatePreviewParams (const SynthParams& params)
{
    const juce::SpinLock::ScopedLockType lock (previewLock);
    previewParams = params;
    previewKind = PreviewKind::fm;
}

void PlaybackEngine::triggerPreviewSampleNote (const SamplerParams& params,
                                               std::shared_ptr<const LoadedSample> sample,
                                               double frequencyHz, float velocity)
{
    {
        const juce::SpinLock::ScopedLockType lock (previewLock);
        previewSamplerParams = params;
        previewSample = std::move (sample);
        previewKind = PreviewKind::sampler;
        previewFrequency = music::VerticalAxis::clampHz (frequencyHz);
        previewVelocity = juce::jlimit (0.0f, 127.0f, velocity);
    }

    previewGate.store (true, std::memory_order_relaxed);
    previewSerial.fetch_add (1, std::memory_order_relaxed);
}

void PlaybackEngine::updatePreviewSampleParams (const SamplerParams& params)
{
    const juce::SpinLock::ScopedLockType lock (previewLock);
    previewSamplerParams = params;
    previewKind = PreviewKind::sampler;
}

void PlaybackEngine::triggerPreviewSubNote (const SubtractiveParams& params,
                                            double frequencyHz, float velocity)
{
    {
        const juce::SpinLock::ScopedLockType lock (previewLock);
        previewSubParams = params;
        previewKind = PreviewKind::subtractive;
        previewFrequency = music::VerticalAxis::clampHz (frequencyHz);
        previewVelocity = juce::jlimit (0.0f, 127.0f, velocity);
    }

    previewGate.store (true, std::memory_order_relaxed);
    previewSerial.fetch_add (1, std::memory_order_relaxed);
}

void PlaybackEngine::updatePreviewSubParams (const SubtractiveParams& params)
{
    const juce::SpinLock::ScopedLockType lock (previewLock);
    previewSubParams = params;
    previewKind = PreviewKind::subtractive;
}

void PlaybackEngine::triggerPreviewMinimoogNote (const MinimoogParams& params,
                                                 double frequencyHz, float velocity)
{
    {
        const juce::SpinLock::ScopedLockType lock (previewLock);
        previewMinimoogParams = params;
        previewKind = PreviewKind::minimoog;
        previewFrequency = music::VerticalAxis::clampHz (frequencyHz);
        previewVelocity = juce::jlimit (0.0f, 127.0f, velocity);
    }

    previewGate.store (true, std::memory_order_relaxed);
    previewSerial.fetch_add (1, std::memory_order_relaxed);
}

void PlaybackEngine::updatePreviewMinimoogParams (const MinimoogParams& params)
{
    const juce::SpinLock::ScopedLockType lock (previewLock);
    previewMinimoogParams = params;
    previewKind = PreviewKind::minimoog;
}


void PlaybackEngine::setGmSoundFont (std::shared_ptr<const GmSoundFont> font)
{
    gmFont = std::move (font);

    for (auto& synth : gmSynths)
        synth.setSoundFont (gmFont);

    previewGm.setSoundFont (gmFont);
}

void PlaybackEngine::triggerPreviewGmNote (const GmParams& params, double frequencyHz,
                                            float velocity)
{
    {
        const juce::SpinLock::ScopedLockType lock (previewLock);
        previewGmParams = params;
        previewKind = PreviewKind::gm;
        previewFrequency = music::VerticalAxis::clampHz (frequencyHz);
        previewVelocity = juce::jlimit (0.0f, 127.0f, velocity);
    }

    previewGate.store (true, std::memory_order_relaxed);
    previewSerial.fetch_add (1, std::memory_order_relaxed);
}

void PlaybackEngine::updatePreviewGmParams (const GmParams& params)
{
    const juce::SpinLock::ScopedLockType lock (previewLock);
    previewGmParams = params;
    previewKind = PreviewKind::gm;
}

void PlaybackEngine::triggerPreviewWaveNote (const WavetableParams& params,
                                             double frequencyHz, float velocity)
{
    {
        const juce::SpinLock::ScopedLockType lock (previewLock);
        previewWaveParams = params;
        previewKind = PreviewKind::wavetable;
        previewFrequency = music::VerticalAxis::clampHz (frequencyHz);
        previewVelocity = juce::jlimit (0.0f, 127.0f, velocity);
    }

    previewGate.store (true, std::memory_order_relaxed);
    previewSerial.fetch_add (1, std::memory_order_relaxed);
}

void PlaybackEngine::updatePreviewWaveParams (const WavetableParams& params)
{
    const juce::SpinLock::ScopedLockType lock (previewLock);
    previewWaveParams = params;
    previewKind = PreviewKind::wavetable;
}

void PlaybackEngine::updatePreviewWaveTables (std::shared_ptr<const wavetable::Wavetable> table0,
                                              std::shared_ptr<const wavetable::Wavetable> table1)
{
    const juce::SpinLock::ScopedLockType lock (previewLock);
    previewWaveTables[0] = std::move (table0);
    previewWaveTables[1] = std::move (table1);
}

void PlaybackEngine::triggerPreviewDrumNote (const DrumKitParams& params, int pad, float velocity)
{
    {
        const juce::SpinLock::ScopedLockType lock (previewLock);
        previewDrumParams = params;
        previewDrumPad = pad;
        previewKind = PreviewKind::drums;
        previewFrequency = 440.0;      // unused, but keeps the struct consistent
        previewVelocity = juce::jlimit (0.0f, 127.0f, velocity);
    }

    previewGate.store (true, std::memory_order_relaxed);
    previewSerial.fetch_add (1, std::memory_order_relaxed);
}

void PlaybackEngine::updatePreviewDrumParams (const DrumKitParams& params)
{
    const juce::SpinLock::ScopedLockType lock (previewLock);
    previewDrumParams = params;
    previewKind = PreviewKind::drums;
}

void PlaybackEngine::triggerPreviewGranularNote (const GranularParams& params,
                                                 std::shared_ptr<const LoadedSample> newSample,
                                                 double frequencyHz, float velocity)
{
    {
        const juce::SpinLock::ScopedLockType lock (previewLock);
        previewGranularParams = params;
        previewSample = std::move (newSample);
        previewKind = PreviewKind::granular;
        previewFrequency = music::VerticalAxis::clampHz (frequencyHz);
        previewVelocity = juce::jlimit (0.0f, 127.0f, velocity);
    }

    previewGate.store (true, std::memory_order_relaxed);
    previewSerial.fetch_add (1, std::memory_order_relaxed);
}

void PlaybackEngine::updatePreviewGranularParams (const GranularParams& params)
{
    const juce::SpinLock::ScopedLockType lock (previewLock);
    previewGranularParams = params;
    previewKind = PreviewKind::granular;
}

void PlaybackEngine::matchTracks (const std::shared_ptr<const RenderSchedule>& target)
{
    if (lastMatched.get() == target.get())
        return;

    const auto trackCount = juce::jmin ((int) target->tracks.size(), kMaxTracks);

    for (int i = 0; i < kMaxTracks; ++i)
    {
        const auto newId = i < trackCount ? target->tracks[(size_t) i].trackId : (juce::int64) 0;

        if (trackIds[(size_t) i] != newId)
        {
            synths[(size_t) i].reset();
            samplePlayers[(size_t) i].reset();
            subtractiveSynths[(size_t) i].reset();
            wavetableSynths[(size_t) i].reset();
            drumKits[(size_t) i].reset();
            granularSynths[(size_t) i].reset();
            hybridSynths[(size_t) i].reset();

            // A different track now sits in this slot: the expression and pedal its
            // curves had put in place belong to the track that just left.
            auto& state = controllerStates[(size_t) i];
            state.expression = 1.0f;
            state.sustainDown = false;
            state.sustainedNotes.clear();

            // A different track now sits in this slot: whatever the old one's
            // plugin was playing has to stop.
            if (auto& hosted = hostedInstruments[(size_t) i]; hosted != nullptr)
            {
                hosted->allNotesOff();
                hosted.reset();
            }

            trackIds[(size_t) i] = newId;
        }

        // The meters read by track id, so the slots publish which track they hold.
        publishedTrackIds[(size_t) i].store (newId, std::memory_order_relaxed);

        cursors[(size_t) i] = 0;
        controllerCursors[(size_t) i] = 0;
        chainSilenceBlocks[(size_t) i] = 0;
    }

    activeTrackCount = trackCount;

    const auto position = getPositionSeconds();

    for (int i = 0; i < trackCount; ++i)
    {
        const auto& info = target->tracks[(size_t) i];

        // A hosted instrument that has just been (re)loaded replaces whatever was
        // in this slot; the old one is told to stop so its notes cannot hang.
        if (hostedInstruments[(size_t) i] != info.instrument)
        {
            if (auto& previous = hostedInstruments[(size_t) i]; previous != nullptr)
                previous->allNotesOff();

            hostedInstruments[(size_t) i] = info.instrument;
        }

        // Same for the insert chain: an effect that has just left it is reset, so a
        // delay line or a reverb tail cannot keep ringing through a chain the user
        // has already changed.
        if (hostedEffects[(size_t) i] != info.effects)
        {
            for (auto& previous : hostedEffects[(size_t) i])
                if (previous != nullptr)
                    previous->reset();

            hostedEffects[(size_t) i] = info.effects;

            // A different chain: whatever "was it still ringing?" state the old one
            // had must not decide for the new one.
            chainSilenceBlocks[(size_t) i] = 0;
        }

        if (! (synths[(size_t) i].getParams() == info.synth))
            synths[(size_t) i].setParams (info.synth);

        if (! (samplePlayers[(size_t) i].getParams() == info.sampler))
            samplePlayers[(size_t) i].setParams (info.sampler);

        samplePlayers[(size_t) i].setSample (info.sample);

        if (! (subtractiveSynths[(size_t) i].getParams() == info.subtractive))
            subtractiveSynths[(size_t) i].setParams (info.subtractive);

        if (! (minimoogSynths[(size_t) i].getParams() == info.minimoog))
            minimoogSynths[(size_t) i].setParams (info.minimoog);

        if (info.kind == TrackKind::gm)
            if (! (gmSynths[(size_t) i].getParams() == info.gm))
                gmSynths[(size_t) i].setParams (info.gm);

        if (! (wavetableSynths[(size_t) i].getParams() == info.wavetable))
            wavetableSynths[(size_t) i].setParams (info.wavetable);

        // The project's own tables (null for a built-in one). Set every time: the
        // snapshot may have been rebuilt with different tables for the same sound.
        wavetableSynths[(size_t) i].setTables (info.wavetables[0], info.wavetables[1]);

        if (! (drumKits[(size_t) i].getParams() == info.drums))
            drumKits[(size_t) i].setParams (info.drums);

        if (! (granularSynths[(size_t) i].getParams() == info.granular))
            granularSynths[(size_t) i].setParams (info.granular);

        // Both sample-based instruments read the same decoded file.
        granularSynths[(size_t) i].setSample (info.sample);

        if (! (hybridSynths[(size_t) i].getParams() == info.hybrid))
            hybridSynths[(size_t) i].setParams (info.hybrid);

        hybridSynths[(size_t) i].setSources (info.hybridSamples, info.hybridWavetables);

        size_t cursor = 0;

        while (cursor < info.events.size() && info.events[cursor].timeSeconds < position)
            ++cursor;

        cursors[(size_t) i] = cursor;
    }

    if (target->studioPlan != lastStudioPlan)
    {
        studioRenderer.setPlan (target->studioPlan);
        lastStudioPlan = target->studioPlan;
    }

    lastMatched = target;
}

void PlaybackEngine::resetClickCursor (const music::TempoMap& map, double startSeconds)
{
    const auto click = music::firstClickAtOrAfter (map, map.tickAtSeconds (startSeconds));

    nextClickTick = click.tick;
    clickCursorValid.store (true, std::memory_order_relaxed);
}

void PlaybackEngine::processClicks (const music::TempoMap& map, double upToSeconds)
{
    if (! metronomeOn.load (std::memory_order_relaxed) || ! clickCursorValid.load())
        return;

    for (int guard = 0; guard < 64; ++guard)
    {
        const auto clickSeconds = map.secondsAtTick (nextClickTick);

        if (clickSeconds > upToSeconds)
            return;

        int bar = 1, beat = 1, tickInBeat = 0;
        map.tickToBarBeat (nextClickTick, bar, beat, tickInBeat);
        triggerClick (beat == 1 && tickInBeat == 0);

        nextClickTick = map.nextBeatTick (nextClickTick);
    }
}

void PlaybackEngine::triggerClick (bool accent)
{
    // Prefer a free voice; if every one is busy, steal the oldest (the first).
    auto index = 0;

    for (int i = 0; i < kMaxClickVoices; ++i)
        if (! clicks[(size_t) i].active)
        {
            index = i;
            break;
        }

    auto& voice = clicks[(size_t) index];
    voice.active = true;
    voice.phase  = 0.0;
    voice.delta  = juce::MathConstants<double>::twoPi * (accent ? 1600.0 : 1000.0) / sampleRate;
    voice.gain   = accent ? 0.34 : 0.22;
    // ~35 ms to silence, counted per sample so it does not depend on the block size.
    voice.decay  = std::exp (-1.0 / (0.012 * sampleRate));
}

void PlaybackEngine::renderClicks (int offset, int numSamples)
{
    if (numSamples <= 0 || output.getNumSamples() < offset + numSamples)
        return;

    for (auto& voice : clicks)
    {
        if (! voice.active)
            continue;

        auto* left  = output.getWritePointer (0, offset);
        auto* right = output.getNumChannels() > 1 ? output.getWritePointer (1, offset) : nullptr;

        for (int i = 0; i < numSamples; ++i)
        {
            const auto sample = (float) (std::sin (voice.phase) * voice.gain);

            left[i] += sample;

            if (right != nullptr)
                right[i] += sample;

            voice.phase += voice.delta;
            voice.gain  *= voice.decay;

            if (voice.gain < 1.0e-5)
            {
                voice.active = false;
                break;
            }
        }
    }
}

void PlaybackEngine::wrapToLoopStart (double startSeconds)
{
    // Whatever a held pedal was keeping alive has to end with the rest: the pass
    // starts again from the top, not from the middle of a chord.
    releaseSustained();
    rewindControllerCursors (startSeconds);

    releaseAllVoices (false);

    // Rewind every cursor to the first event at or after the loop start, so the
    // music plays again exactly as it did the first time round.
    if (auto target = getScheduleSnapshot())
    {
        const auto trackCount = juce::jmin ((int) target->tracks.size(), kMaxTracks);

        for (int i = 0; i < trackCount; ++i)
        {
            const auto& events = target->tracks[(size_t) i].events;

            const auto first = std::lower_bound (events.begin(), events.end(), startSeconds,
                                                 [] (const SynthEvent& event, double seconds)
                                                 { return event.timeSeconds < seconds; });
            cursors[(size_t) i] = (size_t) (first - events.begin());
        }
    }

    clickCursorValid.store (false, std::memory_order_relaxed);
    positionSeconds.store (startSeconds);
}

void PlaybackEngine::triggerEvent (int i, TrackKind kind, const SynthEvent& event,
                                   bool honourSustain, bool equalTemperament)
{
    if (event.isPitch)
    {
        switch (kind)
        {
            case TrackKind::sampler:     samplePlayers[(size_t) i].setNoteFrequency (event.noteId, event.frequency); break;
            case TrackKind::subtractive: subtractiveSynths[(size_t) i].setNoteFrequency (event.noteId, event.frequency); break;
            case TrackKind::minimoog:    minimoogSynths[(size_t) i].setNoteFrequency (event.noteId, event.frequency); break;
            case TrackKind::gm:          gmSynths[(size_t) i].setNoteFrequency (event.noteId, event.frequency); break;
            case TrackKind::wavetable:   wavetableSynths[(size_t) i].setNoteFrequency (event.noteId, event.frequency); break;
            case TrackKind::granular:    granularSynths[(size_t) i].setNoteFrequency (event.noteId, event.frequency); break;
            case TrackKind::hybrid:      hybridSynths[(size_t) i].setNoteFrequency (event.noteId, event.frequency); break;
            case TrackKind::synth:
            case TrackKind::audio:       synths[(size_t) i].setNoteFrequency (event.noteId, event.frequency); break;
            case TrackKind::drums:
            case TrackKind::vst:         break;
        }

        return;
    }

    // A held sustain pedal swallows the note-off: the voice keeps ringing until the
    // pedal comes up (processControllers sends it then). External instruments get
    // the pedal themselves and are left alone here, so nothing is applied twice.
    if (! event.isNoteOn && honourSustain && hostedInstruments[(size_t) i] == nullptr
        && controllerStates[(size_t) i].sustainDown)
    {
        controllerStates[(size_t) i].sustainedNotes.push_back (event.noteId);
        return;
    }

    switch (kind)
    {
        case TrackKind::vst:
        {
            auto& hosted = hostedInstruments[(size_t) i];

            if (hosted != nullptr)
            {
                if (event.isNoteOn)
                    hosted->noteOn (event.noteId,
                                    equalTemperament
                                        ? (float) vst::snapToSemitone ((double) event.frequency)
                                        : event.frequency,
                                    event.velocity);
                else
                    hosted->noteOff (event.noteId);
            }

            break;
        }

        case TrackKind::sampler:
            if (event.isNoteOn)
                samplePlayers[(size_t) i].noteOn (event.noteId, event.frequency,
                                                  event.velocity);
            else
                samplePlayers[(size_t) i].noteOff (event.noteId);
            break;

        case TrackKind::subtractive:
            if (event.isNoteOn)
                subtractiveSynths[(size_t) i].noteOn (event.noteId, event.frequency,
                                                      event.velocity);
            else
                subtractiveSynths[(size_t) i].noteOff (event.noteId);
            break;

        case TrackKind::minimoog:
            if (event.isNoteOn)
                minimoogSynths[(size_t) i].noteOn (event.noteId, event.frequency,
                                                   event.velocity);
            else
                minimoogSynths[(size_t) i].noteOff (event.noteId);
            break;

        case TrackKind::gm:
            if (event.isNoteOn)
                gmSynths[(size_t) i].noteOn (event.noteId, event.frequency,
                                             event.velocity);
            else
                gmSynths[(size_t) i].noteOff (event.noteId);
            break;

        case TrackKind::wavetable:
            if (event.isNoteOn)
                wavetableSynths[(size_t) i].noteOn (event.noteId, event.frequency,
                                                    event.velocity);
            else
                wavetableSynths[(size_t) i].noteOff (event.noteId);
            break;

        case TrackKind::drums:
            if (event.isNoteOn)
                drumKits[(size_t) i].noteOn (event.noteId, event.pad, event.velocity);
            else
                drumKits[(size_t) i].noteOff (event.noteId);
            break;

        case TrackKind::granular:
            if (event.isNoteOn)
                granularSynths[(size_t) i].noteOn (event.noteId, event.frequency,
                                                   event.velocity);
            else
                granularSynths[(size_t) i].noteOff (event.noteId);
            break;

        case TrackKind::hybrid:
            if (event.isNoteOn)
                hybridSynths[(size_t) i].noteOn (event.noteId, event.frequency, event.velocity);
            else
                hybridSynths[(size_t) i].noteOff (event.noteId);
            break;

        case TrackKind::synth:
        case TrackKind::audio:
            if (event.isNoteOn)
                synths[(size_t) i].noteOn (event.noteId, event.frequency, event.velocity);
            else
                synths[(size_t) i].noteOff (event.noteId);
            break;
    }
}

void PlaybackEngine::processEvents (const RenderSchedule& target, double upToSeconds)
{
    const auto trackCount = juce::jmin ((int) target.tracks.size(), kMaxTracks);

    // Controllers first: a pedal pressed exactly at a note's end has to be down
    // before that note-off is looked at.
    processControllers (target, upToSeconds);

    for (int i = 0; i < trackCount; ++i)
    {
        const auto& events = target.tracks[(size_t) i].events;
        auto& cursor = cursors[(size_t) i];

        while (cursor < events.size() && events[cursor].timeSeconds <= upToSeconds)
        {
            triggerEvent (i, target.tracks[(size_t) i].kind, events[cursor], true,
                          target.tracks[(size_t) i].equalTemperament);
            ++cursor;
        }
    }
}

void PlaybackEngine::processControllers (const RenderSchedule& target, double upToSeconds)
{
    const auto trackCount = juce::jmin ((int) target.tracks.size(), kMaxTracks);

    for (int i = 0; i < trackCount; ++i)
    {
        const auto& info = target.tracks[(size_t) i];
        auto& cursor = controllerCursors[(size_t) i];
        auto& state = controllerStates[(size_t) i];
        state.kind = info.kind;

        while (cursor < info.controllers.size()
               && info.controllers[cursor].timeSeconds <= upToSeconds)
        {
            const auto& event = info.controllers[cursor];

            // An external instrument gets the controller messages and applies them
            // itself; the built-in ones have no MIDI, so expression and the pedal
            // are handled right here.
            if (info.instrument != nullptr)
                info.instrument->sendController (event.controller, event.value);
            else
                applyControllerToSlot (i, event.controller, event.value);

            ++cursor;
        }
    }
}

void PlaybackEngine::applyControllerToSlot (int slot, int controller, int value)
{
    auto& state = controllerStates[(size_t) slot];

    if (controller == 11)
    {
        // Expression is a gain multiplier on the track's output, so the mixer's
        // meter drops with it exactly as the ear does.
        state.expression = juce::jlimit (0.0f, 1.0f, (float) value / 127.0f);
    }
    else if (controller == 64)
    {
        const auto down = value >= 64;

        if (state.sustainDown && ! down)
        {
            // The pedal comes up: everything it was holding now ends.
            const auto held = state.sustainedNotes;
            state.sustainedNotes.clear();

            for (const auto noteId : held)
            {
                SynthEvent off;
                off.noteId = noteId;
                off.isNoteOn = false;
                triggerEvent (slot, state.kind, off, false);
            }
        }

        state.sustainDown = down;
    }
}

void PlaybackEngine::releaseSustained()
{
    for (int i = 0; i < kMaxTracks; ++i)
    {
        auto& state = controllerStates[(size_t) i];
        const auto held = state.sustainedNotes;

        state.sustainedNotes.clear();
        state.sustainDown = false;

        for (const auto noteId : held)
        {
            SynthEvent off;
            off.noteId = noteId;
            off.isNoteOn = false;
            triggerEvent (i, state.kind, off, false);
        }
    }
}

void PlaybackEngine::releaseAllVoices (bool immediately)
{
    for (auto& s : synths)
        s.allNotesOff (immediately);

    for (auto& s : samplePlayers)
        s.allNotesOff (immediately);

    for (auto& s : subtractiveSynths)
        s.allNotesOff (immediately);

    for (auto& s : minimoogSynths)
        s.allNotesOff (immediately);

    for (auto& s : gmSynths)
        s.allNotesOff();

    for (auto& s : wavetableSynths)
        s.allNotesOff (immediately);

    for (auto& s : drumKits)
        s.allNotesOff (immediately);

    for (auto& s : granularSynths)
        s.allNotesOff (immediately);

    for (auto& s : hybridSynths)
        s.allNotesOff (immediately);

    for (auto& h : hostedInstruments)
        if (h != nullptr)
        {
            // `immediately` is the hard stop (project change, unload, swapping the
            // instrument in a slot). Everything else - stop, seek, loop wrap - lets
            // go the way a player would: a real note-off each, so the plugin's
            // release and its own reverb decay instead of being cut.
            if (immediately)
                h->allNotesOff();
            else
                h->releaseHeldNotes();
        }
}


void PlaybackEngine::resetLiveStateForTest()
{
    // Message thread, no device running (the offscreen self test): draining the FIFO
    // here is safe, and it is the only way a hand-rendered run ever empties it.
    const auto ready = liveFifo.getNumReady();
    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    liveFifo.prepareToRead (ready, start1, size1, start2, size2);
    liveFifo.finishedRead (size1 + size2);

    releaseAllVoices (true);

    previewGate.store (false, std::memory_order_relaxed);
    previewRunning = false;
    lastPreviewSerial = -1;
}

void PlaybackEngine::rewindControllerCursors (double atSeconds)
{
    auto target = getScheduleSnapshot();

    if (target == nullptr)
        return;

    const auto trackCount = juce::jmin ((int) target->tracks.size(), kMaxTracks);

    for (int i = 0; i < trackCount; ++i)
    {
        const auto& controllers = target->tracks[(size_t) i].controllers;

        // Coming back to a position means "nothing has been sent yet in this pass":
        // whatever the pass that just ended left the expression and the pedal on
        // belongs to that pass, not to this one. Without this a curve that sits in
        // the *second* half of a loop would hold the level it ended on across the
        // wrap, so the loop's first notes would play at the wrong level (or not at
        // all, if the curve ended at zero).
        auto& state = controllerStates[(size_t) i];
        state.expression = 1.0f;
        state.sustainDown = false;

        // One step further back than the note cursors: the value that is *in force*
        // at this position has to be applied again, not just the ones after it.
        const auto first = std::upper_bound (controllers.begin(), controllers.end(), atSeconds,
                                             [] (double seconds, const ControllerEvent& event)
                                             { return seconds < event.timeSeconds; });

        controllerCursors[(size_t) i] = first == controllers.begin()
                                            ? 0
                                            : (size_t) (first - controllers.begin() - 1);
    }
}



double PlaybackEngine::nextEventTime (const RenderSchedule& target) const
{
    const auto trackCount = juce::jmin ((int) target.tracks.size(), kMaxTracks);
    auto best = kInfinity;

    for (int i = 0; i < trackCount; ++i)
    {
        const auto& events = target.tracks[(size_t) i].events;
        const auto cursor = cursors[(size_t) i];

        if (cursor < events.size())
            best = juce::jmin (best, events[cursor].timeSeconds);
    }

    return best;
}

void PlaybackEngine::runEffectChain (const TrackRenderInfo& info, int numSamples)
{
    for (const auto& effect : info.effects)
        if (effect != nullptr)
            effect->process (trackBuffer, 0, numSamples);
}

/** Adds one track's finished signal to the output with its volume and pan.

    The inserts run before this, so the fader is post-inserts (Cubase's Inserts):
    turning a track down turns down what comes out of its effects, not what goes
    into them.
*/
void PlaybackEngine::mixTrackIntoOutput (const TrackRenderInfo& info, int offset, int numSamples)
{
    const auto slot = slotOfTrack (info.trackId);

    // A drawn expression curve (CC11) rides with the fader: it is folded in at the
    // same point, so the mixer's meter drops with what the ear hears. An external
    // instrument is sent the controller instead and applies it itself, so nothing
    // is ever applied twice.
    const auto expression = slot >= 0 && info.instrument == nullptr
                                ? controllerStates[(size_t) slot].expression
                                : 1.0f;
    const auto volume = info.volumeLinear * expression;

    const auto pan = (double) juce::jlimit (-1.0f, 1.0f, info.pan);
    const auto angle = (pan + 1.0) * 0.25 * juce::MathConstants<double>::pi;
    const auto gainLeft  = (float) (std::cos (angle) * volume);
    const auto gainRight = (float) (std::sin (angle) * volume);

    // The meter reads what reaches the output, i.e. after the track's own fader and
    // pan, and it reads each side on its own: a track panned hard left has nothing on
    // the right, and the strip's two bars have to show that. It is the loudest sample
    // since the last read, so chunked rendering cannot make the needles dip.
    const auto peakLeft  = trackBuffer.getMagnitude (0, 0, numSamples) * gainLeft;
    const auto peakRight = (trackBuffer.getNumChannels() > 1
                                ? trackBuffer.getMagnitude (1, 0, numSamples)
                                : trackBuffer.getMagnitude (0, 0, numSamples)) * gainRight;

    if (slot >= 0)
    {
        auto& storedLeft  = trackPeakLeft[(size_t) slot];
        auto& storedRight = trackPeakRight[(size_t) slot];

        if (peakLeft > storedLeft.load (std::memory_order_relaxed))
            storedLeft.store (peakLeft, std::memory_order_relaxed);

        if (peakRight > storedRight.load (std::memory_order_relaxed))
            storedRight.store (peakRight, std::memory_order_relaxed);

        if (meter::isClipping (peakLeft))
            trackClipLeft[(size_t) slot].store (true, std::memory_order_relaxed);

        if (meter::isClipping (peakRight))
            trackClipRight[(size_t) slot].store (true, std::memory_order_relaxed);
    }

    output.addFrom (0, offset, trackBuffer, 0, 0, numSamples, gainLeft);

    if (output.getNumChannels() > 1)
        output.addFrom (1, offset, trackBuffer, 1, 0, numSamples, gainRight);
}

void PlaybackEngine::renderVoices (const RenderSchedule& target, int offset, int numSamples,
                                   double bpm)
{
    const auto trackCount = juce::jmin ((int) target.tracks.size(), kMaxTracks);

    for (int i = 0; i < trackCount; ++i)
    {
        const auto& info = target.tracks[(size_t) i];

        // Every track is rendered into its own buffer at unity, run through its
        // inserts, and only then faded into the output. A track without inserts
        // used to go straight into the mix (marginally cheaper, and what the engine
        // did before inserts existed), but then the mixer had nothing to measure for
        // it: a strip's level is taken where the track meets the output, in
        // mixTrackIntoOutput. The arithmetic is the same one, in the same order.
        trackBuffer.clear (0, numSamples);

        // An external plugin is always live, whether or not the host has sent it a
        // single note: its own on-screen keyboard, its arpeggiator and its effect
        // tails all make sound from the plugin's side. Skipping it while HM had no
        // notes ringing is what made the plugin's own keyboard silent, so it is
        // processed every block from now on.
        if (info.instrument != nullptr)
        {
            info.instrument->renderAdding (trackBuffer, 0, numSamples, 1.0f, 1.0f);
            runEffectChain (info, numSamples);
            mixTrackIntoOutput (info, offset, numSamples);
            continue;
        }

        // Which built-in instrument this track kind renders. A switch with no
        // default: a newly added TrackKind makes the compiler point here instead of
        // silently rendering silence (or the FM synth) for it.
        enum class BuiltIn { none, fm, sampler, subtractive, minimoog, wavetable, drums, granular, hybrid, gm };
        auto builtIn = BuiltIn::none;

        switch (info.kind)
        {
            case TrackKind::synth:       builtIn = BuiltIn::fm; break;
            case TrackKind::sampler:     builtIn = BuiltIn::sampler; break;
            case TrackKind::subtractive: builtIn = BuiltIn::subtractive; break;
            case TrackKind::wavetable:   builtIn = BuiltIn::wavetable; break;
            case TrackKind::drums:       builtIn = BuiltIn::drums; break;
            case TrackKind::granular:    builtIn = BuiltIn::granular; break;
            case TrackKind::hybrid:      builtIn = BuiltIn::hybrid; break;
            case TrackKind::minimoog:    builtIn = BuiltIn::minimoog; break;
            case TrackKind::gm:          builtIn = BuiltIn::gm; break;
            case TrackKind::audio:       break;   // mixed in renderAudioTracks
            case TrackKind::vst:         break;   // rendered by info.instrument
        }

        auto* subtractive = builtIn == BuiltIn::subtractive
                                ? &subtractiveSynths[(size_t) i] : nullptr;
        auto* minimoog = builtIn == BuiltIn::minimoog
                             ? &minimoogSynths[(size_t) i] : nullptr;
        auto* gm = builtIn == BuiltIn::gm ? &gmSynths[(size_t) i] : nullptr;
        auto* sampler = builtIn == BuiltIn::sampler ? &samplePlayers[(size_t) i] : nullptr;
        auto* wavetable = builtIn == BuiltIn::wavetable
                              ? &wavetableSynths[(size_t) i] : nullptr;
        auto* drums = builtIn == BuiltIn::drums ? &drumKits[(size_t) i] : nullptr;
        auto* granular = builtIn == BuiltIn::granular
                             ? &granularSynths[(size_t) i] : nullptr;
        auto* fm = builtIn == BuiltIn::fm ? &synths[(size_t) i] : nullptr;
        auto* hybrid = builtIn == BuiltIn::hybrid ? &hybridSynths[(size_t) i] : nullptr;

        if (hybrid != nullptr)
            hybrid->setTempo (bpm);

        const auto active = subtractive != nullptr ? subtractive->isActive()
                                : minimoog != nullptr ? minimoog->isActive()
                                : sampler != nullptr ? sampler->isActive()
                                : wavetable != nullptr ? wavetable->isActive()
                                : drums != nullptr ? drums->isActive()
                                : granular != nullptr ? granular->isActive()
                                : hybrid != nullptr ? hybrid->isActive()
                                 : gm != nullptr ? gm->isActive()
                                 : fm != nullptr ? fm->isActive()
                                                 : false;

        if (! active)
        {
            // The instrument has nothing left to say, but a track with inserts may
            // still be ringing out: a reverb or a delay keeps producing sound after
            // the last release ends, and cutting it there is exactly what made a
            // reverb tail vanish the moment a note finished. So keep running the
            // chain on silence until it really has gone quiet - then stop, which is
            // the old behaviour (and costs nothing while a track is idle).
            if (info.effects.empty() || chainSilenceBlocks[(size_t) i] >= kChainSilenceBlocksBeforeStop)
                continue;

            trackBuffer.clear (0, numSamples);
            runEffectChain (info, numSamples);
            mixTrackIntoOutput (info, offset, numSamples);

            if (trackBuffer.getMagnitude (0, 0, numSamples) > kChainSilenceThreshold)
                chainSilenceBlocks[(size_t) i] = 0;
            else
                ++chainSilenceBlocks[(size_t) i];

            continue;
        }

        chainSilenceBlocks[(size_t) i] = 0;

        if (subtractive != nullptr)
            subtractive->renderAdding (trackBuffer, 0, numSamples, 1.0f, 1.0f);
        else if (minimoog != nullptr)
            minimoog->renderAdding (trackBuffer, 0, numSamples, 1.0f, 1.0f);
        else if (gm != nullptr)
            gm->renderAdding (trackBuffer, 0, numSamples);
        else if (drums != nullptr)
            drums->renderAdding (trackBuffer, 0, numSamples, 1.0f, 1.0f);
        else if (granular != nullptr)
            granular->renderAdding (trackBuffer, 0, numSamples, 1.0f, 1.0f);
        else if (wavetable != nullptr)
            wavetable->renderAdding (trackBuffer, 0, numSamples, 1.0f, 1.0f);
        else if (sampler != nullptr)
            sampler->renderAdding (trackBuffer, 0, numSamples, 1.0f, 1.0f);
        else if (fm != nullptr)
            fm->renderAdding (trackBuffer, 0, numSamples, 1.0f, 1.0f);
        else if (hybrid != nullptr)
            hybrid->renderAdding (trackBuffer, 0, numSamples, 1.0f, 1.0f);

        runEffectChain (info, numSamples);

        if (target.studioPlan != nullptr)
            studioRenderer.addSource (info.trackId, trackBuffer, numSamples,
                                      controllerStates[(size_t) i].expression);
        else
            mixTrackIntoOutput (info, offset, numSamples);
    }
}

void PlaybackEngine::renderAudioTracks (const RenderSchedule& target, double startSeconds,
                                        int offset, int numSamples)
{
    juce::ignoreUnused (offset);
    const auto trackCount = juce::jmin ((int) target.tracks.size(), kMaxTracks);

    for (int i = 0; i < trackCount; ++i)
    {
        const auto& info = target.tracks[(size_t) i];

        if (info.audioClips.empty())
            continue;

        // The same shape as a voice track: into its own buffer, through the chain,
        // then faded into the output (which is where its meter is read).
        trackBuffer.clear (0, numSamples);
        renderAudioClips (info.audioClips, trackBuffer, 0, numSamples, startSeconds,
                          sampleRate, 1.0f, 1.0f);
        runEffectChain (info, numSamples);

        if (target.studioPlan != nullptr)
            studioRenderer.addSource (info.trackId, trackBuffer, numSamples,
                                      controllerStates[(size_t) i].expression);
        else
            mixTrackIntoOutput (info, offset, numSamples);
    }
}

void PlaybackEngine::renderNextBlock (int numSamples)
{
    // Fades, releases and reverb tails all decay towards zero, and once they get
    // there the arithmetic can fall into denormals - which some CPUs handle in
    // hardware but others trap into a very slow path. One guard around the whole
    // block covers every instrument and every insert chain.
    const juce::ScopedNoDenormals noDenormals;

    numSamples = juce::jmin (numSamples, output.getNumSamples());

    if (numSamples <= 0)
        return;

    output.clear (0, numSamples);

    applyPendingRequests();
    applyPreview();

    // Live notes (a MIDI keyboard, the on-screen keys, a written note's audition)
    // are applied to the track's instrument before anything is rendered, so the
    // very block that carries them already has them sounding - transport or not.
    if (auto target = getScheduleSnapshot())
    {
        matchTracks (target);
        applyLiveEvents (*target);
    }

    if (playing.load (std::memory_order_relaxed))
    {
        const auto target = getScheduleSnapshot();
        const auto map = getTempoMapSnapshot();

        if (target != nullptr)
        {
            matchTracks (target);


            // A new tempo map re-arms the click grid.
            if (lastTempoMap.get() != map.get())
            {
                lastTempoMap = map;
                clickCursorValid.store (false, std::memory_order_relaxed);
            }

            const auto metronome = metronomeOn.load (std::memory_order_relaxed) && map != nullptr;
            const auto loopStart = loopStartSeconds.load (std::memory_order_relaxed);
            const auto loopEnd   = loopEndSeconds.load (std::memory_order_relaxed);
            const auto looping   = loopOn.load (std::memory_order_relaxed) && loopEnd > loopStart;

            auto position = getPositionSeconds();
            int offset = 0;

            while (offset < numSamples)
            {
                // Wrapped past the loop end: everything restarts from the top.
                if (looping && position >= loopEnd - 0.5 / sampleRate)
                {
                    wrapToLoopStart (loopStart);
                    position = loopStart;
                }

                if (metronome && ! clickCursorValid.load())
                    resetClickCursor (*map, position);

                processEvents (*target, position + 1.0e-9);


                if (metronome)
                    processClicks (*map, position + 1.0e-9);

                int chunk = numSamples - offset;
                auto nextTime = nextEventTime (*target);


                if (metronome && clickCursorValid.load())
                    nextTime = juce::jmin (nextTime, map->secondsAtTick (nextClickTick));

                if (looping)
                    nextTime = juce::jmin (nextTime, loopEnd);

                if (nextTime < kInfinity)
                {
                    const auto samplesUntil = (int) std::floor ((nextTime - position) * sampleRate);
                    chunk = juce::jlimit (1, chunk, samplesUntil);
                }

                const auto bpm = map != nullptr
                                     ? map->bpmAt (map->tickAtSeconds (position))
                                     : 120.0;
                studioRenderer.beginChunk (chunk);
                renderVoices (*target, offset, chunk, bpm);
                renderAudioTracks (*target, position, offset, chunk);
                studioRenderer.render (output, offset, chunk);

                if (metronome)
                    renderClicks (offset, chunk);

                offset += chunk;
                position += (double) chunk / sampleRate;

            }

            positionSeconds.store (position);

        }
    }
    else
    {
        // Keep rendering while stopped: released voices finish their tails instead
        // of being cut off, and the live external plugins are processed, which is
        // what lets their own on-screen keyboards be auditioned without pressing
        // play first. The schedule is matched here too, so this works from the very
        // first block rather than only after the transport has run once.
        if (auto target = getScheduleSnapshot())
        {
            matchTracks (target);
            const auto map = getTempoMapSnapshot();
            const auto position = getPositionSeconds();
            const auto bpm = map != nullptr ? map->bpmAt (map->tickAtSeconds (position))
                                            : 120.0;
            studioRenderer.beginChunk (numSamples);
            renderVoices (*target, 0, numSamples, bpm);
            studioRenderer.render (output, 0, numSamples);
        }
    }

    renderPreviewVoices (numSamples);

    // ---- the master meter watches the whole mix ----------------------------
    const auto mixPeakLeft  = output.getMagnitude (0, 0, numSamples);
    const auto mixPeakRight = output.getNumChannels() > 1
                                  ? output.getMagnitude (1, 0, numSamples) : mixPeakLeft;

    if (mixPeakLeft > masterPeakLeft.load (std::memory_order_relaxed))
        masterPeakLeft.store (mixPeakLeft, std::memory_order_relaxed);

    if (mixPeakRight > masterPeakRight.load (std::memory_order_relaxed))
        masterPeakRight.store (mixPeakRight, std::memory_order_relaxed);

    if (meter::isClipping (mixPeakLeft))
        masterClipLeft.store (true, std::memory_order_relaxed);

    if (meter::isClipping (mixPeakRight))
        masterClipRight.store (true, std::memory_order_relaxed);
}

int PlaybackEngine::slotOfTrack (juce::int64 trackId) const noexcept
{
    for (int i = 0; i < kMaxTracks; ++i)
        if (publishedTrackIds[(size_t) i].load (std::memory_order_relaxed) == trackId
            && trackId != 0)
            return i;

    return -1;
}

PlaybackEngine::MeterReading PlaybackEngine::readTrackMeter (juce::int64 trackId) noexcept
{
    MeterReading reading;
    const auto slot = slotOfTrack (trackId);

    if (slot < 0)
        return reading;

    reading.peakLeft  = trackPeakLeft[(size_t) slot].exchange (0.0f, std::memory_order_relaxed);
    reading.peakRight = trackPeakRight[(size_t) slot].exchange (0.0f, std::memory_order_relaxed);
    reading.clipLeft  = trackClipLeft[(size_t) slot].exchange (false, std::memory_order_relaxed);
    reading.clipRight = trackClipRight[(size_t) slot].exchange (false, std::memory_order_relaxed);
    return reading;
}

PlaybackEngine::MeterReading PlaybackEngine::readMasterMeter() noexcept
{
    MeterReading reading;
    reading.peakLeft  = masterPeakLeft.exchange (0.0f, std::memory_order_relaxed);
    reading.peakRight = masterPeakRight.exchange (0.0f, std::memory_order_relaxed);
    reading.clipLeft  = masterClipLeft.exchange (false, std::memory_order_relaxed);
    reading.clipRight = masterClipRight.exchange (false, std::memory_order_relaxed);
    return reading;
}

float PlaybackEngine::getWavetablePosition (juce::int64 trackId, int osc) const noexcept
{
    const auto slot = slotOfTrack (trackId);

    return slot < 0 ? 0.0f : wavetableSynths[(size_t) slot].getPublishedPosition (osc);
}

int PlaybackEngine::getWavetableActiveVoices (juce::int64 trackId) const noexcept
{
    const auto slot = slotOfTrack (trackId);

    return slot < 0 ? 0 : wavetableSynths[(size_t) slot].getPublishedVoices();
}

float PlaybackEngine::getPreviewWavetablePosition (int osc) const noexcept
{
    return previewWavetable.getPublishedPosition (osc);
}

int PlaybackEngine::getPreviewWavetableActiveVoices() const noexcept
{
    return previewWavetable.getPublishedVoices();
}

int PlaybackEngine::getGranularGrainCount (juce::int64 trackId) const noexcept
{
    const auto slot = slotOfTrack (trackId);

    return slot < 0 ? 0 : granularSynths[(size_t) slot].getPublishedGrainCount();
}

int PlaybackEngine::getHybridGranularGrainCount (juce::int64 trackId, int layer) const noexcept
{
    const auto slot = slotOfTrack (trackId);

    return slot < 0 ? 0 : hybridSynths[(size_t) slot].getGranularGrainCount (layer);
}

float PlaybackEngine::getHybridGranularGrainPosition (juce::int64 trackId, int layer,
                                                      int index) const noexcept
{
    const auto slot = slotOfTrack (trackId);

    return slot < 0 ? 0.0f
                    : hybridSynths[(size_t) slot].getGranularGrainPosition (layer, index);
}

float PlaybackEngine::getHybridGranularGrainAge (juce::int64 trackId, int layer,
                                                 int index) const noexcept
{
    const auto slot = slotOfTrack (trackId);

    return slot < 0 ? 0.0f
                    : hybridSynths[(size_t) slot].getGranularGrainAge (layer, index);
}

float PlaybackEngine::getHybridEnvelopePosition (juce::int64 trackId, int index) const noexcept
{
    const auto slot = slotOfTrack (trackId);
    return slot < 0 ? 0.0f : hybridSynths[(size_t) slot].getEnvelopePosition (index);
}

float PlaybackEngine::getHybridEnvelopeValue (juce::int64 trackId, int index) const noexcept
{
    const auto slot = slotOfTrack (trackId);
    return slot < 0 ? 0.0f : hybridSynths[(size_t) slot].getEnvelopeValue (index);
}

float PlaybackEngine::getHybridLfoPhase (juce::int64 trackId, int index) const noexcept
{
    const auto slot = slotOfTrack (trackId);
    return slot < 0 ? 0.0f : hybridSynths[(size_t) slot].getLfoPhase (index);
}

float PlaybackEngine::getHybridLfoValue (juce::int64 trackId, int index) const noexcept
{
    const auto slot = slotOfTrack (trackId);
    return slot < 0 ? 0.0f : hybridSynths[(size_t) slot].getLfoValue (index);
}

int PlaybackEngine::getHybridActiveVoices (juce::int64 trackId) const noexcept
{
    const auto slot = slotOfTrack (trackId);
    return slot < 0 ? 0 : hybridSynths[(size_t) slot].activeVoiceCount();
}

float PlaybackEngine::getHybridWavetablePosition (juce::int64 trackId,
                                                  int layer) const noexcept
{
    const auto slot = slotOfTrack (trackId);
    return slot < 0 ? 0.0f : hybridSynths[(size_t) slot].getWavetablePosition (layer);
}

float PlaybackEngine::getGranularGrainPosition (juce::int64 trackId, int index) const noexcept
{
    const auto slot = slotOfTrack (trackId);

    return slot < 0 ? 0.0f : granularSynths[(size_t) slot].getPublishedGrainPosition (index);
}

float PlaybackEngine::getGranularGrainAge (juce::int64 trackId, int index) const noexcept
{
    const auto slot = slotOfTrack (trackId);

    return slot < 0 ? 0.0f : granularSynths[(size_t) slot].getPublishedGrainAge (index);
}

int PlaybackEngine::getPreviewGranularGrainCount() const noexcept
{
    return previewGranular.getPublishedGrainCount();
}

float PlaybackEngine::getPreviewGranularGrainPosition (int index) const noexcept
{
    return previewGranular.getPublishedGrainPosition (index);
}

float PlaybackEngine::getPreviewGranularGrainAge (int index) const noexcept
{
    return previewGranular.getPublishedGrainAge (index);
}

void PlaybackEngine::applyPendingRequests()
{
    const auto seek = pendingSeekSeconds.exchange (-1.0);
    const auto releaseRequested = pendingReleaseVoices.exchange (false);
    const auto stopRewind = pendingStopRewind.exchange (false);
    const auto tailCut = pendingTailCut.exchange (false);

    if (seek >= 0.0 || releaseRequested)
    {
        // A held pedal is part of "anything already sounding": nothing may hang.
        releaseSustained();

        // Anything already sounding is released, so notes whose note-off now lies
        // in the past cannot hang forever.
        releaseAllVoices (false);
    }

    if (seek >= 0.0)
    {
        // The curves are re-read from wherever we land, so the expression and pedal
        // state are the ones the drawn lines describe at that point.
        for (auto& state : controllerStates)
            state.expression = 1.0f;

        rewindControllerCursors (juce::jmax (0.0, seek));

        // A jump cuts whatever the effects were still ringing out; without this a
        // delay line would replay the audio from before the pointer. 停止's own
        // rewind is not a jump: the tail is meant to ring out there.
        if (! stopRewind)
            for (auto& chain : hostedEffects)
                for (auto& effect : chain)
                    if (effect != nullptr)
                        effect->reset();

        lastMatched.reset();
        positionSeconds.store (juce::jmax (0.0, seek));
        // The click grid has to be re-armed wherever we land.
        clickCursorValid.store (false, std::memory_order_relaxed);
    }

    if (tailCut)
    {
        // 播放 from a stopped transport: the tail the last stop left ringing is cut
        // here rather than released, because "starting again" means the old sound is
        // over - not that it should decay politely under the new one. This runs after
        // the seek above on purpose: stop's own rewind is allowed to skip the chain
        // reset, but starting again is not.
        releaseSustained();
        releaseAllVoices (true);

        for (auto& chain : hostedEffects)
            for (auto& effect : chain)
                if (effect != nullptr)
                    effect->reset();

        // A flushed chain has nothing left to say, so let the "keep a tail alive"
        // path stop rendering it instead of counting the 8 quiet blocks again.
        for (auto& blocks : chainSilenceBlocks)
            blocks = kChainSilenceBlocksBeforeStop;

        tailCutCount.fetch_add (1, std::memory_order_relaxed);
    }
}


void PlaybackEngine::applyPreview()
{
    const auto gate = previewGate.load (std::memory_order_relaxed);

    SynthParams params;
    SamplerParams samplerParams;
    SubtractiveParams subParams;
    MinimoogParams minimoogParams;
    GmParams gmParams;
    WavetableParams waveParams;
    std::array<std::shared_ptr<const wavetable::Wavetable>, 2> waveTables;
    DrumKitParams drumParams;
    GranularParams granularParams;
    std::shared_ptr<const LoadedSample> sample;
    auto kind = PreviewKind::fm;
    double frequency = 440.0;
    float velocity = 100.0f;

    {
        const juce::SpinLock::ScopedLockType lock (previewLock);
        params = previewParams;
        samplerParams = previewSamplerParams;
        subParams = previewSubParams;
        minimoogParams = previewMinimoogParams;
        gmParams = previewGmParams;
        waveParams = previewWaveParams;
        waveTables = previewWaveTables;
        drumParams = previewDrumParams;
        granularParams = previewGranularParams;
        sample = previewSample;
        kind = previewKind;
        frequency = previewFrequency;
        velocity = previewVelocity;
    }

    if (! (previewSynth.getParams() == params))
        previewSynth.setParams (params);

    if (! (previewSampler.getParams() == samplerParams))
        previewSampler.setParams (samplerParams);

    previewSampler.setSample (sample);

    if (! (previewSubtractive.getParams() == subParams))
        previewSubtractive.setParams (subParams);

    if (! (previewMinimoog.getParams() == minimoogParams))
        previewMinimoog.setParams (minimoogParams);

    if (! (previewGm.getParams() == gmParams))
        previewGm.setParams (gmParams);

    if (! (previewWavetable.getParams() == waveParams))
        previewWavetable.setParams (waveParams);

    previewWavetable.setTables (waveTables[0], waveTables[1]);

    if (! (previewDrums.getParams() == drumParams))
        previewDrums.setParams (drumParams);

    if (! (previewGranular.getParams() == granularParams))
        previewGranular.setParams (granularParams);

    previewGranular.setSample (sample);

    const auto serial = previewSerial.load (std::memory_order_relaxed);

    if (gate && (! previewRunning || serial != lastPreviewSerial))
    {
        switch (kind)
        {
            case PreviewKind::sampler:
                previewSampler.noteOn (kPreviewNoteId, (float) frequency, velocity);
                break;

            case PreviewKind::subtractive:
                previewSubtractive.noteOn (kPreviewNoteId, (float) frequency, velocity);
                break;

            case PreviewKind::minimoog:
                previewMinimoog.noteOn (kPreviewNoteId, (float) frequency, velocity);
                break;

            case PreviewKind::gm:
                previewGm.noteOn (kPreviewNoteId, frequency, velocity);
                break;

            case PreviewKind::wavetable:
                previewWavetable.noteOn (kPreviewNoteId, (float) frequency, velocity);
                break;

            case PreviewKind::drums:
                previewDrums.noteOn (kPreviewNoteId, previewDrumPad, velocity);
                break;

            case PreviewKind::granular:
                previewGranular.noteOn (kPreviewNoteId, (float) frequency, velocity);
                break;

            case PreviewKind::fm:
                previewSynth.noteOn (kPreviewNoteId, (float) frequency, velocity);
                break;
        }

        previewRunning = true;
    }
    else if (! gate && previewRunning)
    {
        // One case per kind, and **no default**: adding a PreviewKind without
        // releasing it here is exactly how the granular audition kept sounding for
        // ever, and a switch makes the compiler ask about the new one.
        switch (kind)
        {
            case PreviewKind::fm:          previewSynth.noteOff (kPreviewNoteId); break;
            case PreviewKind::sampler:     previewSampler.noteOff (kPreviewNoteId); break;
            case PreviewKind::subtractive: previewSubtractive.noteOff (kPreviewNoteId); break;
            case PreviewKind::minimoog:    previewMinimoog.noteOff (kPreviewNoteId); break;
            case PreviewKind::gm:          previewGm.noteOff (kPreviewNoteId); break;
            case PreviewKind::wavetable:   previewWavetable.noteOff (kPreviewNoteId); break;
            case PreviewKind::drums:       previewDrums.noteOff (kPreviewNoteId); break;
            case PreviewKind::granular:    previewGranular.noteOff (kPreviewNoteId); break;
        }

        previewRunning = false;
    }

    lastPreviewSerial = serial;
}

void PlaybackEngine::renderPreviewVoices (int numSamples)
{
    if (previewSynth.isActive())
        previewSynth.renderAdding (output, 0, numSamples, 1.0f, 1.0f);

    if (previewSampler.isActive())
        previewSampler.renderAdding (output, 0, numSamples, 1.0f, 1.0f);

    if (previewSubtractive.isActive())
        previewSubtractive.renderAdding (output, 0, numSamples, 1.0f, 1.0f);

    if (previewMinimoog.isActive())
        previewMinimoog.renderAdding (output, 0, numSamples, 1.0f, 1.0f);

    if (previewGm.isActive())
        previewGm.renderAdding (output, 0, numSamples);

    if (previewWavetable.isActive())
        previewWavetable.renderAdding (output, 0, numSamples, 1.0f, 1.0f);

    if (previewDrums.isActive())
        previewDrums.renderAdding (output, 0, numSamples, 1.0f, 1.0f);

    if (previewGranular.isActive())
        previewGranular.renderAdding (output, 0, numSamples, 1.0f, 1.0f);
}

//==============================================================================
AudioEngine::AudioEngine (ProjectDocument& documentToUse)
    : document (documentToUse)
{
    playback.prepare (document.getProject().sampleRate, 1024);

    juce::String gmError;
    playback.setGmSoundFont (GmSoundFont::loadBundled (gmError));

    rebuildSchedule();
}

AudioEngine::~AudioEngine()
{
    shutdown();
}

std::shared_ptr<const RenderSchedule> AudioEngine::makeSchedule (const Project& project)
{
    return std::make_shared<const RenderSchedule> (buildSchedule (project));
}

void AudioEngine::rebuildSchedule()
{
    const auto& project = document.getProject();

    auto schedule = std::make_shared<RenderSchedule> (buildSchedule (project));

    // VST tracks get whichever instances the plugin host is holding right now. A
    // track whose plugin is still loading (or missing) simply stays silent until
    // the host rebuilds the schedule again.
    if (liveProvider != nullptr)
        for (auto& info : schedule->tracks)
        {
            const auto* track = project.findTrack (info.trackId);

            if (track == nullptr)
                continue;

            if (info.kind == TrackKind::vst)
                    info.instrument = liveProvider->instrumentFor (info.trackId, track->vst);

            // The track's insert chain, in signal order. A slot whose plugin is not
            // up yet stays null and the renderer simply skips it.
            info.effects.clear();

            for (int i = 0; i < (int) track->effects.size(); ++i)
                info.effects.push_back (liveProvider->effectFor (info.trackId, i,
                                                                 track->effects[(size_t) i]));
        }

    playback.setSchedule (std::move (schedule));
    playback.setTempoMap (std::make_shared<const music::TempoMap> (project.tempoMarkers));
    refreshTransportAids();
}





void AudioEngine::refreshTransportAids()
{
    const auto& project = document.getProject();
    const music::TempoMap map (project.tempoMarkers);

    playback.setMetronomeEnabled (project.view.metronomeEnabled);
    playback.setLoopRegion (map.secondsAtTick (project.loop.startTick),
                            map.secondsAtTick (project.loop.endTick),
                            project.loop.enabled && project.loop.isValid());
}

juce::String AudioEngine::initialise()
{
    auto error = deviceManager.initialiseWithDefaultDevices (0, 2);

    if (error.isNotEmpty())
        return error;

    juce::AudioDeviceManager::AudioDeviceSetup setup;
    deviceManager.getAudioDeviceSetup (setup);

    setup.sampleRate = document.getProject().sampleRate;
    setup.bufferSize = document.getProject().bufferSize;
    setup.useDefaultInputChannels = false;

    deviceManager.setAudioDeviceSetup (setup, true);

    deviceManager.addAudioCallback (this);
    return {};
}

void AudioEngine::shutdown()
{
    if (deviceRunning)
    {
        deviceManager.removeAudioCallback (this);
        deviceManager.closeAudioDevice();
        deviceRunning = false;
    }
}

void AudioEngine::play()
{
    // Starting from a stopped transport is a fresh pass, so the tail the last 停止
    // left ringing is cut rather than allowed to play on underneath. Pressing play
    // while it is already running is not a restart, so it cuts nothing.
    if (! isPlaying())
        playback.cutSoundingTails();

    // With the loop on, starting outside the cycle jumps straight into it - the
    // same thing every DAW does, and it stops the first pass from playing a
    // region the user has already declared "not this".
    const auto& loop = document.getProject().loop;

    if (loop.enabled && loop.isValid())
    {
        const music::TempoMap map (document.getProject().tempoMarkers);
        const auto start = map.secondsAtTick (loop.startTick);
        const auto end   = map.secondsAtTick (loop.endTick);
        const auto now   = getPositionSeconds();

        if (now < start || now >= end)
            setPositionSeconds (start);
    }

    playbackStartSeconds = getPositionSeconds();
    playback.setPlaying (true);
}

void AudioEngine::stop()
{
    playback.setPlaying (false);
    // Not a jump: this one releases the notes (with real note-offs) and rewinds the
    // cursors, but leaves the insert chains alone, so a reverb or a delay rings out
    // instead of being cut the moment the transport stops.
    playback.setStopRewindSeconds (playbackStartSeconds);
}

void AudioEngine::togglePlay()
{
    if (isPlaying())
        stop();
    else
        play();
}

void AudioEngine::returnToZero()
{
    playback.setPositionSeconds (0.0);
    playbackStartSeconds = 0.0;
}

void AudioEngine::setPositionSeconds (double seconds)
{
    playback.setPositionSeconds (seconds);
    playbackStartSeconds = juce::jmax (0.0, seconds);
}

double AudioEngine::getSampleRate() const noexcept
{
    if (auto* device = deviceManager.getCurrentAudioDevice())
        return device->getCurrentSampleRate();

    return document.getProject().sampleRate;
}

int AudioEngine::getBlockSize() const noexcept
{
    if (auto* device = deviceManager.getCurrentAudioDevice())
        return device->getCurrentBufferSizeSamples();

    return document.getProject().bufferSize;
}

juce::String AudioEngine::getDeviceDescription() const
{
    if (auto* device = deviceManager.getCurrentAudioDevice())
    {
        return device->getTypeName() + " / " + device->getName()
             + "  " + juce::String (device->getCurrentSampleRate() / 1000.0, 1) + " kHz"
             + "  " + juce::String (device->getCurrentBufferSizeSamples()) + juce::String::fromUTF8 (" 采样");
    }

    return NI_TXT ("无音频设备");
}

void AudioEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    const auto newSampleRate = device != nullptr ? device->getCurrentSampleRate() : 48000.0;
    const auto newBlockSize  = device != nullptr ? device->getCurrentBufferSizeSamples() : 512;

    playback.prepare (newSampleRate, juce::jmax (newBlockSize, 1024));

    // Plugins have to be told about the new rate before the first block arrives.
    if (liveProvider != nullptr)
        liveProvider->prepareAll (newSampleRate, juce::jmax (newBlockSize, 1024));

    deviceRunning = true;
}

void AudioEngine::audioDeviceStopped()
{
    playback.allNotesOff();
    deviceRunning = false;
}

void AudioEngine::audioDeviceIOCallbackWithContext (const float* const* inputChannelData,
                                                    int numInputChannels,
                                                    float* const* outputChannelData,
                                                    int numOutputChannels,
                                                    int numSamples,
                                                    const juce::AudioIODeviceCallbackContext& context)
{
    juce::ignoreUnused (inputChannelData, numInputChannels, context);

    for (int channel = 0; channel < numOutputChannels; ++channel)
        if (outputChannelData[channel] != nullptr)
            juce::FloatVectorOperations::clear (outputChannelData[channel], numSamples);

    if (numOutputChannels <= 0)
        return;

    playback.renderNextBlock (numSamples);

    const auto& rendered = playback.getOutputBuffer();

    if (rendered.getNumSamples() < numSamples)
        return;

    for (int channel = 0; channel < numOutputChannels; ++channel)
    {
        if (outputChannelData[channel] == nullptr)
            continue;

        const auto sourceChannel = juce::jmin (channel, rendered.getNumChannels() - 1);

        juce::FloatVectorOperations::copy (outputChannelData[channel],
                                           rendered.getReadPointer (sourceChannel),
                                           numSamples);
    }
}

} // namespace hmdaw

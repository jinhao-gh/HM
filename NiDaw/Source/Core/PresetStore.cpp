#include "PresetStore.h"
#include "Wavetables.h"
#include "WavetableTables.h"
#include "FmMath.h"
#include "ControllerMath.h"

#include <cstring>

namespace nidaw
{

namespace
{
    // The same tolerant readers the project file uses: a missing key always
    // falls back to a sensible default rather than failing the load.
    double readDouble (const juce::var& parent, const char* key, double fallback)
    {
        if (auto* o = parent.getDynamicObject())
        {
            const auto value = o->getProperty (key);

            if (! value.isVoid())
                return (double) value;
        }

        return fallback;
    }

    int readInt (const juce::var& parent, const char* key, int fallback)
    {
        return (int) readDouble (parent, key, (double) fallback);
    }

    juce::int64 readInt64 (const juce::var& parent, const char* key, juce::int64 fallback)
    {
        if (auto* o = parent.getDynamicObject())
        {
            const auto value = o->getProperty (key);

            if (! value.isVoid())
                return (juce::int64) value;
        }

        return fallback;
    }

    bool readBool (const juce::var& parent, const char* key, bool fallback)
    {
        if (auto* o = parent.getDynamicObject())
        {
            const auto value = o->getProperty (key);

            if (! value.isVoid())
                return (bool) value;
        }

        return fallback;
    }

    juce::String readString (const juce::var& parent, const char* key, const juce::String& fallback)
    {
        if (auto* o = parent.getDynamicObject())
        {
            const auto value = o->getProperty (key);

            if (! value.isVoid())
                return value.toString();
        }

        return fallback;
    }

    juce::File& defaultFileOverride()
    {
        static juce::File file;
        return file;
    }

    juce::var dotsToVar (const std::vector<ReferenceDot>& dots)
    {
        juce::Array<juce::var> array;

        for (const auto& dot : dots)
        {
            auto* object = new juce::DynamicObject();
            object->setProperty ("position", dot.position);
            object->setProperty ("colour", dot.colourIndex);
            array.add (juce::var (object));
        }

        return juce::var (array);
    }

    std::vector<ReferenceDot> dotsFromVar (const juce::var& value)
    {
        std::vector<ReferenceDot> dots;

        if (const auto* array = value.getArray())
        {
            for (const auto& entry : *array)
            {
                ReferenceDot dot;

                if (auto* object = entry.getDynamicObject())
                {
                    const auto position = object->getProperty ("position");

                    if (position.isVoid())
                        continue;

                    dot.position = (double) position - std::floor ((double) position);
                    dot.colourIndex = juce::jlimit (0, kReferencePaletteSize - 1,
                                                    (int) object->getProperty ("colour"));
                    dots.push_back (dot);
                }
            }
        }

        std::sort (dots.begin(), dots.end(),
                   [] (const ReferenceDot& a, const ReferenceDot& b) { return a.position < b.position; });

        return dots;
    }
}

//==============================================================================
// The one shape a note and a clip are written in. The project file and the chain
// library both keep clip-shaped content, so these live here rather than in either
// of them (a generator unit's own phrase is a clip).
juce::var noteToVar (const Note& n)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("id", n.id);
    o->setProperty ("startTick", n.startTick);
    o->setProperty ("durationTicks", n.durationTicks);
    o->setProperty ("frequencyHz", n.frequencyHz);
    o->setProperty ("tailFrequencyHz", n.tailFrequencyHz);
    o->setProperty ("pitchCurve", n.pitchCurve);
    o->setProperty ("tiltStart", n.tiltStart);
    o->setProperty ("tiltEnd", n.tiltEnd);
    o->setProperty ("tieFromNoteId", n.tieFromNoteId);
    o->setProperty ("velocity", n.velocity);
    o->setProperty ("muted", n.muted);
    o->setProperty ("comment", n.comment);
    o->setProperty ("pad", n.pad);
    return juce::var (o);
}

Note noteFromVar (const juce::var& v)
{
    Note n;
    n.id            = readInt64 (v, "id", 0);
    n.startTick     = juce::jmax ((juce::int64) 0, readInt64 (v, "startTick", 0));
    n.durationTicks = juce::jmax ((juce::int64) 1, readInt64 (v, "durationTicks", kTicksPerQuarter));
    n.frequencyHz   = juce::jlimit (kMinVisibleHz, kMaxVisibleHz, readDouble (v, "frequencyHz", 440.0));
    n.tailFrequencyHz = juce::jlimit (0.0, kMaxVisibleHz,
                                      readDouble (v, "tailFrequencyHz", 0.0));
    n.pitchCurve    = (float) juce::jlimit (-1.0, 1.0, readDouble (v, "pitchCurve", 0.0));
    n.tiltStart     = (float) juce::jlimit (0.0, 0.999, readDouble (v, "tiltStart", 0.0));
    n.tiltEnd       = (float) juce::jlimit ((double) n.tiltStart, 1.0,
                                            readDouble (v, "tiltEnd", 1.0));
    n.tieFromNoteId = juce::jmax ((juce::int64) 0, readInt64 (v, "tieFromNoteId", 0));
    n.velocity      = juce::jlimit (0, 127, readInt (v, "velocity", 100));
    n.muted         = readBool (v, "muted", false);
    n.comment       = readString (v, "comment", {});
    // Notes written before the drum kit existed are ordinary tonal notes.
    n.pad           = juce::jlimit (-1, kDrumPads - 1, readInt (v, "pad", -1));

    return n;
}

juce::var clipToVar (const Clip& c)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("id", c.id);
    o->setProperty ("name", c.name);
    o->setProperty ("startTick", c.startTick);
    o->setProperty ("lengthTicks", c.lengthTicks);
    o->setProperty ("loopLengthTicks", c.loopLengthTicks);
    o->setProperty ("looped", c.looped);
    o->setProperty ("audioFilePath", c.audioFilePath);
    o->setProperty ("audioFileName", c.audioFileName);
    o->setProperty ("audioOffsetSeconds", c.audioOffsetSeconds);
    o->setProperty ("audioGain", c.audioGain);

    juce::Array<juce::var> notes;

    for (const auto& n : c.notes)
        notes.add (noteToVar (n));

    o->setProperty ("notes", notes);

    // The controller curves drawn under the notes ("controllers"). Written as
    // {cc, points:[{tick, value}]} so a project file stays readable, and only the
    // lanes that actually carry something are written at all.
    juce::Array<juce::var> lanes;

    for (const auto& lane : c.controllerCurves)
    {
        if (lane.points.empty())
            continue;

        auto* laneObject = new juce::DynamicObject();
        laneObject->setProperty ("cc", controllers::clampController (lane.controller));

        juce::Array<juce::var> points;

        for (const auto& point : lane.points)
        {
            auto* pointObject = new juce::DynamicObject();
            pointObject->setProperty ("tick", point.tick);
            pointObject->setProperty ("value", controllers::clampValue (point.value));
            points.add (juce::var (pointObject));
        }

        laneObject->setProperty ("points", points);
        lanes.add (juce::var (laneObject));
    }

    o->setProperty ("controllers", lanes);
    return juce::var (o);
}

Clip clipFromVar (const juce::var& v)
{
    Clip c;
    c.id              = readInt64 (v, "id", 0);
    c.name            = readString (v, "name", NI_TXT ("片段"));
    c.startTick       = juce::jmax ((juce::int64) 0, readInt64 (v, "startTick", 0));
    c.lengthTicks     = juce::jmax (kMinClipTicks, readInt64 (v, "lengthTicks", kDefaultClipTicks));
    c.loopLengthTicks = juce::jmax (kMinClipTicks,
                                    readInt64 (v, "loopLengthTicks", c.lengthTicks));
    c.looped          = readBool (v, "looped", false);
    c.audioFilePath   = readString (v, "audioFilePath", {});
    c.audioFileName   = readString (v, "audioFileName", {});
    c.audioOffsetSeconds = juce::jmax (0.0, readDouble (v, "audioOffsetSeconds", 0.0));
    c.audioGain       = (float) juce::jlimit (0.0, 4.0, readDouble (v, "audioGain", 1.0));

    if (auto* object = v.getDynamicObject())
    {
        if (auto* noteArray = object->getProperty ("notes").getArray())
            for (const auto& nv : *noteArray)
                c.notes.push_back (noteFromVar (nv));

        // A file written before the controller lane existed has no such array: it
        // simply has no curves, which is what an empty list means everywhere else.
        if (auto* laneArray = object->getProperty ("controllers").getArray())
            for (const auto& laneVar : *laneArray)
            {
                const auto controller = controllers::clampController (readInt (laneVar, "cc", 11));

                auto& lane = controllers::laneFor (c, controller);

                if (auto* pointArray = laneVar.getDynamicObject() != nullptr
                                           ? laneVar.getDynamicObject()
                                                 ->getProperty ("points").getArray()
                                           : nullptr)
                    for (const auto& pointVar : *pointArray)
                        controllers::upsertControllerPoint (
                            lane, readInt64 (pointVar, "tick", 0),
                            readInt (pointVar, "value", 0));
            }
    }

    controllers::pruneEmptyCurves (c);
    music::pruneInvalidNoteTies (c);

    return c;
}

juce::var referenceGroupToVar (const ReferenceGroup& group)
{
    auto* object = new juce::DynamicObject();
    object->setProperty ("name", group.name);
    object->setProperty ("frameColour", group.frameColourIndex);
    object->setProperty ("dots", dotsToVar (group.dots));
    return juce::var (object);
}

ReferenceGroup referenceGroupFromVar (const juce::var& value)
{
    ReferenceGroup group;

    if (auto* object = value.getDynamicObject())
    {
        group.name = object->getProperty ("name").toString();

        const auto frameColour = object->getProperty ("frameColour");
        group.frameColourIndex = frameColour.isVoid()
                                     ? kFrameDotColourIndex
                                     : juce::jlimit (0, kReferencePaletteSize - 1, (int) frameColour);

        group.dots = dotsFromVar (object->getProperty ("dots"));
    }

    return group;
}

namespace
{
    /** One key can hold several frequencies, so a step is written as a plain number
        when there is (at most) one - which keeps the file readable and the single
        binding case exactly as it was - and as an array when the key is a stack. */
    juce::var midiFrequenciesToVar (const std::vector<double>& values)
    {
        if (values.size() <= 1u)
            return values.empty() ? juce::var (0.0) : juce::var (values.front());

        juce::Array<juce::var> chord;

        for (const auto hz : values)
            chord.add (hz);

        return juce::var (chord);
    }

    /** The other direction: a number, an array, or nothing at all (an older file).
        0 and negatives are dropped, and the rest comes back low-to-high without
        duplicates - the shape the map itself keeps anyway, so a hand-edited file
        cannot put the rest of the program out of step. */
    std::vector<double> midiFrequenciesFromVar (const juce::var& value)
    {
        std::vector<double> values;

        if (const auto* chord = value.getArray())
        {
            for (const auto& entry : *chord)
                if ((double) entry > 0.0)
                    values.push_back ((double) entry);
        }
        else if ((double) value > 0.0)
            values.push_back ((double) value);

        std::sort (values.begin(), values.end());
        values.erase (std::unique (values.begin(), values.end(),
                                   [] (double a, double b)
                                   { return std::abs (a - b) < 1.0e-6; }),
                      values.end());

        return values;
    }
}

juce::var midiInputStateToVar (const MidiInputState& state)
{
    auto* object = new juce::DynamicObject();
    object->setProperty ("presetName", state.presetName);
    object->setProperty ("mode", state.map.mode == midimap::Map::Mode::free ? 1 : 0);
    object->setProperty ("loopKeys", state.map.loopKeys);
    object->setProperty ("anchorNote", state.map.anchorNote);

    juce::Array<juce::var> tile;

    for (const auto& step : state.map.tile)
        tile.add (midiFrequenciesToVar (step));

    object->setProperty ("tile", tile);

    juce::Array<juce::var> freeNotes;

    for (const auto& [note, hz] : state.map.free)
    {
        auto* pair = new juce::DynamicObject();
        pair->setProperty ("note", note);
        pair->setProperty ("hz", midiFrequenciesToVar (hz));
        freeNotes.add (juce::var (pair));
    }

    object->setProperty ("free", freeNotes);

    // Which reference line group the window was showing, and what it looked like:
    // the copy keeps the window readable even if the source is gone.
    object->setProperty ("groupKind", state.groupSource.kind == MidiGroupSource::Kind::entry ? 1
                                    : state.groupSource.kind == MidiGroupSource::Kind::preset ? 2
                                                                                              : 0);
    object->setProperty ("groupEntryId", state.groupSource.entryId);
    object->setProperty ("groupPresetIndex", state.groupSource.presetIndex);
    object->setProperty ("groupColourIndex", state.groupColourIndex);
    object->setProperty ("group", referenceLineGroupToVar (state.group));

    // 琶音器: the mapping preset carries the arpeggiator too, because the two are
    // one thing to the player ("how this keyboard plays this project").
    object->setProperty ("arp", midiarp::settingsToVar (state.arp));
    return juce::var (object);
}

MidiInputState midiInputStateFromVar (const juce::var& value)
{
    MidiInputState state;

    if (auto* object = value.getDynamicObject())
    {
        state.presetName = object->getProperty ("presetName").toString();
        state.map.mode = (int) object->getProperty ("mode") == 1 ? midimap::Map::Mode::free
                                                                : midimap::Map::Mode::looping;
        state.map.anchorNote = juce::jlimit (midimap::Map::kLowestNote, midimap::Map::kHighestNote,
                                             (int) object->getProperty ("anchorNote"));
        midimap::setLoopKeys (state.map, (int) object->getProperty ("loopKeys"));

        if (const auto* tile = object->getProperty ("tile").getArray())
            for (int i = 0; i < (int) tile->size() && i < (int) state.map.tile.size(); ++i)
                state.map.tile[(size_t) i] = midiFrequenciesFromVar ((*tile)[i]);

        if (const auto* freeNotes = object->getProperty ("free").getArray())
            for (const auto& entry : *freeNotes)
                if (auto* pair = entry.getDynamicObject())
                {
                    auto values = midiFrequenciesFromVar (pair->getProperty ("hz"));

                    if (! values.empty())
                        state.map.free[juce::jlimit (midimap::Map::kLowestNote,
                                                     midimap::Map::kHighestNote,
                                                     (int) pair->getProperty ("note"))
                                       ] = std::move (values);
                }

        const auto kind = (int) object->getProperty ("groupKind");
        state.groupSource.kind = kind == 1 ? MidiGroupSource::Kind::entry
                                : kind == 2 ? MidiGroupSource::Kind::preset
                                            : MidiGroupSource::Kind::none;
        state.groupSource.entryId = (juce::int64) object->getProperty ("groupEntryId");
        state.groupSource.presetIndex = (int) object->getProperty ("groupPresetIndex");
        // Missing (a file written before this field existed) means "every dot keeps
        // its own colour", which is -1 - not the palette entry 0 that a plain
        // (int) cast of a missing property would give.
        if (object->hasProperty ("groupColourIndex"))
            state.groupColourIndex = juce::jlimit (-1, kReferencePaletteSize - 1,
                                                   (int) object->getProperty ("groupColourIndex"));
        state.group = referenceLineGroupFromVar (object->getProperty ("group"));

        // An older file has no "arp": settingsFromVar then says "off", which is
        // what every project written before this build was.
        state.arp = midiarp::settingsFromVar (object->getProperty ("arp"));
    }

    return state;
}

juce::var referenceLineGroupToVar (const ReferenceLineGroup& group)
{
    auto* object = new juce::DynamicObject();
    object->setProperty ("name", group.name);
    object->setProperty ("anchorHz", group.anchorHz);
    object->setProperty ("anchorPosition", group.anchorPosition);
    object->setProperty ("visible", group.visible);
    object->setProperty ("dots", dotsToVar (group.dots));
    return juce::var (object);
}

ReferenceLineGroup referenceLineGroupFromVar (const juce::var& value)
{
    ReferenceLineGroup group;

    if (auto* object = value.getDynamicObject())
    {
        group.name = object->getProperty ("name").toString();

        const auto anchorHz = object->getProperty ("anchorHz");
        group.anchorHz = anchorHz.isVoid() ? 440.0 : (double) anchorHz;

        if (group.anchorHz < kMinVisibleHz || group.anchorHz > kMaxVisibleHz)
            group.anchorHz = 440.0;

        const auto anchorPosition = object->getProperty ("anchorPosition");
        group.anchorPosition = anchorPosition.isVoid() ? 0.0 : (double) anchorPosition;

        const auto visible = object->getProperty ("visible");
        group.visible = visible.isVoid() ? true : (bool) visible;

        group.dots = dotsFromVar (object->getProperty ("dots"));
    }

    return group;
}

//==============================================================================
juce::var synthParamsToVar (const SynthParams& s)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("gain", s.gain);
    o->setProperty ("filterCutoffHz", s.filterCutoffHz);
    o->setProperty ("filterResonance", s.filterResonance);

    const auto& patch = s.patch;
    o->setProperty ("patchName", patch.name);
    o->setProperty ("algorithm", patch.algorithm);
    o->setProperty ("feedback", patch.feedback);
    o->setProperty ("lfoRate", patch.lfoRate);
    o->setProperty ("lfoDelay", patch.lfoDelay);
    o->setProperty ("lfoWaveform", patch.lfoWaveform);
    o->setProperty ("lfoPitchDepth", patch.lfoPitchDepth);
    o->setProperty ("lfoAmpDepth", patch.lfoAmpDepth);
    o->setProperty ("lfoPitchModSens", patch.lfoPitchModSens);
    o->setProperty ("lfoSync", patch.lfoSync);
    o->setProperty ("transpose", patch.transpose);
    o->setProperty ("classic", patch.classicEngine);

    juce::Array<juce::var> pitchRates, pitchLevels;

    for (int i = 0; i < 4; ++i)
    {
        pitchRates.add (patch.pitchEg.rate[i]);
        pitchLevels.add (patch.pitchEg.level[i]);
    }

    o->setProperty ("pitchEgRate", pitchRates);
    o->setProperty ("pitchEgLevel", pitchLevels);

    juce::Array<juce::var> operators;

    for (const auto& op : patch.ops)
    {
        auto* po = new juce::DynamicObject();
        po->setProperty ("ratio", op.ratio);
        po->setProperty ("mode", (int) op.mode);
        po->setProperty ("fixedHz", op.fixedHz);
        po->setProperty ("detune", op.detune);
        po->setProperty ("outputLevel", op.outputLevel);
        po->setProperty ("velSens", op.velSens);
        po->setProperty ("rateScale", op.rateScale);
        po->setProperty ("enabled", op.enabled);
        po->setProperty ("breakPoint", op.breakPoint);
        po->setProperty ("leftCurve", (int) op.leftCurve);
        po->setProperty ("rightCurve", (int) op.rightCurve);
        po->setProperty ("leftDepth", op.leftDepth);
        po->setProperty ("rightDepth", op.rightDepth);
        po->setProperty ("amSens", op.amSens);

        juce::Array<juce::var> rates, levels;

        for (int i = 0; i < 4; ++i)
        {
            rates.add (op.rate[i]);
            levels.add (op.level[i]);
        }

        po->setProperty ("rate", rates);
        po->setProperty ("level", levels);
        operators.add (juce::var (po));
    }

    o->setProperty ("operators", operators);
    return juce::var (o);
}

SynthParams synthParamsFromVar (const juce::var& v)
{
    SynthParams s;
    s.gain     = (float) juce::jlimit (0.0, 1.0, readDouble (v, "gain", 1.0));
    s.filterCutoffHz  = (float) juce::jlimit (20.0, 20000.0,
                                              readDouble (v, "filterCutoffHz", 20000.0));
    s.filterResonance = (float) juce::jlimit (0.1, 20.0,
                                              readDouble (v, "filterResonance", 0.707));

    const auto* object = v.getDynamicObject();
    const auto hasOperators = object != nullptr && ! object->getProperty ("operators").isVoid();

    if (! hasOperators)
    {
        // A project written before the FM engine: map the old two-operator
        // synth (waveform + ADSR + fmRatio/fmDepth) onto operator 1 and 2 of
        // algorithm 1, so old projects still play with their old shape.
        const auto attack  = juce::jlimit (0.0, 10.0, readDouble (v, "attack", 0.005));
        const auto decay   = juce::jlimit (0.0, 10.0, readDouble (v, "decay", 0.120));
        const auto sustain = juce::jlimit (0.0, 1.0, readDouble (v, "sustain", 0.700));
        const auto release = juce::jlimit (0.0, 10.0, readDouble (v, "release", 0.200));
        const auto oldRatio = juce::jlimit (0.0, 32.0, readDouble (v, "fmRatio", 1.0));
        const auto oldDepth = juce::jlimit (0.0, 4.0, readDouble (v, "fmDepth", 0.0));

        auto& patch = s.patch;
        patch.algorithm = 1;
        patch.name = NI_TXT ("（旧工程音色）");

        const auto sustainLevel = (int) std::lround (sustain * 99.0);

        auto& carrier = patch.ops[0];
        carrier.ratio       = 1.0f;
        carrier.outputLevel = 99;
        carrier.rate[0]  = fm::rateForSeconds (attack);
        carrier.rate[1]  = fm::rateForSeconds (decay);
        carrier.rate[2]  = fm::rateForSeconds (decay);
        carrier.rate[3]  = fm::rateForSeconds (release);
        carrier.level[0] = 99;
        carrier.level[1] = sustainLevel;
        carrier.level[2] = sustainLevel;
        carrier.level[3] = 0;

        auto& modulator = patch.ops[1];
        modulator.ratio       = (float) juce::jlimit (0.5, 32.0, oldRatio);
        modulator.outputLevel = (int) std::lround (99.0 * std::sqrt (juce::jlimit (0.0, 1.0,
                                                                                   oldDepth / 2.0)));
        modulator.rate[0]  = fm::rateForSeconds (attack);
        modulator.rate[1]  = fm::rateForSeconds (decay);
        modulator.rate[2]  = fm::rateForSeconds (decay);
        modulator.rate[3]  = fm::rateForSeconds (release);
        modulator.level[0] = 99;
        modulator.level[1] = sustainLevel;
        modulator.level[2] = sustainLevel;
        modulator.level[3] = 0;

        return s;
    }

    auto& patch = s.patch;
    patch.name        = readString (v, "patchName", {});
    patch.algorithm   = juce::jlimit (1, kFmAlgorithmCount, readInt (v, "algorithm", 1));
    patch.feedback    = juce::jlimit (0, 7, readInt (v, "feedback", 0));
    patch.lfoRate     = juce::jlimit (0, 99, readInt (v, "lfoRate", 35));
    patch.lfoDelay    = juce::jlimit (0, 99, readInt (v, "lfoDelay", 0));
    patch.lfoWaveform = juce::jlimit (0, 5, readInt (v, "lfoWaveform", 0));
    patch.lfoPitchDepth = juce::jlimit (0, 99, readInt (v, "lfoPitchDepth", 0));
    patch.lfoAmpDepth   = juce::jlimit (0, 99, readInt (v, "lfoAmpDepth", 0));
    patch.lfoPitchModSens = juce::jlimit (0, 7, readInt (v, "lfoPitchModSens", 0));
    patch.lfoSync       = readBool (v, "lfoSync", false);
    patch.transpose     = juce::jlimit (-24, 24, readInt (v, "transpose", 0));

    // Files written before the DX7 work have no `classic` flag: they keep the
    // old curves so their sound does not change under them.
    patch.classicEngine = readBool (v, "classic", false);

    if (object != nullptr)
    {
        if (const auto* rateArray = object->getProperty ("pitchEgRate").getArray())
            for (int r = 0; r < (int) rateArray->size() && r < 4; ++r)
                patch.pitchEg.rate[r] = juce::jlimit (0, 99, (int) (*rateArray)[r]);

        if (const auto* levelArray = object->getProperty ("pitchEgLevel").getArray())
            for (int l = 0; l < (int) levelArray->size() && l < 4; ++l)
                patch.pitchEg.level[l] = juce::jlimit (0, 99, (int) (*levelArray)[l]);

        if (const auto* opArray = object->getProperty ("operators").getArray())
        {
            for (int i = 0; i < (int) opArray->size() && i < kFmOperators; ++i)
            {
                const auto& ov = (*opArray)[i];
                auto& op = patch.ops[(size_t) i];

                op.ratio       = (float) juce::jlimit (0.25, 32.0, readDouble (ov, "ratio", 1.0));
                op.mode        = readInt (ov, "mode", 0) == 1 ? FmOscMode::fixed : FmOscMode::ratio;
                op.fixedHz     = (float) juce::jlimit (1.0, 10000.0, readDouble (ov, "fixedHz", 1000.0));
                op.detune      = (float) juce::jlimit (-1.0, 1.0, readDouble (ov, "detune", 0.0));
                op.outputLevel = juce::jlimit (0, 99, readInt (ov, "outputLevel", 99));
                op.velSens     = juce::jlimit (0, 7, readInt (ov, "velSens", 0));
                op.rateScale   = juce::jlimit (0, 7, readInt (ov, "rateScale", 0));
                op.breakPoint  = juce::jlimit (0, 127, readInt (ov, "breakPoint", 48));
                op.leftCurve   = (FmScaleCurve) juce::jlimit (0, 3, readInt (ov, "leftCurve", 0));
                op.rightCurve  = (FmScaleCurve) juce::jlimit (0, 3, readInt (ov, "rightCurve", 0));
                op.leftDepth   = juce::jlimit (0, 99, readInt (ov, "leftDepth", 0));
                op.rightDepth  = juce::jlimit (0, 99, readInt (ov, "rightDepth", 0));
                op.amSens      = juce::jlimit (0, 3, readInt (ov, "amSens", 0));
                // A file written before operator switches existed keeps every
                // operator on, so its sound is untouched.
                op.enabled     = readBool (ov, "enabled", true);

                if (const auto* ovObject = ov.getDynamicObject())
                {
                    if (const auto* rateArray = ovObject->getProperty ("rate").getArray())
                        for (int r = 0; r < (int) rateArray->size() && r < 4; ++r)
                            op.rate[r] = juce::jlimit (0, 99, (int) (*rateArray)[r]);

                    if (const auto* levelArray = ovObject->getProperty ("level").getArray())
                        for (int l = 0; l < (int) levelArray->size() && l < 4; ++l)
                            op.level[l] = juce::jlimit (0, 99, (int) (*levelArray)[l]);
                }
            }
        }
    }

    return s;
}

juce::var samplerParamsToVar (const SamplerParams& s)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("name", s.name);
    o->setProperty ("filePath", s.filePath);
    o->setProperty ("fileName", s.fileName);
    o->setProperty ("rootHz", s.rootHz);
    o->setProperty ("looped", s.looped);
    o->setProperty ("sampleStart", s.sampleStart);
    o->setProperty ("sampleEnd", s.sampleEnd);
    o->setProperty ("loopStart", s.loopStart);
    o->setProperty ("loopEnd", s.loopEnd);
    o->setProperty ("crossfade", s.crossfade);
    o->setProperty ("attack", s.attack);
    o->setProperty ("decay", s.decay);
    o->setProperty ("sustain", s.sustain);
    o->setProperty ("release", s.release);
    o->setProperty ("lfoRateHz", s.lfoRateHz);
    o->setProperty ("lfoPitchCents", s.lfoPitchCents);
    o->setProperty ("lfoAmpDepth", s.lfoAmpDepth);
    o->setProperty ("gain", s.gain);
    o->setProperty ("filterCutoffHz", s.filterCutoffHz);
    o->setProperty ("filterResonance", s.filterResonance);
    return juce::var (o);
}

SamplerParams samplerParamsFromVar (const juce::var& v)
{
    SamplerParams s;
    s.name       = readString (v, "name", {});
    s.filePath   = readString (v, "filePath", {});
    s.fileName   = readString (v, "fileName", {});
    s.rootHz     = juce::jlimit (kMinVisibleHz, kMaxVisibleHz, readDouble (v, "rootHz", 440.0));
    s.looped     = readBool (v, "looped", false);
    s.sampleStart = juce::jlimit (0.0, 0.99, readDouble (v, "sampleStart", 0.0));
    s.sampleEnd   = juce::jlimit (0.01, 1.0, readDouble (v, "sampleEnd", 1.0));
    s.loopStart  = juce::jlimit (0.0, 0.99, readDouble (v, "loopStart", 0.0));
    s.loopEnd    = juce::jlimit (0.01, 1.0, readDouble (v, "loopEnd", 1.0));
    s.crossfade  = juce::jlimit (0.0, 1.0, readDouble (v, "crossfade", 0.0));
    s.attack     = (float) juce::jlimit (0.0, 10.0, readDouble (v, "attack", 0.005));
    s.decay      = (float) juce::jlimit (0.0, 10.0, readDouble (v, "decay", 0.100));
    s.sustain    = (float) juce::jlimit (0.0, 1.0, readDouble (v, "sustain", 0.900));
    s.release    = (float) juce::jlimit (0.0, 10.0, readDouble (v, "release", 0.200));
    s.lfoRateHz     = (float) juce::jlimit (0.0, 30.0, readDouble (v, "lfoRateHz", 0.0));
    s.lfoPitchCents = (float) juce::jlimit (0.0, 1200.0, readDouble (v, "lfoPitchCents", 0.0));
    s.lfoAmpDepth   = (float) juce::jlimit (0.0, 1.0, readDouble (v, "lfoAmpDepth", 0.0));
    s.gain       = (float) juce::jlimit (0.0, 1.0, readDouble (v, "gain", 1.0));
    s.filterCutoffHz  = (float) juce::jlimit (20.0, 20000.0,
                                              readDouble (v, "filterCutoffHz", 20000.0));
    s.filterResonance = (float) juce::jlimit (0.1, 20.0,
                                              readDouble (v, "filterResonance", 0.707));
    return s;
}

//==============================================================================
juce::var subtractiveParamsToVar (const SubtractiveParams& s)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("name", s.name);

    juce::Array<juce::var> oscs;

    for (const auto& osc : s.oscs)
    {
        auto* oo = new juce::DynamicObject();
        oo->setProperty ("enabled", osc.enabled);
        oo->setProperty ("wave", (int) osc.wave);
        oo->setProperty ("octave", osc.octave);
        oo->setProperty ("semitone", osc.semitone);
        oo->setProperty ("detuneCents", osc.detuneCents);
        oo->setProperty ("level", osc.level);
        oo->setProperty ("pulseWidth", osc.pulseWidth);
        oscs.add (juce::var (oo));
    }

    o->setProperty ("oscs", oscs);
    o->setProperty ("noiseLevel", s.noiseLevel);

    o->setProperty ("filterMode", (int) s.filterMode);
    o->setProperty ("filterSlopeDb", s.filterSlopeDb);
    o->setProperty ("filterCutoffHz", s.filterCutoffHz);
    o->setProperty ("filterResonance", s.filterResonance);
    o->setProperty ("filterEnvAmount", s.filterEnvAmount);
    o->setProperty ("filterKeyTrack", s.filterKeyTrack);

    o->setProperty ("ampAttack", s.ampAttack);
    o->setProperty ("ampDecay", s.ampDecay);
    o->setProperty ("ampSustain", s.ampSustain);
    o->setProperty ("ampRelease", s.ampRelease);

    o->setProperty ("filterAttack", s.filterAttack);
    o->setProperty ("filterDecay", s.filterDecay);
    o->setProperty ("filterSustain", s.filterSustain);
    o->setProperty ("filterRelease", s.filterRelease);

    o->setProperty ("lfoRateHz", s.lfoRateHz);
    o->setProperty ("lfoWaveform", s.lfoWaveform);
    o->setProperty ("lfoPitchCents", s.lfoPitchCents);
    o->setProperty ("lfoCutoffDepth", s.lfoCutoffDepth);
    o->setProperty ("lfoAmpDepth", s.lfoAmpDepth);

    o->setProperty ("gain", s.gain);
    return juce::var (o);
}

SubtractiveParams subtractiveParamsFromVar (const juce::var& v)
{
    SubtractiveParams s;
    s.name = readString (v, "name", {});

    if (auto* oscArray = v.getDynamicObject() != nullptr
                             ? v.getDynamicObject()->getProperty ("oscs").getArray()
                             : nullptr)
    {
        for (int i = 0; i < (int) s.oscs.size() && i < oscArray->size(); ++i)
        {
            const auto& ov = (*oscArray)[i];
            auto& osc = s.oscs[(size_t) i];

            osc.enabled     = readBool (ov, "enabled", true);
            osc.wave        = (OscWave) juce::jlimit (0, 4, readInt (ov, "wave", 0));
            osc.octave      = juce::jlimit (-2, 2, readInt (ov, "octave", 0));
            osc.semitone    = juce::jlimit (-12, 12, readInt (ov, "semitone", 0));
            osc.detuneCents = (float) juce::jlimit (-50.0, 50.0,
                                                    readDouble (ov, "detuneCents", 0.0));
            osc.level       = (float) juce::jlimit (0.0, 1.0, readDouble (ov, "level", 1.0));
            osc.pulseWidth  = (float) juce::jlimit (0.05, 0.95,
                                                    readDouble (ov, "pulseWidth", 0.5));
        }
    }

    s.noiseLevel = (float) juce::jlimit (0.0, 1.0, readDouble (v, "noiseLevel", 0.0));

    s.filterMode      = (FilterMode) juce::jlimit (0, 2, readInt (v, "filterMode", 0));
    s.filterSlopeDb   = readInt (v, "filterSlopeDb", 24) >= 24 ? 24 : 12;
    s.filterCutoffHz  = (float) juce::jlimit (20.0, 20000.0,
                                              readDouble (v, "filterCutoffHz", 2000.0));
    s.filterResonance = (float) juce::jlimit (0.1, 20.0,
                                              readDouble (v, "filterResonance", 0.707));
    s.filterEnvAmount = (float) juce::jlimit (-1.0, 1.0,
                                              readDouble (v, "filterEnvAmount", 0.5));
    s.filterKeyTrack  = (float) juce::jlimit (0.0, 1.0, readDouble (v, "filterKeyTrack", 0.0));

    s.ampAttack  = (float) juce::jlimit (0.0, 10.0, readDouble (v, "ampAttack", 0.005));
    s.ampDecay   = (float) juce::jlimit (0.0, 10.0, readDouble (v, "ampDecay", 0.200));
    s.ampSustain = (float) juce::jlimit (0.0, 1.0, readDouble (v, "ampSustain", 0.700));
    s.ampRelease = (float) juce::jlimit (0.0, 10.0, readDouble (v, "ampRelease", 0.300));

    s.filterAttack  = (float) juce::jlimit (0.0, 10.0, readDouble (v, "filterAttack", 0.005));
    s.filterDecay   = (float) juce::jlimit (0.0, 10.0, readDouble (v, "filterDecay", 0.300));
    s.filterSustain = (float) juce::jlimit (0.0, 1.0, readDouble (v, "filterSustain", 0.400));
    s.filterRelease = (float) juce::jlimit (0.0, 10.0, readDouble (v, "filterRelease", 0.300));

    s.lfoRateHz       = (float) juce::jlimit (0.0, 20.0, readDouble (v, "lfoRateHz", 5.0));
    s.lfoWaveform     = juce::jlimit (0, 3, readInt (v, "lfoWaveform", 0));
    s.lfoPitchCents   = (float) juce::jlimit (0.0, 100.0, readDouble (v, "lfoPitchCents", 0.0));
    s.lfoCutoffDepth  = (float) juce::jlimit (0.0, 1.0, readDouble (v, "lfoCutoffDepth", 0.0));
    s.lfoAmpDepth     = (float) juce::jlimit (0.0, 1.0, readDouble (v, "lfoAmpDepth", 0.0));

    s.gain = (float) juce::jlimit (0.0, 1.0, readDouble (v, "gain", 1.0));
    return s;
}

//==============================================================================
juce::var minimoogParamsToVar (const MinimoogParams& s)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("name", s.name);

    juce::Array<juce::var> oscs;

    for (const auto& osc : s.oscs)
    {
        auto* oo = new juce::DynamicObject();
        oo->setProperty ("enabled", osc.enabled);
        oo->setProperty ("wave", (int) osc.wave);
        oo->setProperty ("range", (int) osc.range);
        oo->setProperty ("tuneSemitones", osc.tuneSemitones);
        oo->setProperty ("level", osc.level);
        oscs.add (juce::var (oo));
    }

    o->setProperty ("oscs", oscs);
    o->setProperty ("noiseLevel", s.noiseLevel);
    o->setProperty ("noisePink", s.noisePink);
    o->setProperty ("masterTuneCents", s.masterTuneCents);
    o->setProperty ("polyphony", s.polyphony);
    o->setProperty ("velocitySensitivity", s.velocitySensitivity);

    o->setProperty ("glideSeconds", s.glideSeconds);
    o->setProperty ("glideOn", s.glideOn);
    o->setProperty ("modMix", s.modMix);
    o->setProperty ("modDepth", s.modDepth);
    o->setProperty ("oscModOn", s.oscModOn);
    o->setProperty ("filterModOn", s.filterModOn);
    o->setProperty ("osc3Control", s.osc3Control);

    o->setProperty ("filterCutoffHz", s.filterCutoffHz);
    o->setProperty ("filterEmphasis", s.filterEmphasis);
    o->setProperty ("filterContourOctaves", s.filterContourOctaves);
    o->setProperty ("filterKeyTrack1", s.filterKeyTrack1);
    o->setProperty ("filterKeyTrack2", s.filterKeyTrack2);

    o->setProperty ("filterAttack", s.filterAttack);
    o->setProperty ("filterDecay", s.filterDecay);
    o->setProperty ("filterDecayShort", s.filterDecayShort);
    o->setProperty ("filterSustainOn", s.filterSustainOn);

    o->setProperty ("ampAttack", s.ampAttack);
    o->setProperty ("ampDecay", s.ampDecay);
    o->setProperty ("ampDecayShort", s.ampDecayShort);
    o->setProperty ("ampSustainOn", s.ampSustainOn);
    o->setProperty ("releaseOn", s.releaseOn);
    o->setProperty ("volume", s.volume);

    return juce::var (o);
}

MinimoogParams minimoogParamsFromVar (const juce::var& v)
{
    MinimoogParams s;
    s.name = readString (v, "name", {});

    if (auto* oscArray = v.getDynamicObject() != nullptr
                             ? v.getDynamicObject()->getProperty ("oscs").getArray()
                             : nullptr)
    {
        for (int i = 0; i < (int) s.oscs.size() && i < oscArray->size(); ++i)
        {
            const auto& ov = (*oscArray)[i];
            auto& osc = s.oscs[(size_t) i];

            osc.enabled = readBool (ov, "enabled", true);
            osc.wave = (MinimoogWave) juce::jlimit (0, 5, readInt (ov, "wave", 2));
            osc.range = (MinimoogRange) juce::jlimit (0, 5, readInt (ov, "range", 3));
            osc.tuneSemitones = (float) juce::jlimit (-7.0, 7.0,
                                                      readDouble (ov, "tuneSemitones", 0.0));
            osc.level = (float) juce::jlimit (0.0, 1.0, readDouble (ov, "level", 0.8));
        }
    }

    s.noiseLevel = (float) juce::jlimit (0.0, 1.0, readDouble (v, "noiseLevel", 0.0));
    s.noisePink = readBool (v, "noisePink", false);
    s.masterTuneCents = (float) juce::jlimit (-50.0, 50.0,
                                              readDouble (v, "masterTuneCents", 0.0));
    s.polyphony = juce::jlimit (-1, kMaxMinimoogVoices, readInt (v, "polyphony", 0));
    s.velocitySensitivity = (float) juce::jlimit (0.0, 1.0,
                                                  readDouble (v, "velocitySensitivity", 0.7));

    s.glideSeconds = (float) juce::jlimit (0.0, 2.0, readDouble (v, "glideSeconds", 0.0));
    s.glideOn = readBool (v, "glideOn", false);
    s.modMix = (float) juce::jlimit (0.0, 1.0, readDouble (v, "modMix", 0.0));
    s.modDepth = (float) juce::jlimit (0.0, 1.0, readDouble (v, "modDepth", 0.0));
    s.oscModOn = readBool (v, "oscModOn", false);
    s.filterModOn = readBool (v, "filterModOn", false);
    s.osc3Control = readBool (v, "osc3Control", true);

    s.filterCutoffHz = (float) juce::jlimit (10.0, 20000.0,
                                             readDouble (v, "filterCutoffHz", 800.0));
    s.filterEmphasis = (float) juce::jlimit (0.0, 1.0, readDouble (v, "filterEmphasis", 0.0));
    s.filterContourOctaves = (float) juce::jlimit (0.0, 8.0,
                                                   readDouble (v, "filterContourOctaves", 4.0));
    s.filterKeyTrack1 = readBool (v, "filterKeyTrack1", false);
    s.filterKeyTrack2 = readBool (v, "filterKeyTrack2", false);

    s.filterAttack = (float) juce::jlimit (0.001, 20.0, readDouble (v, "filterAttack", 0.005));
    s.filterDecay = (float) juce::jlimit (0.001, 20.0, readDouble (v, "filterDecay", 0.4));
    s.filterDecayShort = readBool (v, "filterDecayShort", false);
    s.filterSustainOn = readBool (v, "filterSustainOn", true);

    s.ampAttack = (float) juce::jlimit (0.001, 20.0, readDouble (v, "ampAttack", 0.005));
    s.ampDecay = (float) juce::jlimit (0.001, 20.0, readDouble (v, "ampDecay", 0.3));
    s.ampDecayShort = readBool (v, "ampDecayShort", false);
    s.ampSustainOn = readBool (v, "ampSustainOn", true);
    s.releaseOn = readBool (v, "releaseOn", true);
    s.volume = (float) juce::jlimit (0.0, 1.0, readDouble (v, "volume", 0.7));

    return s;
}

//==============================================================================
juce::var wavetableParamsToVar (const WavetableParams& s)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("name", s.name);

    juce::Array<juce::var> oscs;

    for (const auto& osc : s.oscs)
    {
        auto* oo = new juce::DynamicObject();
        oo->setProperty ("enabled", osc.enabled);
        oo->setProperty ("tableIndex", osc.tableIndex);
        oo->setProperty ("customTableId", osc.customTableId);
        oo->setProperty ("position", osc.position);
        oo->setProperty ("unisonVoices", osc.unisonVoices);
        oo->setProperty ("unisonDetuneCents", osc.unisonDetuneCents);
        oo->setProperty ("positionDiscrete", osc.positionDiscrete);
        oo->setProperty ("octave", osc.octave);
        oo->setProperty ("semitone", osc.semitone);
        oo->setProperty ("detuneCents", osc.detuneCents);
        oo->setProperty ("level", osc.level);
        oscs.add (juce::var (oo));
    }

    o->setProperty ("oscs", oscs);
    o->setProperty ("noiseLevel", s.noiseLevel);

    o->setProperty ("filterMode", (int) s.filterMode);
    o->setProperty ("filterSlopeDb", s.filterSlopeDb);
    o->setProperty ("filterCutoffHz", s.filterCutoffHz);
    o->setProperty ("filterResonance", s.filterResonance);
    o->setProperty ("filterKeyTrack", s.filterKeyTrack);

    o->setProperty ("ampAttack", s.ampAttack);
    o->setProperty ("ampDecay", s.ampDecay);
    o->setProperty ("ampSustain", s.ampSustain);
    o->setProperty ("ampRelease", s.ampRelease);

    o->setProperty ("posAttack", s.posAttack);
    o->setProperty ("posDecay", s.posDecay);
    o->setProperty ("posSustain", s.posSustain);
    o->setProperty ("posRelease", s.posRelease);
    o->setProperty ("posEnvAmount", s.posEnvAmount);

    o->setProperty ("lfoRateHz", s.lfoRateHz);
    o->setProperty ("lfoWaveform", s.lfoWaveform);
    o->setProperty ("lfoPositionDepth", s.lfoPositionDepth);
    o->setProperty ("lfoPitchCents", s.lfoPitchCents);
    o->setProperty ("lfoCutoffDepth", s.lfoCutoffDepth);
    o->setProperty ("lfoAmpDepth", s.lfoAmpDepth);

    o->setProperty ("gain", s.gain);
    return juce::var (o);
}

WavetableParams wavetableParamsFromVar (const juce::var& v)
{
    WavetableParams s;
    s.name = readString (v, "name", {});

    const auto tableCount = juce::jmax (1, wavetable::builtInTableCount());

    if (auto* oscArray = v.getDynamicObject() != nullptr
                             ? v.getDynamicObject()->getProperty ("oscs").getArray()
                             : nullptr)
    {
        for (int i = 0; i < (int) s.oscs.size() && i < oscArray->size(); ++i)
        {
            const auto& ov = (*oscArray)[i];
            auto& osc = s.oscs[(size_t) i];

            osc.enabled     = readBool (ov, "enabled", true);
            osc.tableIndex  = juce::jlimit (0, tableCount - 1, readInt (ov, "tableIndex", 0));
            // Missing (every project written before custom tables) means "a built-in
            // table": 0 is exactly that.
            osc.customTableId = juce::jmax ((juce::int64) 0,
                                            readInt64 (ov, "customTableId", 0));
            osc.position    = (float) juce::jlimit (0.0, 1.0, readDouble (ov, "position", 0.0));
            osc.unisonVoices = juce::jlimit (1, 16, readInt (ov, "unisonVoices", 1));
            osc.unisonDetuneCents = (float) juce::jlimit (
                0.0, 100.0, readDouble (ov, "unisonDetuneCents", 12.0));
            osc.positionDiscrete = readBool (ov, "positionDiscrete", false);
            osc.octave      = juce::jlimit (-2, 2, readInt (ov, "octave", 0));
            osc.semitone    = juce::jlimit (-12, 12, readInt (ov, "semitone", 0));
            osc.detuneCents = (float) juce::jlimit (-50.0, 50.0,
                                                    readDouble (ov, "detuneCents", 0.0));
            osc.level       = (float) juce::jlimit (0.0, 1.0, readDouble (ov, "level", 1.0));
        }
    }

    s.noiseLevel = (float) juce::jlimit (0.0, 1.0, readDouble (v, "noiseLevel", 0.0));

    s.filterMode      = (FilterMode) juce::jlimit (0, 2, readInt (v, "filterMode", 0));
    s.filterSlopeDb   = readInt (v, "filterSlopeDb", 24) >= 24 ? 24 : 12;
    s.filterCutoffHz  = (float) juce::jlimit (20.0, 20000.0,
                                              readDouble (v, "filterCutoffHz", 8000.0));
    s.filterResonance = (float) juce::jlimit (0.1, 20.0,
                                              readDouble (v, "filterResonance", 0.707));
    s.filterKeyTrack  = (float) juce::jlimit (0.0, 1.0, readDouble (v, "filterKeyTrack", 0.0));

    s.ampAttack  = (float) juce::jlimit (0.0, 10.0, readDouble (v, "ampAttack", 0.005));
    s.ampDecay   = (float) juce::jlimit (0.0, 10.0, readDouble (v, "ampDecay", 0.200));
    s.ampSustain = (float) juce::jlimit (0.0, 1.0, readDouble (v, "ampSustain", 0.700));
    s.ampRelease = (float) juce::jlimit (0.0, 10.0, readDouble (v, "ampRelease", 0.300));

    s.posAttack    = (float) juce::jlimit (0.0, 10.0, readDouble (v, "posAttack", 0.005));
    s.posDecay     = (float) juce::jlimit (0.0, 10.0, readDouble (v, "posDecay", 0.600));
    s.posSustain   = (float) juce::jlimit (0.0, 1.0, readDouble (v, "posSustain", 0.0));
    s.posRelease   = (float) juce::jlimit (0.0, 10.0, readDouble (v, "posRelease", 0.400));
    s.posEnvAmount = (float) juce::jlimit (-1.0, 1.0, readDouble (v, "posEnvAmount", 0.5));

    s.lfoRateHz        = (float) juce::jlimit (0.0, 20.0, readDouble (v, "lfoRateHz", 5.0));
    s.lfoWaveform      = juce::jlimit (0, 3, readInt (v, "lfoWaveform", 0));
    s.lfoPositionDepth = (float) juce::jlimit (0.0, 1.0,
                                               readDouble (v, "lfoPositionDepth", 0.0));
    s.lfoPitchCents    = (float) juce::jlimit (0.0, 100.0, readDouble (v, "lfoPitchCents", 0.0));
    s.lfoCutoffDepth   = (float) juce::jlimit (0.0, 1.0, readDouble (v, "lfoCutoffDepth", 0.0));
    s.lfoAmpDepth      = (float) juce::jlimit (0.0, 1.0, readDouble (v, "lfoAmpDepth", 0.0));

    s.gain = (float) juce::jlimit (0.0, 1.0, readDouble (v, "gain", 1.0));
    return s;
}

//==============================================================================
juce::var customWavetableToVar (const CustomWavetable& table)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("id", table.id);
    o->setProperty ("name", table.name);
    o->setProperty ("frameCount", table.frameCount);

    // The frames as one base64 blob of 16-bit samples. Little-endian on the wire,
    // which is what this machine's int16 already is - a table is a shape, and a
    // byte-swap would only matter on a big-endian host, which HM does not run on.
    juce::MemoryBlock block;
    block.append (table.samples.data(), table.samples.size() * sizeof (juce::int16));
    o->setProperty ("data", juce::Base64::toBase64 (block.getData(), block.getSize()));
    return juce::var (o);
}

CustomWavetable customWavetableFromVar (const juce::var& v)
{
    CustomWavetable table;
    table.id         = juce::jmax ((juce::int64) 0, readInt64 (v, "id", 0));
    table.name       = readString (v, "name", juce::String());
    table.frameCount = juce::jlimit (1, kMaxCustomWavetableFrames,
                                     readInt (v, "frameCount", 1));

    const auto encoded = readString (v, "data", juce::String());

    if (encoded.isNotEmpty())
    {
        juce::MemoryOutputStream stream;

        if (juce::Base64::convertFromBase64 (stream, encoded))
        {
            const auto count = (int) (stream.getDataSize() / sizeof (juce::int16));
            table.samples.resize ((size_t) juce::jmax (0, count));

            if (count > 0)
                std::memcpy (table.samples.data(), stream.getData(),
                             (size_t) count * sizeof (juce::int16));
        }
    }

    wavetabletables::clampToShape (table);
    return table;
}

//==============================================================================
juce::var drumKitToVar (const DrumKitParams& kit)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("name", kit.name);

    juce::Array<juce::var> pads;

    for (const auto& pad : kit.pads)
    {
        auto* po = new juce::DynamicObject();
        po->setProperty ("enabled", pad.enabled);
        po->setProperty ("level", pad.level);
        po->setProperty ("pitch", pad.pitch);
        po->setProperty ("decay", pad.decay);
        po->setProperty ("tone", pad.tone);
        po->setProperty ("snap", pad.snap);
        pads.add (juce::var (po));
    }

    o->setProperty ("pads", pads);
    o->setProperty ("gain", kit.gain);
    o->setProperty ("masterPitch", kit.masterPitch);
    o->setProperty ("masterDecay", kit.masterDecay);
    o->setProperty ("width", kit.width);
    return juce::var (o);
}

DrumKitParams drumKitFromVar (const juce::var& v)
{
    DrumKitParams kit;
    kit.name = readString (v, "name", {});

    if (auto* padArray = v.getDynamicObject() != nullptr
                             ? v.getDynamicObject()->getProperty ("pads").getArray()
                             : nullptr)
    {
        for (int i = 0; i < kDrumPads && i < padArray->size(); ++i)
        {
            const auto& pv = (*padArray)[i];
            auto& pad = kit.pads[(size_t) i];

            pad.enabled = readBool (pv, "enabled", true);
            pad.level   = (float) juce::jlimit (0.0, 1.0, readDouble (pv, "level", 1.0));
            pad.pitch   = (float) juce::jlimit (-12.0, 12.0, readDouble (pv, "pitch", 0.0));
            pad.decay   = (float) juce::jlimit (0.1, 4.0, readDouble (pv, "decay", 1.0));
            pad.tone    = (float) juce::jlimit (0.0, 1.0, readDouble (pv, "tone", 0.5));
            pad.snap    = (float) juce::jlimit (0.0, 1.0, readDouble (pv, "snap", 0.5));
        }
    }

    kit.gain        = (float) juce::jlimit (0.0, 1.0, readDouble (v, "gain", 0.9));
    kit.masterPitch = (float) juce::jlimit (-12.0, 12.0, readDouble (v, "masterPitch", 0.0));
    kit.masterDecay = (float) juce::jlimit (0.1, 4.0, readDouble (v, "masterDecay", 1.0));
    kit.width       = (float) juce::jlimit (0.0, 1.0, readDouble (v, "width", 0.5));
    return kit;
}

//==============================================================================
juce::var granularParamsToVar (const GranularParams& s)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("name", s.name);
    o->setProperty ("filePath", s.filePath);
    o->setProperty ("fileName", s.fileName);
    o->setProperty ("rootHz", s.rootHz);
    o->setProperty ("position", s.position);
    o->setProperty ("scanSpeed", s.scanSpeed);
    o->setProperty ("grainMs", s.grainMs);
    o->setProperty ("densityHz", s.densityHz);
    o->setProperty ("spray", s.spray);
    o->setProperty ("spacingJitter", s.spacingJitter);
    o->setProperty ("pitchJitterCents", s.pitchJitterCents);
    o->setProperty ("reverseProb", s.reverseProb);
    o->setProperty ("posAttack", s.posAttack);
    o->setProperty ("posDecay", s.posDecay);
    o->setProperty ("posSustain", s.posSustain);
    o->setProperty ("posRelease", s.posRelease);
    o->setProperty ("posEnvAmount", s.posEnvAmount);
    o->setProperty ("filterMode", (int) s.filterMode);
    o->setProperty ("filterSlopeDb", s.filterSlopeDb);
    o->setProperty ("filterCutoffHz", s.filterCutoffHz);
    o->setProperty ("filterResonance", s.filterResonance);
    o->setProperty ("filterKeyTrack", s.filterKeyTrack);
    o->setProperty ("ampAttack", s.ampAttack);
    o->setProperty ("ampDecay", s.ampDecay);
    o->setProperty ("ampSustain", s.ampSustain);
    o->setProperty ("ampRelease", s.ampRelease);
    o->setProperty ("stereoSpread", s.stereoSpread);
    o->setProperty ("gain", s.gain);
    return juce::var (o);
}

GranularParams granularParamsFromVar (const juce::var& v)
{
    GranularParams s;
    s.name       = readString (v, "name", {});
    s.filePath   = readString (v, "filePath", {});
    s.fileName   = readString (v, "fileName", {});
    s.rootHz     = juce::jlimit (kMinVisibleHz, kMaxVisibleHz, readDouble (v, "rootHz", 440.0));
    s.position   = (float) juce::jlimit (0.0, 1.0, readDouble (v, "position", 0.25));
    s.scanSpeed  = (float) juce::jlimit (-2.0, 2.0, readDouble (v, "scanSpeed", 0.0));
    s.grainMs    = (float) juce::jlimit (5.0, 500.0, readDouble (v, "grainMs", 120.0));
    s.densityHz  = (float) juce::jlimit (1.0, 100.0, readDouble (v, "densityHz", 20.0));
    s.spray      = (float) juce::jlimit (0.0, 1.0, readDouble (v, "spray", 0.05));
    s.spacingJitter = (float) juce::jlimit (0.0, 0.9, readDouble (v, "spacingJitter", 0.2));
    s.pitchJitterCents = (float) juce::jlimit (0.0, 1200.0,
                                               readDouble (v, "pitchJitterCents", 0.0));
    s.reverseProb = (float) juce::jlimit (0.0, 1.0, readDouble (v, "reverseProb", 0.0));
    s.posAttack  = (float) juce::jlimit (0.0, 10.0, readDouble (v, "posAttack", 0.005));
    s.posDecay   = (float) juce::jlimit (0.0, 10.0, readDouble (v, "posDecay", 0.6));
    s.posSustain = (float) juce::jlimit (0.0, 1.0, readDouble (v, "posSustain", 0.0));
    s.posRelease = (float) juce::jlimit (0.0, 10.0, readDouble (v, "posRelease", 0.4));
    s.posEnvAmount = (float) juce::jlimit (-1.0, 1.0, readDouble (v, "posEnvAmount", 0.5));
    s.filterMode = (FilterMode) juce::jlimit (0, 2, readInt (v, "filterMode", 0));
    s.filterSlopeDb = readInt (v, "filterSlopeDb", 24) >= 24 ? 24 : 12;
    s.filterCutoffHz  = (float) juce::jlimit (20.0, 20000.0,
                                              readDouble (v, "filterCutoffHz", 8000.0));
    s.filterResonance = (float) juce::jlimit (0.1, 20.0,
                                              readDouble (v, "filterResonance", 0.707));
    s.filterKeyTrack  = (float) juce::jlimit (0.0, 1.0, readDouble (v, "filterKeyTrack", 0.0));
    s.ampAttack  = (float) juce::jlimit (0.0, 10.0, readDouble (v, "ampAttack", 0.005));
    s.ampDecay   = (float) juce::jlimit (0.0, 10.0, readDouble (v, "ampDecay", 0.2));
    s.ampSustain = (float) juce::jlimit (0.0, 1.0, readDouble (v, "ampSustain", 0.7));
    s.ampRelease = (float) juce::jlimit (0.0, 10.0, readDouble (v, "ampRelease", 0.3));
    s.stereoSpread = (float) juce::jlimit (0.0, 1.0, readDouble (v, "stereoSpread", 0.3));
    s.gain       = (float) juce::jlimit (0.0, 1.0, readDouble (v, "gain", 1.0));
    return s;
}

namespace
{
    juce::var hybridParamsToVar (const HybridParams& h)
    {
        auto* root = new juce::DynamicObject();
        root->setProperty ("name", h.name);
        root->setProperty ("gain", h.gain);
        juce::Array<juce::var> layers;

        for (const auto& l : h.layers)
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("mode", (int) l.mode);
            o->setProperty ("enabled", l.enabled);
            o->setProperty ("level", l.level);
            o->setProperty ("pan", l.pan);
            o->setProperty ("subOctave", l.subOctave);
            o->setProperty ("noiseLevel", l.noiseLevel);
            o->setProperty ("noiseCutoffHz", l.noiseCutoffHz);
            o->setProperty ("wavetable", wavetableParamsToVar (l.wavetable));
            o->setProperty ("subtractive", subtractiveParamsToVar (l.subtractive));
            o->setProperty ("sampler", samplerParamsToVar (l.sample));
            o->setProperty ("granular", granularParamsToVar (l.granular));
            layers.add (juce::var (o));
        }

        root->setProperty ("layers", layers);
        auto* sub = new juce::DynamicObject();
        sub->setProperty ("enabled", h.sub.enabled);
        sub->setProperty ("waveform", h.sub.waveform);
        sub->setProperty ("octave", h.sub.octave);
        sub->setProperty ("level", h.sub.level);
        sub->setProperty ("pan", h.sub.pan);
        root->setProperty ("sub", juce::var (sub));
        auto* noise = new juce::DynamicObject();
        noise->setProperty ("enabled", h.noise.enabled);
        noise->setProperty ("type", h.noise.type);
        noise->setProperty ("level", h.noise.level);
        noise->setProperty ("pan", h.noise.pan);
        noise->setProperty ("toneHz", h.noise.toneHz);
        root->setProperty ("noise", juce::var (noise));
        juce::Array<juce::var> filters;
        for (const auto& f : h.filters)
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("mode", (int) f.mode);
            o->setProperty ("cutoffHz", f.cutoffHz);
            o->setProperty ("resonance", f.resonance);
            o->setProperty ("drive", f.drive);
            o->setProperty ("keyTrack", f.keyTrack);
            o->setProperty ("slopeDb", f.slopeDb);
            o->setProperty ("envAmount", f.envAmount);
            filters.add (juce::var (o));
        }
        root->setProperty ("filters", filters);
        root->setProperty ("filterRouting", (int) h.filterRouting);
        juce::Array<juce::var> envs;
        for (const auto& e : h.envelopes)
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("attack", e.attack);
            o->setProperty ("decay", e.decay);
            o->setProperty ("sustain", e.sustain);
            o->setProperty ("release", e.release);
            o->setProperty ("timeMode", (int) e.timeMode);
            o->setProperty ("attackDivision", (int) e.attackDivision);
            o->setProperty ("decayDivision", (int) e.decayDivision);
            o->setProperty ("releaseDivision", (int) e.releaseDivision);
            envs.add (juce::var (o));
        }
        root->setProperty ("envelopes", envs);
        juce::Array<juce::var> lfos;
        for (const auto& l : h.lfos)
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("waveform", l.waveform);
            o->setProperty ("rateHz", l.rateHz);
            o->setProperty ("rateMs", l.rateMs);
            o->setProperty ("timeMode", (int) l.timeMode);
            o->setProperty ("syncDivision", (int) l.syncDivision);
            o->setProperty ("sync", l.sync);
            o->setProperty ("retrigger", l.retrigger);
            o->setProperty ("phase", l.phase);
            o->setProperty ("depth", l.depth);
            o->setProperty ("fadeMs", l.fadeMs);
            lfos.add (juce::var (o));
        }
        root->setProperty ("lfos", lfos);
        juce::Array<juce::var> mods;
        for (const auto& m : h.modSlots)
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("enabled", m.enabled);
            o->setProperty ("source", (int) m.source);
            o->setProperty ("destination", (int) m.destination);
            o->setProperty ("amount", m.amount);
            mods.add (juce::var (o));
        }
        root->setProperty ("modSlots", mods);
        juce::Array<juce::var> macros;
        for (auto m : h.macros)
            macros.add (m);
        root->setProperty ("macros", macros);
        juce::Array<juce::var> macroNames;
        for (const auto& name : h.macroNames)
            macroNames.add (name);
        root->setProperty ("macroNames", macroNames);
        juce::Array<juce::var> fxSlots;
        for (const auto& fx : h.fxSlots)
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("enabled", fx.enabled);
            o->setProperty ("bypass", fx.bypass);
            o->setProperty ("type", (int) fx.type);
            o->setProperty ("mix", fx.mix);
            o->setProperty ("param1", fx.param1);
            o->setProperty ("param2", fx.param2);
            fxSlots.add (juce::var (o));
        }
        root->setProperty ("fxSlots", fxSlots);
        return juce::var (root);
    }

    HybridParams hybridParamsFromVar (const juce::var& v)
    {
        HybridParams h;
        h.name = readString (v, "name", {});
        h.gain = (float) juce::jlimit (0.0, 1.0, readDouble (v, "gain", 1.0));

        if (auto* object = v.getDynamicObject())
            if (auto* layers = object->getProperty ("layers").getArray())
                for (int i = 0; i < juce::jmin (3, (int) layers->size()); ++i)
                {
                    const auto& lv = (*layers)[i];
                    auto& l = h.layers[(size_t) i];
                    l.mode = (HybridLayerMode) juce::jlimit (0, 6, readInt (lv, "mode", 0));
                    l.enabled = readBool (lv, "enabled", l.mode != HybridLayerMode::off);
                    l.level = (float) juce::jlimit (0.0, 1.0, readDouble (lv, "level", 1.0));
                    l.pan = (float) juce::jlimit (-1.0, 1.0, readDouble (lv, "pan", 0.0));
                    l.subOctave = juce::jlimit (-2, -1, readInt (lv, "subOctave", -1));
                    l.noiseLevel = (float) juce::jlimit (0.0, 1.0, readDouble (lv, "noiseLevel", 0.4));
                    l.noiseCutoffHz = (float) juce::jlimit (20.0, 20000.0,
                                                            readDouble (lv, "noiseCutoffHz", 8000.0));

                    if (l.mode == HybridLayerMode::subtractive
                        || l.mode == HybridLayerMode::off)
                        l.enabled = false;

                    if (auto* lo = lv.getDynamicObject())
                    {
                        l.wavetable = wavetableParamsFromVar (lo->getProperty ("wavetable"));
                        l.wavetable.oscs[1].enabled = false;
                        l.subtractive = subtractiveParamsFromVar (lo->getProperty ("subtractive"));
                        l.sample = samplerParamsFromVar (lo->getProperty ("sampler"));
                        l.granular = granularParamsFromVar (lo->getProperty ("granular"));
                    }
                }

        if (auto* object = v.getDynamicObject())
        {
            if (auto* sub = object->getProperty ("sub").getDynamicObject())
            {
                h.sub.enabled = (bool) sub->getProperty ("enabled");
                h.sub.waveform = juce::jlimit (0, 2, (int) sub->getProperty ("waveform"));
                h.sub.octave = juce::jlimit (-2, -1, (int) sub->getProperty ("octave"));
                h.sub.level = (float) juce::jlimit (0.0, 1.0, (double) sub->getProperty ("level"));
                h.sub.pan = (float) juce::jlimit (-1.0, 1.0, (double) sub->getProperty ("pan"));
            }
            else
            {
                for (auto& l : h.layers)
                    if (l.mode == HybridLayerMode::sub)
                    {
                        h.sub.enabled = true;
                        h.sub.octave = l.subOctave;
                        l.enabled = false;
                        break;
                    }
            }

            if (auto* noise = object->getProperty ("noise").getDynamicObject())
            {
                h.noise.enabled = (bool) noise->getProperty ("enabled");
                h.noise.type = juce::jlimit (0, 2, (int) noise->getProperty ("type"));
                h.noise.level = (float) juce::jlimit (0.0, 1.0, (double) noise->getProperty ("level"));
                h.noise.pan = (float) juce::jlimit (-1.0, 1.0, (double) noise->getProperty ("pan"));
                h.noise.toneHz = (float) juce::jlimit (20.0, 20000.0, (double) noise->getProperty ("toneHz"));
            }
            else
            {
                for (auto& l : h.layers)
                    if (l.mode == HybridLayerMode::noise)
                    {
                        h.noise.enabled = true;
                        h.noise.level = l.noiseLevel;
                        h.noise.toneHz = l.noiseCutoffHz;
                        l.enabled = false;
                        break;
                    }
            }
            if (auto* filters = object->getProperty ("filters").getArray())
                for (int i = 0; i < juce::jmin (2, (int) filters->size()); ++i)
                {
                    const auto& fv = (*filters)[i];
                    auto& f = h.filters[(size_t) i];
                    f.mode = (FilterMode) juce::jlimit (0, 4, readInt (fv, "mode", 0));
                    f.slopeDb = readInt (fv, "slopeDb", 24) >= 24 ? 24 : 12;
                    f.cutoffHz = (float) juce::jlimit (20.0, 20000.0, readDouble (fv, "cutoffHz", 8000.0));
                    f.resonance = (float) juce::jlimit (0.1, 20.0, readDouble (fv, "resonance", 0.707));
                    f.drive = (float) juce::jlimit (0.0, 1.0, readDouble (fv, "drive", 0.0));
                    f.keyTrack = (float) juce::jlimit (0.0, 1.0, readDouble (fv, "keyTrack", 0.0));
                    f.envAmount = (float) juce::jlimit (-1.0, 1.0, readDouble (fv, "envAmount", 0.0));
                }
            h.filterRouting = (HybridFilterRouting) juce::jlimit (
                0, 2, readInt (v, "filterRouting", 0));
            if (auto* envs = object->getProperty ("envelopes").getArray())
                for (int i = 0; i < juce::jmin (4, (int) envs->size()); ++i)
                {
                    const auto& ev = (*envs)[i];
                    auto& e = h.envelopes[(size_t) i];
                    e.attack = (float) juce::jlimit (0.0, 20.0, readDouble (ev, "attack", 0.005));
                    e.decay = (float) juce::jlimit (0.0, 20.0, readDouble (ev, "decay", 0.2));
                    e.sustain = (float) juce::jlimit (0.0, 1.0, readDouble (ev, "sustain", 0.7));
                    e.release = (float) juce::jlimit (0.0, 20.0, readDouble (ev, "release", 0.3));
                    e.timeMode = (HybridTimeMode) juce::jlimit (
                        0, 1, readInt (ev, "timeMode", 0));
                    e.attackDivision = (HybridTimeDivision) juce::jlimit (
                        0, 7, readInt (ev, "attackDivision", (int) e.attackDivision));
                    e.decayDivision = (HybridTimeDivision) juce::jlimit (
                        0, 7, readInt (ev, "decayDivision", (int) e.decayDivision));
                    e.releaseDivision = (HybridTimeDivision) juce::jlimit (
                        0, 7, readInt (ev, "releaseDivision", (int) e.releaseDivision));
                }
            if (auto* lfos = object->getProperty ("lfos").getArray())
                for (int i = 0; i < juce::jmin (6, (int) lfos->size()); ++i)
                {
                    const auto& lv = (*lfos)[i];
                    auto& l = h.lfos[(size_t) i];
                    l.waveform = juce::jlimit (0, 4, readInt (lv, "waveform", 0));
                    l.rateHz = (float) juce::jlimit (0.01, 40.0, readDouble (lv, "rateHz", 2.0));
                    l.rateMs = (float) juce::jlimit (
                        1.0, 10000.0,
                        readDouble (lv, "rateMs", 1000.0 / juce::jmax (0.01, (double) l.rateHz)));
                    const auto legacySync = readBool (lv, "sync", false);
                    l.timeMode = (HybridTimeMode) juce::jlimit (
                        0, 1, readInt (lv, "timeMode", legacySync ? 1 : 0));
                    l.syncDivision = (HybridTimeDivision) juce::jlimit (
                        0, 7, readInt (lv, "syncDivision", (int) HybridTimeDivision::d1_4));
                    l.sync = l.timeMode == HybridTimeMode::beats;
                    l.retrigger = readBool (lv, "retrigger", true);
                    l.phase = (float) juce::jlimit (0.0, 1.0, readDouble (lv, "phase", 0.0));
                    l.depth = (float) juce::jlimit (
                        0.0, 1.0, readDouble (lv, "depth", 1.0));
                    l.fadeMs = (float) juce::jlimit (
                        0.0, 5000.0, readDouble (lv, "fadeMs", 0.0));
                }
            if (auto* mods = object->getProperty ("modSlots").getArray())
                for (int i = 0; i < juce::jmin (16, (int) mods->size()); ++i)
                {
                    const auto& mv = (*mods)[i];
                    auto& m = h.modSlots[(size_t) i];
                    m.enabled = readBool (mv, "enabled", false);
                    m.source = (HybridModSource) juce::jlimit (
                        0, hybridModSourceCount() - 1, readInt (mv, "source", 0));
                    m.destination = (HybridModDestination) juce::jlimit (
                        0, hybridModDestinationCount() - 1, readInt (mv, "destination", 0));
                    m.amount = (float) juce::jlimit (-1.0, 1.0, readDouble (mv, "amount", 0.0));
                }
            if (auto* macros = object->getProperty ("macros").getArray())
                for (int i = 0; i < juce::jmin (4, (int) macros->size()); ++i)
                    h.macros[(size_t) i] = (float) juce::jlimit (0.0, 1.0, (double) (*macros)[i]);
            if (auto* macroNames = object->getProperty ("macroNames").getArray())
                for (int i = 0; i < juce::jmin (4, (int) macroNames->size()); ++i)
                    h.macroNames[(size_t) i] = (*macroNames)[i].toString();
            if (auto* fxSlots = object->getProperty ("fxSlots").getArray())
                for (int i = 0; i < juce::jmin (4, (int) fxSlots->size()); ++i)
                {
                    const auto& fv = (*fxSlots)[i];
                    auto& fx = h.fxSlots[(size_t) i];
                    fx.enabled = readBool (fv, "enabled", false);
                    fx.bypass = readBool (fv, "bypass", false);
                    fx.type = (HybridFxType) juce::jlimit (0, 8, readInt (fv, "type", 0));
                    fx.mix = (float) juce::jlimit (0.0, 1.0, readDouble (fv, "mix", 1.0));
                    fx.param1 = (float) juce::jlimit (0.0, 1.0, readDouble (fv, "param1", 0.5));
                    fx.param2 = (float) juce::jlimit (0.0, 1.0, readDouble (fv, "param2", 0.5));
                }
        }

        return h;
    }
}

//==============================================================================
juce::File PresetStore::getDefaultFile()
{
    if (defaultFileOverride() != juce::File())
        return defaultFileOverride();

    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("NiDaw")
               .getChildFile ("presets.json");
}

void PresetStore::setDefaultFileForTesting (juce::File file)
{
    defaultFileOverride() = std::move (file);
}

PresetStore::PresetStore (juce::File fileToUse)
    : file (std::move (fileToUse))
{
}

bool PresetStore::load()
{
    referenceGroups.clear();
    referenceLineGroups.clear();
    midiMaps.clear();
    fmPresets.clear();
    samplerPresets.clear();
    subtractivePresets.clear();
    minimoogPresets.clear();
    wavetablePresets.clear();
    drumPresets.clear();
    granularPresets.clear();
    hybridPresets.clear();
    lastError.clear();

    if (! file.existsAsFile())
        return true;   // an empty library is a perfectly good starting point

    const auto parsed = juce::JSON::parse (file.loadFileAsString());

    if (! parsed.isObject())
    {
        lastError = NI_TXT ("预设文件格式无法识别，已按空预设库启动：") + file.getFullPathName();
        return false;
    }

    auto* object = parsed.getDynamicObject();

    if (object == nullptr)
        return true;

    if (const auto* array = object->getProperty ("referenceGroups").getArray())
        for (const auto& entry : *array)
            referenceGroups.push_back (referenceGroupFromVar (entry));

    if (const auto* array = object->getProperty ("referenceLineGroups").getArray())
        for (const auto& entry : *array)
            referenceLineGroups.push_back (referenceLineGroupFromVar (entry));

    if (const auto* array = object->getProperty ("midiMaps").getArray())
        for (const auto& entry : *array)
            midiMaps.push_back (midiInputStateFromVar (entry));

    // Instrument presets are optional sections: a library written before they
    // existed simply reads back as an empty bank.
    if (const auto* array = object->getProperty ("fmPatches").getArray())
        for (const auto& entry : *array)
            fmPresets.push_back (synthParamsFromVar (entry));

    if (const auto* array = object->getProperty ("samplerPatches").getArray())
        for (const auto& entry : *array)
            samplerPresets.push_back (samplerParamsFromVar (entry));

    if (const auto* array = object->getProperty ("subtractivePatches").getArray())
        for (const auto& entry : *array)
            subtractivePresets.push_back (subtractiveParamsFromVar (entry));

    if (const auto* array = object->getProperty ("minimoogPatches").getArray())
        for (const auto& entry : *array)
            minimoogPresets.push_back (minimoogParamsFromVar (entry));

    if (const auto* array = object->getProperty ("wavetablePatches").getArray())
        for (const auto& entry : *array)
        {
            // A patch written before custom tables existed has no "tables" array:
            // the parameters alone, which is exactly what it used to be.
            WavetablePatch patch;
            patch.params = wavetableParamsFromVar (entry);

            if (entry.getDynamicObject() != nullptr)
                if (const auto* tables = entry.getDynamicObject()->getProperty ("tables").getArray())
                    for (const auto& tableVar : *tables)
                    {
                        auto table = customWavetableFromVar (tableVar);
                        wavetabletables::clampToShape (table);

                        if (table.samples.size() > 0)
                            patch.tables.push_back (std::move (table));
                    }

            wavetablePresets.push_back (std::move (patch));
        }

    if (const auto* array = object->getProperty ("drumPatches").getArray())
        for (const auto& entry : *array)
            drumPresets.push_back (drumKitFromVar (entry));

    if (const auto* array = object->getProperty ("granularPatches").getArray())
        for (const auto& entry : *array)
            granularPresets.push_back (granularParamsFromVar (entry));

    // The old hybrid presets belonged to the two-table prototype. They are
    // intentionally discarded so the single-table engine starts from a clean bank.
    hybridPresets.clear();
    if (const auto* array = object->getProperty ("hybridPatchesV2").getArray())
        for (const auto& entry : *array)
            hybridPresets.push_back (hybridParamsFromVar (entry));

    return true;
}

bool PresetStore::save() const
{
    lastError.clear();

    auto* root = new juce::DynamicObject();
    root->setProperty ("format", "nidaw-presets");
    root->setProperty ("version", 1);

    juce::Array<juce::var> groups;

    for (const auto& group : referenceGroups)
        groups.add (referenceGroupToVar (group));

    root->setProperty ("referenceGroups", groups);

    juce::Array<juce::var> lineGroups;

    for (const auto& group : referenceLineGroups)
        lineGroups.add (referenceLineGroupToVar (group));

    root->setProperty ("referenceLineGroups", lineGroups);

    juce::Array<juce::var> maps;

    for (const auto& map : midiMaps)
        maps.add (midiInputStateToVar (map));

    root->setProperty ("midiMaps", maps);

    juce::Array<juce::var> fmPatches;

    for (const auto& preset : fmPresets)
        fmPatches.add (synthParamsToVar (preset));

    root->setProperty ("fmPatches", fmPatches);

    juce::Array<juce::var> samplerPatches;

    for (const auto& preset : samplerPresets)
        samplerPatches.add (samplerParamsToVar (preset));

    root->setProperty ("samplerPatches", samplerPatches);

    juce::Array<juce::var> subtractivePatches;

    for (const auto& preset : subtractivePresets)
        subtractivePatches.add (subtractiveParamsToVar (preset));

    root->setProperty ("subtractivePatches", subtractivePatches);

    juce::Array<juce::var> minimoogPatches;

    for (const auto& preset : minimoogPresets)
        minimoogPatches.add (minimoogParamsToVar (preset));

    root->setProperty ("minimoogPatches", minimoogPatches);

    juce::Array<juce::var> wavetablePatches;

    for (const auto& preset : wavetablePresets)
    {
        auto patch = wavetableParamsToVar (preset.params);

        // The tables this sound needs travel with it, so the preset is complete
        // wherever it is opened (the mipmaps are rebuilt on load).
        if (auto* object = patch.getDynamicObject())
        {
            juce::Array<juce::var> tables;

            for (const auto& table : preset.tables)
                tables.add (customWavetableToVar (table));

            if (! tables.isEmpty())
                object->setProperty ("tables", tables);
        }

        wavetablePatches.add (patch);
    }

    root->setProperty ("wavetablePatches", wavetablePatches);

    juce::Array<juce::var> drumPatches;

    for (const auto& preset : drumPresets)
        drumPatches.add (drumKitToVar (preset));

    root->setProperty ("drumPatches", drumPatches);

    juce::Array<juce::var> granularPatches;

    for (const auto& preset : granularPresets)
        granularPatches.add (granularParamsToVar (preset));

    root->setProperty ("granularPatches", granularPatches);

    juce::Array<juce::var> hybridPatchesV2;

    for (const auto& preset : hybridPresets)
        hybridPatchesV2.add (hybridParamsToVar (preset));

    root->setProperty ("hybridPatchesV2", hybridPatchesV2);
    root->setProperty ("hybridPatches", juce::Array<juce::var>());

    file.getParentDirectory().createDirectory();

    if (! file.replaceWithText (juce::JSON::toString (juce::var (root), false), false, false, "\n"))
    {
        lastError = NI_TXT ("无法写入预设文件：") + file.getFullPathName();
        return false;
    }

    return true;
}

void PresetStore::addReferenceGroup (ReferenceGroup group)
{
    if (group.name.isEmpty())
        group.name = NI_TXT ("参考组 ") + juce::String ((int) referenceGroups.size() + 1);

    referenceGroups.push_back (std::move (group));
    save();
}

void PresetStore::addReferenceLineGroup (ReferenceLineGroup group)
{
    if (group.name.isEmpty())
        group.name = NI_TXT ("参考线组 ") + juce::String ((int) referenceLineGroups.size() + 1);

    group.visible = true;
    referenceLineGroups.push_back (std::move (group));
    save();
}

void PresetStore::replaceReferenceGroup (int index, ReferenceGroup group)
{
    if (index < 0 || index >= (int) referenceGroups.size())
        return;

    if (group.name.isEmpty())
        group.name = referenceGroups[(size_t) index].name;

    referenceGroups[(size_t) index] = std::move (group);
    save();
}

void PresetStore::replaceReferenceLineGroup (int index, ReferenceLineGroup group)
{
    if (index < 0 || index >= (int) referenceLineGroups.size())
        return;

    if (group.name.isEmpty())
        group.name = referenceLineGroups[(size_t) index].name;

    group.visible = true;
    referenceLineGroups[(size_t) index] = std::move (group);
    save();
}

void PresetStore::renameReferenceGroup (int index, const juce::String& newName)
{
    if (index >= 0 && index < (int) referenceGroups.size())
    {
        referenceGroups[(size_t) index].name = newName;
        save();
    }
}

void PresetStore::renameReferenceLineGroup (int index, const juce::String& newName)
{
    if (index >= 0 && index < (int) referenceLineGroups.size())
    {
        referenceLineGroups[(size_t) index].name = newName;
        save();
    }
}

void PresetStore::removeReferenceGroup (int index)
{
    if (index >= 0 && index < (int) referenceGroups.size())
    {
        referenceGroups.erase (referenceGroups.begin() + index);
        save();
    }
}

void PresetStore::removeReferenceLineGroup (int index)
{
    if (index >= 0 && index < (int) referenceLineGroups.size())
    {
        referenceLineGroups.erase (referenceLineGroups.begin() + index);
        save();
    }
}

void PresetStore::addMidiMap (MidiInputState map)
{
    if (map.presetName.isEmpty())
        map.presetName = NI_TXT ("MIDI 映射 ") + juce::String ((int) midiMaps.size() + 1);

    midiMaps.push_back (std::move (map));
    save();
}

void PresetStore::replaceMidiMap (int index, MidiInputState map)
{
    if (index < 0 || index >= (int) midiMaps.size())
        return;

    if (map.presetName.isEmpty())
        map.presetName = midiMaps[(size_t) index].presetName;

    midiMaps[(size_t) index] = std::move (map);
    save();
}

void PresetStore::renameMidiMap (int index, const juce::String& newName)
{
    if (index >= 0 && index < (int) midiMaps.size() && newName.isNotEmpty())
    {
        midiMaps[(size_t) index].presetName = newName;
        save();
    }
}

void PresetStore::removeMidiMap (int index)
{
    if (index >= 0 && index < (int) midiMaps.size())
    {
        midiMaps.erase (midiMaps.begin() + index);
        save();
    }
}

//==============================================================================
int PresetStore::findFmPresetByName (const juce::String& name) const
{
    for (int i = 0; i < (int) fmPresets.size(); ++i)
        if (fmPresets[(size_t) i].patch.name == name)
            return i;

    return -1;
}

int PresetStore::findSamplerPresetByName (const juce::String& name) const
{
    for (int i = 0; i < (int) samplerPresets.size(); ++i)
        if (samplerPresets[(size_t) i].name == name)
            return i;

    return -1;
}

void PresetStore::addFmPreset (SynthParams preset)
{
    if (preset.patch.name.isEmpty())
        preset.patch.name = NI_TXT ("我的音色 ") + juce::String ((int) fmPresets.size() + 1);

    fmPresets.push_back (std::move (preset));
    save();
}

void PresetStore::replaceFmPreset (int index, SynthParams preset)
{
    if (index >= 0 && index < (int) fmPresets.size())
    {
        fmPresets[(size_t) index] = std::move (preset);
        save();
    }
}

void PresetStore::renameFmPreset (int index, const juce::String& newName)
{
    if (index >= 0 && index < (int) fmPresets.size() && newName.isNotEmpty())
    {
        fmPresets[(size_t) index].patch.name = newName;
        save();
    }
}

void PresetStore::removeFmPreset (int index)
{
    if (index >= 0 && index < (int) fmPresets.size())
    {
        fmPresets.erase (fmPresets.begin() + index);
        save();
    }
}

void PresetStore::addSamplerPreset (SamplerParams preset)
{
    if (preset.name.isEmpty())
        preset.name = NI_TXT ("我的采样器音色 ") + juce::String ((int) samplerPresets.size() + 1);

    samplerPresets.push_back (std::move (preset));
    save();
}

void PresetStore::replaceSamplerPreset (int index, SamplerParams preset)
{
    if (index >= 0 && index < (int) samplerPresets.size())
    {
        samplerPresets[(size_t) index] = std::move (preset);
        save();
    }
}

void PresetStore::renameSamplerPreset (int index, const juce::String& newName)
{
    if (index >= 0 && index < (int) samplerPresets.size() && newName.isNotEmpty())
    {
        samplerPresets[(size_t) index].name = newName;
        save();
    }
}

void PresetStore::removeSamplerPreset (int index)
{
    if (index >= 0 && index < (int) samplerPresets.size())
    {
        samplerPresets.erase (samplerPresets.begin() + index);
        save();
    }
}

int PresetStore::findSubtractivePresetByName (const juce::String& name) const
{
    for (int i = 0; i < (int) subtractivePresets.size(); ++i)
        if (subtractivePresets[(size_t) i].name == name)
            return i;

    return -1;
}

void PresetStore::addSubtractivePreset (SubtractiveParams preset)
{
    if (preset.name.isEmpty())
        preset.name = NI_TXT ("我的减法音色 ") + juce::String ((int) subtractivePresets.size() + 1);

    subtractivePresets.push_back (std::move (preset));
    save();
}

void PresetStore::replaceSubtractivePreset (int index, SubtractiveParams preset)
{
    if (index >= 0 && index < (int) subtractivePresets.size())
    {
        subtractivePresets[(size_t) index] = std::move (preset);
        save();
    }
}

void PresetStore::renameSubtractivePreset (int index, const juce::String& newName)
{
    if (index >= 0 && index < (int) subtractivePresets.size() && newName.isNotEmpty())
    {
        subtractivePresets[(size_t) index].name = newName;
        save();
    }
}

void PresetStore::removeSubtractivePreset (int index)
{
    if (index >= 0 && index < (int) subtractivePresets.size())
    {
        subtractivePresets.erase (subtractivePresets.begin() + index);
        save();
    }
}

int PresetStore::findMinimoogPresetByName (const juce::String& name) const
{
    for (int i = 0; i < (int) minimoogPresets.size(); ++i)
        if (minimoogPresets[(size_t) i].name == name)
            return i;

    return -1;
}

void PresetStore::addMinimoogPreset (MinimoogParams preset)
{
    if (preset.name.isEmpty())
        preset.name = NI_TXT ("我的迷你穆格音色 ")
                          + juce::String ((int) minimoogPresets.size() + 1);

    minimoogPresets.push_back (std::move (preset));
    save();
}

void PresetStore::replaceMinimoogPreset (int index, MinimoogParams preset)
{
    if (index >= 0 && index < (int) minimoogPresets.size())
    {
        minimoogPresets[(size_t) index] = std::move (preset);
        save();
    }
}

void PresetStore::renameMinimoogPreset (int index, const juce::String& newName)
{
    if (index >= 0 && index < (int) minimoogPresets.size() && newName.isNotEmpty())
    {
        minimoogPresets[(size_t) index].name = newName;
        save();
    }
}

void PresetStore::removeMinimoogPreset (int index)
{
    if (index >= 0 && index < (int) minimoogPresets.size())
    {
        minimoogPresets.erase (minimoogPresets.begin() + index);
        save();
    }
}

int PresetStore::findWavetablePresetByName (const juce::String& name) const
{
    for (int i = 0; i < (int) wavetablePresets.size(); ++i)
        if (wavetablePresets[(size_t) i].params.name == name)
            return i;

    return -1;
}

void PresetStore::addWavetablePreset (WavetablePatch preset)
{
    if (preset.params.name.isEmpty())
        preset.params.name = NI_TXT ("我的波表音色 ") + juce::String ((int) wavetablePresets.size() + 1);

    wavetablePresets.push_back (std::move (preset));
    save();
}

void PresetStore::replaceWavetablePreset (int index, WavetablePatch preset)
{
    if (index >= 0 && index < (int) wavetablePresets.size())
    {
        wavetablePresets[(size_t) index] = std::move (preset);
        save();
    }
}

void PresetStore::renameWavetablePreset (int index, const juce::String& newName)
{
    if (index >= 0 && index < (int) wavetablePresets.size() && newName.isNotEmpty())
    {
        wavetablePresets[(size_t) index].params.name = newName;
        save();
    }
}

void PresetStore::removeWavetablePreset (int index)
{
    if (index >= 0 && index < (int) wavetablePresets.size())
    {
        wavetablePresets.erase (wavetablePresets.begin() + index);
        save();
    }
}

int PresetStore::findDrumPresetByName (const juce::String& name) const
{
    for (int i = 0; i < (int) drumPresets.size(); ++i)
        if (drumPresets[(size_t) i].name == name)
            return i;

    return -1;
}

void PresetStore::addDrumPreset (DrumKitParams preset)
{
    if (preset.name.isEmpty())
        preset.name = NI_TXT ("我的鼓组 ") + juce::String ((int) drumPresets.size() + 1);

    drumPresets.push_back (std::move (preset));
    save();
}

void PresetStore::replaceDrumPreset (int index, DrumKitParams preset)
{
    if (index >= 0 && index < (int) drumPresets.size())
    {
        drumPresets[(size_t) index] = std::move (preset);
        save();
    }
}

void PresetStore::renameDrumPreset (int index, const juce::String& newName)
{
    if (index >= 0 && index < (int) drumPresets.size() && newName.isNotEmpty())
    {
        drumPresets[(size_t) index].name = newName;
        save();
    }
}

void PresetStore::removeDrumPreset (int index)
{
    if (index >= 0 && index < (int) drumPresets.size())
    {
        drumPresets.erase (drumPresets.begin() + index);
        save();
    }
}

int PresetStore::findGranularPresetByName (const juce::String& name) const
{
    for (int i = 0; i < (int) granularPresets.size(); ++i)
        if (granularPresets[(size_t) i].name == name)
            return i;

    return -1;
}

void PresetStore::addGranularPreset (GranularParams preset)
{
    if (preset.name.isEmpty())
        preset.name = NI_TXT ("我的粒子音色 ") + juce::String ((int) granularPresets.size() + 1);

    granularPresets.push_back (std::move (preset));
    save();
}

void PresetStore::replaceGranularPreset (int index, GranularParams preset)
{
    if (index >= 0 && index < (int) granularPresets.size())
    {
        granularPresets[(size_t) index] = std::move (preset);
        save();
    }
}

void PresetStore::renameGranularPreset (int index, const juce::String& newName)
{
    if (index >= 0 && index < (int) granularPresets.size() && newName.isNotEmpty())
    {
        granularPresets[(size_t) index].name = newName;
        save();
    }
}

void PresetStore::removeGranularPreset (int index)
{
    if (index >= 0 && index < (int) granularPresets.size())
    {
        granularPresets.erase (granularPresets.begin() + index);
        save();
    }
}

int PresetStore::findHybridPresetByName (const juce::String& name) const
{
    for (int i = 0; i < (int) hybridPresets.size(); ++i)
        if (hybridPresets[(size_t) i].name == name)
            return i;

    return -1;
}

void PresetStore::addHybridPreset (HybridParams preset)
{
    if (preset.name.isEmpty())
        preset.name = NI_TXT ("我的混合音色 ") + juce::String ((int) hybridPresets.size() + 1);

    hybridPresets.push_back (std::move (preset));
    save();
}

void PresetStore::replaceHybridPreset (int index, HybridParams preset)
{
    if (index >= 0 && index < (int) hybridPresets.size())
    {
        hybridPresets[(size_t) index] = std::move (preset);
        save();
    }
}

void PresetStore::renameHybridPreset (int index, const juce::String& newName)
{
    if (index >= 0 && index < (int) hybridPresets.size() && newName.isNotEmpty())
    {
        hybridPresets[(size_t) index].name = newName;
        save();
    }
}

void PresetStore::removeHybridPreset (int index)
{
    if (index >= 0 && index < (int) hybridPresets.size())
    {
        hybridPresets.erase (hybridPresets.begin() + index);
        save();
    }
}

} // namespace nidaw

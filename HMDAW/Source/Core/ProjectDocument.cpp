#include "ProjectDocument.h"
#include "EffectChain.h"
#include "FmMath.h"
#include "PresetStore.h"
#include "VstSupport.h"

#include <map>

namespace hmdaw
{

namespace
{
    constexpr int kMaxUndoSteps = 128;

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

    juce::var referenceLineEntryToVar (const ReferenceLineEntry& e)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("id", e.id);
        o->setProperty ("kind", (int) e.kind);
        o->setProperty ("name", e.name);
        o->setProperty ("colourIndex", e.colourIndex);
        o->setProperty ("locked", e.locked);
        o->setProperty ("visible", e.visible);
        o->setProperty ("baseHz", e.baseHz);

        ReferenceLineGroup group;
        group.name           = e.name;
        group.anchorHz       = e.anchorHz;
        group.anchorPosition = e.anchorPosition;
        group.dots           = e.dots;
        group.visible        = e.visible;
        o->setProperty ("group", referenceLineGroupToVar (group));

        o->setProperty ("frozenHz", e.frozenHz);
        o->setProperty ("frozenRatio", e.frozenRatio);

        juce::Array<juce::var> frozen;

        for (const auto hz : e.frozenNotes)
            frozen.add (hz);

        o->setProperty ("frozenNotes", frozen);
        return juce::var (o);
    }

    ReferenceLineEntry referenceLineEntryFromVar (const juce::var& v)
    {
        ReferenceLineEntry e;
        e.id          = readInt64 (v, "id", 0);
        e.kind        = (ReferenceLineKind) juce::jlimit (0, 3, readInt (v, "kind", 0));
        e.name        = readString (v, "name", juce::String());
        e.colourIndex = juce::jlimit (-1, kReferencePaletteSize - 1, readInt (v, "colourIndex", -1));
        e.locked      = readBool (v, "locked", false);
        e.visible     = readBool (v, "visible", true);
        e.baseHz      = readDouble (v, "baseHz", 0.0);
        e.frozenHz    = readDouble (v, "frozenHz", 0.0);
        e.frozenRatio = readDouble (v, "frozenRatio", 2.0);

        if (auto* object = v.getDynamicObject())
        {
            if (auto* frozen = object->getProperty ("frozenNotes").getArray())
                for (const auto& hz : *frozen)
                    e.frozenNotes.push_back ((double) hz);

            const auto group = referenceLineGroupFromVar (object->getProperty ("group"));
            e.anchorHz       = group.anchorHz;
            e.anchorPosition = group.anchorPosition;
            e.dots           = group.dots;
        }

        return e;
    }

    // The instrument formats live in PresetStore so the project file and the
    // preset library share exactly one reader and writer.
    bool writeTextFile (const juce::File& file, const juce::String& text, juce::String& errorMessage)
    {
        file.getParentDirectory().createDirectory();

        if (! file.replaceWithText (text, false, false, "\n"))
        {
            errorMessage = NI_TXT ("无法写入文件：") + file.getFullPathName();
            return false;
        }

        return true;
    }
}

//==============================================================================
ProjectDocument::ProjectDocument()
    : project (makeDefaultProject())
{
}

ProjectDocument::~ProjectDocument() = default;

void ProjectDocument::pushSnapshot (const juce::String& name)
{
    undoStack.push_back ({ name, project });

    if (undoStack.size() > (size_t) kMaxUndoSteps)
        undoStack.erase (undoStack.begin());

    redoStack.clear();
}

void ProjectDocument::applyEdit (const juce::String& name, const std::function<void (Project&)>& mutation)
{
    pushSnapshot (name);
    mutation (project);
    modified = true;
    sendChangeMessage();
}

void ProjectDocument::beginEdit (const juce::String& name)
{
    pushSnapshot (name);
}

void ProjectDocument::applyDerivedEdit (const std::function<void (Project&)>& mutation)
{
    mutation (project);
    sendChangeMessage();
}

void ProjectDocument::notifyEdit()
{
    modified = true;
    sendChangeMessage();
}

void ProjectDocument::restore (Project&& state, const juce::String&)
{
    project = std::move (state);
    modified = true;
    sendChangeMessage();
}

void ProjectDocument::undo()
{
    if (undoStack.empty())
        return;

    auto snapshot = std::move (undoStack.back());
    undoStack.pop_back();

    redoStack.push_back ({ snapshot.name, project });
    project = std::move (snapshot.project);

    modified = true;
    sendChangeMessage();
}

void ProjectDocument::redo()
{
    if (redoStack.empty())
        return;

    auto snapshot = std::move (redoStack.back());
    redoStack.pop_back();

    undoStack.push_back ({ snapshot.name, project });
    project = std::move (snapshot.project);

    modified = true;
    sendChangeMessage();
}

juce::String ProjectDocument::getUndoDescription() const
{
    return undoStack.empty() ? juce::String() : undoStack.back().name;
}

juce::String ProjectDocument::getRedoDescription() const
{
    return redoStack.empty() ? juce::String() : redoStack.back().name;
}

void ProjectDocument::newProject()
{
    undoStack.clear();
    redoStack.clear();
    project = makeDefaultProject();
    currentFile = juce::File();
    modified = false;
    sendChangeMessage();
}

void ProjectDocument::setModified (bool shouldBeModified)
{
    if (modified != shouldBeModified)
    {
        modified = shouldBeModified;
        sendChangeMessage();
    }
}

juce::String ProjectDocument::getProjectName() const
{
    if (currentFile == juce::File())
        return NI_TXT ("未命名工程");

    if (currentFile.getFileName() == "project.nprj")
        return currentFile.getParentDirectory().getFileName();

    return currentFile.getFileNameWithoutExtension();
}

bool ProjectDocument::loadFromFile (const juce::File& file, juce::String& errorMessage)
{
    if (! file.existsAsFile())
    {
        errorMessage = NI_TXT ("文件不存在：") + file.getFullPathName();
        return false;
    }

    Project loaded;

    if (! projectFromJson (file.loadFileAsString(), loaded, errorMessage))
        return false;

    undoStack.clear();
    redoStack.clear();
    project = std::move (loaded);
    currentFile = file;
    modified = false;
    sendChangeMessage();
    return true;
}

bool ProjectDocument::saveToFile (const juce::File& file, juce::String& errorMessage)
{
    if (! writeTextFile (file.withFileExtension ("nprj"), projectToJson (project), errorMessage))
        return false;

    currentFile = file.withFileExtension ("nprj");
    modified = false;
    sendChangeMessage();
    return true;
}

bool ProjectDocument::saveToProjectFolder (const juce::File& file, juce::String& errorMessage)
{
    auto chosen = file.withFileExtension ("nprj");
    const auto root = chosen.getFileName() == "project.nprj"
                          ? chosen.getParentDirectory()
                          : chosen.getParentDirectory().getChildFile (
                                chosen.getFileNameWithoutExtension());
    const auto samplesFolder = root.getChildFile ("samples");
    const auto exportsFolder = root.getChildFile ("exports");

    if (root.createDirectory().failed()
        || samplesFolder.createDirectory().failed()
        || exportsFolder.createDirectory().failed())
    {
        errorMessage = NI_TXT ("无法创建工程文件夹：") + root.getFullPathName();
        return false;
    }

    Project copy = project;
    std::map<juce::String, juce::String> copied;

    auto collect = [&] (juce::String& path) -> bool
    {
        if (path.isEmpty())
            return true;

        if (const auto found = copied.find (path); found != copied.end())
        {
            path = found->second;
            return true;
        }

        const juce::File source (path);

        if (! source.existsAsFile())
            return true;   // leave a missing file visible; loading reports it

        auto target = samplesFolder.getChildFile (source.getFileName());
        int suffix = 1;

        while (target.existsAsFile()
               && target.getSize() != source.getSize())
            target = samplesFolder.getChildFile (
                source.getFileNameWithoutExtension() + "_" + juce::String (suffix++)
                    + source.getFileExtension());

        if (! target.existsAsFile() && ! source.copyFileTo (target))
        {
            errorMessage = NI_TXT ("无法复制音频文件：") + path;
            return false;
        }

        copied[path] = target.getFullPathName();
        path = target.getFullPathName();
        return true;
    };

    for (auto& track : copy.tracks)
    {
        if (! collect (track.sampler.filePath)
            || ! collect (track.granular.filePath))
            return false;

        for (auto& clip : track.clips)
            if (! collect (clip.audioFilePath))
                return false;

        for (auto& layer : track.hybrid.layers)
            if (! collect (layer.sample.filePath)
                || ! collect (layer.granular.filePath))
                return false;
    }

    const auto projectFile = root.getChildFile ("project.nprj");

    if (! writeTextFile (projectFile, projectToJson (copy), errorMessage))
        return false;

    project = std::move (copy);
    currentFile = projectFile;
    modified = false;
    sendChangeMessage();
    return true;
}

Note* ProjectDocument::findNote (juce::int64 trackId, juce::int64 noteId)
{
    if (auto* track = project.findTrack (trackId))
        for (auto& clip : track->clips)
            for (auto& n : clip.notes)
                if (n.id == noteId)
                    return &n;

    return nullptr;
}

juce::int64 ProjectDocument::projectEndTick() const
{
    juce::int64 end = 0;

    for (const auto& t : project.tracks)
    {
        for (const auto& clip : t.clips)
        {
            end = juce::jmax (end, clip.endTick());

            for (const auto& placement : music::expandClip (clip))
                end = juce::jmax (end, placement.endTick);
        }
    }

    return end;
}

double ProjectDocument::projectEndSeconds() const
{
    return makeTempoMap().secondsAtTick (projectEndTick());
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
            o->setProperty ("slopeDb", f.slopeDb);
            o->setProperty ("cutoffHz", f.cutoffHz);
            o->setProperty ("resonance", f.resonance);
            o->setProperty ("drive", f.drive);
            o->setProperty ("keyTrack", f.keyTrack);
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
        {
            if (auto* layers = object->getProperty ("layers").getArray())
            {
                const auto count = juce::jmin ((int) layers->size(), (int) h.layers.size());

                for (int i = 0; i < count; ++i)
                {
                    const auto& lv = (*layers)[i];
                    auto& l = h.layers[(size_t) i];
                    l.mode  = (HybridLayerMode) juce::jlimit (0, 6, readInt (lv, "mode", 0));
                    l.enabled = readBool (lv, "enabled", l.mode != HybridLayerMode::off);
                    l.level = (float) juce::jlimit (0.0, 1.0, readDouble (lv, "level", 1.0));
                    l.pan   = (float) juce::jlimit (-1.0, 1.0, readDouble (lv, "pan", 0.0));
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
juce::String projectToJson (const Project& project)
{
    auto* root = new juce::DynamicObject();
    root->setProperty ("format", "hmdaw-project");
    root->setProperty ("version", project.version);
    root->setProperty ("sampleRate", project.sampleRate);
    root->setProperty ("bufferSize", project.bufferSize);
    root->setProperty ("masterVolumeDb", project.masterVolumeDb);
    root->setProperty ("nextTrackId", project.nextTrackId);
    root->setProperty ("nextNoteId", project.nextNoteId);

    juce::Array<juce::var> markers;

    for (const auto& m : project.tempoMarkers)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("tick", m.tick);
        o->setProperty ("numerator", m.numerator);
        o->setProperty ("denominator", m.denominator);
        o->setProperty ("bpm", m.bpm);
        markers.add (juce::var (o));
    }

    root->setProperty ("tempoMarkers", markers);

    auto* loop = new juce::DynamicObject();
    loop->setProperty ("startTick", project.loop.startTick);
    loop->setProperty ("endTick", project.loop.endTick);
    loop->setProperty ("enabled", project.loop.enabled);
    root->setProperty ("loop", juce::var (loop));

    juce::Array<juce::var> tracks;

    for (const auto& t : project.tracks)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("id", t.id);
        o->setProperty ("kind", (int) t.kind);
        o->setProperty ("name", t.name);
        o->setProperty ("volumeDb", t.volumeDb);
        o->setProperty ("pan", t.pan);
        o->setProperty ("colourIndex", t.colourIndex);
        o->setProperty ("muted", t.muted);
        o->setProperty ("soloed", t.soloed);
        o->setProperty ("synth", synthParamsToVar (t.synth));
        o->setProperty ("sampler", samplerParamsToVar (t.sampler));
        o->setProperty ("subtractive", subtractiveParamsToVar (t.subtractive));
        o->setProperty ("wavetable", wavetableParamsToVar (t.wavetable));
        o->setProperty ("drums", drumKitToVar (t.drums));
        o->setProperty ("vst", vst::vstParamsToVar (t.vst));
        o->setProperty ("granular", granularParamsToVar (t.granular));
        o->setProperty ("hybrid", hybridParamsToVar (t.hybrid));
        o->setProperty ("minimoog", minimoogParamsToVar (t.minimoog));
        o->setProperty ("gm", gmParamsToVar (t.gm));
        o->setProperty ("effects", effects::toVar (t.effects));

        juce::Array<juce::var> clips;

        for (const auto& c : t.clips)
            clips.add (clipToVar (c));

        o->setProperty ("clips", clips);
        tracks.add (juce::var (o));
    }

    root->setProperty ("tracks", tracks);

    auto* view = new juce::DynamicObject();
    view->setProperty ("pixelsPerQuarter", project.view.pixelsPerQuarter);
    view->setProperty ("horizontalOffsetTicks", project.view.horizontalOffsetTicks);
    // The arrangement keeps its own horizontal view; it is written out so the two
    // panes come back exactly as they were left.
    view->setProperty ("arrangementPixelsPerQuarter", project.view.arrangementPixelsPerQuarter);
    view->setProperty ("arrangementHorizontalOffsetTicks",
                       project.view.arrangementHorizontalOffsetTicks);
    view->setProperty ("centreHz", project.view.centreHz);
    view->setProperty ("octavesVisible", project.view.octavesVisible);
    view->setProperty ("autoFitView", project.view.autoFitView);
    auto writeSnap = [view] (const juce::String& prefix, const SnapSettings& snap)
    {
        view->setProperty (prefix + "SnapEnabled", snap.enabled);
        view->setProperty (prefix + "SnapDenominator", snap.clampedDenominator());
    };
    writeSnap ("track", project.view.trackSnap);
    writeSnap ("editor", project.view.editorSnap);
    writeSnap ("controller", project.view.controllerSnap);
    view->setProperty ("ratioArrowVisible", project.view.ratioArrowVisible);
    view->setProperty ("metronomeEnabled", project.view.metronomeEnabled);
    view->setProperty ("editorMode", (int) project.view.editorMode);
    view->setProperty ("showReferenceLinesInPianoMode",
                       project.view.showReferenceLinesInPianoMode);
    view->setProperty ("viewSideView", project.view.sideView);

    juce::Array<juce::var> hiddenTracks;

    for (const auto id : project.view.sideViewHiddenTracks)
        hiddenTracks.add ((juce::int64) id);

    view->setProperty ("viewSideViewHidden", hiddenTracks);
    // The three collapsible columns: how wide they were dragged, and whether they
    // are folded down to a thin bar.
    view->setProperty ("panelReferenceGroupSize", project.view.referenceGroupPanel.size);
    view->setProperty ("panelReferenceGroupCollapsed", project.view.referenceGroupPanel.collapsed);
    view->setProperty ("panelPresetSize", project.view.presetPanel.size);
    view->setProperty ("panelPresetCollapsed", project.view.presetPanel.collapsed);
    view->setProperty ("panelReferenceLinesSize", project.view.referenceLinesPanel.size);
    view->setProperty ("panelReferenceLinesCollapsed",
                       project.view.referenceLinesPanel.collapsed);
    // The controller lane at the bottom of the editor has the same pair: how tall it
    // was dragged, and whether it is folded to a thin bar.
    view->setProperty ("controllerLaneSize", project.view.controllerLane.size);
    view->setProperty ("controllerLaneCollapsed", project.view.controllerLane.collapsed);
    root->setProperty ("view", juce::var (view));

    root->setProperty ("referenceGroup", referenceGroupToVar (project.referenceGroup));

    // Only sealed entries survive a save: an unlocked one is the scratch line the
    // user is working on right now, and it is meant to be cleared by the next one.
    juce::Array<juce::var> referenceLines;

    for (const auto& entry : project.referenceLines)
        if (entry.locked)
            referenceLines.add (referenceLineEntryToVar (entry));

    root->setProperty ("referenceLines", referenceLines);
    root->setProperty ("nextReferenceLineId", project.nextReferenceLineId);

    root->setProperty ("midiInput", midiInputStateToVar (project.midiInput));

    // 自定义波表：没有的时候一个字段都不写，这样不开自定义表的工程与以前
    // 版本写出来的文件逐字节相同。
    if (! project.customWavetables.empty())
    {
        juce::Array<juce::var> customTables;

        for (const auto& table : project.customWavetables)
            customTables.add (customWavetableToVar (table));

        root->setProperty ("customWavetables", customTables);
        root->setProperty ("nextCustomWavetableId", project.nextCustomWavetableId);
    }

    return juce::JSON::toString (juce::var (root), false);
}

bool projectFromJson (const juce::String& json, Project& destination, juce::String& errorMessage)
{
    const auto parsed = juce::JSON::parse (json);

    if (! parsed.isObject())
    {
        errorMessage = NI_TXT ("工程文件格式无法识别。");
        return false;
    }

    Project p;
    p.version     = readInt (parsed, "version", 1);
    p.sampleRate  = readDouble (parsed, "sampleRate", 48000.0);
    p.bufferSize  = readInt (parsed, "bufferSize", 512);
    // A project written before the mixer existed has no master fader: 0 dB, which is
    // exactly how it used to sound.
    p.masterVolumeDb = (float) juce::jlimit (-60.0, 6.0,
                                             readDouble (parsed, "masterVolumeDb", 0.0));
    p.nextTrackId = readInt64 (parsed, "nextTrackId", 1);
    p.nextNoteId  = readInt64 (parsed, "nextNoteId", 1);

    if (p.sampleRate < 8000.0 || p.sampleRate > 384000.0)
        p.sampleRate = 48000.0;

    p.tempoMarkers.clear();

    if (auto* o = parsed.getDynamicObject())
    {
        if (auto* markerArray = o->getProperty ("tempoMarkers").getArray())
        {
            for (const auto& mv : *markerArray)
            {
                TempoMarker m;
                m.tick        = readInt64 (mv, "tick", 0);
                m.numerator   = readInt (mv, "numerator", 4);
                m.denominator = readInt (mv, "denominator", 4);
                m.bpm         = readDouble (mv, "bpm", 120.0);
                p.tempoMarkers.push_back (m);
            }
        }

        // A project written before the loop existed has no region at all.
        if (const auto loopVar = o->getProperty ("loop"); loopVar.isObject())
        {
            p.loop.startTick = juce::jmax ((juce::int64) 0, readInt64 (loopVar, "startTick", 0));
            p.loop.endTick   = juce::jmax ((juce::int64) 0, readInt64 (loopVar, "endTick", 0));
            p.loop.enabled   = readBool (loopVar, "enabled", false);
        }

        if (auto* trackArray = o->getProperty ("tracks").getArray())
        {
            for (const auto& tv : *trackArray)
            {
                InstrumentTrack t;
                t.id          = readInt64 (tv, "id", 0);
                t.kind        = (TrackKind) juce::jlimit (0, 10, readInt (tv, "kind", 0));
                t.name        = readString (tv, "name", NI_TXT ("乐器"));
                t.volumeDb    = (float) readDouble (tv, "volumeDb", -6.0);
                t.pan         = (float) juce::jlimit (-1.0, 1.0, readDouble (tv, "pan", 0.0));
                t.colourIndex = readInt (tv, "colourIndex", 0);
                // A project written before track mute / solo existed has neither.
                t.muted       = readBool (tv, "muted", false);
                t.soloed      = readBool (tv, "soloed", false);
                t.synth       = synthParamsFromVar (tv.getDynamicObject() != nullptr
                                                        ? tv.getDynamicObject()->getProperty ("synth")
                                                        : juce::var());
                t.sampler     = samplerParamsFromVar (tv.getDynamicObject() != nullptr
                                                          ? tv.getDynamicObject()->getProperty ("sampler")
                                                          : juce::var());
                t.subtractive = subtractiveParamsFromVar (tv.getDynamicObject() != nullptr
                                                              ? tv.getDynamicObject()->getProperty ("subtractive")
                                                              : juce::var());
                t.wavetable   = wavetableParamsFromVar (tv.getDynamicObject() != nullptr
                                                            ? tv.getDynamicObject()->getProperty ("wavetable")
                                                            : juce::var());
                t.drums       = drumKitFromVar (tv.getDynamicObject() != nullptr
                                                    ? tv.getDynamicObject()->getProperty ("drums")
                                                    : juce::var());
                t.vst         = vst::vstParamsFromVar (tv.getDynamicObject() != nullptr
                                                           ? tv.getDynamicObject()->getProperty ("vst")
                                                           : juce::var());
                t.granular    = granularParamsFromVar (tv.getDynamicObject() != nullptr
                                                           ? tv.getDynamicObject()->getProperty ("granular")
                                                           : juce::var());
                t.hybrid      = hybridParamsFromVar (tv.getDynamicObject() != nullptr
                                                         ? tv.getDynamicObject()->getProperty ("hybrid")
                                                         : juce::var());
                t.minimoog    = minimoogParamsFromVar (tv.getDynamicObject() != nullptr
                                                           ? tv.getDynamicObject()->getProperty ("minimoog")
                                                           : juce::var());
                t.gm          = gmParamsFromVar (tv.getDynamicObject() != nullptr
                                                      ? tv.getDynamicObject()->getProperty ("gm")
                                                      : juce::var());
                t.effects     = effects::fromVar (tv.getDynamicObject() != nullptr
                                                      ? tv.getDynamicObject()->getProperty ("effects")
                                                      : juce::var());

                auto* trackObject = tv.getDynamicObject();

                if (auto* clipArray = trackObject != nullptr
                                          ? trackObject->getProperty ("clips").getArray()
                                          : nullptr)
                {
                    for (const auto& cv : *clipArray)
                        t.clips.push_back (clipFromVar (cv));
                }

                // Legacy projects stored notes directly on the track: wrap them
                // into a single clip so old files keep working unchanged.
                if (t.clips.empty())
                {
                    if (auto* noteArray = trackObject != nullptr
                                              ? trackObject->getProperty ("notes").getArray()
                                              : nullptr)
                    {
                        if (! noteArray->isEmpty())
                        {
                            Clip clip;
                            clip.id = p.nextClipId++;
                            clip.name = NI_TXT ("片段 1");

                            for (const auto& nv : *noteArray)
                                clip.notes.push_back (noteFromVar (nv));

                            auto firstTick = clip.notes.front().startTick;
                            auto lastTick = clip.notes.front().endTick();

                            for (const auto& n : clip.notes)
                            {
                                firstTick = juce::jmin (firstTick, n.startTick);
                                lastTick  = juce::jmax (lastTick, n.endTick());
                            }

                            for (auto& n : clip.notes)
                                n.startTick -= firstTick;

                            clip.startTick = juce::jmax ((juce::int64) 0, firstTick);
                            clip.lengthTicks = juce::jmax (kMinClipTicks, lastTick - firstTick);
                            clip.loopLengthTicks = clip.lengthTicks;
                            t.clips.push_back (std::move (clip));
                        }
                    }
                }

                p.tracks.push_back (std::move (t));
            }
        }

        const auto v = o->getProperty ("view");

        if (v.getDynamicObject() != nullptr)
        {
            p.view.pixelsPerQuarter      = juce::jlimit (8.0, 2000.0, readDouble (v, "pixelsPerQuarter", 140.0));
            p.view.horizontalOffsetTicks = readDouble (v, "horizontalOffsetTicks", 0.0);
            // A file written before the two panes had a view each: give the
            // arrangement the editor's, which is what it was showing back then.
            p.view.arrangementPixelsPerQuarter =
                v.hasProperty ("arrangementPixelsPerQuarter")
                    ? juce::jlimit (8.0, 2000.0,
                                    readDouble (v, "arrangementPixelsPerQuarter", 140.0))
                    : p.view.pixelsPerQuarter;
            p.view.arrangementHorizontalOffsetTicks =
                v.hasProperty ("arrangementHorizontalOffsetTicks")
                    ? juce::jmax (0.0, readDouble (v, "arrangementHorizontalOffsetTicks", 0.0))
                    : p.view.horizontalOffsetTicks;
            p.view.centreHz              = music::VerticalAxis::clampHz (readDouble (v, "centreHz", 440.0));
            p.view.octavesVisible        = music::VerticalAxis::clampOctaves (readDouble (v, "octavesVisible", 4.0));
            p.view.autoFitView           = readBool (v, "autoFitView", true);
            const auto legacyDivisor = readInt (v, "snapDivisor", 1);
            // A project written before the "不吸附" option existed keeps snapping on.
            const auto legacyEnabled = readBool (v, "snapEnabled", true);
            // A project written before the indicator existed keeps it on.
            auto readSnap = [&v, legacyDivisor, legacyEnabled] (const juce::String& prefix)
            {
                SnapSettings snap;
                const auto enabledKey = prefix + "SnapEnabled";
                const auto denominatorKey = prefix + "SnapDenominator";
                snap.enabled = readBool (v, enabledKey.toRawUTF8(), legacyEnabled);
                snap.denominator = juce::jlimit (
                    1, 512, readInt (v, denominatorKey.toRawUTF8(), legacyDivisor));
                return snap;
            };
            p.view.trackSnap = readSnap ("track");
            p.view.editorSnap = readSnap ("editor");
            p.view.controllerSnap = readSnap ("controller");
            p.view.ratioArrowVisible     = readBool (v, "ratioArrowVisible", true);
            // Same for the metronome: it starts off unless the file says otherwise.
            p.view.metronomeEnabled      = readBool (v, "metronomeEnabled", false);
            // A project written before the piano window existed opens in the
            // reference view, which is what it was saved in.
            p.view.editorMode = readInt (v, "editorMode", 0) == 1 ? EditorMode::piano
                                                                  : EditorMode::reference;
            p.view.showReferenceLinesInPianoMode = readBool (v, "showReferenceLinesInPianoMode",
                                                             true);
            // 侧视 (other tracks as a dim background) is new: an older project has
            // never had it on, so it reads back off.
            p.view.sideView = readBool (v, "viewSideView", false);

            // ...and it shows every other track until the user un-ticks one.
            if (auto* viewObject = v.getDynamicObject())
                if (auto* hidden = viewObject->getProperty ("viewSideViewHidden").getArray())
                    for (const auto& id : *hidden)
                        p.view.sideViewHiddenTracks.insert ((juce::int64) id);
            // Older projects have no column sizes: 0 means "use the panel's own
            // default", and an unfolded column, so they open as they always did.
            p.view.referenceGroupPanel.size =
                juce::jlimit (0, 2000, readInt (v, "panelReferenceGroupSize", 0));
            p.view.referenceGroupPanel.collapsed =
                readBool (v, "panelReferenceGroupCollapsed", false);
            p.view.presetPanel.size = juce::jlimit (0, 2000, readInt (v, "panelPresetSize", 0));
            p.view.presetPanel.collapsed = readBool (v, "panelPresetCollapsed", false);
            p.view.referenceLinesPanel.size =
                juce::jlimit (0, 2000, readInt (v, "panelReferenceLinesSize", 0));
            p.view.referenceLinesPanel.collapsed =
                readBool (v, "panelReferenceLinesCollapsed", false);

            // A project written before the controller lane existed has neither
            // field: the lane opens at its default height, unfolded.
            p.view.controllerLane.size = juce::jlimit (0, 2000, readInt (v, "controllerLaneSize", 0));
            p.view.controllerLane.collapsed = readBool (v, "controllerLaneCollapsed", false);

        }

        p.referenceGroup = referenceGroupFromVar (o->getProperty ("referenceGroup"));

        p.nextReferenceLineId = readInt64 (o, "nextReferenceLineId", 1);

        if (const auto* lineArray = o->getProperty ("referenceLines").getArray())
        {
            for (const auto& entry : *lineArray)
            {
                auto migrated = referenceLineEntryFromVar (entry);

                if (migrated.id <= 0)
                    migrated.id = p.nextReferenceLineId++;

                migrated.locked = true;   // only sealed entries are ever written
                p.referenceLines.push_back (std::move (migrated));
            }
        }
        else
        {
            // Older files: applied reference groups and the hand-typed frequency
            // used to live in their own fields. Both were persistent, so they come
            // back sealed rather than as scratch entries that would vanish on save.
            if (const auto* lineGroupArray = o->getProperty ("referenceLineGroups").getArray())
            {
                for (const auto& entry : *lineGroupArray)
                {
                    auto migrated = music::makeGroupEntry (p.nextReferenceLineId++,
                                                           referenceLineGroupFromVar (entry));
                    migrated.locked = true;
                    p.referenceLines.push_back (std::move (migrated));
                }
            }

            const auto legacyManualHz = readDouble (o->getProperty ("view"), "manualInputHz", 0.0);

            if (legacyManualHz > 0.0)
            {
                auto migrated = music::makeFixedHzEntry (p.nextReferenceLineId++, legacyManualHz,
                                                         juce::String());
                migrated.locked = true;
                p.referenceLines.push_back (std::move (migrated));
            }
        }

        for (const auto& entry : p.referenceLines)
            p.nextReferenceLineId = juce::jmax (p.nextReferenceLineId, entry.id + 1);

    }

    // An empty track list is a perfectly good project now; only a file that has no
    // "tracks" field at all falls back to a fresh project.
    const auto* rootObject = parsed.getDynamicObject();

    if (rootObject == nullptr || rootObject->getProperty ("tracks").isVoid())
        p = makeDefaultProject();

    // MIDI 输入: a file written before it existed has an empty mapping, and the
    // window opens on its default "no reference group picked" state.
    if (rootObject != nullptr)
        p.midiInput = midiInputStateFromVar (rootObject->getProperty ("midiInput"));

    // 自定义波表：旧工程（没有这一段）读成空表，一切照旧。
    if (const auto* customTables = parsed.getDynamicObject() != nullptr
                                       ? parsed.getDynamicObject()->getProperty ("customWavetables").getArray()
                                       : nullptr)
        for (const auto& entry : *customTables)
        {
            auto table = customWavetableFromVar (entry);

            if (! table.samples.empty())
                p.customWavetables.push_back (std::move (table));
        }

    p.nextCustomWavetableId = juce::jmax ((juce::int64) 1,
                                          readInt64 (parsed, "nextCustomWavetableId", 1));

    for (const auto& table : p.customWavetables)
        p.nextCustomWavetableId = juce::jmax (p.nextCustomWavetableId, table.id + 1);

    if (p.tempoMarkers.empty())
        p.tempoMarkers.push_back ({ 0, 4, 4, 120.0 });

    // Keep the id counters ahead of everything that was loaded.
    for (const auto& t : p.tracks)
    {
        p.nextTrackId = juce::jmax (p.nextTrackId, t.id + 1);

        for (const auto& c : t.clips)
        {
            p.nextClipId = juce::jmax (p.nextClipId, c.id + 1);

            for (const auto& n : c.notes)
                p.nextNoteId = juce::jmax (p.nextNoteId, n.id + 1);
        }
    }

    // A track always needs at least one clip so the editor has something to open.
    for (auto& t : p.tracks)
    {
        if (t.clips.empty())
        {
            Clip clip;
            clip.id = p.nextClipId++;
            clip.name = NI_TXT ("片段 1");
            t.clips.push_back (std::move (clip));
        }
    }

    destination = std::move (p);
    return true;
}

} // namespace hmdaw

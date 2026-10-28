#include "VstSupport.h"

namespace nidaw
{
namespace vst
{

double hzForMidiNote (int note) noexcept
{
    return 440.0 * std::pow (2.0, (juce::jlimit (0, 127, note) - 69) / 12.0);
}

MidiPitch midiPitchForHz (double hz) noexcept
{
    if (! (hz > 0.0))
        hz = 440.0;

    const auto exact = 69.0 + 12.0 * std::log2 (hz / 440.0);
    const auto rounded = (int) std::llround (exact);
    const auto clamped = juce::jlimit (0, 127, rounded);

    MidiPitch result;
    result.note = clamped;
    // The cents are measured against the clamped note, so a frequency outside the
    // MIDI range still reports an honest offset from where it actually lands.
    result.cents = 100.0 * (exact - (double) clamped);
    return result;
}

double hzForMidiPitch (int note, double cents) noexcept
{
    return hzForMidiNote (note) * std::pow (2.0, cents / 1200.0);
}

double snapToSemitone (double hz) noexcept
{
    return hzForMidiNote (midiPitchForHz (hz).note);
}

int bendValueFor (double cents) noexcept
{
    const auto semitones = juce::jlimit (-kBendRangeSemitones, kBendRangeSemitones,
                                         cents / 100.0);
    const auto value = (int) std::llround (semitones / kBendRangeSemitones * 8192.0);
    return juce::jlimit (-8192, 8191, value);
}

std::array<ControllerMessage, 6> pitchBendRangeMessages() noexcept
{
    // RPN 0 (pitch bend sensitivity) = kBendRangeSemitones semitones, no cents.
    // CC 101/100 = 127/127 afterwards is the RPN null pair: the channel goes back to
    // "no parameter selected", so nothing that follows can be read as bend-range data.
    return { { { 101, 0 },
               { 100, 0 },
               { 6, (int) kBendRangeSemitones },
               { 38, 0 },
               { 101, 127 },
               { 100, 127 } } };
}

//==============================================================================
ChannelAllocator::Allocation ChannelAllocator::allocate (juce::int64 noteId) noexcept
{
    Allocation result;

    for (size_t i = 0; i < slots.size(); ++i)
    {
        auto& slot = slots[i];

        if (slot.active && slot.noteId == noteId)
        {
            slot.used = ++counter;
            result.channel = (int) i;
            return result;
        }
    }

    // A channel that has never been handed out comes first, lowest index first...
    auto* chosen = (Slot*) nullptr;

    for (auto& slot : slots)
    {
        if (! slot.active && slot.released == 0)
        {
            chosen = &slot;
            break;
        }
    }

    // ...then the free one whose note was released longest ago: the plugin is very
    // likely still ringing that note out, and the longer it has been ringing, the
    // less of it is left for the new note's bend to pull around.
    if (chosen == nullptr)
        for (auto& slot : slots)
            if (! slot.active && (chosen == nullptr || slot.released < chosen->released))
                chosen = &slot;

    if (chosen == nullptr)
    {
        // Every channel is busy: take the one that has been *held* the longest and
        // say whose note that was, so the host can let it go properly instead of
        // leaving it sounding under the new note's bend.
        chosen = &slots[0];

        for (auto& slot : slots)
            if (slot.used < chosen->used)
                chosen = &slot;

        result.evictedNoteId = chosen->noteId;
    }

    chosen->active = true;
    chosen->noteId = noteId;
    chosen->used   = ++counter;
    result.channel = (int) (chosen - slots.data());
    return result;
}

void ChannelAllocator::release (juce::int64 noteId) noexcept
{
    for (auto& slot : slots)
    {
        if (slot.active && slot.noteId == noteId)
        {
            slot.active = false;
            slot.noteId = 0;
            slot.released = ++counter;
            return;
        }
    }
}

int ChannelAllocator::channelFor (juce::int64 noteId) const noexcept
{
    for (size_t i = 0; i < slots.size(); ++i)
        if (slots[i].active && slots[i].noteId == noteId)
            return (int) i;

    return -1;
}

void ChannelAllocator::reset() noexcept
{
    for (auto& slot : slots)
    {
        slot.active = false;
        slot.noteId = 0;
        slot.used = 0;
        slot.released = 0;
    }

    counter = 0;
}

int ChannelAllocator::activeCount() const noexcept
{
    auto count = 0;

    for (const auto& slot : slots)
        if (slot.active)
            ++count;

    return count;
}

int ChannelAllocator::channelsUsedSinceReset() const noexcept
{
    auto count = 0;

    for (const auto& slot : slots)
        if (slot.used != 0)
            ++count;

    return count;
}

//==============================================================================
juce::var vstParamsToVar (const VstParams& params)
{
    auto* object = new juce::DynamicObject();
    object->setProperty ("name", params.name);
    object->setProperty ("pluginPath", params.pluginPath);
    object->setProperty ("pluginUid", params.pluginUid);
    object->setProperty ("stateBase64", params.stateBase64);
    object->setProperty ("equalTemperament", params.equalTemperament);
    return juce::var (object);
}

VstParams vstParamsFromVar (const juce::var& value)
{
    VstParams params;

    if (auto* object = value.getDynamicObject())
    {
        params.name        = object->getProperty ("name").toString();
        params.pluginPath  = object->getProperty ("pluginPath").toString();
        params.pluginUid   = object->getProperty ("pluginUid").toString();
        params.stateBase64 = object->getProperty ("stateBase64").toString();
        // Missing (a project written before this field existed) means "exact
        // frequencies", which is what the host has always done.
        params.equalTemperament = (bool) object->getProperty ("equalTemperament");
    }

    // loadedOk stays false: a fresh project always starts with its plugins
    // unloaded, however the file was written.
    params.loadedOk = false;
    return params;
}

} // namespace vst
} // namespace nidaw

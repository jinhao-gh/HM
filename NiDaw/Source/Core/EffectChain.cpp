#include "EffectChain.h"

namespace nidaw
{
namespace effects
{

namespace
{
    bool isIndexInChain (const std::vector<EffectSlot>& chain, int index)
    {
        return index >= 0 && index < (int) chain.size();
    }
}

bool add (std::vector<EffectSlot>& chain, EffectSlot slot)
{
    if (! slot.hasPlugin() || (int) chain.size() >= kMaxPerTrack)
        return false;

    chain.push_back (std::move (slot));
    return true;
}

bool remove (std::vector<EffectSlot>& chain, int index)
{
    if (! isIndexInChain (chain, index))
        return false;

    chain.erase (chain.begin() + index);
    return true;
}

bool move (std::vector<EffectSlot>& chain, int from, int to)
{
    if (! isIndexInChain (chain, from))
        return false;

    const auto target = juce::jlimit (0, (int) chain.size() - 1, to);

    if (target == from)
        return false;

    auto slot = chain[(size_t) from];
    chain.erase (chain.begin() + from);
    chain.insert (chain.begin() + target, std::move (slot));
    return true;
}

bool moveUp (std::vector<EffectSlot>& chain, int index)
{
    return move (chain, index, index - 1);
}

bool moveDown (std::vector<EffectSlot>& chain, int index)
{
    return move (chain, index, index + 1);
}

bool toggleBypass (std::vector<EffectSlot>& chain, int index)
{
    if (! isIndexInChain (chain, index))
        return false;

    chain[(size_t) index].bypass = ! chain[(size_t) index].bypass;
    return true;
}

juce::var toVar (const std::vector<EffectSlot>& chain)
{
    juce::Array<juce::var> slots;

    for (const auto& slot : chain)
    {
        auto* object = new juce::DynamicObject();
        object->setProperty ("kind", slot.kind);
        object->setProperty ("name", slot.name);
        object->setProperty ("pluginPath", slot.pluginPath);
        object->setProperty ("pluginUid", slot.pluginUid);
        object->setProperty ("stateBase64", slot.stateBase64);
        object->setProperty ("bypass", slot.bypass);
        slots.add (juce::var (object));
    }

    return juce::var (slots);
}

std::vector<EffectSlot> fromVar (const juce::var& value)
{
    std::vector<EffectSlot> chain;

    const auto* array = value.getArray();

    if (array == nullptr)
        return chain;   // a project written before the chain existed has none

    for (const auto& item : *array)
    {
        auto* object = item.getDynamicObject();

        if (object == nullptr)
            continue;

        EffectSlot slot;
        slot.kind        = (int) object->getProperty ("kind");
        slot.name        = object->getProperty ("name").toString();
        slot.pluginPath  = object->getProperty ("pluginPath").toString();
        slot.pluginUid   = object->getProperty ("pluginUid").toString();
        slot.stateBase64 = object->getProperty ("stateBase64").toString();
        slot.bypass      = (bool) object->getProperty ("bypass");

        // A chain with more slots than the editor offers would be unusable; the
        // extra ones are dropped rather than shown in a list that cannot hold them.
        if ((int) chain.size() < kMaxPerTrack)
            chain.push_back (std::move (slot));
    }

    return chain;
}

} // namespace effects
} // namespace nidaw

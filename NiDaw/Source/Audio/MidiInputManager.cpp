#include "MidiInputManager.h"

namespace nidaw
{

namespace
{
    juce::File settingsFileOverride;
}

MidiInputManager::MidiInputManager()
{
    refreshDeviceList();
}

MidiInputManager::~MidiInputManager()
{
    delivery->callback = nullptr;
    closeDevice();
}

void MidiInputManager::refreshDeviceList()
{
    devices.clear();

    for (const auto& device : juce::MidiInput::getAvailableDevices())
        devices.push_back ({ device.name, device.identifier });
}

bool MidiInputManager::openDevice (const juce::String& identifier)
{
    closeDevice();

    if (identifier.isEmpty())
    {
        saveSettings();
        return true;
    }

    // The name the picker knows, so a device that is not here right now is still
    // remembered (and named) rather than silently forgotten.
    openIdentifier = identifier;
    openName = identifier;

    for (const auto& info : devices)
        if (info.identifier == identifier)
            openName = info.name;

    auto device = juce::MidiInput::openDevice (identifier, this);

    if (device == nullptr)
        saveSettings();
    else
    {
        device->start();

        // Between openDevice and start the pointer is already stored, so the callback
        // can find us; the callback only forwards the raw message.
        input = std::move (device);
        saveSettings();
        return true;
    }

    return false;
}

void MidiInputManager::closeDevice()
{
    if (input != nullptr)
    {
        input->stop();
        input.reset();
    }
}

void MidiInputManager::handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& message)
{
    handleIncoming (message);
}

void MidiInputManager::handleIncoming (const juce::MidiMessage& message)
{
    auto target = delivery;
    const auto copy = message;

    // The MIDI thread only posts: everything downstream (the mapping, the engine's
    // live-note queue, recording) then runs on the message thread like any other UI
    // work.
    juce::MessageManager::callAsync ([target, copy]
    {
        if (target->callback != nullptr)
            target->callback (copy);
    });
}

juce::File MidiInputManager::getDefaultSettingsFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("NiDaw")
               .getChildFile ("midi.json");
}

juce::File MidiInputManager::getSettingsFile()
{
    return settingsFileOverride != juce::File() ? settingsFileOverride
                                                : getDefaultSettingsFile();
}

void MidiInputManager::setSettingsFileForTesting (juce::File file)
{
    settingsFileOverride = std::move (file);
}

void MidiInputManager::loadSettings()
{
    const auto file = getSettingsFile();

    if (! file.existsAsFile())
        return;

    const auto parsed = juce::JSON::parse (file.loadFileAsString());

    if (auto* object = parsed.getDynamicObject())
    {
        const auto wanted = object->getProperty ("device").toString();

        if (wanted.isNotEmpty())
            openDevice (wanted);
    }
}

void MidiInputManager::saveSettings() const
{
    auto* object = new juce::DynamicObject();
    object->setProperty ("device", openIdentifier);

    const auto file = getSettingsFile();
    file.getParentDirectory().createDirectory();
    file.replaceWithText (juce::JSON::toString (juce::var (object), false), false, false, "\n");
}

void MidiInputManager::setDeviceListForTest (std::vector<DeviceInfo> list)
{
    devices = std::move (list);
}

void MidiInputManager::simulateMessageForTest (const juce::MidiMessage& message)
{
    handleIncoming (message);
}

} // namespace nidaw

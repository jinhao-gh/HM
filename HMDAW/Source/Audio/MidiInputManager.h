#pragma once

#include <juce_audio_devices/juce_audio_devices.h>

#include <functional>
#include <memory>
#include <vector>

namespace hmdaw
{

/** The MIDI input device, and the notes that come out of it.

    The manager only opens the device and hands every message on; what a message
    *means* (which frequency, which track, whether it is being recorded) is the
    app's business, so this stays a thin wrapper that the offscreen self test can
    drive without any hardware.

    Messages are delivered on the **message thread**: the queue in the audio engine
    has one producer, and the rest of the app never has to think about the MIDI
    thread.
*/
class MidiInputManager : private juce::MidiInputCallback
{
public:
    MidiInputManager();
    ~MidiInputManager();

    struct DeviceInfo
    {
        juce::String name;
        juce::String identifier;
    };

    /** The devices the picker offers, as of the last refresh. */
    const std::vector<DeviceInfo>& getDeviceList() const noexcept { return devices; }
    void refreshDeviceList();

    /** Opens one device (an empty identifier closes whatever is open). Returns
        false when that device could not be opened. */
    bool openDevice (const juce::String& identifier);
    void closeDevice();

    juce::String getOpenIdentifier() const { return openIdentifier; }
    juce::String getOpenName() const { return openName; }
    bool isOpen() const { return input != nullptr; }
    /** Self test: whether the device really opened (the offscreen run has no
        hardware, so it is the *choice* that gets checked there). */
    bool isDeviceOpenForTest() const { return input != nullptr; }

    /** Every incoming message, delivered on the message thread. The callback is
        cleared when the manager goes away, so a message that was already on its way
        is simply dropped. */
    void setMessageCallback (std::function<void (const juce::MidiMessage&)> callback)
    {
        delivery->callback = std::move (callback);
    }

    /** Where the remembered device lives. App level, like the plugin settings: it
        is a property of this machine, not of a project. */
    static juce::File getDefaultSettingsFile();
    /** The file that will really be read and written (the test override when the
        self test has set one). */
    static juce::File getSettingsFile();
    static void setSettingsFileForTesting (juce::File file);
    void loadSettings();
    void saveSettings() const;

    /** Self test: pretend the system has exactly these devices (an empty list means
        "no hardware here"), and feed a message in as if a key had been pressed. */
    void setDeviceListForTest (std::vector<DeviceInfo> list);
    void simulateMessageForTest (const juce::MidiMessage& message);

private:
    void handleIncomingMidiMessage (juce::MidiInput* source,
                                    const juce::MidiMessage& message) override;
    void handleIncoming (const juce::MidiMessage& message);

    std::unique_ptr<juce::MidiInput> input;
    std::vector<DeviceInfo> devices;
    juce::String openIdentifier, openName;

    /** Holds the callback for the posted deliveries: they hold this object, not the
        manager, so a late message finds an empty callback instead of freed memory. */
    struct Delivery
    {
        std::function<void (const juce::MidiMessage&)> callback;
    };

    std::shared_ptr<Delivery> delivery = std::make_shared<Delivery>();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiInputManager)
};

} // namespace hmdaw

#include "MixerPage.h"
#include "Theme.h"

#include <algorithm>
#include <cmath>

namespace hmdaw
{

namespace
{
constexpr int kStripWidth = 116;
constexpr int kStripHeight = 470;
}

class MixerChannelStrip : public juce::Component
{
public:
    MixerChannelStrip (ProjectDocument& documentToUse, studio::Id channelIdToUse)
        : document (documentToUse), channelId (channelIdToUse)
    {
        setOpaque (true);

        addAndMakeVisible (nameLabel);
        nameLabel.setFont (theme::font (12.0f, true));
        nameLabel.setColour (juce::Label::textColourId, theme::text);
        nameLabel.setJustificationType (juce::Justification::centred);

        addAndMakeVisible (inputBox);
        inputBox.onChange = [this] { applyInput(); };

        auto setupLabel = [this] (juce::Label& label, const juce::String& text)
        {
            addAndMakeVisible (label);
            label.setText (text, juce::dontSendNotification);
            label.setFont (theme::font (9.5f));
            label.setColour (juce::Label::textColourId, theme::dimText);
            label.setJustificationType (juce::Justification::centred);
        };

        setupLabel (trimLabel, NI_TXT ("输入"));
        setupLabel (hpfLabel, NI_TXT ("HPF"));
        setupLabel (lowLabel, NI_TXT ("低频"));
        setupLabel (highLabel, NI_TXT ("高频"));
        setupLabel (panLabel, NI_TXT ("声像"));

        addAndMakeVisible (trim);
        trim.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        trim.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 40, 16);
        trim.setRange (-24.0, 24.0, 0.1);
        trim.onValueChange = [this] { applyValue ("trimDb", trim.getValue()); };

        addAndMakeVisible (hpf);
        hpf.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        hpf.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 40, 16);
        hpf.setRange (20.0, 500.0, 1.0);
        hpf.onValueChange = [this] { applyValue ("hpfHz", hpf.getValue()); };

        addAndMakeVisible (low);
        low.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        low.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 40, 16);
        low.setRange (-15.0, 15.0, 0.1);
        low.onValueChange = [this] { applyValue ("lowDb", low.getValue()); };

        addAndMakeVisible (high);
        high.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        high.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 40, 16);
        high.setRange (-15.0, 15.0, 0.1);
        high.onValueChange = [this] { applyValue ("highDb", high.getValue()); };

        addAndMakeVisible (pan);
        pan.setSliderStyle (juce::Slider::LinearHorizontal);
        pan.setTextBoxStyle (juce::Slider::TextBoxRight, false, 40, 16);
        pan.setRange (-1.0, 1.0, 0.01);
        pan.onValueChange = [this] { applyValue ("pan", pan.getValue()); };

        addAndMakeVisible (fader);
        fader.setSliderStyle (juce::Slider::LinearVertical);
        fader.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 48, 18);
        fader.setRange (-60.0, 6.0, 0.1);
        fader.onValueChange = [this] { applyValue ("faderDb", fader.getValue()); };

        addAndMakeVisible (mute);
        mute.setButtonText ("M");
        mute.setClickingTogglesState (true);
        mute.onClick = [this] { applyToggle ("mute", mute.getToggleState()); };

        addAndMakeVisible (solo);
        solo.setButtonText ("S");
        solo.setClickingTogglesState (true);
        solo.onClick = [this] { applyToggle ("solo", solo.getToggleState()); };

        addAndMakeVisible (removeButton);
        removeButton.setButtonText (NI_TXT ("删除"));
        removeButton.onClick = [this]
        {
            if (onRemove) onRemove (channelId);
        };

        refresh();
    }

    std::function<void (studio::Id)> onRemove;
    void setPeak (float newPeak) { peak = juce::jmax (0.0f, newPeak); repaint(); }

    studio::Id getChannelId() const noexcept { return channelId; }

    void refresh()
    {
        const auto* channel = findChannel();

        if (channel == nullptr)
            return;

        const juce::ScopedValueSetter<bool> guard (updating, true);
        nameLabel.setText (NI_TXT ("未选择输入"), juce::dontSendNotification);

        inputBox.clear (juce::dontSendNotification);
        inputBox.addItem (NI_TXT ("无输入"), 1);
        int index = 2;

        for (const auto& input : document.getProject().studio.mixerInputs)
        {
            inputBox.addItem (input.name, index);

            if (input.id == channel->inputId)
            {
                inputBox.setSelectedId (index, juce::dontSendNotification);
                nameLabel.setText (input.name, juce::dontSendNotification);
            }

            ++index;
        }

        if (channel->inputId == 0)
            inputBox.setSelectedId (1, juce::dontSendNotification);

        trim.setValue (channel->trimDb, juce::dontSendNotification);
        hpf.setValue (channel->hpfHz, juce::dontSendNotification);
        low.setValue (channel->lowDb, juce::dontSendNotification);
        high.setValue (channel->highDb, juce::dontSendNotification);
        pan.setValue (channel->pan, juce::dontSendNotification);
        fader.setValue (channel->faderDb, juce::dontSendNotification);
        mute.setToggleState (channel->mute, juce::dontSendNotification);
        solo.setToggleState (channel->solo, juce::dontSendNotification);

        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff303037));
        g.setColour (juce::Colour (0xff4a4a54));
        g.drawRect (getLocalBounds(), 1);

        // Live meters will be added when the audio engine publishes channel peaks.

        g.setColour (juce::Colour (0xff9b9ba7));
        g.setFont (theme::font (9.0f));
        auto meter = getLocalBounds().removeFromRight (8).reduced (2, 40);
        g.setColour (juce::Colour (0xff16161b));
        g.fillRect (meter);
        const auto level = juce::jlimit (0.0f, 1.0f, peak);
        g.setColour (level > 0.9f ? juce::Colours::red
                                  : (level > 0.7f ? juce::Colours::orange : juce::Colour (0xff63d471)));
        g.fillRect (meter.removeFromBottom ((int) (meter.getHeight() * level)));
        g.setColour (juce::Colour (0xff3a3a45));
        g.drawRect (getLocalBounds().removeFromRight (8).reduced (2, 40));

        g.drawText ("SSL 4000E", getLocalBounds().removeFromBottom (14),
                    juce::Justification::centred);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (8);

        nameLabel.setBounds (r.removeFromTop (20));
        inputBox.setBounds (r.removeFromTop (24));
        r.removeFromTop (6);

        auto knobs = r.removeFromTop (90);
        const auto knobWidth = knobs.getWidth() / 2;
        auto trimArea = knobs.removeFromLeft (knobWidth);
        trimLabel.setBounds (trimArea.removeFromTop (12));
        trim.setBounds (trimArea.reduced (4));
        auto hpfArea = knobs.removeFromLeft (knobWidth);
        hpfLabel.setBounds (hpfArea.removeFromTop (12));
        hpf.setBounds (hpfArea.reduced (4));

        knobs = r.removeFromTop (90);
        auto lowArea = knobs.removeFromLeft (knobWidth);
        lowLabel.setBounds (lowArea.removeFromTop (12));
        low.setBounds (lowArea.reduced (4));
        auto highArea = knobs.removeFromLeft (knobWidth);
        highLabel.setBounds (highArea.removeFromTop (12));
        high.setBounds (highArea.reduced (4));

        panLabel.setBounds (r.removeFromTop (12));
        pan.setBounds (r.removeFromTop (22));
        fader.setBounds (r.removeFromTop (150).reduced (20, 4));

        auto buttons = r.removeFromTop (26);
        mute.setBounds (buttons.removeFromLeft (34));
        buttons.removeFromLeft (4);
        solo.setBounds (buttons.removeFromLeft (34));
        buttons.removeFromLeft (4);
        removeButton.setBounds (buttons);
    }

private:
    const studio::MixerChannel* findChannel() const
    {
        for (const auto& channel : document.getProject().studio.mixer.channels)
            if (channel.id == channelId)
                return &channel;

        return nullptr;
    }

    void applyInput()
    {
        if (updating)
            return;

        const auto selected = inputBox.getSelectedId();
        int index = selected - 2;
        studio::Id inputId = 0;

        const auto& inputs = document.getProject().studio.mixerInputs;
        if (index >= 0 && index < (int) inputs.size())
            inputId = inputs[(size_t) index].id;

        document.applyEdit (NI_TXT ("选择混音台输入"), [this, inputId] (Project& project)
        {
            for (auto& channel : project.studio.mixer.channels)
                if (channel.id == channelId)
                {
                    channel.inputId = inputId;
                    return;
                }
        });
    }

    void applyValue (const juce::String& field, double value)
    {
        if (updating)
            return;

        document.applyEdit (NI_TXT ("调整混音台"), [this, field, value] (Project& project)
        {
            for (auto& channel : project.studio.mixer.channels)
                if (channel.id == channelId)
                {
                    if (field == "trimDb") channel.trimDb = value;
                    else if (field == "hpfHz") channel.hpfHz = value;
                    else if (field == "lowDb") channel.lowDb = value;
                    else if (field == "highDb") channel.highDb = value;
                    else if (field == "pan") channel.pan = value;
                    else if (field == "faderDb") channel.faderDb = value;
                    return;
                }
        });
    }

    void applyToggle (const juce::String& field, bool value)
    {
        if (updating)
            return;

        document.applyEdit (NI_TXT ("混音台开关"), [this, field, value] (Project& project)
        {
            for (auto& channel : project.studio.mixer.channels)
                if (channel.id == channelId)
                {
                    if (field == "mute") channel.mute = value;
                    else if (field == "solo") channel.solo = value;
                    return;
                }
        });
    }

    ProjectDocument& document;
    studio::Id channelId = 0;
    float peak = 0.0f;
    bool updating = false;

    juce::Label nameLabel;
    juce::Label trimLabel, hpfLabel, lowLabel, highLabel, panLabel;
    juce::ComboBox inputBox;
    juce::Slider trim, hpf, low, high, pan, fader;
    juce::TextButton mute, solo, removeButton;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixerChannelStrip)
};

//==============================================================================
MixerPage::MixerPage (ProjectDocument& documentToUse, AudioEngine& audioToUse,
                      AppState& appStateToUse)
    : document (documentToUse), audio (audioToUse), appState (appStateToUse)
{
    setOpaque (true);
    startTimerHz (20);

    addAndMakeVisible (titleLabel);
    titleLabel.setText (NI_TXT ("混音台"), juce::dontSendNotification);
    titleLabel.setFont (theme::font (22.0f, true));
    titleLabel.setColour (juce::Label::textColourId, theme::text);

    addAndMakeVisible (hintLabel);
    hintLabel.setText (NI_TXT ("SSL 4000E 风格：每通道选择工作室里的输入接口"),
                       juce::dontSendNotification);
    hintLabel.setFont (theme::font (11.0f));
    hintLabel.setColour (juce::Label::textColourId, theme::dimText);

    addAndMakeVisible (addChannelButton);
    addChannelButton.onClick = [this] { addChannel(); };

    addAndMakeVisible (viewport);
    viewport.setViewedComponent (&stripHolder, false);
    viewport.setScrollBarsShown (true, true);

    addAndMakeVisible (masterLabel);
    masterLabel.setText (NI_TXT ("主输出"), juce::dontSendNotification);
    masterLabel.setFont (theme::font (12.0f, true));
    masterLabel.setColour (juce::Label::textColourId, theme::text);

    addAndMakeVisible (masterGain);
    masterGain.setSliderStyle (juce::Slider::LinearVertical);
    masterGain.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 52, 18);
    masterGain.setRange (-60.0, 6.0, 0.1);
    masterGain.onValueChange = [this]
    {
        document.applyEdit (NI_TXT ("调整主输出"), [this] (Project& project)
        {
            const auto value = masterGain.getValue();
            project.masterVolumeDb = (float) value;
            project.studio.mixer.masterGainDb = value;
        });
    };

    document.addChangeListener (this);
    rebuildStrips();
    refresh();
}

MixerPage::~MixerPage()
{
    document.removeChangeListener (this);
    strips.clear();
}

void MixerPage::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (currentChannelIds() != lastIds)
        rebuildStrips();
    else
        for (auto* strip : strips)
            strip->refresh();

    masterGain.setValue (document.getProject().masterVolumeDb,
                         juce::dontSendNotification);
}

std::vector<studio::Id> MixerPage::currentChannelIds() const
{
    std::vector<studio::Id> ids;

    for (const auto& channel : document.getProject().studio.mixer.channels)
        ids.push_back (channel.id);

    return ids;
}

void MixerPage::rebuildStrips()
{
    strips.clear();
    lastIds = currentChannelIds();

    for (const auto id : lastIds)
    {
        auto* strip = strips.add (new MixerChannelStrip (document, id));
        strip->onRemove = [this] (studio::Id channelId) { removeChannel (channelId); };
        stripHolder.addAndMakeVisible (strip);
    }

    resized();
    refresh();
}

void MixerPage::refresh()
{
    masterGain.setValue (document.getProject().masterVolumeDb, juce::dontSendNotification);
    repaint();
}

void MixerPage::paint (juce::Graphics& g)
{
    g.fillAll (theme::panelBackground);

    auto r = getLocalBounds().reduced (14);
    r.removeFromTop (38);
    g.setColour (theme::outline);
    g.drawHorizontalLine (r.getY(), (float) r.getX(), (float) r.getRight());

    const auto meterX = masterGain.getRight() + 6;
    const auto meterY = masterGain.getY() + 8;
    const auto meterH = juce::jmax (1, masterGain.getHeight() - 16);
    juce::Rectangle<int> meter (meterX, meterY, 10, meterH);
    g.setColour (juce::Colour (0xff16161b));
    g.fillRect (meter);
    const auto db = juce::Decibels::gainToDecibels (masterPeak, -60.0f);
    const auto amount = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
    g.setColour (db > -3.0f ? juce::Colours::red
                            : (db > -12.0f ? juce::Colours::orange : juce::Colour (0xff63d471)));
    g.fillRect (meter.removeFromBottom ((int) (meter.getHeight() * amount)));
    g.setColour (juce::Colour (0xff3a3a45));
    g.drawRect (meterX, meterY, 10, meterH);
}

void MixerPage::timerCallback()
{
    masterPeak = audio.getStudioMasterPeak();
    repaint();

    int index = 0;

    for (auto* strip : strips)
    {
        if (strip == nullptr)
            continue;

        strip->setPeak (audio.getMixerChannelPeak (index));
        ++index;
    }
}

void MixerPage::resized()
{
    auto r = getLocalBounds().reduced (14);

    auto header = r.removeFromTop (36);
    titleLabel.setBounds (header.removeFromLeft (120));
    header.removeFromLeft (20);
    addChannelButton.setBounds (header.removeFromLeft (92));
    header.removeFromLeft (16);
    hintLabel.setBounds (header);

    auto master = r.removeFromRight (104);
    masterLabel.setBounds (master.removeFromTop (24));
    masterGain.setBounds (master.removeFromTop (180).reduced (20, 0));

    r.removeFromTop (8);
    viewport.setBounds (r);
    stripHolder.setSize (juce::jmax (r.getWidth(), kStripWidth * strips.size()),
                         kStripHeight);

    for (int i = 0; i < strips.size(); ++i)
        strips[i]->setBounds (i * kStripWidth, 0, kStripWidth, kStripHeight);
}

void MixerPage::addChannel()
{
    document.applyEdit (NI_TXT ("添加混音台通道"), [] (Project& project)
    {
        studio::MixerChannel channel;
        channel.id = studio::allocateId (project.studio);
        channel.name = juce::String::fromUTF8 ("通道 ") + juce::String ((int) project.studio.mixer.channels.size() + 1);
        project.studio.mixer.channels.push_back (channel);
    });
}

void MixerPage::removeChannel (studio::Id channelId)
{
    document.applyEdit (NI_TXT ("删除混音台通道"), [channelId] (Project& project)
    {
        auto& channels = project.studio.mixer.channels;
        channels.erase (std::remove_if (channels.begin(), channels.end(),
                                        [channelId] (const studio::MixerChannel& c)
                                        { return c.id == channelId; }),
                        channels.end());
    });
}

int MixerPage::getChannelCountForTest() const
{
    return strips.size();
}

juce::int64 MixerPage::getChannelIdForTest (int index) const
{
    return index >= 0 && index < strips.size() ? strips[index]->getChannelId() : 0;
}

juce::String MixerPage::getChannelInputNameForTest (int index) const
{
    const auto id = getChannelIdForTest (index);

    for (const auto& channel : document.getProject().studio.mixer.channels)
        if (channel.id == id)
            for (const auto& input : document.getProject().studio.mixerInputs)
                if (input.id == channel.inputId)
                    return input.name;

    return {};
}

juce::Rectangle<int> MixerPage::getStripBoundsForTest (int index) const
{
    return index >= 0 && index < strips.size() ? strips[index]->getBounds() : juce::Rectangle<int>();
}

} // namespace hmdaw

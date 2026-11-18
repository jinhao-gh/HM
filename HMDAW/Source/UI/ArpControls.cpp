#include "ArpControls.h"
#include "Theme.h"

namespace hmdaw
{

namespace
{
    /** The direction names, in combo-box order. */
    const char* const kDirectionNames[] =
    {
        "上行", "下行", "上下", "下上", "随机", "按弹下顺序"
    };

    const char* const kVelocityModeNames[] = { "固定", "按演奏力度", "递减" };
}

ArpControls::ArpControls (ProjectDocument& documentToUse)
    : document (documentToUse)
{
    addAndMakeVisible (enableButton);
    enableButton.setClickingTogglesState (true);
    enableButton.setButtonText (NI_TXT ("琶音"));
    enableButton.setTooltip (NI_TXT ("琶音器：按住一组音，它会按速率一个一个弹出来（只作用于 MIDI 输入）"));
    enableButton.setColour (juce::TextButton::buttonOnColourId, theme::accentDim);
    enableButton.onClick = [this]
    {
        if (updating)
            return;

        write ([this] (midiarp::Settings& arp) { arp.enabled = enableButton.getToggleState(); });
    };

    addAndMakeVisible (rateBox);
    rateBox.setTooltip (NI_TXT ("速率：同步拍号（1/4 … 1/32，含三连）或自由 Hz"));
    rateBox.onChange = [this]
    {
        if (updating)
            return;

        const auto rates = midiarp::allRates();
        const auto index = juce::jlimit (0, (int) rates.size() - 1, rateBox.getSelectedId() - 1);

        write ([&rates, index] (midiarp::Settings& arp) { arp.rate = rates[(size_t) index]; });
    };

    addAndMakeVisible (freeHzSlider);
    freeHzSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    freeHzSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 48, 17);
    freeHzSlider.setRange (midiarp::Settings::kMinFreeHz, midiarp::Settings::kMaxFreeHz, 0.1);
    freeHzSlider.setTextValueSuffix (NI_TXT (" Hz"));
    freeHzSlider.onValueChange = [this]
    {
        if (updating)
            return;

        write ([this] (midiarp::Settings& arp) { arp.freeHz = freeHzSlider.getValue(); });
    };

    addAndMakeVisible (directionBox);
    directionBox.setTooltip (NI_TXT ("方向：和弦里的音按什么顺序弹"));
    for (int i = 0; i < (int) juce::numElementsInArray (kDirectionNames); ++i)
        directionBox.addItem (NI_TXT (kDirectionNames[i]), i + 1);

    directionBox.onChange = [this]
    {
        if (updating)
            return;

        const auto index = juce::jlimit (0, (int) juce::numElementsInArray (kDirectionNames) - 1,
                                         directionBox.getSelectedId() - 1);

        write ([index] (midiarp::Settings& arp) { arp.direction = (midiarp::Direction) index; });
    };

    addAndMakeVisible (octavesSlider);
    octavesSlider.setSliderStyle (juce::Slider::IncDecButtons);
    octavesSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 34, 17);
    octavesSlider.setRange (midiarp::Settings::kMinOctaves, midiarp::Settings::kMaxOctaves, 1);
    octavesSlider.onValueChange = [this]
    {
        if (updating)
            return;

        write ([this] (midiarp::Settings& arp) { arp.octaves = (int) octavesSlider.getValue(); });
    };

    addAndMakeVisible (gateSlider);
    gateSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    gateSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 40, 17);
    gateSlider.setRange (midiarp::Settings::kMinGatePercent, midiarp::Settings::kMaxGatePercent, 1);
    gateSlider.setTextValueSuffix (NI_TXT (" %"));
    gateSlider.onValueChange = [this]
    {
        if (updating)
            return;

        write ([this] (midiarp::Settings& arp) { arp.gatePercent = (int) gateSlider.getValue(); });
    };

    addAndMakeVisible (swingSlider);
    swingSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    swingSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 40, 17);
    swingSlider.setRange (midiarp::Settings::kMinSwingPercent, midiarp::Settings::kMaxSwingPercent, 1);
    swingSlider.setTextValueSuffix (NI_TXT (" %"));
    swingSlider.onValueChange = [this]
    {
        if (updating)
            return;

        write ([this] (midiarp::Settings& arp) { arp.swingPercent = (int) swingSlider.getValue(); });
    };

    addAndMakeVisible (velocityModeBox);
    velocityModeBox.setTooltip (NI_TXT ("力度：固定 / 按演奏力度 / 每步递减"));
    for (int i = 0; i < (int) juce::numElementsInArray (kVelocityModeNames); ++i)
        velocityModeBox.addItem (NI_TXT (kVelocityModeNames[i]), i + 1);

    velocityModeBox.onChange = [this]
    {
        if (updating)
            return;

        const auto index = juce::jlimit (0,
                                         (int) juce::numElementsInArray (kVelocityModeNames) - 1,
                                         velocityModeBox.getSelectedId() - 1);

        write ([index] (midiarp::Settings& arp)
               { arp.velocityMode = (midiarp::VelocityMode) index; });
    };

    addAndMakeVisible (fixedVelocitySlider);
    fixedVelocitySlider.setSliderStyle (juce::Slider::LinearHorizontal);
    fixedVelocitySlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 40, 17);
    fixedVelocitySlider.setRange (1.0, 127.0, 1.0);
    fixedVelocitySlider.onValueChange = [this]
    {
        if (updating)
            return;

        write ([this] (midiarp::Settings& arp)
               { arp.fixedVelocity = (int) fixedVelocitySlider.getValue(); });
    };

    addAndMakeVisible (holdButton);
    holdButton.setClickingTogglesState (true);
    holdButton.setButtonText (NI_TXT ("Hold"));
    holdButton.setTooltip (NI_TXT ("Hold：松开琴键以后这一段继续弹，再按一个新键才换一组"));
    holdButton.setColour (juce::TextButton::buttonOnColourId, theme::accentDim);
    holdButton.onClick = [this]
    {
        if (updating)
            return;

        write ([this] (midiarp::Settings& arp) { arp.hold = holdButton.getToggleState(); });
    };

    refresh();
}

midiarp::Settings ArpControls::settings() const
{
    return document.getProject().midiInput.arp;
}

void ArpControls::write (const std::function<void (midiarp::Settings&)>& mutation)
{
    // Configuration, not an edit: the project's MIDI input state is written
    // directly, so nothing lands on the undo stack (and the file knows about it
    // because midiInput is saved).
    document.applyDerivedEdit ([&mutation] (Project& project)
    {
        mutation (project.midiInput.arp);
        midiarp::clamp (project.midiInput.arp);
    });

    refresh();

    if (onChanged != nullptr)
        onChanged();
}

void ArpControls::refresh()
{
    const juce::ScopedValueSetter<bool> guard (updating, true);
    const auto arp = settings();

    enableButton.setToggleState (arp.enabled, juce::dontSendNotification);
    holdButton.setToggleState (arp.hold, juce::dontSendNotification);

    const auto rates = midiarp::allRates();
    auto rateId = 1;

    for (int i = 0; i < (int) rates.size(); ++i)
        if (rates[(size_t) i] == arp.rate)
            rateId = i + 1;

    // The rate list is rebuilt each time because the 自由 Hz entry carries the
    // frequency in its text.
    rateBox.clear (juce::dontSendNotification);

    for (int i = 0; i < (int) rates.size(); ++i)
    {
        midiarp::Settings forName;
        forName.rate = rates[(size_t) i];
        forName.freeHz = arp.freeHz;
        rateBox.addItem (midiarp::rateName (forName), i + 1);
    }

    rateBox.setSelectedId (rateId, juce::dontSendNotification);
    freeHzSlider.setValue (arp.freeHz, juce::dontSendNotification);
    freeHzSlider.setEnabled (arp.rate == midiarp::Rate::free);
    directionBox.setSelectedId ((int) arp.direction + 1, juce::dontSendNotification);
    octavesSlider.setValue (arp.octaves, juce::dontSendNotification);
    gateSlider.setValue (arp.gatePercent, juce::dontSendNotification);
    swingSlider.setValue (arp.swingPercent, juce::dontSendNotification);
    velocityModeBox.setSelectedId ((int) arp.velocityMode + 1, juce::dontSendNotification);
    fixedVelocitySlider.setValue (arp.fixedVelocity, juce::dontSendNotification);
    fixedVelocitySlider.setEnabled (arp.velocityMode == midiarp::VelocityMode::fixed);

    repaint();
}

juce::String ArpControls::summary() const
{
    const auto arp = settings();

    if (! arp.enabled)
        return NI_TXT ("琶音关");

    static const char* const directions[] = { "上行", "下行", "上下", "下上", "随机", "按弹下顺序" };
    static const char* const velocityModes[] = { "固定力度", "按演奏力度", "递减" };

    return NI_TXT ("琶音 ") + midiarp::rateName (arp)
               + NI_TXT (" · ") + NI_TXT (directions[juce::jlimit (0, 5, (int) arp.direction)])
               + NI_TXT (" · ") + juce::String (arp.octaves) + NI_TXT (" 个八度")
               + NI_TXT (" · Gate ") + juce::String (arp.gatePercent) + NI_TXT ("%")
               + NI_TXT (" · ") + NI_TXT (velocityModes[juce::jlimit (0, 2, (int) arp.velocityMode)])
               + (arp.hold ? NI_TXT (" · Hold") : juce::String());
}

juce::String ArpControls::getSummaryForTest() const
{
    return summary();
}

void ArpControls::paint (juce::Graphics& g)
{
    g.setColour (theme::panelBackground);
    g.fillRect (getLocalBounds());

    // The labels are painted on the component beside each control, the way the
    // synth panels do it.
    static const char* const labels[] = { "琶音", "速率", "自由 Hz", "方向", "八度",
                                          "Gate", "Swing", "力度", "固定力度", "Hold" };

    const auto cellWidth = getWidth() / kColumns;
    const auto cellHeight = kRowHeight + kRowGap;

    g.setFont (theme::font (11.5f));
    g.setColour (theme::dimText);

    for (int i = 0; i < kColumns * kRows; ++i)
    {
        const auto column = i % kColumns;
        const auto row = i / kColumns;
        auto cell = juce::Rectangle<int> (column * cellWidth, row * cellHeight,
                                          cellWidth, kRowHeight).reduced (2, 0);

        auto labelArea = cell.removeFromLeft (52);
        g.drawText (NI_TXT (labels[i]), labelArea, juce::Justification::centredLeft);
    }

}

void ArpControls::resized()
{
    const auto cellWidth = getWidth() / kColumns;
    const auto cellHeight = kRowHeight + kRowGap;

    const auto cellFor = [cellWidth, cellHeight] (int index)
    {
        const auto column = index % kColumns;
        const auto row = index / kColumns;
        return juce::Rectangle<int> (column * cellWidth, row * cellHeight, cellWidth, kRowHeight)
            .reduced (2, 1);
    };

    auto place = [this, &cellFor] (int index, juce::Component& control)
    {
        auto cell = cellFor (index);
        cell.removeFromLeft (52);
        control.setBounds (cell);
    };

    place (0, enableButton);
    place (1, rateBox);
    place (2, freeHzSlider);
    place (3, directionBox);
    place (4, octavesSlider);
    place (5, gateSlider);
    place (6, swingSlider);
    place (7, velocityModeBox);
    place (8, fixedVelocitySlider);
    place (9, holdButton);
}

void ArpControls::triggerEnableForTest()
{
    enableButton.setToggleState (! enableButton.getToggleState(), juce::dontSendNotification);
    enableButton.onClick();
}

void ArpControls::setRateForTest (int comboId)
{
    rateBox.setSelectedId (comboId, juce::sendNotificationSync);
}

void ArpControls::setDirectionForTest (int comboId)
{
    directionBox.setSelectedId (comboId, juce::sendNotificationSync);
}

void ArpControls::setOctavesForTest (int octaves)
{
    octavesSlider.setValue (octaves, juce::sendNotificationSync);
}

void ArpControls::setGateForTest (int percent)
{
    gateSlider.setValue (percent, juce::sendNotificationSync);
}

void ArpControls::setSwingForTest (int percent)
{
    swingSlider.setValue (percent, juce::sendNotificationSync);
}

void ArpControls::setVelocityModeForTest (int comboId)
{
    velocityModeBox.setSelectedId (comboId, juce::sendNotificationSync);
}

void ArpControls::setHoldForTest (bool shouldHold)
{
    holdButton.setToggleState (shouldHold, juce::dontSendNotification);
    holdButton.onClick();
}

} // namespace hmdaw

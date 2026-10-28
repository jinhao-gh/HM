#pragma once

#include "../Core/ProjectDocument.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace nidaw
{

/** 琶音器's controls, as one component: the same set is shown in the virtual
    keyboard window and in the MIDI input window, and both work on the same piece of
    state (`Project::midiInput.arp`), so a change in one is a change in the other.

    It is configuration, not an edit: nothing here pushes an undo step (the same
    rule the MIDI mapping itself follows).

    The controls are laid out as a five-column by two-row grid, so the component
    works both across the bottom of the keyboard window and in a row of the mapping
    window:

        琶音 | 速率 | 自由 Hz | 方向 | 八度
        Gate | Swing | 力度 | 固定力度 | Hold
*/
class ArpControls : public juce::Component
{
public:
    explicit ArpControls (ProjectDocument& documentToUse);

    /** Something changed: the owner hands the new settings to the player. */
    std::function<void()> onChanged;

    /** Reads the settings back out of the project (the source of truth). */
    void refresh();

    void paint (juce::Graphics& g) override;
    void resized() override;

    static constexpr int kRows = 2;
    static constexpr int kColumns = 5;
    static constexpr int kRowHeight = 22;
    static constexpr int kRowGap = 4;
    /** What `resized()` wants: two rows and a little margin. */
    static constexpr int kDesignHeight = kRows * kRowHeight + (kRows - 1) * kRowGap + 4;

    // ---- diagnostic hooks ---------------------------------------------------
    juce::Rectangle<int> getEnableBoundsForTest() const { return enableButton.getBounds(); }
    juce::Rectangle<int> getRateBoundsForTest() const { return rateBox.getBounds(); }
    juce::Rectangle<int> getDirectionBoundsForTest() const { return directionBox.getBounds(); }
    juce::Rectangle<int> getOctavesBoundsForTest() const { return octavesSlider.getBounds(); }
    juce::Rectangle<int> getGateBoundsForTest() const { return gateSlider.getBounds(); }
    juce::Rectangle<int> getSwingBoundsForTest() const { return swingSlider.getBounds(); }
    juce::Rectangle<int> getVelocityModeBoundsForTest() const { return velocityModeBox.getBounds(); }
    juce::Rectangle<int> getHoldBoundsForTest() const { return holdButton.getBounds(); }
    juce::String getEnableTextForTest() const { return enableButton.getButtonText(); }
    void triggerEnableForTest();
    void setRateForTest (int comboId);
    void setDirectionForTest (int comboId);
    void setOctavesForTest (int octaves);
    void setGateForTest (int percent);
    void setSwingForTest (int percent);
    void setVelocityModeForTest (int comboId);
    void setHoldForTest (bool shouldHold);
    juce::String getSummaryForTest() const;

private:
    midiarp::Settings settings() const;
    /** Applies one change to the project and tells the owner. */
    void write (const std::function<void (midiarp::Settings&)>& mutation);
    juce::String summary() const;

    ProjectDocument& document;

    juce::TextButton enableButton, holdButton;
    juce::ComboBox rateBox, directionBox, velocityModeBox;
    juce::Slider freeHzSlider, octavesSlider, gateSlider, swingSlider, fixedVelocitySlider;
    bool updating = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ArpControls)
};

} // namespace nidaw

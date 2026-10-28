#include "AppLookAndFeel.h"
#include "Theme.h"

namespace nidaw
{

AppLookAndFeel::AppLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, theme::windowBackground);
    setColour (juce::DocumentWindow::textColourId, theme::text);

    setColour (juce::Label::textColourId, theme::text);
    setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);

    setColour (juce::TextEditor::backgroundColourId, theme::panelRaised);
    setColour (juce::TextEditor::textColourId, theme::text);
    setColour (juce::TextEditor::outlineColourId, theme::outline);
    setColour (juce::TextEditor::focusedOutlineColourId, theme::accent);
    setColour (juce::TextEditor::highlightColourId, theme::accentDim);
    setColour (juce::TextEditor::highlightedTextColourId, theme::text);

    setColour (juce::TextButton::buttonColourId, theme::panelRaised);
    setColour (juce::TextButton::buttonOnColourId, theme::accentDim);
    setColour (juce::TextButton::textColourOffId, theme::text);
    setColour (juce::TextButton::textColourOnId, theme::text);

    setColour (juce::ToggleButton::textColourId, theme::text);
    setColour (juce::ToggleButton::tickColourId, theme::accent);
    setColour (juce::ToggleButton::tickDisabledColourId, theme::dimText);

    setColour (juce::ComboBox::backgroundColourId, theme::panelRaised);
    setColour (juce::ComboBox::textColourId, theme::text);
    setColour (juce::ComboBox::outlineColourId, theme::outline);
    setColour (juce::ComboBox::arrowColourId, theme::dimText);
    setColour (juce::ComboBox::buttonColourId, theme::panelRaised);

    setColour (juce::PopupMenu::backgroundColourId, theme::panelRaised);
    setColour (juce::PopupMenu::textColourId, theme::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, theme::accentDim);
    setColour (juce::PopupMenu::highlightedTextColourId, theme::text);

    setColour (juce::Slider::backgroundColourId, theme::panelBackground);
    setColour (juce::Slider::thumbColourId, theme::accent);
    setColour (juce::Slider::trackColourId, theme::accentDim);
    setColour (juce::Slider::textBoxTextColourId, theme::text);
    setColour (juce::Slider::textBoxBackgroundColourId, theme::panelRaised);
    setColour (juce::Slider::textBoxOutlineColourId, theme::outline);

    setColour (juce::ScrollBar::backgroundColourId, theme::panelBackground);
    setColour (juce::ScrollBar::thumbColourId, theme::outline);
    setColour (juce::ScrollBar::trackColourId, theme::panelBackground);

    setColour (juce::ListBox::backgroundColourId, theme::panelBackground);
    setColour (juce::ListBox::outlineColourId, theme::outline);

    setColour (juce::AlertWindow::backgroundColourId, theme::panelRaised);
    setColour (juce::AlertWindow::textColourId, theme::text);
    setColour (juce::AlertWindow::outlineColourId, theme::outline);

    setColour (juce::TooltipWindow::backgroundColourId, theme::panelRaised);
    setColour (juce::TooltipWindow::textColourId, theme::text);
    setColour (juce::TooltipWindow::outlineColourId, theme::outline);
}

} // namespace nidaw

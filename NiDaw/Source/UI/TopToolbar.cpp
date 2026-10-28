#include "TopToolbar.h"
#include "Theme.h"

namespace nidaw
{

TopToolbar::TopToolbar (ProjectDocument& documentToUse)
    : document (documentToUse)
{
    auto setup = [this] (juce::TextButton& button, const juce::String& text)
    {
        addAndMakeVisible (button);
        button.setButtonText (text);
    };

    setup (newButton, NI_TXT ("新建工程"));
    setup (openButton, NI_TXT ("打开"));
    setup (saveButton, NI_TXT ("保存"));
    setup (saveAsButton, NI_TXT ("另存为"));
    setup (exportButton, NI_TXT ("导出 WAV"));
    setup (mixerButton, NI_TXT ("混音台"));
    mixerButton.setTooltip (NI_TXT ("混音台：每条轨一根条带（快捷键 F3）"));
    setup (midiButton, NI_TXT ("MIDI输入"));
    midiButton.setTooltip (NI_TXT ("MIDI 输入：选设备、把琴键绑到参考线组的频率上"));
    setup (keyboardButton, NI_TXT ("虚拟键盘"));
    keyboardButton.setTooltip (NI_TXT ("虚拟键盘：点它或用电脑键盘弹奏，走与 MIDI 键盘同一条路（快捷键 Alt+K）"));

    newButton.onClick    = [this] { if (onNewProject) onNewProject(); };
    openButton.onClick   = [this] { if (onOpenProject) onOpenProject(); };
    saveButton.onClick   = [this] { if (onSaveProject) onSaveProject(); };
    saveAsButton.onClick = [this] { if (onSaveProjectAs) onSaveProjectAs(); };
    exportButton.onClick = [this] { if (onExportWav) onExportWav(); };
    mixerButton.onClick  = [this] { if (onOpenMixer) onOpenMixer(); };
    midiButton.onClick   = [this] { if (onOpenMidiInput) onOpenMidiInput(); };
    keyboardButton.onClick = [this] { if (onOpenVirtualKeyboard) onOpenVirtualKeyboard(); };

    addAndMakeVisible (titleLabel);
    titleLabel.setFont (theme::font (13.5f, true));
    titleLabel.setJustificationType (juce::Justification::centredRight);
    titleLabel.setColour (juce::Label::textColourId, theme::text);

    addAndMakeVisible (modifiedLabel);
    modifiedLabel.setFont (theme::font (11.5f));
    modifiedLabel.setJustificationType (juce::Justification::centredRight);
    modifiedLabel.setColour (juce::Label::textColourId, theme::accent);

    document.addChangeListener (this);
    refresh();
}

TopToolbar::~TopToolbar()
{
    document.removeChangeListener (this);
}

void TopToolbar::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refresh();
}

void TopToolbar::refresh()
{
    titleLabel.setText (document.getProjectName() + NI_TXT ("  ·  HM"),
                        juce::dontSendNotification);
    modifiedLabel.setText (document.isModified() ? NI_TXT ("● 未保存") : juce::String(),
                           juce::dontSendNotification);
}

void TopToolbar::paint (juce::Graphics& g)
{
    g.setColour (theme::panelBackground);
    g.fillRect (getLocalBounds());
    g.setColour (theme::outline);
    g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());
}

void TopToolbar::resized()
{
    auto area = getLocalBounds().reduced (8, 7);

    newButton.setBounds (area.removeFromLeft (92));
    area.removeFromLeft (6);
    openButton.setBounds (area.removeFromLeft (66));
    area.removeFromLeft (6);
    saveButton.setBounds (area.removeFromLeft (66));
    area.removeFromLeft (6);
    saveAsButton.setBounds (area.removeFromLeft (78));
    area.removeFromLeft (6);
    exportButton.setBounds (area.removeFromLeft (96));
    area.removeFromLeft (6);
    mixerButton.setBounds (area.removeFromLeft (86));
    area.removeFromLeft (6);
    midiButton.setBounds (area.removeFromLeft (96));
    area.removeFromLeft (6);
    keyboardButton.setBounds (area.removeFromLeft (96));

    modifiedLabel.setBounds (area.removeFromRight (76));
    titleLabel.setBounds (area.removeFromRight (300));
}

} // namespace nidaw

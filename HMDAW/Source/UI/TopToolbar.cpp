#include "TopToolbar.h"
#include "Theme.h"

namespace hmdaw
{

TopToolbar::TopToolbar (ProjectDocument& documentToUse)
    : document (documentToUse)
{
    auto setup = [this] (juce::TextButton& button, const juce::String& text)
    {
        addAndMakeVisible (button);
        button.setButtonText (text);
        button.setClickingTogglesState (false);
        button.setColour (juce::TextButton::buttonColourId, theme::panelRaised);
        button.setColour (juce::TextButton::textColourOffId, theme::dimText);
    };

    setup (fileButton, NI_TXT ("文件"));
    setup (studioButton, NI_TXT ("工作室"));
    setup (arrangeButton, NI_TXT ("编排"));
    setup (routingButton, NI_TXT ("接线"));
    setup (mixerButton, NI_TXT ("混音台"));

    fileButton.onClick = [this] { showFileMenu(); };
    studioButton.onClick = [this] { selectWorkspace (AppState::Workspace::studio); };
    arrangeButton.onClick = [this] { selectWorkspace (AppState::Workspace::arrange); };
    routingButton.onClick = [this] { selectWorkspace (AppState::Workspace::routing); };
    mixerButton.onClick = [this] { selectWorkspace (AppState::Workspace::mixer); };

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
    setWorkspace (currentWorkspace);
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

void TopToolbar::showFileMenu()
{
    juce::PopupMenu menu;
    menu.addItem (1, NI_TXT ("新建工程"));
    menu.addItem (2, NI_TXT ("打开工程…"));
    menu.addSeparator();
    menu.addItem (3, NI_TXT ("保存"));
    menu.addItem (4, NI_TXT ("另存为…"));
    menu.addSeparator();
    menu.addItem (5, NI_TXT ("导出 WAV…"));
    menu.addSeparator();
    menu.addItem (6, NI_TXT ("退出"));

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&fileButton),
                        [this] (int result)
    {
        switch (result)
        {
            case 1: if (onNewProject) onNewProject(); break;
            case 2: if (onOpenProject) onOpenProject(); break;
            case 3: if (onSaveProject) onSaveProject(); break;
            case 4: if (onSaveProjectAs) onSaveProjectAs(); break;
            case 5: if (onExportWav) onExportWav(); break;
            case 6: juce::JUCEApplication::getInstance()->systemRequestedQuit(); break;
            default: break;
        }
    });
}

void TopToolbar::selectWorkspace (AppState::Workspace workspace)
{
    if (currentWorkspace == workspace)
        return;

    currentWorkspace = workspace;

    if (onWorkspaceChanged)
        onWorkspaceChanged (workspace);

    setWorkspace (workspace);
}

void TopToolbar::setWorkspace (AppState::Workspace workspace)
{
    currentWorkspace = workspace;

    auto setActive = [this, workspace] (juce::TextButton& button,
                                        AppState::Workspace page)
    {
        const auto active = workspace == page;
        button.setColour (juce::TextButton::buttonColourId,
                          active ? theme::accentDim : theme::panelRaised);
        button.setColour (juce::TextButton::textColourOffId,
                          active ? theme::text : theme::dimText);
        button.repaint();
    };

    setActive (studioButton, AppState::Workspace::studio);
    setActive (arrangeButton, AppState::Workspace::arrange);
    setActive (routingButton, AppState::Workspace::routing);
    setActive (mixerButton, AppState::Workspace::mixer);
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

    fileButton.setBounds (area.removeFromLeft (64));
    area.removeFromLeft (10);

    studioButton.setBounds (area.removeFromLeft (78));
    area.removeFromLeft (4);
    arrangeButton.setBounds (area.removeFromLeft (64));
    area.removeFromLeft (4);
    routingButton.setBounds (area.removeFromLeft (64));
    area.removeFromLeft (4);
    mixerButton.setBounds (area.removeFromLeft (78));

    titleLabel.setBounds (area.removeFromRight (300));
    modifiedLabel.setBounds (area.removeFromRight (76));
}

} // namespace hmdaw

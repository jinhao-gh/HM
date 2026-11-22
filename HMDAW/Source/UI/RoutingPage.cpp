#include "RoutingPage.h"
#include "Theme.h"
#include "../Core/Types.h"

namespace hmdaw
{

void RoutingPage::paint (juce::Graphics& g)
{
    g.fillAll (theme::panelBackground);

    auto area = getLocalBounds().reduced (40);
    g.setColour (theme::text);
    g.setFont (theme::font (26.0f, true));
    g.drawText (NI_TXT ("接线"), area.removeFromTop (42),
                juce::Justification::centredLeft);

    g.setColour (theme::dimText);
    g.setFont (theme::font (14.0f));
    g.drawFittedText (NI_TXT ("效果器的组织与接线将在后续版本提供。\n"
                              "当前请继续使用每轨的「效果」插入链。"),
                      area.removeFromTop (100), juce::Justification::topLeft, 4);
}

void RoutingPage::resized() {}

} // namespace hmdaw

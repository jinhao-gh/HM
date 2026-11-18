#pragma once

#include "../Core/Types.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace hmdaw
{

/** The RATE/LEVEL envelope of one FM operator, drawn as a curve.

    Four segments - attack (R1 -> L1), decay (R2 -> L2), the R3 segment down to
    the sustain level L3, and the release (R4 -> L4). The horizontal width of each
    segment follows `fm::rateToSeconds`, compressed with a square root so that a
    near-instant attack is still visible next to a slow release; the vertical axis
    uses the same level-to-amplitude curve the engine uses, so the picture matches
    what is heard.

    Read only: the eight sliders underneath are what edit it.
*/
class EnvelopeCurve : public juce::Component
{
public:
    EnvelopeCurve();

    void setOperator (const FmOperator& newOperator);

    void paint (juce::Graphics& g) override;

private:
    FmOperator op;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EnvelopeCurve)
};

} // namespace hmdaw

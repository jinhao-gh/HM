#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace hmdaw::theme
{

inline const juce::Colour windowBackground   { 0xff17171b };
inline const juce::Colour panelBackground    { 0xff22222a };
inline const juce::Colour panelRaised        { 0xff2b2b34 };
inline const juce::Colour outline            { 0xff3a3a45 };
inline const juce::Colour text               { 0xffe4e4ec };
inline const juce::Colour dimText            { 0xff8d8d9e };
inline const juce::Colour accent             { 0xff4fa3ff };
inline const juce::Colour accentDim          { 0xff2d5c8a };

inline const juce::Colour gridBackground     { 0xff1b1b21 };
inline const juce::Colour gridBeatLine       { 0xff2b2b34 };
inline const juce::Colour gridBarLine        { 0xff4b4b59 };
inline const juce::Colour gridTempoStrip     { 0xff26262f };

inline const juce::Colour referenceLine      { 0xff5fd2c8 };
inline const juce::Colour referenceLineTarget{ 0xff7fe0a0 };
inline const juce::Colour referenceLineDim   { 0xff3e6f6b };
inline const juce::Colour referenceLineManual{ 0xff7f8fb0 };

inline const juce::Colour playhead           { 0xffff6b5b };
inline const juce::Colour noteSelectedOutline{ 0xffffffff };
inline const juce::Colour laneBackground     { 0xff1d1d23 };
inline const juce::Colour laneHeader         { 0xff26262f };

inline juce::Colour trackColour (int index)
{
    static const juce::Colour palette[] =
    {
        juce::Colour (0xff5aa9e6), juce::Colour (0xff7ed957), juce::Colour (0xffe6b95a),
        juce::Colour (0xffd96a9b), juce::Colour (0xff9b8cf2), juce::Colour (0xff4fd1c5),
        juce::Colour (0xffe07a5f), juce::Colour (0xffb0b7c3)
    };

    const auto count = (int) (sizeof (palette) / sizeof (palette[0]));
    return palette[((index % count) + count) % count];
}

inline juce::Font font (float height, bool bold = false)
{
    return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
}

} // namespace hmdaw::theme

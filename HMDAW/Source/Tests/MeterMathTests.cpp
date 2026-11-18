#include "TestSupport.h"

#include "../Core/MeterMath.h"

/** The meter's falling bar, its peak marker and its clip dot.

    The audio thread only reports the peak of the block it just rendered; the
    needle's shape is arithmetic on the message thread, which is what these check -
    no device, no engine, no timing.
*/
class MeterMathTests : public juce::UnitTest
{
public:
    MeterMathTests() : juce::UnitTest ("MeterMath", "hmdaw") {}

    void runTest() override
    {
        beginTest (NI_TXT ("电平条瞬间起、按每秒 20 dB 落"));
        {
            // A peak arrives: the bar is at that level straight away.
            expectWithinAbsoluteError (meter::nextMeterLevel (0.0f, 0.8f, 1.0 / 30.0),
                                       0.8f, 1.0e-6f);

            // No new peak: 0.1 s at 20 dB/s is 2 dB, i.e. x0.7943.
            expectWithinAbsoluteError (meter::nextMeterLevel (0.8f, 0.0f, 0.1),
                                       0.8f * 0.7943282f, 1.0e-4f);

            // It keeps sliding, and reaches nothing at all rather than a denormal
            // that never quite gets there.
            auto level = 0.8f;

            for (int i = 0; i < 200; ++i)
                level = meter::nextMeterLevel (level, 0.0f, 0.1);

            expectEquals (level, 0.0f);

            // A louder peak while it is falling snaps it back up.
            expectWithinAbsoluteError (meter::nextMeterLevel (level, 0.25f, 0.033),
                                       0.25f, 1.0e-6f);
        }

        beginTest (NI_TXT ("峰值保持 1.5 秒，然后跟着电平条一起落"));
        {
            double holdSecondsLeft = 0.0;
            auto hold = meter::nextPeakHold (0.0f, 0.7f, holdSecondsLeft, 1.0 / 30.0);

            expectWithinAbsoluteError (hold, 0.7f, 1.0e-6f);
            expectWithinAbsoluteError (holdSecondsLeft, meter::kPeakHoldSeconds, 1.0e-6);

            // The bar falls away, the marker stays exactly where it was...
            for (int i = 0; i < 5; ++i)
                hold = meter::nextPeakHold (hold, 0.0f, holdSecondsLeft, 0.25);

            expectWithinAbsoluteError (hold, 0.7f, 1.0e-6f);
            expectLessThan (holdSecondsLeft, meter::kPeakHoldSeconds);

            // ...and once 1.5 s are up it starts sliding down with the bar.
            for (int i = 0; i < 8; ++i)
                hold = meter::nextPeakHold (hold, 0.0f, holdSecondsLeft, 0.25);

            expectLessThan (hold, 0.7f);

            // A new, higher peak resets the whole hold.
            double resetSecondsLeft = 0.0;
            const auto higher = meter::nextPeakHold (0.1f, 0.9f, resetSecondsLeft, 0.1);

            expectWithinAbsoluteError (higher, 0.9f, 1.0e-6f);
            expectWithinAbsoluteError (resetSecondsLeft, meter::kPeakHoldSeconds, 1.0e-6);
        }

        beginTest (NI_TXT ("越过满刻度才是削顶"));
        {
            expect (! meter::isClipping (0.0f));
            expect (! meter::isClipping (1.0f));
            expect (meter::isClipping (1.0001f));
            expect (meter::isClipping (4.0f));
        }
    }
};

namespace
{
    MeterMathTests meterMathTests;
}

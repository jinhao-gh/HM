#include "TestSupport.h"

#include "../Core/MidiArp.h"

#include <set>

namespace hmdaw
{

/** 琶音器's arithmetic: the step lengths, the order the notes come out in, the
    gate, swing and the velocity modes - all pure, so none of it needs an audio
    device or a window. */
class MidiArpTests : public juce::UnitTest
{
public:
    MidiArpTests() : juce::UnitTest ("MidiArp", "hmdaw") {}

    void runTest() override
    {
        using namespace midiarp;

        beginTest (NI_TXT ("九种速率：同步拍号按 PPQ 算，自由 Hz 按当前速度折算"));
        {
            Settings settings;
            settings.octaves = 1;

            const auto ticksFor = [&settings] (Rate rate, double bpm = 120.0)
            {
                settings.rate = rate;
                return stepTicksFor (settings, bpm, kTicksPerQuarter);
            };

            expectEquals (ticksFor (Rate::quarter), (juce::int64) 960);
            expectEquals (ticksFor (Rate::quarterTriplet), (juce::int64) 640);
            expectEquals (ticksFor (Rate::eighth), (juce::int64) 480);
            expectEquals (ticksFor (Rate::eighthTriplet), (juce::int64) 320);
            expectEquals (ticksFor (Rate::sixteenth), (juce::int64) 240);
            expectEquals (ticksFor (Rate::sixteenthTriplet), (juce::int64) 160);
            expectEquals (ticksFor (Rate::thirtySecond), (juce::int64) 120);
            expectEquals (ticksFor (Rate::thirtySecondTriplet), (juce::int64) 80);

            // A note value does not care about the tempo; 自由 Hz does.
            expectEquals (ticksFor (Rate::eighth, 60.0), (juce::int64) 480,
                          NI_TXT ("1/8 在 60 bpm 也还是 480 tick"));

            settings.rate = Rate::free;
            settings.freeHz = 8.0;
            expectEquals (stepTicksFor (settings, 120.0, kTicksPerQuarter), (juce::int64) 240,
                          NI_TXT ("8 Hz 在 120 bpm 是 1/8 秒 = 240 tick"));
            expectEquals (stepTicksFor (settings, 240.0, kTicksPerQuarter), (juce::int64) 480,
                          NI_TXT ("速度翻倍，同一频率的 tick 数也翻倍"));

            // ...and the seconds agree with the ticks.
            settings.rate = Rate::eighth;
            expect (std::abs (stepSecondsFor (settings, 120.0, kTicksPerQuarter) - 0.25) < 1.0e-9,
                    NI_TXT ("120 bpm 的 1/8 是 0.25 秒"));
            expect (std::abs (stepSecondsFor (settings, 60.0, kTicksPerQuarter) - 0.5) < 1.0e-9,
                    NI_TXT ("60 bpm 的 1/8 是 0.5 秒"));
        }

        beginTest (NI_TXT ("六种方向：上行 / 下行 / 上下 / 下上 / 随机 / 按弹下顺序"));
        {
            Settings settings;
            const std::vector<int> chord { 60, 64, 67 };

            settings.direction = Direction::up;
            expect (orderFor (settings, chord) == std::vector<int> { 60, 64, 67 });

            settings.direction = Direction::down;
            expect (orderFor (settings, chord) == std::vector<int> { 67, 64, 60 });

            settings.direction = Direction::upDown;
            expect (orderFor (settings, chord) == std::vector<int> { 60, 64, 67, 64 },
                    NI_TXT ("上下不把折返点弹两遍"));

            settings.direction = Direction::downUp;
            expect (orderFor (settings, chord) == std::vector<int> { 67, 64, 60, 64 });

            settings.direction = Direction::asPlayed;
            expect (orderFor (settings, { 67, 60, 64 }) == std::vector<int> { 67, 60, 64 },
                    NI_TXT ("按弹下顺序：就是你按的那个顺序"));

            settings.direction = Direction::random;
            const auto shuffled = orderFor (settings, chord, 7u);
            expectEquals ((int) shuffled.size(), 3);
            expect (std::set<int> (shuffled.begin(), shuffled.end())
                        == std::set<int> { 60, 64, 67 },
                    NI_TXT ("随机也只是把这三个音换个顺序"));
            expect (orderFor (settings, chord, 7u) == shuffled,
                    NI_TXT ("同一个种子给出同一个顺序（自检能断言）"));

            // Low to high, whatever order the keys went down in.
            settings.direction = Direction::up;
            expect (orderFor (settings, { 67, 60, 64 }) == std::vector<int> { 60, 64, 67 });
        }

        beginTest (NI_TXT ("八度：整组往上叠，下行时从最高那个开始"));
        {
            Settings settings;
            settings.octaves = 2;

            expect (orderFor (settings, { 60, 64, 67 })
                        == std::vector<int> { 60, 64, 67, 72, 76, 79 },
                    NI_TXT ("两个八度：一组接一组"));

            settings.direction = Direction::down;
            expect (orderFor (settings, { 60, 64, 67 })
                        == std::vector<int> { 79, 76, 72, 67, 64, 60 });

            settings.octaves = 4;
            expectEquals ((int) orderFor (settings, { 60, 64, 67 }).size(), 12);

            settings.octaves = 99;   // clamped
            clamp (settings);
            expectEquals (settings.octaves, Settings::kMaxOctaves);
        }

        beginTest (NI_TXT ("Gate / Swing / 力度三种模式"));
        {
            Settings settings;
            settings.gatePercent = 50;
            expectEquals (gateTicksFor (settings, 240), (juce::int64) 120);

            settings.gatePercent = 5;
            expectEquals (gateTicksFor (settings, 120), (juce::int64) 6);
            expectEquals (gateTicksFor (settings, 10), (juce::int64) 1,
                          NI_TXT ("再短也至少 1 tick"));

            settings.gatePercent = 200;
            expectEquals (gateTicksFor (settings, 240), (juce::int64) 480);

            settings.swingPercent = 0;
            expectEquals (offsetTicksForStep (settings, 240, 1), (juce::int64) 0);

            settings.swingPercent = 50;
            expectEquals (offsetTicksForStep (settings, 240, 0), (juce::int64) 0,
                          NI_TXT ("正拍不动"));
            expectEquals (offsetTicksForStep (settings, 240, 1), (juce::int64) 120,
                          NI_TXT ("反拍推后半个步长"));
            expectEquals (offsetTicksForStep (settings, 240, 3), (juce::int64) 120);
            expectEquals (offsetTicksForStep (settings, 240, 2), (juce::int64) 0);

            settings.velocityMode = VelocityMode::fixed;
            settings.fixedVelocity = 64;
            expectEquals (velocityForStep (settings, 100, 0, 3), 64);
            expectEquals (velocityForStep (settings, 100, 2, 3), 64);

            settings.velocityMode = VelocityMode::played;
            expectEquals (velocityForStep (settings, 88, 0, 3), 88);
            expectEquals (velocityForStep (settings, 88, 2, 3), 88);

            settings.velocityMode = VelocityMode::decrescendo;
            const auto first = velocityForStep (settings, 120, 0, 3);
            const auto second = velocityForStep (settings, 120, 1, 3);
            const auto third = velocityForStep (settings, 120, 2, 3);
            const auto wrapped = velocityForStep (settings, 120, 3, 3);

            expectEquals (first, 120);
            expectLessThan (second, first);
            expectLessThan (third, second);
            expectEquals (wrapped, first, NI_TXT ("一遍走完从演奏力度重新开始"));
            expectGreaterThan (third, Settings::kMinDecrescendoVelocity - 1,
                               NI_TXT ("不会掉到地板以下"));
        }

        beginTest (NI_TXT ("Hold：松开琴键继续跑，再按新键才换一组"));
        {
            LatchState state;

            updateLatch (state, { 60, 64 }, false);
            expect (state.notes == std::vector<int> { 60, 64 }, NI_TXT ("不 Hold 时就是按着的音"));

            updateLatch (state, {}, false);
            expect (state.notes.empty(), NI_TXT ("不 Hold 时松开就没了"));

            updateLatch (state, { 60 }, true);
            expect (state.notes == std::vector<int> { 60 });

            updateLatch (state, { 60, 64 }, true);
            expect (state.notes == std::vector<int> { 60, 64 },
                    NI_TXT ("Hold 时新按的音加进这一组"));

            updateLatch (state, {}, true);
            expect (state.notes == std::vector<int> { 60, 64 },
                    NI_TXT ("Hold：全部松开以后这一组继续"));

            updateLatch (state, { 67 }, true);
            expect (state.notes == std::vector<int> { 67 },
                    NI_TXT ("全部松开之后按的第一个音开始新的一组"));

            updateLatch (state, {}, true);
            updateLatch (state, {}, false);
            expect (state.notes.empty(), NI_TXT ("关掉 Hold 立刻回到按着的音"));
        }

        beginTest (NI_TXT ("退化：空集合、单音、越界的取值"));
        {
            Settings settings;
            expect (orderFor (settings, {}).empty(), NI_TXT ("没有按着的音就没有步"));
            expectEquals (noteForStep ({}, 0), -1, NI_TXT ("空顺序没有音"));

            const std::vector<int> one { 60 };
            expectEquals (noteForStep (one, 0), 60);
            expectEquals (noteForStep (one, 5), 60, NI_TXT ("单音就一直弹它"));

            const std::vector<int> three { 60, 64, 67 };
            expectEquals (noteForStep (three, 0), 60);
            expectEquals (noteForStep (three, 2), 67);
            expectEquals (noteForStep (three, 3), 60, NI_TXT ("循环回到第一个"));
            expectEquals (noteForStep (three, -1), 67, NI_TXT ("负的步号也绕得回来"));

            settings.octaves = 0;
            settings.gatePercent = 0;
            settings.swingPercent = 100;
            settings.freeHz = 0.0;
            settings.fixedVelocity = 0;
            clamp (settings);

            expectEquals (settings.octaves, Settings::kMinOctaves);
            expectEquals (settings.gatePercent, Settings::kMinGatePercent);
            expectEquals (settings.swingPercent, Settings::kMaxSwingPercent);
            expectEquals (settings.fixedVelocity, 1);
            expect (settings.freeHz >= Settings::kMinFreeHz, NI_TXT ("自由 Hz 也被夹住"));
        }

        beginTest (NI_TXT ("琶音设置随工程 / 预设往返，旧文件读成关"));
        {
            Settings settings;
            settings.enabled = true;
            settings.rate = Rate::eighthTriplet;
            settings.freeHz = 12.5;
            settings.direction = Direction::downUp;
            settings.octaves = 3;
            settings.gatePercent = 35;
            settings.swingPercent = 40;
            settings.velocityMode = VelocityMode::decrescendo;
            settings.fixedVelocity = 77;
            settings.hold = true;

            const auto back = settingsFromVar (settingsToVar (settings));
            expect (isSame (settings, back), NI_TXT ("逐字段往返一致"));

            const auto missing = settingsFromVar (juce::var());
            expect (! missing.enabled, NI_TXT ("旧工程 / 旧预设缺这一段 → 琶音关"));
            expectEquals ((int) missing.rate, (int) Rate::sixteenth, NI_TXT ("默认 1/16"));
            expectEquals (missing.octaves, 1);
            expectEquals (missing.gatePercent, 50);
            expect (! missing.hold);

            // A hand-edited file cannot put the player out of step.
            auto* object = new juce::DynamicObject();
            object->setProperty ("enabled", true);
            object->setProperty ("octaves", 99);
            object->setProperty ("gate", -3);
            object->setProperty ("swing", 999);
            object->setProperty ("rate", 42);
            object->setProperty ("direction", -7);
            object->setProperty ("velocityMode", 88);
            const auto clamped = settingsFromVar (juce::var (object));

            expectEquals (clamped.octaves, Settings::kMaxOctaves);
            expectEquals (clamped.gatePercent, Settings::kMinGatePercent);
            expectEquals (clamped.swingPercent, Settings::kMaxSwingPercent);
            expect ((int) clamped.rate >= 0 && (int) clamped.rate <= (int) Rate::free,
                    NI_TXT ("速率落在枚举里"));
            expect ((int) clamped.direction >= 0
                        && (int) clamped.direction <= (int) Direction::asPlayed,
                    NI_TXT ("方向落在枚举里"));
            expect ((int) clamped.velocityMode >= 0
                        && (int) clamped.velocityMode <= (int) VelocityMode::decrescendo,
                    NI_TXT ("力度模式落在枚举里"));
        }

        beginTest (NI_TXT ("速率的显示名"));
        {
            Settings settings;
            settings.rate = Rate::sixteenth;
            expectEquals (rateName (settings), juce::String ("1/16"));

            settings.rate = Rate::quarterTriplet;
            expectEquals (rateName (settings), juce::String ("1/4T"));

            settings.rate = Rate::free;
            settings.freeHz = 6.5;
            expect (rateName (settings).contains ("6.5"), NI_TXT ("自由速率写频率"));
            expect (rateName (settings).contains ("Hz"));

            expectEquals ((int) allRates().size(), 9, NI_TXT ("九种速率：八种拍号 + 自由"));
        }
    }
};

static MidiArpTests midiArpTests;

} // namespace hmdaw

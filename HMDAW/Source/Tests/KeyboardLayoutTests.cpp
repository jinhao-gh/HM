#include "TestSupport.h"

#include "../Core/KeyboardLayouts.h"

#include <set>

namespace hmdaw
{

/** The computer-keyboard layouts: which key plays which note, and the bass side's
    circle of fifths. All pure, so none of it needs a window. */
class KeyboardLayoutTests : public juce::UnitTest
{
public:
    KeyboardLayoutTests() : juce::UnitTest ("KeyboardLayout", "hmdaw") {}

    void runTest() override
    {
        using namespace keyboard;

        beginTest (NI_TXT ("常规布局：下排一个八度、上排高一个八度"));
        {
            const auto& caps = capsFor (Layout::normal);

            const auto noteOf = [&caps] (juce::juce_wchar key)
            {
                for (const auto& cap : caps)
                    if (cap.key == key)
                        return cap.semitone;

                return -1;
            };

            expectEquals (noteOf (U'Z'), 0, NI_TXT ("Z 是这一排的 C"));
            expectEquals (noteOf (U'M'), 11, NI_TXT ("M 是 B"));
            expectEquals (noteOf (U'Q'), 12, NI_TXT ("Q 高一个八度"));
            expectEquals (noteOf (U'2'), 13);
            expectEquals (noteOf (U'U'), 23, NI_TXT ("U 是上面那一排的 B"));
            expectEquals (noteOf (U'A'), -1, NI_TXT ("中排不映射"));
            expectEquals (noteOf (U'1'), -1, NI_TXT ("数字排的 1 不映射"));
            expectEquals (baseNoteFor (Layout::normal, 0), 60, NI_TXT ("默认从 C4 起"));
            expectEquals (melodyNoteFor (Layout::normal, U'Z', 0), 60);
            expectEquals (melodyNoteFor (Layout::normal, U'U', 0), 83);
            expectEquals (melodyNoteFor (Layout::normal, U'A', 0), -1);
        }

        beginTest (NI_TXT ("巴扬旋律排：q a z w s x… 一路半音往上，w = C4"));
        {
            const std::vector<std::pair<juce::juce_wchar, int>> expected
            {
                { U'Q',  0 }, { U'A',  1 }, { U'Z',  2 }, { U'W',  3 }, { U'S',  4 },
                { U'X',  5 }, { U'E',  6 }, { U'D',  7 }, { U'C',  8 }, { U'R',  9 },
                { U'F', 10 }, { U'V', 11 }, { U'T', 12 }, { U'G', 13 }, { U'B', 14 },
                { U'Y', 15 }, { U'H', 16 }, { U'N', 17 }, { U'U', 18 }, { U'J', 19 },
                { U'M', 20 }, { U'I', 21 }, { U'K', 22 }, { U',', 23 }, { U'O', 24 },
                { U'L', 25 }, { U'.', 26 }, { U'P', 27 }, { U';', 28 }, { U'/', 29 }
            };

            auto allGood = true;

            for (const auto& [key, semitone] : expected)
                if (melodyNoteFor (Layout::bayan, key, 0) != 57 + semitone)
                    allGood = false;

            expect (allGood, NI_TXT ("30 个键就是 30 个连续半音"));
            expectEquals (melodyNoteFor (Layout::bayan, U'Q', 0), 57, NI_TXT ("q = A3"));
            expectEquals (melodyNoteFor (Layout::bayan, U'W', 0), 60, NI_TXT ("w = C4"));
            expectEquals (melodyNoteFor (Layout::bayan, U'X', 0), 62, NI_TXT ("x = D4"));
            expectEquals (melodyNoteFor (Layout::bayan, U'/', 0), 86, NI_TXT ("/ = D6"));
            expectEquals (melodyNoteFor (Layout::bayan, U'1', 0), -1,
                          NI_TXT ("数字排不是旋律排"));
            expectEquals (melodyNoteFor (Layout::bayan, U'Q', 12), 69,
                          NI_TXT ("整排可以被移位"));
        }

        beginTest (NI_TXT ("巴扬低音排：上五度下四度严格交替，5 = F3、6 = C4"));
        {
            const auto note = [] (juce::juce_wchar key) { return bassNoteForKey (key, 0); };

            expectEquals (note (0x0060), 54, NI_TXT ("· = F#3"));
            expectEquals (note (U'1'), 49, NI_TXT ("1 = C#3"));
            expectEquals (note (U'2'), 56, NI_TXT ("2 = G#3"));
            expectEquals (note (U'3'), 51, NI_TXT ("3 = D#3"));
            expectEquals (note (U'4'), 58, NI_TXT ("4 = A#3"));
            expectEquals (note (U'5'), 53, NI_TXT ("5 = F3"));
            expectEquals (note (U'6'), 60, NI_TXT ("6 = C4（比 5 高一个五度）"));
            expectEquals (note (U'7'), 55, NI_TXT ("7 = G3（比 6 低一个四度）"));
            expectEquals (note (U'8'), 62, NI_TXT ("8 = D4（又是上五度）"));
            expectEquals (note (U'9'), 57, NI_TXT ("9 = A3（又是下四度）"));
            expectEquals (note (U'0'), 64);
            expectEquals (note (U'-'), 59);
            expectEquals (note (U'='), 66, NI_TXT ("= F#4"));
            expectEquals (note (0x08), 61, NI_TXT ("⌫ = C#4"));

            // Every neighbouring pair alternates: +7 (a fifth up), -5 (a fourth
            // down), +7, -5 … from one end of the row to the other.
            const std::vector<int> row { 0x0060, U'1', U'2', U'3', U'4', U'5', U'6',
                                         U'7', U'8', U'9', U'0', U'-', U'=', 0x08 };
            auto alternates = true;
            auto eachIsSevenOrFive = true;
            std::set<int> seen;
            auto previousStep = 0;

            for (size_t i = 0; i + 1 < row.size(); ++i)
            {
                const auto step = bassNoteForKey ((juce::juce_wchar) row[i + 1], 0)
                                      - bassNoteForKey ((juce::juce_wchar) row[i], 0);

                // The two directions have to take turns: +7, -5, +7, -5 …
                if (i > 0 && step == previousStep)
                    alternates = false;

                if (step != 7 && step != -5)
                    eachIsSevenOrFive = false;

                previousStep = step;
            }

            // …and the anchor the row is written around: 5 -> 6 is the fifth up,
            // 6 -> 7 the fourth down.
            expectEquals (bassNoteForKey (U'6', 0) - bassNoteForKey (U'5', 0), 7,
                          NI_TXT ("5 → 6 是上五度"));
            expectEquals (bassNoteForKey (U'7', 0) - bassNoteForKey (U'6', 0), -5,
                          NI_TXT ("6 → 7 是下四度"));

            for (int i = 0; i < kBassKeyCount; ++i)
                seen.insert (bassNoteFor (i, 0));

            expect (alternates, NI_TXT ("每一对相邻键都是 +7 / -5 交替（上五度、下四度…）"));
            expect (eachIsSevenOrFive, NI_TXT ("每一次都正好是五度或四度"));
            expectEquals ((int) seen.size(), kBassKeyCount, NI_TXT ("没有两个键是同一个音"));

            // A shift moves the whole row, so the zigzag is unchanged.
            auto shiftsStayOnThePiano = true;
            const auto range = bassShiftRange();

            for (int shift = range.lowest; shift <= range.highest; ++shift)
                for (int i = 0; i < kBassKeyCount; ++i)
                {
                    const auto value = bassNoteFor (i, shift);

                    if (value < kLowestNote || value > kHighestNote)
                        shiftsStayOnThePiano = false;
                }

            expect (shiftsStayOnThePiano,
                    NI_TXT ("整个可移范围内整排都还在钢琴上（最低 ")
                        + juce::String (range.lowest) + NI_TXT(" … 最高 ")
                        + juce::String (range.highest) + NI_TXT ("）"));

            expectEquals (bassNoteForKey (U'5', 7), 60, NI_TXT ("低音上移五度：F3 → C4"));
            expectEquals (bassNoteForKey (U'6', 7), 67, NI_TXT ("…6 也跟着上去（C4 → G4）"));
            expectEquals (bassNoteForKey (U'5', 1), 54, NI_TXT ("上移半音：F3 → F#3"));
            expectEquals (bassNoteForKey (U'5', -1), 52, NI_TXT ("下移半音：F3 → E3"));

            expectEquals (bassNoteForKey (U'Q', 0), -1, NI_TXT ("字母键不是低音键"));
            expectEquals (bassNoteFor (99, 0), -1, NI_TXT ("越界的序号没有音"));
        }

        beginTest (NI_TXT ("移位范围与越界夹取"));
        {
            const auto normal = melodyShiftRange (Layout::normal);
            expectEquals (normal.lowest, -39);
            expectEquals (normal.highest, 25);

            const auto bayan = melodyShiftRange (Layout::bayan);
            expectEquals (bayan.lowest, -36);
            expectEquals (bayan.highest, 22);

            // The top note of each layout stays on the piano at the extremes.
            expectEquals (melodyNoteFor (Layout::normal, U'U', normal.highest), kHighestNote);
            expectEquals (melodyNoteFor (Layout::normal, U'Z', normal.lowest), kLowestNote);
            expectEquals (melodyNoteFor (Layout::bayan, U'/', bayan.highest), kHighestNote);
            expectEquals (melodyNoteFor (Layout::bayan, U'Q', bayan.lowest), kLowestNote);

            const auto bass = bassShiftRange();
            // The bass row is a fixed table, so the limit is the piano: C#3–F#4 is
            // 49–66, i.e. A0 (21) down to C8 (108) leaves -28 … +42 semitones.
            expectEquals (bass.lowest, -28);
            expectEquals (bass.highest, 42);

            // The two extremes still land exactly on the ends of the piano.
            expectEquals (bassNoteFor (1, bass.lowest), kLowestNote);
            expectEquals (bassNoteFor (12, bass.highest), kHighestNote);

            // An octave of the whole row: every key moves by 12 semitones together.
            for (int i = 0; i < kBassKeyCount; ++i)
                expectEquals (bassNoteFor (i, 12) - bassNoteFor (i, 12 - 12), 12,
                              NI_TXT ("整排往上一个八度"));

            for (int i = 0; i < kBassKeyCount; ++i)
                expectEquals (bassNoteFor (i, -12) - bassNoteFor (i, 0), -12,
                              NI_TXT ("整排往下一个八度"));

            Settings settings;
            settings.layout = Layout::bayan;
            settings.melodyShift = 999;
            settings.bassShift = -999;
            settings.velocity = 0;
            clamp (settings);

            expectEquals (settings.melodyShift, bayan.highest);
            expectEquals (settings.bassShift, bass.lowest);
            expectEquals (settings.velocity, 1);
        }

        beginTest (NI_TXT ("一个琴键上可能写着两个电脑键"));
        {
            const auto at60 = keyLettersFor (Layout::bayan, 0, 0, 60);
            expect (at60.contains ("W"), NI_TXT ("C4 上写着旋律排的 w（实际「") + at60 + NI_TXT ("」）"));
            expect (at60.contains ("6"), NI_TXT ("…和低音排的 6"));

            const auto at59 = keyLettersFor (Layout::bayan, 0, 0, 59);
            expect (at59.contains ("Z") && at59.contains ("-"), NI_TXT ("B3 上是 z 与 -"));

            expectEquals (keyLettersFor (Layout::normal, 0, 0, 60), juce::String ("Z"),
                          NI_TXT ("常规布局下 C4 只有 Z"));
            expectEquals (keyLettersFor (Layout::normal, 0, 0, 59), juce::String(),
                          NI_TXT ("没映射的琴键没有字母"));
        }

        beginTest (NI_TXT ("键帽表：四排、真实错位、Backspace 是宽键"));
        {
            const auto& board = physicalBoard();
            expectEquals ((int) board.size(), 44,
                          NI_TXT ("数字排 13 + ⌫ + 三排各 10 个字母键"));

            auto rows = std::set<int>();
            auto backspaceWidth = 0.0f;
            auto hasLeftKey = false;

            for (const auto& cap : board)
            {
                rows.insert (cap.row);

                if (cap.key == 0x08)
                    backspaceWidth = cap.width;

                if (cap.key == 0x0060)
                    hasLeftKey = true;
            }

            expectEquals ((int) rows.size(), 4, NI_TXT ("四排"));
            expectEquals (backspaceWidth, 2.0f, NI_TXT ("Backspace 是两格宽"));
            expect (hasLeftKey, NI_TXT ("`·` 那个键在表里"));

            const auto& bayan = capsFor (Layout::bayan);
            auto bassCaps = 0;
            auto melodyCaps = 0;

            for (const auto& cap : bayan)
            {
                if (cap.bassIndex >= 0) ++bassCaps;
                if (cap.semitone >= 0) ++melodyCaps;
            }

            expectEquals (bassCaps, kBassKeyCount, NI_TXT ("巴扬下低音排 14 个键"));
            expectEquals (melodyCaps, 30, NI_TXT ("巴扬下旋律排 30 个键"));

            auto consistent = true;

            for (const auto& cap : bayan)
                if (noteForCap (Layout::bayan, cap, 0, 0)
                    != (cap.bassIndex >= 0 ? bassNoteFor (cap.bassIndex, 0)
                                           : (cap.semitone >= 0 ? 57 + cap.semitone : -1)))
                    consistent = false;

            expect (consistent, NI_TXT ("键帽的音与两张表一致"));
        }

        beginTest (NI_TXT ("窗口状态随 keyboard.json 往返，旧文件读成默认"));
        {
            Settings settings;
            settings.alwaysOnTop = true;
            settings.collapsed = true;
            settings.layout = Layout::bayan;
            settings.melodyShift = 5;
            settings.bassShift = 7;
            settings.velocity = 77;

            const auto back = settingsFromVar (settingsToVar (settings));
            expect (back.alwaysOnTop && back.collapsed);
            expect (back.layout == Layout::bayan);
            expectEquals (back.melodyShift, 5);
            expectEquals (back.bassShift, 7);
            expectEquals (back.velocity, 77);

            const auto missing = settingsFromVar (juce::var());
            expect (! missing.alwaysOnTop && ! missing.collapsed);
            expect (missing.layout == Layout::normal);
            expectEquals (missing.melodyShift, 0);
            expectEquals (missing.bassShift, 0);
            expectEquals (missing.velocity, 100);

            auto* object = new juce::DynamicObject();
            object->setProperty ("layout", 9);
            object->setProperty ("melodyShift", 9999);
            object->setProperty ("bassShift", 9999);
            object->setProperty ("velocity", 9999);
            const auto clamped = settingsFromVar (juce::var (object));

            expect (clamped.layout == Layout::normal, NI_TXT ("不是 1 就算常规"));
            expectEquals (clamped.melodyShift, melodyShiftRange (Layout::normal).highest);
            expectEquals (clamped.bassShift, bassShiftRange().highest);
            expectEquals (clamped.velocity, 127);
        }
    }
};

static KeyboardLayoutTests keyboardLayoutTests;

} // namespace hmdaw

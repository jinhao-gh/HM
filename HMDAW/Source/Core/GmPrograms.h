#pragma once

#include <juce_core/juce_core.h>

namespace hmdaw::gm
{

struct Program { const char* chinese; const char* english; };

inline const Program* programs (int& count)
{
    static const Program list[128] =
    {
        { "大钢琴", "Acoustic Grand Piano" },
        { "明亮钢琴", "Bright Acoustic Piano" },
        { "电大钢琴", "Electric Grand Piano" },
        { "酒吧钢琴", "Honky-tonk Piano" },
        { "电钢琴 1", "Electric Piano 1" },
        { "电钢琴 2", "Electric Piano 2" },
        { "羽管键琴", "Harpsichord" },
        { "击弦古钢琴", "Clavinet" },
        { "钢片琴", "Celesta" },
        { "钟琴", "Glockenspiel" },
        { "八音盒", "Music Box" },
        { "颤音琴", "Vibraphone" },
        { "马林巴", "Marimba" },
        { "木琴", "Xylophone" },
        { "管钟", "Tubular Bells" },
        { "洋琴", "Dulcimer" },
        { "拉杆风琴", "Drawbar Organ" },
        { "打击风琴", "Percussive Organ" },
        { "摇滚风琴", "Rock Organ" },
        { "教堂管风琴", "Church Organ" },
        { "簧风琴", "Reed Organ" },
        { "手风琴", "Accordion" },
        { "口琴", "Harmonica" },
        { "探戈手风琴", "Tango Accordion" },
        { "尼龙吉他", "Acoustic Guitar (nylon)" },
        { "钢弦吉他", "Acoustic Guitar (steel)" },
        { "爵士电吉他", "Electric Guitar (jazz)" },
        { "清音电吉他", "Electric Guitar (clean)" },
        { "闷音电吉他", "Electric Guitar (muted)" },
        { "过载吉他", "Overdriven Guitar" },
        { "失真吉他", "Distortion Guitar" },
        { "吉他泛音", "Guitar Harmonics" },
        { "原声贝斯", "Acoustic Bass" },
        { "指弹电贝斯", "Electric Bass (finger)" },
        { "拨片电贝斯", "Electric Bass (pick)" },
        { "无品贝斯", "Fretless Bass" },
        { "拍击贝斯 1", "Slap Bass 1" },
        { "拍击贝斯 2", "Slap Bass 2" },
        { "合成贝斯 1", "Synth Bass 1" },
        { "合成贝斯 2", "Synth Bass 2" },
        { "小提琴", "Violin" },
        { "中提琴", "Viola" },
        { "大提琴", "Cello" },
        { "低音提琴", "Contrabass" },
        { "颤音弦乐", "Tremolo Strings" },
        { "拨弦弦乐", "Pizzicato Strings" },
        { "竖琴", "Orchestral Harp" },
        { "定音鼓", "Timpani" },
        { "弦乐合奏 1", "String Ensemble 1" },
        { "弦乐合奏 2", "String Ensemble 2" },
        { "合成弦乐 1", "Synth Strings 1" },
        { "合成弦乐 2", "Synth Strings 2" },
        { "人声合唱", "Choir Aahs" },
        { "人声啊", "Voice Oohs" },
        { "合成人声", "Synth Voice" },
        { "管弦乐重击", "Orchestra Hit" },
        { "小号", "Trumpet" },
        { "长号", "Trombone" },
        { "大号", "Tuba" },
        { "弱音小号", "Muted Trumpet" },
        { "圆号", "French Horn" },
        { "铜管组", "Brass Section" },
        { "合成铜管 1", "Synth Brass 1" },
        { "合成铜管 2", "Synth Brass 2" },
        { "高音萨克斯", "Soprano Sax" },
        { "中音萨克斯", "Alto Sax" },
        { "次中音萨克斯", "Tenor Sax" },
        { "上低音萨克斯", "Baritone Sax" },
        { "双簧管", "Oboe" },
        { "英国管", "English Horn" },
        { "巴松", "Bassoon" },
        { "单簧管", "Clarinet" },
        { "短笛", "Piccolo" },
        { "长笛", "Flute" },
        { "竖笛", "Recorder" },
        { "排箫", "Pan Flute" },
        { "吹瓶声", "Blown Bottle" },
        { "尺八", "Shakuhachi" },
        { "哨声", "Whistle" },
        { "陶笛", "Ocarina" },
        { "方波主音", "Lead 1 (square)" },
        { "锯齿主音", "Lead 2 (sawtooth)" },
        { "汽笛主音", "Lead 3 (calliope)" },
        { "气声主音", "Lead 4 (chiff)" },
        { "合成主音 5", "Lead 5 (charang)" },
        { "人声主音", "Lead 6 (voice)" },
        { "五度主音", "Lead 7 (fifths)" },
        { "贝斯主音", "Lead 8 (bass + lead)" },
        { "新时代铺底", "Pad 1 (new age)" },
        { "温暖铺底", "Pad 2 (warm)" },
        { "复音铺底", "Pad 3 (polysynth)" },
        { "合唱铺底", "Pad 4 (choir)" },
        { "弓弦铺底", "Pad 5 (bowed)" },
        { "金属铺底", "Pad 6 (metallic)" },
        { "光环铺底", "Pad 7 (halo)" },
        { "扫频铺底", "Pad 8 (sweep)" },
        { "雨声效果", "FX 1 (rain)" },
        { "音轨效果", "FX 2 (soundtrack)" },
        { "水晶效果", "FX 3 (crystal)" },
        { "气氛效果", "FX 4 (atmosphere)" },
        { "明亮效果", "FX 5 (brightness)" },
        { "妖精效果", "FX 6 (goblins)" },
        { "回声效果", "FX 7 (echoes)" },
        { "科幻效果", "FX 8 (sci-fi)" },
        { "西塔琴", "Sitar" },
        { "班卓琴", "Banjo" },
        { "三味线", "Shamisen" },
        { "古筝", "Koto" },
        { "卡林巴", "Kalimba" },
        { "风笛", "Bag pipe" },
        { "民族小提琴", "Fiddle" },
        { "唢呐", "Shanai" },
        { "铃铛", "Tinkle Bell" },
        { "阿哥哥", "Agogo" },
        { "钢鼓", "Steel Drums" },
        { "木鱼", "Woodblock" },
        { "太鼓", "Taiko Drum" },
        { "旋律嗵鼓", "Melodic Tom" },
        { "合成鼓", "Synth Drum" },
        { "反向镲", "Reverse Cymbal" },
        { "吉他品噪", "Guitar Fret Noise" },
        { "呼吸声", "Breath Noise" },
        { "海浪", "Seashore" },
        { "鸟鸣", "Bird Tweet" },
        { "电话铃", "Telephone Ring" },
        { "直升机", "Helicopter" },
        { "掌声", "Applause" },
        { "枪声", "Gunshot" },
    };

    count = 128;
    return list;
}

inline int programCount() { return 128; }

inline const char* categoryName (int index)
{
    static const char* names[16] = {
        "钢琴", "色彩打击", "风琴", "吉他", "贝斯", "弦乐", "合奏", "铜管", "簧片", "管乐", "合成主音", "合成铺底", "合成效果", "民族", "打击", "音效"
    };
    return names[juce::jlimit (0, 15, index)];
}

inline int categoryOf (int program) { return juce::jlimit (0, 15, program / 8); }

inline juce::String displayName (int program)
{
    int count = 0;
    const auto* list = programs (count);
    const auto i = juce::jlimit (0, count - 1, program);
    return juce::String (i) + "  " + juce::String::fromUTF8 (list[i].chinese)
             + "  " + juce::String::fromUTF8 (list[i].english);
}

} // namespace hmdaw::gm

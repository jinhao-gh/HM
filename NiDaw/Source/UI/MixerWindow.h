#pragma once

#include "../Audio/AudioEngine.h"
#include "../Core/MeterMath.h"
#include "../Core/ProjectDocument.h"
#include "AppState.h"
#include "Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

namespace nidaw
{

/** 混音台: one strip per track, plus a master strip pinned on the right.

    Every strip is a **real component** (one `Strip` each, living inside a viewport
    whose viewed component is a plain holder). That matters: the strips used to be
    painted by the mixer's own content and hit-tested by hand, but the viewport sat
    on top of them and swallowed every click - so the faders and the M / S boxes
    simply did not answer the mouse. A strip that owns its own bounds, painting and
    mouse handling cannot drift out of step with what is drawn, and the scroll /
    clip / hit-test questions are JUCE's again.

    Everything a strip changes goes through the document (so it is undoable and the
    engine picks it up on the next schedule rebuild); everything a meter shows comes
    from the audio engine's "peak since the last read" counters, with the falling and
    the hold done here on the message thread (see Core/MeterMath.h).
*/
class MixerWindow : public juce::DocumentWindow
{
public:
    MixerWindow (ProjectDocument& document, AppState& appState, AudioEngine& audioEngine,
                 juce::Component* context = nullptr);
    ~MixerWindow() override;

    void resized() override;
    void closeButtonPressed() override;

    void showForProject();

    /** A strip gesture (a fader or pan drag) is one undo step: the drag opens it and
        its own changes are derived edits, then the mouse-up commits it. */
    void beginEdit (const juce::String& description);
    void endEdit();
    void setFaderDb (juce::int64 trackId, float db);
    void setPan (juce::int64 trackId, float pan);
    void toggleMute (juce::int64 trackId);
    void toggleSolo (juce::int64 trackId);
    /** The little box at the right end of an insert row: in circuit / bypassed. */
    void toggleEffectBypass (juce::int64 trackId, int slotIndex);
    /** The insert row's right-click menu: 1 up, 2 down, 3 remove. */
    void performInsertMenuCommand (juce::int64 trackId, int row, int menuId);

    /** The instrument button of a strip, the same entry a lane header's 音色 uses. */
    std::function<void (juce::int64 trackId)> onOpenInstrument;
    /** The transport buttons at the bottom of the window. */
    std::function<void()> onPlayStop;
    std::function<void()> onReturnToStart;
    /** An insert row's 名字: the plugin's own window (or the picker when missing). */
    std::function<void (juce::int64 trackId, int slotIndex)> onOpenEffect;
    /** The 「＋」 row: ask for a plugin and add it to this track's chain. */
    std::function<void (juce::int64 trackId)> onAddEffect;

    static constexpr int kStripWidth = 106;
    static constexpr int kMasterWidth = 106;
    /** Below this the strips keep their shape and the viewport scrolls instead. */
    static constexpr int kMinStripHeight = 400;
    static constexpr int kDesignWidth = 1000;
    static constexpr int kDesignHeight = 580;

    // ---- one channel strip -------------------------------------------------
    class Strip : public juce::Component
    {
    public:
        /** `trackId` 0 means the master strip. */
        Strip (MixerWindow& owner, ProjectDocument& document, AppState& appState,
               juce::int64 trackId);

        void paint (juce::Graphics& g) override;
        void mouseDown (const juce::MouseEvent& e) override;
        void mouseDrag (const juce::MouseEvent& e) override;
        void mouseUp (const juce::MouseEvent& e) override;
        void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

        bool isMaster() const noexcept { return master; }
        juce::int64 getTrackId() const noexcept { return trackId; }
        /** Which track this strip shows. 0 is the master strip. */
        void setTrackId (juce::int64 newTrackId);

        /** Reads the engine's counters for this strip and moves the needles. */
        void updateMeters (double dtSeconds);
        void resetClip();

        /** Which part of the strip a point is in (also used by the mouse). */
        enum class Part { none, name, instrument, insertRow, insertBypass, addRow, pan,
                          fader, meter, mute, solo };

        Part partAt (juce::Point<int> point, int& rowIndex) const;
        juce::Rectangle<int> boundsFor (Part part) const;
        juce::Rectangle<int> insertRowBounds (int row) const;
        /** The bypass control at the right end of an insert row. */
        juce::Rectangle<int> insertBypassBounds (int row) const;
        /** The dB labels the fader's scale carries, top (loudest) first. */
        juce::StringArray scaleLabels() const;

        float getLevelLeft() const noexcept { return levelLeft; }
        float getLevelRight() const noexcept { return levelRight; }
        bool isClipping() const noexcept { return clippingLeft || clippingRight; }

    private:
        /** Where every part of this strip sits, from its own bounds. */
        struct Layout
        {
            juce::Rectangle<int> name, instrument, inserts, addRow, pan;
            juce::Rectangle<int> faderScale, fader, meterLeft, meterRight, meterLabels, mute, solo;
        };

        Layout layout() const;
        void paintTrack (juce::Graphics& g, const Layout& area, const InstrumentTrack& track);
        void paintMaster (juce::Graphics& g, const Layout& area);
        void paintFader (juce::Graphics& g, const Layout& area, float db, juce::Colour colour);
        void paintMeter (juce::Graphics& g, const Layout& area);
        void setFaderFromY (int y);
        void setPanFromX (int x);

        MixerWindow& owner;
        ProjectDocument& document;
        AppState& appState;
        juce::int64 trackId = 0;
        bool master = false;

        /** The metre's state: two bars, each with its own hold marker and clip dot. */
        float levelLeft = 0.0f, levelRight = 0.0f;
        float holdLeft = 1.0f, holdRight = 1.0f;
        double holdSecondsLeft = 0.0, holdSecondsRight = 0.0;
        bool clippingLeft = false, clippingRight = false;

        /** While a fader is being dragged: which value the drag started from. */
        bool draggingFader = false;
        bool draggingPan = false;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Strip)
    };

    // ---- test hooks -------------------------------------------------------|
    /** Track strips (in track order) followed by the master strip. */
    int getStripCountForTest() const;
    /** The strip component itself, so a test can send it real mouse events. */
    juce::Component* getStripComponentForTest (int index) const;
    juce::int64 getStripTrackIdForTest (int index) const;
    bool isMasterStripForTest (int index) const;
    /** Where a part of a strip sits, in that strip's own coordinates. */
    juce::Rectangle<int> getPartBoundsForTest (int index, Strip::Part part) const;
    juce::Rectangle<int> getInsertRowBoundsForTest (int index, int row) const;
    juce::Rectangle<int> getInsertBypassBoundsForTest (int index, int row) const;
    juce::StringArray getFaderScaleLabelsForTest (int index) const;
    /** Where the fader's scale puts a dB value, in the strip's own coordinates. */
    int getFaderTickYForTest (int index, double db) const;
    /** Where the meter's scale puts a dB value (the bars are linear in amplitude). */
    int getMeterTickYForTest (int index, double db) const;
    /** The colour the meters paint a level in: green when quiet, red at the top. */
    static juce::Colour getMeterColourForTest (double db);
    float getFaderValueDbForTest (int index) const;
    int getInsertRowCountForTest (int index) const;
    juce::String getInsertRowTextForTest (int index, int row) const;
    /** What the strip's two bars show (x = left, y = right). */
    juce::Point<float> getMeterLevelsForTest (int index) const;
    bool isClippingForTest (int index) const;
    /** One animation step, so the self test does not have to wait for the timer. */
    void tickMetersForTest();
    void pressPlayStopForTest();
    void pressReturnToStartForTest();
    juce::String getPositionTextForTest() const;

    /** The canvas inside the window: the scrolling strips and the transport row.

        Public only because the strips and the drawing helpers in the .cpp work on
        it; nothing else is expected to build one.
    */
    class Content : public juce::Component, private juce::Timer, private juce::ChangeListener
    {
    public:
        Content (MixerWindow& ownerToUse, ProjectDocument& document, AppState& appState,
                 AudioEngine& audioEngine);
        ~Content() override;

        void paint (juce::Graphics& g) override;
        void resized() override;

        void timerCallback() override;
        void changeListenerCallback (juce::ChangeBroadcaster* source) override;

        /** The strips, rebuilt whenever the project's track list changes. */
        void rebuildRows();
        void updateMeters();
        /** Scrolls the strips, from the wheel over a strip. */
        void scrollStripsBy (float deltaX, float deltaY);

        MixerWindow& owner;
        ProjectDocument& document;
        AppState& appState;
        AudioEngine& audio;

        juce::Viewport stripViewport;
        /** Everything that scrolls: the strips, side by side. */
        juce::Component stripHolder;
        juce::TextButton playButton, stopButton, returnButton;
        juce::Label positionLabel;
        /** The pinned master strip, outside the viewport. */
        std::unique_ptr<Strip> masterStrip;
        std::vector<std::unique_ptr<Strip>> strips;

        /** What the last timer tick used, so the self test can tick by hand. */
        double lastTickSeconds = 1.0 / 30.0;
    };

    Content* content = nullptr;
    juce::Component* placementContext = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixerWindow)
};

} // namespace nidaw

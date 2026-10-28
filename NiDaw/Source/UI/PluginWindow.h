#pragma once

#include "../Audio/PluginHost.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace nidaw
{

/** The external plugin's own interface, in a window of its own.

    One reusable window: opening it for another track swaps the editor inside. A
    plugin without an interface gets JUCE's generic panel instead, so its
    parameters are still reachable.
*/
class PluginWindow : public juce::DocumentWindow
{
public:
    /** `context` is the component the window belongs to (the main window): the
        plugin opens on the same screen the user is already looking at. */
    explicit PluginWindow (PluginHostManager& hostManager, juce::Component* context = nullptr);
    ~PluginWindow() override;

    /** Shows the editor belonging to this track's plugin. When the plugin is not
        up (yet, or at all) the window says so rather than showing an empty frame. */
    void showForTrack (juce::int64 trackId, const juce::String& trackName);
    /** The same, for one slot of a track's insert chain. */
    void showForEffect (juce::int64 trackId, int slotIndex, const juce::String& effectName);
    /** Re-runs the last showForTrack, so a plugin that finished loading after the
        window opened replaces the "not loaded yet" note by itself. */
    void refresh();

    /** Throws the interface away and hides the window. Called before the project's
        plugin instances go: an editor may not outlive the plugin that owns it, and a
        project load is the one moment they are unloaded. */
    void closeForProjectChange();

    /** The plugin's own interface component, or nullptr when there is none on screen.

        The app-wide space bar steps aside while the focus is inside this: Kontakt and
        Serum audition with the space bar, so inside their own interface that key
        belongs to them.
    */
    juce::Component* getEditorComponent() const;

    void closeButtonPressed() override;

    // ---- self test hooks ----
    bool hasEditorForTest() const;
    juce::String getMessageForTest() const;
    /** How big the plugin says its interface wants to be (self test). */
    juce::Point<int> getPluginPreferredSizeForTest() const;
    /** The plugin's own editor component, so a test can resize it and watch the
        window follow (self test). */
    juce::AudioProcessorEditor* getEditorForTest() const;
    /** The size the plugin's interface really has inside the window. When the
        display is too small for it this is still the plugin's own size, and the
        viewport scrolls to reach the rest (self test). */
    juce::Point<int> getEditorViewSizeForTest() const;
    /** True when the window is scrolling the interface because it does not fit. */
    bool isInterfaceScrollableForTest() const;

private:
    class Content : public juce::Component,
                    private juce::ComponentListener
    {
    public:
        explicit Content (PluginHostManager& hostManager);

        void resized() override;
        void showFor (juce::int64 trackId, const juce::String& trackName);
        void showForEffect (juce::int64 trackId, int slotIndex, const juce::String& effectName);
        /** Destroys the interface on screen, before the plugin behind it goes. */
        void closeEditor();
        /** Forgets what was being shown, so the next show builds from scratch. */
        void forgetCurrentTarget();

        bool hasEditor() const noexcept { return editor != nullptr; }
        juce::String getMessage() const { return message.getText(); }
        juce::Point<int> getPreferredSize() const noexcept { return editorNativeSize; }
        juce::AudioProcessorEditor* getEditor() const noexcept { return editor.get(); }
        juce::Point<int> getEditorViewSize() const;
        bool isInterfaceScrollable() const;
        /** True when the plugin lets the host resize its window. */
        bool isEditorResizable() const noexcept { return editorResizable; }

        /** The plugin changed the size of its own interface (Kontakt does this when
            a library loads, and again when its zoom changes), so the window around
            it has to grow or shrink to match. */
        std::function<void (int width, int height)> onEditorSizeChanged;

    private:
        void destroyEditor();
        void layoutEditor();
        /** Puts the interface inside the viewport (only when it is bigger than the
            window) or back as a plain child of the window. */
        void setScrolling (bool shouldScroll);

        /** A plugin may resize itself while we are laying it out, and following that
            would bounce the window back and forth forever. */
        void componentMovedOrResized (juce::Component& component, bool, bool) override;

        PluginHostManager& host;
        std::unique_ptr<juce::AudioProcessorEditor> editor;
        /** The interface lives in here, so a plugin bigger than the window (or
            bigger than the screen) can still be scrolled to instead of being cut
            off: everything past the window edge stays reachable. */
        juce::Viewport viewport;
        juce::Label message;
        /** The size the plugin's interface really has right now, which is also the
            size the window asks for. It is kept up to date from the editor itself -
            by the layout pass and by the plugin's own resizes - because a stale
            snapshot here is what leaves the window smaller than the interface with
            nothing to scroll. */
        juce::Point<int> editorNativeSize { 460, 120 };
        /** True while the interface is bigger than the window, and is therefore
            scrolled instead of squeezed. */
        bool scrolling = false;
        bool editorResizable = false;
        bool editorDrivesSize = false;
        /** Which instance the window is showing: the track's instrument, or one
            slot of its insert chain. */
        bool showsEffect = false;
        juce::int64 currentTrackId = 0;
        int currentSlotIndex = 0;
        /** The plugin instance the interface on screen belongs to. The host reports
            every plugin that finishes loading, so without this the window would pull
            a working interface apart and build it again whenever an unrelated
            plugin was ready. */
        juce::AudioPluginInstance* shownInstance = nullptr;

        /** Brings the editor for the current target up (or explains why there is
            none). */
        void showCurrent (const juce::String& label);
        /** The instance to show, or nullptr; `reason` says why when it is null. */
        juce::AudioPluginInstance* instanceForCurrentTarget (juce::String& reason) const;
    };

    Content* content = nullptr;
    /** The component whose screen the window should open on (the main window). */
    juce::Component* placementContext = nullptr;
    juce::int64 currentTrackId = 0;
    juce::String currentTrackName;
    /** True when the window is showing an insert slot rather than the track's
        instrument. */
    bool currentIsEffect = false;
    int currentSlotIndex = 0;

    /** Puts the window on screen at the size the plugin asked for, shrunk to
        whatever the display can actually give. */
    void placeOnScreen (int width, int height);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginWindow)
};

} // namespace nidaw

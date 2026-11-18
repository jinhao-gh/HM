#include "PluginWindow.h"
#include "Theme.h"

#include "../Core/WindowPlacement.h"

namespace hmdaw
{

PluginWindow::Content::Content (PluginHostManager& hostManager)
    : host (hostManager)
{
    addChildComponent (message);
    message.setFont (theme::font (12.5f));
    message.setColour (juce::Label::textColourId, theme::dimText);
    message.setJustificationType (juce::Justification::centred);

    addChildComponent (viewport);
    viewport.setScrollBarsShown (true, true);
}

void PluginWindow::Content::destroyEditor()
{
    if (editor == nullptr)
        return;

    editor->removeComponentListener (this);

    // The plugin has to be told before its editor goes away - JUCE asserts on it,
    // and a plugin that keeps its own pointer would otherwise be left dangling.
    if (auto* processor = editor->getAudioProcessor())
        processor->editorBeingDeleted (editor.get());

    // Taken out of the viewport first: the viewport does not own it, but it would
    // hold the pointer.
    setScrolling (false);
    editor.reset();
    editorNativeSize = { 0, 0 };
}

void PluginWindow::Content::setScrolling (bool shouldScroll)
{
    // Only an interface that is really there can be scrolled.
    const auto wantsScrolling = shouldScroll && editor != nullptr;

    if (wantsScrolling == scrolling)
        return;

    scrolling = wantsScrolling;

    if (scrolling)
    {
        viewport.setViewedComponent (editor.get(), false);
    }
    else
    {
        viewport.setViewedComponent (nullptr, false);

        if (editor != nullptr)
            addAndMakeVisible (editor.get());
    }

    viewport.setVisible (scrolling);
}

void PluginWindow::Content::closeEditor()
{
    destroyEditor();
    message.setVisible (false);
    viewport.setVisible (false);
}

void PluginWindow::Content::forgetCurrentTarget()
{
    shownInstance = nullptr;
    showsEffect = false;
    currentTrackId = 0;
    currentSlotIndex = 0;
}

void PluginWindow::Content::layoutEditor()
{
    if (editor == nullptr)
    {
        setScrolling (false);
        message.setBounds (getLocalBounds().reduced (12));
        return;
    }

    // The interface may be bigger than the window can ever be (the screen is smaller
    // than the plugin): then it is scrolled, and it keeps **its own** size - HM never
    // invents a size for a plugin. Handing a plugin a size it did not ask for is what
    // made resizable interfaces (Kontakt's zoom steps, Serum's constrainer) fight the
    // window: the plugin would snap back, the window would follow, and the two would
    // keep resizing each other while the user was trying to click something.
    message.setBounds (getLocalBounds().reduced (12));

    if (editorNativeSize.x > getWidth() || editorNativeSize.y > getHeight())
    {
        setScrolling (true);
        viewport.setBounds (getLocalBounds());
        editor->setBounds (0, 0, editorNativeSize.x, editorNativeSize.y);
        return;
    }

    // The normal case, and the one every plugin is written for: the window is the
    // interface's size, so it is a plain child of the window and its own constrainer
    // decides what it accepts (a fixed interface keeps its size, a resizable one
    // fills the window the user made bigger).
    setScrolling (false);
    editor->setBoundsConstrained (getLocalBounds());

    // Read back what the interface really is now: a constrainer, or the plugin
    // itself, may have refused the size we offered, and the window and the
    // scroll decision both have to work from the truth rather than from what we
    // hoped for.
    editorNativeSize = { juce::jmax (1, editor->getWidth()),
                         juce::jmax (1, editor->getHeight()) };
}

juce::Point<int> PluginWindow::Content::getEditorViewSize() const
{
    return editor != nullptr ? juce::Point<int> (editor->getWidth(), editor->getHeight())
                             : juce::Point<int>();
}

bool PluginWindow::Content::isInterfaceScrollable() const
{
    return editor != nullptr && scrolling;
}

void PluginWindow::Content::componentMovedOrResized (juce::Component& component, bool, bool)
{
    if (&component != editor.get())
        return;

    const juce::Point<int> newSize (juce::jmax (1, component.getWidth()),
                                    juce::jmax (1, component.getHeight()));

    // Always record what the interface is now, even when it is us doing the layout:
    // a stale record is what makes the window and the scroll decision disagree with
    // the interface that is really on screen.
    const auto changed = newSize != editorNativeSize;
    editorNativeSize = newSize;

    // Laying the interface out is not the plugin changing its mind, and while the
    // interface is scrolled the window is already as big as the screen allows:
    // chasing the plugin there would only make the two fight.
    if (! changed || editorDrivesSize || scrolling)
        return;

    // The plugin changed its own size (Kontakt does that when a library loads, when
    // its zoom changes, and whenever it refuses a size the window offered it). The
    // window is resized around it; the guard stops that resize from echoing back.
    const juce::ScopedValueSetter<bool> guard (editorDrivesSize, true);

    if (onEditorSizeChanged)
        onEditorSizeChanged (component.getWidth(), component.getHeight());
}

juce::AudioPluginInstance* PluginWindow::Content::instanceForCurrentTarget (juce::String& reason) const
{
    if (showsEffect)
    {
        if (auto plugin = host.effectPluginFor (currentTrackId, currentSlotIndex))
            return &plugin->getInstance();

        reason = host.getEffectError (currentTrackId, currentSlotIndex);
        return nullptr;
    }

    if (auto plugin = host.pluginFor (currentTrackId))
        return &plugin->getInstance();

    reason = host.getLoadError (currentTrackId);
    return nullptr;
}

void PluginWindow::Content::showCurrent (const juce::String&)
{
    juce::String reason;
    auto* instance = instanceForCurrentTarget (reason);

    // Nothing about the plugin this window is showing has changed: the host calls
    // this same code for *every* plugin that finishes loading, and rebuilding a
    // working interface tears it down and builds it again - Kontakt loses its zoom
    // and scroll position, and a plugin that paints before its own values have
    // arrived shows all of its controls at once, knobs sitting at zero included.
    // The interface is only replaced when the plugin behind it really is another
    // one (or the plugin went away and the message has to come back).
    if (instance != nullptr && instance == shownInstance && editor != nullptr)
    {
        layoutEditor();
        return;
    }

    destroyEditor();
    shownInstance = instance;
    message.setVisible (false);

    if (instance == nullptr)
    {
        message.setText (reason.isNotEmpty() ? reason
                                             : NI_TXT ("插件还没有加载完成，稍后再试。"),
                         juce::dontSendNotification);
        message.setVisible (true);
        viewport.setVisible (false);

        editorNativeSize = { 460, 120 };
        editorResizable = false;
        setSize (editorNativeSize.x, editorNativeSize.y);
        message.setBounds (getLocalBounds().reduced (12));
        return;
    }

    // A plugin is allowed to have no interface at all; the generic panel is then
    // the only way to reach its parameters.
    auto* created = instance->hasEditor() ? instance->createEditorAndMakeActive() : nullptr;

    if (created == nullptr)
        created = new juce::GenericAudioProcessorEditor (*instance);

    editor.reset (created);
    // A fresh interface starts as a plain child of the window, the way every plugin
    // expects to be hosted. layoutEditor() moves it into the viewport only if it
    // turns out to be bigger than the window (a screen smaller than the plugin).
    addAndMakeVisible (editor.get());
    viewport.setViewedComponent (nullptr, false);
    viewport.setVisible (false);
    scrolling = false;

    // What the plugin says about itself: how big it is and whether the host may
    // resize it. Kontakt's interface is large and resizable; forcing it into a
    // 640x420 window is what left most of it outside the window.
    editorResizable = editor->isResizable();

    auto width  = editor->getWidth();
    auto height = editor->getHeight();

    if (auto* pluginConstrainer = editor->getConstrainer())
    {
        // Some plugins only report their real size once they are given room, so the
        // minimum they accept is the safer starting point.
        width  = juce::jmax (width, pluginConstrainer->getMinimumWidth());
        height = juce::jmax (height, pluginConstrainer->getMinimumHeight());
    }

    editorNativeSize = { juce::jmax (200, width), juce::jmax (120, height) };

    // The window is the plugin's size; the plugin resizes itself from here on and
    // the window follows (see onEditorSizeChanged), which is how a host is meant to
    // treat a plugin it does not own the layout of.
    setSize (editorNativeSize.x, editorNativeSize.y);

    // A plugin's own keyboard is often driven by the computer keyboard, and that
    // needs the interface to be able to hold the focus: the host grabs it when the
    // interface opens, exactly as a plugin windowed by any other host does.
    editor->setWantsKeyboardFocus (true);
    editor->grabKeyboardFocus();

    editor->addComponentListener (this);
    layoutEditor();
}

void PluginWindow::Content::showFor (juce::int64 trackId, const juce::String&)
{
    showsEffect = false;
    currentTrackId = trackId;
    currentSlotIndex = 0;

    showCurrent ({});
}

void PluginWindow::Content::showForEffect (juce::int64 trackId, int slotIndex, const juce::String&)
{
    showsEffect = true;
    currentTrackId = trackId;
    currentSlotIndex = slotIndex;

    showCurrent ({});
}

void PluginWindow::Content::resized()
{
    layoutEditor();
}

PluginWindow::PluginWindow (PluginHostManager& hostManager, juce::Component* context)
    : juce::DocumentWindow (NI_TXT ("插件"), theme::panelBackground,
                            juce::DocumentWindow::closeButton)
{
    placementContext = context;

    setUsingNativeTitleBar (true);
    setResizable (true, false);

    content = new Content (hostManager);
    setContentOwned (content, false);

    content->onEditorSizeChanged = [this] (int width, int height)
    {
        // The plugin resized its own interface: the window is resized around it
        // instead of clipping whatever grew past the window's edge - but only as
        // far as the screen allows. A plugin bigger than the display keeps its size
        // inside the window and the interface scrolls.
        setSize (juce::jmax (200, width), juce::jmax (120, height));
        placeOnScreen (getWidth(), getHeight());
    };

    setSize (640, 420);
}

PluginWindow::~PluginWindow()
{
    // The editor has to be gone before the window's children are destroyed.
    content = nullptr;
}

void PluginWindow::showForTrack (juce::int64 trackId, const juce::String& trackName)
{
    currentTrackId = trackId;
    currentTrackName = trackName;
    currentIsEffect = false;
    currentSlotIndex = 0;

    content->showFor (trackId, trackName);

    setName (NI_TXT ("插件 · ") + trackName);

    // Whether the window may be resized at all is the plugin's decision, not ours.
    setResizable (content->isEditorResizable(), false);

    const auto width  = juce::jmax (200, content->getPreferredSize().x);
    const auto height = juce::jmax (120, content->getPreferredSize().y);

    placeOnScreen (width, height);

    setVisible (true);
    toFront (true);
}

void PluginWindow::showForEffect (juce::int64 trackId, int slotIndex, const juce::String& effectName)
{
    currentTrackId = trackId;
    currentTrackName = effectName;
    currentIsEffect = true;
    currentSlotIndex = slotIndex;

    content->showForEffect (trackId, slotIndex, effectName);

    setName (NI_TXT ("效果器 · ") + effectName);

    setResizable (content->isEditorResizable(), false);

    const auto width  = juce::jmax (200, content->getPreferredSize().x);
    const auto height = juce::jmax (120, content->getPreferredSize().y);

    placeOnScreen (width, height);

    setVisible (true);
    toFront (true);
}

void PluginWindow::placeOnScreen (int width, int height)
{
    const auto area = placement::displayAreaFor (placementContext);

    if (! isVisible())
    {
        // Where it opens: the plugin's size, or the whole work area when the plugin
        // is bigger than the screen (the interface then scrolls inside).
        setBounds (placement::placeWindow ({ width, height }, area));
        return;
    }

    // Already open: keep where the user put it, but still never let the window
    // grow past the screen - the plugin resizing itself must not push its own
    // bottom-right corner out of reach either.
    const auto size = placement::placeWindow ({ width, height }, area);
    const auto room = placement::usableArea (area);

    setBounds (juce::jlimit (room.getX(),
                             juce::jmax (room.getX(), room.getRight() - size.getWidth()), getX()),
               juce::jlimit (room.getY(),
                             juce::jmax (room.getY(), room.getBottom() - size.getHeight()), getY()),
               size.getWidth(), size.getHeight());
}

void PluginWindow::refresh()
{
    if (currentTrackId == 0)
        return;

    // Swaps in the editor that just became available, without taking the focus
    // away from whatever the user is doing.
    if (currentIsEffect)
        content->showForEffect (currentTrackId, currentSlotIndex, currentTrackName);
    else
        content->showFor (currentTrackId, currentTrackName);

    setResizable (content->isEditorResizable(), false);

    // The window is the interface's size, both ways. Following the plugin upwards
    // only (the old jmax against the current window) meant a plugin could never make
    // itself smaller - Kontakt stepping its zoom down would be stretched straight
    // back to the old size, and the two would keep fighting.
    placeOnScreen (juce::jmax (200, content->getPreferredSize().x),
                   juce::jmax (120, content->getPreferredSize().y));
}

void PluginWindow::closeButtonPressed()
{
    // Hidden rather than destroyed: the window is reused, and the plugin keeps its
    // editor state.
    setVisible (false);
}

void PluginWindow::closeForProjectChange()
{
    if (content != nullptr)
    {
        // The editor belongs to a plugin instance that is about to be unloaded.
        content->closeEditor();
        content->forgetCurrentTarget();
    }

    currentTrackId = 0;
    currentTrackName.clear();
    currentIsEffect = false;
    currentSlotIndex = 0;
    setVisible (false);
}

bool PluginWindow::hasEditorForTest() const
{
    return content != nullptr && content->hasEditor();
}

juce::String PluginWindow::getMessageForTest() const
{
    return content != nullptr ? content->getMessage() : juce::String();
}

juce::Point<int> PluginWindow::getPluginPreferredSizeForTest() const
{
    return content != nullptr ? content->getPreferredSize() : juce::Point<int>();
}

juce::AudioProcessorEditor* PluginWindow::getEditorForTest() const
{
    return content != nullptr ? content->getEditor() : nullptr;
}

juce::Component* PluginWindow::getEditorComponent() const
{
    return content != nullptr ? (juce::Component*) content->getEditor() : nullptr;
}

juce::Point<int> PluginWindow::getEditorViewSizeForTest() const
{
    return content != nullptr ? content->getEditorViewSize() : juce::Point<int>();
}

bool PluginWindow::isInterfaceScrollableForTest() const
{
    return content != nullptr && content->isInterfaceScrollable();
}

} // namespace hmdaw

#pragma once

#include "MusicMath.h"
#include "Types.h"

#include <juce_events/juce_events.h>

#include <functional>

namespace hmdaw
{

/** The project document: state, undo stack and persistence.

    Undo is snapshot based. Every user visible edit funnels through applyEdit(),
    which pushes a copy of the previous state before mutating. Sliders that fire
    continuously call beginEdit() once (on drag start) and then notifyEdit().
*/
class ProjectDocument : public juce::ChangeBroadcaster
{
public:
    ProjectDocument();
    ~ProjectDocument() override;

    const Project& getProject() const noexcept { return project; }

    void applyEdit (const juce::String& name, const std::function<void (Project&)>& mutation);
    void beginEdit (const juce::String& name);
    void notifyEdit();

    /** Mutates derived state without touching the undo stack.

        Used for the 2nd/4th reference line entries, which follow the note
        selection: that is not an edit the user made, so it must not push an undo
        step or mark the project dirty (unlocked entries are never saved anyway).
    */
    void applyDerivedEdit (const std::function<void (Project&)>& mutation);

    void undo();
    void redo();
    bool canUndo() const noexcept { return ! undoStack.empty(); }
    bool canRedo() const noexcept { return ! redoStack.empty(); }
    juce::String getUndoDescription() const;
    juce::String getRedoDescription() const;

    void newProject();
    bool loadFromFile (const juce::File& file, juce::String& errorMessage);
    bool saveToFile (const juce::File& file, juce::String& errorMessage);
    /** Saves into <folder>/project.nprj and copies every referenced audio file
        into <folder>/samples. The chosen path may be the .nprj itself. */
    bool saveToProjectFolder (const juce::File& file, juce::String& errorMessage);

    juce::File getCurrentFile() const { return currentFile; }
    void setCurrentFile (const juce::File& f) { currentFile = f; sendChangeMessage(); }
    juce::String getProjectName() const;

    bool isModified() const noexcept { return modified; }
    void setModified (bool shouldBeModified);

    music::TempoMap makeTempoMap() const { return music::TempoMap (project.tempoMarkers); }

    Note* findNote (juce::int64 trackId, juce::int64 noteId);
    InstrumentTrack* findTrack (juce::int64 trackId) { return project.findTrack (trackId); }

    /** End of the last note, in seconds (0 when the project is empty). */
    double projectEndSeconds() const;
    juce::int64 projectEndTick() const;

    /** View/editing state is persisted but deliberately not undoable. */
    EditorViewState& viewState() noexcept { return project.view; }
    const EditorViewState& viewState() const noexcept { return project.view; }

private:
    struct Snapshot
    {
        juce::String name;
        Project project;
    };

    void pushSnapshot (const juce::String& name);
    void restore (Project&& state, const juce::String& name);

    Project project;
    std::vector<Snapshot> undoStack, redoStack;
    juce::File currentFile;
    bool modified = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ProjectDocument)
};

juce::String projectToJson (const Project& project);
bool projectFromJson (const juce::String& json, Project& destination, juce::String& errorMessage);

} // namespace hmdaw

#include "UI/AppLookAndFeel.h"
#include "UI/AutoScroll.h"
#include "UI/MainComponent.h"
#include "UI/Theme.h"

#include "Audio/Exporter.h"
#include "Audio/HybridFxChain.h"
#include "Audio/PluginHost.h"
#include "Core/ControllerMath.h"
#include "Core/FmMath.h"
#include "Core/WindowPlacement.h"
#include "Core/HybridPresets.h"
#include "Core/WaveformPresets.h"
#include "UI/PluginPicker.h"
#include "UI/PluginWindow.h"

#include "../../TestPlugin/TestInstrumentReport.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include <iostream>
#include <memory>
#include <vector>

#if JUCE_WINDOWS
 #include <windows.h>
#endif


namespace nidaw
{

namespace selftest
{
    /** The MIDI input checks' reference group: C4, plus the ratios of a C major
        scale above it. Ratios rather than twelve-tone frequencies, because that is
        exactly how a 参考组 is built in this app (and the group's dots are what get
        dragged onto the white keys). */
    inline constexpr double kMidiC4Hz = 261.6255653;
    inline const double kMidiCMajorRatios[7] = { 1.0, 9.0 / 8.0, 5.0 / 4.0, 4.0 / 3.0,
                                                 3.0 / 2.0, 5.0 / 3.0, 15.0 / 8.0 };

    /** Absolute addresses from a crash are useless without the module base, and
        the build has no debugger attached: log both so the linker map can be used
        to resolve the faulting function. */
    inline char g_crashLogPath[1024] = {};

    inline LONG WINAPI crashHandler (EXCEPTION_POINTERS* info)
    {
        if (info != nullptr && info->ExceptionRecord != nullptr && g_crashLogPath[0] != 0)
        {
            // Only faults are worth recording. 0x40010006 / 0x4001000A are the
            // CRT's debug-print exceptions and 0xE06D7363 is a C++ exception being
            // thrown (and caught) inside JUCE; logging those makes every healthy
            // run leave a scary crash.log behind.
            const auto code = (unsigned long) info->ExceptionRecord->ExceptionCode;

            if (code == 0x40010006ul || code == 0x4001000Aul || code == 0xE06D7363ul)
                return EXCEPTION_CONTINUE_SEARCH;

            const auto base = (unsigned long long) (juce::pointer_sized_uint) ::GetModuleHandleW (nullptr);
            const auto address = (unsigned long long) (juce::pointer_sized_uint)
                                     info->ExceptionRecord->ExceptionAddress;

            char buffer[512];

            // Which module the fault is in matters more than the offset when the
            // crash is inside a plugin: say so, instead of leaving only an address
            // that resolves to nothing in the map file.
            char moduleName[260] = {};
            HMODULE faultingModule = nullptr;

            if (::GetModuleHandleExA (GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                                          | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                      (LPCSTR) info->ExceptionRecord->ExceptionAddress,
                                      &faultingModule))
                ::GetModuleFileNameA (faultingModule, moduleName, (DWORD) sizeof (moduleName) - 1);

            std::snprintf (buffer, sizeof (buffer),
                           "code=0x%08lX address=0x%llX base=0x%llX offset=0x%llX module=%s\n",
                           (unsigned long) info->ExceptionRecord->ExceptionCode,
                           address, base, base != 0 ? address - base : 0, moduleName);

            const auto handle = ::CreateFileA (g_crashLogPath, FILE_APPEND_DATA, FILE_SHARE_READ,
                                               nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

            if (handle != INVALID_HANDLE_VALUE)
            {
                DWORD written = 0;
                ::WriteFile (handle, buffer, (DWORD) std::strlen (buffer), &written, nullptr);
                ::CloseHandle (handle);
            }
        }

        return EXCEPTION_CONTINUE_SEARCH;
    }

    inline void enableCrashLogging (const juce::File& logFile)
    {
        logFile.replaceWithText ({});
        logFile.getFullPathName().copyToUTF8 (g_crashLogPath, (int) sizeof (g_crashLogPath));
        ::AddVectoredExceptionHandler (1, crashHandler);
    }

    inline juce::Image snapshot (juce::Component& component)
    {
        return component.createComponentSnapshot (component.getLocalBounds(), true, 1.0f);
    }

    inline bool writeImage (const juce::Image& image, const juce::File& file)
    {
        if (! image.isValid())
            return false;

        file.getParentDirectory().createDirectory();
        file.deleteFile();

        std::unique_ptr<juce::FileOutputStream> stream (file.createOutputStream());

        if (stream == nullptr)
            return false;

        juce::PNGImageFormat png;
        return png.writeImageToStream (image, *stream);
    }

    /** Counts pixels close to a given colour, used to prove that a drawing pass
        actually produced the expected elements. */
    inline int countColour (const juce::Image& image, juce::Colour target, int tolerance = 12)
    {
        if (! image.isValid())
            return 0;

        int count = 0;

        for (int y = 0; y < image.getHeight(); ++y)
        {
            for (int x = 0; x < image.getWidth(); ++x)
            {
                const auto pixel = image.getPixelAt (x, y);

                if (std::abs ((int) pixel.getRed()   - (int) target.getRed())   <= tolerance
                    && std::abs ((int) pixel.getGreen() - (int) target.getGreen()) <= tolerance
                    && std::abs ((int) pixel.getBlue()  - (int) target.getBlue())  <= tolerance)
                    ++count;
            }
        }

        return count;
    }

    /** Puts both panes' timelines in the same place.

        That is where a project opens, and it is what the synthetic-mouse checks
        assume: the lanes and the editor have a view each, so a check that only sets
        the editor's would leave the arrangement somewhere else.
    */
    inline void setBothTimelines (ProjectDocument& document, double pixelsPerQuarter,
                                  double offsetTicks)
    {
        auto& view = document.viewState();

        view.pixelsPerQuarter = pixelsPerQuarter;
        view.horizontalOffsetTicks = offsetTicks;
        view.arrangementPixelsPerQuarter = pixelsPerQuarter;
        view.arrangementHorizontalOffsetTicks = offsetTicks;
    }

    /** Counts the pixels inside `area` that are *not* the given colour, so a panel
        that was never filled in can be told apart from one that was painted. */
    inline int countNotColour (const juce::Image& image, juce::Rectangle<int> area,
                               juce::Colour target, int tolerance = 12)
    {
        if (! image.isValid())
            return 0;

        const auto clipped = area.getIntersection (image.getBounds());
        auto count = 0;

        for (int y = clipped.getY(); y < clipped.getBottom(); ++y)
        {
            for (int x = clipped.getX(); x < clipped.getRight(); ++x)
            {
                const auto pixel = image.getPixelAt (x, y);

                if (std::abs ((int) pixel.getRed()   - (int) target.getRed())   > tolerance
                    || std::abs ((int) pixel.getGreen() - (int) target.getGreen()) > tolerance
                    || std::abs ((int) pixel.getBlue()  - (int) target.getBlue())  > tolerance)
                    ++count;
            }
        }

        return count;
    }

    int run (const juce::File& outputDirectory, const juce::File& testPlugin,
             const juce::File& testEffect)
    {
        outputDirectory.createDirectory();

        const auto trace = [&outputDirectory] (const juce::String& message)
        {
            outputDirectory.getChildFile ("trace.log").appendText (message + "\n");
        };

        trace ("run() entered");

        enableCrashLogging (outputDirectory.getChildFile ("crash.log"));

        // The preset library goes to a throwaway file: a self test run must never
        // touch the real library in %APPDATA%.
        const auto presetFile = outputDirectory.getChildFile ("selftest-presets.json");
        presetFile.deleteFile();
        PresetStore::setDefaultFileForTesting (presetFile);

        // Same idea for the VST3 scan cache: a self test never touches the real
        // plugin list in %APPDATA%.
        const auto pluginListFile = outputDirectory.getChildFile ("selftest-pluginlist.xml");
        pluginListFile.deleteFile();
        PluginHostManager::setDefaultListFileForTesting (pluginListFile);

        // And for the plugin event log: same reason, same place.
        const auto pluginEventsFile = outputDirectory.getChildFile ("selftest-plugin-events.log");
        pluginEventsFile.deleteFile();
        PluginHostManager::setEventsLogFileForTesting (pluginEventsFile);

        // Same for the export narration: every step of Ctrl+E lands in the run's own
        // folder, so what the export did can be read back after the fact.
        const auto exportLogFile = outputDirectory.getChildFile ("selftest-export.log");
        exportLogFile.deleteFile();
        MainComponent::setExportLogFileForTesting (exportLogFile);

        // And for the MIDI input device choice: a self test must never open (or
        // remember) real hardware.
        const auto midiSettingsFile = outputDirectory.getChildFile ("selftest-midi.json");
        midiSettingsFile.deleteFile();
        MidiInputManager::setSettingsFileForTesting (midiSettingsFile);

        // Same for the virtual keyboard's own state (collapsed / pinned / layout /
        // shifts / velocity): the run must not read or write the user's real file.
        const auto keyboardSettingsFile = outputDirectory.getChildFile ("selftest-keyboard.json");
        keyboardSettingsFile.deleteFile();
        VirtualKeyboardWindow::setSettingsFileForTesting (keyboardSettingsFile);

        // Anything the plugin host does that is worth following goes to the same
        // trace file the rest of the run uses.
        PluginHostManager::traceHook = [&trace] (const juce::String& message)
        {
            trace (message);
        };

    // No audio device: this run renders every block itself (see MainComponent's ctor).
    MainComponent component (false, false);
        trace ("MainComponent constructed");

        trace ("output folder: " + outputDirectory.getFullPathName());
        trace ("test plugin: "
                   + (testPlugin.exists() ? testPlugin.getFullPathName()
                                          : juce::String ("<not given or not found>")));
        trace ("test effect: "
                   + (testEffect.exists() ? testEffect.getFullPathName()
                                          : juce::String ("<not given or not found>")));

        component.setSize (1460, 950);
        trace ("resized");

        int failures = 0;
        juce::StringArray log;

        const auto logFile = outputDirectory.getChildFile ("selftest.log");

        auto flushLog = [&log, &logFile]
        {
            logFile.replaceWithText (log.joinIntoString ("\n"));
        };

        auto check = [&failures, &log, &flushLog] (bool condition, const juce::String& what)
        {
            std::cout << (condition ? "  ok   " : "  FAIL ") << what << std::endl;
            log.add ((condition ? juce::String ("ok    ") : juce::String ("FAIL  ")) + what);
            flushLog();

            if (! condition)
                ++failures;
        };

        auto checkColourCount = [&check, &log] (const juce::Image& image, juce::Colour colour,
                                                int minimum, const juce::String& what,
                                                int tolerance = 18)
        {
            const auto counted = countColour (image, colour, tolerance);
            log.add ("      " + what + " pixels = " + juce::String (counted));
            check (counted >= minimum, what);
        };

        auto beginTest = [&log, &flushLog] (const juce::String& name)
        {
            std::cout << "-- " << name << std::endl;
            log.add ({});
            log.add ("-- " + name);
            flushLog();
        };

        auto checkNear = [&check] (double actual, double expected, double tolerance,
                                   const juce::String& what)
        {
            check (std::abs (actual - expected) <= tolerance, what);
        };

        /** Counts matching pixels inside one rectangle, for "is it drawn there?" checks. */
        auto countColourInRect = [] (const juce::Image& image, juce::Colour colour,
                                     juce::Rectangle<int> rect, int tolerance)
        {
            int count = 0;

            for (int y = rect.getY(); y < rect.getBottom(); ++y)
                for (int x = rect.getX(); x < rect.getRight(); ++x)
                {
                    if (x < 0 || y < 0 || x >= image.getWidth() || y >= image.getHeight())
                        continue;

                    const auto pixel = image.getPixelAt (x, y);

                    if (std::abs (pixel.getRed()   - colour.getRed())   <= tolerance
                        && std::abs (pixel.getGreen() - colour.getGreen()) <= tolerance
                        && std::abs (pixel.getBlue()  - colour.getBlue())  <= tolerance)
                        ++count;
                }

            return count;
        };

        // ChangeBroadcaster delivers asynchronously, so pump the queue between steps.
        auto pump = [] { juce::MessageManager::getInstance()->runDispatchLoopUntil (60); };

        // The long-press timer needs the message loop to actually run for a while.
        auto pumpFor = [] (int milliseconds)
        {
            juce::MessageManager::getInstance()->runDispatchLoopUntil (milliseconds);
        };

        trace ("about to pump");
        pump();
        trace ("pumped");

        const auto emptyImage = snapshot (component);
        trace ("first snapshot taken");
        check (writeImage (emptyImage, outputDirectory.getChildFile ("01-empty.png")),
               "empty project snapshot");

        auto& document = component.getDocument();
        auto& appState = component.getAppState();

        /** The channel-spread scenes (see further down, next to the other plugin
            checks). They are *defined* there but *run* at the very end of the run:
            they drive the transport, put their own VST track in the editor and clear
            the project, and the snapshots taken in between would otherwise be taken
            with the state they leave behind. */
        std::function<void()> runChannelScenes;

        check (document.getProject().tracks.empty(),
               "a new project starts with no tracks (got "
                   + juce::String ((int) document.getProject().tracks.size()) + ")");

        // Create the first track through the same entry point the + menu uses.
        component.getTrackArea().addTrack (TrackKind::synth);
        pump();

        check (document.getProject().tracks.size() == 1
                   && document.getProject().tracks.front().kind == TrackKind::synth
                   && document.getProject().tracks.front().clips.size() == 1,
               "the + menu added one synth track with an empty clip");

        // A new synth track starts on a factory preset; the note/workflow checks
        // below measure the rendered frequency, which needs a plain sine carrier.
        document.applyEdit ("selftest sine patch", [] (Project& project)
        {
            if (! project.tracks.empty())
                project.tracks.front().synth.patch = FmPatch();
        });
        pump();

        const auto trackId = document.getProject().tracks.front().id;

    const auto firstClipId = [&document, trackId]
    {
        const auto* track = document.findTrack (trackId);
        return track != nullptr && ! track->clips.empty() ? track->clips.front().id : (juce::int64) 0;
    };


        // Write a small harmonic series, the core authoring workflow.
        document.applyEdit ("selftest notes", [trackId] (Project& project)
        {
            if (auto* track = project.findTrack (trackId))
            {
                const double frequencies[] = { 220.0, 330.0, 440.0, 660.0, 880.0 };

                for (int i = 0; i < 5; ++i)
                {
                    Note note;
                    note.id = project.nextNoteId++;
                    note.startTick = (juce::int64) i * kTicksPerQuarter;
                    note.durationTicks = kTicksPerQuarter;
                    note.frequencyHz = frequencies[i];
                    note.velocity = 100;
                    track->clips.front().notes.push_back (note);
                }
            }

            // A sealed hand-typed frequency: it has to survive the save below.
            auto line = music::makeFixedHzEntry (project.nextReferenceLineId++, 440.0, juce::String());
            line.locked = true;
            project.referenceLines.push_back (std::move (line));
        });

        pump();

        check (document.getProject().tracks.front().clips.front().notes.size() == 5
                   && document.getProject().tracks.front().clips.front().notes.back().endTick()
                          == 5 * kTicksPerQuarter,
               "five notes on the timeline");

        std::vector<juce::int64> noteIds;

        if (auto* track = document.findTrack (trackId))
            for (const auto& note : track->clips.front().notes)
                noteIds.push_back (note.id);

        check (noteIds.size() == 5, "notes were created");

        // Open the editor and select the 440 Hz note: reference lines, the note
        // edit strip and the options column must all appear.
        appState.setEditorOpen (true);
        appState.selectTrack (trackId); appState.selectClip (firstClipId());

        if (noteIds.size() >= 3)
            appState.selectNote (noteIds[2], false);

        pump();

        check (appState.hasNoteSelection(), "a note is selected");

        const auto selectedImage = snapshot (component);
        check (writeImage (selectedImage, outputDirectory.getChildFile ("02-selected-note.png")),
               "snapshot with a selected note");

        // Prove the editor actually drew its pieces rather than just laying out.
        checkColourCount (selectedImage, theme::gridBackground, 20000, "grid background is drawn");
        // The sealed 440 Hz status entry is drawn in the "manual" role colour. Its
        // octave family also swallows the selected 440 Hz note's own family (first
        // entry wins when two of them want the same frequency), which is why the
        // plain colour is checked further down with a non-colliding note.
        checkColourCount (selectedImage, theme::referenceLineManual, 500,
                          "the hand-typed reference line family is drawn");
        checkColourCount (selectedImage, theme::trackColour (0), 200, "notes are drawn");
        checkColourCount (selectedImage, theme::accent, 50, "note edit strip is drawn");

        // Switch to the 1:3 ratio: the note's own octave family must dim and the
        // two target families must appear. The ratio is a lens on the selection,
        // not note data.
        component.getAppState().selectNote (noteIds.size() >= 1 ? noteIds[0] : 0, false);
        pump();

        component.getAppState().setCurrentRatio (3.0);
        pump();

        const auto ratioImage = snapshot (component);
        check (writeImage (ratioImage, outputDirectory.getChildFile ("03-ratio-1to3.png")),
               "snapshot with the 1:3 ratio");

        // The two target families use their own colour and the dimmed own family
        // must also be present once a non-octave ratio is active.
        checkColourCount (ratioImage, theme::referenceLineTarget, 200,
                          "target frequency families are drawn");
        checkColourCount (ratioImage, theme::referenceLineDim, 200,
                          "the note's own family is dimmed to dashed", 45);

        // The ratio is a lens on the current selection, not note data, so selecting
        // the note again puts it straight back to 1:2.
        component.getAppState().selectNote (noteIds.empty() ? (juce::int64) 0 : noteIds.front(),
                                            false);
        pump();

        check (std::abs (component.getAppState().currentRatio - 2.0) < 1.0e-9,
               "selecting a note resets the ratio to 1:2 (got "
                   + juce::String (component.getAppState().currentRatio, 3) + ")");

        // Save and reload through the real JSON path.
        const auto projectFile = outputDirectory.getChildFile ("selftest.nprj");
        juce::String error;

        check (document.saveToFile (projectFile, error), "save project: " + error);

        ProjectDocument reloaded;
        check (reloaded.loadFromFile (projectFile, error), "load project: " + error);
        check (reloaded.getProject().tracks.front().clips.front().notes.size() == 5, "reloaded note count");
        const auto& reloadedLines = reloaded.getProject().referenceLines;
        check (reloadedLines.size() == 1
                   && std::abs (reloadedLines.front().baseHz - 440.0) < 1.0e-9
                   && reloadedLines.front().locked,
               "reloaded reference line state (got "
                   + juce::String ((int) reloadedLines.size()) + " entries)");

        // End to end audio: render the project through the real exporter and read
        // the file back, measuring the fundamental of the first note.
        const auto wavFile = outputDirectory.getChildFile ("selftest.wav");
        juce::String exportError;

        ExportSettings defaultExport;
        defaultExport.format = ExportSettings::Format::wav;
        defaultExport.bitsPerSample = 24;

        check (Exporter::renderToFile (document.getProject(), wavFile, defaultExport, {}, {}, {},
                                       exportError),
               "export WAV: " + exportError);

        juce::WavAudioFormat wavFormat;
        std::unique_ptr<juce::AudioFormatReader> reader (
            wavFormat.createReaderFor (new juce::FileInputStream (wavFile), true));

        check (reader != nullptr, "exported WAV is readable");

        if (reader != nullptr)
        {
            check (reader->numChannels == 2, "export is stereo");
            check (std::abs (reader->sampleRate - 48000.0) < 1.0, "export is 48 kHz");
            check (reader->bitsPerSample == 24, "export is 24 bit");

            // The project is five quarter notes at 120 bpm = 2.5 s, plus the tail
            // that covers the patch's slowest release.
            auto slowestRelease = 0.0;

            for (const auto& op : document.getProject().tracks.front().synth.patch.ops)
                slowestRelease = juce::jmax (slowestRelease, fm::rateToSeconds (op.rate[3]));

            const auto expectedSeconds = 2.5 + juce::jmax (0.5, slowestRelease + 0.3);
            const auto actualSeconds = (double) reader->lengthInSamples / reader->sampleRate;

            check (std::abs (actualSeconds - expectedSeconds) < 0.2,
                   "export length " + juce::String (actualSeconds, 3) + " s");

            const auto samplesToRead = juce::jmin ((juce::int64) (reader->sampleRate * 0.5),
                                                   reader->lengthInSamples);

            juce::AudioBuffer<float> rendered (1, (int) samplesToRead);
            reader->read (&rendered, 0, (int) samplesToRead, 0, true, false);

            const auto start = (int) (reader->sampleRate * 0.05);
            const auto end = (int) samplesToRead;
            const auto* data = rendered.getReadPointer (0);

            double first = -1.0, last = -1.0;
            int crossings = 0;

            for (int i = start + 1; i < end; ++i)
            {
                if (data[i - 1] <= 0.0f && data[i] > 0.0f)
                {
                    const auto previous = (double) data[i - 1];
                    const auto current = (double) data[i];
                    const auto fraction = previous == current ? 0.0
                                                              : (-previous) / (current - previous);
                    const auto time = ((double) (i - 1) + fraction) / reader->sampleRate;

                    if (first < 0.0) first = time;
                    last = time;
                    ++crossings;
                }
            }

            const auto measured = crossings > 1 && last > first
                                      ? (double) (crossings - 1) / (last - first)
                                      : 0.0;

            check (std::abs (measured - 220.0) < 0.5,
                   "first note renders at 220 Hz (measured "
                       + juce::String (measured, 3) + " Hz)");

            check (rendered.getRMSLevel (0, start, end - start) > 0.01f,
                   "exported audio is not silent");
        }

        // ---- editor interaction: note drag, edge resize, ruler seek -----------

        auto makeMouseEvent = [] (juce::Component& target, juce::Point<float> position,
                                  juce::Point<float> downPosition,
                                  juce::ModifierKeys modifiers = juce::ModifierKeys())
        {
            return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(),
                                     position, modifiers, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                     &target, &target, juce::Time::getCurrentTime(),
                                     downPosition, juce::Time::getCurrentTime(), 1, false);
        };

        auto readNote = [&document, trackId] (juce::int64 noteId)
        {
            if (const auto* note = document.findNote (trackId, noteId))
                return *note;

            return Note();
        };

        juce::int64 dragNoteId = 0;

        document.applyEdit ("selftest setup", [&dragNoteId, trackId] (Project& project)
        {
            auto* track = project.findTrack (trackId);

            if (track == nullptr)
                return;

            track->clips.front().notes.clear();

            Note note;
            note.id = project.nextNoteId++;
            note.startTick = 0;
            note.durationTicks = kTicksPerQuarter;
            note.frequencyHz = 220.0;
            note.velocity = 100;
            dragNoteId = note.id;
            track->clips.front().notes.push_back (note);

            auto line = music::makeFixedHzEntry (project.nextReferenceLineId++, 220.0, juce::String());
            project.referenceLines.push_back (std::move (line));
            project.view.trackSnap.denominator = 1;
            project.view.editorSnap.denominator = 1;
            project.view.controllerSnap.denominator = 1;
            project.view.pixelsPerQuarter = 140.0;
            project.view.horizontalOffsetTicks = 0.0;
            project.view.centreHz = 440.0;
            project.view.octavesVisible = 4.0;
            project.view.autoFitView = false;
        });

        pump();

        appState.setEditorOpen (true);
        appState.selectTrack (trackId); appState.selectClip (firstClipId());
        appState.selectNote (dragNoteId, false);
        pump();

        auto& grid = component.getEditorPanel().getGrid();

        beginTest ("dragging a note moves it onto the reference line under the mouse");

        {
            const auto down = grid.pointFor (220.0, 480);      // middle of the note body
            const auto target = grid.pointFor (440.0, 1440);   // one beat later, one octave up

            grid.mouseDown (makeMouseEvent (grid, down, down));
            grid.mouseDrag (makeMouseEvent (grid, target, down));

            // Mid drag the preview should be on screen but the document untouched.
            const auto previewImage = snapshot (component);
            check (writeImage (previewImage, outputDirectory.getChildFile ("04-drag-preview.png")),
                   "drag preview snapshot");
            checkColourCount (previewImage, theme::accent, 150, "the drag preview is drawn");
            check (std::abs (readNote (dragNoteId).frequencyHz - 220.0) < 1.0e-9,
                   "the document is unchanged until the mouse is released");

            grid.mouseUp (makeMouseEvent (grid, target, down));
        }

        pump();

        check (std::abs (readNote (dragNoteId).frequencyHz - 440.0) < 1.0e-6,
               "the note landed on the 440 Hz reference line (got "
                   + juce::String (readNote (dragNoteId).frequencyHz, 3) + " Hz)");
        check (readNote (dragNoteId).startTick == 960,
               "the note moved one beat later (got tick "
                   + juce::String (readNote (dragNoteId).startTick) + ")");

        document.undo();
        pump();

        check (std::abs (readNote (dragNoteId).frequencyHz - 220.0) < 1.0e-6
                   && readNote (dragNoteId).startTick == 0,
               "a single undo reverted the whole drag");

        beginTest ("dragging the right edge changes the length");

        {
            const auto edge = grid.pointFor (220.0, kTicksPerQuarter);
            const auto down = juce::Point<float> (edge.x - 2.0f, edge.y);
            const auto target = grid.pointFor (220.0, 2 * kTicksPerQuarter);

            grid.mouseDown (makeMouseEvent (grid, down, down));
            grid.mouseDrag (makeMouseEvent (grid, target, down));
            grid.mouseUp (makeMouseEvent (grid, target, down));
        }

        pump();

        check (readNote (dragNoteId).durationTicks == 2 * kTicksPerQuarter,
               "the note is two beats long (got " + juce::String (readNote (dragNoteId).durationTicks) + ")");
        check (readNote (dragNoteId).startTick == 0, "the start stayed where it was");

        document.undo();
        pump();

        check (readNote (dragNoteId).durationTicks == kTicksPerQuarter,
               "a single undo reverted the resize");

        beginTest ("dragging the left edge moves the start and keeps the end");

        {
            // The snap grid is one beat, so a note can never be shorter than that:
            // give it two beats of room before dragging the start edge inwards.
            document.applyEdit ("selftest lengthen", [trackId, dragNoteId] (Project& project)
            {
                if (auto* track = project.findTrack (trackId))
                    for (auto& note : track->clips.front().notes)
                        if (note.id == dragNoteId)
                        {
                            note.startTick = 0;
                            note.durationTicks = 2 * kTicksPerQuarter;
                        }
            });

            pump();

            const auto edge = grid.pointFor (220.0, 0);
            const auto down = juce::Point<float> (edge.x + 2.0f, edge.y);
            const auto target = grid.pointFor (220.0, kTicksPerQuarter);

            grid.mouseDown (makeMouseEvent (grid, down, down));
            grid.mouseDrag (makeMouseEvent (grid, target, down));
            grid.mouseUp (makeMouseEvent (grid, target, down));
        }

        pump();

        check (readNote (dragNoteId).startTick == kTicksPerQuarter
                   && readNote (dragNoteId).endTick() == 2 * kTicksPerQuarter,
               "start moved one beat later and the end did not move (start "
                   + juce::String (readNote (dragNoteId).startTick) + ", end "
                   + juce::String (readNote (dragNoteId).endTick()) + ")");

        document.undo();
        pump();

        check (readNote (dragNoteId).startTick == 0 && readNote (dragNoteId).endTick() == 2 * kTicksPerQuarter,
               "a single undo reverted the start edge drag");

        beginTest ("clicking the time ruler sets the playback start position");

        {
            auto& ruler = component.getEditorPanel().getRuler();
            const auto point = juce::Point<float> (280.0f, 10.0f);   // tick 1920 == 1.0 s

            ruler.mouseDown (makeMouseEvent (ruler, point, point));
        }

        pump();

        check (std::abs (component.getAudioEngine().getPositionSeconds() - 1.0) < 0.02,
               "the transport moved to 1.0 s (got "
                   + juce::String (component.getAudioEngine().getPositionSeconds(), 4) + " s)");

        beginTest ("the instrument sound editor draws");

        {
            SynthEditor synthEditor (document, component.getAudioEngine(),
                                     component.getPresetStore());
            synthEditor.setSize (780, 560);
            synthEditor.setTrack (trackId);

            const auto synthImage = snapshot (synthEditor);
            check (writeImage (synthImage, outputDirectory.getChildFile ("05-synth-editor.png")),
                   "sound editor snapshot");

            check (synthEditor.getControlBoundsForTest (6).getHeight() > 0,
                   "the audition keyboard has a real size ("
                       + juce::String (synthEditor.getControlBoundsForTest (6).getWidth()) + " x "
                       + juce::String (synthEditor.getControlBoundsForTest (6).getHeight()) + ")");
            checkColourCount (synthImage, juce::Colour (0xffe6e6ee), 2000,
                              "the keyboard white keys are drawn");
            checkColourCount (synthImage, juce::Colour (0xff17171b), 500,
                              "the keyboard black keys are drawn");
            checkColourCount (synthImage, theme::accent, 150,
                              "the selected algorithm is framed in the library");
            checkColourCount (synthImage, theme::accent, 20,
                              "section headings are drawn");
        }

        beginTest ("the FM editor walks the library, the operators and an envelope");

        {
            SynthEditor editor (document, component.getAudioEngine(),
                                component.getPresetStore());
            editor.setSize (780, 560);
            editor.setTrack (trackId);
            pump();

            // The preset bank is listed and picking one rewrites the whole patch.
            const auto pianoIndex = fm::findPresetByName (NI_TXT ("电钢琴 1"));
            check (pianoIndex >= 0, "the DX7 bank lists 电钢琴 1");
            editor.selectPresetForTest (pianoIndex);   // DX7 E.PIANO 1
            pump();

            const auto* track = document.findTrack (trackId);

            check (track != nullptr && track->synth.patch.name == NI_TXT ("电钢琴 1"),
                   "picking a preset names the patch (got '"
                       + (track != nullptr ? track->synth.patch.name : juce::String()) + "')");
            check (track != nullptr && track->synth.patch.algorithm == 37,
                   "the electric piano brings its own DX7 algorithm (got "
                       + juce::String (track != nullptr ? track->synth.patch.algorithm : 0) + ")");
            check (editor.getSelectedPresetIndex() == pianoIndex,
                   "the preset box follows the patch");

            // ---- level one: the library, 32 diagrams per page ------------------
            editor.setViewForTest (0);
            pump();

            auto cellsWithSize = 0;

            for (int i = 0; i < 32; ++i)
                if (editor.getAlgorithmCellBoundsForTest (i).getWidth() > 20
                    && editor.getAlgorithmCellBoundsForTest (i).getHeight() > 10)
                    ++cellsWithSize;

            check (cellsWithSize == 32,
                   "the first page lays out 32 algorithm pictures (got "
                       + juce::String (cellsWithSize) + ")");

            // ... and each one really draws its boxes. The pictures are built
            // while they still have zero size, so this is the regression test for
            // "the library renders as 32 empty squares".
            auto cellsWithBoxes = 0;

            for (int i = 0; i < 32; ++i)
            {
                auto hasBox = false;

                for (int op = 0; op < kFmOperators; ++op)
                    if (! editor.getAlgorithmCellBoxForTest (i, op).isEmpty())
                        hasBox = true;

                if (hasBox)
                    ++cellsWithBoxes;
            }

            check (cellsWithBoxes == 32,
                   "every algorithm picture draws its operator boxes (got "
                       + juce::String (cellsWithBoxes) + ")");

            const auto libraryImage = snapshot (editor);
            check (writeImage (libraryImage, outputDirectory.getChildFile ("17-fm-algorithms.png")),
                   "FM algorithm library snapshot");
            checkColourCount (libraryImage, theme::outline, 3000,
                              "the operator boxes inside the pictures are drawn", 12);

            // Clicking picture 17 picks that routing and drops one level down.
            editor.selectAlgorithmForTest (17);
            pump();

            check (document.findTrack (trackId)->synth.patch.algorithm == 17,
                   "clicking a picture sets the routing (got "
                       + juce::String (document.findTrack (trackId)->synth.patch.algorithm) + ")");
            check (editor.getViewForTest() == 1, "the click moved to the operator overview");

            // ---- the second page: the 32 DX7 routings -------------------------
            editor.setViewForTest (0);
            editor.setLibraryPageForTest (1);
            pump();

            check (editor.getLibraryPageForTest() == 1, "the library has a DX7 page");

            auto dx7CellsWithSize = 0;

            for (int i = 0; i < 32; ++i)
                if (editor.getAlgorithmCellBoundsForTest (i).getWidth() > 20
                    && editor.getAlgorithmCellBoundsForTest (i).getHeight() > 10)
                    ++dx7CellsWithSize;

            check (dx7CellsWithSize == 32,
                   "the DX7 page lays out 32 algorithm pictures (got "
                       + juce::String (dx7CellsWithSize) + ")");

            const auto dx7Image = snapshot (editor);
            check (writeImage (dx7Image, outputDirectory.getChildFile ("20-fm-algorithms-dx7.png")),
                   "FM DX7 algorithm library snapshot");

            editor.selectAlgorithmForTest (33);
            pump();

            check (document.findTrack (trackId)->synth.patch.algorithm == 33,
                   "picking a DX7 routing sets algorithm 33");
            check (editor.getLibraryPageForTest() == 1,
                   "picking a DX7 routing keeps the DX7 page");

            // Back to a known HM routing for the operator tests below.
            editor.selectAlgorithmForTest (17);
            editor.setLibraryPageForTest (0);
            pump();

            // ---- level two: the six operator boxes ----------------------------
            auto boxesWithSize = 0;

            for (int i = 0; i < kFmOperators; ++i)
                if (! editor.getOperatorBoxBoundsForTest (i).isEmpty())
                    ++boxesWithSize;

            check (boxesWithSize == kFmOperators,
                   "all six operator boxes are laid out (got " + juce::String (boxesWithSize) + ")");

            const auto overviewImage = snapshot (editor);
            check (writeImage (overviewImage, outputDirectory.getChildFile ("18-fm-operators.png")),
                   "FM operator overview snapshot");

            // 12-fm-operators.png is the name this picture had before the editor
            // became a three level drill-down; keep the alias in step so an older
            // delivery folder never shows a page that no longer exists.
            writeImage (overviewImage, outputDirectory.getChildFile ("12-fm-operators.png"));

            checkColourCount (overviewImage, theme::referenceLineDim, 1000,
                              "the modulation lines are drawn", 30);
            checkColourCount (overviewImage, theme::accent, 80,
                              "the operator switches are drawn");

            // The classic six-deep stack has to read bottom-up: every modulator
            // sits one row above the operator it feeds.
            editor.selectAlgorithmForTest (1);
            pump();

            {
                auto stacked = true;

                for (int i = 1; i < kFmOperators; ++i)
                    if (editor.getOperatorBoxBoundsForTest (i).getY()
                            >= editor.getOperatorBoxBoundsForTest (i - 1).getY())
                        stacked = false;

                check (stacked, "algorithm 1 stacks 6-5-4-3-2-1 with the carrier at the bottom");
            }

            editor.selectAlgorithmForTest (17);
            pump();

            // ---- level three: OP3 in detail -----------------------------------
            editor.setOperatorForTest (2);
            pump();

            check (editor.getOperatorForTest() == 2, "clicking a box opens that operator");
            check (editor.getViewForTest() == 2, "the box opened the detail page");
            check (editor.getControlBoundsForTest (3).getWidth() > 0
                       && editor.getControlBoundsForTest (4).getWidth() > 0,
                   "the envelope sliders have real sizes");
            check (editor.getControlBoundsForTest (5).getWidth() > 100,
                   "the envelope curve has a real size ("
                       + juce::String (editor.getControlBoundsForTest (5).getWidth()) + " x "
                       + juce::String (editor.getControlBoundsForTest (5).getHeight()) + ")");

            editor.setRateForTest (0, 33);
            pump();

            check (document.findTrack (trackId)->synth.patch.ops[2].rate[0] == 33,
                   "moving an envelope slider writes to the selected operator (got "
                       + juce::String (document.findTrack (trackId)->synth.patch.ops[2].rate[0]) + ")");
            check (document.findTrack (trackId)->synth.patch.ops[1].rate[0] != 33,
                   "the other operators are left alone");

            const auto envelopeImage = snapshot (editor);
            check (writeImage (envelopeImage, outputDirectory.getChildFile ("19-fm-envelope.png")),
                   "FM envelope snapshot");
            checkColourCount (envelopeImage, theme::accent, 200,
                              "the envelope curve is drawn");

            // The LFO and output screens still lay out.
            editor.setViewForTest (3);
            pump();
            check (writeImage (snapshot (editor), outputDirectory.getChildFile ("13-fm-lfo.png")),
                   "FM LFO screen snapshot");

            editor.setViewForTest (4);
            pump();
            check (writeImage (snapshot (editor), outputDirectory.getChildFile ("14-fm-output.png")),
                   "FM output screen snapshot");

            check (editor.getControlBoundsForTest (17).getWidth() > 0
                       && editor.getControlBoundsForTest (18).getWidth() > 0,
                   "the LFO screen has the sync switch and pitch sensitivity");

            // ---- the extras screen: fixed frequency, scaling, pitch EG --------
            editor.setViewForTest (5);
            pump();

            auto extrasControls = 0;

            for (int which = 9; which <= 16; ++which)
                if (editor.getControlBoundsForTest (which).getWidth() > 0
                    && editor.getControlBoundsForTest (which).getHeight() > 0)
                    ++extrasControls;

            for (int which = 19; which <= 20; ++which)
                if (editor.getControlBoundsForTest (which).getWidth() > 0)
                    ++extrasControls;

            check (extrasControls == 10,
                   "the extras screen lays out every DX7 control (got "
                       + juce::String (extrasControls) + ")");
            check (writeImage (snapshot (editor), outputDirectory.getChildFile ("21-fm-extras.png")),
                   "FM extras screen snapshot");

            editor.setOscModeForTest (true);
            pump();

            const auto selectedForMode = editor.getOperatorForTest();
            check (document.findTrack (trackId)->synth.patch.ops[(size_t) selectedForMode].mode
                       == FmOscMode::fixed,
                   "the extras screen switches an operator to fixed frequency");

            editor.setOscModeForTest (false);
            pump();
            check (document.findTrack (trackId)->synth.patch.ops[(size_t) selectedForMode].mode
                       == FmOscMode::ratio,
                   "the extras screen switches it back to ratio");

            // ---- the per-operator switch --------------------------------------
            editor.setViewForTest (1);
            pump();
            editor.toggleOperatorForTest (2);       // the switch on the OP3 box
            pump();

            check (! document.findTrack (trackId)->synth.patch.ops[2].enabled,
                   "the overview switch turns an operator off");
            check (document.findTrack (trackId)->synth.patch.ops[1].enabled,
                   "only the clicked operator changes");

            editor.setViewForTest (2);
            pump();

            check (! document.findTrack (trackId)->synth.patch.ops[2].enabled,
                   "the detail page shows the same switch state");

            editor.toggleOperatorForTest (2);
            pump();

            check (document.findTrack (trackId)->synth.patch.ops[2].enabled,
                   "the detail page switch turns it back on");

            // Switching every carrier off silences the track; switching them back
            // on brings the sound straight back.
            const auto& algorithm = getFmAlgorithm (document.findTrack (trackId)->synth.patch.algorithm);
            std::vector<int> carriers;

            for (int i = 0; i < kFmOperators; ++i)
                if ((algorithm.carriers & (juce::uint8) (1u << i)) != 0)
                    carriers.push_back (i);

            check (! carriers.empty(), "the chosen algorithm has at least one carrier");

            for (auto index : carriers)
                editor.toggleOperatorForTest (index);

            pump();

            {
                TrackSynth synth;
                synth.prepare (48000.0);
                synth.setParams (document.findTrack (trackId)->synth);
                synth.noteOn (1, 440.0f, 127.0f);

                juce::AudioBuffer<float> silent (2, 4800);
                silent.clear();
                synth.renderAdding (silent, 0, 4800, 1.0f, 1.0f);

                check (silent.getRMSLevel (0, 0, 4800) < 1.0e-6f,
                       "a patch whose carriers are all off renders silence (RMS "
                           + juce::String (silent.getRMSLevel (0, 0, 4800), 8) + ")");
            }

            for (auto index : carriers)
                editor.toggleOperatorForTest (index);

            pump();

            {
                TrackSynth synth;
                synth.prepare (48000.0);
                synth.setParams (document.findTrack (trackId)->synth);
                synth.noteOn (1, 440.0f, 127.0f);

                juce::AudioBuffer<float> restored (2, 4800);
                restored.clear();
                synth.renderAdding (restored, 0, 4800, 1.0f, 1.0f);

                check (restored.getRMSLevel (0, 0, 4800) > 0.01f,
                       "switching them back on restores the sound");
            }

            // ---- saving, loading, renaming and deleting a preset --------------
            document.applyEdit ("selftest patch for the preset", [] (Project& project)
            {
                if (! project.tracks.empty())
                {
                    auto& synth = project.tracks.front().synth;
                    synth.patch = FmPatch();
                    synth.patch.name = NI_TXT ("自检音色");
                    synth.patch.ops[1].ratio       = 3.0f;
                    synth.patch.ops[1].outputLevel = 71;
                    synth.patch.ops[1].enabled     = false;
                    synth.gain = 0.42f;
                }
            });
            pump();

            check (editor.savePresetForTest (NI_TXT ("测试音色"), false),
                   "the current sound saves as a preset");
            pump();

            const auto& savedFm = component.getPresetStore().getFmPresets();

            check (savedFm.size() == 1,
                   "the library holds one FM preset (got "
                       + juce::String ((int) savedFm.size()) + ")");
            check (savedFm.size() == 1 && std::abs (savedFm.front().patch.ops[1].ratio - 3.0f) < 1.0e-6f
                       && savedFm.front().patch.ops[1].outputLevel == 71
                       && ! savedFm.front().patch.ops[1].enabled
                       && std::abs (savedFm.front().gain - 0.42f) < 1.0e-6f,
                   "the saved preset keeps every operator and the output stage");

            editor.selectPresetForTest (0);        // a factory sound first
            pump();
            check (document.findTrack (trackId)->synth.patch.name == NI_TXT ("钢琴 1"),
                   "a factory preset loads over it");

            editor.loadUserPresetForTest (0);
            pump();

            const auto* reloadedTrack = document.findTrack (trackId);
            check (reloadedTrack != nullptr && reloadedTrack->synth.patch.name == NI_TXT ("测试音色")
                       && reloadedTrack->synth.patch.ops[1].outputLevel == 71
                       && ! reloadedTrack->synth.patch.ops[1].enabled,
                   "loading the saved preset brings the whole sound back");
            check (editor.getSelectedPresetIdForTest() == kUserPresetIdBase,
                   "the list highlights the user preset (id "
                       + juce::String (editor.getSelectedPresetIdForTest()) + ")");

            check (editor.renamePresetForTest (NI_TXT ("测试音色 2")), "the preset renames");
            pump();

            check (component.getPresetStore().getFmPresets().size() == 1
                       && component.getPresetStore().getFmPresets().front().patch.name
                              == NI_TXT ("测试音色 2"),
                   "the rename lands in the library");
            check (document.findTrack (trackId)->synth.patch.name == NI_TXT ("测试音色 2"),
                   "the loaded sound follows the new name");

            // Saving the same name again goes down the overwrite branch.
            check (! editor.savePresetForTest (NI_TXT ("测试音色 2"), false),
                   "saving over an existing name without permission is refused");
            check (editor.savePresetForTest (NI_TXT ("测试音色 2"), true),
                   "confirming the overwrite replaces the preset");
            pump();
            check (component.getPresetStore().getFmPresets().size() == 1,
                   "the overwrite did not add a second entry");

            const auto presetImage = snapshot (editor);
            check (writeImage (presetImage, outputDirectory.getChildFile ("20-preset-saved.png")),
                   "saved preset snapshot");
            check (editor.getControlBoundsForTest (0).getHeight() > 0
                       && editor.getControlBoundsForTest (0).getWidth() > 100,
                   "the preset row really is laid out ("
                       + juce::String (editor.getControlBoundsForTest (0).getWidth()) + " x "
                       + juce::String (editor.getControlBoundsForTest (0).getHeight()) + ")");

            check (editor.deletePresetForTest(), "the preset deletes");
            pump();
            check (component.getPresetStore().getFmPresets().empty(),
                   "the library is empty again");

            // Put the track back on a plain sine for the checks further down.
            document.applyEdit ("selftest sine patch again", [] (Project& project)
            {
                if (! project.tracks.empty())
                    project.tracks.front().synth.patch = FmPatch();
            });
            pump();
        }

        beginTest ("a sampler track imports an audio file and plays it at the note's pitch");

        {
            // A short WAV to import: one second of 220 Hz.
            const auto sampleFile = outputDirectory.getChildFile ("selftest-sample.wav");
            sampleFile.deleteFile();

            {
                juce::WavAudioFormat wav;
                std::unique_ptr<juce::OutputStream> stream (sampleFile.createOutputStream());

                if (stream != nullptr)
                {
                    const auto options = juce::AudioFormatWriterOptions{}
                                             .withSampleFormat (
                                                 juce::AudioFormatWriterOptions::SampleFormat::integral)
                                             .withSampleRate (48000.0)
                                             .withNumChannels (1)
                                             .withBitsPerSample (16);
                    auto writer = wav.createWriterFor (stream, options);

                    if (writer != nullptr)
                    {
                        juce::AudioBuffer<float> tone (1, 48000);

                        for (int i = 0; i < tone.getNumSamples(); ++i)
                            tone.setSample (0, i, (float) std::sin (2.0 * juce::MathConstants<double>::pi
                                                                     * 220.0 * i / 48000.0));

                        writer->writeFromAudioSampleBuffer (tone, 0, tone.getNumSamples());
                    }
                }
            }

            auto& lanes = component.getTrackArea();
            lanes.addTrack (TrackKind::sampler);
            pump();

            const auto& tracks = document.getProject().tracks;
            check (tracks.size() == 2 && tracks.back().kind == TrackKind::sampler,
                   "the + menu added a sampler track (got "
                       + juce::String ((int) tracks.size()) + " tracks)");

            const auto samplerTrackId = tracks.back().id;

            SamplerEditor samplerEditor (document, component.getAudioEngine(),
                                         component.getPresetStore());
            samplerEditor.setSize (780, 560);
            samplerEditor.setTrack (samplerTrackId);
            pump();

            for (int which = 0; which < 5; ++which)
            {
                const auto bounds = samplerEditor.getControlBoundsForTest (which);
                check (bounds.getWidth() > 0 && bounds.getHeight() > 0,
                       "sampler control " + juce::String (which) + " has a real size ("
                           + juce::String (bounds.getWidth()) + " x "
                           + juce::String (bounds.getHeight()) + ")");
            }

            // Every parameter needs a caption: the four envelope sliders under a
            // single "包络" label left the user guessing which one was the decay.
            {
                const auto captions = samplerEditor.getCaptionsForTest();
                const juce::StringArray expected {
                    NI_TXT ("基准音高"), NI_TXT ("循环起点"), NI_TXT ("循环终点"),
                    NI_TXT ("循环播放"), NI_TXT ("起音"), NI_TXT ("衰减"),
                    NI_TXT ("保持"), NI_TXT ("释放"), NI_TXT ("截止"),
                    NI_TXT ("共振"), NI_TXT ("速率"), NI_TXT ("音高"),
                    NI_TXT ("幅度"), NI_TXT ("增益") };

                juce::StringArray missing;

                for (const auto& name : expected)
                    if (! captions.contains (name))
                        missing.add (name);

                check (missing.isEmpty(),
                       NI_TXT ("every sampler parameter is named (missing: ")
                           + missing.joinIntoString (", ") + NI_TXT ("; got: ")
                           + captions + ")");
            }

            const auto loadError = samplerEditor.loadFile (sampleFile);
            pump();

            check (loadError.isEmpty(), "the audio file imports: " + loadError);

            const auto* samplerTrack = document.findTrack (samplerTrackId);

            check (samplerTrack != nullptr
                       && samplerTrack->sampler.filePath == sampleFile.getFullPathName(),
                   "the track remembers the imported file");

            samplerEditor.setRootHzForTest (220.0);
            samplerEditor.setLoopForTest (true, 0.25, 0.75);
            pump();

            check (samplerTrack != nullptr && samplerTrack->sampler.looped
                       && std::abs (samplerTrack->sampler.rootHz - 220.0) < 1.0e-9,
                   "the sampler settings land on the track");

            // The schedule has to carry the decoded sample, and the track has to be
            // rendered by the sample player rather than the FM synth.
            const auto samplerSchedule = buildSchedule (document.getProject());
            check ((int) samplerSchedule.tracks.size() == 2
                       && samplerSchedule.tracks.back().kind == TrackKind::sampler
                       && samplerSchedule.tracks.back().sample != nullptr
                       && samplerSchedule.tracks.back().sample->audio.getNumSamples() == 48000,
                   "the render schedule carries the imported sample");

            const auto samplerImage = snapshot (samplerEditor);
            check (writeImage (samplerImage, outputDirectory.getChildFile ("15-sampler.png")),
                   "sampler editor snapshot");
            // The waveform is 85% opaque over the dark grid, so compare loosely.
            checkColourCount (samplerImage, theme::referenceLine.withAlpha (0.85f), 200,
                              "the sample waveform is drawn", 40);
            checkColourCount (samplerImage, theme::accent, 100,
                              "the loop markers and headings are drawn");

            // The sampler carries a preset row of its own.
            check (samplerEditor.savePresetForTest (NI_TXT ("自检采样"), false),
                   "the sampler sound saves as a preset");
            pump();

            const auto& samplerPresets = component.getPresetStore().getSamplerPresets();
            check (samplerPresets.size() == 1
                       && samplerPresets.front().filePath == sampleFile.getFullPathName()
                       && samplerPresets.front().looped
                       && std::abs (samplerPresets.front().rootHz - 220.0) < 1.0e-9,
                   "the sampler preset keeps the file and the loop settings (got "
                       + juce::String ((int) samplerPresets.size()) + ")");
            check (samplerEditor.getPresetBar().getComboBoxBoundsForTest().getHeight() > 0,
                   "the sampler preset row has a real size");

            // Leave the instrument library empty again for the checks below.
            component.getPresetStore().removeSamplerPreset (0);
            pump();

            // A file that is not there has to be *said*, not silently shown as a
            // zero-length sample (the "载入失败" line used to be overwritten).
            const auto missingSample = outputDirectory.getChildFile ("nidaw-sampler-missing.wav");
            missingSample.deleteFile();

            document.applyEdit ("selftest sampler missing file",
                                [samplerTrackId, missingSample] (Project& project)
            {
                if (auto* t = project.findTrack (samplerTrackId))
                    t->sampler.filePath = missingSample.getFullPathName();
            });
            pump();

            check (samplerEditor.getFileLabelTextForTest().contains (NI_TXT ("载入失败")),
                   NI_TXT ("采样器也会写明载入失败（「")
                       + samplerEditor.getFileLabelTextForTest() + NI_TXT ("」）"));

            // Leaving it behind would confuse the later "one track" assumptions.
            lanes.deleteTrackNow (samplerTrackId);
            pump();

            check ((int) document.getProject().tracks.size() == 1,
                   "the sampler track is cleaned up again");
        }

        beginTest ("an audio track holds an imported file as a clip");

        {
            // The sampler block above already wrote this WAV; reuse it.
            const auto sampleFile = outputDirectory.getChildFile ("selftest-sample.wav");

            auto& lanes = component.getTrackArea();
            const auto audioTrackId = lanes.addTrack (TrackKind::audio);
            pump();

            check (audioTrackId != 0
                       && document.getProject().tracks.size() == 2
                       && document.getProject().tracks.back().kind == TrackKind::audio
                       && document.getProject().tracks.back().clips.empty(),
                   "an audio track starts empty (it waits for a file)");

            const auto clipId = lanes.importAudioFile (audioTrackId, sampleFile);
            pump();

            const auto* clip = document.getProject().findClip (clipId);

            check (clipId != 0 && clip != nullptr && clip->isAudio()
                       && clip->audioFilePath == sampleFile.getFullPathName(),
                   "the imported file became an audio clip");
            check (clip != nullptr && clip->lengthTicks == kTicksPerQuarter * 2,
                   "the clip covers the file's length (got "
                       + juce::String (clip != nullptr ? clip->lengthTicks : 0)
                       + " ticks, expected " + juce::String (kTicksPerQuarter * 2) + ")");

            // The schedule carries it as a placed audio clip, not as notes.
            const auto audioSchedule = buildSchedule (document.getProject());
            check ((int) audioSchedule.tracks.size() == 2
                       && audioSchedule.tracks.back().kind == TrackKind::audio
                       && audioSchedule.tracks.back().audioClips.size() == 1
                       && audioSchedule.tracks.back().audioClips.front().sample != nullptr
                       && audioSchedule.tracks.back().events.empty(),
                   "the schedule carries the placed audio clip");

            const auto laneImage = snapshot (lanes);
            checkColourCount (laneImage, juce::Colours::white.withAlpha (0.65f), 100,
                              "the lane draws the clip's waveform", 90);

            appState.selectTrack (audioTrackId);
            appState.selectClip (clipId);
            pump();

            const auto gridImage = snapshot (component.getEditorPanel().getGrid());
            checkColourCount (gridImage, theme::referenceLine.withAlpha (0.85f), 200,
                              "the editor draws the clip's waveform", 40);
            check (writeImage (gridImage, outputDirectory.getChildFile ("16-audio-track.png")),
                   "audio clip editor snapshot");

            lanes.deleteTrackNow (audioTrackId);
            pump();
        }

        beginTest ("the sound editor window can be created and pointed at a track");

        {
            SynthWindow window (document, component.getAudioEngine(),
                                component.getPresetStore(), false);
            window.getEditor().setTrack (trackId);

            check (window.getEditor().getTrackId() == trackId,
                   "the window's editor follows the requested track");
            check (! window.isVisible(), "the window stays hidden until it is asked for");
        }

        // ---- reference group segment -------------------------------------

        beginTest ("the reference group segment builds dots and crosses");

        {
            // A known note to anchor everything to.
            juce::int64 referenceNoteId = 0;

            document.applyEdit ("selftest reference setup", [&referenceNoteId, trackId] (Project& project)
            {
                auto* track = project.findTrack (trackId);

                if (track == nullptr)
                    return;

                track->clips.front().notes.clear();

                Note note;
                note.id = project.nextNoteId++;
                note.startTick = 0;
                note.durationTicks = kTicksPerQuarter;
                note.frequencyHz = 220.0;
                note.velocity = 100;
                referenceNoteId = note.id;
                track->clips.front().notes.push_back (note);

                project.referenceGroup.dots.clear();
                project.referenceLines.clear();
                project.view.autoFitView = false;
                project.view.centreHz = 440.0;
                project.view.octavesVisible = 4.0;
            });

            pump();

            appState.setEditorOpen (true);
            appState.selectTrack (trackId); appState.selectClip (firstClipId());
            appState.selectNote (referenceNoteId, false);
            pump();

            auto& panel = component.getEditorPanel().getReferenceGroupPanel();
            auto& ladder = panel.getLadder();

            check (ladder.getWidth() > 100 && ladder.getHeight() > 100,
                   "the segment has a usable size (" + juce::String (ladder.getWidth()) + " x "
                       + juce::String (ladder.getHeight()) + ")");

            // Select the bottom frame dot.
            const auto framePoint = ladder.pointForPosition (0.0);
            ladder.mouseDown (makeMouseEvent (ladder, framePoint, framePoint));
            pump();

            check (ladder.getSelectedDotCount() == 1, "the frame dot can be selected");

            // 1:3 on that dot produces a cross at the multiplied and divided classes.
            ladder.showRatioCrosses (3.0);
            pump();

            const auto crossImage = snapshot (component);
            check (writeImage (crossImage, outputDirectory.getChildFile ("06-reference-crosses.png")),
                   "reference segment snapshot");
            checkColourCount (crossImage, theme::accent, 150, "the segment and its crosses are drawn");

            // Clicking a cross turns it into a real dot.
            const auto crossPoint = ladder.pointForPosition (music::ratioToPosition (3.0));
            ladder.mouseDown (makeMouseEvent (ladder, crossPoint, crossPoint));
            ladder.mouseUp (makeMouseEvent (ladder, crossPoint, crossPoint));
            pump();

            check (document.getProject().referenceGroup.dots.size() == 1,
                   "clicking a cross created one dot (got "
                       + juce::String ((int) document.getProject().referenceGroup.dots.size()) + ")");

            // Give the dot a colour that nothing else in the editor uses, so the
            // pixel check below can only be matching this group's reference lines.
            document.applyEdit ("selftest dot colour", [trackId] (Project& project)
            {
                juce::ignoreUnused (trackId);

                if (! project.referenceGroup.dots.empty())
                    project.referenceGroup.dots.front().colourIndex = 4;
            });

            pump();

            beginTest ("applying the group to a note creates reference lines");

            appState.setReferencePick (AppState::ReferencePick::apply);
            appState.armDot (0.0);
            pump();

            const auto notePoint = grid.pointFor (220.0, 0);
            grid.mouseDown (makeMouseEvent (grid, notePoint, notePoint));
            pump();

            const auto& groups = document.getProject().referenceLines;
            const auto groupCount = (int) std::count_if (groups.begin(), groups.end(),
                                                         [] (const ReferenceLineEntry& e)
                                                         { return e.kind == ReferenceLineKind::group; });
            check (groupCount == 1, "one reference line group was created (got "
                                        + juce::String (groupCount) + ")");

            const auto firstGroup = std::find_if (groups.begin(), groups.end(),
                                                  [] (const ReferenceLineEntry& e)
                                                  { return e.kind == ReferenceLineKind::group; });

            if (firstGroup != groups.end())
            {
                checkNear (firstGroup->anchorHz, 220.0, 1.0e-6,
                           "the group is anchored to the clicked note");
            }

            check (! appState.referenceLineHistory.empty(),
                   "the group was recorded in the history");

            const auto appliedImage = snapshot (component);
            check (writeImage (appliedImage, outputDirectory.getChildFile ("07-reference-applied.png")),
                   "applied reference group snapshot");
            checkColourCount (appliedImage, theme::trackColour (4), 300,
                              "the group's reference lines use the dot's colour");

            beginTest ("the reference line status row is really laid out");

            const auto rowBounds = component.getEditorPanel().getOptionsPanel().getRowBounds (0);

            check (rowBounds.getWidth() > 0 && rowBounds.getHeight() > 0,
                   "the reference control row has a real size (" + juce::String (rowBounds.getWidth())
                       + " x " + juce::String (rowBounds.getHeight()) + ")");

            beginTest ("the preset beside panel renders");

            appState.toggleSidePanel (AppState::SidePanel::presets);
            appState.setPresetTab (false);
            pump();

            const auto presetImage = snapshot (component);
            check (writeImage (presetImage, outputDirectory.getChildFile ("08-preset-panel.png")),
                   "preset panel snapshot");

            appState.toggleSidePanel (AppState::SidePanel::none);
            pump();
        }

        // ---- frame dots, and marquee note selection ------------------------

        const juce::ModifierKeys ctrlModifier (juce::ModifierKeys::ctrlModifier);

        beginTest ("both ends of the octave can be selected and the octave divided");

        {
            auto& panel = component.getEditorPanel().getReferenceGroupPanel();
            auto& ladder = panel.getLadder();

            document.applyEdit ("selftest clear segment", [] (Project& project)
            {
                project.referenceGroup.dots.clear();
            });
            pump();

            const auto bottomPoint = ladder.pointForPosition (0.0);
            ladder.mouseDown (makeMouseEvent (ladder, bottomPoint, bottomPoint));

            const auto topPoint = ladder.pointForPosition (1.0);
            ladder.mouseDown (makeMouseEvent (ladder, topPoint, topPoint, ctrlModifier));

            check (ladder.getSelectedDotCount() == 2,
                   "both frame dots are selected (got "
                       + juce::String (ladder.getSelectedDotCount()) + ")");

            ladder.showDivisionCrosses (12);
            const auto crosses = ladder.getCrossPositions();

            check (crosses.size() == 11,
                   "dividing the octave into 12 gives 11 crosses (got "
                       + juce::String ((int) crosses.size()) + ")");

            if (! crosses.empty())
            {
                const auto crossPoint = ladder.pointForPosition (crosses.front());
                ladder.mouseDown (makeMouseEvent (ladder, crossPoint, crossPoint));
                ladder.mouseUp (makeMouseEvent (ladder, crossPoint, crossPoint));
                pump();

                const auto& dots = document.getProject().referenceGroup.dots;
                const auto position = dots.empty() ? 0.0 : dots.front().position;

            check (dots.size() == 1 && std::abs (position - 1.0 / 12.0) < 1.0e-6,
                   "the new dot landed on one twelfth of the octave (got "
                       + juce::String (position, 5) + ")");
            }

            beginTest ("promoting one cross leaves the rest of the batch alone");

            document.applyEdit ("selftest clear again", [] (Project& project)
            {
                project.referenceGroup.dots.clear();
            });
            pump();

            ladder.clearTransientCrosses();

            // Select one interior dot so the ratio crosses can be generated.
            document.applyEdit ("selftest seed dot", [] (Project& project)
            {
                music::addDotAt (project.referenceGroup, 0.25, 0);
            });
            pump();

            const auto seedPoint = ladder.pointForPosition (0.25);
            ladder.mouseDown (makeMouseEvent (ladder, seedPoint, seedPoint));
            ladder.showRatioCrosses (3.0);

            const auto batchSize = ladder.getCrossPositions().size();
            check (batchSize == 2, "the ratio produced two crosses (got "
                                       + juce::String ((int) batchSize)
                                       + "; dots=" + juce::String ((int) document.getProject().referenceGroup.dots.size())
                                       + " selectedDots=" + juce::String (ladder.getSelectedDotCount())
                                       + " selectedPos=" + juce::String (ladder.getSelectedDotPosition(), 4)
                                       + " pick=" + juce::String ((int) appState.referencePick) + ")");

            if (batchSize == 2)
            {
                const auto firstCross = ladder.pointForPosition (ladder.getCrossPositions().front());
                ladder.mouseDown (makeMouseEvent (ladder, firstCross, firstCross));
                ladder.mouseUp (makeMouseEvent (ladder, firstCross, firstCross));
                pump();

                // The promoted cross disappears, the other one must survive: the
                // batch is only cleared when the next batch is generated.
                check (ladder.getCrossPositions().size() == 1,
                       "the other cross is still there (got "
                           + juce::String ((int) ladder.getCrossPositions().size()) + ")");
            }

            beginTest ("the frame dots can be coloured");

            const auto framePoint = ladder.pointForPosition (0.0);
            ladder.mouseDown (makeMouseEvent (ladder, framePoint, framePoint));
            pump();

            check (panel.isPaletteEnabled(),
                   "the palette is enabled while a frame dot is selected");
        }

        // ---- 参考组 / 参考线组编辑栏（第三十九批） ---------------------------

        beginTest (NI_TXT ("三处「编辑」都把左栏换成编辑栏（参考线组预设 / 参考线组状态 / 参考组预设）"));

        {
            auto& editorPanel = component.getEditorPanel();
            auto& referencePanel = editorPanel.getReferenceGroupPanel();
            auto& editor = editorPanel.getGroupEditorPanel();
            auto& presets = component.getPresetStore();

            appState.setEditorOpen (true);
            appState.clearNotes();

            // A clean segment with TWO dots, so "the preview" and "what 返回 put
            // back" can never be confused for one another.
            document.applyEdit ("selftest group editor setup", [] (Project& project)
            {
                project.referenceGroup = ReferenceGroup();
                project.referenceGroup.name = NI_TXT ("工程的参考组");
                music::addDotAt (project.referenceGroup, 0.1, 1);
                music::addDotAt (project.referenceGroup, 0.6, 1);
                project.referenceLines.clear();
            });
            pump();

            ReferenceGroup presetGroup;
            presetGroup.name = NI_TXT ("预设参考组");
            music::addDotAt (presetGroup, 0.5, 3);

            const auto referenceGroupIndex = (int) presets.getReferenceGroups().size();
            presets.addReferenceGroup (presetGroup);

            ReferenceLineGroup presetLine;
            presetLine.name = NI_TXT ("预设参考线组");
            presetLine.anchorHz = 880.0;
            presetLine.anchorPosition = 0.0;
            presetLine.dots.push_back ({ 0.25, 4 });

            const auto lineGroupIndex = (int) presets.getReferenceLineGroups().size();
            presets.addReferenceLineGroup (presetLine);

            auto lineEntryId = (juce::int64) 0;
            const auto entryGroup = presetLine;

            document.applyEdit ("selftest group entry", [&lineEntryId, entryGroup] (Project& project)
            {
                auto entry = music::makeGroupEntry (project.nextReferenceLineId++, entryGroup);
                entry.name = NI_TXT ("工程的参考线组");
                lineEntryId = entry.id;
                project.referenceLines.push_back (std::move (entry));
            });
            pump();

            // --- 1. the 参考线组预设 row's 编辑 ---
            appState.toggleSidePanel (AppState::SidePanel::presets);
            appState.setPresetTab (true);
            pump();

            auto& presetPanel = editorPanel.getPresetPanel();

            check ((int) presets.getReferenceLineGroups().size() == lineGroupIndex + 1
                       && lineGroupIndex < presetPanel.getRowCountForTest(),
                   NI_TXT ("预设库里多了一条参考线组，面板上也有对应的行"));
            check (presetPanel.getRowEditTextForTest (lineGroupIndex) == NI_TXT ("编辑"),
                   NI_TXT ("参考线组预设那一行有「编辑」按钮（实际是「")
                       + presetPanel.getRowEditTextForTest (lineGroupIndex) + NI_TXT ("」）"));

            const auto undoBeforeEditor = document.getUndoDescription();

            presetPanel.triggerEditForTest (lineGroupIndex);
            pump();

            check (editorPanel.isGroupEditorOpenForTest() && editor.isVisible(),
                   NI_TXT ("左栏变成了编辑栏"));
            check (! referencePanel.isVisible(), NI_TXT ("参考组栏让开了那一格"));
            check (editor.getHeadingForTest().contains (NI_TXT ("参考线组"))
                       && editor.getHeadingForTest().contains (NI_TXT ("预设参考线组")),
                   NI_TXT ("表头写出了正在编辑哪一条（") + editor.getHeadingForTest() + NI_TXT ("）"));
            check (editor.getLadder().getLook().plainLine,
                   NI_TXT ("参考线组编辑栏的线段是一根普通直线"));
            check (editor.getLadder().getLook().frequencyLabels,
                   NI_TXT ("它旁边写的是频率而不是比率"));
            check (editor.isAnchorRowVisibleForTest()
                       && editor.getAnchorTextForTest().contains ("880"),
                   NI_TXT ("锚点标了出来（") + editor.getAnchorTextForTest() + NI_TXT ("）"));
            check (editor.getBackBoundsForTest().getWidth() > 0
                       && editor.getSaveBoundsForTest().getWidth() > 0
                       && editor.getSaveAsBoundsForTest().getWidth() > 0,
                   NI_TXT ("返回 / 保存 / 另存为 都排得下"));
            check (document.getUndoDescription() == undoBeforeEditor,
                   NI_TXT ("打开编辑栏不进撤销栈（还是「") + undoBeforeEditor + NI_TXT ("」）"));

            // The preview really is on the project's reference group, and it is the
            // preset's dots, not the project's.
            check (document.getProject().referenceGroup.dots.size() == 1
                       && std::abs (document.getProject().referenceGroup.dots.front().position - 0.25) < 1.0e-6,
                   NI_TXT ("预览装进了工程的参考组（")
                       + juce::String ((int) document.getProject().referenceGroup.dots.size())
                       + NI_TXT (" 个圆点）"));

            const auto previewImage = snapshot (component);
            check (writeImage (previewImage, outputDirectory.getChildFile ("44-reference-line-group-editor.png")),
                   NI_TXT ("参考线组编辑栏快照"));
            check (countColourInRect (previewImage, theme::accent,
                                      component.getLocalArea (&editor, editor.getLocalBounds()), 24) > 200,
                   NI_TXT ("编辑栏自己的框画了出来"));

            // Change a dot inside the editor: promote one of the ratio crosses.
            auto& editorLadder = editor.getLadder();
            const auto dotPoint = editorLadder.pointForPosition (0.25);
            editorLadder.mouseDown (makeMouseEvent (editorLadder, dotPoint, dotPoint));
            pump();

            check (editorLadder.getSelectedDotCount() == 1,
                   NI_TXT ("编辑栏的线段也能选圆点（选中 ")
                       + juce::String (editorLadder.getSelectedDotCount()) + NI_TXT (" 个）"));

            editorLadder.showRatioCrosses (3.0);
            pump();

            const auto editorCrosses = editorLadder.getCrossPositions();

            check (editorCrosses.size() == 2,
                   NI_TXT ("1:3 生成两个叉点（") + juce::String ((int) editorCrosses.size())
                       + NI_TXT (" 个）"));

            if (! editorCrosses.empty())
            {
                const auto crossPoint = editorLadder.pointForPosition (editorCrosses.front());
                editorLadder.mouseDown (makeMouseEvent (editorLadder, crossPoint, crossPoint));
                editorLadder.mouseUp (makeMouseEvent (editorLadder, crossPoint, crossPoint));
                pump();
            }

            check (document.getProject().referenceGroup.dots.size() == 2,
                   NI_TXT ("加了一个圆点，工程的参考组跟着变（现在 ")
                       + juce::String ((int) document.getProject().referenceGroup.dots.size())
                       + NI_TXT (" 个）"));
            check (document.getUndoDescription() == undoBeforeEditor,
                   NI_TXT ("预览里的改动也一样不进撤销栈"));

            editor.triggerBackForTest();
            pump();

            check (! editorPanel.isGroupEditorOpenForTest() && referencePanel.isVisible(),
                   NI_TXT ("返回后参考组栏又回来了"));

            const auto& restored = document.getProject().referenceGroup;

            check (restored.dots.size() == 2
                       && std::abs (restored.dots[0].position - 0.1) < 1.0e-6
                       && std::abs (restored.dots[1].position - 0.6) < 1.0e-6,
                   NI_TXT ("没保存的改动被丢掉了（现在 ")
                       + juce::String ((int) restored.dots.size()) + NI_TXT (" 个圆点）"));

            // --- 2. 保存 back into the preset ---
            presetPanel.triggerEditForTest (lineGroupIndex);
            pump();

            editorLadder.mouseDown (makeMouseEvent (editorLadder, dotPoint, dotPoint));
            editorLadder.showRatioCrosses (3.0);

            if (! editorLadder.getCrossPositions().empty())
            {
                const auto crossPoint = editorLadder.pointForPosition (editorLadder.getCrossPositions().front());
                editorLadder.mouseDown (makeMouseEvent (editorLadder, crossPoint, crossPoint));
                editorLadder.mouseUp (makeMouseEvent (editorLadder, crossPoint, crossPoint));
            }

            pump();
            editor.triggerSaveForTest();
            pump();

            check ((int) presets.getReferenceLineGroups()[(size_t) lineGroupIndex].dots.size() == 2,
                   NI_TXT ("保存写回了那条预设（现在 ")
                       + juce::String ((int) presets.getReferenceLineGroups()[(size_t) lineGroupIndex]
                                            .dots.size())
                       + NI_TXT (" 个圆点）"));
            check (presets.getFile().exists(), NI_TXT ("预设库文件写到盘上了"));
            check (! editorPanel.isGroupEditorOpenForTest(),
                   NI_TXT ("保存完编辑栏收起来了"));

            // --- 3. the 参考线组 status row's 编辑 ---
            {
                auto& optionsPanel = editorPanel.getOptionsPanel();
                auto rowIndex = -1;

                for (int i = 0; i < optionsPanel.getRowCount(); ++i)
                    if (optionsPanel.getRowLineId (i) == lineEntryId)
                        rowIndex = i;

                check (rowIndex >= 0, NI_TXT ("工程里的参考线组在状态栏里有一行"));

                if (rowIndex >= 0)
                {
                    check (optionsPanel.getRowButtonText (rowIndex, 3) == NI_TXT ("编辑"),
                           NI_TXT ("那一行的第三个按钮是「编辑」（实际是「")
                               + optionsPanel.getRowButtonText (rowIndex, 3) + NI_TXT ("」）"));
                    check (optionsPanel.isRowEditEnabled (rowIndex),
                           NI_TXT ("参考线组这一类的编辑按钮是亮的"));

                    optionsPanel.triggerRowButton (rowIndex, 3);
                    pump();

                    check (editorPanel.isGroupEditorOpenForTest()
                               && editor.getLadder().getLook().plainLine
                               && editor.getHeadingForTest().contains (NI_TXT ("工程的参考线组")),
                           NI_TXT ("状态栏的「编辑」也打开参考线组编辑栏（表头「")
                               + editor.getHeadingForTest() + NI_TXT ("」）"));
                }
            }

            // Save it into the project entry: one edit, one Ctrl+Z.
            editorLadder.mouseDown (makeMouseEvent (editorLadder, dotPoint, dotPoint));
            editorLadder.showRatioCrosses (3.0);

            if (! editorLadder.getCrossPositions().empty())
            {
                const auto crossPoint = editorLadder.pointForPosition (editorLadder.getCrossPositions().front());
                editorLadder.mouseDown (makeMouseEvent (editorLadder, crossPoint, crossPoint));
                editorLadder.mouseUp (makeMouseEvent (editorLadder, crossPoint, crossPoint));
            }

            pump();
            editor.triggerSaveForTest();
            pump();

            const auto entryPickedDots = [] (const Project& project, juce::int64 id)
            {
                const auto* entry = project.findReferenceLine (id);
                return entry != nullptr ? (int) entry->dots.size() : -1;
            };

            check (entryPickedDots (document.getProject(), lineEntryId) == 2,
                   NI_TXT ("保存写回了工程里那一条（现在 ")
                       + juce::String (entryPickedDots (document.getProject(), lineEntryId))
                       + NI_TXT (" 个圆点）"));
            check (document.getUndoDescription() == NI_TXT ("编辑参考线组"),
                   NI_TXT ("那一次保存留了一条撤销记录（「") + document.getUndoDescription()
                       + NI_TXT ("」）"));

            document.undo();
            pump();

            check (entryPickedDots (document.getProject(), lineEntryId) == 1,
                   NI_TXT ("一次 Ctrl+Z 就收回去了（现在 ")
                       + juce::String (entryPickedDots (document.getProject(), lineEntryId))
                       + NI_TXT (" 个圆点）"));

            // --- 4. the 参考组预设 row's 编辑, and 另存为 ---
            const auto savedGroupCount = (int) presets.getReferenceGroups().size();

            component.openGroupEditorForPreset (false, referenceGroupIndex);
            pump();

            check (editorPanel.isGroupEditorOpenForTest()
                       && ! editor.getLadder().getLook().plainLine
                       && ! editor.getLadder().getLook().frequencyLabels,
                   NI_TXT ("参考组编辑栏保留分框线、旁边写比率"));
            check (! editor.isAnchorRowVisibleForTest(), NI_TXT ("参考组没有锚点那一行"));
            check (editor.getHeadingForTest().contains (NI_TXT ("预设参考组")),
                   NI_TXT ("表头写着预设的名字（") + editor.getHeadingForTest() + NI_TXT ("）"));
            check (document.getProject().referenceGroup.dots.size() == 1
                       && std::abs (document.getProject().referenceGroup.dots.front().position - 0.5) < 1.0e-6,
                   NI_TXT ("预览是那条参考组预设的内容"));

            const auto groupEditorImage = snapshot (component);
            check (writeImage (groupEditorImage, outputDirectory.getChildFile ("45-reference-group-editor.png")),
                   NI_TXT ("参考组编辑栏快照"));

            editor.setNameForTest (NI_TXT ("另存的名字"));
            editor.triggerSaveAsForTest();
            pump();

            check ((int) presets.getReferenceGroups().size() == savedGroupCount + 1,
                   NI_TXT ("另存为在库里多了一条（")
                       + juce::String ((int) presets.getReferenceGroups().size() - savedGroupCount)
                       + NI_TXT (" 条）"));
            check (! presets.getReferenceGroups().empty()
                       && presets.getReferenceGroups().back().name == NI_TXT ("另存的名字"),
                   NI_TXT ("新的那条就用名字框里的名字（「")
                       + (presets.getReferenceGroups().empty()
                              ? juce::String()
                              : presets.getReferenceGroups().back().name)
                       + NI_TXT ("」）"));
            check (! editorPanel.isGroupEditorOpenForTest(), NI_TXT ("另存为之后编辑栏也收起来"));

            appState.toggleSidePanel (AppState::SidePanel::none);
            pump();
        }

        beginTest (NI_TXT ("八度线段：框选、1-5 快捷键、叉点旁的标签"));

        {
            auto& editorPanel = component.getEditorPanel();
            auto& panel = editorPanel.getReferenceGroupPanel();
            auto& ladder = panel.getLadder();

            document.applyEdit ("selftest ladder marquee setup", [] (Project& project)
            {
                project.referenceGroup = ReferenceGroup();
                project.referenceGroup.name = NI_TXT ("框选测试");
                music::addDotAt (project.referenceGroup, 0.5, 0);
                project.referenceLines.clear();
            });
            appState.setEditorOpen (true);
            appState.clearNotes();
            pump();

            check (ladder.getWidth() > 100 && ladder.getHeight() > 100,
                   NI_TXT ("线段有可用的大小（") + juce::String (ladder.getWidth()) + NI_TXT (" x ")
                       + juce::String (ladder.getHeight()) + NI_TXT ("）"));

            const auto seedPoint = ladder.pointForPosition (0.5);
            ladder.mouseDown (makeMouseEvent (ladder, seedPoint, seedPoint));
            pump();

            check (ladder.getSelectedDotCount() == 1, NI_TXT ("先选中中间那个圆点"));

            // 1-5 go to whichever column has the keyboard. With the focus on the
            // segment they are the segment's, not the note edit strip's.
            const auto ratioBeforeKeys = appState.currentRatio;
            const auto undoBeforeKeys = document.getUndoDescription();

            component.sendKeyPressForTest (juce::KeyPress ('2', juce::ModifierKeys(), 0), &ladder);
            pump();

            check (ladder.getCrossPositions().size() == 2,
                   NI_TXT ("焦点在参考组栏时按 2 生成两个 1:3 叉点（")
                       + juce::String ((int) ladder.getCrossPositions().size()) + NI_TXT (" 个）"));
            check (std::abs (appState.currentRatio - ratioBeforeKeys) < 1.0e-9,
                   NI_TXT ("这一个 2 没有跑去改音符的比率（还是 ")
                       + juce::String (appState.currentRatio, 3) + NI_TXT ("）"));
            check (document.getUndoDescription() == undoBeforeKeys,
                   NI_TXT ("按比率键只是预览，不进撤销栈"));

            // The caption beside a ratio cross is the one the canvas arrow uses.
            const auto crossPoint = ladder.pointForPosition (ladder.getCrossPositions().front());
            ladder.mouseMove (makeMouseEvent (ladder, crossPoint, crossPoint));
            pump();

            check (ladder.getHoverLabelForTest() == NI_TXT ("正 1:3")
                       || ladder.getHoverLabelForTest() == NI_TXT ("负 1:3"),
                   NI_TXT ("悬停在叉点旁时标签是「正/负 + 频率比」（实际是「")
                       + ladder.getHoverLabelForTest() + NI_TXT ("」）"));

            ladder.mouseExit (makeMouseEvent (ladder, crossPoint, crossPoint));
            pump();

            check (ladder.getHoverLabelForTest().isEmpty(),
                   NI_TXT ("鼠标离开后标签收起来"));

            // 框选：the band promotes every cross it touches and selects every dot.
            {
                const auto centreX = (float) ladder.getWidth() * 0.5f;
                const auto top = juce::Point<float> (centreX, 8.0f);
                const auto bottom = juce::Point<float> (centreX, (float) ladder.getHeight() - 8.0f);
                const auto dotsBefore = (int) document.getProject().referenceGroup.dots.size();

                ladder.mouseDown (makeMouseEvent (ladder, top, top));
                ladder.mouseDrag (makeMouseEvent (ladder, bottom, top));

                check (ladder.isMarqueeActiveForTest(), NI_TXT ("拖出了一条橡皮筋"));

                ladder.mouseUp (makeMouseEvent (ladder, bottom, top));
                pump();

                check ((int) document.getProject().referenceGroup.dots.size() == dotsBefore + 2,
                       NI_TXT ("框到的两个叉点一次全变成了圆点（")
                           + juce::String ((int) document.getProject().referenceGroup.dots.size()
                                           - dotsBefore)
                           + NI_TXT (" 个）"));
                check (ladder.getSelectedDotPositionsForTest().size() == 3,
                       NI_TXT ("框到的圆点也被选中了（")
                           + juce::String ((int) ladder.getSelectedDotPositionsForTest().size())
                           + NI_TXT (" 个）"));
                check (document.getUndoDescription() == NI_TXT ("建立圆点"),
                       NI_TXT ("一整框只留一条撤销记录（「") + document.getUndoDescription()
                           + NI_TXT ("」）"));

                auto& controls = panel.getControls();
                const auto& dots = document.getProject().referenceGroup.dots;

                // 颜色一次落在所有选中的圆点上。
                controls.triggerColourForTest (5);
                pump();

                auto everyDotRecoloured = ! dots.empty();

                for (const auto& dot : dots)
                    everyDotRecoloured = everyDotRecoloured && dot.colourIndex == 5;

                check (everyDotRecoloured,
                       NI_TXT ("颜色一次作用到了所有选中的圆点（") + juce::String ((int) dots.size())
                           + NI_TXT (" 个）"));

                // Three dots: a ratio lens has no single dot to stand on, so the
                // keys are grey and the status line says why.
                check (! controls.isRatioEnabledForTest(),
                       NI_TXT ("选中多个圆点时频率比是灰的"));
                check (controls.getStatusText().contains (NI_TXT ("一个圆点"))
                           || controls.getStatusText().contains (NI_TXT ("多个圆点")),
                       NI_TXT ("状态行写明了为什么灰（「") + controls.getStatusText() + NI_TXT ("」）"));

                // Two dots: division is on, the ratio lens is still off.
                const auto twoPoint = ladder.pointForPosition (dots[0].position);
                const auto otherPoint = ladder.pointForPosition (dots[1].position);

                ladder.mouseDown (makeMouseEvent (ladder, twoPoint, twoPoint));
                ladder.mouseDown (makeMouseEvent (ladder, otherPoint, otherPoint,
                                                  juce::ModifierKeys (juce::ModifierKeys::ctrlModifier)));
                pump();

                check (ladder.getSelectedDotCount() == 2, NI_TXT ("选中两个圆点（实际 ")
                                                                    + juce::String (ladder.getSelectedDotCount())
                                                                    + NI_TXT (" 个）"));
                check (controls.isDivisionEnabledForTest(), NI_TXT ("两个圆点时「等分」可用"));
                check (! controls.isRatioEnabledForTest(), NI_TXT ("两个圆点时频率比仍然灰着"));

                // One dot: the ratio keys come back, division goes grey.
                ladder.mouseDown (makeMouseEvent (ladder, twoPoint, twoPoint));
                pump();

                check (ladder.getSelectedDotCount() == 1 && controls.isRatioEnabledForTest(),
                       NI_TXT ("选中一个圆点后比率键又能用了"));
                check (! controls.isDivisionEnabledForTest(), NI_TXT ("一个圆点时「等分」灰掉"));
            }

            // 画布拿回焦点：同一个 2 又回到音符编辑栏那条路（它需要正好一个音符）。
            {
                juce::int64 lensNoteId = 0;

                document.applyEdit ("selftest lens note", [&lensNoteId, trackId] (Project& project)
                {
                    if (auto* track = project.findTrack (trackId))
                    {
                        track->clips.front().notes.clear();

                        Note note;
                        note.id = project.nextNoteId++;
                        note.startTick = 0;
                        note.durationTicks = kTicksPerQuarter;
                        note.frequencyHz = 440.0;
                        note.velocity = 100;
                        lensNoteId = note.id;
                        track->clips.front().notes.push_back (note);
                    }
                });

                appState.selectTrack (trackId);
                appState.selectClip (firstClipId());
                appState.selectNote (lensNoteId, false);
                pump();

                auto& canvas = editorPanel.getGrid();

                component.sendKeyPressForTest (juce::KeyPress ('2', juce::ModifierKeys(), 0), &canvas);
                pump();

                check (std::abs (appState.currentRatio - 3.0) < 1.0e-9,
                       NI_TXT ("焦点回到画布后按 2 又走音符编辑栏那条（现在是 ")
                           + juce::String (appState.currentRatio, 3) + NI_TXT ("）"));
            }
        }

        beginTest (NI_TXT ("选中多个音符或圆点时，右键一次把它们都删掉"));

        {
            const juce::ModifierKeys rightButton (juce::ModifierKeys::rightButtonModifier);
            const juce::ModifierKeys ctrlKey (juce::ModifierKeys::ctrlModifier);
            juce::int64 deleteNoteIds[3] = { 0, 0, 0 };
            const double noteHz[3] = { 220.0, 440.0, 880.0 };

            document.applyEdit ("selftest note delete setup", [&deleteNoteIds, &noteHz, trackId]
                                (Project& project)
            {
                if (auto* track = project.findTrack (trackId))
                {
                    track->clips.front().notes.clear();

                    for (int i = 0; i < 3; ++i)
                    {
                        Note note;
                        note.id = project.nextNoteId++;
                        note.startTick = (juce::int64) i * 4 * kTicksPerQuarter;
                        note.durationTicks = kTicksPerQuarter;
                        note.frequencyHz = noteHz[i];
                        note.velocity = 100;
                        deleteNoteIds[i] = note.id;
                        track->clips.front().notes.push_back (note);
                    }

                    project.view.autoFitView = false;
                    project.view.centreHz = 440.0;
                    project.view.octavesVisible = 8.0;
                    project.view.pixelsPerQuarter = 140.0;
                    project.view.horizontalOffsetTicks = 0.0;
                }
            });

            appState.setEditorOpen (true);
            appState.selectTrack (trackId);
            appState.selectClip (firstClipId());
            appState.clearNotes();
            pump();

            const auto pointFor = [&grid, &noteHz] (int index)
            {
                return grid.pointFor (noteHz[index],
                                      (juce::int64) index * 4 * kTicksPerQuarter);
            };

            // Select two of the three, then right click one of the selected ones.
            appState.selectNote (deleteNoteIds[0], false);
            appState.selectNote (deleteNoteIds[1], true);
            pump();

            check (appState.selectedNoteIds.size() == 2,
                   NI_TXT ("先选中两个音符（实际 ")
                       + juce::String ((int) appState.selectedNoteIds.size()) + NI_TXT (" 个）"));

            const auto onSelected = pointFor (1);
            grid.mouseDown (makeMouseEvent (grid, onSelected, onSelected, rightButton));
            grid.mouseUp (makeMouseEvent (grid, onSelected, onSelected, rightButton));
            pump();

            check (document.findNote (trackId, deleteNoteIds[0]) == nullptr
                       && document.findNote (trackId, deleteNoteIds[1]) == nullptr,
                   NI_TXT ("右键点在选中的音符上，两个一起删掉"));
            check (document.findNote (trackId, deleteNoteIds[2]) != nullptr,
                   NI_TXT ("没选中的那个还在"));
            check (document.getUndoDescription() == NI_TXT ("删除音符"),
                   NI_TXT ("整组删除只留一条撤销记录（「") + document.getUndoDescription()
                       + NI_TXT ("」）"));

            document.undo();
            pump();

            check (document.findNote (trackId, deleteNoteIds[0]) != nullptr
                       && document.findNote (trackId, deleteNoteIds[1]) != nullptr,
                   NI_TXT ("一次 Ctrl+Z 把两个音符都放回来"));

            // A note outside the selection still goes on its own - the multi delete
            // is what the selection says, not what the pointer says.
            appState.selectNote (deleteNoteIds[0], false);
            pump();

            const auto onUnselected = pointFor (1);
            grid.mouseDown (makeMouseEvent (grid, onUnselected, onUnselected, rightButton));
            grid.mouseUp (makeMouseEvent (grid, onUnselected, onUnselected, rightButton));
            pump();

            check (document.findNote (trackId, deleteNoteIds[1]) == nullptr
                       && document.findNote (trackId, deleteNoteIds[2]) != nullptr,
                   NI_TXT ("右键点没选中的音符时只删它自己"));

            // ---- the same rule on the octave segment's dots -------------------
            auto& panel = component.getEditorPanel().getReferenceGroupPanel();
            auto& ladder = panel.getLadder();

            document.applyEdit ("selftest dot delete setup", [] (Project& project)
            {
                project.referenceGroup = ReferenceGroup();
                project.referenceGroup.name = NI_TXT ("删除测试");
                music::addDotAt (project.referenceGroup, 0.2, 0);
                music::addDotAt (project.referenceGroup, 0.5, 1);
                music::addDotAt (project.referenceGroup, 0.8, 2);
            });
            appState.clearNotes();
            pump();

            const auto dotCount = [&document] ()
            {
                return (int) document.getProject().referenceGroup.dots.size();
            };

            check (dotCount() == 3, NI_TXT ("线段上先有三个圆点（实际 ")
                                         + juce::String (dotCount()) + NI_TXT (" 个）"));

            const auto dotPoint = [&ladder] (double position)
            {
                return ladder.pointForPosition (position);
            };

            ladder.mouseDown (makeMouseEvent (ladder, dotPoint (0.2), dotPoint (0.2)));
            ladder.mouseDown (makeMouseEvent (ladder, dotPoint (0.5), dotPoint (0.5), ctrlKey));
            pump();

            check (ladder.getSelectedDotCount() == 2,
                   NI_TXT ("两个圆点被选中（实际 ")
                       + juce::String (ladder.getSelectedDotCount()) + NI_TXT (" 个）"));

            ladder.mouseDown (makeMouseEvent (ladder, dotPoint (0.5), dotPoint (0.5), rightButton));
            ladder.mouseUp (makeMouseEvent (ladder, dotPoint (0.5), dotPoint (0.5), rightButton));
            pump();

            check (dotCount() == 1
                       && ! document.getProject().referenceGroup.dots.empty()
                       && std::abs (document.getProject().referenceGroup.dots.front().position - 0.8)
                              < 1.0e-6,
                   NI_TXT ("右键点在选中的圆点上，两个一起删（现在剩 ")
                       + juce::String (dotCount()) + NI_TXT (" 个）"));
            check (document.getUndoDescription() == NI_TXT ("删除圆点"),
                   NI_TXT ("整组删除只留一条撤销记录（「") + document.getUndoDescription()
                       + NI_TXT ("」）"));

            document.undo();
            pump();

            check (dotCount() == 3, NI_TXT ("一次 Ctrl+Z 把两个圆点都放回来（现在 ")
                                        + juce::String (dotCount()) + NI_TXT (" 个）"));

            // Only one selected: that one goes on its own, as before.
            ladder.mouseDown (makeMouseEvent (ladder, dotPoint (0.2), dotPoint (0.2)));
            pump();

            ladder.mouseDown (makeMouseEvent (ladder, dotPoint (0.2), dotPoint (0.2), rightButton));
            ladder.mouseUp (makeMouseEvent (ladder, dotPoint (0.2), dotPoint (0.2), rightButton));
            pump();

            check (dotCount() == 2, NI_TXT ("只选中一个圆点时右键只删它自己（现在剩 ")
                                        + juce::String (dotCount()) + NI_TXT (" 个）"));
        }

        beginTest (NI_TXT ("从圆点或叉点旁边也能拖出框选（第三十九批追加）"));

        {
            auto& panel = component.getEditorPanel().getReferenceGroupPanel();
            auto& ladder = panel.getLadder();

            const auto dotPositions = [&document] ()
            {
                std::vector<double> positions;

                for (const auto& dot : document.getProject().referenceGroup.dots)
                    positions.push_back (dot.position);

                return positions;
            };

            document.applyEdit ("selftest band beside dot setup", [] (Project& project)
            {
                project.referenceGroup = ReferenceGroup();
                project.referenceGroup.name = NI_TXT ("旁边也能拖");
                music::addDotAt (project.referenceGroup, 0.2, 0);
                music::addDotAt (project.referenceGroup, 0.5, 1);
                music::addDotAt (project.referenceGroup, 0.8, 2);
                project.referenceLines.clear();
            });
            appState.clearNotes();
            pump();

            // ---- 1. pressing right beside a dot, then dragging ----------------
            const auto dotPoint = ladder.pointForPosition (0.5);
            // 4 px off the dot's line: still inside the 7 px hit radius it selects
            // with, which is exactly the spot that used to swallow the gesture.
            const auto besideDot = juce::Point<float> (dotPoint.x, dotPoint.y + 4.0f);
            const auto lowEnd = juce::Point<float> (dotPoint.x, (float) ladder.getHeight() - 10.0f);

            ladder.mouseDown (makeMouseEvent (ladder, besideDot, besideDot));
            pump();

            check (ladder.getSelectedDotCount() == 1,
                   NI_TXT ("按在圆点旁边（命中半径内）先选中它（选中 ")
                       + juce::String (ladder.getSelectedDotCount()) + NI_TXT (" 个）"));

            ladder.mouseDrag (makeMouseEvent (ladder, lowEnd, besideDot));
            ladder.mouseUp (makeMouseEvent (ladder, lowEnd, besideDot));
            pump();

            const auto bandSelection = ladder.getSelectedDotPositionsForTest();
            const auto inBand = [&bandSelection] (double position)
            {
                return std::find (bandSelection.begin(), bandSelection.end(), position)
                       != bandSelection.end();
            };

            check (ladder.getSelectedDotCount() == 2 && inBand (0.5) && inBand (0.2),
                   NI_TXT ("从它旁边拖出去照样框到了下面的圆点（选中 ")
                       + juce::String (ladder.getSelectedDotCount()) + NI_TXT (" 个）"));

            // ---- 2. pressing on a cross, then dragging ------------------------
            document.applyEdit ("selftest band beside cross setup", [] (Project& project)
            {
                project.referenceGroup = ReferenceGroup();
                project.referenceGroup.name = NI_TXT ("叉点旁边");
                music::addDotAt (project.referenceGroup, 0.5, 0);
            });
            appState.clearNotes();
            pump();

            // Clear whatever was selected before the group was replaced.
            const auto emptyPoint = ladder.pointForPosition (0.3);
            ladder.mouseDown (makeMouseEvent (ladder, emptyPoint, emptyPoint));
            ladder.mouseUp (makeMouseEvent (ladder, emptyPoint, emptyPoint));
            pump();

            const auto seedPoint = ladder.pointForPosition (0.5);
            ladder.mouseDown (makeMouseEvent (ladder, seedPoint, seedPoint));
            ladder.mouseUp (makeMouseEvent (ladder, seedPoint, seedPoint));
            ladder.showRatioCrosses (3.0);
            pump();

            const auto crosses = ladder.getCrossPositions();

            check (crosses.size() == 2, NI_TXT ("先有两个 1:3 叉点（")
                                           + juce::String ((int) crosses.size()) + NI_TXT (" 个）"));

            if (! crosses.empty())
            {
                // The lower of the two crosses: dragging up from it sweeps the dot.
                const auto lower = *std::min_element (crosses.begin(), crosses.end());
                const auto crossPoint = ladder.pointForPosition (lower);
                const auto crossEnd = juce::Point<float> (crossPoint.x,
                                                          (float) ladder.getHeight() * 0.4f);

                ladder.mouseDown (makeMouseEvent (ladder, crossPoint, crossPoint));
                pump();

                check (dotPositions().size() == 1,
                       NI_TXT ("按在叉点上、还没松手时它不会变成圆点（现在 ")
                           + juce::String ((int) dotPositions().size()) + NI_TXT (" 个圆点）"));

                ladder.mouseDrag (makeMouseEvent (ladder, crossEnd, crossPoint));
                ladder.mouseUp (makeMouseEvent (ladder, crossEnd, crossPoint));
                pump();

                check (dotPositions().size() == 2,
                       NI_TXT ("拖出去以后，框到的叉点才变成圆点（现在 ")
                           + juce::String ((int) dotPositions().size()) + NI_TXT (" 个圆点）"));
            }
        }

        beginTest ("marquee selection picks up the notes it touches");

        {
            juce::int64 marqueeNoteIds[3] = { 0, 0, 0 };

            document.applyEdit ("selftest marquee setup", [&marqueeNoteIds, trackId] (Project& project)
            {
                auto* track = project.findTrack (trackId);

                if (track == nullptr)
                    return;

                track->clips.front().notes.clear();

                const double frequencies[3] = { 110.0, 440.0, 1760.0 };

                for (int i = 0; i < 3; ++i)
                {
                    Note note;
                    note.id = project.nextNoteId++;
                    note.startTick = (juce::int64) i * 2 * kTicksPerQuarter;
                    note.durationTicks = kTicksPerQuarter;
                    note.frequencyHz = frequencies[i];
                    note.velocity = 100;
                    marqueeNoteIds[i] = note.id;
                    track->clips.front().notes.push_back (note);
                }

                project.referenceGroup.dots.clear();
                project.referenceLines.clear();
                project.view.autoFitView = false;
                project.view.centreHz = 440.0;
                project.view.octavesVisible = 8.0;
                project.view.pixelsPerQuarter = 140.0;
                project.view.horizontalOffsetTicks = 0.0;
            });

            pump();

            appState.selectTrack (trackId); appState.selectClip (firstClipId());
            appState.clearNotes();
            pump();

            auto& marqueeGrid = component.getEditorPanel().getGrid();

            // A band spanning everything from 3000 Hz down to 50 Hz, over the first
            // two beats: it swallows the first note and only clips the second.
            const auto bandStart = marqueeGrid.pointFor (3000.0, 0);
            const auto bandEnd = marqueeGrid.pointFor (50.0, 2000);

            marqueeGrid.mouseDown (makeMouseEvent (marqueeGrid, bandStart, bandStart));
            marqueeGrid.mouseDrag (makeMouseEvent (marqueeGrid, bandEnd, bandStart));
            marqueeGrid.mouseUp (makeMouseEvent (marqueeGrid, bandEnd, bandStart));
            pump();

            check (appState.selectedNoteIds.size() == 2,
                   "the band selected two notes (got "
                       + juce::String ((int) appState.selectedNoteIds.size()) + ")");
            check (appState.isNoteSelected (marqueeNoteIds[0])
                       && appState.isNoteSelected (marqueeNoteIds[1]),
                   "both touched notes are selected");
            check (! appState.isNoteSelected (marqueeNoteIds[2]),
                   "the note outside the band is left alone");

            beginTest ("Ctrl+marquee adds to the existing selection");

            appState.setSelectedNotes ({ marqueeNoteIds[1] });
            pump();

            // Note 1 is at 440 Hz, so its octave family sits every 17 px or so in
            // this canvas - and pressing *on* a reference line is a different
            // gesture (it writes a note). The band's corner is therefore put half an
            // octave away from the nearest line, which is the furthest a frequency
            // can be from one however tall the canvas is.
            const auto addStart = marqueeGrid.pointFor (440.0 * std::sqrt (32.0), 3300);
            const auto addEnd = marqueeGrid.pointFor (50.0, 4000);

            marqueeGrid.mouseDown (makeMouseEvent (marqueeGrid, addStart, addStart, ctrlModifier));
            marqueeGrid.mouseDrag (makeMouseEvent (marqueeGrid, addEnd, addStart, ctrlModifier));
            marqueeGrid.mouseUp (makeMouseEvent (marqueeGrid, addEnd, addStart, ctrlModifier));
            pump();

            check (appState.selectedNoteIds.size() == 2
                       && appState.isNoteSelected (marqueeNoteIds[1])
                       && appState.isNoteSelected (marqueeNoteIds[2]),
                   "the band was unioned into the existing selection (got "
                       + juce::String ((int) appState.selectedNoteIds.size())
                       + ", grid " + juce::String (marqueeGrid.getHeight())
                       + " px, down " + addStart.toString() + " up " + addEnd.toString()
                       + ", n2rect " + marqueeGrid.getNoteRectForTest (marqueeNoteIds[2]).toString()
                       + ")");

            beginTest ("a band that touches nothing clears the selection");

            appState.setSelectedNotes ({ marqueeNoteIds[0] });
            pump();

            // Same rule, and the band still touches nothing: note 0 is at 110 Hz
            // (far below it) and the other two are to the right of it.
            const auto emptyStart = marqueeGrid.pointFor (110.0 * std::sqrt (2048.0), 0);
            const auto emptyEnd = marqueeGrid.pointFor (110.0 * std::sqrt (8.0), 1000);

            marqueeGrid.mouseDown (makeMouseEvent (marqueeGrid, emptyStart, emptyStart));
            marqueeGrid.mouseDrag (makeMouseEvent (marqueeGrid, emptyEnd, emptyStart));
            marqueeGrid.mouseUp (makeMouseEvent (marqueeGrid, emptyEnd, emptyStart));
            pump();

            check (appState.selectedNoteIds.empty(),
                   "the selection was cleared (got "
                       + juce::String ((int) appState.selectedNoteIds.size())
                       + ", down " + emptyStart.toString() + " up " + emptyEnd.toString()
                       + ", grid " + juce::String (marqueeGrid.getHeight()) + " px)");

            beginTest ("pressing on a reference line still writes a note");

            document.applyEdit ("selftest manual line", [] (Project& project)
            {
                auto line = music::makeFixedHzEntry (project.nextReferenceLineId++, 440.0, juce::String());
                project.referenceLines.push_back (std::move (line));
            });
            pump();

            const auto before = [&document, trackId]
            {
                const auto* track = document.findTrack (trackId);
                return track != nullptr ? (int) track->clips.front().notes.size() : 0;
            }();

            const auto linePoint = marqueeGrid.pointFor (440.0, 4500);

            marqueeGrid.mouseDown (makeMouseEvent (marqueeGrid, linePoint, linePoint));
            marqueeGrid.mouseDrag (makeMouseEvent (marqueeGrid, linePoint, linePoint));
            marqueeGrid.mouseUp (makeMouseEvent (marqueeGrid, linePoint, linePoint));
            pump();

            const auto after = [&document, trackId]
            {
                const auto* track = document.findTrack (trackId);
                return track != nullptr ? (int) track->clips.front().notes.size() : 0;
            }();

            check (after == before + 1,
                   "a note was written instead of starting a band (" + juce::String (before)
                       + " -> " + juce::String (after) + ")");

            beginTest ("a vertical drag from a reference line box-selects instead");

            {
                const auto countBefore = [&document, trackId]
                {
                    const auto* track = document.findTrack (trackId);
                    return track != nullptr ? (int) track->clips.front().notes.size() : 0;
                }();

                // Press on the 440 Hz line, then move mostly upwards: the band wins
                // and nothing is written.
                const auto lineDragFrom = marqueeGrid.pointFor (440.0, 1920);
                const auto lineDragTo   = marqueeGrid.pointFor (2000.0, 2050);

                marqueeGrid.mouseDown (makeMouseEvent (marqueeGrid, lineDragFrom, lineDragFrom));
                marqueeGrid.mouseDrag (makeMouseEvent (marqueeGrid, lineDragTo, lineDragFrom));
                marqueeGrid.mouseUp (makeMouseEvent (marqueeGrid, lineDragTo, lineDragFrom));
                pump();

                const auto countAfter = [&document, trackId]
                {
                    const auto* track = document.findTrack (trackId);
                    return track != nullptr ? (int) track->clips.front().notes.size() : 0;
                }();

                check (countAfter == countBefore,
                       "a vertical drag from a line wrote no note ("
                           + juce::String (countBefore) + " -> " + juce::String (countAfter) + ")");
                check (appState.isNoteSelected (marqueeNoteIds[1]),
                       "and it rubber-banded the note it covered");
            }
        }

        // ---- track lane: clips, gestures and copy / paste --------------------

        auto& lanes = component.getTrackArea();

        const auto laneY = [] (int index)
        {
            return TrackArea::topBarHeight + TrackArea::markerHeight + TrackArea::rulerHeight
                   + index * TrackArea::laneHeight + TrackArea::laneHeight / 2;
        };

        const auto laneX = [&lanes] (double tick)
        {
            return TrackArea::headerWidth + (int) lanes.xForTick (tick);
        };

        const juce::ModifierKeys ctrlForKeys (juce::ModifierKeys::ctrlModifier);
        const auto pressKey = [&component, ctrlForKeys] (juce::juce_wchar character)
        {
            component.keyPressed (juce::KeyPress (character, ctrlForKeys, 0));
        };

        juce::int64 laneTrackId = 0;

        beginTest ("a long press on an empty lane creates a clip");

        {
            document.applyEdit ("selftest lane setup", [&laneTrackId] (Project& project)
            {
                project.tracks.resize (1);
                auto& track = project.tracks.front();
                track.clips.clear();
                laneTrackId = track.id;

                project.view.pixelsPerQuarter = 140.0;
                project.view.horizontalOffsetTicks = 0.0;
                project.view.trackSnap.denominator = 1;
                project.view.editorSnap.denominator = 1;
                project.view.controllerSnap.denominator = 1;
            });
            pump();

            appState.selectTrack (laneTrackId);
            pump();

            const auto start = juce::Point<float> ((float) (laneX (0.0) + 20), (float) laneY (0));
            lanes.mouseDown (makeMouseEvent (lanes, start, start));

            check (lanes.isLongPressPendingForTest(), "pressing empty lane arms the long press");

            // Let the 300 ms long-press timer fire.
            pumpFor (450);

            // The timer callback advances the mode, so after the hold this is no
            // longer a pending press but an armed clip creation.
            check (lanes.isCreatingClipForTest(),
                   "holding still long enough arms clip creation");

            const auto finish = juce::Point<float> ((float) laneX (2880.0), (float) laneY (0));
            lanes.mouseDrag (makeMouseEvent (lanes, finish, start));

            check (lanes.isCreatingClipForTest(),
                   "dragging after the hold starts creating a clip");

            lanes.mouseUp (makeMouseEvent (lanes, finish, start));
            pump();

            const auto* track = document.findTrack (laneTrackId);
            const auto count = track != nullptr ? (int) track->clips.size() : -1;
            check (count == 1, "one clip was created (got " + juce::String (count) + ")");

            if (track != nullptr && ! track->clips.empty())
            {
                const auto& clip = track->clips.front();
                check (clip.startTick == 0 && clip.lengthTicks == 2880,
                       "the clip covers ticks 0 to 2880 (got " + juce::String (clip.startTick)
                           + " + " + juce::String (clip.lengthTicks) + ")");
            }

            beginTest ("a short press does not create a clip");

            const auto quickStart = juce::Point<float> ((float) laneX (9600.0), (float) laneY (0));
            const auto quickEnd = juce::Point<float> ((float) laneX (12000.0), (float) laneY (0));
            lanes.mouseDown (makeMouseEvent (lanes, quickStart, quickStart));
            lanes.mouseDrag (makeMouseEvent (lanes, quickEnd, quickStart));
            lanes.mouseUp (makeMouseEvent (lanes, quickEnd, quickStart));
            pump();

            check (document.findTrack (laneTrackId)->clips.size() == 1,
                   "dragging without holding still did not create anything");
        }

        beginTest ("dragging a clip moves it");

        {
            const auto body = juce::Point<float> ((float) laneX (960.0), (float) laneY (0));
            const auto target = juce::Point<float> ((float) (laneX (960.0) + 280), (float) laneY (0));

            lanes.mouseDown (makeMouseEvent (lanes, body, body));
            lanes.mouseDrag (makeMouseEvent (lanes, target, body));
            lanes.mouseUp (makeMouseEvent (lanes, target, body));
            pump();

            const auto* track = document.findTrack (laneTrackId);
            const auto start = track != nullptr && ! track->clips.empty() ? track->clips.front().startTick : -1;

            check (start == 1920, "the clip moved two beats later (got tick "
                                      + juce::String (start) + ")");
        }

        juce::int64 secondTrackId = 0;

        beginTest ("a clip can be dragged onto another track");

        {
            document.applyEdit ("selftest second track", [&secondTrackId] (Project& project)
            {
                InstrumentTrack track;
                track.id = project.nextTrackId++;
                track.name = NI_TXT ("乐器 2");
                track.colourIndex = 1;

                auto clip = makeDefaultClip (project);
                clip.startTick = 19200;
                track.clips.push_back (std::move (clip));

                secondTrackId = track.id;
                project.tracks.push_back (std::move (track));
            });
            pump();

            const auto body = juce::Point<float> ((float) laneX (2400.0), (float) laneY (0));
            const auto target = juce::Point<float> ((float) laneX (2400.0), (float) laneY (1));

            lanes.mouseDown (makeMouseEvent (lanes, body, body));
            lanes.mouseDrag (makeMouseEvent (lanes, target, body));
            lanes.mouseUp (makeMouseEvent (lanes, target, body));
            pump();

            const auto* first = document.findTrack (laneTrackId);
            const auto* second = document.findTrack (secondTrackId);

            check (first != nullptr && first->clips.empty(), "the clip left its original track");

            const auto arrived = second != nullptr ? (int) second->clips.size() : -1;
            check (arrived == 2, "the clip arrived on the second track (got "
                                     + juce::String (arrived) + ")");
        }

        beginTest ("the lower right corner resizes, the upper right edge loops");

        {
            // The moved clip now lives on the second track spanning 1920..4800.
            const auto rightEdge = laneX (4800.0);
            const auto laneTop = laneY (1) - TrackArea::laneHeight / 2 + 6;

            const auto corner = juce::Point<float> ((float) (rightEdge - 2), (float) (laneTop + 64));
            const auto cornerTarget = juce::Point<float> ((float) (rightEdge - 2 + 140), (float) (laneTop + 64));

            lanes.mouseDown (makeMouseEvent (lanes, corner, corner));
            lanes.mouseDrag (makeMouseEvent (lanes, cornerTarget, corner));
            lanes.mouseUp (makeMouseEvent (lanes, cornerTarget, corner));
            pump();

            const auto lengthAfterResize = [&document, secondTrackId]
            {
                const auto* track = document.findTrack (secondTrackId);

                for (const auto& clip : track->clips)
                    if (clip.startTick == 1920)
                        return (int) clip.lengthTicks;

                return -1;
            }();

            check (lengthAfterResize == 3840, "resizing set the length to 3840 (got "
                                                  + juce::String (lengthAfterResize) + ")");

            const auto loopEdge = laneX (5760.0);
            const auto loopGrab = juce::Point<float> ((float) (loopEdge - 2), (float) (laneTop + 14));
            const auto loopTarget = juce::Point<float> ((float) (loopEdge - 2 + 140), (float) (laneTop + 14));

            lanes.mouseDown (makeMouseEvent (lanes, loopGrab, loopGrab));
            lanes.mouseDrag (makeMouseEvent (lanes, loopTarget, loopGrab));
            lanes.mouseUp (makeMouseEvent (lanes, loopTarget, loopGrab));
            pump();

            const auto* track = document.findTrack (secondTrackId);
            bool looped = false;
            juce::int64 length = -1;

            for (const auto& clip : track->clips)
                if (clip.startTick == 1920)
                {
                    looped = clip.looped;
                    length = clip.lengthTicks;
                }

            check (looped, "dragging the upper right edge turned on looping");
            check (length == 4800, "the looped clip is 4800 ticks long (got "
                                       + juce::String (length) + ")");
        }

        beginTest ("copying and pasting notes lands them at the playhead");

        {
            juce::int64 noteClipId = 0;
            juce::int64 copiedA = 0, copiedB = 0;

            document.applyEdit ("selftest paste notes", [&noteClipId, &copiedA, &copiedB] (Project& project)
            {
                auto& track = project.tracks.front();
                track.clips.clear();

                auto clip = makeDefaultClip (project);
                clip.startTick = 0;
                clip.lengthTicks = 960;          // short: pasting has to grow it
                clip.loopLengthTicks = 960;
                noteClipId = clip.id;

                Note a;
                a.id = project.nextNoteId++;
                a.startTick = 0;
                a.durationTicks = 240;
                a.frequencyHz = 220.0;

                Note b;
                b.id = project.nextNoteId++;
                b.startTick = 480;
                b.durationTicks = 240;
                b.frequencyHz = 330.0;

                copiedA = a.id;
                copiedB = b.id;
                clip.notes = { a, b };
                track.clips.push_back (std::move (clip));
            });
            pump();

            appState.selectTrack (document.getProject().tracks.front().id);
            appState.selectClip (noteClipId);
            pump();

            appState.setSelectedNotes ({ copiedA, copiedB });
            pump();

            pressKey ('C');

            // 1.0 s at 120 bpm is tick 1920.
            component.getAudioEngine().setPositionSeconds (1.0);
            pump();

            pressKey ('V');
            pump();

            const auto* clip = document.getProject().findClip (noteClipId);
            const auto count = clip != nullptr ? (int) clip->notes.size() : -1;

            check (count == 4, "two notes were pasted (got " + juce::String (count) + ")");

            if (clip != nullptr && clip->notes.size() == 4)
            {
                const auto pastedA = clip->notes[2].startTick;
                const auto pastedB = clip->notes[3].startTick;

                check (pastedA == 1920 && pastedB == 2400,
                       "the pasted notes start at the playhead (got " + juce::String (pastedA)
                           + ", " + juce::String (pastedB) + ")");
                // The second note ends at 480 + 240 = 720, so 720 + 1920 = 2640.
                check (clip->lengthTicks == 2640,
                       "the clip grew to hold the paste (got " + juce::String (clip->lengthTicks) + ")");
            }

            beginTest ("copying and pasting a clip lands it on the selected track");

            appState.clearNotes();
            appState.selectClip (noteClipId);
            pump();

            pressKey ('C');

            component.getAudioEngine().setPositionSeconds (3.0);   // tick 5760
            pump();

            const auto before = (int) document.getProject().tracks.front().clips.size();
            appState.selectTrack (document.getProject().tracks.front().id);
            pump();

            pressKey ('V');
            pump();

            const auto& clips = document.getProject().tracks.front().clips;
            check ((int) clips.size() == before + 1,
                   "the clip was pasted (got " + juce::String ((int) clips.size()) + ")");

            bool overlap = false;

            for (int i = 0; i < (int) clips.size(); ++i)
                for (int j = i + 1; j < (int) clips.size(); ++j)
                    if (clips[(size_t) i].startTick < clips[(size_t) j].endTick()
                        && clips[(size_t) j].startTick < clips[(size_t) i].endTick())
                        overlap = true;

            check (! overlap, "the pasted clip does not overlap the existing one");
        }

        beginTest ("repeating a clip copies it back to back");

        {
            juce::int64 repeatSourceId = 0;

            document.applyEdit ("selftest repeat setup", [&repeatSourceId] (Project& project)
            {
                auto& track = project.tracks.front();
                track.clips.clear();

                auto clip = makeDefaultClip (project);
                clip.startTick = 0;
                clip.lengthTicks = 1920;
                clip.loopLengthTicks = 1920;
                repeatSourceId = clip.id;

                Note n;
                n.id = project.nextNoteId++;
                n.startTick = 0;
                n.durationTicks = 480;
                n.frequencyHz = 220.0;
                clip.notes = { n };

                track.clips.push_back (std::move (clip));
            });
            pump();

            appState.selectTrack (document.getProject().tracks.front().id);
            appState.selectClip (repeatSourceId);
            pump();

            // Ctrl+D is "duplicate once"; the options panel button asks for N and
            // runs the same action.
            pressKey ('D');
            pump();

            const auto afterShortcut = (int) document.getProject().tracks.front().clips.size();
            check (afterShortcut == 2,
                   "Ctrl+D added exactly one copy (got " + juce::String (afterShortcut)
                       + ", selected clip " + juce::String (appState.selectedClipId) + ")");

            component.repeatSelectedClip (2);
            pump();

            const auto clipCount = (int) document.getProject().tracks.front().clips.size();
            check (clipCount == 4, "one shortcut copy plus two more (got " + juce::String (clipCount) + ")");

            if (clipCount == 4)
            {
                const auto& clips = document.getProject().tracks.front().clips;

                check (clips[1].startTick == 1920 && clips[2].startTick == 3840
                           && clips[3].startTick == 5760,
                       "the copies sit back to back (got " + juce::String (clips[1].startTick)
                           + ", " + juce::String (clips[2].startTick)
                           + ", " + juce::String (clips[3].startTick) + ")");

                check (clips[3].notes.size() == 1 && clips[3].notes.front().startTick == 0
                           && clips[3].notes.front().frequencyHz == 220.0,
                       "the copy carries the source's notes");

                bool overlap = false;

                for (int i = 0; i < clipCount; ++i)
                    for (int j = i + 1; j < clipCount; ++j)
                        if (clips[(size_t) i].startTick < clips[(size_t) j].endTick()
                            && clips[(size_t) j].startTick < clips[(size_t) i].endTick())
                            overlap = true;

                check (! overlap, "the repeats do not overlap");
            }

            // The two copies made by repeatSelectedClip were a single edit.
            document.undo();
            pump();

            check ((int) document.getProject().tracks.front().clips.size() == afterShortcut,
                   "one undo removed the whole repeat batch");
        }

        beginTest ("right clicking a clip opens a menu instead of deleting it");

        {
            juce::int64 menuClipId = 0;

            document.applyEdit ("selftest clip menu setup", [&menuClipId] (Project& project)
            {
                auto& track = project.tracks.front();
                track.clips.clear();

                auto clip = makeDefaultClip (project);
                clip.startTick = 0;
                clip.lengthTicks = 3840;
                clip.loopLengthTicks = 3840;
                menuClipId = clip.id;

                Note n;
                n.id = project.nextNoteId++;
                n.startTick = 0;
                n.durationTicks = 3840;
                n.frequencyHz = 220.0;
                clip.notes = { n };

                track.clips.push_back (std::move (clip));
            });

            appState.selectTrack (document.getProject().tracks.front().id);
            appState.selectClip (menuClipId);
            lanes.refresh();
            pump();

            const auto clipCountBefore = (int) document.getProject().tracks.front().clips.size();
            const auto clipCentre = lanes.clipBoundsForTest (menuClipId).getCentre().toFloat();
            const auto rightClick = makeMouseEvent (lanes, clipCentre, clipCentre,
                                                    juce::ModifierKeys (juce::ModifierKeys::rightButtonModifier));

            lanes.mouseDown (rightClick);
            pump();

            check ((int) document.getProject().tracks.front().clips.size() == clipCountBefore,
                   "a right click no longer deletes the clip outright");

            const auto menuItems = lanes.getClipMenuItemsForTest (menuClipId, 960);

            // The clip strip that used to live down the right of the arrangement is
            // gone: 重命名 / 循环 / 重复 now travel with the clip's own menu.
            check ((int) menuItems.size() == 7, "the menu has seven entries (got "
                                                    + juce::String ((int) menuItems.size()) + ")");

            if (menuItems.size() == 7)
            {
                check (menuItems[0].name == NI_TXT ("删除") && menuItems[1].name == NI_TXT ("切割")
                           && menuItems[2].name == NI_TXT ("粘合") && menuItems[3].name == NI_TXT ("复制")
                           && menuItems[4].name == NI_TXT ("重命名…")
                           && menuItems[5].name == NI_TXT ("循环")
                           && menuItems[6].name == NI_TXT ("重复…"),
                       NI_TXT ("the entries are 删除 / 切割 / 粘合 / 复制 / 重命名 / 循环 / 重复"));

                check (menuItems[1].enabled, "cutting in the middle is offered");
                check (! menuItems[2].enabled,
                       "gluing is greyed out with no neighbour to join");
                check (menuItems[5].ticked
                           == document.getProject().findClip (menuClipId)->looped,
                       "the loop entry is ticked exactly when the clip loops");
            }

            // A cut outside the clip (or right on its edge) is refused.
            const auto edgeMenu = lanes.getClipMenuItemsForTest (menuClipId, 0);
            check ((int) edgeMenu.size() == 7 && ! edgeMenu[1].enabled,
                   "cutting on the clip's edge is greyed out");

            // ---- 切割：the cut lands where the click did ------------------------
            check (lanes.performClipCommand (TrackArea::ClipCommand::split, menuClipId, 960),
                   "the split command ran");
            pump();

            const auto& afterSplit = document.getProject().tracks.front().clips;
            check ((int) afterSplit.size() == 2,
                   "one clip became two (got " + juce::String ((int) afterSplit.size()) + ")");

            if ((int) afterSplit.size() == 2)
            {
                check (afterSplit[0].startTick == 0 && afterSplit[0].endTick() == 960
                           && afterSplit[1].startTick == 960 && afterSplit[1].endTick() == 3840,
                       "the cut sits at the clicked tick ("
                           + juce::String (afterSplit[0].endTick()) + " / "
                           + juce::String (afterSplit[1].startTick) + ")");
                check (afterSplit[0].notes.size() + afterSplit[1].notes.size() == 2,
                       "the straddled note was cut in two");
            }

            // ---- 粘合：and now the neighbour is there --------------------------
            const auto glueMenu = lanes.getClipMenuItemsForTest (menuClipId, 960);
            check ((int) glueMenu.size() == 7 && glueMenu[2].enabled,
                   "gluing becomes available once the halves are adjacent");

            check (lanes.performClipCommand (TrackArea::ClipCommand::glue, menuClipId, 960),
                   "the glue command ran");
            pump();

            const auto& afterGlue = document.getProject().tracks.front().clips;
            check ((int) afterGlue.size() == 1 && afterGlue.front().endTick() == 3840,
                   "the two halves are one clip again ("
                       + juce::String ((int) afterGlue.size()) + " clips)");
            check (! afterGlue.empty() && afterGlue.front().notes.size() == 2,
                   "the glued clip holds both notes");

            // ---- 复制：into the same clipboard Ctrl+C uses ----------------------
            check (lanes.performClipCommand (TrackArea::ClipCommand::copy, menuClipId, 960),
                   "the copy command ran");
            pump();

            check (appState.hasClipClipboard && appState.clipClipboard.id == menuClipId,
                   "the clip landed on the clip clipboard");

            component.pasteClipboard();
            pump();

            check ((int) document.getProject().tracks.front().clips.size() == 2,
                   "Ctrl+V pastes it back as a second clip");

            // ---- 删除：with one undo -------------------------------------------
            check (lanes.performClipCommand (TrackArea::ClipCommand::deleteClip, menuClipId, 960),
                   "the delete command ran");
            pump();

            check ((int) document.getProject().tracks.front().clips.size() == 1,
                   "the clip is gone");

            document.undo();
            pump();

            check ((int) document.getProject().tracks.front().clips.size() == 2,
                   "one undo brings it back");
        }

        beginTest ("the editor divider is dragged up and down, not sideways");

        {
            auto* bar = component.getLayoutResizerForTest();

            check (bar != nullptr, "the divider exists");

            if (bar != nullptr)
            {
                const auto before = component.getEditorPanel().getHeight();
                const auto centre = bar->getLocalBounds().getCentre();

                // Sideways: should do nothing at all.
                bar->mouseDown (makeMouseEvent (*bar, centre.toFloat(), centre.toFloat()));
                bar->mouseDrag (makeMouseEvent (*bar, centre.toFloat() + juce::Point<float> (60.0f, 0.0f),
                                                centre.toFloat()));
                bar->mouseUp (makeMouseEvent (*bar, centre.toFloat(), centre.toFloat()));
                pump();

                check (component.getEditorPanel().getHeight() == before,
                       "dragging the divider sideways leaves the editor height alone ("
                           + juce::String (component.getEditorPanel().getHeight()) + " vs "
                           + juce::String (before) + ")");

                // Upwards: the editor should grow.
                bar->mouseDown (makeMouseEvent (*bar, centre.toFloat(), centre.toFloat()));
                bar->mouseDrag (makeMouseEvent (*bar, centre.toFloat() + juce::Point<float> (0.0f, -60.0f),
                                                centre.toFloat()));
                bar->mouseUp (makeMouseEvent (*bar, centre.toFloat(), centre.toFloat()));
                pump();

                const auto after = component.getEditorPanel().getHeight();

                check (after > before,
                       "dragging the divider upwards grows the editor ("
                           + juce::String (before) + " -> " + juce::String (after) + ")");
            }
        }

        beginTest ("dragging against an edge scrolls the timeline onwards");

        {
            juce::int64 dragClipId = 0;

            document.applyEdit ("selftest autoscroll setup", [&dragClipId] (Project& project)
            {
                auto& track = project.tracks.front();
                track.clips.clear();

                auto clip = makeDefaultClip (project);
                clip.startTick = 0;
                clip.lengthTicks = 3840;
                clip.loopLengthTicks = 3840;
                dragClipId = clip.id;

                Note n;
                n.id = project.nextNoteId++;
                n.startTick = 0;
                n.durationTicks = 1920;
                n.frequencyHz = 220.0;
                clip.notes = { n };

                track.clips.push_back (std::move (clip));
            });

            appState.selectTrack (document.getProject().tracks.front().id);
            appState.selectClip (dragClipId);

            document.viewState().pixelsPerQuarter = 140.0;
            document.viewState().horizontalOffsetTicks = 0.0;
            document.viewState().arrangementPixelsPerQuarter = 140.0;
            document.viewState().arrangementHorizontalOffsetTicks = 0.0;
            lanes.refresh();
            pump();

            // Grab the clip by its middle and drag it to the right edge of the lane.
            const auto clipBounds = lanes.clipBoundsForTest (dragClipId);
            const auto grabPoint = clipBounds.getCentre();
            const auto edgePoint = juce::Point<int> (lanes.getWidth() - 4, grabPoint.y);

            lanes.mouseDown (makeMouseEvent (lanes, grabPoint.toFloat(), grabPoint.toFloat()));
            lanes.mouseDrag (makeMouseEvent (lanes, edgePoint.toFloat(), grabPoint.toFloat()));

            // The baselines are read before the message loop runs and the "after"
            // values right after the manual steps: the auto-scroll timer starts with
            // the drag, so any pump in between can add a step of its own and make the
            // recorded numbers depend on machine load.
            const auto offsetBefore = document.viewState().arrangementHorizontalOffsetTicks;
            const auto previewEndBefore = lanes.getDragPreviewEndTickForTest();

            // The timer would do this on its own; run it by hand since the test has
            // no real message loop driving timers. One step is smaller than the
            // snap grid, so run a few of them before looking for a change.
            for (int i = 0; i < 12; ++i)
                lanes.tickAutoScrollForTest();

            const auto offsetAfter = document.viewState().arrangementHorizontalOffsetTicks;
            const auto previewEndAfter = lanes.getDragPreviewEndTickForTest();

            check (offsetAfter > offsetBefore,
                   "holding the clip against the edge scrolls the timeline ("
                       + juce::String (offsetBefore) + " -> " + juce::String (offsetAfter) + ")");
            check (previewEndAfter > previewEndBefore,
                   "and the clip keeps going with it (" + juce::String (previewEndBefore)
                       + " -> " + juce::String (previewEndAfter) + ")");

            lanes.mouseUp (makeMouseEvent (lanes, edgePoint.toFloat(), grabPoint.toFloat()));
            pump();

            check (! lanes.isAutoScrollingForTest(),
                   "releasing the mouse stops the scrolling");

            // What was previewed is what gets committed: the clip really does end
            // up further along than the screen allowed.
            const auto* movedClip = document.getProject().findClip (dragClipId);

            check (movedClip != nullptr && movedClip->endTick() == previewEndAfter,
                   "the clip is committed where the scrolling drag left it ("
                       + juce::String (movedClip != nullptr ? movedClip->endTick() : 0) + " vs "
                       + juce::String (previewEndAfter) + ")");
        }

        beginTest ("dragging a note against the edge scrolls it further too");

        {
            auto& sectionGrid = component.getEditorPanel().getGrid();

            juce::int64 noteId = 0;
            juce::int64 clipId = 0;

            document.applyEdit ("selftest note autoscroll setup",
                                [&noteId, &clipId] (Project& project)
            {
                auto& track = project.tracks.front();
                track.clips.clear();
                project.referenceLines.clear();

                auto clip = makeDefaultClip (project);
                clip.startTick = 0;
                clip.lengthTicks = 38400;
                clip.loopLengthTicks = 38400;
                clipId = clip.id;

                Note n;
                n.id = project.nextNoteId++;
                n.startTick = 0;
                n.durationTicks = 960;
                n.frequencyHz = 220.0;
                noteId = n.id;
                clip.notes = { n };

                track.clips.push_back (std::move (clip));
            });

            appState.selectTrack (document.getProject().tracks.front().id);
            appState.selectClip (clipId);
            appState.selectNote (noteId, false);

            document.viewState().pixelsPerQuarter = 140.0;
            document.viewState().horizontalOffsetTicks = 0.0;
            document.viewState().octavesVisible = 4.0;
            document.viewState().centreHz = 220.0;
            sectionGrid.refresh();
            pump();

            const auto grab = sectionGrid.pointFor (220.0, 480);
            const auto edge = juce::Point<float> ((float) sectionGrid.getWidth() - 4.0f, grab.y);

            // The baseline has to be taken before the message loop runs: the
            // auto-scroll timer starts as soon as the drag does.
            sectionGrid.mouseDown (makeMouseEvent (sectionGrid, grab, grab));
            sectionGrid.mouseDrag (makeMouseEvent (sectionGrid, edge, grab));

            const auto offsetBefore = document.viewState().horizontalOffsetTicks;
            const auto rawBefore = sectionGrid.getDragRawTickForTest();

            // Run the auto-scroll by hand: the test has no message loop driving
            // timers. Enough steps to carry the tick past a whole snap unit, since
            // one step is far shorter than the sectionGrid.
            for (int i = 0; i < 24; ++i)
                sectionGrid.tickAutoScrollForTest();

            const auto offsetAfter = document.viewState().horizontalOffsetTicks;
            const auto rawAfter = sectionGrid.getDragRawTickForTest();

            check (offsetAfter > offsetBefore,
                   "a note held against the edge scrolls the editor ("
                       + juce::String (offsetBefore) + " -> " + juce::String (offsetAfter) + ")");
            check (rawAfter > rawBefore,
                   "the tick under the pointer keeps growing ("
                       + juce::String (rawBefore) + " -> " + juce::String (rawAfter) + ")");
            check (rawAfter - rawBefore >= kTicksPerQuarter,
                   "holding the edge carries the drag past a whole snap unit ("
                       + juce::String (rawAfter - rawBefore) + " ticks)");

            // A real mouse keeps reporting positions, so send one more and drop:
            // the note has to land away from where it started.
            sectionGrid.mouseDrag (makeMouseEvent (sectionGrid, edge, grab));
            sectionGrid.mouseUp (makeMouseEvent (sectionGrid, edge, grab));
            pump();

            const auto* dropped = document.findNote (document.getProject().tracks.front().id, noteId);

            check (dropped != nullptr, "the dragged note is still there after the drop");
            check (dropped != nullptr && dropped->startTick > 0,
                   "and it was moved in time (" + juce::String (dropped != nullptr
                                                                    ? dropped->startTick : 0)
                       + " ticks in)");
        }

        beginTest (NI_TXT ("上下拖动碰到边界时，音高轴与轨道表也跟着滚（第三十九批追加）"));

        {
            // ---- 1. the pitch axis: a note dragged against the top edge --------
            juce::int64 climbId = 0;
            juce::int64 offScreenId = 0;
            juce::int64 nearBandId = 0;
            juce::int64 verticalClipId = 0;

            document.applyEdit ("selftest vertical autoscroll setup",
                                [&climbId, &offScreenId, &nearBandId, &verticalClipId] (Project& project)
            {
                auto& track = project.tracks.front();
                track.clips.clear();
                project.referenceLines.clear();

                // A locked 110 Hz family. The reference view snaps a dragged note to
                // the lines it can see, so the visible set has to be re-taken while
                // the axis slides - otherwise the pitch could never leave the first
                // screenful.
                auto entry = music::makeFixedHzEntry (project.nextReferenceLineId++, 110.0,
                                                      juce::String());
                entry.locked = true;
                project.referenceLines.push_back (std::move (entry));

                auto clip = makeDefaultClip (project);
                clip.startTick = 0;
                clip.lengthTicks = 38400;
                clip.loopLengthTicks = 38400;
                verticalClipId = clip.id;

                Note low;
                low.id = project.nextNoteId++;
                low.startTick = 0;
                low.durationTicks = 960;
                low.frequencyHz = 440.0;

                Note high;
                high.id = project.nextNoteId++;
                high.startTick = 1920;
                high.durationTicks = 960;
                high.frequencyHz = 3520.0;      // above the first view (110..1760 Hz)

                // One octave over the line the band will start on: inside the band
                // straight away, and still inside it after the view slides (which is
                // what the band riding the content, not the screen, means).
                Note nearBand;
                nearBand.id = project.nextNoteId++;
                nearBand.startTick = 1920;
                nearBand.durationTicks = 960;
                nearBand.frequencyHz = 440.0;

                climbId = low.id;
                offScreenId = high.id;
                nearBandId = nearBand.id;
                clip.notes = { low, high, nearBand };
                track.clips.push_back (std::move (clip));
            });

            appState.selectTrack (document.getProject().tracks.front().id);
            appState.selectClip (verticalClipId);
            appState.selectNote (climbId, false);

            auto& pitchView = document.viewState();
            pitchView.autoFitView = false;
            pitchView.editorMode = EditorMode::reference;
            pitchView.centreHz = 440.0;
            pitchView.octavesVisible = 4.0;
            pitchView.pixelsPerQuarter = 140.0;
            pitchView.horizontalOffsetTicks = 0.0;
            grid.refresh();
            pump();

            const auto noteGrab = grid.pointFor (440.0, 480);
            const auto topEdge = juce::Point<float> (noteGrab.x, 6.0f);

            // Down on the note, then straight up against the top edge: the pitch
            // axis is the one that has to give way.
            grid.mouseDown (makeMouseEvent (grid, noteGrab, noteGrab));
            grid.mouseDrag (makeMouseEvent (grid, topEdge, noteGrab));

            // Slide the axis by about 2.6 octaves: enough to bring the off-screen
            // 3520 Hz note into the middle of the canvas, and worked out from the
            // canvas height so it does not depend on how tall the editor is.
            const auto pixelsPerOctave = (double) juce::jmax (24, grid.getHeight() - 12) / 4.0;
            const auto verticalSteps = juce::jmax (2, (int) std::round (2.6 * pixelsPerOctave
                                                                       / (double) autoscroll::kStepPixels));

            const auto centreBefore = pitchView.centreHz;
            const auto pitchBefore = grid.getDragRawFrequencyForTest();

            // The timer would do this on its own; run it by hand since the test has
            // no real message loop driving timers.
            for (int i = 0; i < verticalSteps; ++i)
                grid.tickAutoScrollForTest();

            const auto centreAfter = pitchView.centreHz;
            const auto pitchAfter = grid.getDragRawFrequencyForTest();

            check (centreAfter > centreBefore,
                   NI_TXT ("拖到上边界时音高轴跟着往上走（") + juce::String (centreBefore, 1)
                       + NI_TXT (" Hz -> ") + juce::String (centreAfter, 1) + NI_TXT (" Hz）"));
            check (pitchAfter > pitchBefore,
                   NI_TXT ("指针底下的音高一直往上爬（") + juce::String (pitchBefore, 2)
                       + NI_TXT (" Hz -> ") + juce::String (pitchAfter, 2) + NI_TXT (" Hz）"));
            check (pitchAfter > 1760.0,
                   NI_TXT ("爬过了拖拽开始时画布顶上那条线（1760 Hz，现在 ")
                       + juce::String (pitchAfter, 2) + NI_TXT (" Hz）"));

            grid.mouseUp (makeMouseEvent (grid, topEdge, noteGrab));
            pump();

            // ---- 2. a rubber band against the top edge reaches an off-screen pitch
            pitchView.centreHz = 440.0;
            pitchView.octavesVisible = 4.0;
            grid.refresh();
            appState.clearNotes();
            pump();

            check (! appState.isNoteSelected (offScreenId),
                   NI_TXT ("先确认 3520 Hz 那个音符在画面外、也没被选中"));

            const auto bandStart = grid.pointFor (220.0, 2400);
            // Mostly vertical on purpose: the first movement decides the gesture,
            // and only a band follows the edge up (a horizontal drag would write a
            // note instead, and writing is pinned to the row it started in).
            const auto bandEnd = juce::Point<float> (bandStart.x - 40.0f, 6.0f);

            // A vertical drag from a line box-selects instead of writing a note, and
            // the band's top corner is held against the edge.
            grid.mouseDown (makeMouseEvent (grid, bandStart, bandStart));
            grid.mouseDrag (makeMouseEvent (grid, bandEnd, bandStart));

            check (appState.isNoteSelected (nearBandId),
                   NI_TXT ("按住不放的时候，紧挨着按点的那个音符已经被框住了"));

            const auto bandCentreBefore = pitchView.centreHz;

            for (int i = 0; i < verticalSteps; ++i)
                grid.tickAutoScrollForTest();

            check (pitchView.centreHz > bandCentreBefore,
                   NI_TXT ("橡皮筋按住上边界也把音高轴往上带（")
                       + juce::String (bandCentreBefore, 1) + NI_TXT (" Hz -> ")
                       + juce::String (pitchView.centreHz, 1) + NI_TXT (" Hz）"));
            check (appState.isNoteSelected (nearBandId),
                   NI_TXT ("视图滚走以后，它还在框里（框的角跟着内容走，不跟着屏幕走）"));
            check (appState.isNoteSelected (offScreenId),
                   NI_TXT ("原来在画面外的那个音高被框了进来（现在选中 ")
                       + juce::String ((int) appState.selectedNoteIds.size()) + NI_TXT (" 个）"));

            grid.mouseUp (makeMouseEvent (grid, bandEnd, bandStart));
            pump();

            // A note being WRITTEN is pinned to the row the press landed in - that
            // row is the pitch it will commit - so the axis must stay put.
            pitchView.centreHz = 440.0;
            pitchView.octavesVisible = 4.0;
            grid.refresh();
            appState.clearNotes();
            pump();

            const auto writeGrab = grid.pointFor (1760.0, 2400);
            const auto writeEnd = juce::Point<float> (writeGrab.x + 200.0f, writeGrab.y);

            grid.mouseDown (makeMouseEvent (grid, writeGrab, writeGrab));
            grid.mouseDrag (makeMouseEvent (grid, writeEnd, writeGrab));

            const auto writeCentreBefore = pitchView.centreHz;

            for (int i = 0; i < verticalSteps; ++i)
                grid.tickAutoScrollForTest();

            check (juce::approximatelyEqual (pitchView.centreHz, writeCentreBefore),
                   NI_TXT ("写音符时贴着上边界也不滚音高轴（")
                       + juce::String (writeCentreBefore, 1) + NI_TXT (" Hz -> ")
                       + juce::String (pitchView.centreHz, 1) + NI_TXT (" Hz）"));

            grid.mouseUp (makeMouseEvent (grid, writeEnd, writeGrab));
            pump();

            // ---- 3. the arrangement: a clip dragged against the bottom edge -----
            juce::int64 moveClipId = 0;
            juce::int64 bottomTrackId = 0;

            document.applyEdit ("selftest lane autoscroll setup",
                                [&moveClipId, &bottomTrackId] (Project& project)
            {
                project.tracks.clear();
                project.nextTrackId = 1;

                for (int i = 0; i < 12; ++i)
                {
                    const auto id = addTrack (project, TrackKind::synth);
                    bottomTrackId = id;

                    if (i == 0)
                    {
                        auto& clip = project.findTrack (id)->clips.front();
                        clip.startTick = 0;
                        clip.lengthTicks = 3840;
                        clip.loopLengthTicks = 3840;
                        moveClipId = clip.id;
                    }
                }
            });
            pump();

            lanes.setLaneScrollForTest (0);
            lanes.refresh();
            pump();

            appState.selectTrack (document.getProject().tracks.front().id);
            appState.selectClip (moveClipId);
            document.viewState().arrangementPixelsPerQuarter = 140.0;
            document.viewState().arrangementHorizontalOffsetTicks = 0.0;
            lanes.refresh();
            pump();

            const auto clipGrab = lanes.clipBoundsForTest (moveClipId).getCentre();
            const auto laneBottomEdge = juce::Point<int> (clipGrab.x, lanes.getHeight() - 4);

            lanes.mouseDown (makeMouseEvent (lanes, clipGrab.toFloat(), clipGrab.toFloat()));
            lanes.mouseDrag (makeMouseEvent (lanes, laneBottomEdge.toFloat(), clipGrab.toFloat()));

            const auto laneScrollBefore = lanes.getLaneScrollForTest();

            const auto laneTicks = lanes.getMaxLaneScrollForTest() / autoscroll::kStepPixels + 4;

            for (int i = 0; i < laneTicks; ++i)
                lanes.tickAutoScrollForTest();

            const auto laneScrollAfter = lanes.getLaneScrollForTest();

            check (laneScrollAfter > laneScrollBefore,
                   NI_TXT ("片段拖到下边界时轨道表跟着往下滚（")
                       + juce::String (laneScrollBefore) + NI_TXT (" -> ")
                       + juce::String (laneScrollAfter) + NI_TXT (" px）"));
            check (laneScrollAfter == lanes.getMaxLaneScrollForTest(),
                   NI_TXT ("一直滚到最底下（因为一直在边界上，滚到 ")
                       + juce::String (lanes.getMaxLaneScrollForTest()) + NI_TXT (" px）"));

            lanes.mouseUp (makeMouseEvent (lanes, laneBottomEdge.toFloat(), clipGrab.toFloat()));
            pump();

            juce::int64 landedTrackId = 0;

            for (const auto& track : document.getProject().tracks)
                if (track.findClip (moveClipId) != nullptr)
                    landedTrackId = track.id;

            check (landedTrackId == bottomTrackId,
                   NI_TXT ("片段落到了原来滚出画面外的那条轨上（共 ")
                       + juce::String ((int) document.getProject().tracks.size())
                       + NI_TXT (" 条轨）"));
        }

        beginTest (NI_TXT ("侧视：把其他轨道的音符当背景板画出来（第四十批）"));

        {
            auto& editor = component.getEditorPanel();
            auto& canvas = editor.getGrid();
            juce::int64 sideOpenTrackId = 0;
            juce::int64 sideGhostTrackId = 0;
            juce::int64 sideDrumTrackId = 0;
            juce::int64 sideOpenNoteId = 0;

            document.applyEdit ("selftest side view setup",
                                [&sideOpenTrackId, &sideGhostTrackId, &sideDrumTrackId,
                                 &sideOpenNoteId] (Project& project)
            {
                project.tracks.clear();
                project.nextTrackId = 1;
                project.referenceLines.clear();

                // A: the track being edited - one 440 Hz note in a one bar clip.
                sideOpenTrackId = addTrack (project, TrackKind::synth);

                {
                    auto& clip = project.findTrack (sideOpenTrackId)->clips.front();
                    clip.startTick = 0;
                    clip.lengthTicks = 3840;
                    clip.loopLengthTicks = 3840;

                    Note note;
                    note.id = project.nextNoteId++;
                    note.startTick = 0;
                    note.durationTicks = 960;
                    note.frequencyHz = 440.0;
                    note.velocity = 100;
                    sideOpenNoteId = note.id;
                    clip.notes = { note };
                }

                // B: a ten bar clip looping every bar, with one 660 Hz note - the
                // side view has to show every repeat, not just the first bar.
                sideGhostTrackId = addTrack (project, TrackKind::synth);

                {
                    auto& clip = project.findTrack (sideGhostTrackId)->clips.front();
                    clip.startTick = 0;
                    clip.lengthTicks = 38400;
                    clip.looped = true;
                    clip.loopLengthTicks = 3840;

                    Note note;
                    note.id = project.nextNoteId++;
                    note.startTick = 0;
                    note.durationTicks = 960;
                    note.frequencyHz = 660.0;
                    note.velocity = 100;
                    clip.notes = { note };
                }

                // C: a drum track - its "notes" are pad hits, so they are not
                // pitches and can never be background material for this canvas.
                sideDrumTrackId = addTrack (project, TrackKind::drums);

                {
                    auto& clip = project.findTrack (sideDrumTrackId)->clips.front();
                    clip.startTick = 0;
                    clip.lengthTicks = 3840;
                    clip.loopLengthTicks = 3840;

                    Note hit;
                    hit.id = project.nextNoteId++;
                    hit.startTick = 0;
                    hit.durationTicks = 240;
                    hit.pad = 0;
                    hit.frequencyHz = 60.0;
                    clip.notes = { hit };
                }
            });

            appState.setEditorOpen (true);
            appState.selectTrack (sideOpenTrackId);
            appState.selectClip (document.getProject().findTrack (sideOpenTrackId)->clips.front().id);
            appState.clearNotes();

            auto& sideState = document.viewState();
            sideState.autoFitView = false;
            sideState.editorMode = EditorMode::reference;
            sideState.centreHz = 440.0;
            sideState.octavesVisible = 4.0;
            sideState.pixelsPerQuarter = 140.0;
            sideState.horizontalOffsetTicks = 0.0;
            sideState.sideView = false;
            canvas.refresh();

            // The playhead is part of the canvas, so park the transport before the
            // two snapshots below: a moving playhead would look like "something
            // else changed" and hide the thing this block is really measuring.
            auto& sideAudio = component.getAudioEngine();
            sideAudio.stop();
            sideAudio.setPositionSeconds (0.0);
            canvas.refreshPlayhead();
            pump();

            const auto canvasRect = component.getLocalArea (&canvas, canvas.getLocalBounds());

            check (editor.getSideViewButtonTextForTest() == NI_TXT ("侧视"),
                   NI_TXT ("表头那个按钮写着「侧视」（实际是「")
                       + editor.getSideViewButtonTextForTest() + NI_TXT ("」）"));
            check (! editor.getSideViewButtonBoundsForTest().isEmpty(),
                   NI_TXT ("参考线模式下它有真实尺寸（")
                       + editor.getSideViewButtonBoundsForTest().toString() + NI_TXT ("）"));
            check (editor.isSideViewButtonEnabledForTest(), NI_TXT ("合成器轨上可以用"));
            check (! editor.isSideViewOnForTest(), NI_TXT ("默认是关的"));
            check (canvas.getSideViewGhostRectsForTest().empty(),
                   NI_TXT ("关着的时候没有幽灵"));

            const auto sideOffImage = snapshot (component);
            const auto undoBeforeSideView = document.getUndoDescription();

            editor.toggleSideViewForTest();
            pump();

            check (editor.isSideViewOnForTest() && sideState.sideView,
                   NI_TXT ("点一下打开了（工程状态也跟着变）"));
            check (document.getUndoDescription() == undoBeforeSideView,
                   NI_TXT ("这只是视图开关，不进撤销栈（还是「") + undoBeforeSideView
                       + NI_TXT ("」）"));

            // What the painter will draw: the looped clip's note once per bar, and
            // nothing at all from the drum track.
            const auto ghosts = canvas.getSideViewGhostRectsForTest();

            check (ghosts.size() == 2,
                   NI_TXT ("循环片段按实际发声音平铺成两个幽灵（实际 ")
                       + juce::String ((int) ghosts.size()) + NI_TXT (" 个）"));

            if (ghosts.size() == 2)
            {
                const auto firstBar = canvas.pointFor (660.0, 0);
                const auto secondBar = canvas.pointFor (660.0, 3840);

                check (std::abs (ghosts[0].getX() - firstBar.x) <= 1.5f
                           && std::abs (ghosts[0].getCentreY() - firstBar.y) <= 1.5f,
                       NI_TXT ("第一个幽灵落在 660 Hz 的第一小节上（")
                           + ghosts[0].toString() + NI_TXT (" vs ")
                           + juce::String (firstBar.x, 1) + NI_TXT (", ")
                           + juce::String (firstBar.y, 1) + NI_TXT ("）"));
                check (std::abs (ghosts[1].getX() - secondBar.x) <= 1.5f
                           && std::abs (ghosts[1].getCentreY() - secondBar.y) <= 1.5f,
                       NI_TXT ("第二个幽灵是循环出来的那一遍（")
                           + ghosts[1].toString() + NI_TXT ("）"));
            }

            // The ghosts really reach the bitmap, and nothing else changed: every
            // pixel that differs between off and on is inside one of those two.
            const auto sideOnImage = snapshot (component);
            const auto ownNoteRect = canvas.getNoteRectForTest (sideOpenNoteId)
                                         .getSmallestIntegerContainer();
            auto changedPixels = 0;
            auto changedInOwnNote = 0;
            juce::Rectangle<int> unexpected;

            for (int y = canvasRect.getY(); y < canvasRect.getBottom(); ++y)
            {
                for (int x = canvasRect.getX(); x < canvasRect.getRight(); ++x)
                {
                    if (sideOffImage.getPixelAt (x, y) == sideOnImage.getPixelAt (x, y))
                        continue;

                    ++changedPixels;

                    if (ownNoteRect.contains (x, y))
                        ++changedInOwnNote;

                    auto inside = false;

                    for (const auto& ghost : ghosts)
                        // The ghost rectangles are in the canvas's own coordinates;
                        // this scan walks the whole component's pixels.
                        inside = inside || ghost.expanded (2.0f)
                                              .contains ((float) (x - canvasRect.getX()),
                                                         (float) (y - canvasRect.getY()));

                    if (! inside)
                        unexpected = unexpected.isEmpty()
                                         ? juce::Rectangle<int> (x, y, 1, 1)
                                         : unexpected.getUnion (juce::Rectangle<int> (x, y, 1, 1));
                }
            }

            check (changedPixels > 0,
                   NI_TXT ("打开之后画面上真的多了东西（") + juce::String (changedPixels)
                       + NI_TXT (" 个像素）"));
            check (unexpected.isEmpty(),
                   NI_TXT ("变化全在那两个幽灵的矩形里（越界的变化在画布内 ")
                       + unexpected.translated (-canvasRect.getX(), -canvasRect.getY()).toString()
                       + NI_TXT ("，共 ") + juce::String (changedPixels) + NI_TXT (" 个像素变了）"));
            check (changedInOwnNote == 0,
                   NI_TXT ("当前轨道自己的音符一个像素都没动（变了 ")
                       + juce::String (changedInOwnNote) + NI_TXT (" 个像素）"));

            // A background board is not a target: pressing on a ghost selects
            // nothing and writes nothing.
            const auto ghostHit = canvas.pointFor (660.0, 480);
            const auto notesBeforePress =
                (int) document.getProject().findTrack (sideOpenTrackId)->clips.front().notes.size();

            canvas.mouseDown (makeMouseEvent (canvas, ghostHit, ghostHit));
            canvas.mouseDrag (makeMouseEvent (canvas, ghostHit + juce::Point<float> (0.0f, 12.0f),
                                              ghostHit));
            canvas.mouseUp (makeMouseEvent (canvas, ghostHit + juce::Point<float> (0.0f, 12.0f),
                                            ghostHit));
            pump();

            check (appState.selectedNoteIds.empty(),
                   NI_TXT ("点幽灵不会选中任何音符（选中 ")
                       + juce::String ((int) appState.selectedNoteIds.size()) + NI_TXT (" 个）"));
            check ((int) document.getProject().findTrack (sideOpenTrackId)->clips.front().notes.size()
                       == notesBeforePress,
                   NI_TXT ("也不会在它上面写音符（还是 ")
                       + juce::String (notesBeforePress) + NI_TXT (" 个）"));

            // The piano window shares the frequency axis, so the ghosts show there
            // too - and the button keeps its place.
            editor.selectEditorModeForTest (2);
            pump();

            check (! editor.getSideViewButtonBoundsForTest().isEmpty(),
                   NI_TXT ("钢琴窗模式下按钮还在（")
                       + editor.getSideViewButtonBoundsForTest().toString() + NI_TXT ("）"));
            check (canvas.getSideViewGhostRectsForTest().size() == 2,
                   NI_TXT ("钢琴窗模式下幽灵也画（")
                       + juce::String ((int) canvas.getSideViewGhostRectsForTest().size())
                       + NI_TXT (" 个）"));

            editor.selectEditorModeForTest (1);
            pump();

            // A drum track has no frequency axis: the switch greys out, and no
            // ghosts are drawn for it.
            appState.selectTrack (sideDrumTrackId);
            pump();

            check (! editor.isSideViewButtonEnabledForTest(),
                   NI_TXT ("打开的轨道是鼓机时「侧视」置灰"));
            check (canvas.getSideViewGhostRectsForTest().empty(),
                   NI_TXT ("鼓机轨的画布上也不画幽灵"));

            appState.selectTrack (sideOpenTrackId);
            sideState.sideView = false;
            pump();

            check (canvas.getSideViewGhostRectsForTest().empty(),
                   NI_TXT ("关掉之后幽灵就没了"));
        }

        beginTest (NI_TXT ("侧视：勾选想看的轨道（第四十批追加）"));

        {
            auto& editor = component.getEditorPanel();
            auto& canvas = editor.getGrid();
            juce::int64 pickOpenId = 0;
            juce::int64 pickLoopId = 0;
            juce::int64 pickSingleId = 0;
            juce::int64 pickDrumId = 0;

            document.applyEdit ("selftest side view picker setup",
                                [&pickOpenId, &pickLoopId, &pickSingleId, &pickDrumId] (Project& project)
            {
                project.tracks.clear();
                project.nextTrackId = 1;
                project.referenceLines.clear();

                pickOpenId   = addTrack (project, TrackKind::synth);
                pickLoopId   = addTrack (project, TrackKind::synth);
                pickSingleId = addTrack (project, TrackKind::synth);
                pickDrumId   = addTrack (project, TrackKind::drums);

                auto setClip = [] (Project& p, juce::int64 trackId, juce::int64 length,
                                   bool looped, double hz)
                {
                    auto& clip = p.findTrack (trackId)->clips.front();
                    clip.startTick = 0;
                    clip.lengthTicks = length;
                    clip.looped = looped;
                    clip.loopLengthTicks = 3840;

                    Note note;
                    note.id = p.nextNoteId++;
                    note.startTick = 0;
                    note.durationTicks = 960;
                    note.frequencyHz = hz;
                    note.velocity = 100;
                    clip.notes = { note };
                };

                // The one being edited, two that can be ghosts (the first loops, so
                // it is worth two ghosts), and a drum track that never can be.
                setClip (project, pickOpenId, 3840, false, 440.0);
                setClip (project, pickLoopId, 7680, true, 660.0);
                setClip (project, pickSingleId, 3840, false, 880.0);

                auto& drums = project.findTrack (pickDrumId)->clips.front();
                drums.startTick = 0;
                drums.lengthTicks = 3840;
                drums.loopLengthTicks = 3840;
            });

            appState.setEditorOpen (true);
            appState.selectTrack (pickOpenId);
            appState.selectClip (document.getProject().findTrack (pickOpenId)->clips.front().id);
            appState.clearNotes();

            auto& pickView = document.viewState();
            pickView.autoFitView = false;
            pickView.editorMode = EditorMode::reference;
            pickView.centreHz = 440.0;
            pickView.octavesVisible = 4.0;
            pickView.pixelsPerQuarter = 140.0;
            pickView.horizontalOffsetTicks = 0.0;
            pickView.sideViewHiddenTracks.clear();
            pickView.sideView = true;

            auto& pickAudio = component.getAudioEngine();
            pickAudio.stop();
            pickAudio.setPositionSeconds (0.0);
            canvas.refresh();
            canvas.refreshPlayhead();
            pump();

            const auto ghostCount = [&canvas] ()
            {
                return (int) canvas.getSideViewGhostRectsForTest().size();
            };

            const auto selectable = editor.getSideViewSelectableTracksForTest();

            check (selectable.size() == 2
                       && selectable[0] == pickLoopId && selectable[1] == pickSingleId,
                   NI_TXT ("清单只列能当背景的其他轨道（当前轨与鼓机轨都不列；实际 ")
                       + juce::String ((int) selectable.size()) + NI_TXT (" 条）"));

            const auto pickerItems = editor.getSideViewMenuItemsForTest();

            check (pickerItems.size() == 5,
                   NI_TXT ("菜单是「显示侧视 + 两条轨 + 全选 / 全不选」（实际 ")
                       + juce::String ((int) pickerItems.size()) + NI_TXT (" 项）"));
            check (! pickerItems.empty() && pickerItems[0].text == NI_TXT ("显示侧视")
                       && pickerItems[0].ticked,
                   NI_TXT ("第一项是「显示侧视」，现在是勾上的"));
            check (pickerItems.size() == 5 && pickerItems[1].trackId == pickLoopId
                       && pickerItems[1].ticked && pickerItems[2].trackId == pickSingleId
                       && pickerItems[2].ticked,
                   NI_TXT ("两条轨道的复选框默认都勾着（等于「全看」）"));

            check (ghostCount() == 3,
                   NI_TXT ("默认三条幽灵：循环那条两遍 + 单条那条一遍（实际 ")
                       + juce::String (ghostCount()) + NI_TXT (" 条）"));

            // Un-tick the looping one: both of its ghosts go, the other track stays.
            check (editor.performSideViewMenuCommandForTest (pickerItems[1].menuId),
                   NI_TXT ("点掉循环那条轨道"));
            pump();

            check (ghostCount() == 1 && pickView.sideViewHiddenTracks.count (pickLoopId) == 1,
                   NI_TXT ("它的两遍幽灵一起没了，另一条还在（现在 ")
                       + juce::String (ghostCount()) + NI_TXT (" 条）"));
            check (editor.getSideViewMenuItemsForTest()[1].ticked == false,
                   NI_TXT ("菜单里那一项的勾也跟着掉了"));

            check (editor.performSideViewMenuCommandForTest (pickerItems[2].menuId),
                   NI_TXT ("再点掉另一条轨道"));
            pump();

            check (ghostCount() == 0,
                   NI_TXT ("两条都点掉就一条幽灵也没有（现在 ") + juce::String (ghostCount())
                       + NI_TXT (" 条）"));

            // 全选 / 全不选: the two shortcuts at the bottom of the same menu.
            check (editor.performSideViewMenuCommandForTest (2), NI_TXT ("全选"));
            pump();

            check (ghostCount() == 3 && pickView.sideViewHiddenTracks.empty(),
                   NI_TXT ("「全选」把两条都放回来（现在 ") + juce::String (ghostCount())
                       + NI_TXT (" 条）"));

            check (editor.performSideViewMenuCommandForTest (3), NI_TXT ("全不选"));
            pump();

            check (ghostCount() == 0 && pickView.sideViewHiddenTracks.size() == 2,
                   NI_TXT ("「全不选」把两条都收起来（现在 ") + juce::String (ghostCount())
                       + NI_TXT (" 条）"));

            check (editor.performSideViewMenuCommandForTest (2), NI_TXT ("再全选回来"));
            pump();

            check (ghostCount() == 3, NI_TXT ("又都回来了"));

            // The little arrow beside the button asks for the picker - and only for
            // the picker: clicking it must not flip 侧视 itself. (A synthetic mouse
            // click cannot reach a real juce::Button offscreen, so this goes through
            // the arrow's own onClick, the same path the mouse takes.)
            const auto requestsBefore = editor.getSideViewPickerRequestCountForTest();
            const auto sideViewBefore = pickView.sideView;

            check (! editor.getSideViewArrowBoundsForTest().isEmpty(),
                   NI_TXT ("「侧视」右边确实有一个小箭头（")
                       + editor.getSideViewArrowBoundsForTest().toString() + NI_TXT ("）"));

            editor.triggerSideViewArrowForTest();
            pump();

            check (editor.getSideViewPickerRequestCountForTest() == requestsBefore + 1,
                   NI_TXT ("点小箭头弹出「看哪些轨道」的菜单（第 ")
                       + juce::String (editor.getSideViewPickerRequestCountForTest())
                       + NI_TXT (" 次）"));
            check (pickView.sideView == sideViewBefore,
                   NI_TXT ("点箭头不会顺手把侧视开 / 关掉"));

            editor.toggleSideViewForTest();
            pump();

            check (pickView.sideView != sideViewBefore,
                   NI_TXT ("那个按钮本身仍然是开 / 关侧视"));

            editor.toggleSideViewForTest();
            pickView.sideViewHiddenTracks.clear();
            pump();
        }

        beginTest (NI_TXT ("MIDI 输入：设备、映射窗口、弹奏发声（第四十一批·第一步）"));

        {
            juce::int64 midiTrackId = 0;
            juce::int64 midiEntryId = 0;

            document.applyEdit ("selftest midi setup", [&midiTrackId, &midiEntryId] (Project& project)
            {
                project.tracks.clear();
                project.nextTrackId = 1;
                project.referenceLines.clear();
                project.midiInput = {};

                midiTrackId = addTrack (project, TrackKind::synth);

                auto& clip = project.findTrack (midiTrackId)->clips.front();
                clip.startTick = 0;
                clip.lengthTicks = 3840;
                clip.loopLengthTicks = 3840;

                // A 参考线组 to map onto: C4 pinned on a C major scale, as seven
                // dots (C D E F G A B). That is the group the user describes - the
                // seven white keys of one octave line up with its seven dots.
                ReferenceLineGroup group;
                group.name = NI_TXT ("C 大调");
                group.anchorHz = kMidiC4Hz;
                group.anchorPosition = 0.0;
                // One dot per scale degree, C major: the seven the user drags the
                // seven white keys onto.
                for (int i = 0; i < 7; ++i)
                    group.dots.push_back ({ music::ratioToPosition (kMidiCMajorRatios[i]),
                                            i % 4 });

                auto entry = music::makeGroupEntry (project.nextReferenceLineId++, group);
                entry.name = group.name;
                // Sealed, or the next selection change would clear it as scratch.
                entry.locked = true;
                midiEntryId = entry.id;
                project.referenceLines.push_back (std::move (entry));
            });
            pump();

            appState.setEditorOpen (true);
            appState.selectTrack (midiTrackId);
            appState.selectClip (document.getProject().findTrack (midiTrackId)->clips.front().id);
            appState.clearNotes();
            document.viewState().editorMode = EditorMode::reference;
            document.viewState().autoFitView = false;
            pump();

            component.openMidiInputWindow();
            pump();

            auto* midiWin = component.getMidiInputWindowForTest();

            check (midiWin != nullptr && midiWin->isVisible(),
                   NI_TXT ("「MIDI输入」打开了一个窗口"));

            if (midiWin != nullptr)
            {
                auto& midiPanel = midiWin->getPanel();
                auto& midiCanvas = midiPanel.getCanvas();

                check (! midiPanel.getBounds().isEmpty() && ! midiCanvas.getBounds().isEmpty(),
                       NI_TXT ("窗口里那一栏和画布都排得下（窗口 ")
                           + midiWin->getBounds().toString() + NI_TXT ("）"));
                check (midiPanel.getTrackTextForTest().contains (
                           document.getProject().findTrack (midiTrackId)->name),
                       NI_TXT ("窗口写着弹的是当前选中轨（「")
                           + midiPanel.getTrackTextForTest() + NI_TXT ("」）"));

                // ---- the device picker -------------------------------------
                component.setMidiDeviceListForTest ({ NI_TXT ("测试键盘 A"),
                                                      NI_TXT ("测试键盘 B") });
                pump();

                midiPanel.setDeviceForTest (0);
                pump();

                check (component.getMidiOpenDeviceNameForTest() == NI_TXT ("测试键盘 A"),
                       NI_TXT ("选中的设备被记住（「")
                           + component.getMidiOpenDeviceNameForTest() + NI_TXT ("」）"));
                check (! component.isMidiDeviceOpenForTest(),
                       NI_TXT ("自检机器上没有真设备，所以只是「记住了选择」"));

                const auto midiSettings = MidiInputManager::getSettingsFile();

                check (midiSettings.existsAsFile()
                           && midiSettings.loadFileAsString().contains ("test-midi-device-0"),
                       NI_TXT ("设备选择写进了设置文件（") + midiSettings.getFileName() + NI_TXT ("）"));

                // ---- the group picker and its dots -------------------------
                auto groupEntryCount = 0;

                for (const auto& entry : document.getProject().referenceLines)
                    if (entry.kind == ReferenceLineKind::group)
                        ++groupEntryCount;

                check (groupEntryCount == 1,
                       NI_TXT ("工程里那条参考线组还在（实际 ")
                           + juce::String (groupEntryCount) + NI_TXT (" 条）"));

                midiPanel.setGroupForTest (2);   // the first 状态栏 entry
                pump();

                check (midiCanvas.getDotCountForTest() == 21,
                       NI_TXT ("右侧把这一组画成三个八度：C 大调 7 个点 × 3 = 21（实际 ")
                           + juce::String (midiCanvas.getDotCountForTest()) + NI_TXT (" 个）"));

                // Octave-major order: dots 0..6 are the octave below, 7..13 the
                // anchor's own octave, 14..20 the one above.
                auto dotsExact = midiCanvas.getDotCountForTest() == 21;

                for (int i = 0; i < 7; ++i)
                {
                    const auto expected = kMidiC4Hz * kMidiCMajorRatios[i];

                    if (std::abs (midiCanvas.getDotFrequencyForTest (i) - expected * 0.5) > 1.0e-6
                        || std::abs (midiCanvas.getDotFrequencyForTest (7 + i) - expected) > 1.0e-6
                        || std::abs (midiCanvas.getDotFrequencyForTest (14 + i) - expected * 2.0) > 1.0e-6)
                        dotsExact = false;
                }

                check (dotsExact,
                       NI_TXT ("三个八度是同一组：中间那个八度就是 C4 ")
                           + juce::String (midiCanvas.getDotFrequencyForTest (7), 3)
                           + NI_TXT (" … B4 ") + juce::String (midiCanvas.getDotFrequencyForTest (13), 3)
                           + NI_TXT (" Hz，上下各再一个八度"));

                // 左列上下翻转：低的音在下、高的音在上（与右列的频率轴同向）。
                check (midiCanvas.getKeyBoundsForTest (60).getCentreY()
                           > midiCanvas.getKeyBoundsForTest (71).getCentreY()
                       && midiCanvas.getDotBoundsForTest (7).getCentreY()
                              > midiCanvas.getDotBoundsForTest (14).getCentreY(),
                       NI_TXT ("左边低音在下、高音在上（C4 的键比 B4 的键低），右列也是越往上越高"));

                // ---- drag a key onto a dot ---------------------------------
                const auto notes = midiCanvas.getVisibleNotesForTest();

                check ((int) notes.size() == 12 && notes.front() == 60,
                       NI_TXT ("循环模式默认一格 12 个半音、从锚点 C4 起（实际 ")
                           + juce::String ((int) notes.size()) + NI_TXT (" 个）"));

                check (midiCanvas.getKeyCaptionForTest (60) == juce::String ("C4")
                           && midiCanvas.getKeyCaptionForTest (61) == juce::String ("C#4")
                           && midiCanvas.getKeyCaptionForTest (71) == juce::String ("B4"),
                       NI_TXT ("每个琴键都写着音名（C4 / C#4 / … / B4）"));

                const int whiteSteps[7] = { 0, 2, 4, 5, 7, 9, 11 };
                const int blackSteps[5] = { 1, 3, 6, 8, 10 };

                auto allStepsEmpty = true;

                for (const auto note : notes)
                    if (! midiCanvas.isStepEmptyForTest (note))
                        allStepsEmpty = false;

                check (allStepsEmpty,
                       NI_TXT ("还没连线时 12 个步骤全是空的（5 个黑键当然也是）"));

                // ---- the seven white keys onto the seven dots ----------------
                auto draggedAll = true;
                auto farDropWorked = false;

                for (int i = 0; i < 7; ++i)
                {
                    const auto keyNote = 60 + whiteSteps[i];
                    const auto keyCentre = midiCanvas.getKeyBoundsForTest (keyNote)
                                               .getCentre().toFloat();
                    // 拖到**锚点那个八度**的那一个圆点：存下来的就是 C4 这一组的频率。
                    const auto anchorOctaveDot = 7 + i;
                    auto dotCentre = midiCanvas.getDotBoundsForTest (anchorOctaveDot)
                                         .getCentre().toFloat();

                    // One of the seven is let go a third of the way towards the next
                    // dot, i.e. clearly off the dot it is aiming at: the column takes
                    // the nearest dot, so "拖到圆点附近就行" is what this checks.
                    if (i == 3)
                    {
                        const auto here = (float) midiCanvas.getDotBoundsForTest (10).getCentre().y;
                        const auto next = (float) midiCanvas.getDotBoundsForTest (11).getCentre().y;
                        const auto loosened = here + (next - here) / 3.0f;

                        farDropWorked = std::abs (loosened - here) > 5.0f
                                        && midiCanvas.getDropDotForTest ({ (int) dotCentre.x,
                                                                           (int) loosened }) == 10;
                        dotCentre = { dotCentre.x, loosened };
                    }

                    midiCanvas.mouseDown (makeMouseEvent (midiCanvas, keyCentre, keyCentre));
                    midiCanvas.mouseDrag (makeMouseEvent (midiCanvas, dotCentre, keyCentre));
                    midiCanvas.mouseUp (makeMouseEvent (midiCanvas, dotCentre, keyCentre));
                    pump();

                    if (! midiCanvas.isKeyBoundForTest (keyNote))
                        draggedAll = false;
                }

                check (draggedAll, NI_TXT ("7 个白键一条条拖到 7 个圆点上，全部绑好了"));
                check (farDropWorked,
                       NI_TXT ("其中一条故意没落在圆点正中（差十几像素），照样按最近的圆点接住"));

                // 超出 14 px 磁力也一样接住：圆点上方 20 px 的空白处。
                {
                    const auto topDot = midiCanvas.getDotBoundsForTest (20).getCentre();
                    const auto above = juce::Point<int> (topDot.x, juce::jmax (2, topDot.y - 20));
                    const auto caught = midiCanvas.getDropDotForTest (above);

                    check (std::abs (above.y - topDot.y) >= 14
                               && caught >= 0
                               && midiCanvas.isDotBindableForTest (caught),
                           NI_TXT ("离最近圆点 ") + juce::String (std::abs (above.y - topDot.y))
                               + NI_TXT (" px（超出磁力）也接住最近的圆点（第 ")
                               + juce::String (caught) + NI_TXT (" 个）"));
                }

                // ---- 白键出音阶、黑键不发音、整组跟着键盘走 ------------------
                auto whiteAscending = true;

                for (int i = 0; i < 7; ++i)
                    if (std::abs (component.frequencyForMidiNote (60 + whiteSteps[i])
                                  - kMidiC4Hz * kMidiCMajorRatios[i]) > 1.0e-6)
                        whiteAscending = false;

                auto blackSilent = true;

                for (const auto step : blackSteps)
                    if (component.frequencyForMidiNote (60 + step) != 0.0)
                        blackSilent = false;

                check (whiteAscending,
                       NI_TXT ("白键从左到右就是 C 大调、一路递增（C4 ")
                           + juce::String (component.frequencyForMidiNote (60), 3)
                           + NI_TXT (" → B4 ")
                           + juce::String (component.frequencyForMidiNote (71), 3) + NI_TXT (" Hz）"));
                check (blackSilent, NI_TXT ("5 个黑键都没绑，按下去不发音"));
                checkNear (component.frequencyForMidiNote (72), kMidiC4Hz * 2.0, 1.0e-6,
                           NI_TXT ("高一个八度：整组升上去（C5 = 两倍的 C4）"));
                checkNear (component.frequencyForMidiNote (48), kMidiC4Hz * 0.5, 1.0e-6,
                           NI_TXT ("低一个八度：整组降下来（C3 = 一半）"));
                checkNear (component.frequencyForMidiNote (59),
                           kMidiC4Hz * kMidiCMajorRatios[6] * 0.5, 1.0e-6,
                           NI_TXT ("B3 是 B4 的一半，而不是重复 B4 那个音"));

                check (midiCanvas.isStepEmptyForTest (61) && ! midiCanvas.isStepEmptyForTest (60),
                       NI_TXT ("空格标记跟着绑定走：C 有音、C# 还是「空」"));

                // ---- 第四十一批·用户反馈：连不上 / 圆点颜色不对 ---------------
                {
                    // 「圆点的颜色没有匹配参考线组的颜色」: the canvas paints a group's
                    // lines with "the entry's colour if the status entry has one,
                    // otherwise THAT DOT's own colour". The group here carries four
                    // different dot colours (the selftest built it with i % 4), so
                    // the column must show four - and before the fix it showed one.
                    auto coloursMatch = midiCanvas.getDotCountForTest() == 21;

                    // 21 dots = 3 octaves of the group's 7 (whose colours cycle 0..3).
                    for (int i = 0; i < midiCanvas.getDotCountForTest(); ++i)
                        if (midiCanvas.getDotColourForTest (i)
                            != theme::trackColour ((i % 7) % 4))
                            coloursMatch = false;

                    check (coloursMatch,
                           NI_TXT ("每个圆点都用它自己那个圆点的颜色（这一组有 4 种）"));

                    const auto feedbackEntryId = document.getProject().midiInput.groupSource.entryId;

                    // ...and the entry's own colour, when it has one, wins for all of
                    // them - exactly like the canvas.
                    document.applyEdit ("selftest midi group colour", [feedbackEntryId] (Project& project)
                    {
                        for (auto& entry : project.referenceLines)
                            if (entry.id == feedbackEntryId)
                                entry.colourIndex = 4;
                    });
                    pump();
                    midiPanel.setGroupForTest (2);
                    pump();

                    auto allEntryColour = true;

                    for (int i = 0; i < midiCanvas.getDotCountForTest(); ++i)
                        if (midiCanvas.getDotColourForTest (i) != theme::trackColour (4))
                            allEntryColour = false;

                    check (allEntryColour,
                           NI_TXT ("参考线状态条目自己设了颜色时，整组圆点都用那个颜色"));

                    // Put the group back exactly as it was.
                    document.applyEdit ("selftest midi group restore", [feedbackEntryId] (Project& project)
                    {
                        for (auto& entry : project.referenceLines)
                            if (entry.id == feedbackEntryId)
                                entry.colourIndex = -1;
                    });
                    pump();
                    midiPanel.setGroupForTest (2);
                    pump();

                    check (midiCanvas.getDotColourForTest (0) == theme::trackColour (0)
                               && midiCanvas.getDotColourForTest (3) == theme::trackColour (3),
                           NI_TXT ("还原之后又各用各的颜色"));
                }

                // ---- the other direction, and right-click -------------------
                {
                    const juce::ModifierKeys rightClick (juce::ModifierKeys::rightButtonModifier);

                    // Right click a key lets that key go.
                    const auto eKey = 64;   // E4, the third white key
                    const auto eCentre = midiCanvas.getKeyBoundsForTest (eKey).getCentre().toFloat();

                    midiCanvas.mouseDown (makeMouseEvent (midiCanvas, eCentre, eCentre, rightClick));
                    midiCanvas.mouseUp (makeMouseEvent (midiCanvas, eCentre, eCentre, rightClick));
                    pump();

                    check (! midiCanvas.isKeyBoundForTest (eKey),
                           NI_TXT ("右键琴键断开那一条"));

                    // ...and the opposite drag binds it again: press the dot, drop on
                    // the key (the direction that did not exist before). Dot 9 is E in
                    // the anchor's own octave (dots 7..13 are that octave).
                    const auto dot2 = midiCanvas.getDotBoundsForTest (9).getCentre().toFloat();

                    midiCanvas.mouseDown (makeMouseEvent (midiCanvas, dot2, dot2));
                    midiCanvas.mouseDrag (makeMouseEvent (midiCanvas, eCentre, dot2));
                    midiCanvas.mouseUp (makeMouseEvent (midiCanvas, eCentre, dot2));
                    pump();

                    check (midiCanvas.isKeyBoundForTest (eKey)
                               && std::abs (component.frequencyForMidiNote (eKey)
                                            - kMidiC4Hz * kMidiCMajorRatios[2]) < 1.0e-6,
                           NI_TXT ("从圆点拖到琴键也能绑（以前只有琴键 → 圆点这一个方向）"));

                    // Right click a dot lets go of every key bound to it (dot 7 = the
                    // anchor octave's C, which the C key is bound to).
                    const auto dot0 = midiCanvas.getDotBoundsForTest (7).getCentre().toFloat();
                    const auto keysOnDot0 = midiCanvas.getKeysBoundToDotForTest (7);

                    midiCanvas.mouseDown (makeMouseEvent (midiCanvas, dot0, dot0, rightClick));
                    midiCanvas.mouseUp (makeMouseEvent (midiCanvas, dot0, dot0, rightClick));
                    pump();

                    check (! keysOnDot0.empty()
                               && ! midiCanvas.isKeyBoundForTest (keysOnDot0.front()),
                           NI_TXT ("右键圆点把连到它上面的琴键松开（") 
                               + juce::String ((int) keysOnDot0.size()) + NI_TXT (" 个）"));

                    // Put the whole C major tile back for the checks below (binding to
                    // the anchor octave's dots: 7 + i).
                    for (int i = 0; i < 7; ++i)
                        midiCanvas.bindKeyForTest (60 + whiteSteps[i], 7 + i);

                    pump();

                    check (std::abs (component.frequencyForMidiNote (60) - kMidiC4Hz) < 1.0e-6
                               && component.frequencyForMidiNote (61) == 0.0,
                           NI_TXT ("重新连好之后又是 C 大调、黑键又是空的"));
                }

                // ---- 一个键连多个圆点，右键断掉其中的一条 / 全部 -----------
                {
                    const juce::ModifierKeys rightClick (juce::ModifierKeys::rightButtonModifier);

                    // The C key already has its own cable; hang the octave above's C on
                    // it as well (dot 14 = C5).
                    midiCanvas.bindKeyForTest (60, 14);
                    pump();

                    check (midiCanvas.getCableCountForTest (60) == 2
                               && document.getProject().midiInput.map.bindingCount (60) == 2,
                           NI_TXT ("一个钢琴键连上两个圆点（")
                               + juce::String (midiCanvas.getCableCountForTest (60))
                               + NI_TXT (" 条）"));

                    check (midiCanvas.getKeyFrequencyTextForTest (60)
                               == formatHz (kMidiC4Hz) + NI_TXT (" · ")
                                      + formatHz (kMidiC4Hz * 2.0),
                           NI_TXT ("这一行把两条线的频率一起写出来（「")
                               + midiCanvas.getKeyFrequencyTextForTest (60) + NI_TXT ("」）"));

                    // Pressing it sounds both: one voice per cable.
                    auto& chordAudio = component.getAudioEngine();
                    chordAudio.stop();
                    chordAudio.setPositionSeconds (0.0);

                    component.simulateMidiDeviceMessageForTest (
                        juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100));
                    pump();

                    check (component.getHeldMidiVoiceCountForTest() == 2
                               && component.getHeldMidiNoteCountForTest() == 1,
                           NI_TXT ("按下这个键，引擎响了两个声部（")
                               + juce::String (component.getHeldMidiVoiceCountForTest())
                               + NI_TXT (" 个）"));

                    component.simulateMidiDeviceMessageForTest (juce::MidiMessage::noteOff (1, 60));
                    pump();

                    check (component.getHeldMidiVoiceCountForTest() == 0
                               && component.getHeldMidiNoteCountForTest() == 0,
                           NI_TXT ("松开之后两个声部都不挂"));

                    // Right-click the lower cable: only that one line goes.
                    const auto lineX = midiCanvas.getWidth()
                                           - MidiInputCanvas::kGroupStripWidth + 24;
                    const auto keyRect60 = midiCanvas.getKeyBoundsForTest (60);
                    const auto cableMid = juce::Point<float> (
                        (float) ((MidiInputCanvas::kKeyStripWidth + lineX - 6) / 2),
                        (float) ((keyRect60.getCentreY()
                                  + midiCanvas.getDotBoundsForTest (7).getCentreY()) / 2));

                    check (midiCanvas.getCableNoteAtForTest (cableMid.toInt()) == 60,
                           NI_TXT ("两条线中间那一条认得出是哪个键的"));

                    midiCanvas.mouseDown (makeMouseEvent (midiCanvas, cableMid, cableMid, rightClick));
                    midiCanvas.mouseUp (makeMouseEvent (midiCanvas, cableMid, cableMid, rightClick));
                    pump();

                    check (midiCanvas.getCableCountForTest (60) == 1
                               && std::abs (component.frequencyForMidiNote (60)
                                            - kMidiC4Hz * 2.0) < 1.0e-6,
                           NI_TXT ("右键一条连线只断开那一条（还剩 ")
                               + juce::String (midiCanvas.getCableCountForTest (60))
                               + NI_TXT (" 条，留下的是上面那条）"));

                    // Right-click the key row itself: all of its cables go, including
                    // from the half of a black key's row its own body does not cover.
                    const auto keyCentre = keyRect60.getCentre().toFloat();

                    midiCanvas.mouseDown (makeMouseEvent (midiCanvas, keyCentre, keyCentre, rightClick));
                    midiCanvas.mouseUp (makeMouseEvent (midiCanvas, keyCentre, keyCentre, rightClick));
                    pump();

                    check (midiCanvas.getCableCountForTest (60) == 0,
                           NI_TXT ("右键琴键断开这个键剩下的连线"));

                    midiCanvas.bindKeyForTest (61, 7);   // C#4: a black key
                    pump();

                    const auto blackRow = midiCanvas.getKeyBoundsForTest (61);
                    const auto beyondBody = juce::Point<float> (84.0f, (float) blackRow.getCentreY());

                    midiCanvas.mouseDown (makeMouseEvent (midiCanvas, beyondBody, beyondBody, rightClick));
                    midiCanvas.mouseUp (makeMouseEvent (midiCanvas, beyondBody, beyondBody, rightClick));
                    pump();

                    check (midiCanvas.getCableCountForTest (61) == 0,
                           NI_TXT ("黑键右侧那半行右键也能断开（以前那里是空白）"));

                    // Put the C major tile back for the checks below.
                    for (int i = 0; i < 7; ++i)
                        midiCanvas.bindKeyForTest (60 + whiteSteps[i], 7 + i);

                    pump();

                    check (midiCanvas.getCableCountForTest (60) == 1,
                           NI_TXT ("重新连一遍之后 C 键又只有一条线"));
                }

                // ---- 右侧那一列：上下缩放与滑动 -----------------------------
                {
                    const juce::ModifierKeys noMods;

                    midiCanvas.resetGroupViewForTest();
                    pump();

                    const auto dotAtFit = midiCanvas.getDotBoundsForTest (7).getCentreY();
                    const auto spanBefore = midiCanvas.getDotBoundsForTest (0).getCentreY()
                                                - midiCanvas.getDotBoundsForTest (14).getCentreY();

                    check (midiCanvas.getGroupZoomForTest() == 1.0f
                               && midiCanvas.getGroupScrollForTest() == 0.0f,
                           NI_TXT ("一开始是「全览」：缩放 1、滑动 0"));

                    // Ctrl+wheel over the column zooms about the pointer, so the dot
                    // under the mouse stays put.
                    const auto columnCentre = juce::Point<float> (
                        (float) (midiCanvas.getWidth() - MidiInputCanvas::kGroupStripWidth / 2),
                        (float) (midiCanvas.getHeight() / 2));
                    const juce::MouseWheelDetails wheelUp { 0.0f, 0.5f, false, false, false };
                    const juce::ModifierKeys ctrl (juce::ModifierKeys::ctrlModifier);

                    midiCanvas.mouseWheelMove (
                        makeMouseEvent (midiCanvas, columnCentre, columnCentre, ctrl), wheelUp);
                    pump();

                    check (midiCanvas.getGroupZoomForTest() > 1.0f,
                           NI_TXT ("Ctrl+滚轮把右边那一列放大了（×")
                               + juce::String (midiCanvas.getGroupZoomForTest(), 2) + NI_TXT ("）"));

                    midiCanvas.resetGroupViewForTest();
                    midiCanvas.zoomGroupForTest (2.0f, (float) (midiCanvas.getHeight() / 2));
                    pump();

                    const auto spanAfter = midiCanvas.getDotBoundsForTest (0).getCentreY()
                                               - midiCanvas.getDotBoundsForTest (14).getCentreY();

                    check (std::abs (midiCanvas.getGroupZoomForTest() - 2.0f) < 0.001f
                               && std::abs (spanAfter - spanBefore * 2) <= 4,
                           NI_TXT ("放大两倍之后两个圆点的间距也翻倍（")
                               + juce::String (spanBefore) + NI_TXT (" → ")
                               + juce::String (spanAfter) + NI_TXT (" px）"));

                    // A plain wheel over the column slides it (48 px a notch)...
                    const auto dotBefore = midiCanvas.getDotBoundsForTest (7).getCentreY();

                    midiCanvas.mouseWheelMove (
                        makeMouseEvent (midiCanvas, columnCentre, columnCentre, noMods),
                        juce::MouseWheelDetails { 0.0f, 0.5f, false, false, false });
                    pump();

                    const auto dotAfter = midiCanvas.getDotBoundsForTest (7).getCentreY();

                    check (dotAfter > dotBefore
                               && std::abs ((dotAfter - dotBefore) - 24) <= 4,
                           NI_TXT ("滚轮往上滑，右边的圆点跟着往下走（看到更高的音，")
                               + juce::String (dotBefore) + NI_TXT (" → ")
                               + juce::String (dotAfter) + NI_TXT (" px）"));

                    // ...and it can never be scrolled off the canvas.
                    midiCanvas.scrollGroupForTest (-1000.0f);
                    pump();

                    const auto reach = midiCanvas.getHeight() * 0.5f
                                           * (midiCanvas.getGroupZoomForTest() - 1.0f);

                    check (std::abs (std::abs (midiCanvas.getGroupScrollForTest()) - reach) <= 1.0f,
                           NI_TXT ("滑到底就停在边缘（")
                               + juce::String (midiCanvas.getGroupScrollForTest(), 1)
                               + NI_TXT (" / 上限 ") + juce::String (reach, 1) + NI_TXT (" px）"));

                    // 双击那一列 = 回到全览（走真实的 double-click 入口）。
                    midiCanvas.mouseDoubleClick (
                        makeMouseEvent (midiCanvas, columnCentre, columnCentre));
                    pump();

                    check (midiCanvas.getGroupZoomForTest() == 1.0f
                               && midiCanvas.getGroupScrollForTest() == 0.0f
                               && midiCanvas.getDotBoundsForTest (7).getCentreY() == dotAtFit,
                           NI_TXT ("双击那一列就回到全览，圆点回到原来的高度"));
                }

                // ---- 应用: teach the anchor with "a key, then a MIDI key" ---
                midiPanel.triggerApplyForTest();
                pump();

                check (midiPanel.isLearningAnchor(),
                       NI_TXT ("点「应用」进入学习态（状态行：")
                           + midiPanel.getStatusTextForTest() + NI_TXT ("）"));

                // Pressing a MIDI key before the left-hand one says which half of the
                // gesture is still missing (instead of doing nothing at all).
                component.simulateMidiDeviceMessageForTest (
                    juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100));
                pump();

                check (midiPanel.getStatusTextForTest().contains (NI_TXT ("先点一个左侧琴键")),
                       NI_TXT ("先按 MIDI 键会被提示（状态行：")
                           + midiPanel.getStatusTextForTest() + NI_TXT ("）"));
                check (document.getProject().midiInput.map.anchorNote == 60,
                       NI_TXT ("这一下没有偷偷改锚点（还是 ")
                           + juce::String (document.getProject().midiInput.map.anchorNote)
                           + NI_TXT ("）"));

                component.simulateMidiDeviceMessageForTest (juce::MidiMessage::noteOff (1, 60));
                pump();

                // The user's flow: click the C key, then press C on the MIDI keyboard.
                const auto cKeyCentre = midiCanvas.getKeyBoundsForTest (60).getCentre().toFloat();

                midiCanvas.mouseDown (makeMouseEvent (midiCanvas, cKeyCentre, cKeyCentre));
                midiCanvas.mouseUp (makeMouseEvent (midiCanvas, cKeyCentre, cKeyCentre));
                pump();

                check (midiCanvas.getLearningKey() == 60,
                       NI_TXT ("先点了左侧的 C4 键"));

                component.simulateMidiDeviceMessageForTest (
                    juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100));
                pump();

                check (! midiPanel.isLearningAnchor(),
                       NI_TXT ("再按一个 MIDI 键，锚点就学会了"));
                check (document.getProject().midiInput.map.anchorNote == 60,
                       NI_TXT ("锚点 = 按下的音 − 琴键在格里的序号（60 − 0，实际 ")
                           + juce::String (document.getProject().midiInput.map.anchorNote)
                           + NI_TXT ("）"));

                const auto statusAfterLearn = midiPanel.getStatusTextForTest();

                check (statusAfterLearn.contains (NI_TXT ("锚点 C4"))
                           && statusAfterLearn.contains (NI_TXT ("已绑 7 个"))
                           && statusAfterLearn.contains (NI_TXT ("空 5 个")),
                       NI_TXT ("状态行一次说清：锚点 / 已绑几个 / 空几个（「")
                           + statusAfterLearn + NI_TXT ("」）"));
                check (midiCanvas.getKeyCaptionForTest (60) == juce::String ("C4")
                           && statusAfterLearn.contains (NI_TXT ("一格 12 个半音")),
                       NI_TXT ("状态行也写明了这一格有多长"));

                component.simulateMidiDeviceMessageForTest (juce::MidiMessage::noteOff (1, 60));
                pump();

                // A non-zero step: click D4 and press D5 - the whole tile moves up an
                // octave, so C lands on C5.
                midiPanel.triggerApplyForTest();
                pump();

                const auto dKeyCentre = midiCanvas.getKeyBoundsForTest (62).getCentre().toFloat();

                midiCanvas.mouseDown (makeMouseEvent (midiCanvas, dKeyCentre, dKeyCentre));
                midiCanvas.mouseUp (makeMouseEvent (midiCanvas, dKeyCentre, dKeyCentre));
                pump();

                component.simulateMidiDeviceMessageForTest (
                    juce::MidiMessage::noteOn (1, 74, (juce::uint8) 100));
                pump();
                component.simulateMidiDeviceMessageForTest (juce::MidiMessage::noteOff (1, 74));
                pump();

                check (document.getProject().midiInput.map.anchorNote == 72,
                       NI_TXT ("点 D4、按 D5 之后锚点整体上移一个八度（74 − 2 = 72，实际 ")
                           + juce::String (document.getProject().midiInput.map.anchorNote)
                           + NI_TXT ("）"));
                // 应用 说的是「这一格从这个键开始」：锚点自己就是这一格的第一个音
                // （还是那一组自己的频率），再往上一个八度才是它的两倍。
                checkNear (component.frequencyForMidiNote (72), kMidiC4Hz, 1.0e-6,
                           NI_TXT ("锚点落到 72 之后，72 就是这一格的第一个音（还是 ")
                               + juce::String (kMidiC4Hz, 3) + NI_TXT (" Hz）"));
                checkNear (component.frequencyForMidiNote (84), kMidiC4Hz * 2.0, 1.0e-6,
                           NI_TXT ("再往上一个八度才是它的两倍（C6 = 两倍的 C4）"));

                // Put the anchor back on C4 for the playing checks below.
                midiPanel.triggerApplyForTest();
                pump();
                midiCanvas.mouseDown (makeMouseEvent (midiCanvas, cKeyCentre, cKeyCentre));
                midiCanvas.mouseUp (makeMouseEvent (midiCanvas, cKeyCentre, cKeyCentre));
                pump();
                component.simulateMidiDeviceMessageForTest (
                    juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100));
                pump();
                component.simulateMidiDeviceMessageForTest (juce::MidiMessage::noteOff (1, 60));
                pump();

                check (document.getProject().midiInput.map.anchorNote == 60,
                       NI_TXT ("再对齐一次，锚点回到 C4（实际 ")
                           + juce::String (document.getProject().midiInput.map.anchorNote)
                           + NI_TXT ("）"));

                // ---- playing through the device ---------------------------
                // G4 - a white key, so it is one of the seven the tile holds.
                const auto playNote = 67;

                auto& midiAudio = component.getAudioEngine();
                midiAudio.stop();
                midiAudio.setPositionSeconds (0.0);
                midiAudio.readTrackMeter (midiTrackId);   // clear whatever is stored

                component.simulateMidiDeviceMessageForTest (
                    juce::MidiMessage::noteOn (1, playNote, (juce::uint8) 120));
                pump();

                check (component.getHeldMidiNoteCountForTest() == 1,
                       NI_TXT ("MIDI 键按下：引擎收到一个正在响的实时音符（")
                           + juce::String (component.getHeldMidiNoteCountForTest())
                           + NI_TXT (" 个）"));

                auto midiPeak = 0.0f;

                for (int i = 0; i < 8; ++i)
                {
                    midiAudio.renderBlockForTest (512);
                    const auto reading = midiAudio.readTrackMeter (midiTrackId);
                    midiPeak = juce::jmax (midiPeak, reading.peakLeft, reading.peakRight);
                }

                check (midiPeak > 0.001f,
                       NI_TXT ("走带停着也有声音：这条轨的电平表读到 ") + juce::String (midiPeak, 4));

                component.simulateMidiDeviceMessageForTest (
                    juce::MidiMessage::noteOff (1, playNote));
                pump();

                check (component.getHeldMidiNoteCountForTest() == 0,
                       NI_TXT ("松开之后就不挂了"));

                // ---- 钢琴窗模式 = 十二平均律 -------------------------------
                document.applyDerivedEdit ([] (Project& project)
                {
                    project.view.editorMode = EditorMode::piano;
                });
                pump();

                checkNear (component.frequencyForMidiNote (70), 466.163757, 0.01,
                           NI_TXT ("钢琴窗模式下 A#4 走十二平均律（466.16 Hz），映射暂时让位"));
                check (midiPanel.getStatusTextForTest().contains (NI_TXT ("十二平均律")),
                       NI_TXT ("状态行也这么写（「") + midiPanel.getStatusTextForTest()
                           + NI_TXT ("」）"));

                document.applyDerivedEdit ([] (Project& project)
                {
                    project.view.editorMode = EditorMode::reference;
                });
                pump();

                checkNear (component.frequencyForMidiNote (playNote),
                           kMidiC4Hz * kMidiCMajorRatios[4], 1.0e-6,
                           NI_TXT ("切回参考线模式，映射又回来了（G4 还是那个 G4）"));

                // ---- 非循环模式 -------------------------------------------
                midiPanel.setModeForTest (false);
                pump();

                check ((int) midiCanvas.getVisibleNotesForTest().size() == 128,
                       NI_TXT ("非循环模式显示全部 128 个琴键（实际 ")
                           + juce::String ((int) midiCanvas.getVisibleNotesForTest().size())
                           + NI_TXT (" 个）"));

                midiCanvas.bindKeyForTest (30, 0);
                pump();

                check (std::abs (document.getProject().midiInput.map.frequencyFor (30)
                                 - midiCanvas.getDotFrequencyForTest (0)) < 1.0e-6
                           && document.getProject().midiInput.map.frequencyFor (42) == 0.0,
                       NI_TXT ("非循环里每个键各管各的（30 绑上了，42 还是空的）"));

                // The wheel over the *left* strip still scrolls the keys: the group
                // column's own view must not have stolen it.
                {
                    const auto stripPoint = juce::Point<float> (
                        40.0f, (float) (midiCanvas.getHeight() / 2));
                    const auto rowBefore = midiCanvas.getKeyBoundsForTest (30).getY();

                    midiCanvas.mouseWheelMove (
                        makeMouseEvent (midiCanvas, stripPoint, stripPoint),
                        juce::MouseWheelDetails { 0.0f, 0.5f, false, false, false });
                    pump();

                    check (midiCanvas.getKeyBoundsForTest (30).getY() != rowBefore,
                           NI_TXT ("左列琴键的滚轮照旧管用（")
                               + juce::String (rowBefore) + NI_TXT (" → ")
                               + juce::String (midiCanvas.getKeyBoundsForTest (30).getY())
                               + NI_TXT (" px）"));
                }

                midiPanel.setLoopKeysForTest (5);   // hmm, this switches nothing; keep looping for later?

                // ---- presets ----------------------------------------------
                midiPanel.setModeForTest (true);
                pump();
                midiPanel.setLoopKeysForTest (5);
                pump();

                check (document.getProject().midiInput.map.loopKeys == 5,
                       NI_TXT ("循环键数改成 5"));

                midiPanel.saveAsForTest (NI_TXT ("自检映射"));
                pump();

                check (midiPanel.getPresetCountForTest() == 1
                           && midiPanel.getPresetNameForTest() == NI_TXT ("自检映射"),
                       NI_TXT ("另存为存下了这张映射（「") + midiPanel.getPresetNameForTest()
                           + NI_TXT ("」）"));

                // ---- 改名… -------------------------------------------------
                check (! midiPanel.getRenameButtonBoundsForTest().isEmpty(),
                       NI_TXT ("MIDI 窗口里有一个「改名…」按钮（")
                           + midiPanel.getRenameButtonBoundsForTest().toString() + NI_TXT ("）"));

                midiPanel.renamePresetForTest (NI_TXT ("改过名的映射"));
                pump();

                check (midiPanel.getPresetNameForTest() == NI_TXT ("改过名的映射")
                           && midiPanel.getPresetCountForTest() == 1
                           && component.getPresetStore().getMidiMaps().front().presetName
                                  == NI_TXT ("改过名的映射"),
                       NI_TXT ("改名把库里的那条与工程里记的名字一起改了（「")
                           + midiPanel.getPresetNameForTest() + NI_TXT ("」）"));

                // A name that is only whitespace changes nothing.
                midiPanel.renamePresetForTest (NI_TXT ("   "));
                pump();

                check (midiPanel.getPresetNameForTest() == NI_TXT ("改过名的映射"),
                       NI_TXT ("空名字被忽略"));

                midiPanel.setLoopKeysForTest (9);
                pump();
                midiPanel.loadPresetForTest (0);
                pump();

                check (document.getProject().midiInput.map.loopKeys == 5,
                       NI_TXT ("载入预设把它整份拿回来（键数又回到 5）"));

                // Put the window back to the mapping the picture should show: one
                // octave, the seven white keys, the C major group.
                midiPanel.setLoopKeysForTest (12);
                pump();

                const auto midiImage = snapshot (midiPanel);

                check (writeImage (midiImage, outputDirectory.getChildFile ("46-midi-input.png")),
                       NI_TXT ("MIDI 输入窗口快照"));

                // ---- 装不下就绑不了 ------------------------------------------
                {
                    // Give the group more dots than one tile has steps. The extra ones
                    // are drawn grey and cannot be dropped on, and the hint says how
                    // to get them back (make the tile longer).
                    const auto tileEntryId = document.getProject().midiInput.groupSource.entryId;
                    std::vector<ReferenceDot> sevenDots;

                    document.applyEdit ("selftest midi overfull group", [tileEntryId, &sevenDots] (Project& project)
                    {
                        for (auto& entry : project.referenceLines)
                            if (entry.id == tileEntryId && entry.kind == ReferenceLineKind::group)
                            {
                                sevenDots = entry.dots;

                                for (int i = 0; i < 7; ++i)
                                    entry.dots.push_back ({ music::ratioToPosition (1.0 + 0.04 * (i + 1)),
                                                            i % 4 });
                            }
                    });
                    pump();
                    midiPanel.setGroupForTest (2);
                    pump();

                    // 14 dots an octave × 3 octaves = 42 drawn; one tile only holds 12
                    // of them (per octave), so 36 can be bound.
                    check (midiCanvas.getDotCountForTest() == 42
                               && midiCanvas.getBindableDotCountForTest() == 36,
                           NI_TXT ("圆点比一个格还多：三个八度共 42 个点，只有 36 个装得下（实际 ")
                               + juce::String (midiCanvas.getBindableDotCountForTest())
                               + NI_TXT (" 个）"));
                    check (! midiCanvas.isDotBindableForTest (12)
                               && ! midiCanvas.isDotBindableForTest (13)
                               && midiCanvas.isDotBindableForTest (11)
                               && midiCanvas.isDotBindableForTest (14)
                               && midiCanvas.getDotColourForTest (12) == theme::outline
                               && midiCanvas.getDotColourForTest (0) == theme::trackColour (0),
                           NI_TXT ("每一格里装不下的那几个画成灰的、也绑不了；装得下的还是自己的颜色"));

                    const auto beyond = midiCanvas.getDotBoundsForTest (13).getCentre();
                    const auto target = midiCanvas.getDropDotForTest (beyond);

                    check (target >= 0 && midiCanvas.isDotBindableForTest (target),
                           NI_TXT ("往装不下的圆点上拖，落点会被挪到装得下的那一个（第 ")
                               + juce::String (target) + NI_TXT (" 个）"));

                    // Put the group back: seven dots again.
                    document.applyEdit ("selftest midi group restore", [tileEntryId, &sevenDots] (Project& project)
                    {
                        for (auto& entry : project.referenceLines)
                            if (entry.id == tileEntryId)
                                entry.dots = sevenDots;
                    });
                    pump();
                    midiPanel.setGroupForTest (2);
                    pump();
                }

                midiPanel.deletePresetForTest();
                pump();

                check (midiPanel.getPresetCountForTest() == 0,
                       NI_TXT ("删除按钮把那条预设删掉"));

                midiWin->setVisible (false);
            }

            // ---- writing a note makes a sound ------------------------------
            {
                auto& writeGrid = component.getEditorPanel().getGrid();
                auto& writeAudio = component.getAudioEngine();
                const auto writeTrackId = midiTrackId;

                writeAudio.stop();
                writeAudio.setPositionSeconds (0.0);
                writeAudio.readTrackMeter (writeTrackId);

                document.viewState().centreHz = 220.0;
                document.viewState().octavesVisible = 4.0;
                document.viewState().pixelsPerQuarter = 140.0;
                document.viewState().horizontalOffsetTicks = 0.0;
                writeGrid.refresh();
                pump();

                // Write on one of the reference group's own lines: in reference mode
                // a press in empty space is a rubber band, not a note.
                const auto grab = writeGrid.pointFor (kMidiC4Hz * kMidiCMajorRatios[4], 480);
                const auto end = juce::Point<float> (grab.x + 70.0f, grab.y);

                writeGrid.mouseDown (makeMouseEvent (writeGrid, grab, grab));
                writeGrid.mouseDrag (makeMouseEvent (writeGrid, end, grab));
                writeGrid.mouseUp (makeMouseEvent (writeGrid, end, grab));
                pump();

                auto writePeak = 0.0f;

                for (int i = 0; i < 8; ++i)
                {
                    writeAudio.renderBlockForTest (512);
                    const auto reading = writeAudio.readTrackMeter (writeTrackId);
                    writePeak = juce::jmax (writePeak, reading.peakLeft, reading.peakRight);
                }

                check (writePeak > 0.001f,
                       NI_TXT ("写一个音符也会响一下（这条轨电平表读到 ")
                           + juce::String (writePeak, 4) + NI_TXT ("）"));
            }
        }

        beginTest (NI_TXT ("虚拟键盘窗口：点键 / 电脑键盘 / 与硬件同一条路（第四十七批）"));

        {
            juce::int64 kbTrackId = 0;

            document.applyEdit ("selftest virtual keyboard setup", [&kbTrackId] (Project& project)
            {
                project.tracks.clear();
                project.referenceLines.clear();
                project.nextTrackId = 1;
                project.loop.enabled = false;
                project.midiInput = {};

                kbTrackId = addTrack (project, TrackKind::synth);

                auto& clip = project.findTrack (kbTrackId)->clips.front();
                clip.startTick = 0;
                clip.lengthTicks = 3840;
                clip.loopLengthTicks = 3840;
                clip.notes.clear();

                // The C major group again: seven dots, one octave.
                ReferenceLineGroup group;
                group.name = NI_TXT ("C 大调");
                group.anchorHz = kMidiC4Hz;
                group.anchorPosition = 0.0;

                for (int i = 0; i < 7; ++i)
                    group.dots.push_back ({ music::ratioToPosition (kMidiCMajorRatios[i]), i % 4 });

                auto entry = music::makeGroupEntry (project.nextReferenceLineId++, group);
                entry.name = group.name;
                entry.locked = true;
                project.referenceLines.push_back (std::move (entry));

                // The seven white keys onto the group: D4 then plays the group's
                // third dot, which is exactly where the mapping differs from twelve
                // tone equal temperament - so the two modes can be told apart.
                project.midiInput.map.mode = midimap::Map::Mode::looping;
                project.midiInput.map.anchorNote = 60;
                midimap::setLoopKeys (project.midiInput.map, 12);

                const int whiteSteps[7] = { 0, 2, 4, 5, 7, 9, 11 };

                for (int i = 0; i < 7; ++i)
                    project.midiInput.map.setFrequency (60 + whiteSteps[i],
                                                        kMidiC4Hz * kMidiCMajorRatios[i]);
            });
            pump();

            appState.selectTrack (kbTrackId);
            appState.selectClip (document.getProject().findTrack (kbTrackId)->clips.front().id);
            appState.setEditorOpen (true);
            appState.setMidiRecordArmed (false);
            document.viewState().editorMode = EditorMode::reference;
            pump();

            component.openVirtualKeyboardWindow();
            pump();

            auto* keyboardWin = component.getVirtualKeyboardWindowForTest();

            check (keyboardWin != nullptr && keyboardWin->isVisible()
                       && keyboardWin->getBounds().getWidth() > 0
                       && keyboardWin->getBounds().getHeight() > 0,
                   keyboardWin != nullptr
                       ? (NI_TXT ("「虚拟键盘」开出了一个有真实尺寸的窗口（")
                              + keyboardWin->getBounds().toString() + NI_TXT ("）"))
                       : NI_TXT ("虚拟键盘窗口没开出来"));

            if (keyboardWin != nullptr)
            {
                auto& kb = keyboardWin->getPanel();

                check (kb.getKeyboardBoundsForTest().getWidth() > 200
                           && kb.getKeyboardBoundsForTest().getHeight() >= 60,
                       NI_TXT ("88 键的键盘条排下来了（")
                           + kb.getKeyboardBoundsForTest().toString() + NI_TXT ("）"));
                check (! kb.getVelocityBoundsForTest().isEmpty()
                           && ! kb.getDiagramBoundsForTest().isEmpty()
                           && ! kb.getArpBoundsForTest().isEmpty(),
                       NI_TXT ("力度 / 电脑键盘图示 / 琶音这几块都有真实尺寸（图示 ")
                           + kb.getDiagramBoundsForTest().toString() + NI_TXT ("）"));
                check (! kb.getBassOctaveBoundsForTest (0).isEmpty()
                           && ! kb.getBassOctaveBoundsForTest (1).isEmpty(),
                       NI_TXT ("低音排的两个八度按钮（-12 / +12）排在第二行里（")
                           + kb.getBassOctaveBoundsForTest (0).toString() + NI_TXT ("）"));
                check (kb.typingNoteForTest ('Z') == 60 && kb.typingNoteForTest ('S') == 61
                           && kb.typingNoteForTest ('Q') == 72,
                       NI_TXT ("常规布局的键位表对得上（Z=C4、S=C#4、Q=高一个八度）"));

                // ---- 点一个键: it goes through the mapping --------------------
                // A4 is the clearest witness: the group's 5:3 dot is 436.04 Hz,
                // twelve tone equal temperament is 440 Hz.
                kb.pressKeyForTest (69, 100);
                pump();

                const auto mapped = component.getLastLiveNoteForTest();

                check (component.getHeldMidiNoteCountForTest() == 1 && mapped.midiNote == 69,
                       NI_TXT ("点一下虚拟键盘真的按下了一个音"));
                check (std::abs (mapped.frequencyHz - kMidiC4Hz * kMidiCMajorRatios[5]) < 1.0e-6,
                       NI_TXT ("虚拟键盘走的是 MIDI 映射（A4 = ")
                           + juce::String (mapped.frequencyHz, 3) + NI_TXT (" Hz）"));
                check (kb.isKeyDownForTest (69), NI_TXT ("按下的那个键在窗口里亮着"));

                kb.releaseKeyForTest (69);
                pump();

                check (component.getHeldMidiNoteCountForTest() == 0 && ! kb.isKeyDownForTest (69),
                       NI_TXT ("松开就放掉了，也不挂音"));

                // ---- 真鼠标路径：点钢琴键真的出声（电平表说了算）----------------
                {
                    auto& kbAudio = component.getAudioEngine();

                    kbAudio.stop();
                    kbAudio.setPositionSeconds (0.0);
                    kbAudio.readTrackMeter (kbTrackId);
                    pump();

                    kb.pressKeyWithMouseForTest (60);   // C4: bound in this mapping
                    pump();

                    auto mousePeak = 0.0f;

                    for (int i = 0; i < 8; ++i)
                    {
                        kbAudio.renderBlockForTest (512);
                        const auto reading = kbAudio.readTrackMeter (kbTrackId);
                        mousePeak = juce::jmax (mousePeak, reading.peakLeft, reading.peakRight);
                    }

                    check (mousePeak > 0.001f,
                           NI_TXT ("用鼠标点钢琴键，这一轨的电平表真的动了（峰值 ")
                               + juce::String (mousePeak, 4) + NI_TXT ("）"));

                    kb.releaseKeyWithMouseForTest (60);
                    pump();

                    check (component.getHeldMidiNoteCountForTest() == 0
                               && component.getHeldMidiVoiceCountForTest() == 0,
                           NI_TXT ("松开鼠标那一刻 note-off 就到了引擎（不留挂音）"));

                    // The sound itself rings out: the default patch has a real release,
                    // so this looks at the tail rather than the instant after the click.
                    auto tailPeak = 0.0f;

                    for (int i = 0; i < 120; ++i)
                    {
                        kbAudio.renderBlockForTest (512);
                        const auto reading = kbAudio.readTrackMeter (kbTrackId);

                        if (i >= 112)
                            tailPeak = juce::jmax (tailPeak, reading.peakLeft, reading.peakRight);
                    }

                    check (tailPeak < mousePeak * 0.25f,
                           NI_TXT ("松手之后余音会衰下去（收尾峰值 ")
                               + juce::String (tailPeak, 4) + NI_TXT (" 对按下的 ")
                               + juce::String (mousePeak, 4) + NI_TXT ("）"));
                }

                check (keyboardWin->isPanelOnKeyListenersForTest(),
                       NI_TXT ("面板挂在窗口的按键监听上（焦点落在窗口本身时也能弹）"));

                // ---- 钢琴窗模式: the same key is twelve tone equal temperament --
                document.viewState().editorMode = EditorMode::piano;
                pump();

                kb.pressKeyForTest (69, 100);
                pump();

                check (std::abs (component.getLastLiveNoteForTest().frequencyHz
                                 - 440.0) < 1.0e-6,
                       NI_TXT ("钢琴窗模式下同一个键走十二平均律（")
                           + juce::String (component.getLastLiveNoteForTest().frequencyHz, 3)
                           + NI_TXT (" Hz，而不是映射的 ")
                           + juce::String (kMidiC4Hz * kMidiCMajorRatios[5], 3) + NI_TXT (" Hz）"));

                kb.releaseKeyForTest (69);
                document.viewState().editorMode = EditorMode::reference;
                pump();

                // ---- the velocity control decides how hard it is struck --------
                kb.setVelocityForTest (64.0);
                kb.pressKeyForTest (60, kb.getVelocityForTest());
                pump();

                check (std::abs (component.getLastLiveNoteForTest().velocity - 64.0f) < 0.5f,
                       NI_TXT ("力度控件决定发出去的力度（实际 ")
                           + juce::String (component.getLastLiveNoteForTest().velocity, 1)
                           + NI_TXT ("）"));

                kb.releaseKeyForTest (60);
                pump();

                // ---- a hardware key lights up here too -------------------------
                component.simulateMidiDeviceMessageForTest (
                    juce::MidiMessage::noteOn (1, 67, (juce::uint8) 100));
                pump();

                check (kb.isKeyDownForTest (67),
                       NI_TXT ("硬件键盘弹的音在虚拟键盘上也亮着"));

                component.simulateMidiDeviceMessageForTest (juce::MidiMessage::noteOff (1, 67));
                pump();

                check (! kb.isKeyDownForTest (67), NI_TXT ("松开以后就不亮了"));

                // ---- 88 keys in view -------------------------------------------
                check (kb.getLowestVisibleKeyForTest() == keyboard::kLowestNote,
                       NI_TXT ("88 键一次看全（最低可见键 = A0，实际 ")
                           + juce::String (kb.getLowestVisibleKeyForTest()) + NI_TXT ("）"));

                // ---- 钢琴条变窄 + 打开窗口就能直接弹 ---------------------------
                check (kb.getKeyboardBoundsForTest().getHeight() <= 100
                           && keyboardWin->getHeight() <= 380,
                       NI_TXT ("钢琴条收窄了（高 ")
                           + juce::String (kb.getKeyboardBoundsForTest().getHeight())
                           + NI_TXT (" px，窗口 ") + juce::String (keyboardWin->getHeight())
                           + NI_TXT (" px）"));
                check (kb.getWantsKeyboardFocus(),
                       NI_TXT ("面板自己要键盘焦点：窗口一激活就把焦点拿过来（离屏运行里没有真焦点，"
                               "所以这里只能断言它愿意接）"));

                // The computer keys play straight from the layout table: no click on
                // the piano, and the note really reaches the engine.
                kb.setKeyDownForTest ('Z', true);
                pump();

                check (component.getHeldMidiVoiceCountForTest() > 0
                           && component.getHeldMidiNoteCountForTest() == 1
                           && kb.getTypingKeyCountForTest() == 1,
                       NI_TXT ("直接敲电脑键就出声（没有点过钢琴键；引擎里 ")
                           + juce::String (component.getHeldMidiVoiceCountForTest())
                           + NI_TXT (" 个声部）"));

                // ---- z / x / c 一起按：三个音都要出来 ---------------------------
                // The report this guards: holding three keys gives two notes. If the
                // system only reports two of them, the keyboard's own key-rolling is
                // the limit, and the status line below names what it did report.
                kb.setKeyDownForTest ('X', true);
                kb.setKeyDownForTest ('C', true);
                pump();

                check (component.getHeldMidiVoiceCountForTest() == 3
                           && component.getHeldMidiNoteCountForTest() == 3
                           && kb.getTypingKeyCountForTest() == 3,
                       NI_TXT ("z / x / c 一起按就是三个音（实际 ")
                           + juce::String (component.getHeldMidiVoiceCountForTest())
                           + NI_TXT (" 个声部）"));
                check (kb.getTypingKeysTextForTest().contains ("Z")
                           && kb.getTypingKeysTextForTest().contains ("X")
                           && kb.getTypingKeysTextForTest().contains ("C")
                           && kb.getStatusForTest().contains (kb.getTypingKeysTextForTest()),
                       NI_TXT ("状态行列出系统真正报告按下的键（「")
                           + kb.getTypingKeysTextForTest() + NI_TXT ("」）"));

                kb.setKeyDownForTest ('X', false);
                pump();

                check (component.getHeldMidiVoiceCountForTest() == 2
                           && kb.getTypingKeyCountForTest() == 2,
                       NI_TXT ("松开其中一个，另外两个还响着（实际 ")
                           + juce::String (component.getHeldMidiVoiceCountForTest())
                           + NI_TXT (" 个声部）"));

                kb.setKeyDownForTest ('C', false);
                pump();

                check (component.getHeldMidiVoiceCountForTest() == 1
                           && kb.getTypingKeyCountForTest() == 1,
                       NI_TXT ("再松一个就只剩 z 那一个音"));

                // ---- 卡音：漏掉的 key-up 由心跳补上 ----------------------------
                kb.simulateMissedKeyUpForTest ('Z');   // the key-up never arrived

                check (component.getHeldMidiVoiceCountForTest() > 0
                           && kb.getTypingKeyCountForTest() == 1,
                       NI_TXT ("（假装那一次松键事件丢了：音还响着，窗口也以为它还按着）"));

                kb.runTypingWatchdogForTest();
                pump();

                check (component.getHeldMidiVoiceCountForTest() == 0
                           && kb.getTypingKeyCountForTest() == 0
                           && ! kb.isKeyDownForTest (60),
                       NI_TXT ("心跳发现键已经松开，把那个音放掉了（不再一直响）"));

                // The rule is about the *application*, not about one window: clicking
                // the synth editor (or the mixer) must not take the computer keys
                // away, so a patch can be auditioned with a knob under the mouse.
                kb.setApplicationForegroundForTest (true);

                check (kb.typingIsLiveForTest (&component),
                       NI_TXT ("焦点在主窗口时电脑键盘照样能弹（边调音色边试听）"));
                check (kb.typingIsLiveForTest (nullptr),
                       NI_TXT ("谁也不持有焦点时照旧能弹"));

                juce::TextEditor typingBox;   // stands in for a focused text field
                check (! kb.typingIsLiveForTest (&typingBox),
                       NI_TXT ("焦点在文本框里时不弹（打字优先）"));

                kb.setApplicationForegroundForTest (false);
                check (! kb.typingIsLiveForTest (&component),
                       NI_TXT ("整个程序切到后台时不弹"));
                kb.setApplicationForegroundForTest (true);

                // ... and a key that is still held is let go when HM goes behind
                // another application.
                kb.setKeyDownForTest ('X', true);

                check (component.getHeldMidiVoiceCountForTest() > 0,
                       NI_TXT ("再按一个键（D4）"));

                kb.setWindowFocusedForTest (false);
                kb.runTypingWatchdogForTest();
                pump();

                check (component.getHeldMidiVoiceCountForTest() == 0
                           && kb.getTypingKeyCountForTest() == 0,
                       NI_TXT ("整个程序切到后台时整批放掉（键还按着也不响）"));

                kb.setWindowFocusedForTest (true);
                kb.setKeyDownForTest ('X', false);   // the fake key is up again
                pump();

                // ---- 卡音：移位 / 换布局 / 关窗都要把音放掉 --------------------
                kb.pressKeyForTest (60, 100);
                pump();

                check (component.getHeldMidiVoiceCountForTest() > 0
                           && kb.getStartedNoteCountForTest() == 1,
                       NI_TXT ("按着一个键（引擎里 ")
                           + juce::String (component.getHeldMidiVoiceCountForTest())
                           + NI_TXT (" 个声部）"));

                kb.shiftMelodyForTest (12);
                pump();

                check (component.getHeldMidiVoiceCountForTest() == 0
                           && kb.getStartedNoteCountForTest() == 0
                           && ! kb.isKeyDownForTest (60),
                       NI_TXT ("换八度时手弹的音被放掉了（不再挂音）"));
                check (kb.typingNoteForTest ('Z') == 72,
                       NI_TXT ("同一个键在新八度上照旧有音（不是死键：Z = ")
                           + midimap::noteName (kb.typingNoteForTest ('Z')) + NI_TXT ("）"));

                kb.pressKeyForTest (72, 100);
                pump();

                check (component.getHeldMidiVoiceCountForTest() > 0,
                       NI_TXT ("新八度上按下同一个键真的出声"));

                kb.releaseKeyForTest (72);
                kb.shiftMelodyForTest (-12);
                pump();

                // ---- 巴扬布局：旋律排 -------------------------------------------
                kb.setLayoutForTest (2);
                pump();

                check (kb.getLayoutForTest() == keyboard::Layout::bayan,
                       NI_TXT ("切到巴扬布局"));
                check (kb.typingNoteForTest ('Q') == 57 && kb.typingNoteForTest ('W') == 60
                           && kb.typingNoteForTest ('X') == 62 && kb.typingNoteForTest ('/') == 86,
                       NI_TXT ("巴扬旋律排：q=A3、w=C4、x=D4、/ = D6（实际 w = ")
                           + midimap::noteName (kb.typingNoteForTest ('W')) + NI_TXT ("）"));

                // ---- 巴扬布局：低音排（上五度下四度严格交替）--------------------
                check (kb.bassNoteForTest ('5') == 53 && kb.bassNoteForTest ('6') == 60
                           && kb.bassNoteForTest ('4') == 58 && kb.bassNoteForTest ('7') == 55
                           && kb.bassNoteForTest (0x0060) == 54 && kb.bassNoteForTest (8) == 61,
                       NI_TXT ("低音排：5=F3、6=C4（上五度）、4=A#3、7=G3（下四度）、·=F#3、⌫=C#4（实际 ")
                           + midimap::noteName (kb.bassNoteForTest ('5')) + NI_TXT (" / ")
                           + midimap::noteName (kb.bassNoteForTest ('6')) + NI_TXT ("）"));
                check (kb.bassNoteForTest ('6') - kb.bassNoteForTest ('5') == 7
                           && kb.bassNoteForTest ('7') - kb.bassNoteForTest ('6') == -5
                           && kb.bassNoteForTest ('8') - kb.bassNoteForTest ('7') == 7
                           && kb.bassNoteForTest ('9') - kb.bassNoteForTest ('8') == -5,
                       NI_TXT ("低音排是 +7 / -5 交替（上五度、下四度、上五度…）"));

                auto bassOnThePiano = true;

                const int bassKeyCodes[] = { 0x0060, '1', '2', '3', '4', '5', '6', '7', '8',
                                             '9', '0', '-', '=', 8 };

                for (const auto key : bassKeyCodes)
                {
                    const auto note = kb.bassNoteForTest (key);

                    if (note < keyboard::kLowestNote || note > keyboard::kHighestNote)
                        bassOnThePiano = false;
                }

                check (bassOnThePiano, NI_TXT ("低音排 14 个键都在钢琴上"));

                // ---- 每来源独立：旋律的 x 与低音的 8 都是 D4 --------------------
                kb.setKeyDownForTest ('X', true);   // melody: D4
                kb.setKeyDownForTest ('8', true);   // bass: the same D4
                pump();

                check (kb.typingNoteForTest ('X') == 62 && kb.bassNoteForTest ('8') == 62,
                       NI_TXT ("旋律的 x 与低音的 8 指的是同一个音（D4）"));
                check (component.getHeldMidiVoiceCountForTest() == 2
                           && component.getHeldMidiNoteCountForTest() == 1,
                       NI_TXT ("同一个音被两条来源按着 → 各响一个声部（实际 ")
                           + juce::String (component.getHeldMidiVoiceCountForTest())
                           + NI_TXT (" 个声部、")
                           + juce::String (component.getHeldMidiNoteCountForTest())
                           + NI_TXT (" 个音）"));

                kb.setKeyDownForTest ('X', false);
                pump();

                check (component.getHeldMidiVoiceCountForTest() == 1,
                       NI_TXT ("先松开旋律那个键 → 还剩一个声部（低音的那个还在响）"));

                kb.setKeyDownForTest ('8', false);
                pump();

                check (component.getHeldMidiVoiceCountForTest() == 0,
                       NI_TXT ("两个都松开 → 全部放掉"));

                // ---- 两套移位各管各的 -------------------------------------------
                kb.shiftMelodyForTest (1);
                pump();

                check (kb.typingNoteForTest ('W') == 61 && kb.getMelodyShiftForTest() == 1,
                       NI_TXT ("旋律半音移位：w 从 C4 变成 ")
                           + midimap::noteName (kb.typingNoteForTest ('W')));

                kb.shiftMelodyForTest (12);
                pump();

                check (kb.typingNoteForTest ('W') == 73 && kb.getMelodyShiftForTest() == 13,
                       NI_TXT ("旋律八度移位：w = ")
                           + midimap::noteName (kb.typingNoteForTest ('W')));

                kb.shiftMelodyForTest (-13);
                pump();

                const auto melodyBeforeBass = kb.typingNoteForTest ('W');
                kb.shiftBassForTest (7);
                pump();

                check (kb.bassNoteForTest ('5') == 60 && kb.getBassShiftForTest() == 7,
                       NI_TXT ("低音五度移位：5 从 F3 变成 ")
                           + midimap::noteName (kb.bassNoteForTest ('5')));
                check (kb.typingNoteForTest ('W') == melodyBeforeBass,
                       NI_TXT ("低音移位不动旋律"));

                kb.shiftBassForTest (-7);
                pump();

                // ---- 低音的八度按钮：整排上下 12 个半音 -------------------------
                const auto bassFive = kb.bassNoteForTest ('5');

                kb.shiftBassForTest (12);
                pump();

                check (kb.bassNoteForTest ('5') == bassFive + 12
                           && kb.getBassShiftForTest() == 12
                           && kb.typingNoteForTest ('W') == melodyBeforeBass,
                       NI_TXT ("低音八度移位：整排往上 12 个半音（5 = ")
                           + midimap::noteName (kb.bassNoteForTest ('5'))
                           + NI_TXT ("，旋律不动）"));

                kb.shiftBassForTest (-24);
                pump();

                check (kb.bassNoteForTest ('5') == bassFive - 12 && kb.getBassShiftForTest() == -12,
                       NI_TXT ("往下一个八度的两下就是下面那个八度（5 = ")
                           + midimap::noteName (kb.bassNoteForTest ('5')) + NI_TXT ("）"));

                kb.shiftBassForTest (12);
                pump();

                // 顶到底：一头一尾整排都还在钢琴里，按钮就停住
                for (int i = 0; i < 64; ++i)
                    kb.shiftBassForTest (1);

                pump();
                check (kb.getBassShiftForTest() == keyboard::bassShiftRange().highest,
                       NI_TXT ("低音偏移顶到 +")
                           + juce::String (keyboard::bassShiftRange().highest)
                           + NI_TXT (" 就停住（整排还在钢琴上）"));

                for (int i = 0; i < 128; ++i)
                    kb.shiftBassForTest (-1);

                pump();
                check (kb.getBassShiftForTest() == keyboard::bassShiftRange().lowest,
                       NI_TXT ("另一头停在 ")
                           + juce::String (keyboard::bassShiftRange().lowest));

                kb.shiftBassForTest (-kb.getBassShiftForTest());

                pump();
                check (kb.getBassShiftForTest() == 0, NI_TXT ("再回到 0（两个偏移互不影响）"));

                // ---- 钢琴键上写着电脑键字母 -------------------------------------
                check (kb.getPianoLetterForTest (60).contains ("W")
                           && kb.getPianoLetterForTest (60).contains ("6"),
                       NI_TXT ("C4 上写着旋律排的 w 与低音排的 6（实际「")
                           + kb.getPianoLetterForTest (60) + NI_TXT ("」）"));
                check (kb.getPianoLetterForTest (59).contains ("Z")
                           && kb.getPianoLetterForTest (59).contains ("-"),
                       NI_TXT ("B3 上写着 z 与 -（实际「")
                           + kb.getPianoLetterForTest (59) + NI_TXT ("」）"));

                // ---- 电脑键盘图示：白键画白、黑键画黑 ---------------------------
                {
                    const auto bayanImage = snapshot (kb);
                    const auto whiteCaps = countColour (bayanImage, juce::Colour (0xffe6e6ee), 10);
                    const auto blackCaps = countColour (bayanImage, juce::Colour (0xff17171b), 10);

                    check (whiteCaps > 5000,
                           NI_TXT ("白键帽真的画成白的（像素 ")
                               + juce::String (whiteCaps) + NI_TXT (" 个）"));
                    check (blackCaps > 1500,
                           NI_TXT ("黑键帽真的画成黑的（像素 ")
                               + juce::String (blackCaps) + NI_TXT (" 个）"));

                    check (writeImage (bayanImage,
                                       outputDirectory.getChildFile ("49-virtual-keyboard.png")),
                           NI_TXT ("虚拟键盘快照（巴扬图示 + 钢琴上的字母 + 琶音面板）"));
                }

                // ---- 折叠成小窗 + 置顶 -----------------------------------------
                kb.toggleCollapseForTest();
                pump();

                check (kb.isCollapsedForTest() && keyboardWin->getHeight() <= 140,
                       NI_TXT ("折叠成小窗（窗口高 ")
                           + juce::String (keyboardWin->getHeight()) + NI_TXT ("）"));
                check (! kb.isComponentVisibleForTest (0) && ! kb.isComponentVisibleForTest (2)
                           && kb.isComponentVisibleForTest (1),
                       NI_TXT ("折叠后键盘条与琶音藏起来、电脑键盘图示还在"));
                check (! kb.getDiagramBoundsForTest().isEmpty(),
                       NI_TXT ("折叠时图示仍有真实尺寸（")
                           + kb.getDiagramBoundsForTest().toString() + NI_TXT ("）"));
                check (! kb.getBassOctaveBoundsForTest (0).isEmpty()
                           && ! kb.getBassOctaveBoundsForTest (1).isEmpty(),
                       NI_TXT ("折叠时两个八度按钮也还在（")
                           + kb.getBassOctaveBoundsForTest (1).toString() + NI_TXT ("）"));

                {
                    const auto compactImage = snapshot (kb);

                    check (writeImage (compactImage,
                                       outputDirectory.getChildFile ("50-virtual-keyboard-compact.png")),
                           NI_TXT ("折叠小窗快照"));
                }

                kb.toggleCollapseForTest();
                pump();

                check (! kb.isCollapsedForTest() && keyboardWin->getHeight() >= 340
                           && kb.isComponentVisibleForTest (0) && kb.isComponentVisibleForTest (2),
                       NI_TXT ("再点一下展开回来（窗口高 ")
                           + juce::String (keyboardWin->getHeight()) + NI_TXT ("）"));

                kb.togglePinForTest();
                pump();

                check (keyboardWin->isAlwaysOnTop(), NI_TXT ("「置顶」让窗口浮在最上层"));

                kb.togglePinForTest();
                pump();

                check (! keyboardWin->isAlwaysOnTop(), NI_TXT ("再点一下取消置顶"));

                // ---- 状态记进 keyboard.json ------------------------------------
                const auto savedText = keyboardWin->getSettingsFileTextForTest();
                const auto saved = keyboard::settingsFromVar (juce::JSON::parse (savedText));

                check (savedText.contains ("bassShift") && savedText.contains ("layout")
                           && saved.layout == keyboard::Layout::bayan
                           && saved.melodyShift == 0 && saved.bassShift == 0,
                       NI_TXT ("折叠 / 置顶 / 布局 / 两个偏移都写进了 keyboard.json"));

                // ---- 虚拟键盘开着时，1–6 不再选频率比 ---------------------------
                {
                    const auto usedWhileOpen = component.sendKeyPressForTest (
                        juce::KeyPress ('3'), &component);

                    check (! usedWhileOpen,
                           NI_TXT ("虚拟键盘开着时 `3` 不再被频率比接走"));
                }

                // ---- 虚拟键盘开着时，弹奏键不再被单键快捷键吃掉 ----------------
                check (kb.isTypingKey ('N')
                           && ! component.sendKeyPressForTest (juce::KeyPress ('N'), &component),
                       NI_TXT ("虚拟键盘开着时「n」只弹音，不再同时封参考线"));

                // ---- the space bar still belongs to the transport --------------
                check (! kb.keyPressed (juce::KeyPress (juce::KeyPress::spaceKey)),
                       NI_TXT ("虚拟键盘不吃空格，空格还是控制走带"));

                // ---- 关掉窗口：不留挂音，1–6 立刻回来 --------------------------
                kb.pressKeyForTest (64, 100);
                pump();

                keyboardWin->hide();
                pump();

                check (! keyboardWin->isVisible(), NI_TXT ("窗口收起来了"));
                check (component.getHeldMidiVoiceCountForTest() == 0
                           && kb.getStartedNoteCountForTest() == 0,
                       NI_TXT ("关窗时正按着的音被放掉了（不挂音）"));
                check (component.sendKeyPressForTest (juce::KeyPress ('3'), &component),
                       NI_TXT ("窗口关掉以后 `3` 又归频率比了"));

                component.openVirtualKeyboardWindow();
                pump();

                check (keyboardWin->isVisible(), NI_TXT ("再打开还在（复用同一个窗口）"));

                // Put it away before the scenes that follow: 1-6 belong to the 频率比
                // while it is closed, and those scenes use exactly those keys.
                keyboardWin->hide();
                pump();
            }
        }

        beginTest (NI_TXT ("琶音器：速率 / 方向 / 八度 / Gate / Swing / Hold / 录制（第四十七批）"));

        {
            juce::int64 arpTrackId = 0;
            juce::int64 arpClipId = 0;

            document.applyEdit ("selftest arp setup", [&arpTrackId, &arpClipId] (Project& project)
            {
                project.tracks.clear();
                project.nextTrackId = 1;
                project.nextNoteId = 1;
                project.loop.enabled = false;
                project.midiInput = {};

                arpTrackId = addTrack (project, TrackKind::synth);

                auto* track = project.findTrack (arpTrackId);
                track->name = NI_TXT ("琶音轨");

                auto& clip = track->clips.front();
                clip.startTick = 0;
                clip.lengthTicks = 3840;
                clip.loopLengthTicks = 3840;
                clip.notes.clear();
                arpClipId = clip.id;

                // A plain C major triad: 60 / 64 / 67.
                project.midiInput.map.mode = midimap::Map::Mode::looping;
                project.midiInput.map.anchorNote = 60;
                midimap::setLoopKeys (project.midiInput.map, 12);
                project.midiInput.map.setFrequency (60, 261.6255653);
                project.midiInput.map.setFrequency (64, 329.6275569);
                project.midiInput.map.setFrequency (67, 391.9954360);
            });
            pump();

            appState.selectTrack (arpTrackId);
            appState.selectClip (arpClipId);
            appState.setEditorOpen (true);
            appState.setMidiRecordIntoClip (true);
            appState.setMidiRecordArmed (false);
            document.viewState().editorMode = EditorMode::reference;
            pump();

            auto& arpAudio = component.getAudioEngine();
            arpAudio.stop();
            pump();

            auto arpSettings = component.getArpSettingsForTest();
            arpSettings.enabled = true;
            arpSettings.rate = midiarp::Rate::sixteenth;
            arpSettings.direction = midiarp::Direction::up;
            arpSettings.octaves = 1;
            arpSettings.gatePercent = 50;
            arpSettings.swingPercent = 0;
            arpSettings.velocityMode = midiarp::VelocityMode::played;
            arpSettings.hold = false;

            component.setArpClockForTest (0);
            component.setArpSettingsForTest (arpSettings);
            pump();

            check (component.getArpSettingsForTest().enabled,
                   NI_TXT ("琶音开关真的写进了工程（配置，不进撤销栈）"));

            // ---- a held chord becomes a phrase ---------------------------------
            for (const auto note : { 60, 64, 67 })
                component.simulateMidiDeviceMessageForTest (
                    juce::MidiMessage::noteOn (1, note, (juce::uint8) 100));

            pump();

            check (component.getHeldMidiNoteCountForTest() == 3,
                   NI_TXT ("三个键都按着"));
            check (component.getHeldMidiVoiceCountForTest() == 0,
                   NI_TXT ("琶音开着时按键本身不发声，由琶音来弹"));
            check (component.getArpOrderForTest() == std::vector<int> { 60, 64, 67 },
                   NI_TXT ("和弦按上行排成三个音"));

            // The clock is pinned, so the steps can be walked through one by one.
            const auto stepThrough = [&component] (juce::int64 ticks)
            {
                component.setArpClockForTest (ticks);
                component.arpTickForTest();
                return component.getArpSoundingNoteForTest();
            };

            const auto firstStep = stepThrough (0);
            const auto firstVoices = component.getArpVoiceCountForTest();
            const auto secondStep = stepThrough (240);
            const auto thirdStep = stepThrough (480);

            check (firstStep == 60 && secondStep == 64 && thirdStep == 67,
                   NI_TXT ("1/16 的格子上音符一个接一个往上走（")
                       + juce::String (firstStep) + NI_TXT (" → ") + juce::String (secondStep)
                       + NI_TXT (" → ") + juce::String (thirdStep) + NI_TXT ("）"));
            check (firstVoices == 1 && component.getArpFiredStepCountForTest() >= 3,
                   NI_TXT ("每一步都真的发了一个音（一共发了 ")
                       + juce::String (component.getArpFiredStepCountForTest())
                       + NI_TXT (" 步）"));

            // ---- 方向 / 八度 ---------------------------------------------------
            auto downSettings = arpSettings;
            downSettings.direction = midiarp::Direction::down;

            component.setArpClockForTest (960);
            component.setArpSettingsForTest (downSettings);
            pump();

            check (component.getArpOrderForTest() == std::vector<int> { 67, 64, 60 },
                   NI_TXT ("改成下行以后顺序倒过来"));
            check (stepThrough (960) == 67 && stepThrough (1200) == 64,
                   NI_TXT ("下行真的从最高的音开始弹"));

            auto twoOctaveSettings = arpSettings;
            twoOctaveSettings.octaves = 2;

            component.setArpClockForTest (0);
            component.setArpSettingsForTest (twoOctaveSettings);
            pump();

            check (component.getArpOrderForTest().size() == 6
                       && component.getArpOrderForTest()[3] == 72,
                   NI_TXT ("两个八度：一组接一组（")
                       + juce::String ((int) component.getArpOrderForTest().size())
                       + NI_TXT (" 个音）"));

            // ---- 速率: 1/16 takes twice as many steps as 1/8 --------------------
            const auto countSteps = [&component] (const midiarp::Settings& settings, int spanTicks)
            {
                component.setArpClockForTest (0);
                component.setArpSettingsForTest (settings);
                component.arpTickForTest();

                const auto before = component.getArpFiredStepCountForTest();

                for (int tick = 10; tick <= spanTicks; tick += 10)
                {
                    component.setArpClockForTest (tick);
                    component.arpTickForTest();
                }

                return component.getArpFiredStepCountForTest() - before;
            };

            auto eighthSettings = arpSettings;
            eighthSettings.rate = midiarp::Rate::eighth;
            auto sixteenthSettings = arpSettings;
            sixteenthSettings.rate = midiarp::Rate::sixteenth;

            const auto sixteenthSteps = countSteps (sixteenthSettings, 1920);
            const auto eighthSteps = countSteps (eighthSettings, 1920);

            check (sixteenthSteps == 8 && eighthSteps == 4,
                   NI_TXT ("一小节里 1/16 走 8 步、1/8 走 4 步（实际 ")
                       + juce::String (sixteenthSteps) + NI_TXT (" / ")
                       + juce::String (eighthSteps) + NI_TXT ("）"));

            // ---- Gate: how long each note sounds --------------------------------
            auto halfGate = sixteenthSettings;
            halfGate.gatePercent = 50;
            auto fullGate = sixteenthSettings;
            fullGate.gatePercent = 100;

            component.setArpClockForTest (0);
            component.setArpSettingsForTest (halfGate);
            stepThrough (0);

            component.setArpClockForTest (100);
            component.arpTickForTest();
            const auto stillOnWithHalfGate = component.getArpSoundingNoteForTest();

            component.setArpClockForTest (130);
            component.arpTickForTest();
            const auto offWithHalfGate = component.getArpSoundingNoteForTest();

            component.setArpClockForTest (0);
            component.setArpSettingsForTest (fullGate);
            stepThrough (0);

            component.setArpClockForTest (130);
            component.arpTickForTest();
            const auto stillOnWithFullGate = component.getArpSoundingNoteForTest();

            check (stillOnWithHalfGate == 60 && offWithHalfGate == -1,
                   NI_TXT ("Gate 50%：半格之后那个音就放掉了"));
            check (stillOnWithFullGate == 60,
                   NI_TXT ("Gate 100%：同一时刻它还响着"));

            // ---- Swing: the off-beats wait --------------------------------------
            auto swingSettings = sixteenthSettings;
            swingSettings.swingPercent = 50;

            component.setArpClockForTest (0);
            component.setArpSettingsForTest (swingSettings);
            component.arpTickForTest();

            const auto beforeSwing = component.getArpFiredStepCountForTest();

            component.setArpClockForTest (240);   // the off-beat, still waiting
            component.arpTickForTest();
            const auto atTheGrid = component.getArpFiredStepCountForTest() - beforeSwing;

            component.setArpClockForTest (361);   // past the swing offset
            component.arpTickForTest();
            const auto afterTheSwing = component.getArpFiredStepCountForTest() - beforeSwing;

            check (atTheGrid == 0 && afterTheSwing == 1,
                   NI_TXT ("Swing 把反拍推后了半格（正点上还没弹，推后以后才弹）"));

            // ---- Hold: let go and it keeps running ------------------------------
            auto holdSettings = sixteenthSettings;
            holdSettings.hold = true;

            component.setArpClockForTest (0);
            component.setArpSettingsForTest (holdSettings);
            pump();

            for (const auto note : { 60, 64, 67 })
                component.simulateMidiDeviceMessageForTest (juce::MidiMessage::noteOff (1, note));

            pump();

            check (component.getHeldMidiNoteCountForTest() == 0
                       && component.getArpOrderForTest() == std::vector<int> { 60, 64, 67 },
                   NI_TXT ("Hold：全部松开以后这一组还在跑"));

            component.simulateMidiDeviceMessageForTest (
                juce::MidiMessage::noteOn (1, 62, (juce::uint8) 100));
            pump();

            check (component.getArpOrderForTest() == std::vector<int> { 62 },
                   NI_TXT ("全部松开之后按的第一个音开始新的一组"));

            component.simulateMidiDeviceMessageForTest (juce::MidiMessage::noteOff (1, 62));
            pump();

            // ---- 录制: the take gets the notes the arpeggiator played -----------
            auto recordSettings = sixteenthSettings;
            recordSettings.hold = false;

            component.setArpClockForTest (0);
            component.setArpSettingsForTest (recordSettings);
            pump();

            appState.setMidiRecordArmed (true);
            arpAudio.setPositionSeconds (0.0);
            arpAudio.play();
            pump();

            for (const auto note : { 60, 64, 67 })
                component.simulateMidiDeviceMessageForTest (
                    juce::MidiMessage::noteOn (1, note, (juce::uint8) 100));

            pump();
            stepThrough (0);
            stepThrough (480);
            stepThrough (960);

            arpAudio.stop();
            pump();

            const auto arpNotesIn = [&document, arpClipId]()
            {
                for (const auto& track : document.getProject().tracks)
                    for (const auto& clip : track.clips)
                        if (clip.id == arpClipId)
                            return (int) clip.notes.size();

                return -1;
            };

            const auto arpNoteAt = [&document, arpClipId] (int index) -> const Note*
            {
                for (const auto& track : document.getProject().tracks)
                    for (const auto& clip : track.clips)
                        if (clip.id == arpClipId && index < (int) clip.notes.size())
                            return &clip.notes[(size_t) index];

                return nullptr;
            };

            const auto* recordedFirst = arpNoteAt (0);
            const auto* recordedSecond = arpNoteAt (1);

            check (arpNotesIn() == 3,
                   NI_TXT ("录下来的是琶音后的音符，不是按着的三个和弦音（实际 ")
                       + juce::String (arpNotesIn()) + NI_TXT (" 个）"));
            check (recordedFirst != nullptr && recordedFirst->startTick == 0
                       && recordedFirst->durationTicks == 120,
                   recordedFirst != nullptr
                       ? (NI_TXT ("第一个音落在琶音的格子上、长度就是 Gate（tick ")
                              + juce::String (recordedFirst->startTick) + NI_TXT (" + ")
                              + juce::String (recordedFirst->durationTicks) + NI_TXT ("）"))
                       : NI_TXT ("琶音录下来的第一个音不见了"));
            check (recordedSecond != nullptr && recordedSecond->startTick == 480,
                   recordedSecond != nullptr
                       ? (NI_TXT ("第二个音落在它该在的那一格（tick ")
                              + juce::String (recordedSecond->startTick) + NI_TXT ("）"))
                       : NI_TXT ("琶音录下来的第二个音不见了"));
            check (document.getUndoDescription() == NI_TXT ("录制 MIDI"),
                   NI_TXT ("琶音录下来的音也是一音一条「录制 MIDI」（「")
                       + document.getUndoDescription() + NI_TXT ("」）"));

            appState.setMidiRecordArmed (false);
            pump();

            // ---- 关掉琶音以后按着的键自己发声 ----------------------------------
            component.setArpEnabledForTest (true);
            pump();

            component.simulateMidiDeviceMessageForTest (
                juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100));
            pump();

            check (component.getHeldMidiVoiceCountForTest() == 0,
                   NI_TXT ("琶音开着：按着的键自己不发声"));

            component.setArpEnabledForTest (false);
            pump();

            check (component.getHeldMidiVoiceCountForTest() > 0,
                   NI_TXT ("关掉琶音以后这个按着的键自己响起来了"));

            component.simulateMidiDeviceMessageForTest (juce::MidiMessage::noteOff (1, 60));
            pump();

            // ---- 两个窗口的琶音控件是同一份状态 --------------------------------
            component.openMidiInputWindow();
            component.openVirtualKeyboardWindow();
            pump();

            auto* midiWin47 = component.getMidiInputWindowForTest();
            auto* keyboardWin47 = component.getVirtualKeyboardWindowForTest();

            if (midiWin47 != nullptr && keyboardWin47 != nullptr)
            {
                auto& midiArp = midiWin47->getPanel().getArpControlsForTest();
                auto& keyboardArp = keyboardWin47->getPanel().getArpControlsForTest();

                check (! midiArp.getEnableBoundsForTest().isEmpty()
                           && ! midiArp.getRateBoundsForTest().isEmpty()
                           && ! midiArp.getDirectionBoundsForTest().isEmpty()
                           && ! midiArp.getHoldBoundsForTest().isEmpty(),
                       NI_TXT ("MIDI 输入窗口里也有一整排琶音控件"));

                component.setArpEnabledForTest (true);
                pump();

                check (midiArp.getEnableTextForTest() == NI_TXT ("琶音")
                           && keyboardArp.getSummaryForTest().contains (NI_TXT ("琶音")),
                       NI_TXT ("两个窗口的读数都跟着同一份设置走"));

                midiArp.triggerEnableForTest();   // turn it off from the mapping window
                pump();

                check (! component.getArpSettingsForTest().enabled,
                       NI_TXT ("在 MIDI 输入窗口里点一下就把琶音关了"));
                check (keyboardArp.getSummaryForTest().contains (NI_TXT ("琶音关")),
                       NI_TXT ("另一个窗口的读数跟着变成「琶音关」（「")
                           + keyboardArp.getSummaryForTest() + NI_TXT ("」）"));

                keyboardArp.triggerEnableForTest();   // and back on from the keyboard
                pump();

                check (component.getArpSettingsForTest().enabled
                           && midiArp.getSummaryForTest().contains (NI_TXT ("琶音")),
                       NI_TXT ("从虚拟键盘窗口也能把它打开，另一个窗口跟着变"));
            }

            component.setArpEnabledForTest (false);
            component.clearArpClockForTest();
            arpAudio.stop();
            pump();

            // Every key this scene pressed is let go: later scenes (the drum track's
            // "MIDI 输入 does not route here" check, for one) read the held count.
            for (const auto note : { 60, 62, 64, 67 })
                component.simulateMidiDeviceMessageForTest (juce::MidiMessage::noteOff (1, note));

            pump();

            // The project is left as the later scenes expect it.
            document.viewState().editorMode = EditorMode::reference;
            appState.setMidiRecordArmed (false);

            // And the virtual keyboard is put away: while it is on screen the 1-6
            // keys belong to it (its bass row), and later scenes change a note's
            // ratio with those very keys.
            if (keyboardWin47 != nullptr)
                keyboardWin47->hide();

            pump();
        }

        //==============================================================================
        // 第四十七批: the keyboard's own data (velocity, pedals, wheels).
        //==============================================================================

        beginTest (NI_TXT ("MIDI 输入：按键力度、延音踏板与表情踏板（第四十七批）"));

        {
            juce::int64 ccTrackId = 0;
            juce::int64 ccClipId = 0;

            document.applyEdit ("selftest midi controller setup",
                                [&ccTrackId, &ccClipId] (Project& project)
            {
                project.tracks.clear();
                project.referenceLines.clear();
                project.nextTrackId = 1;
                project.nextNoteId = 1;
                project.loop.enabled = false;
                project.midiInput = {};

                ccTrackId = addTrack (project, TrackKind::synth);

                auto* track = project.findTrack (ccTrackId);
                track->name = NI_TXT ("控制器轨");

                // A quick, sustaining patch: a live note is easy to measure, and it
                // stops quickly once the pedal comes up.
                auto& patch = track->synth.patch;
                patch.ops[0].outputLevel = 99;

                for (int stage = 0; stage < 4; ++stage)
                {
                    patch.ops[0].rate[stage] = 99;
                    patch.ops[0].level[stage] = stage < 3 ? 99 : 0;
                }

                auto& clip = track->clips.front();
                clip.startTick = 0;
                clip.lengthTicks = 3840;
                clip.loopLengthTicks = 3840;
                clip.notes.clear();
                ccClipId = clip.id;

                // One bound key: MIDI 60 plays 440 Hz.
                project.midiInput.map.mode = midimap::Map::Mode::looping;
                project.midiInput.map.anchorNote = 60;
                midimap::setLoopKeys (project.midiInput.map, 12);
                project.midiInput.map.setFrequency (60, 440.0);
            });
            pump();

            appState.selectTrack (ccTrackId);
            appState.selectClip (ccClipId);
            appState.setEditorOpen (true);
            appState.setMidiRecordIntoClip (true);
            document.viewState().editorMode = EditorMode::reference;
            pump();

            auto& ccAudio = component.getAudioEngine();

            const auto lanePoints = [&document, ccClipId] (int controller)
            {
                for (const auto& track : document.getProject().tracks)
                    for (const auto& clip : track.clips)
                        if (clip.id == ccClipId)
                            for (const auto& curve : clip.controllerCurves)
                                if (curve.controller == controller)
                                    return (int) curve.points.size();

                return 0;
            };

            const auto lastNoteIn = [&document, ccClipId]() -> const Note*
            {
                for (const auto& track : document.getProject().tracks)
                    for (const auto& clip : track.clips)
                        if (clip.id == ccClipId && ! clip.notes.empty())
                            return &clip.notes.back();

                return nullptr;
            };

            ccAudio.stop();
            ccAudio.setPositionSeconds (0.0);
            appState.setMidiRecordArmed (true);
            ccAudio.play();
            pump();

            // ---- 力度: the velocity the key was really struck with -------------
            component.simulateMidiDeviceMessageForTest (
                juce::MidiMessage::noteOn (1, 60, (juce::uint8) 80));
            pump();
            ccAudio.setPositionSeconds (0.25);
            component.simulateMidiDeviceMessageForTest (juce::MidiMessage::noteOff (1, 60));
            pump();

            const auto* struck = lastNoteIn();

            check (struck != nullptr && struck->velocity == 80,
                   NI_TXT ("录下来的音符用的是弹下去的力度（实际 ")
                       + juce::String (struck != nullptr ? struck->velocity : -1)
                       + NI_TXT ("，以前一律写死 100）"));

            // ---- 延音踏板: written into the clip's controller curve -------------
            const auto undoBeforePedal = document.getUndoDescription();

            component.simulateMidiDeviceMessageForTest (
                juce::MidiMessage::controllerEvent (1, 64, 127));
            pump();
            // A beat apart: the recorded tick is snapped to the grid (the same rule a
            // written note's start follows), so two changes inside one beat would
            // collapse onto one point.
            ccAudio.setPositionSeconds (1.0);
            pump();   // the message is delivered on the message thread: let the seek land first
            component.simulateMidiDeviceMessageForTest (
                juce::MidiMessage::controllerEvent (1, 64, 0));
            pump();

            check (lanePoints (64) == 2,
                   NI_TXT ("踩下 / 抬起写成了 CC64 曲线的两个点（实际 ")
                       + juce::String (lanePoints (64)) + NI_TXT (" 个）"));
            check (document.getUndoDescription() == NI_TXT ("录制控制器"),
                   NI_TXT ("这一趟踏板是一条撤销（「") + document.getUndoDescription()
                       + NI_TXT ("」）"));

            document.undo();
            pump();

            check (lanePoints (64) == 0 && document.getUndoDescription() == undoBeforePedal,
                   NI_TXT ("一次 Ctrl+Z 把这一条踏板手势整个收回去"));

            // ---- 表情: a stream of values is one gesture, so one undo ----------
            ccAudio.setPositionSeconds (0.0);
            pump();
            component.simulateMidiDeviceMessageForTest (
                juce::MidiMessage::controllerEvent (1, 11, 20));
            pump();
            ccAudio.setPositionSeconds (0.5);
            pump();
            component.simulateMidiDeviceMessageForTest (
                juce::MidiMessage::controllerEvent (1, 11, 60));
            pump();
            ccAudio.setPositionSeconds (1.0);
            pump();
            component.simulateMidiDeviceMessageForTest (
                juce::MidiMessage::controllerEvent (1, 11, 100));
            pump();

            check (lanePoints (11) == 3,
                   NI_TXT ("连续推表情写成了一条曲线（实际 ")
                       + juce::String (lanePoints (11)) + NI_TXT (" 个点）"));

            document.undo();
            pump();

            check (lanePoints (11) == 0,
                   NI_TXT ("连续三次变化合并成一条撤销，不是三条"));

            // ---- with the transport stopped nothing is written down -------------
            ccAudio.stop();
            appState.setMidiRecordArmed (false);
            pump();

            const auto undoWhenStopped = document.getUndoDescription();
            component.simulateMidiDeviceMessageForTest (
                juce::MidiMessage::controllerEvent (1, 64, 127));
            pump();

            check (document.getUndoDescription() == undoWhenStopped && lanePoints (64) == 0,
                   NI_TXT ("走带停着时踩踏板只在实时发声，不写工程"));

            // ---- the live controllers really reach the engine -------------------
            const auto livePeak = [&ccAudio, ccTrackId] (int blocks)
            {
                auto peak = 0.0f;

                for (int i = 0; i < blocks; ++i)
                {
                    ccAudio.renderBlockForTest (512);
                    const auto reading = ccAudio.readTrackMeter (ccTrackId);
                    peak = juce::jmax (peak, reading.peakLeft, reading.peakRight);
                }

                return peak;
            };

            // The self test renders by hand: start this measurement from an engine
            // with nothing queued or sounding (earlier scenes audition notes).
            ccAudio.resetLiveStateForTest();
            pump();

            component.simulateMidiDeviceMessageForTest (
                juce::MidiMessage::noteOn (1, 60, (juce::uint8) 127));
            pump();

            const auto loud = livePeak (10);

            component.simulateMidiDeviceMessageForTest (
                juce::MidiMessage::controllerEvent (1, 11, 0));
            pump();

            const auto quiet = livePeak (10);

            check (loud > 0.001f && quiet < loud * 0.2f,
                   NI_TXT ("实时 CC11 把这一轨拉下去了（电平表 ")
                       + juce::String (loud, 4) + NI_TXT (" → ") + juce::String (quiet, 4)
                       + NI_TXT ("，和画出来的曲线同一条路）"));

            // The pedal holds a *live* note's note-off back until it comes up.
            component.simulateMidiDeviceMessageForTest (
                juce::MidiMessage::controllerEvent (1, 11, 127));
            component.simulateMidiDeviceMessageForTest (
                juce::MidiMessage::controllerEvent (1, 64, 127));
            component.simulateMidiDeviceMessageForTest (juce::MidiMessage::noteOff (1, 60));
            pump();

            const auto held = livePeak (20);

            component.simulateMidiDeviceMessageForTest (
                juce::MidiMessage::controllerEvent (1, 64, 0));
            pump();
            livePeak (12);   // let the release finish

            const auto released = livePeak (6);

            check (held > 0.001f && released < held * 0.2f,
                   NI_TXT ("踏板踩着时实时音符继续响，抬起以后放掉（")
                       + juce::String (held, 4) + NI_TXT (" → ")
                       + juce::String (released, 4) + NI_TXT ("）"));

            ccAudio.stop();
            pump();
        }

        beginTest (NI_TXT ("MIDI 录制：片段内 / 片段外、一键一条撤销（第四十一批·第二步）"));

        {
            juce::int64 recTrackId = 0;
            juce::int64 recClipId = 0;

            document.applyEdit ("selftest midi record setup", [&recTrackId, &recClipId] (Project& project)
            {
                project.tracks.clear();
                project.nextTrackId = 1;
                project.nextNoteId = 1;
                project.loop.enabled = false;
                project.midiInput = {};

                recTrackId = addTrack (project, TrackKind::synth);

                auto* track = project.findTrack (recTrackId);
                track->name = NI_TXT ("录音轨");

                auto& clip = track->clips.front();
                clip.startTick = 0;
                clip.lengthTicks = 960;
                clip.loopLengthTicks = 960;
                clip.notes.clear();
                recClipId = clip.id;

                // One bound key: MIDI 60 plays 440 Hz, and the tile repeats every
                // twelve semitones - an octave higher each time it does.
                project.midiInput.map.mode = midimap::Map::Mode::looping;
                project.midiInput.map.anchorNote = 60;
                midimap::setLoopKeys (project.midiInput.map, 12);
                project.midiInput.map.setFrequency (60, 440.0);
            });
            pump();

            appState.selectTrack (recTrackId);
            appState.selectClip (recClipId);
            appState.setEditorOpen (true);
            appState.setMidiRecordIntoClip (true);
            pump();

            auto& recAudio = component.getAudioEngine();
            auto& recTransport = component.getTransportBar();

            recTransport.refresh();

            check (recTransport.getRecordTextForTest() == NI_TXT ("录制")
                       && ! recTransport.getRecordButtonBoundsForTest().isEmpty(),
                   NI_TXT ("走带栏上真的有一个「录制」按钮（「")
                       + recTransport.getRecordTextForTest() + NI_TXT ("」）"));
            check (recTransport.getRecordTargetTextForTest() == NI_TXT ("片段内")
                       && ! recTransport.getRecordTargetBoundsForTest().isEmpty(),
                   NI_TXT ("旁边是「片段内 / 片段外」的切换（现在写着「")
                       + recTransport.getRecordTargetTextForTest() + NI_TXT ("」）"));

            // ---- `*` arms and disarms, on the main row and the numpad --------
            check (component.sendKeyPressForTest (juce::KeyPress ('*', juce::ModifierKeys(), 0),
                                                  &component),
                   NI_TXT ("`*` 被走带接住了"));
            pump();
            check (appState.midiRecordArmed, NI_TXT ("按 `*` 武装了录制"));
            check (component.getTransportBar().getRecordTextForTest() == NI_TXT ("停止录制"),
                   NI_TXT ("武装之后按钮变成「停止录制」（写着「")
                       + component.getTransportBar().getRecordTextForTest() + NI_TXT ("」）"));

            check (component.sendKeyPressForTest (juce::KeyPress (juce::KeyPress::numberPadMultiply),
                                                  &component),
                   NI_TXT ("小键盘的 `*` 也被接住了"));
            pump();
            check (! appState.midiRecordArmed, NI_TXT ("小键盘的 `*` 解除了武装"));
            check (component.getTransportBar().getRecordTextForTest() == NI_TXT ("录制"),
                   NI_TXT ("再按一下就回到「录制」（写着「")
                       + component.getTransportBar().getRecordTextForTest() + NI_TXT ("」）"));

            // How many notes the clip holds right now.
            const auto notesIn = [&document] (juce::int64 clipId)
            {
                for (const auto& track : document.getProject().tracks)
                    for (const auto& clip : track.clips)
                        if (clip.id == clipId)
                            return (int) clip.notes.size();

                return -1;
            };

            const auto clipLengthOf = [&document] (juce::int64 clipId)
            {
                for (const auto& track : document.getProject().tracks)
                    for (const auto& clip : track.clips)
                        if (clip.id == clipId)
                            return clip.lengthTicks;

                return (juce::int64) -1;
            };

            // ---- 片段内: the note lands in the open clip ----------------------
            recAudio.stop();
            recAudio.setPositionSeconds (0.0);
            appState.setMidiRecordArmed (true);
            recAudio.play();
            pump();

            const auto notesBefore = notesIn (recClipId);
            const auto undoBeforeTake = document.getUndoDescription();

            component.simulateMidiDeviceMessageForTest (
                juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100));
            pump();

            check (component.getPendingRecordCountForTest() == 1,
                   NI_TXT ("按住键的时候那一音还等着松开（")
                       + juce::String (component.getPendingRecordCountForTest()) + NI_TXT (" 个）"));

            // 0.5 s at 120 bpm 4:4 is tick 960: one quarter note after the start.
            recAudio.setPositionSeconds (0.5);
            component.simulateMidiDeviceMessageForTest (juce::MidiMessage::noteOff (1, 60));
            pump();

            check (notesIn (recClipId) == notesBefore + 1,
                   NI_TXT ("松开琴键就把这一音写进了打开的片段（")
                       + juce::String (notesBefore) + NI_TXT (" → ")
                       + juce::String (notesIn (recClipId)) + NI_TXT (" 个）"));

            const auto* written = [&document, recClipId]() -> const Note*
            {
                for (const auto& track : document.getProject().tracks)
                    for (const auto& clip : track.clips)
                        if (clip.id == recClipId && ! clip.notes.empty())
                            return &clip.notes.back();

                return nullptr;
            }();

            check (written != nullptr
                       && written->startTick == 0
                       && written->durationTicks == 960
                       && std::abs (written->frequencyHz - 440.0) < 1.0e-6,
                   written != nullptr
                       ? (NI_TXT ("音符落在起点、长一拍、频率就是绑定的 440 Hz（tick ")
                              + juce::String (written->startTick) + NI_TXT (" + ")
                              + juce::String (written->durationTicks) + NI_TXT ("）"))
                       : NI_TXT ("录下来的音符不见了"));

            check (document.getUndoDescription() == NI_TXT ("录制 MIDI")
                       && document.getUndoDescription() != undoBeforeTake,
                   NI_TXT ("一个音就是一条撤销记录（「") + document.getUndoDescription()
                       + NI_TXT ("」）"));

            // ---- a take that runs past the end makes the clip longer ----------
            recAudio.setPositionSeconds (0.5);   // tick 960
            component.simulateMidiDeviceMessageForTest (
                juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100));
            pump();
            recAudio.setPositionSeconds (0.75);  // tick 1440
            component.simulateMidiDeviceMessageForTest (juce::MidiMessage::noteOff (1, 60));
            pump();

            check (notesIn (recClipId) == notesBefore + 2 && clipLengthOf (recClipId) >= 1440,
                   NI_TXT ("越过片段末尾的音符把片段拉长了（长 ")
                       + juce::String (clipLengthOf (recClipId)) + NI_TXT(" tick，")
                       + juce::String (notesIn (recClipId)) + NI_TXT (" 个音符）"));

            document.undo();
            pump();

            check (notesIn (recClipId) == notesBefore + 1 && clipLengthOf (recClipId) == 960,
                   NI_TXT ("一次撤销只收回刚录的那一个音，片段长度也还原（")
                       + juce::String (notesIn (recClipId)) + NI_TXT (" 个，长 ")
                       + juce::String (clipLengthOf (recClipId)) + NI_TXT ("）"));

            appState.setMidiRecordArmed (false);
            pump();

            // ---- a key played while the transport is stopped ----------------
            recAudio.stop();
            recAudio.setPositionSeconds (0.0);
            recAudio.readTrackMeter (recTrackId);
            appState.setMidiRecordArmed (true);
            pump();

            const auto idleBefore = notesIn (recClipId);

            component.simulateMidiDeviceMessageForTest (
                juce::MidiMessage::noteOn (1, 60, (juce::uint8) 120));
            pump();

            auto idlePeak = 0.0f;

            for (int i = 0; i < 6; ++i)
            {
                recAudio.renderBlockForTest (512);
                const auto reading = recAudio.readTrackMeter (recTrackId);
                idlePeak = juce::jmax (idlePeak, reading.peakLeft, reading.peakRight);
            }

            check (idlePeak > 0.001f,
                   NI_TXT ("走带停着的时候按琴键仍然听得见（电平表读到 ")
                       + juce::String (idlePeak, 4) + NI_TXT ("）"));
            check (notesIn (recClipId) == idleBefore,
                   NI_TXT ("走带停着的时候不落音符（还是 ")
                       + juce::String (notesIn (recClipId)) + NI_TXT (" 个）"));

            component.simulateMidiDeviceMessageForTest (juce::MidiMessage::noteOff (1, 60));
            pump();

            // ---- 片段外: the take becomes a clip of its own -------------------
            appState.setMidiRecordIntoClip (false);
            recTransport.refresh();
            pump();

            check (recTransport.getRecordTargetTextForTest() == NI_TXT ("片段外"),
                   NI_TXT ("切换按钮写成了「") + recTransport.getRecordTargetTextForTest()
                       + NI_TXT ("」"));

            const auto clipsBefore = (int) document.getProject().findTrack (recTrackId)->clips.size();

            recAudio.setPositionSeconds (1.0);   // tick 1920
            recAudio.play();
            pump();

            component.simulateMidiDeviceMessageForTest (
                juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100));
            pump();
            recAudio.setPositionSeconds (1.5);   // tick 2880
            component.simulateMidiDeviceMessageForTest (juce::MidiMessage::noteOff (1, 60));
            pump();

            const auto* madeClip = [&document, recTrackId]() -> const Clip*
            {
                const auto* track = document.getProject().findTrack (recTrackId);

                for (const auto& clip : track->clips)
                    if (clip.startTick == 1920)
                        return &clip;

                return nullptr;
            }();

            check ((int) document.getProject().findTrack (recTrackId)->clips.size() == clipsBefore + 1,
                   NI_TXT ("片段外录制新建了一个片段（")
                       + juce::String (clipsBefore) + NI_TXT (" → ")
                       + juce::String ((int) document.getProject().findTrack (recTrackId)->clips.size())
                       + NI_TXT (" 个）"));
            check (madeClip != nullptr && madeClip->lengthTicks == 960
                       && (int) madeClip->notes.size() == 1
                       && madeClip->notes.front().startTick == 0,
                   madeClip != nullptr
                       ? (NI_TXT ("新片段从起点铺到音符末尾，音符相对片段起点的 tick 是 ")
                              + juce::String (madeClip->notes.front().startTick) + NI_TXT ("，长 ")
                              + juce::String (madeClip->lengthTicks))
                       : NI_TXT ("片段外录制什么也没建出来"));
            check (document.getUndoDescription() == NI_TXT ("录制 MIDI"),
                   NI_TXT ("这一音同样是一条「录制 MIDI」撤销（「")
                       + document.getUndoDescription() + NI_TXT ("」）"));

            // ...and that one step takes the whole new clip away again.
            document.undo();
            pump();

            check ((int) document.getProject().findTrack (recTrackId)->clips.size() == clipsBefore,
                   NI_TXT ("一次撤销就把新片段收了回去（")
                       + juce::String ((int) document.getProject().findTrack (recTrackId)->clips.size())
                       + NI_TXT (" 个）"));

            appState.setMidiRecordArmed (false);
            recAudio.stop();
            pump();

            // ---- the drum machine and audio tracks take no MIDI --------------
            document.applyEdit ("selftest midi drums", [] (Project& project)
            {
                addTrack (project, TrackKind::drums);
            });
            pump();

            appState.selectTrack (document.getProject().tracks.back().id);
            pump();

            component.simulateMidiDeviceMessageForTest (
                juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100));
            pump();

            check (component.getHeldMidiNoteCountForTest() == 0,
                   NI_TXT ("鼓机轨不接 MIDI 输入（响着的实时音符有 ")
                       + juce::String (component.getHeldMidiNoteCountForTest()) + NI_TXT (" 个）"));

            component.simulateMidiDeviceMessageForTest (juce::MidiMessage::noteOff (1, 60));
            pump();
        }

        beginTest (NI_TXT ("片段可以重叠：两段都留下、都发声（第四十一批·第三步）"));

        {
            juce::int64 overlapTrackId = 0;
            juce::int64 overlapFirstClipId = 0;

            document.applyEdit ("selftest overlap setup", [&overlapTrackId, &overlapFirstClipId] (Project& project)
            {
                project.tracks.clear();
                project.nextTrackId = 1;
                project.nextNoteId = 1;
                project.loop.enabled = false;

                overlapTrackId = addTrack (project, TrackKind::synth);

                auto* track = project.findTrack (overlapTrackId);
                track->clips.clear();

                auto clip = makeDefaultClip (project);
                clip.startTick = 0;
                clip.lengthTicks = 1920;
                clip.loopLengthTicks = 1920;

                Note low;
                low.id = project.nextNoteId++;
                low.startTick = 0;
                low.durationTicks = 960;
                low.frequencyHz = 220.0;
                clip.notes = { low };

                overlapFirstClipId = clip.id;
                track->clips.push_back (std::move (clip));

                project.view.pixelsPerQuarter = 140.0;
                project.view.horizontalOffsetTicks = 0.0;
                project.view.trackSnap.denominator = 1;
                project.view.editorSnap.denominator = 1;
                project.view.controllerSnap.denominator = 1;
            });
            pump();

            appState.selectTrack (overlapTrackId);
            pump();

            // A clip next to the first one, drawn out from empty lane space.
            {
                const auto start = juce::Point<float> ((float) laneX (1920.0), (float) laneY (0));
                lanes.mouseDown (makeMouseEvent (lanes, start, start));
                lanes.forceLongPressForTest();

                const auto finish = juce::Point<float> ((float) laneX (3840.0), (float) laneY (0));
                lanes.mouseDrag (makeMouseEvent (lanes, finish, start));
                lanes.mouseUp (makeMouseEvent (lanes, finish, start));
                pump();
            }

            check ((int) document.getProject().findTrack (overlapTrackId)->clips.size() == 2,
                   NI_TXT ("旁边先画出第二个片段（")
                       + juce::String ((int) document.getProject().findTrack (overlapTrackId)->clips.size())
                       + NI_TXT (" 个）"));

            // Drag it back over the first one: it used to be trimmed, it must now land.
            {
                const auto body = juce::Point<float> ((float) laneX (2880.0), (float) laneY (0));
                const auto target = juce::Point<float> ((float) laneX (1920.0), (float) laneY (0));

                lanes.mouseDown (makeMouseEvent (lanes, body, body));
                lanes.mouseDrag (makeMouseEvent (lanes, target, body));
                lanes.mouseUp (makeMouseEvent (lanes, target, body));
                pump();
            }

            const auto* track = document.getProject().findTrack (overlapTrackId);
            const auto* first = track != nullptr ? track->findClip (overlapFirstClipId) : nullptr;
            const Clip* second = nullptr;

            if (track != nullptr)
                for (const auto& clip : track->clips)
                    if (clip.id != overlapFirstClipId)
                        second = &clip;

            const auto secondId    = second != nullptr ? second->id        : (juce::int64) 0;
            const auto secondStart = second != nullptr ? second->startTick : (juce::int64) 0;

            check (track != nullptr && track->clips.size() == 2,
                   NI_TXT ("拖动以后两个片段都还在（")
                       + juce::String (track != nullptr ? (int) track->clips.size() : -1)
                       + NI_TXT (" 个）"));
            check (first != nullptr && first->startTick == 0 && first->lengthTicks == 1920,
                   NI_TXT ("原先那一段没有被裁：还是 ")
                       + juce::String (first != nullptr ? first->startTick : -1) + NI_TXT (" + ")
                       + juce::String (first != nullptr ? first->lengthTicks : -1));
            check (second != nullptr && second->startTick < (first != nullptr ? first->endTick() : 0),
                   NI_TXT ("第二段真的压在它上面（起点 ")
                       + juce::String (second != nullptr ? second->startTick : -1) + NI_TXT ("）"));

            // The clip that landed on top gets a note of its own: two clips whose
            // windows overlap must both sound, and the schedule is where that shows.
            if (secondId != 0)
            {
                document.applyEdit ("selftest overlap note", [secondId] (Project& project)
                {
                    if (auto* clip = project.findClip (secondId))
                    {
                        Note high;
                        high.id = project.nextNoteId++;
                        high.startTick = 0;
                        high.durationTicks = 960;
                        high.frequencyHz = 330.0;
                        clip->notes = { high };
                    }
                });
                pump();
            }

            const auto overlapSchedule = buildSchedule (document.getProject());
            check ((int) overlapSchedule.tracks.front().events.size() == 4,
                   NI_TXT ("重叠处的两段都进了调度（")
                       + juce::String ((int) overlapSchedule.tracks.front().events.size())
                       + NI_TXT (" 个事件 = 两个音）"));

            // Clicking where they overlap picks the one drawn on top.
            if (secondId != 0)
            {
                const auto inside = juce::Point<float> ((float) laneX ((double) (secondStart + 240)),
                                                        (float) laneY (0));
                lanes.mouseDown (makeMouseEvent (lanes, inside, inside));
                lanes.mouseUp (makeMouseEvent (lanes, inside, inside));
                pump();

                check (appState.selectedClipId == secondId,
                       NI_TXT ("点在重叠处选中的是最上面那一段（")
                           + juce::String (appState.selectedClipId) + NI_TXT (" / 应为 ")
                           + juce::String (secondId) + NI_TXT ("）"));
            }
        }

        beginTest ("exporting honours the settings and writes stems");

        {
            // Two tracks, so a stem batch has something to separate.
            juce::int64 extraTrackId = 0;

            document.applyEdit ("selftest export setup", [&extraTrackId] (Project& project)
            {
                // Rebuild from scratch: earlier tests left tracks behind, and a
                // stem export renders every track it is given.
                project.tracks.clear();
                project.nextTrackId = 1;

                const auto firstId = addTrack (project, TrackKind::synth);

                Note n;
                n.id = project.nextNoteId++;
                n.startTick = 0;
                n.durationTicks = 960;
                n.frequencyHz = 220.0;
                project.findTrack (firstId)->clips.front().notes = { n };

                extraTrackId = addTrack (project, TrackKind::synth);

                auto* second = project.findTrack (extraTrackId);
                second->name = NI_TXT ("第二轨");

                Note high;
                high.id = project.nextNoteId++;
                high.startTick = 0;
                high.durationTicks = 960;
                high.frequencyHz = 660.0;
                second->clips.front().notes = { high };
            });
            pump();

            const auto exportFolder = outputDirectory.getChildFile ("selftest-export");

            // One file, WAV 16 bit at 44.1 kHz.
            ExportSettings single;
            single.format = ExportSettings::Format::wav;
            single.bitsPerSample = 16;
            single.sampleRate = 44100.0;
            single.master = true;

            const auto singleFile = exportFolder.getChildFile ("mix.wav");
            juce::String sectionExportError;

            trace ("export (single) starting at " + juce::String (juce::Time::getMillisecondCounter()));

            check (component.runExport (single, singleFile, false, sectionExportError),
                   "a single file export runs: " + sectionExportError);

            trace ("export (single) finished at " + juce::String (juce::Time::getMillisecondCounter()));

            juce::AudioFormatManager exportFormats;
            exportFormats.registerBasicFormats();

            {
                std::unique_ptr<juce::AudioFormatReader> sectionReader (
                    exportFormats.createReaderFor (singleFile));

                check (sectionReader != nullptr, "the exported file is readable");

                if (sectionReader != nullptr)
                {
                    check (std::abs (sectionReader->sampleRate - 44100.0) < 1.0,
                           "it is written at the rate that was asked for ("
                               + juce::String (sectionReader->sampleRate, 1) + " Hz)");
                    check ((int) sectionReader->bitsPerSample == 16,
                           "16 bit as asked (got " + juce::String ((int) sectionReader->bitsPerSample) + ")");
                }
            }

            // A batch: the master mix plus one file per track.
            ExportSettings batch;
            batch.format = ExportSettings::Format::wav;
            batch.bitsPerSample = 24;
            batch.master = true;

            for (const auto& track : document.getProject().tracks)
                batch.trackIds.insert (track.id);

            std::vector<juce::File> written;

            // Called before the message is built: argument evaluation order is not
            // guaranteed, and the error only exists afterwards.
            const auto batchRan = component.runExport (batch, exportFolder, true,
                                                       sectionExportError, &written);

            trace ("export (batch) finished at " + juce::String (juce::Time::getMillisecondCounter()));

            check (batchRan, "the stem batch runs: " + sectionExportError);
            check ((int) written.size() == 3,
                   "the batch wrote the master mix and one file per track (got "
                       + juce::String ((int) written.size()) + ")");

            auto readable = 0;

            for (const auto& file : written)
            {
                std::unique_ptr<juce::AudioFormatReader> sectionReader (
                    exportFormats.createReaderFor (file));

                if (sectionReader != nullptr && (int) sectionReader->bitsPerSample == 24)
                    ++readable;
            }

            check (readable == (int) written.size(),
                   "every stem is a readable 24 bit file (got " + juce::String (readable) + ")");

            // The export kept a narration of what it did, and every plugin it built
            // for the render was built on the message thread: loading a plugin,
            // preparing it and applying its state are message-thread work, and doing
            // them on the render thread is what can take a real sampler (Kontakt)
            // down mid-export.
            {
                const auto narration = MainComponent::getExportLogFile();
                const auto text = narration.existsAsFile() ? narration.loadFileAsString()
                                                           : juce::String();

                // (Ctrl+E's own steps - the dialog and the choosers - cannot run in a
                // headless test; the render half is what this drives. The VST half is
                // checked where a plugin is actually exported.)
                check (text.contains ("render starting"),
                       NI_TXT ("the export wrote down what it did"));
                check (text.contains ("render finished") || text.contains ("the render failed"),
                       NI_TXT ("and how it ended"));
                check (PluginHostManager::getOfflineThreadViolationsForTest() == 0,
                       NI_TXT ("every offline plugin instance was built on the message thread (violations ")
                           + juce::String (PluginHostManager::getOfflineThreadViolationsForTest())
                           + NI_TXT (")"));
            }

            // FLAC keeps only its legal depths, and the dialog would clamp to them.
            ExportSettings flac;
            flac.format = ExportSettings::Format::flac;
            flac.bitsPerSample = 32;
            flac.clampToFormat();

            check (flac.bitsPerSample == 24, "FLAC has no float: it falls back to 24 bit");
        }

        beginTest ("closing asks about unsaved work");

        {
            // The self test never blocks, but it still reports the truth about
            // whether anything needs saving.
            check (component.getDocument().isModified(),
                   "the project is dirty after all these edits");
            check (component.confirmQuit(),
                   "closing does not block while the self test is driving");

            document.saveToFile (outputDirectory.getChildFile ("selftest-quitcheck.nprj"),
                                 error);
            pump();

            check (! component.getDocument().isModified(),
                   "saving clears the modified flag, so closing would not ask");
            check (component.confirmQuit(), "and closing is then allowed straight away");
        }

        beginTest ("a scrolled clip does not paint over the track header");

        {
            document.applyEdit ("selftest scroll setup", [] (Project& project)
            {
                auto& track = project.tracks.front();
                track.clips.clear();

                auto clip = makeDefaultClip (project);
                clip.startTick = 0;
                clip.lengthTicks = 19200;
                clip.loopLengthTicks = 19200;
                track.clips.push_back (std::move (clip));
            });
            pump();

            auto& laneArea = component.getTrackArea();
            auto& view = document.viewState();

            const juce::Rectangle<int> headerRect (0,
                                                   TrackArea::topBarHeight + TrackArea::markerHeight
                                                       + TrackArea::rulerHeight,
                                                   TrackArea::headerWidth,
                                                   TrackArea::laneHeight);

            check (laneArea.getWidth() > TrackArea::headerWidth
                       && laneArea.getHeight() >= headerRect.getBottom(),
                   "the track area is big enough to check the header");

            // The lanes draw with the arrangement's own timeline.
            view.arrangementHorizontalOffsetTicks = 0.0;
            const auto unscrolled = snapshot (laneArea);

            // One beat in: the clip now starts left of the lane content area, so
            // its rectangle would reach into the header without the clip region.
            view.arrangementHorizontalOffsetTicks = 960.0;
            const auto scrolled = snapshot (laneArea);

            int changedPixels = 0;

            for (int y = headerRect.getY(); y < headerRect.getBottom(); ++y)
                for (int x = headerRect.getX(); x < headerRect.getRight(); ++x)
                    if (unscrolled.getPixelAt (x, y) != scrolled.getPixelAt (x, y))
                        ++changedPixels;

            check (changedPixels == 0,
                   "the header is untouched when a clip scrolls under it (changed pixels "
                       + juce::String (changedPixels) + ")");

            view.arrangementHorizontalOffsetTicks = 0.0;
            pump();
        }

        // ---- reference control panel -----------------------------------------

        beginTest ("the reference control panel starts empty and + adds a hertz line");

        juce::int64 fixedLineId = 0;
        {
            auto& panel = component.getEditorPanel().getOptionsPanel();

            // Leave the project with a single track and nothing but one note.
            document.applyEdit ("selftest panel setup", [] (Project& project)
            {
                project.referenceLines.clear();
                project.tracks.resize (1);

                auto& track = project.tracks.front();
                track.clips.clear();

                auto clip = makeDefaultClip (project);
                clip.startTick = 0;
                clip.lengthTicks = 19200;
                clip.loopLengthTicks = 19200;

                Note n;
                n.id = project.nextNoteId++;
                n.startTick = 0;
                n.durationTicks = 480;
                n.frequencyHz = 660.0;
                n.velocity = 100;
                clip.notes = { n };

                track.clips.push_back (std::move (clip));
            });

            appState.clearNotes();
            appState.selectTrack (document.getProject().tracks.front().id);
            appState.selectClip (document.getProject().tracks.front().clips.front().id);
            pump();

            const auto emptyPanelImage = snapshot (component);
            check (writeImage (emptyPanelImage,
                               outputDirectory.getChildFile ("09-reference-panel-empty.png")),
                   "empty reference panel snapshot");

            check (panel.getRowCount() == 0, "the panel starts empty (got "
                                                 + juce::String (panel.getRowCount()) + " rows)");

            fixedLineId = panel.addFixedFrequency (440.0, NI_TXT ("测试赫兹"));
            pump();

            check (fixedLineId != 0 && panel.getRowCount() == 1,
                   "the + entry created one status row (got "
                       + juce::String (panel.getRowCount()) + ")");

            const auto rowBounds = panel.getRowBounds (0);
            check (rowBounds.getWidth() > 0 && rowBounds.getHeight() > 0,
                   "the status row has a real size (" + juce::String (rowBounds.getWidth())
                       + " x " + juce::String (rowBounds.getHeight()) + ")");

            // Every one of the five buttons has to carry text.
            auto missingText = juce::String();

            for (int which = 0; which < 5; ++which)
                if (panel.getRowButtonText (0, which).isEmpty())
                    missingText << which << " ";

            check (missingText.isEmpty(),
                   "every status row button has a label (missing: " + missingText + ")");
        }

        beginTest ("hiding, soloing, recolouring and deleting act on the drawn lines");

        if (fixedLineId != 0)
        {
            auto& panel = component.getEditorPanel().getOptionsPanel();

            pump();
            const auto shownImage = snapshot (component);
            const auto shownManual = countColour (shownImage, theme::referenceLineManual);

            check (shownManual > 400, "the hertz family is drawn (pixels = "
                                          + juce::String (shownManual) + ")");

            panel.triggerRowButton (0, 1);   // hide
            pump();

            const auto hiddenImage = snapshot (component);
            const auto hiddenManual = countColour (hiddenImage, theme::referenceLineManual);

            // Not zero: the panel's own colour bar uses the entry colour too, so the
            // canvas count simply has to collapse.
            check (hiddenManual < 400 && shownManual > hiddenManual * 4,
                   "hiding removes the lines from the canvas (pixels = "
                       + juce::String (hiddenManual) + ")");

            panel.triggerRowButton (0, 1);   // show again
            pump();

            check (countColour (snapshot (component), theme::referenceLineManual) > 400,
                   "showing brings them back");

            // The status row's third button used to be a colour palette. It is the
            // 编辑 button now, and a 赫兹 row has nothing for the group editor to
            // work on, so it has to be greyed out (the colour palette is gone).
            check (panel.getRowButtonText (0, 3) == NI_TXT ("编辑"),
                   NI_TXT ("状态行第三个按钮现在是「编辑」（实际是「")
                       + panel.getRowButtonText (0, 3) + NI_TXT ("」）"));
            check (! panel.isRowEditEnabled (0),
                   NI_TXT ("赫兹这类没有可编辑的内容，编辑按钮是灰的"));

            // No status row offers a colour any more, so the test colours the entry
            // the way another path would: colourIndex still lives on the entry and
            // the canvas still draws its lines in it. The solo check below needs the
            // two entries to be told apart.
            document.applyEdit ("selftest recolour", [] (Project& project)
            {
                if (! project.referenceLines.empty())
                    project.referenceLines.front().colourIndex = 3;
            });
            pump();

            const auto recoloured = snapshot (component);
            const auto newColour = countColour (recoloured, theme::trackColour (3));
            const auto oldColour = countColour (recoloured, theme::referenceLineManual);

            check (newColour > 400 && oldColour < 400,
                   "an entry's own colour repaints the family (pixels = "
                       + juce::String (newColour) + ", old = " + juce::String (oldColour) + ")");

            // Seal the first one, then add a second: an unlocked status is scratch and
            // would be cleared by the next new reference line.
            panel.triggerRowButton (0, 0);   // lock
            pump();

            // 300 is deliberately not in the 440 family: entries that want the same
            // frequency are deduplicated, so a colliding one would draw nothing.
            const auto secondId = panel.addFixedFrequency (300.0, NI_TXT ("第二条"));
            pump();

            check (panel.getRowCount() == 2, "the panel holds two rows now (got "
                                                 + juce::String (panel.getRowCount()) + ")");

            panel.triggerRowButton (0, 2);   // solo the first row
            pump();

            const auto soloImage = snapshot (component);

            check (countColour (soloImage, theme::trackColour (3)) > 400
                       && countColour (soloImage, theme::referenceLineManual) < 400,
                   "solo keeps only the soloed entry's lines");

            panel.triggerRowButton (0, 2);   // leave solo
            pump();

            check (countColour (snapshot (component), theme::referenceLineManual) > 400,
                   "leaving solo restores the other entries");

            panel.triggerRowButton (1, 4);   // delete the second row
            pump();

            check (panel.getRowCount() == 1 && secondId != 0,
                   "delete removes the row (got " + juce::String (panel.getRowCount()) + ")");
        }

        beginTest ("locking keeps an entry while a new one clears the scratch lines");

        {
            auto& panel = component.getEditorPanel().getOptionsPanel();

            document.applyEdit ("selftest lock setup", [] (Project& project)
            {
                project.referenceLines.clear();

                auto sealed = music::makeFixedHzEntry (project.nextReferenceLineId++, 440.0,
                                                       NI_TXT ("保留"));
                sealed.locked = true;
                project.referenceLines.push_back (std::move (sealed));
            });

            appState.clearNotes();
            pump();

            check (panel.getRowCount() == 1, "the setup left one status row (got "
                                                 + juce::String (panel.getRowCount()) + ")");

            const auto sealedId = panel.getRowLineId (0);

            panel.triggerRowButton (0, 0);   // unlock
            pump();

            check (document.getProject().findReferenceLine (sealedId) != nullptr
                       && ! document.getProject().findReferenceLine (sealedId)->locked,
                   "the lock button unlocks a sealed entry");

            panel.triggerRowButton (0, 0);   // seal it again
            pump();

            check (document.getProject().findReferenceLine (sealedId) != nullptr
                       && document.getProject().findReferenceLine (sealedId)->locked,
                   "the lock button seals an entry");

            const auto scratchId = panel.addFixedFrequency (1320.0, juce::String());
            pump();

            check (document.getProject().findReferenceLine (scratchId) != nullptr
                       && document.getProject().findReferenceLine (sealedId) != nullptr,
                   "the sealed entry survives a new reference line");

            panel.addFixedFrequency (1760.0, juce::String());
            pump();

            check (document.getProject().findReferenceLine (scratchId) == nullptr,
                   "the next new line clears the previous scratch entry");
            check (document.getProject().findReferenceLine (sealedId) != nullptr,
                   "the sealed entry is still there");

            // Reordering: the sealed 440 row must be able to move to the end.
            auto& lines = document.getProject().referenceLines;
            const auto before = lines.front().id;
            document.applyEdit ("selftest reorder", [] (Project& project)
            {
                music::moveEntry (project.referenceLines, 0, (int) project.referenceLines.size() - 1);
            });
            pump();

            check (document.getProject().referenceLines.back().id == before,
                   "moving a status row changes the order");

            // The up/down keys walk the same list, and n seals what is selected.
            component.keyPressed (juce::KeyPress (juce::KeyPress::upKey, juce::ModifierKeys(), 0));
            pump();

            check (panel.getSelectedLineId() == document.getProject().referenceLines.front().id,
                   "the up key moves the panel selection");
        }

        beginTest ("several selected notes list their frequencies and form the 4th kind");

        {
            juce::int64 firstNoteId = 0, secondNoteId = 0, clipId = 0;

            document.applyEdit ("selftest multi note setup",
                                [&firstNoteId, &secondNoteId, &clipId] (Project& project)
            {
                project.referenceLines.clear();
                auto& track = project.tracks.front();
                track.clips.clear();

                auto clip = makeDefaultClip (project);
                clip.startTick = 0;
                clip.lengthTicks = 19200;
                clip.loopLengthTicks = 19200;
                clipId = clip.id;

                const double frequencies[] = { 220.0, 330.0, 440.0 };

                for (int i = 0; i < 3; ++i)
                {
                    Note n;
                    n.id = project.nextNoteId++;
                    n.startTick = (juce::int64) i * 960;
                    n.durationTicks = 480;
                    n.frequencyHz = frequencies[i];
                    n.velocity = 100;
                    clip.notes.push_back (n);

                    if (i == 0) firstNoteId = n.id;
                    if (i == 1) secondNoteId = n.id;
                }

                track.clips.push_back (std::move (clip));
            });

            appState.selectTrack (document.getProject().tracks.front().id);
            appState.selectClip (clipId);
            pump();

            appState.setSelectedNotes ({ firstNoteId, secondNoteId });
            pump();

            auto& bar = component.getEditorPanel().getNoteEditBar();

            check (bar.getMultiRowCount() == 2, "the note edit bar lists both notes (got "
                                                    + juce::String (bar.getMultiRowCount()) + ")");

            check (bar.getMultiRowBounds (1).getWidth() > 0,
                   "the second frequency row has a real size");

            // The ratio row stays on screen with several notes selected (the lens
            // itself needs one note, so the keys go grey and the row says why) - a row
            // that disappears is what "the ratio options are gone" looked like.
            check (bar.isRatioRowVisible(),
                   NI_TXT ("the ratio row stays on screen with several notes"));
            check (bar.getRatioRowBoundsForTest().getHeight() >= 18,
                   NI_TXT ("and it really has a row to draw in (got ")
                       + juce::String (bar.getRatioRowBoundsForTest().getHeight()) + NI_TXT (" px)"));
            check (! bar.areRatioKeysEnabledForTest(),
                   NI_TXT ("but its keys are greyed out (the lens needs one note)"));
            check (bar.isRatioRowVisibleForTest() && bar.getBaseHeight() == NoteEditBar::singleNoteHeight,
                   NI_TXT ("and the strip keeps both rows (got ")
                       + juce::String (bar.getBaseHeight()) + NI_TXT (")"));
            check (bar.getTargetTextForTest().contains (NI_TXT ("选中一个音符")),
                   NI_TXT ("the row says why it is grey (got '") + bar.getTargetTextForTest()
                       + NI_TXT ("')"));

            // The keys are the point of the row: they must still have a real width.
            for (int i = 0; i < 5; ++i)
                check (bar.getRatioButtonBoundsForTest (i).getWidth() >= 18,
                       NI_TXT ("ratio key ") + juce::String (i + 1) + NI_TXT (" keeps a usable width (got ")
                           + juce::String (bar.getRatioButtonBoundsForTest (i).getWidth()) + NI_TXT (")"));

            const auto ratioBeforeKey = appState.currentRatio;
            component.keyPressed (juce::KeyPress ('3', juce::ModifierKeys(), 0));
            pump();

            check (std::abs (appState.currentRatio - ratioBeforeKey) < 1.0e-9,
                   "the ratio keys do nothing with several notes selected");

            // Every row has its own mute switch, and it only touches its own note.
            check (bar.getMultiRowMuteBounds (0).getWidth() > 0
                       && bar.getMultiRowMuteBounds (1).getWidth() > 0,
                   "both per-note rows have a mute switch");

            bar.triggerMultiRowMute (1);
            pump();

            const auto mutedOf = [&document, clipId] (juce::int64 noteId)
            {
                if (const auto* c = document.getProject().findClip (clipId))
                    for (const auto& n : c->notes)
                        if (n.id == noteId)
                            return n.muted;

                return true;   // missing note reads as "not what we want"
            };

            check (bar.isMultiRowMuted (1) && mutedOf (secondNoteId) && ! mutedOf (firstNoteId),
                   "muting one row only mutes that note");

            bar.triggerMultiRowMute (1);
            pump();

            check (! mutedOf (secondNoteId), "unmuting the row brings it back");

            bar.applyMultiRowFrequency (1, 495.0);
            pump();

            const auto* clip = document.getProject().findClip (clipId);

            auto frequencyOf = [clip] (juce::int64 noteId)
            {
                if (clip != nullptr)
                    for (const auto& n : clip->notes)
                        if (n.id == noteId)
                            return n.frequencyHz;

                return 0.0;
            };

            checkNear (frequencyOf (secondNoteId), 495.0, 1.0e-6,
                       "editing a row changes that note");
            checkNear (frequencyOf (firstNoteId), 220.0, 1.0e-6,
                       "the other selected note is untouched");

            const auto kindsOf = [&document] (ReferenceLineKind kind)
            {
                return (int) std::count_if (document.getProject().referenceLines.begin(),
                                            document.getProject().referenceLines.end(),
                                            [kind] (const ReferenceLineEntry& e)
                                            { return e.kind == kind; });
            };

            check (kindsOf (ReferenceLineKind::multiNote) == 1 && kindsOf (ReferenceLineKind::noteRatio) == 0,
                   "the multi-note entry replaced the single-note one");

            const auto multiImage = snapshot (component);
            check (writeImage (multiImage, outputDirectory.getChildFile ("10-multi-note.png")),
                   "multi-note snapshot");
            checkColourCount (multiImage, theme::referenceLine, 500,
                              "the selected notes' own families are drawn");

            // Sealing it must survive a move back to a single note.
            auto& panel = component.getEditorPanel().getOptionsPanel();
            auto multiId = (juce::int64) 0;

            for (const auto& entry : document.getProject().referenceLines)
                if (entry.kind == ReferenceLineKind::multiNote)
                    multiId = entry.id;

            check (multiId != 0, "the multi-note status exists");
            appState.selectReferenceLine (multiId);
            pump();

            panel.toggleSelectedLock();
            pump();

            check (document.getProject().findReferenceLine (multiId) != nullptr
                       && document.getProject().findReferenceLine (multiId)->locked,
                   "the multi-note entry can be sealed");

            appState.setSelectedNotes ({ firstNoteId });
            pump();

            check (kindsOf (ReferenceLineKind::noteRatio) == 0,
                   "a sealed multi-note entry blocks the single-note kind");
        }

        // ---- 未锁定的那一行：显示 / 隐藏、独显 要能被后来的选中继承 ------------

        beginTest (NI_TXT ("an unlocked status inherits 隐藏 and 独显 across selections"));

        {
            juce::int64 firstNoteId = 0, secondNoteId = 0, thirdNoteId = 0, clipId = 0;

            document.applyEdit ("selftest inherit setup",
                                [&firstNoteId, &secondNoteId, &thirdNoteId, &clipId] (Project& project)
            {
                project.referenceLines.clear();
                auto& track = project.tracks.front();

                // The 4th-kind entry the previous section sealed would block the
                // derived 2nd kind, so start from a clean project again.
                track.clips.clear();

                auto clip = makeDefaultClip (project);
                clip.startTick = 0;
                clip.lengthTicks = 19200;
                clip.loopLengthTicks = 19200;
                clipId = clip.id;

                const double frequencies[] = { 220.0, 330.0, 440.0 };
                juce::int64* ids[] = { &firstNoteId, &secondNoteId, &thirdNoteId };

                for (int i = 0; i < 3; ++i)
                {
                    Note n;
                    n.id = project.nextNoteId++;
                    n.startTick = (juce::int64) i * 960;
                    n.durationTicks = 480;
                    n.frequencyHz = frequencies[i];
                    n.velocity = 100;
                    clip.notes.push_back (n);
                    *ids[i] = n.id;
                }

                track.clips.push_back (std::move (clip));
            });

            appState.selectTrack (document.getProject().tracks.front().id);
            appState.selectClip (clipId);
            appState.clearNotes();
            pump();

            auto& panel = component.getEditorPanel().getOptionsPanel();

            /** The row that follows the selection (the derived one). */
            const auto derived = [&document] () -> const ReferenceLineEntry*
            {
                for (const auto& e : document.getProject().referenceLines)
                    if (! e.locked && (e.kind == ReferenceLineKind::noteRatio
                                       || e.kind == ReferenceLineKind::multiNote))
                        return &e;

                return nullptr;
            };

            // ---- 隐藏 travels with the selection -----------------------------
            appState.setSelectedNotes ({ firstNoteId });
            pump();

            check (derived() != nullptr && derived()->kind == ReferenceLineKind::noteRatio,
                   NI_TXT ("one selected note brings the 2nd kind"));
            check (derived() != nullptr && derived()->visible,
                   NI_TXT ("and it starts visible"));

            panel.triggerRowButton (0, 1);   // hide
            pump();

            check (derived() != nullptr && ! derived()->visible,
                   NI_TXT ("hiding the row really hides it"));

            appState.setSelectedNotes ({ secondNoteId });
            pump();

            check (derived() != nullptr && derived()->kind == ReferenceLineKind::noteRatio,
                   NI_TXT ("another single note brings the 2nd kind again"));
            check (derived() != nullptr && ! derived()->visible,
                   NI_TXT ("the new row inherits 隐藏 from the previous selection"));

            // ---- 独显 travels too, including across the kind change -----------
            panel.triggerRowButton (0, 2);   // solo
            pump();

            const auto soloedId = appState.soloReferenceLineId;

            check (soloedId != 0 && derived() != nullptr && derived()->id == soloedId,
                   NI_TXT ("独显 marks the row that follows the selection"));

            appState.setSelectedNotes ({ thirdNoteId });
            pump();

            check (derived() != nullptr && appState.soloReferenceLineId == derived()->id,
                   NI_TXT ("a new single note inherits 独显"));

            appState.setSelectedNotes ({ firstNoteId, secondNoteId });
            pump();

            check (derived() != nullptr && derived()->kind == ReferenceLineKind::multiNote,
                   NI_TXT ("two selected notes bring the 4th kind"));
            check (derived() != nullptr && appState.soloReferenceLineId == derived()->id,
                   NI_TXT ("and 独显 carries over to it as well"));
            check (derived() != nullptr && ! derived()->visible,
                   NI_TXT ("so does 隐藏"));

            // ---- cleared selection: the row goes, the settings stay ----------
            appState.clearNotes();
            pump();

            check (derived() == nullptr, NI_TXT ("clearing the selection drops the row"));
            check (appState.soloReferenceLineId == 0,
                   NI_TXT ("a 独显 that named the dropped row is cleared"));

            appState.setSelectedNotes ({ secondNoteId, thirdNoteId });
            pump();

            check (derived() != nullptr && derived()->kind == ReferenceLineKind::multiNote,
                   NI_TXT ("selecting two notes again rebuilds the 4th kind"));
            check (derived() != nullptr && ! derived()->visible,
                   NI_TXT ("the 隐藏 it was given earlier is still remembered"));
            check (derived() != nullptr && appState.soloReferenceLineId == derived()->id,
                   NI_TXT ("and so is 独显"));

            // Put it back the way the rest of the run expects to find it.
            panel.triggerRowButton (0, 2);   // solo off
            panel.triggerRowButton (0, 1);   // visible again
            pump();

            check (derived() != nullptr && derived()->visible
                       && appState.soloReferenceLineId == 0,
                   NI_TXT ("and the row can be shown and un-soloed again"));

            appState.clearNotes();
            pump();
        }

        // ---- several notes: apply them as a reference line group ---------------

        beginTest (NI_TXT ("a multi-note selection can be applied as a reference line group"));

        {
            juce::int64 lowId = 0, highId = 0, clipId = 0, sectionTrackId = 0;
            const double lowHz = 220.0, highHz = 330.0;

            document.applyEdit ("selftest apply group setup", [&] (Project& project)
            {
                project.referenceLines.clear();
                project.tracks.resize (1);

                auto& track = project.tracks.front();
                sectionTrackId = track.id;
                track.clips.clear();

                auto clip = makeDefaultClip (project);
                clip.startTick = 0;
                clip.lengthTicks = 19200;
                clip.loopLengthTicks = 19200;
                clipId = clip.id;

                Note a;
                a.id = project.nextNoteId++;
                a.startTick = 0;
                a.durationTicks = 480;
                a.frequencyHz = lowHz;
                a.velocity = 100;
                lowId = a.id;

                Note b;
                b.id = project.nextNoteId++;
                b.startTick = 960;
                b.durationTicks = 480;
                b.frequencyHz = highHz;
                b.velocity = 100;
                highId = b.id;

                clip.notes = { a, b };
                track.clips.push_back (std::move (clip));
            });

            appState.selectTrack (sectionTrackId);
            appState.selectClip (clipId);
            appState.clearNotes();

            document.viewState().pixelsPerQuarter = 140.0;
            document.viewState().horizontalOffsetTicks = 0.0;
            document.viewState().centreHz = 440.0;
            document.viewState().octavesVisible = 4.0;

            auto& editorPanel = component.getEditorPanel();
            auto& sectionGrid = editorPanel.getGrid();
            auto& bar = editorPanel.getNoteEditBar();
            auto& panel = editorPanel.getOptionsPanel();
            auto& presetPanel = editorPanel.getPresetPanel();

            sectionGrid.refresh();
            pump();

            // The group entry the button made, looked up fresh every time: editing
            // the project can move the vector the row lives in.
            const auto groupEntry = [&document] () -> const ReferenceLineEntry*
            {
                for (const auto& entry : document.getProject().referenceLines)
                    if (entry.kind == ReferenceLineKind::group)
                        return &entry;

                return nullptr;
            };

            const auto rowOfLine = [&panel] (juce::int64 id)
            {
                for (int i = 0; i < 8; ++i)
                    if (panel.getRowLineId (i) == id)
                        return i;

                return -1;
            };

            // One note is not a group.
            appState.setSelectedNotes ({ lowId });
            pump();

            check (bar.getGroupButtonBoundsForTest().isEmpty(),
                   NI_TXT ("the 应用为参考线组 button is absent with a single note"));

            appState.setSelectedNotes ({ lowId, highId });
            pump();

            check (bar.getGroupButtonBoundsForTest().getWidth() > 60
                       && bar.getGroupButtonBoundsForTest().getHeight() > 0,
                   NI_TXT ("it appears with two notes (")
                       + juce::String (bar.getGroupButtonBoundsForTest().getWidth()) + " x "
                       + juce::String (bar.getGroupButtonBoundsForTest().getHeight()) + ")");

            const auto historyBefore = appState.referenceLineHistory.size();
            const auto dotColourBefore = countColour (snapshot (sectionGrid), theme::trackColour (1), 18);
            // The serial keeps counting across the whole session.
            const auto expectedName = NI_TXT ("参考线组 ")
                                          + juce::String (appState.referenceGroupSerial + 1);

            bar.triggerGroupApplyForTest();
            pump();

            check (document.getUndoDescription() == NI_TXT ("应用参考线组"),
                   NI_TXT ("applying is one undo step (got '")
                       + document.getUndoDescription() + "')");

            auto groupCount = 0;

            for (const auto& entry : document.getProject().referenceLines)
                if (entry.kind == ReferenceLineKind::group)
                    ++groupCount;

            check (groupCount == 1, NI_TXT ("one 参考线组 entry appears (got ")
                                         + juce::String (groupCount) + ")");

            auto groupId = (juce::int64) 0;

            if (const auto* group = groupEntry())
            {
                groupId = group->id;

                check (group->locked, NI_TXT ("and it starts sealed"));
                check (group->name == expectedName,
                       NI_TXT ("named from the serial (got '") + group->name + "')");
                checkNear (group->anchorHz, lowHz, 1.0e-9,
                           NI_TXT ("anchored on the lowest note"));
                check (group->dots.size() == 2,
                       NI_TXT ("the anchor plus one interval dot (got ")
                           + juce::String ((int) group->dots.size()) + ")");
            }

            const auto groupRow = rowOfLine (groupId);

            check (groupId != 0 && groupRow >= 0
                       && panel.getRowTypeText (groupRow) == NI_TXT ("参考线组"),
                   NI_TXT ("the control panel lists it as the 3rd kind (row ")
                       + juce::String (groupRow) + ")");
            check (appState.referenceLineHistory.size() == historyBefore + 1,
                   NI_TXT ("and the history gained one entry"));
            check (! appState.referenceLineHistory.empty()
                       && appState.referenceLineHistory.front().name == expectedName,
                   NI_TXT ("carrying the same name"));

            // Its families really are on the canvas: the group draws the dots in
            // their own palette colours, where the 4th kind drew plain lines.
            const auto dotColourAfter = countColour (snapshot (sectionGrid), theme::trackColour (1), 18);

            check (dotColourAfter > dotColourBefore + 200,
                   NI_TXT ("its frequency family is drawn (pixels ")
                       + juce::String (dotColourBefore) + " -> " + juce::String (dotColourAfter) + ")");

            appState.toggleSidePanel (AppState::SidePanel::history);
            pump();

            check (presetPanel.getRowCountForTest() >= 1
                       && presetPanel.getRowTitleForTest (0) == expectedName,
                   NI_TXT ("the history panel lists it (row 0 = '")
                       + presetPanel.getRowTitleForTest (0) + "')");

            const auto groupImage = snapshot (component);
            check (writeImage (groupImage, outputDirectory.getChildFile ("25-apply-group.png")),
                   "applied reference line group snapshot");

            appState.toggleSidePanel (AppState::SidePanel::history);
            pump();

            // Sealed: the next new entry must leave it alone...
            panel.addFixedFrequency (660.0, NI_TXT ("新线"));
            pump();

            check (document.getProject().findReferenceLine (groupId) != nullptr,
                   NI_TXT ("a sealed group outlives the next new entry"));

            // ...but unlock it and it is scratch again, replaced by the next one.
            panel.triggerRowButton (rowOfLine (groupId), 0);
            pump();

            const auto* unsealed = document.getProject().findReferenceLine (groupId);

            check (unsealed != nullptr && ! unsealed->locked,
                   NI_TXT ("the lock button unseals it"));

            panel.addFixedFrequency (880.0, NI_TXT ("又一条"));
            pump();

            check (document.getProject().findReferenceLine (groupId) == nullptr,
                   NI_TXT ("and then a new entry replaces it"));
        }

        // ---- 不吸附: edits land on the tick under the pointer ------------------

        beginTest (NI_TXT ("不吸附 lands edits on the exact tick under the pointer"));

        {
            juce::int64 clipId = 0, sectionTrackId = 0;

            document.applyEdit ("selftest snap setup", [&] (Project& project)
            {
                project.referenceLines.clear();
                project.tracks.resize (1);

                auto& track = project.tracks.front();
                sectionTrackId = track.id;
                track.clips.clear();

                auto clip = makeDefaultClip (project);
                clip.startTick = 0;
                clip.lengthTicks = 19200;
                clip.loopLengthTicks = 19200;
                clipId = clip.id;
                track.clips.push_back (std::move (clip));

                project.referenceLines.push_back (
                    music::makeFixedHzEntry (project.nextReferenceLineId++, 440.0, NI_TXT ("440")));
            });

            appState.selectTrack (sectionTrackId);
            appState.selectClip (clipId);
            appState.clearNotes();

            document.viewState().pixelsPerQuarter = 140.0;
            document.viewState().horizontalOffsetTicks = 0.0;
            document.viewState().centreHz = 440.0;
            document.viewState().octavesVisible = 4.0;

            auto& editorPanel = component.getEditorPanel();
            auto& sectionGrid = editorPanel.getGrid();

            sectionGrid.refresh();
            pump();

            // 1/4 拍 first, so the remembered subdivision is 240 ticks.
            editorPanel.selectSnapForTest (4);
            pump();

            check (document.viewState().editorSnap.enabled
                       && document.viewState().editorSnap.denominator == 4,
                   NI_TXT ("the 1/4 拍 entry sets the subdivision"));

            editorPanel.selectSnapForTest (5);
            pump();

            check (! document.viewState().editorSnap.enabled
                       && document.viewState().editorSnap.denominator == 4,
                   NI_TXT ("不吸附 keeps that subdivision for the default length"));
            check (editorPanel.getSnapBoxIdForTest() == 5,
                   NI_TXT ("and the combo shows it"));

            // A plain click on the 440 line: free position, subdivision length.
            const auto offGridTick = (juce::int64) 1000;
            const auto spot = sectionGrid.pointFor (440.0, offGridTick);

            sectionGrid.mouseDown (makeMouseEvent (sectionGrid, spot, spot));
            sectionGrid.mouseUp (makeMouseEvent (sectionGrid, spot, spot));
            pump();

            check (document.getUndoDescription() == NI_TXT ("写入音符"),
                   NI_TXT ("the write is one undo step (got '")
                       + document.getUndoDescription() + "')");

            if (const auto* clip = document.getProject().findClip (clipId))
            {
                check (clip->notes.size() == 1, NI_TXT ("the click wrote one note (got ")
                                                     + juce::String ((int) clip->notes.size()) + ")");

                if (clip->notes.size() == 1)
                {
                    check (clip->notes.front().startTick == offGridTick,
                           NI_TXT ("which sits on the exact tick under the pointer (got ")
                               + juce::String (clip->notes.front().startTick) + ")");
                    check (clip->notes.front().durationTicks == 240,
                           NI_TXT ("while a plain click still writes one subdivision (got ")
                               + juce::String (clip->notes.front().durationTicks) + ")");
                }
            }

            // Back on 1 拍 the same click rounds to the beat sectionGrid again.
            editorPanel.selectSnapForTest (1);
            pump();

            const auto onGridSpot = sectionGrid.pointFor (880.0, offGridTick);

            sectionGrid.mouseDown (makeMouseEvent (sectionGrid, onGridSpot, onGridSpot));
            sectionGrid.mouseUp (makeMouseEvent (sectionGrid, onGridSpot, onGridSpot));
            pump();

            auto snappedStarts = std::vector<juce::int64>();

            if (const auto* clip = document.getProject().findClip (clipId))
                for (const auto& note : clip->notes)
                    snappedStarts.push_back (note.startTick);

            check (std::find (snappedStarts.begin(), snappedStarts.end(), (juce::int64) 960)
                       != snappedStarts.end(),
                   NI_TXT ("with 1 拍 the note rounds back onto the beat"));

            // And a clip drag stops quantizing too. 283 px is 1941 ticks, which is
            // deliberately off the 240-tick sectionGrid.
            lanes.getSnapControlForTest().setEnabledForTest (false);
            pump();

            const auto body = juce::Point<float> ((float) laneX (960.0), (float) laneY (0));
            const auto target = juce::Point<float> ((float) (laneX (960.0) + 283), (float) laneY (0));

            lanes.mouseDown (makeMouseEvent (lanes, body, body));
            lanes.mouseDrag (makeMouseEvent (lanes, target, body));
            lanes.mouseUp (makeMouseEvent (lanes, target, body));
            pump();

            const auto* track = document.findTrack (sectionTrackId);
            const auto movedStart = track != nullptr && ! track->clips.empty()
                                        ? track->clips.front().startTick
                                        : (juce::int64) -1;

            // The drag delta is 1941 ticks from the mouse-down tick of 960, and the
            // clip started at 0, so it lands on 1941 - not on a 240-tick boundary.
            check (movedStart == 1941,
                   NI_TXT ("a clip drag lands off the grid (got tick ")
                       + juce::String (movedStart) + ")");

            check (document.getUndoDescription() == NI_TXT ("移动片段"),
                   NI_TXT ("the off-grid drag is one undo step (got '")
                       + document.getUndoDescription() + "')");

            document.undo();
            pump();

            const auto* restored = document.findTrack (sectionTrackId);

            check (restored != nullptr && ! restored->clips.empty()
                       && restored->clips.front().startTick == 0,
                   NI_TXT ("and one undo puts the clip back"));

            // Leave the default in place for whatever runs next.
            editorPanel.selectSnapForTest (1);
            lanes.getSnapControlForTest().setEnabledForTest (true);
            pump();
        }

        // ---- one note: ratio lens, mute and comment ---------------------------

        beginTest ("the ratio lens resets to 1:2 and the ratio row comes back");

        {
            juce::int64 noteA = 0, noteB = 0, clipId = 0;

            document.applyEdit ("selftest single note setup",
                                [&noteA, &noteB, &clipId] (Project& project)
            {
                project.referenceLines.clear();

                auto& track = project.tracks.front();
                track.clips.clear();

                auto clip = makeDefaultClip (project);
                clip.startTick = 0;
                clip.lengthTicks = 19200;
                clip.loopLengthTicks = 19200;
                clipId = clip.id;

                Note a;
                a.id = project.nextNoteId++;
                a.startTick = 0;
                a.durationTicks = 960;
                a.frequencyHz = 220.0;
                a.velocity = 100;
                noteA = a.id;

                Note b;
                b.id = project.nextNoteId++;
                b.startTick = 1920;
                b.durationTicks = 960;
                b.frequencyHz = 660.0;
                b.velocity = 100;
                noteB = b.id;

                clip.notes = { a, b };
                track.clips.push_back (std::move (clip));
            });

            appState.clearNotes();
            appState.selectTrack (document.getProject().tracks.front().id);
            appState.selectClip (clipId);
            pump();

            auto& bar = component.getEditorPanel().getNoteEditBar();
            auto& sectionGrid = component.getEditorPanel().getGrid();

            appState.selectNote (noteA, false);
            pump();

            check (bar.isRatioRowVisible(), "the ratio row is back with one note");
            check (bar.getBaseHeight() == NoteEditBar::singleNoteHeight,
                   "the strip is two rows tall again");

            // ...and it has to be *really* laid out, not merely flagged visible. The
            // strip is the same size with no note and with one note (that is what
            // keeps the canvas from jumping), so JUCE never asks the bar to lay out
            // again when a single note is picked: the row used to keep the
            // zero-sized rectangle it was given while nothing was selected - which is
            // what "the ratio options are not shown with one note" looked like.
            check (bar.getRatioRowBoundsForTest().getHeight() >= 18,
                   NI_TXT ("选中一个音符时，比率那一行真的有高度（")
                       + juce::String (bar.getRatioRowBoundsForTest().getHeight())
                       + NI_TXT (" px）"));
            check (bar.isTargetVisibleForTest() && ! bar.getTargetLabelBoundsForTest().isEmpty(),
                   NI_TXT ("右边的目标频率读数也排得下（")
                       + bar.getTargetLabelBoundsForTest().toString() + NI_TXT ("）"));

            auto keysLaidOut = true;

            for (int i = 0; i < 5; ++i)
                keysLaidOut = keysLaidOut && bar.isRatioButtonVisibleForTest (i)
                                  && bar.getRatioButtonBoundsForTest (i).getWidth() >= 18
                                  && bar.getRatioButtonBoundsForTest (i).getHeight() > 0;

            check (keysLaidOut,
                   NI_TXT ("五个比率键都真的排在那一行里（第一个 ")
                       + bar.getRatioButtonBoundsForTest (0).toString() + NI_TXT ("）"));

            checkNear (appState.currentRatio, 2.0, 1.0e-9,
                       "a freshly selected note starts at 1:2");

            // The ratio keys have to survive the tightest realistic layout: the
            // smallest window, with the preset side panel taking its share of the
            // width. Keys squeezed to zero width are what "the ratio options are gone"
            // looked like.
            {
                appState.toggleSidePanel (AppState::SidePanel::presets);
                component.setSize (1120, 950);
                pump();

                auto thinnest = 1000;

                for (int i = 0; i < 5; ++i)
                    thinnest = juce::jmin (thinnest, bar.getRatioButtonBoundsForTest (i).getWidth());

                check (bar.isRatioRowVisible() && bar.areRatioKeysEnabledForTest(),
                       NI_TXT ("one note keeps the ratio row live on the smallest window"));
                check (thinnest >= 18,
                       NI_TXT ("and every ratio key keeps a real width with the preset panel open (thinnest ")
                           + juce::String (thinnest) + NI_TXT (" px)"));

                appState.toggleSidePanel (AppState::SidePanel::presets);
                component.setSize (1460, 950);
                pump();

                check (bar.getRatioButtonBoundsForTest (0).getWidth() >= 40,
                       NI_TXT ("back at the usual size the keys get their normal width (got ")
                           + juce::String (bar.getRatioButtonBoundsForTest (0).getWidth())
                           + NI_TXT (")"));
            }

            // Hotkey 2 is 1:3 (hotkeys run 1..5 over 1:2, 1:3, 1:5, 1:7, 1:9): the
            // own family turns dashed and the two target families appear.
            component.keyPressed (juce::KeyPress ('2', juce::ModifierKeys(), 0));
            pump();

            checkNear (appState.currentRatio, 3.0, 1.0e-9, "the 2 key sets the 1:3 ratio");

            const auto thirdImage = snapshot (component);
            checkColourCount (thirdImage, theme::referenceLineDim, 200,
                              "the 1:3 lens dims the note's own family", 45);

            const auto ratioEntryName = [&document]
            {
                for (const auto& entry : document.getProject().referenceLines)
                    if (entry.kind == ReferenceLineKind::noteRatio)
                        return entry.name;

                return juce::String();
            }();

            check (ratioEntryName.contains ("1:3"),
                   "the status entry shows the current ratio (got '" + ratioEntryName + "')");

            // Another note and back: the lens is back at 1:2.
            appState.selectNote (noteB, false);
            pump();
            appState.selectNote (noteA, false);
            pump();

            checkNear (appState.currentRatio, 2.0, 1.0e-9,
                       "re-selecting the note put the ratio back to 1:2");

        beginTest ("hovering a ratio target line draws the ratio arrow");

        {
            // One note at 220 Hz with the 1:3 lens on, and the view centred on it
            // so both the 660 Hz and the 73.33 Hz target families are on screen.
            appState.selectNote (noteA, false);
            component.keyPressed (juce::KeyPress ('2', juce::ModifierKeys(), 0));
            document.viewState().octavesVisible = 4.0;
            sectionGrid.centreOnSelectedNote();
            sectionGrid.refresh();
            pump();

            checkNear (appState.currentRatio, 3.0, 1.0e-9, "the 1:3 lens is on again");

            const auto hoverAt = [&makeMouseEvent, &sectionGrid] (double hz, juce::int64 tick = 0)
            {
                const auto point = sectionGrid.pointFor (hz, tick);
                sectionGrid.mouseMove (makeMouseEvent (sectionGrid, point, point));
            };

            // Two beats in, so the pointer is away from the canvas edge and the
            // caption has room on its usual side.
            hoverAt (660.0, 1920);
            pump();

            const auto upBounds = sectionGrid.getRatioArrowBoundsForTest();
            const auto upLabelBounds = sectionGrid.getRatioArrowLabelBoundsForTest();
            const auto hoverPoint = sectionGrid.pointFor (660.0, 1920);
            const auto y220 = sectionGrid.pointFor (220.0, 0).y;
            const auto y660 = hoverPoint.y;
            const auto yLower = sectionGrid.pointFor (220.0 / 3.0, 0).y;

            check (! upBounds.isEmpty(), "hovering the 660 Hz target line draws an arrow");
            check (sectionGrid.getRatioArrowLabelForTest() == NI_TXT ("正 1:3"),
                   "the arrow is captioned with the positive ratio (got '"
                       + sectionGrid.getRatioArrowLabelForTest() + "')");
            check (std::abs ((double) upBounds.getY() - (double) y660) <= 3.0
                       && std::abs ((double) upBounds.getBottom() - (double) y220) <= 3.0,
                   "the multiplied arrow runs from the 220 Hz line up to 660 Hz ("
                       + juce::String (upBounds.getY()) + " .. " + juce::String (upBounds.getBottom())
                       + " vs " + juce::String (y660) + " .. " + juce::String (y220) + ")");

            // The shaft sits on the pointer's own column, and the caption hangs off
            // the arrow head rather than the middle of the shaft.
            check (std::abs ((double) upBounds.getCentreX() - (double) hoverPoint.x) <= 2.0,
                   "the shaft follows the pointer's column (shaft x "
                       + juce::String (upBounds.getCentreX()) + " vs pointer "
                       + juce::String (hoverPoint.x) + ")");
            check (! upLabelBounds.isEmpty(), "the caption plate has a real size");
            check (std::abs ((double) upLabelBounds.getCentreY() - (double) y660) <= 2.0,
                   "the caption is centred on the arrow head (label y "
                       + juce::String (upLabelBounds.getCentreY()) + " vs head "
                       + juce::String (y660) + ")");
            check (! upLabelBounds.contains ((int) hoverPoint.x, (int) hoverPoint.y),
                   "the caption never sits under the mouse pointer (label "
                       + upLabelBounds.toString() + " vs pointer " + hoverPoint.toString() + ")");

            const auto arrowImage = snapshot (sectionGrid);
            check (writeImage (arrowImage, outputDirectory.getChildFile ("21-ratio-arrow.png")),
                   "ratio arrow snapshot");
            checkColourCount (arrowImage, theme::accent, 150, "the arrow is drawn", 18);
            check (countColourInRect (arrowImage, theme::accent, upBounds, 18) >= 120,
                   "the arrow pixels are inside the arrow's own bounds ("
                       + juce::String (countColourInRect (arrowImage, theme::accent, upBounds, 18))
                       + ")");

            // The divided family is the mirror image: head below, tail above.
            hoverAt (220.0 / 3.0, 1920);
            pump();

            const auto downBounds = sectionGrid.getRatioArrowBoundsForTest();
            const auto downLabelBounds = sectionGrid.getRatioArrowLabelBoundsForTest();
            const auto hoverLowerPoint = sectionGrid.pointFor (220.0 / 3.0, 1920);

            check (! downBounds.isEmpty(), "hovering the 73.33 Hz target line draws an arrow");
            check (sectionGrid.getRatioArrowLabelForTest() == NI_TXT ("负 1:3"),
                   "the arrow is captioned with the negative ratio (got '"
                       + sectionGrid.getRatioArrowLabelForTest() + "')");
            check (std::abs ((double) downBounds.getY() - (double) y220) <= 3.0
                       && std::abs ((double) downBounds.getBottom() - (double) yLower) <= 3.0,
                   "the divided arrow runs from the 220 Hz line down to 73.33 Hz ("
                       + juce::String (downBounds.getY()) + " .. " + juce::String (downBounds.getBottom())
                       + " vs " + juce::String (y220) + " .. " + juce::String (yLower) + ")");
            check (std::abs ((double) downBounds.getCentreX() - (double) hoverLowerPoint.x) <= 2.0,
                   "the downward shaft also follows the pointer's column");
            check (std::abs ((double) downLabelBounds.getCentreY() - (double) yLower) <= 2.0,
                   "the caption follows the head downwards (label y "
                       + juce::String (downLabelBounds.getCentreY()) + " vs head "
                       + juce::String (yLower) + ")");
            check (! downLabelBounds.contains ((int) hoverLowerPoint.x, (int) hoverLowerPoint.y),
                   "the downward caption also keeps clear of the pointer");

            // Right at the canvas edge there is no room on the usual side, so the
            // caption flips and still avoids the pointer.
            hoverAt (660.0, 0);
            pump();

            const auto edgeLabelBounds = sectionGrid.getRatioArrowLabelBoundsForTest();
            const auto edgePoint = sectionGrid.pointFor (660.0, 0);

            check (! edgeLabelBounds.isEmpty(), "the caption still draws at the edge");
            check (edgeLabelBounds.getX() > edgePoint.x,
                   "with no room on the left the caption flips to the right (label x "
                       + juce::String (edgeLabelBounds.getX()) + " vs pointer "
                       + juce::String (edgePoint.x) + ")");
            check (! edgeLabelBounds.contains ((int) edgePoint.x, (int) edgePoint.y),
                   "the flipped caption keeps clear of the pointer too");

            // The base note's own (dashed, non-interactive) octaves get nothing.
            hoverAt (440.0);
            pump();

            check (sectionGrid.getRatioArrowBoundsForTest().isEmpty(),
                   "hovering the note's own dashed octave draws no arrow");

            // Back to the octave ratio: there are no ratio families any more.
            appState.currentRatio = 2.0;
            sectionGrid.refresh();
            hoverAt (660.0);
            pump();

            check (sectionGrid.getRatioArrowBoundsForTest().isEmpty(),
                   "with the 1:2 ratio there is nothing to explain");

            // Putting the mouse down on the line still writes a note, not an arrow.
            appState.currentRatio = 3.0;
            sectionGrid.refresh();
            hoverAt (660.0);
            pump();

            check (! sectionGrid.getRatioArrowBoundsForTest().isEmpty(), "the arrow is back");

            sectionGrid.mouseExit (makeMouseEvent (sectionGrid, sectionGrid.pointFor (660.0, 0),
                                            sectionGrid.pointFor (660.0, 0)));
            pump();

            check (sectionGrid.getRatioArrowBoundsForTest().isEmpty(),
                   "leaving the canvas clears the arrow");

            const auto clearedImage = snapshot (sectionGrid);
            check (countColourInRect (clearedImage, theme::accent, upBounds, 18)
                       < countColourInRect (arrowImage, theme::accent, upBounds, 18),
                   "the arrow pixels are gone once the mouse leaves");

            // Regression: writing a note is a drag, and the arrow has to come back
            // as soon as the button is released. It used to stay away until the
            // next mouse-down, because the drag flag was never cleared on mouse-up.
            const auto* clipBefore = document.getProject().findClip (clipId);
            const auto notesBefore = clipBefore != nullptr ? (int) clipBefore->notes.size() : 0;
            std::set<juce::int64> idsBefore;

            if (clipBefore != nullptr)
                for (const auto& n : clipBefore->notes)
                    idsBefore.insert (n.id);

            const auto writeFrom = sectionGrid.pointFor (660.0, 3840);
            const auto writeTo = sectionGrid.pointFor (660.0, 4800);

            sectionGrid.mouseDown (makeMouseEvent (sectionGrid, writeFrom, writeFrom));
            sectionGrid.mouseDrag (makeMouseEvent (sectionGrid, writeTo, writeFrom));
            sectionGrid.mouseUp (makeMouseEvent (sectionGrid, writeTo, writeFrom));
            pump();

            const auto* clipAfter = document.getProject().findClip (clipId);
            const auto notesAfter = clipAfter != nullptr ? (int) clipAfter->notes.size() : 0;
            juce::int64 writtenNoteId = 0;

            if (clipAfter != nullptr)
                for (const auto& n : clipAfter->notes)
                    if (idsBefore.find (n.id) == idsBefore.end())
                        writtenNoteId = n.id;

            check (notesAfter == notesBefore + 1,
                   "dragging on the 660 Hz line wrote a note (" + juce::String (notesBefore)
                       + " -> " + juce::String (notesAfter) + ")");
            check (writtenNoteId != 0, "the freshly written note can be identified");

            // Picking a ratio for the freshly written note, then hovering the line
            // it divides down to.
            component.keyPressed (juce::KeyPress ('2', juce::ModifierKeys(), 0));
            pump();

            checkNear (appState.currentRatio, 3.0, 1.0e-9,
                       "the ratio applies to the note that was just written");

            hoverAt (220.0, 3840);
            pump();

            check (! sectionGrid.getRatioArrowBoundsForTest().isEmpty(),
                   "the arrow is back after writing a note (the drag really ended)");
            check (sectionGrid.getRatioArrowLabelForTest() == NI_TXT ("负 1:3"),
                   "the arrow explains the new note's divided family (got '"
                       + sectionGrid.getRatioArrowLabelForTest() + "')");

            // ---- the 指示线 switch at the bottom of the control panel ---------
            auto& optionsPanel = component.getEditorPanel().getOptionsPanel();
            const auto indicatorBounds = optionsPanel.getIndicatorRowBounds();

            check (indicatorBounds.getWidth() > 150
                       && indicatorBounds.getHeight() == OptionsPanel::indicatorRowHeight,
                   NI_TXT ("the 指示线 row is laid out like a status row (")
                       + juce::String (indicatorBounds.getWidth()) + " x "
                       + juce::String (indicatorBounds.getHeight()) + ", expected height "
                       + juce::String (OptionsPanel::indicatorRowHeight) + ")");

            const auto indicatorImage = snapshot (optionsPanel);
            check (writeImage (indicatorImage, outputDirectory.getChildFile ("22-indicator-row.png")),
                   "indicator row snapshot");
            check (countColourInRect (indicatorImage, theme::accent, indicatorBounds, 18) >= 150,
                   "the row carries the same colour bar as a status row ("
                       + juce::String (countColourInRect (indicatorImage, theme::accent,
                                                          indicatorBounds, 18)) + " pixels)");

            check (optionsPanel.isIndicatorVisible(),
                   NI_TXT ("the 指示线 switch starts visible"));
            check (optionsPanel.getIndicatorButtonText() == NI_TXT ("隐藏"),
                   "the button offers to hide the arrow (got '"
                       + optionsPanel.getIndicatorButtonText() + "')");
            check (optionsPanel.getIndicatorStateTextForTest() == NI_TXT ("已显示"),
                   NI_TXT ("the row's second line reads 已显示 (got '")
                       + optionsPanel.getIndicatorStateTextForTest() + "')");

            optionsPanel.triggerIndicator();
            pump();

            check (! optionsPanel.isIndicatorVisible(), "the switch hides the indicator");
            check (sectionGrid.getRatioArrowBoundsForTest().isEmpty(),
                   "no arrow is drawn while the indicator is hidden");
            check (optionsPanel.getIndicatorButtonText() == NI_TXT ("显示"),
                   "the button now offers to show it (got '"
                       + optionsPanel.getIndicatorButtonText() + "')");
            check (optionsPanel.getIndicatorStateTextForTest() == NI_TXT ("已隐藏"),
                   NI_TXT ("the row dims to 已隐藏 (got '")
                       + optionsPanel.getIndicatorStateTextForTest() + "')");

            // Hovering while it is hidden must leave the canvas clean.
            hoverAt (440.0, 3840);
            pump();

            check (sectionGrid.getRatioArrowBoundsForTest().isEmpty(),
                   "hovering a target line while hidden draws nothing");

            optionsPanel.triggerIndicator();
            pump();

            check (optionsPanel.isIndicatorVisible(), "the switch turns it back on");
            check (! sectionGrid.getRatioArrowBoundsForTest().isEmpty(),
                   "the arrow returns as soon as the indicator is shown again");
            check (optionsPanel.getIndicatorStateTextForTest() == NI_TXT ("已显示"),
                   NI_TXT ("and the row reads 已显示 again"));

            // Leave the document as it was found: drop the note this test wrote and
            // put the selection back, so the checks after it still see the original
            // two notes.
            document.applyEdit (NI_TXT ("selftest remove the written note"),
                                [clipId, writtenNoteId] (Project& project)
            {
                if (auto* clip = project.findClip (clipId))
                    clip->notes.erase (std::remove_if (clip->notes.begin(), clip->notes.end(),
                                                       [writtenNoteId] (const Note& n)
                                                       { return n.id == writtenNoteId; }),
                                       clip->notes.end());
            });

            appState.selectNote (noteA, false);
            pump();

            check ((int) document.getProject().findClip (clipId)->notes.size() == notesBefore,
                   "the note written by this test is cleaned up again");
        }

            beginTest ("a muted note is grey, makes no sound and has one undo step");

            const auto gridBefore = snapshot (grid);
            const auto trackColourBefore = countColour (gridBefore, theme::trackColour (0), 30);

            check (trackColourBefore > 400, "the note is drawn in the track colour (pixels = "
                                                + juce::String (trackColourBefore) + ")");

            bar.triggerMute();
            pump();

            const auto isMutedNow = [&document, clipId, noteA]
            {
                if (const auto* c = document.getProject().findClip (clipId))
                    for (const auto& n : c->notes)
                        if (n.id == noteA)
                            return n.muted;

                return false;
            };

            check (bar.isMuteOn() && isMutedNow(), "the mute switch mutes the note");

            const auto gridMuted = snapshot (grid);
            const auto trackColourAfter = countColour (gridMuted, theme::trackColour (0), 30);

            // A muted note is a *hollow* outline in the track colour: the fill is
            // gone (so fewer track-coloured pixels), and the middle of the note no
            // longer reads as the track colour.
            const auto mutedNoteRect = grid.getNoteRectForTest (noteA);

            check (trackColourAfter < trackColourBefore,
                   "muting empties the note's fill (track pixels "
                       + juce::String (trackColourBefore) + " -> "
                       + juce::String (trackColourAfter) + ")");

            if (! mutedNoteRect.isEmpty())
            {
                const auto insidePixel = gridMuted.getPixelAt ((int) mutedNoteRect.getCentreX(),
                                                               (int) mutedNoteRect.getCentreY());
                const auto trackColour = theme::trackColour (0);
                const auto differs = std::abs (insidePixel.getRed() - trackColour.getRed()) > 30
                                         || std::abs (insidePixel.getGreen() - trackColour.getGreen()) > 30
                                         || std::abs (insidePixel.getBlue() - trackColour.getBlue()) > 30;

                check (differs,
                       "the muted note is hollow: its middle is not the track colour");
            }

            check (buildSchedule (document.getProject()).tracks.front().events.size() == 2,
                   "the muted note produces no events");

            document.undo();
            pump();

            check (! isMutedNow() && ! bar.isMuteOn()
                       && countColour (snapshot (grid), theme::trackColour (0), 30)
                              > trackColourBefore / 2,
                   "one undo un-mutes the note");

        beginTest ("a note's comment is drawn above the note, not inside it");

            document.applyEdit ("selftest comment", [clipId, noteA] (Project& project)
            {
                if (auto* c = project.findClip (clipId))
                    for (auto& n : c->notes)
                        if (n.id == noteA)
                            n.comment = NI_TXT ("备注文字");
            });
            pump();

            const auto noteRect = grid.getNoteRectForTest (noteA);
            const auto commentRect = grid.getCommentRectForTest (noteA);

            check (! noteRect.isEmpty() && ! commentRect.isEmpty(),
                   "both rectangles exist");
            check (! commentRect.toFloat().intersects (noteRect),
                   "the comment rectangle does not overlap the note");
            check (commentRect.getBottom() <= (int) noteRect.getY(),
                   "the comment sits above the note");

            const auto gridWithComment = snapshot (grid);
            // A handful of pixels is enough: CJK glyphs at 9.5 px are thin, and the
            // point of the check is "drawn here at all" rather than a glyph count.
            check (countColourInRect (gridWithComment, theme::text, commentRect, 40) > 3,
                   "the comment text is actually painted there (pixels = "
                       + juce::String (countColourInRect (gridWithComment, theme::text,
                                                          commentRect, 40)) + ")");
            check (countColourInRect (gridWithComment, theme::text,
                                      noteRect.toNearestInt().reduced (2, 2), 40) < 30,
                   "nothing is painted inside the note itself");

            check (writeImage (gridWithComment, outputDirectory.getChildFile ("11-mute-comment.png")),
                  "muted / comment snapshot");
        }

        // ---- the product name -------------------------------------------------

        beginTest (NI_TXT ("the software calls itself HM"));

        {
            if (auto* app = juce::JUCEApplication::getInstance())
                check (app->getApplicationName() == NI_TXT ("HM"),
                       NI_TXT ("the application name is HM (got '")
                           + app->getApplicationName() + "')");
            else
                check (false, NI_TXT ("there is no application instance to ask"));

            // The window title / taskbar name comes from the same string, and the
            // toolbar strip shows it next to the project name.
            auto& toolbar = component.getTopToolbar();
            toolbar.refresh();
            pump();

            check (toolbar.getTitleTextForTest()
                       == document.getProjectName() + NI_TXT ("  ·  HM"),
                   NI_TXT ("the toolbar title names it too (got '")
                       + toolbar.getTitleTextForTest() + "')");
        }

        // ---- the transport: loop region and metronome ------------------------

        beginTest (NI_TXT ("the transport carries the loop region and the metronome"));

        {
            auto& transport = component.getTransportBar();
            auto& ruler = component.getEditorPanel().getRuler();

            document.applyEdit ("selftest transport setup", [] (Project& project)
            {
                project.loop = {};
                project.view.metronomeEnabled = false;
            });

            document.viewState().pixelsPerQuarter = 140.0;
            document.viewState().horizontalOffsetTicks = 0.0;
            document.viewState().trackSnap.enabled = true;
            document.viewState().trackSnap.denominator = 1;
            document.viewState().editorSnap.enabled = true;
            document.viewState().editorSnap.denominator = 1;
            document.viewState().controllerSnap.enabled = true;
            document.viewState().controllerSnap.denominator = 1;

            transport.refresh();
            pump();

            const auto xOf = [&document] (juce::int64 tick)
            {
                const auto& view = document.viewState();
                return (int) std::round ((double) tick * view.pixelsPerQuarter
                                         / (double) kTicksPerQuarter);
            };

            check (transport.getLoopButtonBoundsForTest().getWidth() > 40
                       && transport.getLoopButtonBoundsForTest().getHeight() > 0,
                   NI_TXT ("the 循环 button is laid out (")
                       + juce::String (transport.getLoopButtonBoundsForTest().getWidth()) + " px)");
            check (transport.getMetronomeButtonBoundsForTest().getWidth() > 40
                       && transport.getMetronomeButtonBoundsForTest().getHeight() > 0,
                   NI_TXT ("the 节拍器 button is laid out (")
                       + juce::String (transport.getMetronomeButtonBoundsForTest().getWidth()) + " px)");
            check (transport.getLoopTextForTest() == NI_TXT ("未设置"),
                   NI_TXT ("the loop read-out starts empty (got '")
                       + transport.getLoopTextForTest() + "')");

            // Force a paint so the band bounds are up to date.
            (void) snapshot (ruler);
            check (ruler.getLoopBandBoundsForTest().isEmpty(),
                   NI_TXT ("and nothing is drawn on the ruler yet"));

            // ---- shift drag on the ruler draws out the cycle ------------------
            const juce::ModifierKeys shift (juce::ModifierKeys::shiftModifier);
            const auto y = ruler.getHeight() / 2;
            const auto down = juce::Point<float> ((float) xOf (960), (float) y);
            const auto move = juce::Point<float> ((float) xOf (3840), (float) y);

            ruler.mouseDown (makeMouseEvent (ruler, down, down, shift));
            ruler.mouseDrag (makeMouseEvent (ruler, move, down, shift));
            pump();

            check (! document.getProject().loop.isValid(),
                   NI_TXT ("nothing is committed while the drag is in flight"));

            (void) snapshot (ruler);
            check (! ruler.getLoopBandBoundsForTest().isEmpty(),
                   NI_TXT ("the band previews during the drag"));

            const auto undoBefore = document.getUndoDescription();

            ruler.mouseUp (makeMouseEvent (ruler, move, down, shift));
            pump();
            transport.refresh();

            const auto& loop = document.getProject().loop;

            check (loop.startTick == 960 && loop.endTick == 3840 && loop.enabled,
                   NI_TXT ("the drag set the cycle (")
                       + juce::String (loop.startTick) + " - " + juce::String (loop.endTick) + ")");
            check (document.getUndoDescription() == NI_TXT ("设置循环区间")
                       && document.getUndoDescription() != undoBefore,
                   NI_TXT ("and it is one undo step (got '")
                       + document.getUndoDescription() + "')");
            check (transport.getLoopTextForTest() == NI_TXT ("1.2 → 2.1"),
                   NI_TXT ("the read-out follows (got '") + transport.getLoopTextForTest() + "')");

            (void) snapshot (ruler);

            const auto band = ruler.getLoopBandBoundsForTest();

            check (std::abs (band.getX() - xOf (960)) <= 2
                       && std::abs (band.getWidth() - (xOf (3840) - xOf (960))) <= 3,
                   NI_TXT ("the band covers the region (x ")
                       + juce::String (band.getX()) + " w " + juce::String (band.getWidth()) + ")");
            check (band.getHeight() == ruler.getHeight(),
                   NI_TXT ("and it spans the ruler"));

            // ---- the loop button toggles it, without losing the region --------
            transport.triggerLoopButtonForTest();
            pump();

            check (! document.getProject().loop.enabled
                       && document.getProject().loop.isValid(),
                   NI_TXT ("the button turns the loop off and keeps the region"));

            transport.triggerLoopButtonForTest();
            pump();

            check (document.getProject().loop.enabled, NI_TXT ("and back on again"));

            const auto loopImage = snapshot (component);
            check (writeImage (loopImage, outputDirectory.getChildFile ("26-loop-metronome.png")),
                   "loop / metronome snapshot");

            // ---- the metronome toggle ----------------------------------------
            transport.triggerMetronomeButtonForTest();
            pump();

            check (document.getProject().view.metronomeEnabled,
                   NI_TXT ("the metronome button turns the click on"));

            transport.triggerMetronomeButtonForTest();
            pump();

            check (! document.getProject().view.metronomeEnabled,
                   NI_TXT ("and off again"));

            // ---- shift click clears the region --------------------------------
            ruler.mouseDown (makeMouseEvent (ruler, down, down, shift));
            ruler.mouseUp (makeMouseEvent (ruler, down, down, shift));
            pump();
            transport.refresh();

            check (! document.getProject().loop.isValid()
                       && ! document.getProject().loop.enabled,
                   NI_TXT ("a shift click clears the cycle"));
            check (transport.getLoopTextForTest() == NI_TXT ("未设置"),
                   NI_TXT ("and the read-out goes back to 未设置"));

            // ---- with no region the button cycles the bar the playhead is in --
            transport.triggerLoopButtonForTest();
            pump();

            const auto& created = document.getProject().loop;

            check (created.isValid() && created.enabled
                       && created.endTick == created.startTick + 3840,
                   NI_TXT ("with nothing set it cycles the current bar (")
                       + juce::String (created.startTick) + " - "
                       + juce::String (created.endTick) + ")");
        }

        // ---- the instrument column is the lane header ------------------------

        // ---- track level mute / solo -----------------------------------------

        beginTest (NI_TXT ("the lane header carries M and S"));

        {
            auto& sectionLanes = component.getTrackArea();

            // Start from two tracks so solo has something to silence.
            while (! document.getProject().tracks.empty())
                sectionLanes.deleteTrackNow (document.getProject().tracks.front().id);

            const auto firstId = sectionLanes.addTrack (TrackKind::synth);
            const auto secondId = sectionLanes.addTrack (TrackKind::synth);
            pump();

            check (firstId != 0 && secondId != 0
                       && (int) document.getProject().tracks.size() == 2,
                   NI_TXT ("two tracks to solo between"));

            auto& firstHeader = *sectionLanes.getLaneHeaderForTest (0);
            auto& secondHeader = *sectionLanes.getLaneHeaderForTest (1);

            check (firstHeader.getControlBoundsForTest (5).getWidth() > 0
                       && firstHeader.getControlBoundsForTest (6).getWidth() > 0,
                   NI_TXT ("the M and S buttons have real sizes"));

            // ---- mute ---------------------------------------------------------
            firstHeader.pressMuteForTest();
            pump();

            const auto* first = document.findTrack (firstId);

            check (first != nullptr && first->muted && firstHeader.isMuteOnForTest(),
                   NI_TXT ("M mutes the track and lights up"));
            check (document.getUndoDescription() == NI_TXT ("静音音轨"),
                   NI_TXT ("muting is one undo step (got '")
                       + document.getUndoDescription() + "')");
            check ((int) buildSchedule (document.getProject()).tracks.size() == 1,
                   NI_TXT ("a muted track is not in the schedule"));

            document.undo();
            pump();

            check (! document.findTrack (firstId)->muted,
                   NI_TXT ("one undo brings the track back"));

            // ---- solo ---------------------------------------------------------
            secondHeader.pressSoloForTest();
            pump();

            const auto soloed = buildSchedule (document.getProject());

            check (document.findTrack (secondId)->soloed && secondHeader.isSoloOnForTest(),
                   NI_TXT ("S solos the track and lights up"));
            check ((int) soloed.tracks.size() == 1,
                   NI_TXT ("solo leaves only the soloed track in the schedule"));

            if (! soloed.tracks.empty())
                check (soloed.tracks.front().trackId == secondId,
                       NI_TXT ("and it is the right one"));

            const auto soloImage = snapshot (component);
            check (writeImage (soloImage, outputDirectory.getChildFile ("28-mute-solo.png")),
                   "mute / solo snapshot");

            // Mute still wins when the same track is soloed too.
            secondHeader.pressMuteForTest();
            pump();

            check (document.findTrack (secondId)->muted
                       && buildSchedule (document.getProject()).tracks.empty(),
                   NI_TXT ("mute beats solo on the same track"));

            secondHeader.pressMuteForTest();
            secondHeader.pressSoloForTest();
            pump();

            check (! document.findTrack (secondId)->muted
                       && ! document.findTrack (secondId)->soloed
                       && buildSchedule (document.getProject()).tracks.size() == 2,
                   NI_TXT ("turning both off brings everything back"));

            // ---- the state is saved with the project --------------------------
            // Give both tracks a note, then mute the first one.
            document.applyEdit ("selftest mute solo notes", [firstId, secondId] (Project& project)
            {
                if (auto* target = project.findTrack (firstId))
                {
                    Note note;
                    note.id = project.nextNoteId++;
                    note.durationTicks = 480;
                    note.frequencyHz = 220.0;
                    target->clips.front().notes = { note };
                    target->muted = true;
                }

                if (auto* target = project.findTrack (secondId))
                {
                    Note note;
                    note.id = project.nextNoteId++;
                    note.durationTicks = 480;
                    note.frequencyHz = 660.0;
                    target->clips.front().notes = { note };
                }
            });
            pump();

            Project sectionProject;
            juce::String sectionError;

            check (projectFromJson (projectToJson (document.getProject()), sectionProject, sectionError), sectionError);
            check (sectionProject.tracks.size() == 2 && sectionProject.tracks.front().muted
                       && ! sectionProject.tracks.back().muted,
                   NI_TXT ("the project file keeps the M / S state"));

            // ---- a muted stem is skipped, and named in the report --------------
            {
                const auto folder = outputDirectory.getChildFile ("stems-mute");
                ExportSettings settings;
                settings.format = ExportSettings::Format::wav;
                settings.bitsPerSample = 24;
                settings.master = true;
                settings.trackIds = { firstId, secondId };

                std::vector<juce::File> written;
                std::vector<juce::String> skipped;
                juce::String sectionExportError;

                // Evaluate the call first: the message would otherwise be built
                // before the export had a chance to fill the sectionError string in.
                const auto exported = component.runExport (settings, folder, true, sectionExportError,
                                                           &written, &skipped);

                check (exported,
                       NI_TXT ("the batch exports around the muted track: ") + sectionExportError);
                check ((int) written.size() == 2,
                       NI_TXT ("the master mix plus the one audible stem"));
                check ((int) skipped.size() == 1,
                       NI_TXT ("and one track was reported as skipped"));

                if (! skipped.empty())
                    check (skipped.front() == document.findTrack (firstId)->name,
                           NI_TXT ("the skipped one is the muted track"));
            }
        }

        beginTest ("the instrument header carries the track controls");

        {
            auto& sectionLanes = component.getTrackArea();

            auto* header = sectionLanes.getLaneHeaderForTest (0);
            check (header != nullptr, "the first lane has an instrument header");

            if (header != nullptr)
            {
                const char* names[] = { "name", "timbre", "volume", "pan", "delete", "M", "S" };

                for (int which = 0; which < 7; ++which)
                {
                    const auto bounds = header->getControlBoundsForTest (which);
                    check (bounds.getWidth() > 0 && bounds.getHeight() > 0,
                           juce::String ("the header's ") + names[which] + " control has a real size ("
                               + juce::String (bounds.getWidth()) + " x "
                               + juce::String (bounds.getHeight()) + ")");
                }

                const auto headerTrackId = header->getTrackId();

                header->setNameForTest (NI_TXT ("改过的名字"));
                header->setVolumeForTest (-12.0);
                header->setPanForTest (-0.5);
                pump();

                const auto* track = document.findTrack (headerTrackId);

                check (track != nullptr && track->name == NI_TXT ("改过的名字")
                           && std::abs (track->volumeDb + 12.0f) < 1.0e-3f
                           && std::abs (track->pan + 0.5f) < 1.0e-3f,
                       "the header edits the track it belongs to");

                // The timbre button must ask for its own track.
                juce::int64 requested = 0;
                const auto original = sectionLanes.onEditTimbre;
                sectionLanes.onEditTimbre = [&requested] (juce::int64 id) { requested = id; };

                header->pressTimbreForTest();
                sectionLanes.onEditTimbre = original;

                check (requested == headerTrackId, "the timbre button asks for its own track");
            }

            // The instrument list is gone: the editor now starts at the window edge.
            check (component.getEditorPanel().getX() == 0,
                   "the editor panel starts at x = 0 (got "
                       + juce::String (component.getEditorPanel().getX()) + ")");

            const auto items = sectionLanes.getAddMenuItemsForTest();

            check (items.size() == 8, NI_TXT ("the + menu offers eight kinds (got ")
                                          + juce::String ((int) items.size()) + ")");

            if (items.size() == 8)
            {
                check (items[0].first == NI_TXT ("混合合成器")
                           && items[1].first == NI_TXT ("迷你穆格")
                           && items[2].first == NI_TXT ("FM合成器")
                           && items[3].first == NI_TXT ("鼓机")
                           && items[4].first == NI_TXT ("采样器")
                           && items[5].first == NI_TXT ("粒子合成器")
                           && items[6].first == NI_TXT ("音频")
                           && items[7].first == NI_TXT ("VST 乐器"),
                        NI_TXT ("the entries name the eight kinds (got '")
                           + items[0].first + "', '" + items[1].first + "', '"
                           + items[2].first + "', '" + items[3].first + "', '"
                           + items[4].first + "', '" + items[5].first + "', '"
                           + items[6].first + "', '" + items[7].first + "')");
                check (items[0].second && items[1].second && items[2].second
                           && items[3].second && items[4].second && items[5].second
                           && items[6].second && items[7].second,
                       NI_TXT ("all eight track kinds can be created"));
            }
        }

        beginTest (NI_TXT ("the Minimoog instrument has its four-section panel"));

        {
            auto& sectionLanes = component.getTrackArea();
            const auto miniId = sectionLanes.addTrack (TrackKind::minimoog);
            pump();

            auto* miniTrack = document.findTrack (miniId);
            check (miniTrack != nullptr && miniTrack->kind == TrackKind::minimoog,
                   NI_TXT ("the new-track entry makes a Minimoog track"));
            check (miniTrack != nullptr && miniTrack->minimoog.name == NI_TXT ("经典主音"),
                   NI_TXT ("a new Minimoog track starts on the classic lead (got '")
                       + (miniTrack != nullptr ? miniTrack->minimoog.name : juce::String())
                       + "')");

            SynthWindow window (document, component.getAudioEngine(),
                                component.getPresetStore(), false);
            window.showForTrack (miniId, miniTrack != nullptr ? miniTrack->name
                                                             : juce::String ("Mini"),
                                 TrackKind::minimoog);
            pump();

            auto& editor = window.getMinimoogEditor();

            auto sizedControls = 0;

            for (int which = 1; which <= 20; ++which)
            {
                const auto bounds = editor.getControlBoundsForTest (which);

                if (bounds.getWidth() > 0 && bounds.getHeight() > 0)
                    ++sizedControls;
            }

            check (sizedControls == 20,
                   NI_TXT ("the Minimoog panel lays out its controls (got ")
                       + juce::String (sizedControls) + ")");

            check (writeImage (snapshot (editor), outputDirectory.getChildFile ("62-minimoog.png")),
                   "Minimoog editor snapshot");

            const auto counts = editor.getPresetCountsForTest();
            check (counts.first == 30, NI_TXT ("the Minimoog bank has 30 sounds (got ")
                                            + juce::String (counts.first) + ")");

            editor.selectPresetForTest (kBuiltInPresetIdBase + 2);   // 深潜贝斯
            pump();

            check (document.findTrack (miniId)->minimoog.name == NI_TXT ("深潜贝斯"),
                   NI_TXT ("picking a Minimoog preset names the patch (got '")
                       + document.findTrack (miniId)->minimoog.name + "')");

            editor.setPolyphonyForTest (8);
            pump();
            check (editor.getPolyphonyForTest() == 8,
                   NI_TXT ("the polyphony selector writes the voice count"));

            editor.setParamForTest (10, -1.0);    // cutoff knob down
            pump();
            check (document.findTrack (miniId)->minimoog.filterCutoffHz < 400.0f,
                   NI_TXT ("the cutoff knob writes back (got ")
                       + juce::String (document.findTrack (miniId)->minimoog.filterCutoffHz, 1)
                       + " Hz)");
        }

        beginTest (NI_TXT ("the hybrid instrument has a real four-slot FX page"));

        {
            auto& sectionLanes = component.getTrackArea();
            const auto hybridId = sectionLanes.addTrack (TrackKind::hybrid);
            pump();

            auto* hybridTrack = document.findTrack (hybridId);
            check (hybridTrack != nullptr && hybridTrack->kind == TrackKind::hybrid,
                   NI_TXT ("a hybrid track was created for the FX page"));

            SynthWindow window (document, component.getAudioEngine(),
                                component.getPresetStore(), false);
            window.showForTrack (hybridId, hybridTrack != nullptr ? hybridTrack->name
                                                                 : juce::String ("Hybrid"),
                                 TrackKind::hybrid);
            pump();

            auto& hybridEditor = window.getHybridEditor();
            check (hybridEditor.getHeight() == HybridEditor::kDesignHeight,
                   NI_TXT ("the hybrid page is 820 px tall"));
            check (hybridEditor.getEnvelopeTabCountForTest() == 4
                       && hybridEditor.getLfoTabCountForTest() == 6,
                   NI_TXT ("four envelope tabs and six LFO tabs are present"));
            const auto envPanel = hybridEditor.getEnvelopePanelBoundsForTest();
            const auto filterPanel = hybridEditor.getFilterPanelBoundsForTest();
            const auto lfoPanel = hybridEditor.getLfoPanelBoundsForTest();
            check (envPanel.getWidth() > 0 && envPanel.getHeight() > 0
                       && filterPanel.getWidth() > 0 && filterPanel.getHeight() > 0
                       && lfoPanel.getWidth() > 0 && lfoPanel.getHeight() > 0,
                   NI_TXT ("envelope, filter and LFO panels all have real sizes"));
            const auto firstCard = hybridEditor.getLayerCardBoundsForTest (0);
            check (firstCard.getWidth() <= 190,
                   NI_TXT ("the main oscillator cards are narrower"));
            check (hybridEditor.getThumbnailBoundsForTest (0).getHeight() >= 80,
                   NI_TXT ("the main thumbnail is taller"));
            check (filterPanel.getY() == firstCard.getY()
                       && filterPanel.getX() >= firstCard.getRight(),
                   NI_TXT ("the filter panel sits on the source row, right of the oscillators"));
            check (envPanel.getY() >= firstCard.getBottom(),
                   NI_TXT ("the envelope panel sits below the oscillator row"));
            check (envPanel.getRight() <= lfoPanel.getX(),
                   NI_TXT ("the envelope panel sits left of the LFO panel"));
            check (hybridEditor.getFilterTabCountForTest() == 2,
                   NI_TXT ("the filter panel has F1/F2 tabs"));
            check (hybridEditor.getLfoDepthBoundsForTest().getWidth() > 0
                       && hybridEditor.getLfoFadeBoundsForTest().getWidth() > 0,
                   NI_TXT ("LFO depth and fade knobs have real sizes"));
            if (hybridTrack != nullptr)
            {
                hybridTrack->hybrid.filters[0].cutoffHz = 777.0f;
                hybridTrack->hybrid.filters[1].cutoffHz = 4444.0f;
                hybridEditor.selectFilterForTest (1);
                check (std::abs (hybridEditor.getActiveFilterCutoffForTest() - 4444.0f) < 0.1f,
                       NI_TXT ("Filter 2 tab edits the second filter"));
                hybridEditor.selectFilterForTest (0);
                check (std::abs (hybridEditor.getActiveFilterCutoffForTest() - 777.0f) < 0.1f,
                       NI_TXT ("Filter 1 tab edits the first filter"));
            }
            check (hybridEditor.getEditButtonBoundsForTest (0).getWidth() > 0,
                   NI_TXT ("the main oscillator edit button has a real size"));
            for (int which = 0; which < 3; ++which)
                check (hybridEditor.getLayerKnobBoundsForTest (0, which).getWidth() > 0,
                       NI_TXT ("unison / detune / position knobs have real sizes"));
            for (int which = 0; which < 2; ++which)
                check (hybridEditor.getLayerKnobLabelBoundsForTest (0, which).getWidth() > 0,
                       NI_TXT ("level and pan knobs both have text labels"));
            hybridEditor.toggleThumbnailForTest (0);
            check (hybridEditor.getThumbnailThreeDForTest (0),
                   NI_TXT ("the thumbnail switches to 3D"));
            if (hybridTrack != nullptr)
            {
                hybridTrack->hybrid.layers[0].wavetable.oscs[0].position = 0.7f;
                hybridEditor.refresh();
                check (hybridEditor.getThumbnailThreeDForTest (0),
                       NI_TXT ("changing a knob does not leave 3D thumbnail mode"));
            }

            if (auto* thumbnail = hybridEditor.getThumbnailForTest (0))
            {
                (void) snapshot (*thumbnail);
                const auto extent = thumbnail->getThreeDExtentForTest();
                check (! extent.isEmpty()
                           && extent.getX() >= -0.5f && extent.getY() >= -0.5f
                           && extent.getRight() <= (float) thumbnail->getWidth() + 1.0f
                           && extent.getBottom() <= (float) thumbnail->getHeight() + 1.0f,
                       NI_TXT ("every 3D slice stays inside the thumbnail (")
                           + extent.toString() + NI_TXT (" in ")
                           + thumbnail->getBounds().toString() + ")");
                check (extent.getHeight() < (float) thumbnail->getHeight() * 0.85f,
                       NI_TXT ("the narrowed 3D stack leaves room above and below"));

                if (hybridTrack != nullptr)
                {
                    hybridTrack->hybrid.layers[0].wavetable.oscs[0].position = 0.05f;
                    hybridEditor.refresh();
                    (void) snapshot (*thumbnail);
                    const auto lowFrame = thumbnail->getActiveFrameForTest();

                    hybridTrack->hybrid.layers[0].wavetable.oscs[0].position = 0.95f;
                    hybridEditor.refresh();
                    (void) snapshot (*thumbnail);
                    const auto highFrame = thumbnail->getActiveFrameForTest();

                    check (lowFrame >= 0 && highFrame >= 0 && lowFrame != highFrame,
                           NI_TXT ("moving the wavetable position moves the highlighted 3D slice"));
                }
            }

            auto& hybridPresetBar = hybridEditor.getPresetBarForTest();
            hybridPresetBar.refresh();
            check (hybridPresetBar.getCategoryCountForTest() == 8,
                   NI_TXT ("the factory catalogue has eight second-level categories"));
            check (hybridPresetBar.isMenuModeForTest(),
                   NI_TXT ("the preset bar is a two-level category menu"));
            check (hybridPresetBar.getVisibleEntryCountForTest()
                       == hybridpresets::factoryPresetCount(),
                   NI_TXT ("the preset list starts with every factory sound"));
            check (hybridPresetBar.describeMenuForTest().contains (NI_TXT ("钢琴与键盘=6"))
                       && hybridPresetBar.describeMenuForTest().contains (NI_TXT ("贝斯=6")),
                   NI_TXT ("every category holds six sounds (" )
                       + hybridPresetBar.describeMenuForTest() + NI_TXT (")"));

            const auto firstBassId = hybridPresetBar.getVisibleEntryIdForTest (12);
            check (firstBassId > 0 && hybridPresetBar.loadPreset (firstBassId),
                   NI_TXT ("a factory sound loads from the category menu"));
            pump();
            check (hybridTrack != nullptr
                       && hybridTrack->hybrid.name == hybridpresets::name (12),
                   NI_TXT ("the chosen factory sound reaches the instrument"));
            check (writeImage (snapshot (hybridEditor),
                               outputDirectory.getChildFile ("56-hybrid-presets.png")),
                   NI_TXT ("factory preset catalogue snapshot"));

            check (hybridEditor.getFxButtonBoundsForTest().getWidth() > 0,
                   NI_TXT ("the FX button has a real size"));
            check (writeImage (snapshot (hybridEditor),
                               outputDirectory.getChildFile ("54-hybrid-main.png")),
                   NI_TXT ("hybrid main page snapshot"));

            const auto hadCustomTable = hybridTrack != nullptr
                                            && hybridTrack->hybrid.layers[0]
                                                   .wavetable.oscs[0].customTableId != 0;

            hybridEditor.openWaveEditorForTest (0);
            pump();

            const auto waveBounds = hybridEditor.getEmbeddedWaveEditorBoundsForTest();
            const auto backBounds = hybridEditor.getEmbedBackBoundsForTest();
            check (! waveBounds.isEmpty() && ! backBounds.isEmpty()
                       && ! waveBounds.intersects (backBounds)
                       && backBounds.getBottom() <= waveBounds.getY(),
                   NI_TXT ("the return button has its own strip above the wave editor"));

            auto* wavePanel = hybridEditor.getEmbeddedWaveEditorForTest();
            check (wavePanel != nullptr
                       && wavePanel->getToolbarBoundsForTest (5).getWidth() > 0,
                   NI_TXT ("the waveform preset button has a real size"));

            if (wavePanel != nullptr && hybridTrack != nullptr)
            {
                const auto tablesBefore = document.getProject().customWavetables.size();
                const auto tableIdBefore =
                    hybridTrack->hybrid.layers[0].wavetable.oscs[0].customTableId;

                wavePanel->triggerWaveformPresetForTest (12);
                pump();

                const auto tableIdAfter =
                    hybridTrack->hybrid.layers[0].wavetable.oscs[0].customTableId;
                const auto* built = document.getProject().findCustomWavetable (tableIdAfter);

                check (document.getProject().customWavetables.size() == tablesBefore + 1,
                       NI_TXT ("a waveform preset becomes one new project table"));
                check (tableIdAfter != 0 && tableIdAfter != tableIdBefore
                           && built != nullptr && built->frameCount == 8,
                       NI_TXT ("the multi-frame waveform preset builds eight frames"));
                check (built != nullptr && built->name == waveformpresets::name (12),
                       NI_TXT ("the new table carries the preset name"));
                check (document.getUndoDescription() == NI_TXT ("载入波形预设"),
                       NI_TXT ("the waveform preset is one undo entry (got '")
                           + document.getUndoDescription() + "')");
                check (writeImage (snapshot (hybridEditor),
                                   outputDirectory.getChildFile ("57-hybrid-wavetable-editor.png")),
                       NI_TXT ("embedded wave editor snapshot"));

                document.undo();
                pump();

                auto* afterUndo = document.findTrack (hybridId);
                check (afterUndo != nullptr
                           && afterUndo->hybrid.layers[0].wavetable.oscs[0].customTableId
                                  == tableIdBefore,
                       NI_TXT ("undo returns the oscillator to its previous table"));
            }

            hybridEditor.closeWaveEditorForTest();
            pump();

            // Opening the editor on a built-in table makes a project copy. The
            // rest of the self test counts the project's tables, so put that copy
            // back the way it was when this check is done.
            if (! hadCustomTable)
            {
                document.undo();
                pump();
            }

            hybridTrack = document.findTrack (hybridId);

            hybridEditor.assignModulationForTest ((int) HybridModSource::lfo1,
                                                  (int) HybridModDestination::layer1Position);
            pump();

            auto assignmentFound = false;

            if (hybridTrack != nullptr)
                for (const auto& slot : hybridTrack->hybrid.modSlots)
                    if (slot.enabled && slot.source == HybridModSource::lfo1
                        && slot.destination == HybridModDestination::layer1Position)
                        assignmentFound = true;

            check (assignmentFound,
                   NI_TXT ("right-click assignment reaches the modulation matrix"));

            if (hybridTrack != nullptr)
            {
                hybridTrack->hybrid.layers[0].wavetable.oscs[0].positionDiscrete = true;
                hybridEditor.refresh();
                pump();
                check (hybridEditor.getPositionDiscreteForTest (0),
                       NI_TXT ("position mode can be switched to discrete"));
            }

            hybridEditor.showFxPageForTest();
            pump();

            auto* fxPanel = hybridEditor.getFxPanelForTest();
            check (fxPanel != nullptr && fxPanel->isVisible(),
                   NI_TXT ("the FX page opens inside the hybrid editor"));

            if (fxPanel != nullptr)
            {
                for (int slot = 0; slot < 4; ++slot)
                {
                    const auto bounds = fxPanel->getSlotBoundsForTest (slot);
                    check (bounds.getWidth() > 0 && bounds.getHeight() > 0,
                           NI_TXT ("FX slot ") + juce::String (slot + 1)
                               + NI_TXT (" has a real size"));
                }

                fxPanel->setTypeForTest (0, (int) HybridFxType::reverb);
                pump();
                check (fxPanel->getTypeForTest (0) == (int) HybridFxType::reverb,
                       NI_TXT ("choosing reverb updates the first slot"));
                check (hybridTrack != nullptr
                           && hybridTrack->hybrid.fxSlots[0].type == HybridFxType::reverb,
                       NI_TXT ("and the project model receives the same type"));
                check (fxPanel->getEnabledCountForTest() == 0,
                       NI_TXT ("all four FX slots start disabled"));
                check (writeImage (snapshot (*fxPanel),
                                   outputDirectory.getChildFile ("52-hybrid-fx.png")),
                       NI_TXT ("hybrid FX page snapshot"));
            }

            hybridEditor.showMixerPageForTest();
            pump();
            auto* mixerPanel = hybridEditor.getMixerPanelForTest();
            check (mixerPanel != nullptr && mixerPanel->isVisible(),
                   NI_TXT ("the mixer overview opens inside the hybrid editor"));

            if (mixerPanel != nullptr)
            {
                for (int source = 0; source < 5; ++source)
                {
                    const auto bounds = mixerPanel->getSourceBoundsForTest (source);
                    check (bounds.getWidth() > 0 && bounds.getHeight() > 0,
                           NI_TXT ("mixer source ") + juce::String (source + 1)
                               + NI_TXT (" has a real size"));
                }

                const auto filterBounds = mixerPanel->getFilterBoundsForTest();
                check (filterBounds.getWidth() > 0 && filterBounds.getHeight() > 0,
                       NI_TXT ("the mixer's dual-filter row has a real size"));
                check (writeImage (snapshot (*mixerPanel),
                                   outputDirectory.getChildFile ("53-hybrid-mixer.png")),
                       NI_TXT ("hybrid mixer page snapshot"));
            }

            hybridEditor.showModulationPageForTest();
            pump();
            check (writeImage (snapshot (hybridEditor),
                               outputDirectory.getChildFile ("55-hybrid-modulation.png")),
                   NI_TXT ("hybrid modulation page snapshot"));
        }

        // ---- the subtractive instrument --------------------------------------

        beginTest (NI_TXT ("a subtractive track is created and its editor works"));

        {
            auto& sectionLanes = component.getTrackArea();
            const auto before = (int) document.getProject().tracks.size();
            const auto newId = sectionLanes.addTrack (TrackKind::subtractive);
            pump();

            auto* track = document.findTrack (newId);

            check (newId != 0 && (int) document.getProject().tracks.size() == before + 1,
                   NI_TXT ("the new-track entry makes a subtractive track"));
            check (track != nullptr && track->kind == TrackKind::subtractive,
                   NI_TXT ("with the right kind"));
            check (track != nullptr && (int) track->clips.size() == 1,
                   NI_TXT ("and one empty clip to write into"));
            check (track != nullptr && track->subtractive.name == NI_TXT ("温暖铺底"),
                   NI_TXT ("it starts on the first factory sound (got '")
                       + (track != nullptr ? track->subtractive.name : juce::String()) + "')");

            // ---- the rename ---------------------------------------------------
            check (trackKindLabel (TrackKind::synth) == NI_TXT ("FM合成器"),
                   NI_TXT ("the FM kind is now called FM合成器 (got '")
                       + trackKindLabel (TrackKind::synth) + "')");
            check (trackKindLabel (TrackKind::subtractive) == NI_TXT ("减法合成器"),
                   NI_TXT ("and the new one is 减法合成器"));

            if (track != nullptr)
            {
                SynthWindow window (document, component.getAudioEngine(),
                                    component.getPresetStore(), false);
                window.getSubtractiveEditor().setTrack (newId);
                pump();

                auto& editor = window.getSubtractiveEditor();

                check (editor.getTrackId() == newId,
                       NI_TXT ("the window's subtractive editor follows the track"));

                auto laidOut = 0;

                for (int i = 0; i < 16; ++i)
                    if (! editor.getControlBoundsForTest (i).isEmpty())
                        ++laidOut;

                check (laidOut >= 14, NI_TXT ("the controls have real sizes (got ")
                                          + juce::String (laidOut) + NI_TXT (" of 16)"));

                // Every combo entry is readable text: a Chinese option written
                // without NI_TXT turns into mojibake (juce::String(const char*)
                // is ASCII only), which is exactly what happened to 正弦.
                check (editor.getChoiceTextForTest (0, 4) == NI_TXT ("正弦")
                           && editor.getChoiceTextForTest (3, 1) == NI_TXT ("正弦")
                           && editor.getChoiceTextForTest (0, 0) == NI_TXT ("锯齿")
                           && editor.getChoiceTextForTest (3, 3) == NI_TXT ("采样保持"),
                       NI_TXT ("every wave choice reads as text (got '")
                           + editor.getChoiceTextForTest (0, 4) + "', '"
                           + editor.getChoiceTextForTest (3, 1) + "')");

                // The display drew both pictures.
                const auto editorImage = snapshot (editor);
                check (writeImage (editorImage,
                                   outputDirectory.getChildFile ("27-subtractive.png")),
                       "subtractive editor snapshot");
                checkColourCount (editorImage, theme::referenceLine, 200,
                                  NI_TXT ("the waveform is drawn"), 40);
                checkColourCount (editorImage, theme::accent, 200,
                                  NI_TXT ("and the filter response"), 40);

                // Changing a combo has to reach the document: an unwired
                // onChange made every drop-down list read-only.
                editor.setParamForTest (9, 5.0);      // 第 5 个波形 = 正弦
                pump();

                check (document.findTrack (newId)->subtractive.oscs[0].wave == OscWave::sine,
                       NI_TXT ("picking a wave form writes it to the track"));

                editor.setParamForTest (10, 3.0);     // 滤波模式 = 高通
                pump();

                check (document.findTrack (newId)->subtractive.filterMode == FilterMode::highPass,
                       NI_TXT ("and so does the filter mode"));

                // ---- factory and user presets ---------------------------------
                const auto counts = editor.getPresetCountsForTest();
                check (counts.first == 12, NI_TXT ("twelve factory sounds (got ")
                                               + juce::String (counts.first) + ")");

                const auto wasDefault = track->subtractive;
                editor.selectPresetForTest (kBuiltInPresetIdBase + 2);   // 深潜低音
                pump();

                const auto* afterPreset = document.findTrack (newId);
                check (afterPreset != nullptr
                           && afterPreset->subtractive.name == NI_TXT ("深潜低音")
                           && ! (afterPreset->subtractive == wasDefault),
                       NI_TXT ("choosing a factory sound changes the parameters (got '")
                           + (afterPreset != nullptr ? afterPreset->subtractive.name
                                                     : juce::String()) + "')");

                check (editor.savePresetForTest (NI_TXT ("测试减法音色"), false),
                       NI_TXT ("the current sound saves as a preset"));
                pump();

                const auto& saved = component.getPresetStore().getSubtractivePresets();

                check (saved.size() == 1
                           && saved.front().filterCutoffHz
                                  == document.findTrack (newId)->subtractive.filterCutoffHz,
                       NI_TXT ("the library keeps the whole sound"));

                editor.selectPresetForTest (kBuiltInPresetIdBase);   // back to a factory one
                pump();
                editor.loadUserPresetForTest (0);
                pump();

                const auto* sectionProject = document.findTrack (newId);
                check (sectionProject != nullptr
                           && sectionProject->subtractive.name == NI_TXT ("测试减法音色")
                           && sectionProject->subtractive.filterCutoffHz
                                  == saved.front().filterCutoffHz,
                       NI_TXT ("and loading it brings the sound back"));

                check (editor.renamePresetForTest (NI_TXT ("测试减法音色 2")),
                       NI_TXT ("the preset renames"));
                pump();

                check (component.getPresetStore().getSubtractivePresets().size() == 1
                           && component.getPresetStore().getSubtractivePresets().front().name
                                  == NI_TXT ("测试减法音色 2"),
                       NI_TXT ("the rename lands in the library"));

                check (editor.deletePresetForTest(), NI_TXT ("and it can be deleted"));
                pump();

                check (component.getPresetStore().getSubtractivePresets().empty(),
                       NI_TXT ("the library is empty again"));
            }

            sectionLanes.deleteTrackNow (newId);
            pump();
        }

        // ---- the wavetable instrument ----------------------------------------

        beginTest (NI_TXT ("a wavetable track is created and its editor works"));

        {
            auto& sectionLanes = component.getTrackArea();
            const auto before = (int) document.getProject().tracks.size();
            const auto newId = sectionLanes.addTrack (TrackKind::wavetable);
            pump();

            auto* track = document.findTrack (newId);

            check (newId != 0 && (int) document.getProject().tracks.size() == before + 1,
                   NI_TXT ("the new-track entry makes a wavetable track"));
            check (track != nullptr && track->kind == TrackKind::wavetable,
                   NI_TXT ("with the right kind"));
            check (track != nullptr && (int) track->clips.size() == 1,
                   NI_TXT ("and one empty clip to write into"));
            check (track != nullptr && track->wavetable.name == NI_TXT ("温暖铺底"),
                   NI_TXT ("it starts on the first factory sound (got '")
                       + (track != nullptr ? track->wavetable.name : juce::String()) + "')");
            check (trackKindLabel (TrackKind::wavetable) == NI_TXT ("波表合成器"),
                   NI_TXT ("and the kind label names it"));

            if (track != nullptr)
            {
                // The table bank has to be laid out before anything can read it.
                check (wavetable::builtInTableCount() == 16,
                       NI_TXT ("sixteen factory tables (got ")
                           + juce::String (wavetable::builtInTableCount()) + ")");

                const auto& table = wavetable::getTable (0);

                check (table.frameCount == 8 && table.levelCount > 1
                           && (int) table.levels.size() == table.levelCount,
                       NI_TXT ("each table has eight frames and mipmap levels"));

                SynthWindow window (document, component.getAudioEngine(),
                                    component.getPresetStore(), false);
                // The real entry point: it also sizes the window for the page, and the
                // wavetable page is taller than the others (the waterfall view).
                window.showForTrack (newId, track->name, TrackKind::wavetable);
                pump();

                auto& editor = window.getWavetableEditor();

                check (editor.getTrackId() == newId,
                       NI_TXT ("the window's wavetable editor follows the track"));

                // The display reports its bounds while painting, so paint once
                // before asking where the curve and the strip landed.
                (void) snapshot (editor);

                auto laidOut = 0;

                for (int i = 0; i < 21; ++i)
                    if (! editor.getControlBoundsForTest (i).isEmpty())
                        ++laidOut;

                check (laidOut >= 20, NI_TXT ("the controls have real sizes (got ")
                                          + juce::String (laidOut) + NI_TXT (" of 21)"));

                check (editor.getChoiceTextForTest (0, 0) == NI_TXT ("正弦→锯齿")
                           && editor.getChoiceTextForTest (0, 15) == NI_TXT ("管风琴")
                           && editor.getChoiceTextForTest (2, 1) == NI_TXT ("带通")
                           && editor.getChoiceTextForTest (4, 3) == NI_TXT ("采样保持"),
                       NI_TXT ("the combos read as text (got '")
                           + editor.getChoiceTextForTest (0, 0) + "', '"
                           + editor.getChoiceTextForTest (0, 15) + "')");

                const auto editorImage = snapshot (editor);
                check (writeImage (editorImage,
                                   outputDirectory.getChildFile ("29-wavetable.png")),
                       "wavetable editor snapshot");
                checkColourCount (editorImage, theme::referenceLine, 200,
                                  NI_TXT ("the playing wave is drawn"), 40);
                checkColourCount (editorImage, theme::accent, 300,
                                  NI_TXT ("and the cursor is drawn"), 40);

                // ---- 3D 瀑布视图 + 实时光标 + 直接操作（第四十六批）----------
                check (window.getPageHeightForTest() == WavetableEditor::kDesignHeightWithWaterfall
                           && SynthWindow::pageHeightFor (TrackKind::synth) == SynthWindow::kDesignHeight,
                       NI_TXT ("波表页比别的页高 80 px（波表 ")
                           + juce::String (window.getPageHeightForTest()) + NI_TXT (" / 其它 ")
                           + juce::String (SynthWindow::kDesignHeight) + NI_TXT ("）"));
                check (editor.getWaterfallBoundsForTest().getHeight() == 168
                           && ! editor.getWaterfallPlotBoundsForTest().isEmpty(),
                       NI_TXT ("瀑布视图有真实尺寸（")
                           + editor.getWaterfallBoundsForTest().toString() + NI_TXT ("）"));
                check (! editor.getOscillatorButtonBoundsForTest (0).isEmpty()
                           && ! editor.getOscillatorButtonBoundsForTest (1).isEmpty(),
                       NI_TXT ("「振荡器 1 / 振荡器 2」两个按钮都排下来了"));
                check (editor.getDisplayedOscillatorForTest() == 0
                           && editor.getDisplayedTableNameForTest()
                                  == wavetable::tableName (track->wavetable.oscs[0].tableIndex),
                       NI_TXT ("默认画的是振荡器 1 那张表（「")
                           + editor.getDisplayedTableNameForTest() + NI_TXT ("」）"));

                // The cursor follows the position: at 0 it sits by the nearest frame,
                // at 1 by the furthest one.
                editor.setParamForTest (0, 0.0);
                pump();

                const auto lowCursor = editor.getCursorPointForTest();

                editor.setParamForTest (0, 1.0);
                pump();

                const auto highCursor = editor.getCursorPointForTest();

                check (highCursor.y < lowCursor.y - 20,
                       NI_TXT ("位置从 0 到 1，光标往上走了（")
                           + juce::String (lowCursor.y) + NI_TXT (" → ")
                           + juce::String (highCursor.y) + NI_TXT (" px）"));

                // ---- 拖动 = 连续改位置，一次拖动一条撤销 --------------------
                editor.setParamForTest (0, 0.0);
                pump();

                {
                    auto& view = editor.getDisplayForTest();
                    const auto plot = editor.getWaterfallPlotBoundsForTest();
                    const auto dragFrom = juce::Point<float> ((float) plot.getCentreX(),
                                                              (float) plot.getBottom() - 10.0f);
                    const auto dragTo = juce::Point<float> ((float) plot.getCentreX(),
                                                            (float) plot.getCentreY());

                    view.mouseDown (makeMouseEvent (view, dragFrom, dragFrom));
                    view.mouseDrag (makeMouseEvent (view, dragTo, dragFrom));
                    view.mouseUp (makeMouseEvent (view, dragTo, dragFrom));
                    pump();

                    const auto dragged = document.findTrack (newId)->wavetable.oscs[0].position;

                    check (dragged > 0.25f && dragged < 0.75f,
                           NI_TXT ("在瀑布上拖到中间，位置跟着走到中间（")
                               + juce::String (dragged, 3) + NI_TXT ("）"));
                    check (document.getUndoDescription() == NI_TXT ("改波表位置"),
                           NI_TXT ("而且是一次「改波表位置」撤销（「")
                               + document.getUndoDescription() + NI_TXT ("」）"));

                    document.undo();
                    pump();

                    check (document.findTrack (newId)->wavetable.oscs[0].position == 0.0f,
                           NI_TXT ("一次 Ctrl+Z 回到拖动之前"));

                    // ---- 点一帧 / 滚轮 / 双击 ---------------------------------
                    const auto row = view.getFrameRowBoundsForTest (5);
                    const auto centre = row.getCentre().toFloat();

                    view.mouseDown (makeMouseEvent (view, centre, centre));
                    view.mouseUp (makeMouseEvent (view, centre, centre));
                    pump();

                    check (std::abs (document.findTrack (newId)->wavetable.oscs[0].position - 5.0f / 7.0f)
                               < 0.02f,
                           NI_TXT ("点第 5 帧就跳到 5/7（实际 ")
                               + juce::String (document.findTrack (newId)->wavetable.oscs[0].position, 3)
                               + NI_TXT ("）"));

                    const auto beforeWheel = document.findTrack (newId)->wavetable.oscs[0].position;

                    view.mouseWheelMove (makeMouseEvent (view, centre, centre),
                                         juce::MouseWheelDetails { 0.0f, -0.5f, false, false, false });
                    pump();

                    check (std::abs ((document.findTrack (newId)->wavetable.oscs[0].position - beforeWheel)
                                     + 0.01f) < 0.002f,
                           NI_TXT ("滚轮一格 = 0.01（") + juce::String (beforeWheel, 3)
                               + NI_TXT (" → ")
                               + juce::String (document.findTrack (newId)->wavetable.oscs[0].position, 3)
                               + NI_TXT ("）"));

                    view.mouseDoubleClick (makeMouseEvent (view, centre, centre));
                    pump();

                    check (std::abs (document.findTrack (newId)->wavetable.oscs[0].position - 0.5f) < 0.001f,
                           NI_TXT ("双击回到中间 0.50（实际 ")
                               + juce::String (document.findTrack (newId)->wavetable.oscs[0].position, 3)
                               + NI_TXT ("）"));
                }

                // ---- 自定义表必须画出来（旧显示条只画内置表）----------------
                {
                    component.copyBuiltInWavetable (0);
                    pump();

                    check (document.findTrack (newId)->wavetable.oscs[0].customTableId != 0,
                           NI_TXT ("振荡器 1 换成了本工程的一张自定义表"));
                    check (editor.getDisplayedTableNameForTest() != wavetable::tableName (0)
                               && editor.getDisplayedTableNameForTest().contains (NI_TXT ("副本")),
                           NI_TXT ("画面画的也是那一张（「")
                               + editor.getDisplayedTableNameForTest() + NI_TXT ("」）"));

                    const auto customImage = snapshot (editor);

                    check (writeImage (customImage,
                                       outputDirectory.getChildFile ("48-wavetable-3d.png")),
                           NI_TXT ("3D 波表快照（自定义表 + 光标）"));

                    // Leave the project as this scene found it: the custom-table
                    // section below counts the project's own tables.
                    component.deleteCustomWavetable (
                        document.findTrack (newId)->wavetable.oscs[0].customTableId);
                    pump();

                    check (document.findTrack (newId)->wavetable.oscs[0].customTableId == 0,
                           NI_TXT ("删掉这张表之后振荡器 1 退回内置表（这一节不留痕迹）"));
                }

                // ---- 实时光标：试听声部一响，光标就跟着引擎报的位置走 --------
                {
                    auto& sectionAudio = component.getAudioEngine();

                    editor.setParamForTest (0, 0.0);
                    editor.setParamForTest (7, 0.05);   // a short amp release, so the
                                                        // audition really finishes here
                    pump();
                    sectionAudio.triggerPreviewWaveNote (document.findTrack (newId)->wavetable,
                                                         440.0, 100.0f);

                    for (int block = 0; block < 40; ++block)
                        sectionAudio.renderBlockForTest (512);

                    editor.tickWaterfallCursorForTest();
                    pump();

                    check (sectionAudio.getPreviewWavetableActiveVoices() > 0,
                           NI_TXT ("试听声部在响（")
                               + juce::String (sectionAudio.getPreviewWavetableActiveVoices())
                               + NI_TXT (" 个声部）"));
                    check (editor.isShowingLivePositionForTest()
                               && editor.getDisplayedPositionForTest() > 0.0,
                           NI_TXT ("视图显示的是引擎报的实时位置（")
                               + juce::String (editor.getDisplayedPositionForTest(), 3) + NI_TXT ("）"));

                    const auto cursorBefore = editor.getCursorPointForTest();

                    for (int block = 0; block < 40; ++block)
                        sectionAudio.renderBlockForTest (512);

                    editor.tickWaterfallCursorForTest();
                    pump();

                    check (editor.getCursorPointForTest() != cursorBefore,
                           NI_TXT ("位置包络扫过去，光标跟着动了（")
                               + juce::String (cursorBefore.y) + NI_TXT (" → ")
                               + juce::String (editor.getCursorPointForTest().y) + NI_TXT (" px）"));

                    sectionAudio.releasePreviewNote();

                    for (int block = 0; block < 240; ++block)
                        sectionAudio.renderBlockForTest (512);

                    editor.tickWaterfallCursorForTest();
                    pump();

                    check (sectionAudio.getPreviewWavetableActiveVoices() == 0,
                           NI_TXT ("松开之后预览声部归零，光标停在最后的位置"));
                    check (! editor.isShowingLivePositionForTest(),
                           NI_TXT ("视图回到「显示参数位置」而不是一直挂着实时值"));
                }

                // ---- factory and user presets ---------------------------------
                const auto counts = editor.getPresetCountsForTest();
                check (counts.first == 12, NI_TXT ("twelve factory sounds (got ")
                                               + juce::String (counts.first) + ")");

                const auto wasDefault = track->wavetable;
                editor.selectPresetForTest (kBuiltInPresetIdBase + 7);   // 扫频铺底
                pump();

                const auto* afterPreset = document.findTrack (newId);
                check (afterPreset != nullptr
                           && afterPreset->wavetable.name == NI_TXT ("扫频铺底")
                           && ! (afterPreset->wavetable == wasDefault),
                       NI_TXT ("choosing a factory sound changes the parameters (got '")
                           + (afterPreset != nullptr ? afterPreset->wavetable.name
                                                     : juce::String()) + "')");

                // A different table can be picked by hand as well.
                editor.setParamForTest (1, 6.0);      // 振荡器 1 → 第 6 张波表
                pump();

                check (document.findTrack (newId)->wavetable.oscs[0].tableIndex == 5,
                       NI_TXT ("the table combo writes the index (got ")
                           + juce::String (document.findTrack (newId)->wavetable.oscs[0].tableIndex)
                           + ")");

                check (editor.savePresetForTest (NI_TXT ("测试波表音色"), false),
                       NI_TXT ("the current sound saves as a preset"));
                pump();

                const auto& saved = component.getPresetStore().getWavetablePatches();

                check (saved.size() == 1
                           && saved.front().params.oscs[0].tableIndex
                                  == document.findTrack (newId)->wavetable.oscs[0].tableIndex
                           && saved.front().params.posEnvAmount
                                  == document.findTrack (newId)->wavetable.posEnvAmount,
                       NI_TXT ("the library keeps the whole sound"));

                editor.selectPresetForTest (kBuiltInPresetIdBase);
                pump();
                editor.loadUserPresetForTest (0);
                pump();

                const auto* sectionProject = document.findTrack (newId);
                check (sectionProject != nullptr
                           && sectionProject->wavetable.name == NI_TXT ("测试波表音色")
                           && sectionProject->wavetable.oscs[0].tableIndex == 5,
                       NI_TXT ("and loading it brings the sound back"));

                check (editor.renamePresetForTest (NI_TXT ("测试波表音色 2")),
                       NI_TXT ("the preset renames"));
                pump();

                check (component.getPresetStore().getWavetablePatches().size() == 1
                           && component.getPresetStore().getWavetablePatches().front().params.name
                                  == NI_TXT ("测试波表音色 2"),
                       NI_TXT ("the rename lands in the library"));

                check (editor.deletePresetForTest(), NI_TXT ("and it can be deleted"));
                pump();

                check (component.getPresetStore().getWavetablePatches().empty(),
                       NI_TXT ("the library is empty again"));
            }

            if (track != nullptr)
                sectionLanes.deleteTrackNow (newId);

            pump();
        }

        // ---- the drum machine -------------------------------------------------

        beginTest (NI_TXT ("自定义波表：导入 / 取样 / 拷内置 / 手绘 / 谐波（第四十二批）"));

        {
            auto& wtLanes = component.getTrackArea();
            const auto wtTrackId = wtLanes.addTrack (TrackKind::wavetable);
            pump();

            appState.selectTrack (wtTrackId);
            pump();

            component.openSynthWindow (wtTrackId);
            pump();

            auto* wtSynth = component.getSynthWindowForTest();

            check (wtSynth != nullptr && ! wtSynth->getWavetableEditor().getEditTableButtonBoundsForTest().isEmpty(),
                   NI_TXT ("波表音色窗口里有一个「波表编辑器…」按钮"));

            if (wtSynth != nullptr)
            {
                auto& wtEditor = wtSynth->getWavetableEditor();
                wtEditor.triggerEditTableForTest();
                pump();

                auto* wtWindow = component.getWavetableTableWindowForTest();

                check (wtWindow != nullptr && wtWindow->isVisible(),
                       NI_TXT ("点它打开了波表编辑器"));
                check (wtWindow != nullptr
                           && placement::displayAreaFor (&component).contains (wtWindow->getBounds()),
                       NI_TXT ("窗口开在屏幕里（") + (wtWindow != nullptr ? wtWindow->getBounds().toString()
                                                                        : juce::String())
                           + NI_TXT ("）"));

                if (wtWindow != nullptr)
                {
                    auto& wtPanel = wtWindow->getPanel();
                    auto* wtProject = &document.getProject();
                    const auto firstTableId = wtPanel.getTableId();
                    const auto* firstTable = wtProject->findCustomWavetable (firstTableId);
                    const auto* wtTrack = wtProject->findTrack (wtTrackId);

                    check (firstTableId != 0 && firstTable != nullptr,
                           NI_TXT ("内置表被复制成了一张自定义表（id ")
                               + juce::String (firstTableId) + NI_TXT ("）"));
                    check (firstTable != nullptr && firstTable->name.contains (NI_TXT ("副本")),
                           NI_TXT ("名字叫「") + (firstTable != nullptr ? firstTable->name : juce::String())
                               + NI_TXT ("」"));
                    check (wtTrack != nullptr && wtTrack->wavetable.oscs[0].customTableId == firstTableId,
                           NI_TXT ("振荡器 1 指到了这张新表"));
                    check (wtPanel.getFrameCountForTest() == 8,
                           NI_TXT ("拷出来的表是 8 帧（实际 ")
                               + juce::String (wtPanel.getFrameCountForTest()) + NI_TXT ("）"));
                    check (! wtPanel.getCanvasBoundsForTest().isEmpty()
                               && ! wtPanel.getFrameStripBoundsForTest().isEmpty()
                               && ! wtPanel.getHarmonicBoundsForTest().isEmpty(),
                           NI_TXT ("画布 / 帧列表 / 谐波条都排得下"));
                    check (wtPanel.getStatusTextForTest().contains (NI_TXT ("帧"))
                               && wtPanel.getStatusTextForTest().contains (NI_TXT ("KB")),
                           NI_TXT ("状态行写着大小（「") + wtPanel.getStatusTextForTest()
                               + NI_TXT ("」）"));

                    // ---- 手绘：一次手势一条撤销 ------------------------------
                    {
                        const auto before = wtPanel.getSampleForTest (0, 100);
                        wtPanel.selectFrameForTest (0);
                        wtPanel.drawLineForTest (-0.5f, 0.5f);
                        pump();

                        check (std::abs (wtPanel.getSampleForTest (0, 100)) > 0.05f,
                               NI_TXT ("画进去的值真的在表里（")
                                   + juce::String (wtPanel.getSampleForTest (0, 100), 3) + NI_TXT ("）"));
                        check (document.getUndoDescription() == NI_TXT ("编辑波表"),
                               NI_TXT ("一次手势只留一条撤销（「") + document.getUndoDescription()
                                   + NI_TXT ("」）"));

                        document.undo();
                        pump();

                        check (std::abs (wtPanel.getSampleForTest (0, 100) - before) < 1.0e-3f,
                               NI_TXT ("撤销逐字段还原（") + juce::String (wtPanel.getSampleForTest (0, 100), 3)
                                   + NI_TXT ("）"));
                    }

                    // ---- 谐波：只留第 3 次 ------------------------------------
                    {
                        // The strip was filled by analysing the frame, so every other
                        // bar has to be taken back to zero first.
                        for (int bar = 0; bar < (int) wtPanel.getHarmonicsForTest().size(); ++bar)
                            wtPanel.setHarmonicForTest (bar, bar == 2 ? 1.0f : 0.0f);
                        for (int bar = 0; bar < (int) wtPanel.getPhasesForTest().size(); ++bar)
                            wtPanel.setPhaseForTest (bar, 0.0f);

                        wtPanel.applyHarmonicsForTest();
                        pump();

                        auto sineLike = true;

                        for (const auto i : { 64, 256, 512, 1024 })
                        {
                            const auto expected = (float) std::sin (2.0 * juce::MathConstants<double>::pi
                                                                    * 3.0 * (double) i
                                                                    / (double) kCustomWavetableFrameSize);

                            if (std::abs (wtPanel.getSampleForTest (0, i) - expected) > 0.02f)
                                sineLike = false;
                        }

                        check (sineLike, NI_TXT ("按谐波生成之后这一帧就是 3 次正弦"));
                        check (document.getUndoDescription() == NI_TXT ("按谐波生成波表"),
                               NI_TXT ("谐波生成是一条撤销（「") + document.getUndoDescription()
                                   + NI_TXT ("」）"));
                    }

                    // ---- 帧数 1..8 -------------------------------------------
                    wtPanel.setFrameCountForTest (3);
                    pump();

                    const auto* threeFrame = wtProject->findCustomWavetable (firstTableId);

                    check (wtPanel.getFrameCountForTest() == 3
                               && threeFrame != nullptr && threeFrame->frameCount == 3
                               && (int) threeFrame->samples.size() == 3 * kCustomWavetableFrameSize,
                           NI_TXT ("帧数改成 3：数据也跟着变成 3 帧"));

                    wtPanel.setFrameCountForTest (8);
                    pump();

                    check (wtPanel.getFrameCountForTest() == 8,
                           NI_TXT ("再改回 8 帧"));

                    // ---- 拷内置 ----------------------------------------------
                    component.copyBuiltInWavetable (5);
                    pump();

                    const auto* wtTrackAfterCopy = wtProject->findTrack (wtTrackId);
                    const auto secondTableId = wtTrackAfterCopy != nullptr
                                                   ? wtTrackAfterCopy->wavetable.oscs[0].customTableId
                                                   : (juce::int64) 0;
                    const auto* secondTable = wtProject->findCustomWavetable (secondTableId);

                    check (secondTableId != firstTableId && secondTable != nullptr
                               && secondTable->name.contains (NI_TXT ("副本")),
                           NI_TXT ("「拷内置…」变成了第二张自定义表（「")
                               + (secondTable != nullptr ? secondTable->name : juce::String())
                               + NI_TXT ("」）"));
                    check (wtProject->customWavetables.size() == 2,
                           NI_TXT ("工程里现在有两张自定义表（实际 ")
                               + juce::String ((int) wtProject->customWavetables.size()) + NI_TXT (" 张）"));
                    check (wtWindow->getPanel().getTableId() == secondTableId,
                           NI_TXT ("窗口跟着切到新做的那张表"));

                    // ---- 改名 ------------------------------------------------
                    wtWindow->getPanel().setNameForTest (NI_TXT ("我的第一张表"));
                    pump();

                    const auto* renamed = wtProject->findCustomWavetable (secondTableId);

                    check (renamed != nullptr && renamed->name == NI_TXT ("我的第一张表"),
                           NI_TXT ("名字框改的是工程里那张表"));

                    // ---- 导入 WAV --------------------------------------------
                    {
                        const auto waveFile = outputDirectory.getChildFile ("selftest-sample.wav");
                        const auto importedId = component.importWavetableFromFile (waveFile);
                        pump();

                        const auto* imported = wtProject->findCustomWavetable (importedId);

                        check (importedId != 0 && imported != nullptr,
                               NI_TXT ("导入一个 WAV 得到了一张表（id ") + juce::String (importedId)
                                   + NI_TXT ("）"));
                        check (imported != nullptr && imported->name == waveFile.getFileNameWithoutExtension(),
                               NI_TXT ("名字取文件名（「")
                                   + (imported != nullptr ? imported->name : juce::String()) + NI_TXT ("」）"));
                        check (imported != nullptr && imported->frameCount == 23,
                               NI_TXT ("一秒 48 kHz 的素材按 2048 点一帧切，切成 23 帧（实际 ")
                                   + juce::String (imported != nullptr ? imported->frameCount : -1)
                                   + NI_TXT (" 帧）"));
                        check (imported != nullptr
                                   && wtTrackAfterCopy->wavetable.oscs[0].customTableId == importedId,
                               NI_TXT ("导入之后振荡器 1 就指着它"));
                    }

                    // ---- 从片段取样 ------------------------------------------
                    {
                        const auto waveFile = outputDirectory.getChildFile ("selftest-sample.wav");
                        const auto audioTrackId = wtLanes.addTrack (TrackKind::audio);
                        pump();

                        const auto audioClipId = wtLanes.importAudioFile (audioTrackId, waveFile);
                        pump();

                        appState.selectTrack (audioTrackId);
                        appState.selectClip (audioClipId);
                        pump();

                        const auto sampledId = component.sampleWavetableFromOpenClip();
                        pump();

                        check (sampledId != 0 && wtProject->findCustomWavetable (sampledId) != nullptr,
                               NI_TXT ("从打开的音频片段取样，得到了一张表"));

                        // A synth clip has no audio: it must say so and make nothing.
                        const auto waveTrack = wtProject->findTrack (wtTrackId);
                        const auto synthClipId = waveTrack != nullptr && ! waveTrack->clips.empty()
                                                     ? waveTrack->clips.front().id
                                                     : (juce::int64) 0;
                        const auto beforeCount = (int) wtProject->customWavetables.size();

                        appState.selectTrack (wtTrackId);
                        appState.selectClip (synthClipId);
                        pump();

                        check (component.sampleWavetableFromOpenClip() == 0
                                   && (int) wtProject->customWavetables.size() == beforeCount,
                               NI_TXT ("非音频片段：不建表"));
                        check (component.getWavetableStatusNoticeForTest().contains (NI_TXT ("不是音频片段")),
                               NI_TXT ("而且状态行写明原因（「")
                                   + component.getWavetableStatusNoticeForTest() + NI_TXT ("」）"));
                    }

                    // ---- 下拉里能选到自定义表 ---------------------------------
                    {
                        const auto customCount = wtEditor.getCustomTableChoiceCountForTest();

                        check (customCount == (int) wtProject->customWavetables.size() && customCount >= 4,
                               NI_TXT ("音色窗口的波表下拉里列出了这些自定义表（")
                                   + juce::String (customCount) + NI_TXT (" 张）"));

                        const auto target = (juce::int64) 1;   // the first table made

                        check (wtEditor.selectTableChoiceForTest (
                                   0, wavetable::builtInTableCount()
                                          + (int) wtProject->customWavetables.size() - 1),
                               NI_TXT ("能选中最后一张自定义表"));
                        pump();

                        const auto* oscTrack = wtProject->findTrack (wtTrackId);

                        check (oscTrack != nullptr && oscTrack->wavetable.oscs[0].customTableId != 0
                                   && wtProject->findCustomWavetable (
                                          oscTrack->wavetable.oscs[0].customTableId) != nullptr,
                               NI_TXT ("选中之后振荡器就用了它（id ")
                                   + juce::String (oscTrack != nullptr
                                                       ? oscTrack->wavetable.oscs[0].customTableId
                                                       : (juce::int64) 0)
                                   + NI_TXT ("）"));
                        juce::ignoreUnused (target);
                    }

                    // ---- 工程往返 --------------------------------------------
                    {
                        const auto tableCount = (int) wtProject->customWavetables.size();
                        Project waveReloaded;
                        juce::String waveError;
                        const auto storedJson = projectToJson (*wtProject);

                        check (projectFromJson (storedJson, waveReloaded, waveError)
                                   && (int) waveReloaded.customWavetables.size() == tableCount,
                               NI_TXT ("工程存盘再读回：") + juce::String (tableCount)
                                   + NI_TXT (" 张表都在（") + waveError + NI_TXT ("）"));
                        const auto* reloadedTrack = waveReloaded.findTrack (wtTrackId);

                        check (reloadedTrack != nullptr
                                   && reloadedTrack->wavetable.oscs[0].customTableId != 0
                                   && waveReloaded.findCustomWavetable (
                                          reloadedTrack->wavetable.oscs[0].customTableId) != nullptr,
                               NI_TXT ("振荡器的引用也跟着存下来了（id ")
                                   + juce::String (reloadedTrack != nullptr
                                                       ? reloadedTrack->wavetable.oscs[0].customTableId
                                                       : (juce::int64) 0)
                                   + NI_TXT ("）"));
                    }

                    // ---- 快照 -------------------------------------------------
                    const auto wtImage = snapshot (wtPanel);

                    check (writeImage (wtImage, outputDirectory.getChildFile ("47-wavetable-editor.png")),
                           NI_TXT ("波表编辑器快照"));

                    // ---- 窄窗口不会把右侧按钮裁掉 -----------------------------
                    {
                        const auto wide = wtWindow->getBounds();
                        wtWindow->setBounds (wide.withWidth (660).withHeight (380));
                        pump();

                        check (wtWindow->getBounds().getWidth() < 780
                                   && ! wtPanel.getToolbarBoundsForTest (3).isEmpty()
                                   && ! wtPanel.getToolbarBoundsForTest (4).isEmpty(),
                               NI_TXT ("窗口缩到 660 px 时「试听 / 删除这张表」仍然有尺寸（")
                                   + wtPanel.getToolbarBoundsForTest (4).toString() + NI_TXT ("）"));
                        check (wtPanel.getCanvasBoundsForTest().getWidth() >= 520,
                               NI_TXT ("画布仍然保留设计尺寸（面板 ")
                                   + juce::String (wtPanel.getWidth()) + NI_TXT (" px 宽，窗口滚动）"));

                        wtWindow->setBounds (wide);
                        pump();
                    }

                    // ---- 删除：从编辑器窗口的按钮删 ---------------------------
                    {
                        const auto* beforeButtons = wtProject->findTrack (wtTrackId);
                        const auto buttonTableId = beforeButtons != nullptr
                                                       ? beforeButtons->wavetable.oscs[0].customTableId
                                                       : (juce::int64) 0;
                        const auto tablesBefore = (int) wtProject->customWavetables.size();

                        wtPanel.setTableId (buttonTableId);
                        pump();

                        check (wtPanel.isDeleteEnabledForTest(),
                               NI_TXT ("打开一张自定义表时「删除这张表」是可点的"));

                        wtPanel.triggerDeleteForTest();
                        pump();

                        check ((int) document.getProject().customWavetables.size() == tablesBefore - 1
                                   && document.getProject().findCustomWavetable (buttonTableId) == nullptr,
                               NI_TXT ("点它真的把表从工程里删掉了（")
                                   + juce::String (tablesBefore) + NI_TXT(" → ")
                                   + juce::String ((int) document.getProject().customWavetables.size())
                                   + NI_TXT (" 张）"));

                        const auto* afterDelete = document.getProject().findTrack (wtTrackId);

                        check (afterDelete != nullptr && afterDelete->wavetable.oscs[0].customTableId == 0,
                               NI_TXT ("还指着它的振荡器退回内置表"));
                        check (! wtWindow->isVisible(), NI_TXT ("删完窗口收起来"));
                    }

                    // ---- 删除：也能从音色窗口那一行删 -------------------------
                    {
                        appState.selectTrack (wtTrackId);
                        pump();

                        // The button above already took its table away: make a new one
                        // so there is something for this path to delete.
                        component.copyBuiltInWavetable (2);
                        pump();

                        const auto* pageTrack = document.getProject().findTrack (wtTrackId);
                        const auto pageTableId = pageTrack != nullptr
                                                     ? pageTrack->wavetable.oscs[0].customTableId
                                                     : (juce::int64) 0;

                        component.openSynthWindow (wtTrackId);
                        pump();

                        check (pageTableId != 0, NI_TXT ("此时振荡器用的是一张自定义表"));
                        check (! wtEditor.getDeleteTableButtonBoundsForTest().isEmpty()
                                   && wtEditor.isDeleteTableEnabledForTest(),
                               NI_TXT ("音色窗口那一行也有「删除波表」，而且亮着"));

                        wtEditor.triggerDeleteTableForTest();
                        pump();

                        const auto* afterPageDelete = document.getProject().findTrack (wtTrackId);

                        check (document.getProject().findCustomWavetable (pageTableId) == nullptr
                                   && afterPageDelete != nullptr
                                   && afterPageDelete->wavetable.oscs[0].customTableId == 0,
                               NI_TXT ("点它同样删掉了那张表，振荡器退回内置"));
                        check (! wtEditor.isDeleteTableEnabledForTest(),
                               NI_TXT ("用内置表时「删除波表」是灰的"));
                    }

                    // ---- 删除：引用的振荡器退回内置 ---------------------------
                    {
                        // A fresh table, so there is something to delete through the
                        // plain call as well (the two buttons above already took
                        // their turns).
                        component.copyBuiltInWavetable (7);
                        pump();

                        const auto* beforeDelete = document.getProject().findTrack (wtTrackId);
                        const auto usedTableId = beforeDelete != nullptr
                                                     ? beforeDelete->wavetable.oscs[0].customTableId
                                                     : (juce::int64) 0;

                        check (usedTableId != 0, NI_TXT ("又造了一张表来删"));

                        component.deleteCustomWavetable (usedTableId);
                        pump();

                        const auto* deletedTrack = document.getProject().findTrack (wtTrackId);

                        check (document.getProject().findCustomWavetable (usedTableId) == nullptr,
                               NI_TXT ("删掉之后工程里没有这张表了"));
                        check (deletedTrack != nullptr && deletedTrack->wavetable.oscs[0].customTableId == 0,
                               NI_TXT ("还指着它的振荡器退回内置表"));
                        check (component.getWavetableStatusNoticeForTest().contains (NI_TXT ("退回内置")),
                               NI_TXT ("状态行写明退回了内置（「")
                                   + component.getWavetableStatusNoticeForTest() + NI_TXT ("」）"));
                    }
                }
            }

            wtLanes.deleteTrackNow (wtTrackId);
            pump();
        }

        beginTest (NI_TXT ("a drum track is created and its pad grid works"));

        {
            auto& sectionLanes = component.getTrackArea();
            const auto before = (int) document.getProject().tracks.size();
            const auto newId = sectionLanes.addTrack (TrackKind::drums);
            pump();

            auto* track = document.findTrack (newId);

            check (newId != 0 && (int) document.getProject().tracks.size() == before + 1,
                   NI_TXT ("the new-track entry makes a drum track"));
            check (track != nullptr && track->kind == TrackKind::drums,
                   NI_TXT ("with the right kind"));
            check (track != nullptr && (int) track->clips.size() == 1,
                   NI_TXT ("and one empty clip to write into"));
            check (trackKindLabel (TrackKind::drums) == NI_TXT ("鼓机"),
                   NI_TXT ("and the kind label names it"));

            if (auto* header = sectionLanes.getLaneHeaderForTest (before))
                check (header->getTypeTextForTest() == NI_TXT ("鼓机"),
                       NI_TXT ("the lane header's type row says 鼓机 (got '")
                           + header->getTypeTextForTest() + "')");

            if (track == nullptr)
            {
                pump();
            }
            else
            {
                const auto clipId = track->clips.front().id;

                // Frame the clip from the left so the row helper's x lands on screen.
                document.applyEdit (NI_TXT ("鼓机自检取景"), [] (Project& project)
                {
                    project.view.autoFitView = false;
                    project.view.horizontalOffsetTicks = 0.0;
                    project.view.pixelsPerQuarter = 40.0;
                });
                pump();

                appState.selectTrack (newId);
                appState.selectClip (clipId);
                appState.clearNotes();
                pump();

                auto& sectionGrid = component.getEditorPanel().getGrid();

                check (sectionGrid.getDrumRowCountForTest() == kDrumPads,
                       NI_TXT ("the drum grid shows twelve rows (got ")
                           + juce::String (sectionGrid.getDrumRowCountForTest()) + ")");

                auto rowsLaidOut = 0;

                for (int pad = 0; pad < kDrumPads; ++pad)
                {
                    const auto bounds = sectionGrid.getDrumRowBoundsForTest (pad);

                    if (bounds.getWidth() > 0 && bounds.getHeight() > 0)
                        ++rowsLaidOut;
                }

                check (rowsLaidOut == kDrumPads,
                       NI_TXT ("every pad row has a real size (got ")
                           + juce::String (rowsLaidOut) + NI_TXT (" of ")
                           + juce::String (kDrumPads) + ")");

                // ---- writing a note picks the row's drum ----------------------
                const auto rowPad = 5;
                const auto writeAt = sectionGrid.pointForPadForTest (rowPad, 0);

                sectionGrid.mouseDown (makeMouseEvent (sectionGrid, writeAt, writeAt));
                sectionGrid.mouseUp (makeMouseEvent (sectionGrid, writeAt, writeAt));
                pump();

                const auto noteId = appState.primaryNoteId;
                check (noteId != 0, NI_TXT ("clicking a pad row writes a note"));

                auto readDrumNote = [&document, newId] (juce::int64 id)
                {
                    if (const auto* note = document.findNote (newId, id))
                        return *note;

                    return Note();
                };

                check (readDrumNote (noteId).pad == rowPad,
                       NI_TXT ("the note hits the drum of that row (pad ")
                           + juce::String (readDrumNote (noteId).pad) + ")");

                const auto startTick = readDrumNote (noteId).startTick;

                // ---- dragging down picks another drum, not another pitch ------
                const auto grab = sectionGrid.pointForPadForTest (rowPad, 480);
                const auto drop = sectionGrid.pointForPadForTest (8, 480);

                sectionGrid.mouseDown (makeMouseEvent (sectionGrid, grab, grab));
                sectionGrid.mouseDrag (makeMouseEvent (sectionGrid, drop, grab));
                sectionGrid.mouseUp (makeMouseEvent (sectionGrid, drop, grab));
                pump();

                check (readDrumNote (noteId).pad == 8,
                       NI_TXT ("dragging down changes the drum to pad 8 (got ")
                           + juce::String (readDrumNote (noteId).pad) + ")");
                check (readDrumNote (noteId).startTick == startTick,
                       NI_TXT ("and leaves the start tick alone"));

                // ---- Alt + vertical drag is the velocity ----------------------
                const auto velocityBefore = readDrumNote (noteId).velocity;
                const juce::ModifierKeys alt (juce::ModifierKeys::altModifier);
                const auto vGrab = sectionGrid.pointForPadForTest (8, 480);
                const auto vDrop = vGrab.translated (0.0f, -30.0f);

                sectionGrid.mouseDown (makeMouseEvent (sectionGrid, vGrab, vGrab, alt));
                sectionGrid.mouseDrag (makeMouseEvent (sectionGrid, vDrop, vGrab, alt));
                sectionGrid.mouseUp (makeMouseEvent (sectionGrid, vDrop, vGrab, alt));
                pump();

                check (readDrumNote (noteId).velocity > velocityBefore,
                       NI_TXT ("Alt + dragging up raises the velocity (")
                           + juce::String (velocityBefore) + NI_TXT (" -> ")
                           + juce::String (readDrumNote (noteId).velocity) + ")");
                check (readDrumNote (noteId).pad == 8 && readDrumNote (noteId).startTick == startTick,
                       NI_TXT ("and neither the drum nor the tick moved"));

                // ---- a horizontal drag writes the hit, with that length -------
                {
                    const auto notesBeforeDrag =
                        (int) document.findTrack (newId)->clips.front().notes.size();
                    const auto dragRow = (int) DrumPad::clap;
                    const auto dragFrom = sectionGrid.pointForPadForTest (dragRow, 0);

                    // A few pixels of vertical jitter must not turn this into a
                    // rubber band: horizontally it is a write.
                    const auto dragTo = sectionGrid.pointForPadForTest (dragRow, 960)
                                            .translated (0.0f, 4.0f);

                    sectionGrid.mouseDown (makeMouseEvent (sectionGrid, dragFrom, dragFrom));
                    sectionGrid.mouseDrag (makeMouseEvent (sectionGrid, dragTo, dragFrom));
                    sectionGrid.mouseUp (makeMouseEvent (sectionGrid, dragTo, dragFrom));
                    pump();

                    const auto draggedId = appState.primaryNoteId;

                    check ((int) document.findTrack (newId)->clips.front().notes.size()
                               == notesBeforeDrag + 1,
                           NI_TXT ("a horizontal drag writes exactly one drum note (got ")
                               + juce::String ((int) document.findTrack (newId)
                                                    ->clips.front().notes.size())
                               + NI_TXT (" from ") + juce::String (notesBeforeDrag) + ")");
                    check (draggedId != noteId && draggedId != 0
                               && readDrumNote (draggedId).pad == dragRow,
                           NI_TXT ("and it hits the row the drag started in (pad ")
                               + juce::String (readDrumNote (draggedId).pad) + ")");
                    check (readDrumNote (draggedId).startTick == 0
                               && readDrumNote (draggedId).durationTicks == kTicksPerQuarter,
                           NI_TXT ("with the length the drag covered (got ")
                               + juce::String (readDrumNote (draggedId).startTick) + " + "
                               + juce::String (readDrumNote (draggedId).durationTicks) + ")");
                }

                // ---- the fill brightness follows the velocity -----------------
                juce::int64 loudId = 0, quietId = 0;

                document.applyEdit (NI_TXT ("鼓机自检力度"), [newId, &loudId, &quietId] (Project& project)
                {
                    if (auto* t = project.findTrack (newId))
                        if (! t->clips.empty())
                        {
                            Note loud;
                            loud.id = project.nextNoteId++;
                            loud.startTick = 1920;
                            loud.durationTicks = 960;
                            loud.pad = (int) DrumPad::kick;
                            loud.frequencyHz = drumPadBaseHz (loud.pad);
                            loud.velocity = 127;

                            Note quiet = loud;
                            quiet.id = project.nextNoteId++;
                            quiet.startTick = 3840;
                            quiet.pad = (int) DrumPad::snare;
                            quiet.frequencyHz = drumPadBaseHz (quiet.pad);
                            quiet.velocity = 20;

                            t->clips.front().notes.push_back (loud);
                            t->clips.front().notes.push_back (quiet);
                            loudId = loud.id;
                            quietId = quiet.id;
                        }
                });
                pump();

                appState.clearNotes();
                pump();

                const auto drumImage = snapshot (sectionGrid);
                check (writeImage (drumImage, outputDirectory.getChildFile ("30-drums-grid.png")),
                       "drum grid snapshot");

                auto meanBrightness = [&drumImage] (juce::Rectangle<float> rect)
                {
                    const auto box = rect.reduced (3.0f).getSmallestIntegerContainer()
                                         .getIntersection (drumImage.getBounds());

                    if (box.isEmpty())
                        return 0.0;

                    auto sum = 0.0;

                    for (int y = box.getY(); y < box.getBottom(); ++y)
                        for (int x = box.getX(); x < box.getRight(); ++x)
                        {
                            const auto pixel = drumImage.getPixelAt (x, y);
                            sum += ((double) pixel.getRed() + pixel.getGreen()
                                        + pixel.getBlue())
                                   / 3.0;
                        }

                    return sum / (double) (box.getWidth() * box.getHeight());
                };

                const auto loudMean  = meanBrightness (sectionGrid.getNoteRectForTest (loudId));
                const auto quietMean = meanBrightness (sectionGrid.getNoteRectForTest (quietId));

                check (loudMean > quietMean + 12.0,
                       NI_TXT ("a full velocity note is brighter than a soft one (")
                           + juce::String (loudMean, 1) + NI_TXT (" vs ")
                           + juce::String (quietMean, 1) + ")");

                // ---- the note bar swaps the frequency for the drum name -------
                appState.selectNote (loudId, false);
                pump();

                auto& noteBar = component.getEditorPanel().getNoteEditBar();

                check (noteBar.getFrequencyTextForTest() == drumPadName ((int) DrumPad::kick),
                       NI_TXT ("the frequency field shows the drum name (got '")
                           + noteBar.getFrequencyTextForTest() + "')");
                check (noteBar.isFrequencyReadOnlyForTest(),
                       NI_TXT ("and the field is read only on a drum track"));
                check (! noteBar.isRatioRowVisibleForTest(),
                       NI_TXT ("the ratio row is hidden on a drum track"));

                // ---- a vertical drag box-selects and writes nothing ------------
                const auto notesBeforeBand =
                    (int) document.findTrack (newId)->clips.front().notes.size();
                const auto bandStart = sectionGrid.pointForPadForTest (11, 1920);
                const auto bandEnd   = sectionGrid.pointForPadForTest (0, 4800);

                // Runs down the rows with only a little sideways movement: the
                // vertical axis wins, so this is a rubber band, not a new hit.
                sectionGrid.mouseDown (makeMouseEvent (sectionGrid, bandStart, bandStart));
                sectionGrid.mouseDrag (makeMouseEvent (sectionGrid, bandEnd, bandStart));
                sectionGrid.mouseUp (makeMouseEvent (sectionGrid, bandEnd, bandStart));
                pump();

                check ((int) document.findTrack (newId)->clips.front().notes.size()
                           == notesBeforeBand,
                       NI_TXT ("a vertical drag wrote no drum note (")
                           + juce::String (notesBeforeBand) + NI_TXT (" -> ")
                           + juce::String ((int) document.findTrack (newId)
                                                ->clips.front().notes.size()) + ")");
                check (appState.selectedNoteIds.size() == 2,
                       NI_TXT ("the band selected both drum hits (got ")
                           + juce::String ((int) appState.selectedNoteIds.size()) + ")");

                const juce::ModifierKeys rightButton (juce::ModifierKeys::rightButtonModifier);
                const auto deleteAt = sectionGrid.pointForPadForTest ((int) DrumPad::snare, 3900);

                // The band left two hits selected, so a right click on one of them
                // takes the whole selection - one edit, not one note.
                sectionGrid.mouseDown (makeMouseEvent (sectionGrid, deleteAt, deleteAt, rightButton));
                sectionGrid.mouseUp (makeMouseEvent (sectionGrid, deleteAt, deleteAt, rightButton));
                pump();

                check (document.findNote (newId, quietId) == nullptr
                           && document.findNote (newId, loudId) == nullptr,
                       NI_TXT ("右键点在选中的鼓音符上，选中的一起删（现在剩 ")
                           + juce::String ((int) document.findTrack (newId)->clips.front().notes.size())
                           + NI_TXT (" 个）"));
                check (document.getUndoDescription() == NI_TXT ("删除音符"),
                       NI_TXT ("整组删除只留一条撤销记录（「") + document.getUndoDescription()
                           + NI_TXT ("」）"));

                document.undo();
                pump();

                check (document.findNote (newId, quietId) != nullptr
                           && document.findNote (newId, loudId) != nullptr,
                       NI_TXT ("一次 Ctrl+Z 把两个鼓音符都放回来"));

                appState.selectNote (loudId, false);
                pump();

                const auto notesBeforePaste = (int) document.findTrack (newId)->clips.front().notes.size();
                component.copySelection();
                component.pasteClipboard();
                pump();

                check ((int) document.findTrack (newId)->clips.front().notes.size()
                           == notesBeforePaste + 1,
                       NI_TXT ("copy / paste still writes a drum note"));

                // ---- the drum machine window ----------------------------------
                SynthWindow window (document, component.getAudioEngine(),
                                    component.getPresetStore(), false);
                window.getDrumEditor().setTrack (newId);
                pump();

                auto& editor = window.getDrumEditor();

                check (editor.getPadCountForTest() == kDrumPads,
                       NI_TXT ("the drum window has twelve pads"));

                auto controlsLaidOut = 0;

                for (int i = 0; i < 12 + 14; ++i)
                    if (! editor.getControlBoundsForTest (i).isEmpty())
                        ++controlsLaidOut;

                check (controlsLaidOut >= 12 + 8,
                       NI_TXT ("its pads, knobs and keyboard have real sizes (got ")
                           + juce::String (controlsLaidOut) + ")");

                editor.selectPadForTest (4);
                pump();
                check (editor.getSelectedPadForTest() == 4,
                       NI_TXT ("selecting a pad switches which drum is edited"));

                editor.setPadParamForTest (4, 2, 2.5);     // 2 = decay
                pump();
                check (std::abs (document.findTrack (newId)->drums.pads[4].decay - 2.5f) < 1.0e-4f,
                       NI_TXT ("editing the decay writes back to the document"));

                const auto editorImage = snapshot (editor);
                check (writeImage (editorImage, outputDirectory.getChildFile ("30-drums.png")),
                       "drum machine window snapshot");

                check (editor.savePresetForTest (NI_TXT ("测试鼓组"), false),
                       NI_TXT ("the kit saves as a preset"));
                pump();

                check (component.getPresetStore().getDrumPresets().size() == 1,
                       NI_TXT ("the library keeps the kit"));

                check (editor.loadUserPresetForTest (0), NI_TXT ("and loads it back"));
                pump();

                check (editor.renamePresetForTest (NI_TXT ("测试鼓组 2")),
                       NI_TXT ("the preset renames"));
                pump();

                check (component.getPresetStore().getDrumPresets().size() == 1
                           && component.getPresetStore().getDrumPresets().front().name
                                  == NI_TXT ("测试鼓组 2"),
                       NI_TXT ("the rename lands in the library"));

                check (editor.deletePresetForTest(), NI_TXT ("and it can be deleted"));
                pump();

                check (component.getPresetStore().getDrumPresets().empty(),
                       NI_TXT ("the library is empty again"));

                // ---- a drum track exports -------------------------------------
                const auto drumFile = outputDirectory.getChildFile ("selftest-drums.wav");
                drumFile.deleteFile();

                ExportSettings drumExport;
                drumExport.format = ExportSettings::Format::wav;
                drumExport.bitsPerSample = 24;

                juce::String drumError;
                check (Exporter::renderToFile (document.getProject(), drumFile, drumExport,
                                               {}, {}, {}, drumError),
                       NI_TXT ("a drum track exports: ") + drumError);

                juce::WavAudioFormat drumFormat;
                std::unique_ptr<juce::AudioFormatReader> drumReader (
                    drumFormat.createReaderFor (new juce::FileInputStream (drumFile), true));

                check (drumReader != nullptr, NI_TXT ("the drum export is readable"));

                if (drumReader != nullptr)
                {
                    juce::AudioBuffer<float> drumBuffer ((int) drumReader->numChannels,
                                                         (int) juce::jmin ((juce::int64) 20000,
                                                                           drumReader->lengthInSamples));
                    drumReader->read (&drumBuffer, 0, drumBuffer.getNumSamples(), 0, true, true);

                    check (drumBuffer.getRMSLevel (0, 0, drumBuffer.getNumSamples()) > 0.001f,
                           NI_TXT ("and it is not silent"));
                }

                drumFile.deleteFile();
            }

            if (track != nullptr)
                sectionLanes.deleteTrackNow (newId);

            pump();
        }

        beginTest ("a tonal track still shows the frequency bar after the drums");

        {
            // A fresh synth track: earlier tests may have emptied the original
            // lane, and this check only needs one pitch note to select.
            auto& sectionLanes = component.getTrackArea();
            const auto tonalId = sectionLanes.addTrack (TrackKind::synth);
            pump();

            juce::int64 tonalNoteId = 0;
            juce::int64 tonalClipId = 0;

            document.applyEdit (NI_TXT ("自检音高音符"), [tonalId, &tonalNoteId, &tonalClipId] (Project& project)
            {
                if (auto* t = project.findTrack (tonalId))
                    if (! t->clips.empty())
                    {
                        Note note;
                        note.id = project.nextNoteId++;
                        note.startTick = 0;
                        note.durationTicks = 480;
                        note.frequencyHz = 660.0;
                        tonalNoteId = note.id;
                        t->clips.front().notes.push_back (note);
                        tonalClipId = t->clips.front().id;
                    }
            });
            pump();

            appState.selectTrack (tonalId);
            appState.selectClip (tonalClipId);

            if (tonalNoteId != 0)
                appState.selectNote (tonalNoteId, false);

            pump();

            auto& noteBar = component.getEditorPanel().getNoteEditBar();

            check (noteBar.isRatioRowVisibleForTest(),
                   NI_TXT ("switching back to a pitch track brings the ratio row back"));
            check (! noteBar.isFrequencyReadOnlyForTest(),
                   NI_TXT ("and the frequency field is editable again"));

            sectionLanes.deleteTrackNow (tonalId);
            pump();
        }

        beginTest ("deleting every track leaves an empty project");

        {
            auto& sectionLanes = component.getTrackArea();
            const auto before = (int) document.getProject().tracks.size();

            check (before >= 1, "there is a track to delete");

            for (int i = 0; i < before; ++i)
            {
                auto* header = sectionLanes.getLaneHeaderForTest (0);

                if (header == nullptr)
                    break;

                sectionLanes.deleteTrackNow (header->getTrackId());
                pump();
            }

            check (document.getProject().tracks.empty(),
                   "the project is empty again (got "
                       + juce::String ((int) document.getProject().tracks.size()) + " tracks)");
            check (appState.selectedTrackId == 0, "no track stays selected");

            for (int i = 0; i < before; ++i)
                document.undo();

            pump();

            check ((int) document.getProject().tracks.size() == before,
                   "undo brings the deleted tracks back (got "
                       + juce::String ((int) document.getProject().tracks.size()) + ")");
        }

        // ---- 粒子合成器（第五十二批）-------------------------------------------

        beginTest (NI_TXT ("粒子合成器：导入 / 参数 / 实时颗粒云（第五十二批）"));

        {
            auto& sectionLanes = component.getTrackArea();
            const auto before = (int) document.getProject().tracks.size();
            const auto newId = sectionLanes.addTrack (TrackKind::granular);
            pump();

            auto* track = document.findTrack (newId);

            check (newId != 0 && (int) document.getProject().tracks.size() == before + 1,
                   NI_TXT ("「＋ 新建轨道」能建出一条粒子合成器轨"));
            check (track != nullptr && track->kind == TrackKind::granular,
                   NI_TXT ("类型就是粒子合成器"));
            check (track != nullptr && (int) track->clips.size() == 1,
                   NI_TXT ("并且带一个空片段"));
            check (trackKindLabel (TrackKind::granular) == NI_TXT ("粒子合成器"),
                   NI_TXT ("类型名字写着「粒子合成器」"));

            if (auto* header = sectionLanes.getLaneHeaderForTest (before))
                check (header->getTypeTextForTest() == NI_TXT ("粒子合成器"),
                       NI_TXT ("行头的类型那一行也写着「粒子合成器」（实际「")
                           + header->getTypeTextForTest() + NI_TXT ("」）"));

            document.applyEdit (NI_TXT ("粒子自检取景"), [newId] (Project& project)
            {
                if (auto* t = project.findTrack (newId))
                {
                    t->name = NI_TXT ("粒子合成器 1");
                    t->volumeDb = -6.0f;
                }
            });
            pump();

            appState.selectTrack (newId);
            appState.setEditorOpen (true);
            pump();

            track = document.findTrack (newId);

            SynthWindow window (document, component.getAudioEngine(),
                                component.getPresetStore(), false);
            // The real entry point: it also sizes the window for the page, and the
            // granular page is taller than the rest (the cloud view).
            window.showForTrack (newId, track->name, TrackKind::granular);
            pump();

            auto& editor = window.getGranularEditor();
            auto& sectionAudio = component.getAudioEngine();

            check (editor.getTrackId() == newId,
                   NI_TXT ("音色窗口的粒子页跟着这条轨"));

            (void) snapshot (editor);   // lay the page out before asking for bounds

            check (window.getPageHeightForTest() == GranularEditor::kDesignHeightWithCloud
                       && SynthWindow::pageHeightFor (TrackKind::granular)
                              == GranularEditor::kDesignHeightWithCloud
                       && GranularEditor::kDesignHeightWithCloud
                              > SynthWindow::kDesignHeight,
                   NI_TXT ("粒子页比别的页高（粒子 ")
                       + juce::String (window.getPageHeightForTest()) + NI_TXT (" / 其它 ")
                       + juce::String (SynthWindow::kDesignHeight) + NI_TXT ("）"));

            auto placedControls = 0;

            for (int which = 0; which < 8; ++which)
                if (! editor.getControlBoundsForTest (which).isEmpty())
                    ++placedControls;

            check (placedControls == 8,
                   NI_TXT ("导入按钮 / 颗粒画布 / 取样位置 / 粒度 / 密度 / 键盘 / 滤波器 / 扫描 都排得下（")
                       + juce::String (placedControls)
                       + NI_TXT (" 个）"));

            const auto captions = editor.getCaptionsForTest();
            const std::vector<juce::String> wantedCaptions {
                NI_TXT ("粒度"), NI_TXT ("密度"), NI_TXT ("位置抖动"), NI_TXT ("间距抖动"),
                NI_TXT ("音高抖动"), NI_TXT ("反向概率"), NI_TXT ("位置"), NI_TXT ("扫描速度"),
                NI_TXT ("根音"), NI_TXT ("声像扩展"), NI_TXT ("增益"), NI_TXT ("位置包络"),
                NI_TXT ("起音"), NI_TXT ("衰减"), NI_TXT ("保持"), NI_TXT ("释放"),
                NI_TXT ("包络量"), NI_TXT ("音量包络"), NI_TXT ("滤波器"), NI_TXT ("类型"),
                NI_TXT ("斜率"), NI_TXT ("截止"), NI_TXT ("共振"), NI_TXT ("键跟踪")
            };

            auto described = true;

            for (const auto& word : wantedCaptions)
                if (! captions.contains (word))
                    described = false;

            check (described, NI_TXT ("每一条参数都有文字说明（实际「") + captions + NI_TXT ("」）"));

            check (editor.getFileLabelTextForTest() == NI_TXT ("还没有载入音频文件"),
                   NI_TXT ("还没导入文件时写得清清楚楚"));

            // No source at all: the audition must simply stay silent.
            sectionAudio.triggerPreviewGranularNote (track->granular, nullptr, 440.0, 100.0f);

            for (int block = 0; block < 10; ++block)
                sectionAudio.renderBlockForTest (512);

            editor.tickCloudForTest();

            check (sectionAudio.getPreviewGranularGrainCount() == 0
                       && editor.getCloudDotCountForTest() == 0,
                   NI_TXT ("没有音源时试听不出颗粒，也不崩"));

            // ---- 导入一个文件 -------------------------------------------------
            const auto grainFile = outputDirectory.getChildFile ("selftest-granular.wav");
            grainFile.deleteFile();

            {
                juce::WavAudioFormat wav;
                std::unique_ptr<juce::OutputStream> stream (grainFile.createOutputStream());

                if (stream != nullptr)
                {
                    const auto options = juce::AudioFormatWriterOptions{}
                                             .withSampleFormat (
                                                 juce::AudioFormatWriterOptions::SampleFormat::integral)
                                             .withSampleRate (48000.0)
                                             .withNumChannels (1)
                                             .withBitsPerSample (16);
                    auto writer = wav.createWriterFor (stream, options);

                    if (writer != nullptr)
                    {
                        juce::AudioBuffer<float> tone (1, 72000);

                        for (int i = 0; i < tone.getNumSamples(); ++i)
                            tone.setSample (0, i,
                                            (float) std::sin (2.0 * juce::MathConstants<double>::pi
                                                              * 440.0 * i / 48000.0));

                        writer->writeFromAudioSampleBuffer (tone, 0, tone.getNumSamples());
                    }
                }
            }

            const auto importError = editor.loadFile (grainFile);
            pump();

            check (importError.isEmpty(), NI_TXT ("导入音频成功（") + importError + NI_TXT ("）"));
            check (editor.getFileLabelTextForTest().contains ("selftest-granular.wav"),
                   NI_TXT ("文件名出现在编辑器里（「") + editor.getFileLabelTextForTest()
                       + NI_TXT ("」）"));
            check (! document.findTrack (newId)->granular.filePath.isEmpty(),
                   NI_TXT ("路径进了工程"));

            // ---- 试听：颗粒云真的在响，散点跟着走 ------------------------------
            editor.setParamForTest (3, 60.0);    // 密度
            pump();

            juce::String grainError;
            auto grainSample = getCachedSample (grainFile.getFullPathName(), grainError);

            sectionAudio.triggerPreviewGranularNote (document.findTrack (newId)->granular,
                                                     grainSample, 523.25, 110.0f);

            for (int block = 0; block < 40; ++block)
                sectionAudio.renderBlockForTest (512);

            const auto cloudGrains = sectionAudio.getPreviewGranularGrainCount();

            check (cloudGrains > 0, NI_TXT ("试听的时候颗粒云在响（")
                                        + juce::String (cloudGrains) + NI_TXT (" 颗）"));

            editor.tickCloudForTest();
            const auto dotsBefore = editor.getCloudDotCountForTest();

            check (dotsBefore > 0, NI_TXT ("散点画布上真的画出了颗粒（")
                                       + juce::String (dotsBefore) + NI_TXT (" 个点）"));

            const auto granularImage = snapshot (editor);
            check (writeImage (granularImage, outputDirectory.getChildFile ("51-granular.png")),
                   NI_TXT ("粒子合成器快照（波形 + 颗粒散点）"));
            checkColourCount (granularImage, theme::referenceLine, 200,
                              NI_TXT ("波形画出来了"), 40);
            checkColourCount (granularImage, theme::accent, 60, NI_TXT ("散点画出来了"), 10);

            // ---- 松开：试听声部必须真的停下来 --------------------------------
            // The editor's keyboard sends a note-off when the key comes up; the
            // engine releases the audition voice here. Without this the granular
            // cloud simply kept sounding for ever.
            sectionAudio.releasePreviewNote();

            for (int block = 0; block < 80; ++block)
                sectionAudio.renderBlockForTest (512);

            editor.tickCloudForTest();

            check (sectionAudio.getPreviewGranularGrainCount() == 0
                       && editor.getCloudDotCountForTest() == 0,
                   NI_TXT ("松开以后颗粒云停下来（还剩 ")
                       + juce::String (sectionAudio.getPreviewGranularGrainCount())
                       + NI_TXT (" 颗）"));

            // ---- 预设：保存 / 改名 / 载入 / 删除 --------------------------------
            check (editor.savePresetForTest (NI_TXT ("自检粒子"), true),
                   NI_TXT ("当前音色存成了一条预设"));
            pump();

            const auto& granularPresets = component.getPresetStore().getGranularPresets();

            check (granularPresets.size() == 1
                       && granularPresets.front().filePath == grainFile.getFullPathName()
                       && std::abs (granularPresets.front().densityHz - 60.0f) < 0.01f,
                   NI_TXT ("预设把文件名与参数一起存了（")
                       + juce::String ((int) granularPresets.size()) + NI_TXT (" 条）"));
            check (editor.getSelectedPresetIndex() == 0,
                   NI_TXT ("预设行选中了刚存的那条"));
            check (editor.getPresetBar().getComboBoxBoundsForTest().getHeight() > 0,
                   NI_TXT ("预设行有真实尺寸"));

            check (editor.getPresetBar().renamePreset (kUserPresetIdBase,
                                                       NI_TXT ("改过名的粒子预设")),
                   NI_TXT ("预设能改名"));
            check (component.getPresetStore().getGranularPresets().front().name
                       == NI_TXT ("改过名的粒子预设"),
                   NI_TXT ("改的名字进了库"));

            document.applyEdit (NI_TXT ("粒子自检改参数"), [newId] (Project& project)
            {
                if (auto* t = project.findTrack (newId))
                    t->granular.densityHz = 5.0f;
            });
            pump();

            check (editor.getPresetBar().loadPreset (kUserPresetIdBase),
                   NI_TXT ("预设能载入"));
            pump();
            check (std::abs (document.findTrack (newId)->granular.densityHz - 60.0f) < 0.01f,
                   NI_TXT ("载入把参数拿回来了（密度又回到 60）"));

            check (editor.getPresetBar().deletePreset (kUserPresetIdBase),
                   NI_TXT ("预设能删除"));
            check (component.getPresetStore().getGranularPresets().empty(),
                   NI_TXT ("库里又空了"));

            // ---- 存盘再读回：参数与文件都在 ------------------------------------
            const auto granularProjectFile = outputDirectory.getChildFile ("selftest-granular.nprj");
            juce::String saveError;

            check (document.saveToFile (granularProjectFile, saveError), saveError);

            const auto savedJson = granularProjectFile.loadFileAsString();

            Project reloadedGranular;
            juce::String loadError;

            check (projectFromJson (savedJson, reloadedGranular, loadError), loadError);

            const InstrumentTrack* reloadedGrain = nullptr;

            for (const auto& t : reloadedGranular.tracks)
                if (t.kind == TrackKind::granular && t.id == newId)
                    reloadedGrain = &t;

            if (reloadedGrain != nullptr)
                check (reloadedGrain->granular.filePath == grainFile.getFullPathName()
                           && std::abs (reloadedGrain->granular.densityHz - 60.0f) < 0.01f,
                       NI_TXT ("存盘再读回，文件与参数都在"));
            else
                check (false, NI_TXT ("读回来的工程里应该有那条粒子合成器轨"));

            // ---- 文件不见了：不崩，写明找不到，只是没声音 ----------------------
            const auto missingPath = outputDirectory.getChildFile ("nidaw-not-here.wav");
            missingPath.deleteFile();

            clearSampleCache();

            document.applyEdit (NI_TXT ("粒子自检丢文件"),
                                [newId, missingPath] (Project& project)
            {
                if (auto* t = project.findTrack (newId))
                    t->granular.filePath = missingPath.getFullPathName();
            });
            pump();

            check (editor.getFileLabelTextForTest().contains (NI_TXT ("载入失败")),
                   NI_TXT ("文件不在时编辑器写明载入失败（「")
                       + editor.getFileLabelTextForTest() + NI_TXT ("」）"));

            sectionAudio.triggerPreviewGranularNote (document.findTrack (newId)->granular,
                                                     nullptr, 523.25, 110.0f);

            for (int block = 0; block < 10; ++block)
                sectionAudio.renderBlockForTest (512);

            check (sectionAudio.getPreviewGranularGrainCount() == 0,
                   NI_TXT ("文件不在时不崩，也没有声音"));

            // Leave the arrangement as it was found.
            sectionLanes.deleteTrackNow (newId);
            pump();

            check ((int) document.getProject().tracks.size() == before,
                   "the granular track is cleaned up again");
        }

        // ---- 音轨栏与编辑栏各自一套视图 + 循环片段的缩略图 -------------------

        beginTest (NI_TXT ("音轨栏与编辑栏各自平移缩放，缩略图按真刻度平铺"));

        {
            auto& sectionLanes = component.getTrackArea();
            auto& sectionGrid = component.getEditorPanel().getGrid();

            // One clip that loops every bar, four bars long, with a single note in
            // its one-bar period.
            const auto barTicks = (juce::int64) kTicksPerQuarter * 4;
            juce::int64 clipId = 0;

            document.applyEdit ("selftest split view setup", [&clipId, barTicks] (Project& project)
            {
                project.tracks.clear();
                project.referenceLines.clear();
                project.nextTrackId = 1;

                const auto id = addTrack (project, TrackKind::synth);
                auto* track = project.findTrack (id);

                if (track == nullptr)
                    return;

                track->clips.clear();

                auto clip = makeDefaultClip (project);
                clip.startTick = 0;
                clip.lengthTicks = barTicks * 4;
                clip.loopLengthTicks = barTicks;
                clip.looped = true;

                Note note;
                note.id = project.nextNoteId++;
                note.startTick = 0;
                note.durationTicks = kTicksPerQuarter;
                note.frequencyHz = 440.0;
                note.velocity = 100;
                clip.notes.push_back (note);

                clipId = clip.id;
                track->clips.push_back (std::move (clip));
            });
            pump();

            appState.selectTrack (document.getProject().tracks.front().id);
            appState.selectClip (clipId);
            appState.setEditorOpen (true);

            setBothTimelines (document, 140.0, 0.0);
            pump();

            // ---- 缩略图：一个周期画一遍，按真刻度排开 ------------------------
            const auto bars = sectionLanes.getClipThumbnailRectsForTest (clipId);
            const auto pixelsPerBar = (double) barTicks * 140.0 / (double) kTicksPerQuarter;

            check ((int) bars.size() == 4,
                   NI_TXT ("a one-bar loop four bars long draws four periods (got ")
                       + juce::String ((int) bars.size()) + ")");

            if (bars.size() == 4)
            {
                auto spacedRight = true;

                for (size_t i = 1; i < bars.size(); ++i)
                    if (std::abs ((double) (bars[i].getX() - bars[i - 1].getX()) - pixelsPerBar)
                        > 1.5)
                        spacedRight = false;

                check (spacedRight,
                       NI_TXT ("and they sit one period apart at the real tick scale"));

                // The repeat dividers are drawn at the same ticks, so the picture and
                // the dividers agree instead of one of them being stretched.
                auto linesUp = true;

                for (size_t i = 0; i < bars.size(); ++i)
                {
                    const auto divider = (double) TrackArea::headerWidth
                                         + sectionLanes.xForTick ((double) ((juce::int64) i * barTicks));

                    if (std::abs ((double) bars[i].getX() - divider) > 1.5)
                        linesUp = false;
                }

                check (linesUp, NI_TXT ("and every period starts where its divider is"));
            }

            // Growing the clip adds a period; it does not restretch the ones already
            // there. Before this was fixed the whole thumbnail was scaled to the clip
            // width, so dragging the length made the notes slide around.
            document.applyEdit ("selftest grow loop clip", [clipId, barTicks] (Project& project)
            {
                if (auto* clip = project.findClip (clipId))
                    clip->lengthTicks = barTicks * 5;
            });
            pump();

            const auto grown = sectionLanes.getClipThumbnailRectsForTest (clipId);

            check ((int) grown.size() == 5,
                   NI_TXT ("growing the clip by a bar adds one more period (got ")
                       + juce::String ((int) grown.size()) + ")");

            auto unchanged = grown.size() >= 4 && bars.size() == 4;

            for (size_t i = 0; unchanged && i < 4; ++i)
                if (grown[i] != bars[i])
                    unchanged = false;

            check (unchanged,
                   NI_TXT ("and every period that was already there stays exactly where it was"));

            // ---- 两栏的横向视图互相独立 ---------------------------------------
            const auto probeTick = (juce::int64) kTicksPerQuarter * 2;
            const auto clipXBefore = sectionLanes.clipBoundsForTest (clipId).getX();
            const auto noteXBefore = sectionGrid.pointFor (440.0, probeTick).x;

            // Zoom the editor only: the arrangement must not move under the user.
            document.viewState().pixelsPerQuarter = 280.0;
            pump();

            const auto noteXZoomed = sectionGrid.pointFor (440.0, probeTick).x;

            check (noteXZoomed != noteXBefore,
                   NI_TXT ("zooming the editor moves the editor (")
                       + juce::String (noteXBefore, 1) + " -> " + juce::String (noteXZoomed, 1)
                       + NI_TXT (")"));
            check (sectionLanes.clipBoundsForTest (clipId).getX() == clipXBefore,
                   NI_TXT ("while the arrangement stays exactly where it was (")
                       + juce::String (sectionLanes.clipBoundsForTest (clipId).getX()) + NI_TXT (")"));

            // Scroll the arrangement only: the editor must not move.
            document.viewState().arrangementHorizontalOffsetTicks = 1920.0;
            pump();

            check (sectionLanes.clipBoundsForTest (clipId).getX() != clipXBefore,
                   NI_TXT ("scrolling the arrangement moves the clips (")
                       + juce::String (sectionLanes.clipBoundsForTest (clipId).getX()) + NI_TXT (")"));
            check (sectionGrid.pointFor (440.0, probeTick).x == noteXZoomed,
                   NI_TXT ("while the editor stays where it was"));

            // Within one pane the ruler and the sectionLanes still agree pixel for pixel:
            // only the two panes are independent of each other.
            check (std::abs (sectionLanes.getRuler().getXForTickForTest (1920.0)
                             - sectionLanes.xForTick (1920.0)) < 1.0e-6,
                   NI_TXT ("the arrangement's ruler and lanes still share one mapping"));

            // And the editor's ruler read the editor's mapping all along, so it was
            // not moved by the arrangement either.
            check (std::abs (document.viewState().arrangementHorizontalOffsetTicks - 1920.0)
                       < 1.0e-6
                       && std::abs (document.viewState().horizontalOffsetTicks) < 1.0e-6,
                   NI_TXT ("the two views really are separate numbers"));

            // Back where the rest of the run expects them.
            setBothTimelines (document, 140.0, 0.0);
            pump();
        }

        // ---- external VST3 instruments --------------------------------------

        beginTest (NI_TXT ("external VST3 instruments are hosted, scanned and saved"));

        {
            auto& host = component.getPluginHost();
            auto& sectionLanes = component.getTrackArea();

            // Pretend a scan found something. Everything below then works without
            // a single plugin being installed on this machine.
            juce::PluginDescription fakeA;
            fakeA.name = NI_TXT ("测试音源 A");
            fakeA.manufacturerName = "HM";
            fakeA.fileOrIdentifier = outputDirectory.getChildFile ("fake-a.vst3").getFullPathName();
            fakeA.pluginFormatName = "VST3";
            fakeA.isInstrument = true;
            fakeA.uniqueId = 1001;

            host.addPluginForTest (fakeA);
            host.saveCacheForTest();

            check (host.getPluginCount() >= 1, NI_TXT ("a scanned plugin lands in the list"));

            // The list really is a cache: a second host reads the same file back.
            {
                PluginHostManager sectionProject;
                check (sectionProject.getPluginCount() >= 1,
                       NI_TXT ("the plugin cache survives a reload (got ")
                           + juce::String (sectionProject.getPluginCount()) + ")");
            }

            // Scanning a folder with nothing in it is not an error.
            {
                const auto emptyFolder = outputDirectory.getChildFile ("selftest-empty-plugins");
                emptyFolder.createDirectory();

                juce::String scanError;
                const auto found = host.scanFolderNow (emptyFolder, scanError);

                check (found == 0 && scanError.isEmpty(),
                       NI_TXT ("scanning an empty folder finds nothing and reports no error"));
            }

            // A track whose plugin is missing keeps its place, and says so.
            const auto missingPath = outputDirectory.getChildFile ("missing.vst3").getFullPathName();
            const auto vstTrackId = sectionLanes.addVstTrack (NI_TXT ("缺失的插件"), missingPath, "999");
            pump();

            const auto* vstTrack = document.getProject().findTrack (vstTrackId);

            check (vstTrackId != 0 && vstTrack != nullptr && vstTrack->kind == TrackKind::vst,
                   NI_TXT ("a VST track can be created"));
            check (vstTrack != nullptr && vstTrack->name == NI_TXT ("缺失的插件"),
                   NI_TXT ("the track takes the plugin's name"));
            check (vstTrack != nullptr && vstTrack->vst.pluginPath == missingPath,
                   NI_TXT ("the track remembers where its plugin lives"));

            if (vstTrack != nullptr)
            {
                host.requestInstrument (vstTrackId, vstTrack->vst);
                pump();

                check (host.getLoadError (vstTrackId).isNotEmpty(),
                       NI_TXT ("a missing plugin explains why it could not be loaded"));
            }

            check (writeImage (snapshot (component),
                               outputDirectory.getChildFile ("31-vst-track.png")),
                   NI_TXT ("snapshot with a VST track"));

            if (auto* header = sectionLanes.getLaneHeaderForTest ((int) document.getProject().tracks.size() - 1))
            {
                check (header->getTypeTextForTest() == NI_TXT ("VST 乐器"),
                       NI_TXT ("the lane header calls it a VST instrument"));
                check (header->getTypeLineForTest().contains (NI_TXT ("插件缺失")),
                       NI_TXT ("the lane header marks the plugin as missing"));
                check (writeImage (snapshot (*header),
                                   outputDirectory.getChildFile ("33-vst-missing.png")),
                       NI_TXT ("lane header snapshot for a missing plugin"));
            }
            else
                check (false, NI_TXT ("the VST lane has a header"));

            // The plugin window explains the same thing instead of showing an
            // empty frame.
            if (vstTrack != nullptr)
            {
                PluginWindow window (host);
                window.showForTrack (vstTrackId, vstTrack->name);

                check (! window.hasEditorForTest() && window.getMessageForTest().isNotEmpty(),
                       NI_TXT ("the plugin window says the plugin is not loaded"));
            }

            // The picker lays out, filters, and reports what was chosen.
            {
                PluginPicker picker (host, NI_TXT ("选择 VST 乐器"));
                picker.setSize (600, 520);
                picker.resized();

                // Two vendors, two useful types, one plugin with no vendor and one
                // with no category: everything the grouping has to cope with.
                juce::Array<juce::PluginDescription> listed;
                juce::PluginDescription one;
                one.name = NI_TXT ("测试音源 A");
                one.manufacturerName = "Vendor One";
                one.isInstrument = true;
                one.category = "Instrument|Synth";
                juce::PluginDescription two;
                two.name = NI_TXT ("测试音源 B");
                two.manufacturerName = "Vendor One";
                two.isInstrument = true;
                two.category = "Instrument|Sampler";
                juce::PluginDescription three;
                three.name = "Third Plugin";
                three.manufacturerName = "Vendor Two";
                three.isInstrument = true;
                three.category = "Fx|Reverb";   // an odd combination, but legal
                juce::PluginDescription four;
                four.name = "Anonymous";
                four.isInstrument = true;       // no vendor and no category at all
                listed.add (one);
                listed.add (two);
                listed.add (three);
                listed.add (four);

                picker.setPluginsForTest (listed);

                check (picker.getVisibleCountForTest() == 4,
                       NI_TXT ("the picker lists the instruments it was given"));
                check (picker.getTreeBoundsForTest().getHeight() > 40,
                       NI_TXT ("the plugin tree has room to show them"));
                check (! picker.getSearchBoundsForTest().isEmpty()
                           && ! picker.getGroupingBoundsForTest().isEmpty()
                           && ! picker.getScanButtonBoundsForTest().isEmpty()
                           && ! picker.getOkButtonBoundsForTest().isEmpty(),
                       NI_TXT ("the picker has a search box, a grouping box, a scan button and an OK button"));

                // ---- 分组：默认按厂家 ---------------------------------------
                check (picker.getGroupingForTest() == PluginGroupAxis::vendor,
                       NI_TXT ("the picker starts grouped by vendor"));
                check (picker.getGroupCountForTest() == 3,
                       NI_TXT ("two vendors plus 未知厂家 make three headings (got ")
                           + juce::String (picker.getGroupCountForTest()) + ")");
                check (picker.getGroupTextForTest (0) == juce::String ("Vendor One (2)"),
                       NI_TXT ("the first heading names the vendor and its count (got '")
                           + picker.getGroupTextForTest (0) + "')");
                check (picker.getGroupTextForTest (2) == NI_TXT ("未知厂家 (1)"),
                       NI_TXT ("a plugin without a vendor lands in 未知厂家, last (got '")
                           + picker.getGroupTextForTest (2) + "')");
                check (picker.getPluginTextForTest (0, 0) == NI_TXT ("测试音源 A - Vendor One"),
                       NI_TXT ("plugin rows still read 'name - vendor' (got '")
                           + picker.getPluginTextForTest (0, 0) + "')");
                check (picker.getShownPluginCountForTest() == 4,
                       NI_TXT ("headings start open, so every plugin is on screen"));

                // What the TreeView really holds - counted by walking the widget,
                // not the data the picker keeps beside it. The first version of this
                // tree never filled the widget at all, and every other hook here
                // reads the data, so the list stayed empty while the checks stayed
                // green (the user found it, the tests did not).
                check (picker.getTreeVisibleRowCountForTest() == 4,
                       NI_TXT ("the tree widget itself holds the four plugin rows (got ")
                           + juce::String (picker.getTreeVisibleRowCountForTest()) + ")");
                check (picker.getTreeItemCountForTest() >= 7,
                       NI_TXT ("the headings are in the widget too (got ")
                           + juce::String (picker.getTreeItemCountForTest())
                           + NI_TXT (" items)"));

                {
                    const auto painted = countNotColour (snapshot (picker),
                                                         picker.getTreeBoundsForTest(),
                                                         theme::gridBackground);

                    check (painted > 200,
                           NI_TXT ("and the rows are really painted, not just modelled (pixels = ")
                               + juce::String (painted) + ")");
                }

                picker.setGroupOpenForTest (0, false);

                check (! picker.isGroupOpenForTest (0)
                           && picker.getShownPluginCountForTest() == 2
                           && picker.getTreeVisibleRowCountForTest() == 2,
                       NI_TXT ("closing a heading hides its plugins (shown ")
                           + juce::String (picker.getShownPluginCountForTest()) + ")");

                picker.setGroupOpenForTest (0, true);

                check (picker.getShownPluginCountForTest() == 4
                           && picker.getTreeVisibleRowCountForTest() == 4,
                       NI_TXT ("opening it again brings them back"));

                check (writeImage (snapshot (picker),
                                   outputDirectory.getChildFile ("36-plugin-picker-vendor.png")),
                       NI_TXT ("picker grouped by vendor snapshot"));

                // ---- 分组：按类型 -------------------------------------------
                picker.setGroupingForTest (PluginGroupAxis::type);

                check (picker.getGroupingForTest() == PluginGroupAxis::type,
                       NI_TXT ("the picker can group by type"));

                juce::StringArray typeHeadings;

                for (int i = 0; i < picker.getGroupCountForTest(); ++i)
                    typeHeadings.add (picker.getGroupTextForTest (i));

                check (typeHeadings.contains (NI_TXT ("合成器 (1)"))
                           && typeHeadings.contains (NI_TXT ("采样器 (1)"))
                           && typeHeadings.contains (NI_TXT ("混响 (1)"))
                           && typeHeadings.contains (NI_TXT ("其他 (1)")),
                       NI_TXT ("every VST3 type gets its own Chinese heading (got '")
                           + typeHeadings.joinIntoString ("', '") + "')");

                check (writeImage (snapshot (picker),
                                   outputDirectory.getChildFile ("37-plugin-picker-type.png")),
                       NI_TXT ("picker grouped by type snapshot"));

                // ---- 分组：不分组 -------------------------------------------
                picker.setGroupingForTest (PluginGroupAxis::none);

                check (picker.getGroupCountForTest() == 1
                           && picker.getGroupTextForTest (0) == NI_TXT ("全部插件 (4)"),
                       NI_TXT ("不分组 puts everything in one plain list (got '")
                           + picker.getGroupTextForTest (0) + "')");

                // With no heading the four rows hang straight off the invisible root,
                // so the widget has exactly four rows and no heading of its own.
                check (picker.getTreeVisibleRowCountForTest() == 4
                           && picker.getTreeItemCountForTest() == 4,
                       NI_TXT ("and the four rows are really in the widget (got ")
                           + juce::String (picker.getTreeItemCountForTest()) + " items, "
                           + juce::String (picker.getTreeVisibleRowCountForTest())
                           + NI_TXT (" shown)"));

                picker.setGroupingForTest (PluginGroupAxis::vendor);

                picker.setSearchTextForTest (juce::String ("B"));

                check (picker.getVisibleCountForTest() == 1,
                       NI_TXT ("typing in the search box filters the list"));
                check (picker.getGroupCountForTest() == 1,
                       NI_TXT ("and leaves only the heading that holds the hit"));

                // A search that hits nothing, and a picker with nothing to show at
                // all, must both say so instead of leaving a blank box where the
                // list should be.
                picker.setSearchTextForTest (juce::String ("zzzznothing"));

                check (picker.getTreeItemCountForTest() == 0
                           && picker.getTreeVisibleRowCountForTest() == 0,
                       NI_TXT ("a search with no hit leaves the tree empty"));
                check (countNotColour (snapshot (picker), picker.getTreeBoundsForTest(),
                                       theme::gridBackground) > 100,
                       NI_TXT ("and the window explains that instead of showing nothing"));

                picker.setSearchTextForTest ({});

                {
                    PluginPicker emptyPicker (component.getPluginHost(),
                                              NI_TXT ("选择 VST 乐器"));
                    emptyPicker.setSize (600, 520);
                    emptyPicker.resized();
                    emptyPicker.setPluginsForTest ({});

                    check (emptyPicker.getTreeItemCountForTest() == 0,
                           NI_TXT ("a picker with nothing to show has an empty tree"));
                    check (countNotColour (snapshot (emptyPicker),
                                           emptyPicker.getTreeBoundsForTest(),
                                           theme::gridBackground) > 100,
                           NI_TXT ("and tells the user to scan or add a folder"));
                }

                juce::String chosen;
                picker.onChosen = [&chosen] (const juce::PluginDescription& description)
                {
                    chosen = description.name;
                };
                picker.chooseForTest (0, 0);

                check (chosen == NI_TXT ("测试音源 A"),
                       NI_TXT ("double clicking a plugin row reports that plugin"));

                check (writeImage (snapshot (picker),
                                   outputDirectory.getChildFile ("32-plugin-picker.png")),
                       NI_TXT ("plugin picker snapshot"));
            }
        }

        // ---- with a real plugin: instantiate, play at pitch, keep the state ----

        if (testPlugin.exists())
        {
            beginTest (NI_TXT ("a real VST3 instrument loads, plays in tune and saves its state"));

            auto& host = component.getPluginHost();

            juce::String scanError;
            const auto found = host.scanFolderNow (testPlugin.getParentDirectory(), scanError);

            check (found >= 1, NI_TXT ("a scan finds the test plugin (") + scanError + ")");

            // The path the user takes: scan a folder, then open the picker and pick
            // the plugin out of the list. This one reads the host's real list (the
            // checks above feed the picker a made-up one), so it is the check that
            // would have caught a picker whose tree was never filled in.
            {
                PluginPicker picker (component.getPluginHost(), NI_TXT ("选择 VST 乐器"));
                picker.setSize (600, 520);
                picker.resized();

                check (picker.getVisibleCountForTest() >= 1,
                       NI_TXT ("the picker offers what the scan found (got ")
                           + juce::String (picker.getVisibleCountForTest()) + ")");
                check (picker.getTreeVisibleRowCountForTest() >= 1,
                       NI_TXT ("and the rows are in the tree, not just in the list behind it (got ")
                           + juce::String (picker.getTreeVisibleRowCountForTest()) + ")");
            }

            VstParams params;
            params.name = "HM Test Instrument";
            params.pluginPath = testPlugin.getFullPathName();

            juce::String sectionError;
            auto instrument = host.makeOfflineInstrument (params, 48000.0,
                                                          Exporter::kRenderBlockSize, sectionError);

            check (instrument != nullptr, NI_TXT ("the test plugin instantiates: ") + sectionError);

            if (instrument != nullptr)
            {
                const auto state = instrument->captureState();
                check (state.getSize() > 0, NI_TXT ("the plugin hands out a state chunk"));

                // A 660 Hz note is not on a semitone: it lands on MIDI 76 plus a
                // couple of cents, which only comes back at 660 Hz if the channel's
                // pitch bend really made it to the plugin.
                auto project = makeDefaultProject();
                project.sampleRate = 48000.0;

                const auto sectionTrackId = addTrack (project, TrackKind::vst);

                if (auto* track = project.findTrack (sectionTrackId))
                {
                    track->vst = params;

                    Note note;
                    note.id = 1;
                    note.startTick = 0;
                    note.durationTicks = kTicksPerQuarter * 2;
                    note.frequencyHz = 660.0;
                    note.velocity = 110;
                    track->clips.front().notes.push_back (note);
                }

                const auto vstWav = outputDirectory.getChildFile ("selftest-vst.wav");

                ExportSettings settings;
                settings.format = ExportSettings::Format::wav;
                settings.bitsPerSample = 24;

                const auto factory = [&host, &params] (juce::int64, const VstParams&)
                {
                    juce::String factoryError;
                    return host.makeOfflineInstrument (params, 48000.0,
                                                       Exporter::kRenderBlockSize, factoryError);
                };

                juce::String renderError;
                check (Exporter::renderToFile (project, vstWav, settings, {}, {}, {}, renderError,
                                               factory),
                       NI_TXT ("a VST track renders to a file: ") + renderError);

                juce::WavAudioFormat sectionWavFormat;
                std::unique_ptr<juce::AudioFormatReader> sectionReader (
                    sectionWavFormat.createReaderFor (new juce::FileInputStream (vstWav), true));

                check (sectionReader != nullptr, NI_TXT ("the rendered VST file is readable"));

                if (sectionReader != nullptr)
                {
                    const auto start = (juce::int64) (sectionReader->sampleRate * 0.20);
                    const auto count = juce::jmin ((juce::int64) (sectionReader->sampleRate * 0.5),
                                                   sectionReader->lengthInSamples - start);

                    if (count > 100)
                    {
                        juce::AudioBuffer<float> rendered (1, (int) count);
                        sectionReader->read (&rendered, 0, (int) count, start, true, false);

                        const auto* data = rendered.getReadPointer (0);
                        double first = -1.0, last = -1.0;
                        int crossings = 0;

                        for (int i = 1; i < (int) count; ++i)
                        {
                            if (data[i - 1] <= 0.0f && data[i] > 0.0f)
                            {
                                const auto previous = (double) data[i - 1];
                                const auto current = (double) data[i];
                                const auto fraction = previous == current
                                                          ? 0.0 : (-previous) / (current - previous);
                                const auto time = ((double) (i - 1) + fraction) / sectionReader->sampleRate;

                                if (first < 0.0) first = time;
                                last = time;
                                ++crossings;
                            }
                        }

                        const auto measured = crossings > 1 && last > first
                                                  ? (double) (crossings - 1) / (last - first)
                                                  : 0.0;

                        checkNear (measured, 660.0, 1.0,
                                   NI_TXT ("the exported note really is 660 Hz (measured ")
                                       + juce::String (measured, 3) + NI_TXT (" Hz)"));
                        check (rendered.getRMSLevel (0, 0, (int) count) > 0.01f,
                               NI_TXT ("the hosted instrument is not silent"));
                    }
                    else
                        check (false, NI_TXT ("the rendered VST file is long enough to measure"));
                }

                // The state has to survive the project file as well: that is what
                // makes a saved project sound the same when it is reopened. The
                // track is loaded the way the app loads one, so this also covers
                // the live path (create the instance, prepare it, hand it out).
                juce::int64 liveTrackId = 0;

                document.applyEdit ("selftest vst live track",
                                    [&liveTrackId, &params] (Project& state)
                {
                    liveTrackId = addTrack (state, TrackKind::vst);

                    if (auto* track = state.findTrack (liveTrackId))
                    {
                        track->name = params.name;
                        track->vst = params;
                    }
                });
                pump();

                host.requestInstrument (liveTrackId, params);

                for (int attempt = 0; attempt < 150 && host.pluginFor (liveTrackId) == nullptr;
                     ++attempt)
                    pump();

                const auto livePlugin = host.pluginFor (liveTrackId);

                check (livePlugin != nullptr, NI_TXT ("the host loads a plugin for a track"));

                if (livePlugin != nullptr)
                {
                    document.applyDerivedEdit ([&host] (Project& state)
                    {
                        host.captureStates (state);
                    });

                    // A plugin that brings no interface of its own still gets one
                    // to click on.
                    PluginWindow window (host);
                    window.showForTrack (liveTrackId, params.name);

                    check (window.hasEditorForTest(),
                           NI_TXT ("the plugin's interface opens in its own window"));

                    // The window has to be at least as big as the interface the
                    // plugin asked for: a host that keeps its own default size
                    // leaves most of a large plugin (Kontakt) outside the window.
                    const auto wanted = window.getPluginPreferredSizeForTest();
                    const auto screen = placement::displayAreaFor (nullptr);
                    // The window keeps a small gap from the screen edges, so that
                    // is the area a window can actually occupy.
                    const auto room = placement::usableArea (screen);

                    // The test plugin's interface is 900x600 (see NiDaw/TestPlugin).
                    check (wanted.x >= 900 && wanted.y >= 600,
                           NI_TXT ("the plugin's own size was read (")
                               + juce::String (wanted.x) + "x" + juce::String (wanted.y) + NI_TXT (")"));

                    // The window opens at the plugin's size, or - on a screen that
                    // cannot give that much - at the work area; either way it never
                    // hangs off the screen, and the interface keeps its own size so
                    // the parts that do not fit can be scrolled to.
                    check (screen.contains (window.getBounds()),
                           NI_TXT ("the plugin window opens inside the screen (")
                               + window.getBounds().toString() + NI_TXT (" in ")
                               + screen.toString() + NI_TXT (")"));

                    if (wanted.x <= room.getWidth() && wanted.y <= room.getHeight())
                    {
                        check (window.getWidth() >= wanted.x && window.getHeight() >= wanted.y,
                               NI_TXT ("the window is at least as big as the interface (")
                                   + juce::String (window.getWidth()) + "x"
                                   + juce::String (window.getHeight()) + NI_TXT (" for ")
                                   + juce::String (wanted.x) + "x" + juce::String (wanted.y)
                                   + NI_TXT (")"));
                    }
                    else
                    {
                        check (window.getWidth() <= room.getWidth()
                                   && window.getHeight() <= room.getHeight(),
                               NI_TXT ("a screen smaller than the plugin gets the window shrunk (")
                                   + juce::String (window.getWidth()) + "x"
                                   + juce::String (window.getHeight()) + NI_TXT (")"));
                    }

                    const auto viewSize = window.getEditorViewSizeForTest();

                    check (viewSize.x >= wanted.x && viewSize.y >= wanted.y,
                           NI_TXT ("the interface itself keeps the size it asked for (")
                               + juce::String (viewSize.x) + "x" + juce::String (viewSize.y)
                               + NI_TXT (")"));

                    // And when the plugin resizes itself (Kontakt does that when a
                    // library loads or its zoom changes) the window follows instead
                    // of clipping whatever grew past its edge.
                    if (auto* editor = window.getEditorForTest())
                    {
                        editor->setSize (1200, 320);
                        pump();

                        check (screen.contains (window.getBounds()),
                               NI_TXT ("the window follows a plugin that resizes itself (")
                                   + juce::String (window.getWidth()) + "x"
                                   + juce::String (window.getHeight()) + NI_TXT (")"));
                        check (window.getWidth() >= juce::jmin (1200, room.getWidth())
                                   && window.getHeight() >= juce::jmin (320, room.getHeight()),
                               NI_TXT ("and it grows as far as the screen allows (")
                                   + juce::String (window.getWidth()) + "x"
                                   + juce::String (window.getHeight()) + NI_TXT (")"));

                        // What the window works from is the size the interface has
                        // *now*, not the snapshot taken when it was created: a stale
                        // record is what left the window smaller than the interface
                        // with nothing to scroll.
                        check (window.getPluginPreferredSizeForTest()
                                   == juce::Point<int> (1200, 320),
                               NI_TXT ("the recorded interface size follows the plugin (got ")
                                   + juce::String (window.getPluginPreferredSizeForTest().x)
                                   + "x"
                                   + juce::String (window.getPluginPreferredSizeForTest().y)
                                   + NI_TXT (")"));

                        // A plugin's own keyboard is usually driven by the computer
                        // keyboard, which needs the interface to be able to hold the
                        // focus: the host grabs it when the window opens.
                        check (editor->getWantsKeyboardFocus(),
                               NI_TXT ("the interface can hold the keyboard focus"));

                        // The other direction matters just as much: a plugin that
                        // makes itself smaller (Kontakt stepping its zoom down) must
                        // not be stretched straight back by a window that refuses to
                        // shrink - that tug of war is what made the interface flicker
                        // and stop responding.
                        editor->setSize (400, 300);
                        pump();

                        check (window.getWidth() == 400 && window.getHeight() == 300,
                               NI_TXT ("the window shrinks when the plugin does (")
                                   + juce::String (window.getWidth()) + "x"
                                   + juce::String (window.getHeight()) + NI_TXT (")"));
                        check (window.getPluginPreferredSizeForTest()
                                   == juce::Point<int> (400, 300),
                               NI_TXT ("and the recorded size shrank with it"));

                        const auto settledWindow = juce::Point<int> (window.getWidth(),
                                                                     window.getHeight());
                        const auto settledEditor = window.getEditorViewSizeForTest();

                        // A few more trips round the message loop: the size must not
                        // keep changing (that is what "混乱" looked like).
                        for (int i = 0; i < 8; ++i)
                            pump();

                        check (juce::Point<int> (window.getWidth(), window.getHeight())
                                   == settledWindow
                                   && window.getEditorViewSizeForTest() == settledEditor,
                               NI_TXT ("and nothing keeps resizing anything (")
                                   + juce::String (window.getWidth()) + "x"
                                   + juce::String (window.getHeight()) + NI_TXT (" / ")
                                   + juce::String (settledEditor.x) + "x"
                                   + juce::String (settledEditor.y) + NI_TXT (")"));

                        // A window smaller than the interface (a screen that cannot
                        // give the plugin its size): the interface is scrolled at its
                        // own size and the window stops chasing it, so the plugin is
                        // never squeezed and the two never fight.
                        editor->setSize (900, 600);
                        pump();

                        window.setSize (400, 300);
                        pump();

                        check (window.isInterfaceScrollableForTest(),
                               NI_TXT ("an interface bigger than its window is scrolled"));
                        check (window.getEditorViewSizeForTest() == juce::Point<int> (900, 600),
                               NI_TXT ("and it keeps the size the plugin asked for (")
                                   + juce::String (window.getEditorViewSizeForTest().x) + "x"
                                   + juce::String (window.getEditorViewSizeForTest().y)
                                   + NI_TXT (")"));
                        check (window.getWidth() == 400 && window.getHeight() == 300,
                               NI_TXT ("the window is not pushed back to the plugin's size (")
                                   + juce::String (window.getWidth()) + "x"
                                   + juce::String (window.getHeight()) + NI_TXT (")"));

                        editor->setSize (1200, 900);
                        pump();

                        check (window.getEditorViewSizeForTest() == juce::Point<int> (1200, 900),
                               NI_TXT ("a plugin that grows while scrolling keeps its new size"));
                        check (window.getWidth() == 400 && window.getHeight() == 300,
                               NI_TXT ("while the window stays where the screen put it (")
                                   + juce::String (window.getWidth()) + "x"
                                   + juce::String (window.getHeight()) + NI_TXT (")"));
                    }
                    else
                        check (false, NI_TXT ("the plugin editor is reachable for the sizing check"));

                    // The host reports *every* plugin that finishes loading, so a
                    // window that re-runs its setup unconditionally pulls a working
                    // interface apart and builds it again - which is how a plugin
                    // ends up painted before its own values have arrived (controls
                    // sitting at zero) while the user is trying to work in it.
                    if (auto* editorBefore = window.getEditorForTest())
                    {
                        window.refresh();
                        pump();

                        check (window.getEditorForTest() == editorBefore,
                               NI_TXT ("an interface is not rebuilt when its own plugin has not changed"));
                    }
                    else
                        check (false, NI_TXT ("the interface is still there to compare"));
                }

                const auto json = projectToJson (document.getProject());

                Project sectionProject;
                juce::String jsonError;

                check (projectFromJson (json, sectionProject, jsonError),
                       NI_TXT ("a project with a VST track round trips: ") + jsonError);

                auto storedState = juce::String();

                for (const auto& track : sectionProject.tracks)
                    if (track.kind == TrackKind::vst && track.vst.pluginPath == params.pluginPath)
                        storedState = track.vst.stateBase64;

                check (storedState.isNotEmpty(),
                       NI_TXT ("the plugin state is stored in the project file"));

                const auto liveState = livePlugin != nullptr ? livePlugin->captureState() : state;

                check (storedState == juce::Base64::toBase64 (liveState.getData(),
                                                              liveState.getSize()),
                       NI_TXT ("the stored state is the state the plugin handed out"));

                // ---- what an export renders is the plugin the user is hearing ----
                //
                // Not the state the project happens to hold: a track whose project
                // has not been saved since the plugin was loaded would otherwise be
                // rendered by the instrument's default patch, and a sampler's
                // default patch is an empty rack - a silent file with nothing on
                // screen to explain it.
                document.applyEdit ("selftest vst export note", [&liveTrackId] (Project& state)
                {
                    if (auto* track = state.findTrack (liveTrackId))
                    {
                        Note note;
                        note.id = state.nextNoteId++;
                        note.startTick = 0;
                        note.durationTicks = kTicksPerQuarter * 2;
                        note.frequencyHz = 660.0;
                        note.velocity = 110;
                        track->clips.front().notes = { note };
                    }
                });
                pump();

                auto* gainParameter = (juce::AudioProcessorParameter*) nullptr;

                for (auto* parameter : livePlugin->getInstance().getParameters())
                    if (parameter != nullptr && parameter->getName (64).containsIgnoreCase ("gain"))
                        gainParameter = parameter;

                check (gainParameter != nullptr,
                       NI_TXT ("the test plugin exposes the volume the export is checked with"));

                auto exportStem = [&component, &outputDirectory, &liveTrackId,
                                   &check] (const juce::String& folderName) -> juce::File
                {
                    ExportSettings settings;
                    settings.format = ExportSettings::Format::wav;
                    settings.bitsPerSample = 24;
                    settings.master = false;
                    settings.trackIds = { liveTrackId };

                    std::vector<juce::File> written;
                    juce::String error;

                    const auto ran = component.runExport (settings,
                                                          outputDirectory.getChildFile (folderName),
                                                          true, error, &written, nullptr);

                    check (ran && written.size() == 1,
                           NI_TXT ("one VST track exports to a stem of its own (") + error + NI_TXT (")"));

                    return written.empty() ? juce::File() : written.front();
                };

                juce::WavAudioFormat stemFormat;

                auto levelOfStem = [&stemFormat] (const juce::File& file)
                {
                    std::unique_ptr<juce::AudioFormatReader> reader (
                        stemFormat.createReaderFor (new juce::FileInputStream (file), true));

                    if (reader == nullptr)
                        return 0.0f;

                    const auto samples = (int) juce::jmin ((juce::int64) (reader->sampleRate * 0.5),
                                                           reader->lengthInSamples);

                    if (samples <= 10)
                        return 0.0f;

                    juce::AudioBuffer<float> buffer (1, samples);
                    reader->read (&buffer, 0, samples, 0, true, false);

                    return buffer.getRMSLevel (0, samples / 10, samples - samples / 10);
                };

                const auto loudStem = exportStem ("selftest-vst-stem-full");
                const auto loud = levelOfStem (loudStem);

                auto stateNow = [&livePlugin]
                {
                    const auto block = livePlugin->captureState();
                    return juce::Base64::toBase64 (block.getData(), block.getSize());
                };

                const auto stateBefore = stateNow();

                // The user turning the instrument's own volume down, which is the one
                // thing this side never writes itself.
                if (gainParameter != nullptr)
                    gainParameter->setValueNotifyingHost (0.25f);

                pump();

                const auto stateAfter = stateNow();

                check (stateAfter != stateBefore,
                       NI_TXT ("the plugin's own volume change is visible in its state"));

                const auto quietStem = exportStem ("selftest-vst-stem-quiet");
                const auto quiet = levelOfStem (quietStem);

                // The export narrated the plugin work: which track, which plugin, and
                // whether it came up - the thing that turns "导出时未响应" into a
                // one-line answer.
                {
                    const auto narration = MainComponent::getExportLogFile();
                    const auto text = narration.existsAsFile() ? narration.loadFileAsString()
                                                               : juce::String();

                    check (text.contains ("offline instance for track"),
                           NI_TXT ("the export log names the plugin it is preparing"));
                    check (text.contains ("offline instance ready"),
                           NI_TXT ("and says when it came up"));
                    check (text.contains ("capturing the live plugin states"),
                           NI_TXT ("and that it read the live plugins' state first"));
                }

                // The guard that puts a live plugin back if building the private
                // instance for the export disturbed it (Kontakt's instrument volume
                // going to zero is the reported case). Driven here with a state the
                // live plugin does not have, which must be put back onto it.
                {
                    auto guardPlugin = host.pluginFor (liveTrackId);
                    auto* guardGain = (juce::AudioProcessorParameter*) nullptr;

                    if (guardPlugin != nullptr)
                        for (auto* parameter : guardPlugin->getInstance().getParameters())
                            if (parameter != nullptr
                                && parameter->getName (64).containsIgnoreCase ("Gain"))
                                guardGain = parameter;

                    check (guardPlugin != nullptr && guardGain != nullptr,
                           NI_TXT ("the live plugin and its volume are reachable"));

                    if (guardPlugin != nullptr && guardGain != nullptr)
                    {
                        auto snapshotOfLiveState = [&]
                        {
                            auto before = document.getProject();
                            const auto state = guardPlugin->captureState();

                            for (auto& track : before.tracks)
                                if (track.kind == TrackKind::vst
                                    && track.vst.pluginPath == params.pluginPath)
                                    track.vst.stateBase64 = juce::Base64::toBase64 (
                                        state.getData(), state.getSize());

                            return before;
                        };

                        // Healthy case: the state the render started from is the state
                        // the live plugin still has, so nothing may be written back.
                        const auto restoresBefore
                            = PluginHostManager::getLiveStateRestoresForTest();

                        check (host.restoreStatesChangedSince (snapshotOfLiveState()) == 0,
                               NI_TXT ("an unmoved live plugin is left alone by the guard"));
                        check (PluginHostManager::getLiveStateRestoresForTest() == restoresBefore,
                               NI_TXT ("and nothing was written back"));
                        // The plugin's own state (what the user hears) is untouched: the
                        // host-side parameter cache is a different thing and is not what
                        // this guard is about.
                        auto beforeBytes = snapshotOfLiveState();
                        auto beforeState = juce::MemoryBlock();

                        for (const auto& track : beforeBytes.tracks)
                            if (track.kind == TrackKind::vst
                                && track.vst.pluginPath == params.pluginPath)
                            {
                                juce::MemoryOutputStream stream (beforeState, false);
                                juce::Base64::convertFromBase64 (stream, track.vst.stateBase64);
                            }

                        const auto afterState = guardPlugin->captureState();

                        check (afterState.getSize() == beforeState.getSize()
                                   && std::memcmp (afterState.getData(), beforeState.getData(),
                                                   afterState.getSize()) == 0,
                               NI_TXT ("and the plugin's own state is byte for byte where it was"));

                        // Disturbed case: a snapshot that is not what the live plugin
                        // has (exactly the shape of "the export moved its state") is
                        // detected and written back onto the instance.
                        auto moved = snapshotOfLiveState();
                        const auto extra = (juce::uint8) 0x5a;

                        for (auto& track : moved.tracks)
                            if (track.kind == TrackKind::vst
                                && track.vst.pluginPath == params.pluginPath)
                            {
                                auto block = juce::MemoryBlock();
                                juce::MemoryOutputStream stream (block, false);
                                juce::Base64::convertFromBase64 (stream, track.vst.stateBase64);
                                block.append (&extra, 1);   // no longer the live state
                                track.vst.stateBase64 = juce::Base64::toBase64 (block.getData(),
                                                                               block.getSize());
                            }

                        const auto putBack = host.restoreStatesChangedSince (moved);

                        check (putBack == 1
                                   && PluginHostManager::getLiveStateRestoresForTest()
                                          == restoresBefore + 1,
                               NI_TXT ("a live plugin whose state moved gets it put back (")
                                   + juce::String (putBack) + NI_TXT (")"));
                        check (host.restoreStatesChangedSince (snapshotOfLiveState()) == 0,
                               NI_TXT ("and afterwards the guard is quiet again"));

                        // ---- 设备重启不会把插件的声音改掉 --------------------
                        //
                        // The test plugin resets its own gain whenever it is prepared
                        // (the stand-in for the samplers that re-initialise their
                        // instrument when the host re-prepares them). A device restart
                        // with a different rate must therefore be met by the same
                        // guard: the user changed an audio device, not their sound.
                        {
                            guardGain->setValueNotifyingHost (0.75f);
                            pump();

                            // (A real device swap goes through the plugin's own state,
                            // so put the volume there the way the plugin reports it.)
                            juce::MemoryBlock userBlock (sizeof (juce::uint32) + sizeof (float));
                            const auto userMagic = (juce::uint32) 0x484d5631;
                            const auto userGain = 0.75f;

                            userBlock.copyFrom (&userMagic, 0, sizeof (userMagic));
                            userBlock.copyFrom (&userGain, sizeof (userMagic), sizeof (userGain));

                            guardPlugin->getInstance().setStateInformation (userBlock.getData(),
                                                                           (int) userBlock.getSize());
                            pump();

                            const auto stateBeforeRestart = guardPlugin->captureState();
                            const auto restoresBeforeRestart
                                = PluginHostManager::getLiveStateRestoresForTest();

                            // 96 kHz / 2048: a different rate and block, i.e. the plugin
                            // is really re-prepared (and resets itself).
                            host.prepareAll (96000.0, 2048);
                            host.prepareAll (48000.0, 1024);
                            pump();

                            const auto stateAfterRestart = guardPlugin->captureState();

                            check (PluginHostManager::getLiveStateRestoresForTest()
                                       > restoresBeforeRestart,
                                   NI_TXT ("re-preparing the plugin was seen and answered"));
                            check (stateAfterRestart.getSize() == stateBeforeRestart.getSize()
                                       && std::memcmp (stateAfterRestart.getData(),
                                                       stateBeforeRestart.getData(),
                                                       stateAfterRestart.getSize()) == 0,
                                   NI_TXT ("and the plugin's own state is back where the user had it"));
                        }
                    }
                }

                check (loud > 0.01f,
                       NI_TXT ("the exported stem of a VST track has audio (")
                           + juce::String (loud, 4) + NI_TXT (")"));
                check (quiet > 0.001f && std::abs (quiet * 2.0f - loud) < loud * 0.1f,
                       NI_TXT ("the export renders the plugin's live volume (")
                           + juce::String (loud, 4) + NI_TXT (" -> ")
                           + juce::String (quiet, 4) + NI_TXT (")"));

                // Put the volume back: the rest of the run should not have to know
                // that this check turned it down.
                if (gainParameter != nullptr)
                    gainParameter->setValueNotifyingHost (0.5f);

                pump();

                // ---- 打开工程：插件必须按文件里的状态重建 --------------------

                // The plugin's own volume, read the way any host reads it. Looking at
                // HM's stored blob would not do: that is the VST3 wrapper's state, not
                // the instrument's.
                auto pluginParameterValue = [] (const std::shared_ptr<HostedPlugin>& plugin,
                                                const juce::String& name)
                {
                    if (plugin == nullptr)
                        return -1.0f;

                    for (auto* parameter : plugin->getInstance().getParameters())
                        if (parameter != nullptr
                            && parameter->getName (64).containsIgnoreCase (name))
                            return parameter->getValue();

                    return -1.0f;
                };

                auto pluginGainNow = [&liveTrackId, &host, &pluginParameterValue]
                {
                    return pluginParameterValue (host.pluginFor (liveTrackId), "Gain");
                };

                // The parameter object belongs to the instance that is up now: after a
                // project load the old instance is gone, and its parameters with it.
                auto gainParameterForCurrentPlugin = [&liveTrackId, &host]
                    () -> juce::AudioProcessorParameter*
                {
                    const auto plugin = host.pluginFor (liveTrackId);

                    if (plugin == nullptr)
                        return nullptr;

                    for (auto* parameter : plugin->getInstance().getParameters())
                        if (parameter != nullptr
                            && parameter->getName (64).containsIgnoreCase ("Gain"))
                            return parameter;

                    return nullptr;
                };

                juce::String saveError;

                // Project A: the plugin sits where the user left it (0.5).
                const auto fileA = outputDirectory.getChildFile ("selftest-vst-a.nprj");
                document.applyDerivedEdit ([&host] (Project& project)
                {
                    host.captureStates (project);
                });
                check (document.saveToFile (fileA, saveError), saveError);

                // Project B: the same plugin, turned down. Saving B stores B's state.
                if (gainParameter != nullptr)
                    gainParameter->setValueNotifyingHost (0.25f);

                pump();

                const auto fileB = outputDirectory.getChildFile ("selftest-vst-b.nprj");
                document.applyDerivedEdit ([&host] (Project& project)
                {
                    host.captureStates (project);
                });
                check (document.saveToFile (fileB, saveError), saveError);
                checkNear (pluginGainNow(), 0.25, 0.01,
                           NI_TXT ("project B leaves the plugin where it was turned to"));

                // Opening A again has to bring A's sound back. Before this batch the
                // live instance was simply kept, so the track played B's volume while
                // the file said A.
                check (component.loadProjectFile (fileA), saveError);

                for (int attempt = 0; attempt < 300 && host.pluginFor (liveTrackId) == nullptr;
                     ++attempt)
                    pump();

                check (host.pluginFor (liveTrackId) != nullptr,
                       NI_TXT ("opening a project brings the plugin up again"));
                checkNear (pluginGainNow(), 0.5, 0.01,
                           NI_TXT ("and it comes up with the volume the file holds (not the previous project's)"));

                const auto pluginAfterLoad = host.pluginFor (liveTrackId);

                if (pluginAfterLoad != nullptr && gainParameter != nullptr)
                {
                    // ---- 删除 + 撤销：回来的是刚才那个声音 --------------------

                    // The instance was rebuilt by the project load, so the parameter
                    // has to be taken from the plugin that is up now.
                    if (auto* currentGain = gainParameterForCurrentPlugin())
                        currentGain->setValueNotifyingHost (0.75f);

                    pump();

                    const auto beforeDelete = pluginGainNow();
                    checkNear (beforeDelete, 0.75, 0.01, NI_TXT ("the plugin is where the user left it"));

                    component.getTrackArea().deleteTrackNow (liveTrackId);
                    pump();

                    check (host.pluginFor (liveTrackId) == nullptr,
                           NI_TXT ("a deleted track no longer owns its plugin"));

                    document.undo();
                    pump();

                    for (int attempt = 0; attempt < 300 && host.pluginFor (liveTrackId) == nullptr;
                         ++attempt)
                        pump();

                    checkNear (pluginGainNow(), beforeDelete, 0.01,
                               NI_TXT ("undo brings the track back playing what it was playing"));
                }

                // ---- 宿主动插件时从来不碰音量类控制器 ------------------------

                {
                    juce::String midiRenderError;
                    auto midiInstrument = host.makeOfflineInstrument (params, 48000.0,
                                                                      Exporter::kRenderBlockSize,
                                                                      midiRenderError);

                    check (midiInstrument != nullptr,
                           NI_TXT ("a private instance for the MIDI check: ") + midiRenderError);

                    if (midiInstrument != nullptr)
                    {
                        auto midiProject = makeDefaultProject();
                        midiProject.sampleRate = 48000.0;

                        const auto midiTrackId = addTrack (midiProject, TrackKind::vst);

                        if (auto* track = midiProject.findTrack (midiTrackId))
                        {
                            track->vst = params;

                            Note note;
                            note.id = 1;
                            note.durationTicks = kTicksPerQuarter * 2;
                            note.frequencyHz = 660.0;
                            note.velocity = 110;
                            track->clips.front().notes.push_back (note);
                        }

                        const auto midiFile = outputDirectory.getChildFile ("selftest-vst-midi.wav");
                        const auto midiFactory = [&midiInstrument] (juce::int64, const VstParams&)
                        {
                            return midiInstrument;
                        };

                        ExportSettings midiSettings;
                        midiSettings.format = ExportSettings::Format::wav;
                        midiSettings.bitsPerSample = 24;

                        check (Exporter::renderToFile (midiProject, midiFile, midiSettings, {}, {}, {},
                                                       midiRenderError, midiFactory),
                               NI_TXT ("the MIDI check renders through the plugin: ")
                                   + midiRenderError);

                        // The transport's stop / seek path: the same call the engine
                        // makes. A note has to be sounding for it to have anything to
                        // release: HM only speaks to the channels that are holding one.
                        juce::AudioBuffer<float> tail (2, 512);

                        midiInstrument->noteOn (1, 440.0f, 100.0f);
                        midiInstrument->renderAdding (tail, 0, tail.getNumSamples(), 1.0f, 1.0f);
                        midiInstrument->allNotesOff();

                        midiInstrument->renderAdding (tail, 0, tail.getNumSamples(), 1.0f, 1.0f);

                        // The plugin reports what it was sent through one parameter
                        // (see TestInstrumentReport.h): the host can read parameters,
                        // and nothing else a plugin keeps reaches it.
                        const auto receipts = (int) std::lround (
                            pluginParameterValue (std::static_pointer_cast<HostedPlugin> (midiInstrument),
                                                  hmtests::kReceiptsParameterName)
                            * (double) hmtests::kReceiptsMax);

                        check ((receipts & hmtests::kSawNoteOn) != 0
                                   && (receipts & hmtests::kSawPitchBend) != 0,
                               NI_TXT ("notes and pitch bends really reached the plugin (")
                                   + juce::String (receipts) + NI_TXT (")"));
                        check ((receipts & hmtests::kSawCc101) != 0
                                   && (receipts & hmtests::kSawCc100) != 0
                                   && (receipts & hmtests::kSawCc6) != 0
                                   && (receipts & hmtests::kSawCc38) != 0,
                               NI_TXT ("the pitch bend range RPN reached it (")
                                   + juce::String (receipts) + NI_TXT (")"));
                        check ((receipts & hmtests::kSawCc123) != 0,
                               NI_TXT ("and so did the all-notes-off the transport sends"));
                        check ((receipts & (hmtests::kSawCc7 | hmtests::kSawCc10
                                            | hmtests::kSawCc11)) == 0,
                               NI_TXT ("no volume, pan or expression controller ever arrived (")
                                   + juce::String (receipts) + NI_TXT (")"));
                    }
                }

                // ---- 事件日志：以后能拿它当证据 ------------------------------

                {
                    const auto eventsLog = PluginHostManager::getEventsLogFile();

                    check (eventsLog.existsAsFile(), NI_TXT ("the plugin event log is written"));

                    const auto text = eventsLog.existsAsFile() ? eventsLog.loadFileAsString()
                                                               : juce::String();

                    check (text.contains ("create") && text.contains ("state-out"),
                           NI_TXT ("and it records the instance being built and its state being captured"));
                }

                // ---- 一个音一个通道：同时响的音不能互相拉音高 ------------------
                // 用户报的「同时响多个音时只有一条弯音是对的」的回归。每个音要拿到
                // 自己的通道，而通道不能刚被上一个音放开就立刻交给下一个音——插件的
                // 释音还在那个通道上响着，新音的弯音会把旧音的音尾一起拉走。
                runChannelScenes = [&component, &document, &testPlugin, &pump, &check,
                                    &checkNear, &outputDirectory]
                {
                    auto& voiceAudio = component.getAudioEngine();
                    auto& voiceAppState = component.getAppState();

                    auto& host = component.getPluginHost();

                    VstParams params;
                    params.name = "HM Test Instrument";
                    params.pluginPath = testPlugin.getFullPathName();

                    juce::String scanError;
                    host.scanFolderNow (testPlugin.getParentDirectory(), scanError);

                    juce::int64 chordTrackId = 0;

                    auto setUpChords = [&] (int chords, int notesPerChord, double cents,
                                            bool pedalDown)
                    {
                        document.applyEdit ("selftest vst channel spread setup",
                                            [&chordTrackId, &params, chords, notesPerChord,
                                             cents, pedalDown] (Project& project)
                        {
                            project.tracks.clear();
                            project.referenceLines.clear();
                            project.nextNoteId = 1;
                            project.nextClipId = 1;
                            project.loop = {};
                            // A track id the host has never seen: every instance is
                            // per track, and this scene reads that instance's counters.
                            project.nextTrackId = juce::jmax (project.nextTrackId,
                                                              (juce::int64) 100000) + 1;

                            chordTrackId = addTrack (project, TrackKind::vst);

                            if (auto* track = project.findTrack (chordTrackId))
                            {
                                track->vst = params;

                                auto& clip = track->clips.front();
                                clip.notes.clear();
                                clip.lengthTicks = kTicksPerQuarter * juce::jmax (1, chords);

                                for (int chord = 0; chord < chords; ++chord)
                                    for (int i = 0; i < notesPerChord; ++i)
                                    {
                                        Note note;
                                        note.id = project.nextNoteId++;
                                        // Block chords that touch: the next chord starts
                                        // exactly where the previous one ends.
                                        note.startTick = (juce::int64) chord * kTicksPerQuarter;
                                        note.durationTicks = kTicksPerQuarter;
                                        // Deliberately not on a semitone, so every note
                                        // really needs a bend of its own.
                                        note.frequencyHz = 440.0 * std::pow (2.0,
                                                             (double) (i * 5) / 12.0
                                                                 + cents / 1200.0);
                                        note.velocity = 100;
                                        clip.notes.push_back (note);
                                    }

                                if (pedalDown)
                                {
                                    // A pedal pressed before the notes: the channel-level
                                    // state has to reach the channel each note lands on.
                                    ControllerCurve pedal;
                                    pedal.controller = 64;
                                    pedal.points = { { 0, 127 } };
                                    clip.controllerCurves.push_back (pedal);
                                }
                            }
                        });

                        pump();

                        host.requestInstrument (chordTrackId, params);

                        for (int attempt = 0;
                             attempt < 150 && host.pluginFor (chordTrackId) == nullptr; ++attempt)
                            pump();

                        voiceAudio.rebuildSchedule();
                        voiceAudio.stop();
                        voiceAudio.setPositionSeconds (0.0);
                        pump();
                    };

                    auto chordPlugin = [&host, &chordTrackId] { return host.pluginFor (chordTrackId); };

                    auto counterValue = [] (const std::shared_ptr<HostedPlugin>& plugin,
                                            const juce::String& name, double range)
                    {
                        if (plugin == nullptr)
                            return -1;

                        for (auto* parameter : plugin->getInstance().getParameters())
                            if (parameter != nullptr && parameter->getName (64).containsIgnoreCase (name))
                                return (int) std::lround (parameter->getValue() * range);

                        return -1;
                    };

                    auto resetCounters = [&]
                    {
                        if (auto plugin = chordPlugin())
                            for (auto* parameter : plugin->getInstance().getParameters())
                                if (parameter != nullptr
                                    && parameter->getName (64)
                                           .containsIgnoreCase (hmtests::kResetCountersParameterName))
                                    parameter->setValueNotifyingHost (1.0f);
                    };

                    auto conflicts = [&] { return counterValue (chordPlugin(), hmtests::kConflictsParameterName,
                                                                (double) hmtests::kReceiptsMax); };
                    auto channelsUsed = [&] { return counterValue (chordPlugin(), hmtests::kChannelsUsedParameterName,
                                                                  16.0); };
                    auto pedalChannels = [&] { return counterValue (chordPlugin(), hmtests::kPedalChannelsParameterName,
                                                                   16.0); };

                    auto renderBlocks = [&voiceAudio] (int blocks)
                    {
                        for (int i = 0; i < blocks; ++i)
                            voiceAudio.renderBlockForTest (512);
                    };

                    // ---- ① 工程和弦 + 同时按一个会撞号的 MIDI 键 ---------------
                    setUpChords (1, 3, 20.0, false);

                    // Nothing from earlier scenes may still be queued in the live
                    // FIFO: this scene counts the plugin's channels exactly.
                    voiceAudio.resetLiveStateForTest();

                    // …and nothing from earlier scenes may still be *held* in the
                    // keyboard bookkeeping (a held key would make the note-on below
                    // a no-op, leaving the plugin with the chord's 3 channels).
                    component.releaseAllLiveNotes();
                    pump();

                    check (chordPlugin() != nullptr, NI_TXT ("和弦场景里插件也起来了"));

                    resetCounters();

                    voiceAppState.selectTrack (chordTrackId);
                    document.applyDerivedEdit ([] (Project& project)
                    {
                        project.midiInput.map = {};
                        project.midiInput.map.addFrequency (60, 523.25);
                    });
                    pump();

                    voiceAudio.play();
                    renderBlocks (8);
                    pump();

                    // The MIDI keyboard's first note is numbered 1 - the same number the
                    // schedule gives its own first event. That used to arrive as one note
                    // (one channel, one bend), which pulled the project's note around.
                    component.simulateMidiDeviceMessageForTest (
                        juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100));
                    // The manager posts its delivery (a real device arrives on the
                    // message thread), so the note only reaches the engine after a
                    // pump - render before pumping and the note is still in the air.
                    pump();
                    renderBlocks (8);

                    check (conflicts() == 0,
                           NI_TXT ("工程和弦与实时音同时响，插件没有收到「一个通道两个音高」（")
                               + juce::String (conflicts()) + NI_TXT (" 次）"));
                    check (channelsUsed() == 4,
                           NI_TXT ("4 个音落在 4 个通道上（实际 ")
                               + juce::String (channelsUsed()) + NI_TXT (" 个）"));

                    component.simulateMidiDeviceMessageForTest (juce::MidiMessage::noteOff (1, 60));
                    pump();
                    voiceAudio.stop();
                    pump();

                    // ---- ② 连续的和弦：通道要铺开，不能刚放开就还给下一个音 ---
                    setUpChords (4, 4, 20.0, false);

                    // The first block after a schedule change is when the engine
                    // puts the new track's instrument into its slot and re-prepares
                    // it. Do that *before* resetting the counters: a prepare that
                    // lands later clears the plugin's "channels seen" underneath the
                    // scene, which is how this assertion once read 12 instead of 16.
                    voiceAudio.renderBlockForTest (512);
                    pump();

                    resetCounters();

                    voiceAudio.play();
                    renderBlocks (200);      // 4 beats at 120 bpm = 2 s
                    voiceAudio.stop();
                    pump();

                    check (channelsUsed() == vst::kMidiChannels,
                           NI_TXT ("16 个音用满了 16 个通道，没有把刚放开的那个立刻还给下一个音（实际 ")
                               + juce::String (channelsUsed()) + NI_TXT (" 个）"));
                    check (conflicts() == 0,
                           NI_TXT ("连弹四个和弦也没有一个通道被两个音高共用（")
                               + juce::String (conflicts()) + NI_TXT (" 次）"));

                    // ---- ③ 第 17 个音：抢通道要先把最老的音正常收掉 -------------
                    setUpChords (1, 17, 20.0, false);

                    // Same settle as ②: the instance has to be in its slot and
                    // prepared before the counters are cleared.
                    voiceAudio.renderBlockForTest (512);
                    pump();

                    resetCounters();

                    voiceAudio.play();
                    renderBlocks (8);
                    voiceAudio.stop();
                    pump();

                    const auto stolen = chordPlugin() != nullptr
                                            ? chordPlugin()->evictedNoteCountForTest() : -1;

                    check (stolen >= 1,
                           NI_TXT ("17 个音时最老的那个被收掉腾出通道（")
                               + juce::String (stolen) + NI_TXT (" 次）"));
                    check (conflicts() == 0,
                           NI_TXT ("被收掉的音不会继续占着通道被新音拉走（")
                               + juce::String (conflicts()) + NI_TXT (" 次）"));

                    // ---- ④ 踏板：通道级的 CC 要跟着音走 ------------------------
                    setUpChords (1, 3, 20.0, true);
                    resetCounters();

                    voiceAudio.play();
                    renderBlocks (8);
                    voiceAudio.stop();
                    pump();

                    check (pedalChannels() >= 3,
                           NI_TXT ("踏板落到了每个有音的通道上（")
                               + juce::String (pedalChannels()) + NI_TXT (" 个）"));

                    // ---- ⑤ 按半音演奏：整机弯音的插件也有退路 -------------------
                    // 有些乐器（实测 Serum 2 就是）把 pitch bend 当成**整个音色**的：
                    // 一条弯音把所有正在响的音一起拉走。这种插件做不了"每个音自己的
                    // 音分"，所以那条 VST 轨可以改成按半音演奏（完全不发弯音）。
                    {
                        const auto menuItems = component.getTrackMenuItemsForTest (chordTrackId);

                        check (menuItems.size() == 1
                                   && menuItems.front().text.contains (NI_TXT ("按半音")),
                               NI_TXT ("VST 轨的行头右键菜单里有「按半音演奏」（「")
                                   + (menuItems.empty() ? juce::String() : menuItems.front().text)
                                   + NI_TXT ("」）"));
                        check (! menuItems.empty() && ! menuItems.front().ticked,
                               NI_TXT ("默认还是精确频率（不打勾）"));

                        component.performTrackMenuCommandForTest (chordTrackId, 1);
                        pump();

                        check (document.getProject().findTrack (chordTrackId) != nullptr
                                   && document.getProject().findTrack (chordTrackId)->vst.equalTemperament,
                               NI_TXT ("勾上之后这一轨改成按半音演奏"));

                        component.performTrackMenuCommandForTest (chordTrackId, 1);
                        pump();

                        check (! document.getProject().findTrack (chordTrackId)->vst.equalTemperament,
                               NI_TXT ("再点一下切回精确频率"));

                        // Now the pitch it really plays at: 660 Hz (a 1:3) is 1.9 cents
                        // above E5, so the exact mode gives 660 Hz and the semitone mode
                        // gives 659.26 Hz - and the semitone mode needs no bend at all,
                        // which is what makes a whole-patch-bend plugin usable.
                        auto measureHz = [] (const juce::File& file) -> double
                        {
                            juce::AudioFormatManager manager;
                            manager.registerBasicFormats();
                            std::unique_ptr<juce::AudioFormatReader> reader (manager.createReaderFor (file));

                            if (reader == nullptr)
                                return 0.0;

                            const auto start = (juce::int64) (reader->sampleRate * 0.20);
                            const auto count = juce::jmin ((juce::int64) (reader->sampleRate * 0.5),
                                                           reader->lengthInSamples - start);

                            if (count < 100)
                                return 0.0;

                            juce::AudioBuffer<float> rendered (1, (int) count);
                            reader->read (&rendered, 0, (int) count, start, true, false);

                            const auto* data = rendered.getReadPointer (0);
                            double first = -1.0, last = -1.0;
                            auto crossings = 0;

                            for (int i = 1; i < (int) count; ++i)
                                if (data[i - 1] <= 0.0f && data[i] > 0.0f)
                                {
                                    const auto previous = (double) data[i - 1];
                                    const auto current = (double) data[i];
                                    const auto fraction = previous == current
                                                              ? 0.0 : (-previous) / (current - previous);
                                    const auto time = ((double) (i - 1) + fraction) / reader->sampleRate;

                                    if (first < 0.0)
                                        first = time;

                                    last = time;
                                    ++crossings;
                                }

                            return crossings > 1 && last > first
                                       ? (double) (crossings - 1) / (last - first) : 0.0;
                        };

                        auto renderTuning = [&] (bool semitoneMode, const juce::String& fileName) -> double
                        {
                            auto tuningProject = makeDefaultProject();
                            tuningProject.sampleRate = 48000.0;

                            const auto tuningTrackId = addTrack (tuningProject, TrackKind::vst);
                            auto* tuningTrack = tuningProject.findTrack (tuningTrackId);
                            tuningTrack->vst = params;
                            tuningTrack->vst.equalTemperament = semitoneMode;
                            tuningTrack->clips.front().notes.clear();

                            Note tuningNote;
                            tuningNote.id = tuningProject.nextNoteId++;
                            tuningNote.durationTicks = kTicksPerQuarter * 2;
                            tuningNote.frequencyHz = 660.0;
                            tuningNote.velocity = 110;
                            tuningTrack->clips.front().notes.push_back (tuningNote);

                            const auto file = outputDirectory.getChildFile (fileName);
                            juce::String renderError;
                            auto tuningInstrument = host.makeOfflineInstrument (params, 48000.0,
                                                                                Exporter::kRenderBlockSize,
                                                                                renderError);

                            if (tuningInstrument == nullptr)
                                return 0.0;

                            ExportSettings settings;
                            settings.format = ExportSettings::Format::wav;
                            settings.bitsPerSample = 24;

                            const auto factory = [&tuningInstrument] (juce::int64, const VstParams&)
                            {
                                return tuningInstrument;
                            };

                            if (! Exporter::renderToFile (tuningProject, file, settings, {}, {}, {},
                                                          renderError, factory))
                                return 0.0;

                            return measureHz (file);
                        };

                        const auto exactPitch = renderTuning (false, "selftest-vst-exact.wav");
                        const auto semitonePitch = renderTuning (true, "selftest-vst-semitone.wav");

                        checkNear (exactPitch, 660.0, 1.0,
                                   NI_TXT ("精确模式：660 Hz 就是 660 Hz（实测 ")
                                       + juce::String (exactPitch, 3) + NI_TXT (" Hz）"));
                        checkNear (semitonePitch, vst::hzForMidiNote (76), 1.0,
                                   NI_TXT ("按半音演奏：落在最近的半音上（E5 = 659.26，实测 ")
                                       + juce::String (semitonePitch, 3) + NI_TXT (" Hz）"));
                    }
                };
            }
        }
        else
        {
            trace ("no --vst3 argument: the real plugin checks were skipped");
        }

        // ---- 插件扫描在子进程里跑：坏插件只会被记成读取失败 --------------------

        beginTest (NI_TXT ("插件扫描在子进程里跑：坏插件只会被记成读取失败"));

        {
            auto& host = component.getPluginHost();

            // A file that claims to be a VST3 and is not one: reading it is exactly
            // what used to be able to take the whole app down with it.
            const auto badFolder = outputDirectory.getChildFile ("selftest-scan-bad");
            badFolder.createDirectory();
            badFolder.getChildFile ("Broken.vst3").replaceWithText ("this is not a plugin");

            juce::String scanError;
            auto failed = 0;

            // Reaching the next line at all is the point: the loading happened in a
            // child, so a file it cannot read is a number, not a crash.
            const auto found = host.scanFolderNow (badFolder, scanError, &failed);

            check (found == 0 && failed == 1 && scanError.isEmpty(),
                   NI_TXT ("a bundle that cannot be read is one failed file (")
                       + juce::String (found) + NI_TXT (" found / ") + juce::String (failed)
                       + NI_TXT (" failed)"));

            // A child that dies the way a crashing plugin would: the test switch makes
            // it exit without writing a result.
            host.setScanChildArgumentsForTest ({ scanproto::kFailSwitch });

            failed = 0;
            const auto foundAfterDeath = host.scanFolderNow (badFolder, scanError, &failed);

            host.setScanChildArgumentsForTest ({});

            check (foundAfterDeath == 0 && failed == 1,
                   NI_TXT ("a child that dies is one failed file, and the scan carries on (")
                       + juce::String (failed) + NI_TXT (" failed)"));

            // A child that never comes back: the parent has to time it out and kill
            // it, or one bad plugin would stop the whole scan.
            host.setScanChildArgumentsForTest ({ scanproto::kHangSwitch });
            host.setScanTimeoutForTest (1200);

            failed = 0;
            const auto startMs = juce::Time::getMillisecondCounter();
            const auto foundAfterHang = host.scanFolderNow (badFolder, scanError, &failed);
            const auto elapsedMs = juce::Time::getMillisecondCounter() - startMs;

            host.setScanChildArgumentsForTest ({});
            host.setScanTimeoutForTest (scanproto::kDefaultTimeoutMs);

            check (foundAfterHang == 0 && failed == 1 && elapsedMs < 10000,
                   NI_TXT ("a child that hangs is killed and counted (")
                       + juce::String (failed) + NI_TXT (" failed in ")
                       + juce::String (elapsedMs) + NI_TXT (" ms)"));
        }

        // ---- 导出时缺插件：说清楚，而不是写一个没有声音的文件 ------------------

        beginTest (NI_TXT ("导出的轨道缺插件时说明白，不写没有声音的文件"));

        {
            const auto missingPath = outputDirectory.getChildFile ("gone.vst3").getFullPathName();

            document.applyEdit ("selftest export missing plugin", [&missingPath] (Project& project)
            {
                project.tracks.clear();
                project.referenceLines.clear();
                project.nextTrackId = 1;

                const auto id = addTrack (project, TrackKind::vst);

                if (auto* track = project.findTrack (id))
                {
                    track->name = NI_TXT ("丢失乐器的轨道");
                    track->vst.name = "No Such Instrument";
                    track->vst.pluginPath = missingPath;
                    track->vst.pluginUid = "4242";

                    Note note;
                    note.id = project.nextNoteId++;
                    note.durationTicks = kTicksPerQuarter * 2;
                    note.frequencyHz = 440.0;
                    note.velocity = 100;
                    track->clips.front().notes = { note };
                }
            });
            pump();

            const auto missingTrackId = document.getProject().tracks.front().id;
            const auto trackName = document.getProject().tracks.front().name;
            const auto folder = outputDirectory.getChildFile ("selftest-export-missing");

            ExportSettings settings;
            settings.format = ExportSettings::Format::wav;
            settings.bitsPerSample = 24;
            settings.master = false;
            settings.trackIds = { missingTrackId };

            std::vector<juce::File> written;
            juce::String missingError;
            const auto ran = component.runExport (settings, folder, true, missingError, &written,
                                                  nullptr);

            check (! ran,
                   NI_TXT ("an export of a track whose plugin is gone is refused"));
            check (missingError.contains (trackName),
                   NI_TXT ("and the message names the track that would have been silent (got '")
                       + missingError + "')");
            check (written.empty(), NI_TXT ("so no silent file is written for it"));

            // The same track silenced by mute is a different story: the user is not
            // asking for its sound, so its missing plugin is not in the way.
            document.applyEdit ("selftest mute the missing plugin track",
                                [missingTrackId] (Project& project)
            {
                if (auto* track = project.findTrack (missingTrackId))
                    track->muted = true;
            });
            pump();

            juce::String mutedError;
            const auto ranMuted = component.runExport (settings, folder.getChildFile ("muted"), true,
                                                       mutedError, &written, nullptr);

            check (! ranMuted && ! mutedError.contains (trackName),
                   NI_TXT ("a muted track is reported as nothing to write, not as a missing plugin (got '")
                       + mutedError + "')");
        }

        // ---- 效果器栏：每条轨道一条 VST3 插入链 -------------------------------

        beginTest (NI_TXT ("每条轨道一条效果器链（VST3 插入）"));

        {
            auto& panel = component.getEffectsBar();
            auto& host = component.getPluginHost();

            // One synth track with a note that is easy to measure.
            document.applyEdit ("selftest effects setup", [] (Project& project)
            {
                project.tracks.clear();
                project.referenceLines.clear();
                project.nextTrackId = 1;

                const auto id = addTrack (project, TrackKind::synth);
                auto* track = project.findTrack (id);
                track->clips.front().notes.clear();

                Note note;
                note.id = project.nextNoteId++;
                note.startTick = 0;
                note.durationTicks = kTicksPerQuarter * 2;
                note.frequencyHz = 440.0;
                note.velocity = 110;
                track->clips.front().notes.push_back (note);
            });
            pump();

            const auto sectionTrackId = document.getProject().tracks.front().id;
            appState.selectTrack (sectionTrackId);
            appState.selectClip (document.getProject().tracks.front().clips.front().id);
            appState.setEditorOpen (true);
            pump();

            // The chain opens from the lane header's 效果 button now: the column it
            // used to live in is gone, so this is the path a user takes.
            component.openEffectsWindow (sectionTrackId);
            pump();

            check (panel.getHeaderTextForTest().startsWith (NI_TXT ("效果器")),
                   NI_TXT ("the effects bar names itself (got '")
                       + panel.getHeaderTextForTest() + "')");
            check (panel.getRowCountForTest() == 0,
                   NI_TXT ("a fresh track has no inserts"));
            check (! panel.getAddButtonBoundsForTest().isEmpty(),
                   NI_TXT ("the 添加效果器 button has a real size"));
            check (panel.getAddButtonBoundsForTest().getWidth() > 0,
                   NI_TXT ("and it is inside the bar"));

            // The chain lives in the project: adding one is a normal edit.
            VstParams dummy;   // only to build a plausible PluginDescription below
            juce::ignoreUnused (dummy);

            juce::PluginDescription fakeEffect;
            fakeEffect.name = NI_TXT ("假效果器");
            fakeEffect.manufacturerName = "HM";
            fakeEffect.isInstrument = false;
            fakeEffect.category = "Fx|Reverb";
            fakeEffect.fileOrIdentifier = outputDirectory.getChildFile ("missing-fx.vst3")
                                              .getFullPathName();
            fakeEffect.uniqueId = 4242;

            panel.addEffect (fakeEffect);
            pump();

            check (panel.getRowCountForTest() == 1,
                   NI_TXT ("adding an effect puts a row in the bar"));
            check (document.getProject().tracks.front().effects.size() == 1,
                   NI_TXT ("and a slot in the project"));
            check (panel.getRowBoundsForTest (0).getWidth() > 0,
                   NI_TXT ("the row has a real size"));
            check (panel.getRowTextForTest (0).contains (NI_TXT ("假效果器")),
                   NI_TXT ("the row names the effect (got '")
                       + panel.getRowTextForTest (0) + "')");
            check (panel.getRowTextForTest (0).contains (NI_TXT ("插件缺失")),
                   NI_TXT ("a plugin that is not there is called out"));

            // Bypass, order and removal are all ordinary edits.
            panel.pressBypassForTest (0);
            pump();

            check (panel.isBypassOnForTest (0)
                       && document.getProject().tracks.front().effects.front().bypass,
                   NI_TXT ("旁路 switches on and is stored in the project"));

            panel.pressBypassForTest (0);
            pump();

            check (! document.getProject().tracks.front().effects.front().bypass,
                   NI_TXT ("and off again"));

            panel.pressDeleteForTest (0);
            pump();

            check (panel.getRowCountForTest() == 0
                       && document.getProject().tracks.front().effects.empty(),
                   NI_TXT ("✕ removes the slot"));

            // The snapshot is the window the chain now lives in (the column it used
            // to sit in is gone), so what it shows is what the user sees.
            if (auto* effectsWindow = component.getEffectsWindowForTest(); effectsWindow != nullptr)
                check (writeImage (snapshot (*effectsWindow),
                                   outputDirectory.getChildFile ("38-effects-bar.png")),
                       NI_TXT ("effects bar snapshot (the window)"));
            else
                check (false, NI_TXT ("有没有效果器窗口？"));

            // The picker in effect mode only offers effects.
            {
                PluginPicker picker (host, NI_TXT ("选择效果器"), PluginPicker::Kind::effect);
                picker.setSize (600, 520);
                picker.resized();

                juce::Array<juce::PluginDescription> listed;
                juce::PluginDescription instrument;
                instrument.name = "An Instrument";
                instrument.isInstrument = true;
                instrument.category = "Instrument|Synth";
                juce::PluginDescription effect;
                effect.name = NI_TXT ("一个效果器");
                effect.manufacturerName = "HM";
                effect.isInstrument = false;
                effect.category = "Fx|Delay";
                listed.add (instrument);
                listed.add (effect);

                picker.setPluginsForTest (listed);

                check (picker.getVisibleCountForTest() == 1,
                       NI_TXT ("the effect picker hides instruments (got ")
                           + juce::String (picker.getVisibleCountForTest()) + ")");

                // Grouping is remembered between runs, so ask for the type axis
                // rather than assuming which one is current.
                picker.setGroupingForTest (PluginGroupAxis::type);

                check (picker.getGroupTextForTest (0) == NI_TXT ("延迟 (1)"),
                       NI_TXT ("and groups what is left by its type (got '")
                           + picker.getGroupTextForTest (0) + "')");

                // The same widget-level check the instrument picker gets: the row has
                // to be in the TreeView and painted, not only in the picker's data.
                check (picker.getTreeVisibleRowCountForTest() == 1
                           && picker.getTreeItemCountForTest() == 2,
                       NI_TXT ("the effect row is really in the widget (got ")
                           + juce::String (picker.getTreeItemCountForTest()) + " items, "
                           + juce::String (picker.getTreeVisibleRowCountForTest())
                           + NI_TXT (" shown)"));

                {
                    const auto painted = countNotColour (snapshot (picker),
                                                         picker.getTreeBoundsForTest(),
                                                         theme::gridBackground);

                    check (painted > 200,
                           NI_TXT ("and it is painted (pixels = ") + juce::String (painted) + ")");
                }

                juce::String chosen;
                picker.onChosen = [&chosen] (const juce::PluginDescription& description)
                {
                    chosen = description.name;
                };
                picker.chooseForTest (0, 0);

                check (chosen == NI_TXT ("一个效果器"),
                       NI_TXT ("choosing an effect reports it"));

                check (writeImage (snapshot (picker),
                                   outputDirectory.getChildFile ("39-effects-picker.png")),
                       NI_TXT ("effect picker snapshot"));
            }

            // Loading a real effect and hearing it work needs the test plugin.
            if (testEffect.exists())
            {
                EffectSlot slot;
                slot.kind = 0;
                slot.name = "HM Test Effect";
                slot.pluginPath = testEffect.getFullPathName();

                document.applyEdit ("selftest insert effect", [&slot] (Project& project)
                {
                    if (! project.tracks.empty())
                        project.tracks.front().effects.push_back (slot);
                });
                pump();

                // The host loads it in the background; give it a few pumps.
                for (int attempt = 0; attempt < 150
                                      && host.effectPluginFor (sectionTrackId, 0) == nullptr; ++attempt)
                    pump();

                auto loaded = host.effectPluginFor (sectionTrackId, 0);

                check (loaded != nullptr, NI_TXT ("the test effect loads into a slot"));

                if (loaded != nullptr)
                {
                    check (panel.getRowTextForTest (0).contains ("HM Test Effect"),
                           NI_TXT ("the row shows the loaded plugin's name (got '")
                               + panel.getRowTextForTest (0) + "')");

                    // Render a second of the project with the effect in the chain.
                    const auto withEffect = outputDirectory.getChildFile ("selftest-fx-on.wav");
                    const auto withoutEffect = outputDirectory.getChildFile ("selftest-fx-off.wav");

                    ExportSettings settings;
                    settings.format = ExportSettings::Format::wav;
                    settings.bitsPerSample = 24;

                    const auto effectFactory = [&host, &testEffect]
                        (juce::int64, int, const EffectSlot& s)
                            -> std::shared_ptr<HostedEffect>
                    {
                        juce::String error;
                        return host.makeOfflineEffect (s, 48000.0, Exporter::kRenderBlockSize, error);
                    };

                    juce::String sectionError;

                    check (Exporter::renderToFile (document.getProject(), withEffect, settings,
                                                   {}, {}, {}, sectionError, {}, effectFactory),
                           NI_TXT ("exporting with the insert chain runs: ") + sectionError);

                    // The same project with the slot bypassed: the chain must let the
                    // audio through untouched.
                    auto bypassed = document.getProject();
                    bypassed.tracks.front().effects.front().bypass = true;

                    check (Exporter::renderToFile (bypassed, withoutEffect, settings,
                                                   {}, {}, {}, sectionError, {}, effectFactory),
                           NI_TXT ("and with it bypassed: ") + sectionError);

                    juce::WavAudioFormat sectionWavFormat;
                    auto levelOf = [&sectionWavFormat] (const juce::File& file)
                    {
                        std::unique_ptr<juce::AudioFormatReader> reader (
                            sectionWavFormat.createReaderFor (new juce::FileInputStream (file), true));

                        if (reader == nullptr)
                            return 0.0f;

                        const auto samples = (int) juce::jmin ((juce::int64) (reader->sampleRate * 0.5),
                                                               reader->lengthInSamples);
                        juce::AudioBuffer<float> buffer (1, samples);
                        reader->read (&buffer, 0, samples, 0, true, false);

                        return buffer.getRMSLevel (0, samples / 10, samples - samples / 10);
                    };

                    const auto loud = levelOf (withoutEffect);
                    const auto quiet = levelOf (withEffect);

                    check (loud > 0.01f, NI_TXT ("the bypassed render has audio (")
                                            + juce::String (loud, 4) + ")");
                    check (std::abs (quiet * 2.0f - loud) < loud * 0.1f,
                           NI_TXT ("the effect really halved it (")
                               + juce::String (loud, 4) + NI_TXT (" -> ")
                               + juce::String (quiet, 4) + ")");

                    // Its state travels with the project like an instrument's.
                    const auto state = loaded->captureState();

                    check (state.getSize() > 0, NI_TXT ("the effect hands out a state chunk"));

                    document.applyDerivedEdit ([&host] (Project& project)
                    {
                        host.captureEffectStates (project);
                    });

                    const auto json = projectToJson (document.getProject());
                    Project sectionProject;
                    juce::String jsonError;

                    check (projectFromJson (json, sectionProject, jsonError),
                           NI_TXT ("a project with an insert chain round trips: ") + jsonError);
                    check (! sectionProject.tracks.empty() && sectionProject.tracks.front().effects.size() == 1,
                           NI_TXT ("the chain is in the project file"));
                    check (! sectionProject.tracks.empty()
                               && ! sectionProject.tracks.front().effects.front().stateBase64.isEmpty(),
                           NI_TXT ("with the effect's state in it"));

                    // Bypass is a project setting, not a plugin one.
                    auto& bar = component.getEffectsBar();
                    component.openEffectsWindow (sectionTrackId);
                    pump();
                    bar.pressBypassForTest (0);
                    pump();

                    check (document.getProject().tracks.front().effects.front().bypass
                               && loaded->isBypassed(),
                           NI_TXT ("旁路 reaches the live instance"));

                    bar.pressBypassForTest (0);
                    pump();
                }

                // Two slots, so the order buttons have something to do.
                document.applyEdit ("selftest second effect", [&slot] (Project& project)
                {
                    if (! project.tracks.empty())
                    {
                        auto second = slot;
                        second.name = "Second Effect";
                        project.tracks.front().effects.push_back (second);
                    }
                });
                pump();

                check (panel.getRowCountForTest() == 2,
                       NI_TXT ("two slots make two rows"));

                panel.pressUpForTest (1);
                pump();

                check (document.getProject().tracks.front().effects.front().name == "Second Effect",
                       NI_TXT ("↑ moves a slot towards the front of the chain (got '")
                           + document.getProject().tracks.front().effects.front().name + "')");

                panel.pressDownForTest (0);
                pump();

                check (document.getProject().tracks.front().effects.back().name == "Second Effect",
                       NI_TXT ("and ↓ moves it back"));

                // Clean up: the rest of the run expects a plain synth track.
                document.applyEdit ("selftest clear effects", [] (Project& project)
                {
                    for (auto& track : project.tracks)
                        track.effects.clear();
                });
                pump();
            }
            else
            {
                trace ("no --vst3fx argument: the real effect checks were skipped");
            }
        }

        // ---- 窗口都开在屏幕里（小屏 / 缩放时按钮不能跑到屏幕外） ----------------

        beginTest (NI_TXT ("每个窗口都开在屏幕里"));

        {
            const auto screen = placement::displayAreaFor (nullptr);
            const auto room = placement::usableArea (screen);

            check (screen.getWidth() > 0 && screen.getHeight() > 0,
                   NI_TXT ("the run has a real screen (")
                       + screen.toString() + NI_TXT (")"));

            // The main window: the design size on this screen, or the whole work
            // area when the screen is smaller. Either way its own bottom bar (the
            // transport, 音频设置) stays on it.
            const auto main = placement::placeWindow ({ placement::kMainWindowWidth,
                                                        placement::kMainWindowHeight },
                                                      screen, 0);

            check (screen.contains (main),
                   NI_TXT ("the main window would open inside the screen (")
                       + main.toString() + NI_TXT (")"));
            check (main.getHeight() <= screen.getHeight()
                       && main.getWidth() <= screen.getWidth(),
                   NI_TXT ("and never bigger than it (")
                       + juce::String (main.getWidth()) + "x" + juce::String (main.getHeight())
                       + NI_TXT (" for ") + screen.toString() + NI_TXT (")"));

            // The minimum it may be resized to also has to fit the screen.
            const auto minimum = placement::fitMinimum ({ placement::kMainWindowMinimumWidth,
                                                          placement::kMainWindowMinimumHeight },
                                                        screen, 0);

            check (minimum.x <= screen.getWidth() && minimum.y <= screen.getHeight(),
                   NI_TXT ("the resize minimum fits the screen (")
                       + juce::String (minimum.x) + "x" + juce::String (minimum.y) + NI_TXT (")"));

            // The screen this bug was reported from: 1366x768 with the taskbar.
            const juce::Rectangle<int> laptop { 0, 0, 1366, 728 };
            const auto onLaptop = placement::placeWindow ({ placement::kMainWindowWidth,
                                                           placement::kMainWindowHeight },
                                                          laptop, 0);

            check (laptop.contains (onLaptop),
                   NI_TXT ("a 1366x768 screen gets a window that fits (")
                       + onLaptop.toString() + NI_TXT (")"));
            check (onLaptop.getHeight() == laptop.getHeight()
                       && onLaptop.getWidth() == laptop.getWidth(),
                   NI_TXT ("using the whole work area when the design size is too big"));

            // 150% Windows scaling on a 1920x1080 screen: 1280x672 of work area.
            const juce::Rectangle<int> scaled { 0, 0, 1280, 672 };
            const auto onScaled = placement::placeWindow ({ placement::kMainWindowWidth,
                                                           placement::kMainWindowHeight },
                                                          scaled, 0);
            const auto scaledMinimum = placement::fitMinimum ({ placement::kMainWindowMinimumWidth,
                                                               placement::kMainWindowMinimumHeight },
                                                              scaled, 0);

            check (scaled.contains (onScaled) && scaledMinimum.y <= scaled.getHeight(),
                   NI_TXT ("a scaled display gets a fitted window and a minimum it can keep (")
                       + onScaled.toString() + NI_TXT (" / ")
                       + juce::String (scaledMinimum.x) + "x"
                       + juce::String (scaledMinimum.y) + NI_TXT (")"));

            // The sound editor: its panel keeps the size it is designed for, and the
            // window scrolls when the screen cannot give that much.
            SynthWindow window (document, component.getAudioEngine(),
                                component.getPresetStore(), false);
            pump();

            check (screen.contains (window.getBounds()),
                   NI_TXT ("the sound window opens inside the screen (")
                       + window.getBounds().toString() + NI_TXT (")"));

            const auto panel = window.getPanelSizeForTest();

            check (panel.x >= SynthWindow::kDesignWidth && panel.y >= SynthWindow::kDesignHeight,
                   NI_TXT ("and its panel keeps the size it is designed for (")
                       + juce::String (panel.x) + "x" + juce::String (panel.y) + NI_TXT (")"));

            // Pretend the display is much smaller than the panel (a scaled laptop):
            // the window shrinks and the panel stays whole, with scrollbars.
            window.setSize (520, 380);
            pump();

            const auto smallPanel = window.getPanelSizeForTest();

            check (smallPanel.x >= SynthWindow::kDesignWidth
                       && smallPanel.y >= SynthWindow::kDesignHeight,
                   NI_TXT ("a window smaller than the panel does not squeeze it (")
                       + juce::String (smallPanel.x) + "x" + juce::String (smallPanel.y)
                       + NI_TXT (")"));
            check (window.isPanelScrollableForTest(),
                   NI_TXT ("the window scrolls to the rest instead of losing it"));

            // The plugin picker: the same rule, because its buttons sit at the bottom
            // of the window. It asks for 600x520 and never gets more than the screen.
            {
                PluginPicker picker (component.getPluginHost(), NI_TXT ("选择效果器"),
                                     PluginPicker::Kind::effect);
                const auto wantedPicker = juce::Point<int> (picker.getWidth(), picker.getHeight());
                const auto fitted = placement::placeWindow (wantedPicker, screen);

                check (fitted.getWidth() == juce::jmin (wantedPicker.x, room.getWidth())
                           && fitted.getHeight() == juce::jmin (wantedPicker.y, room.getHeight()),
                       NI_TXT ("the picker gets its 600x520, or all the work area (")
                           + fitted.toString() + NI_TXT (" for a screen of ")
                           + screen.toString() + NI_TXT (")"));
                check (screen.contains (fitted),
                       NI_TXT ("and its 确定 / 取消 buttons stay on the screen (")
                           + fitted.toString() + NI_TXT (")"));
            }
        }

        // ---- 编辑栏的两种模式：参考线模式 / 钢琴窗模式 -------------------------

        beginTest (NI_TXT ("编辑栏有参考线模式与钢琴窗模式两种选择"));

        {
            auto& panel = component.getEditorPanel();
            auto& sectionGrid = panel.getGrid();
            auto& sectionLanes = component.getTrackArea();

            // A clean scene: one synth track, one clip, a couple of notes.
            document.applyEdit ("selftest piano setup", [] (Project& project)
            {
                project.tracks.clear();
                project.referenceLines.clear();
                project.nextTrackId = 1;

                const auto id = addTrack (project, TrackKind::synth);
                auto* track = project.findTrack (id);
                track->clips.front().notes.clear();
            });
            pump();

            const auto& setupTrack = document.getProject().tracks.front();
            appState.selectTrack (setupTrack.id);
            appState.selectClip (setupTrack.clips.front().id);
            appState.setEditorOpen (true);
            pump();

            check (panel.getEditorModeForTest() == 1,
                   NI_TXT ("the editor starts in 参考线模式"));
            check (! sectionGrid.isPianoMode(),
                   NI_TXT ("so the canvas is the frequency axis"));

            panel.selectEditorModeForTest (2);
            pump();

            check (panel.getEditorModeForTest() == 2,
                   NI_TXT ("钢琴窗模式 can be chosen"));
            check (document.viewState().editorMode == EditorMode::piano,
                   NI_TXT ("the choice is remembered in the view state"));
            check (sectionGrid.isPianoMode(), NI_TXT ("the canvas is a piano roll now"));
            check (sectionGrid.getPianoRowCountForTest() > 0,
                   NI_TXT ("the piano roll draws semitone rows (")
                       + juce::String (sectionGrid.getPianoRowCountForTest()) + NI_TXT (")"));

            const auto keys = sectionGrid.getPianoKeysBoundsForTest();

            check (keys.getWidth() > 0 && keys.getHeight() > 40,
                   NI_TXT ("the keyboard strip is on screen"));
            check (! panel.getModeBoxBoundsForTest().isEmpty(),
                   NI_TXT ("the mode box has a real size"));
            check (panel.isPianoLinesButtonVisibleForTest(),
                   NI_TXT ("the 参考线显示 toggle appears in the piano window"));
            check (! panel.getPianoLinesButtonBoundsForTest().isEmpty(),
                   NI_TXT ("and it has a real size too"));

            check (writeImage (snapshot (component),
                               outputDirectory.getChildFile ("34-piano-roll.png")),
                   NI_TXT ("piano roll snapshot"));

            // Writing on a row: the note must land on that semitone, not on the
            // exact frequency under the pointer.
            const auto countNotes = [&document] ()
            {
                const auto& project = document.getProject();
                return project.tracks.empty() ? 0
                                              : (int) project.tracks.front().clips.front().notes.size();
            };

            const auto notesBeforeWrite = countNotes();
            const auto writeRow = 64;   // E4
            // Press 45% of the way down the row: the frequency under the pointer is
            // between two semitones, so only quantization can explain the result.
            const auto writeStart = sectionGrid.pointForSemitoneForTest (writeRow, 1920)
                                        .translated (0.0f, sectionGrid.pianoRowHeightForTest() * 0.45f);
            const auto writeEnd = juce::Point<float> (writeStart.x + 140.0f, writeStart.y + 2.0f);

            sectionGrid.mouseDown (makeMouseEvent (sectionGrid, writeStart, writeStart));
            sectionGrid.mouseDrag (makeMouseEvent (sectionGrid, writeEnd, writeStart));
            sectionGrid.mouseUp (makeMouseEvent (sectionGrid, writeEnd, writeStart));
            pump();

            check (countNotes() == notesBeforeWrite + 1,
                   NI_TXT ("a horizontal drag in the piano window writes a note"));

            if (countNotes() == notesBeforeWrite + 1)
            {
                const auto& written = document.getProject().tracks.front().clips.front().notes.back();

                checkNear (written.frequencyHz, music::piano::frequencyOf (writeRow), 1.0e-9,
                           NI_TXT ("and its pitch is exactly that semitone (")
                               + juce::String (written.frequencyHz, 3) + NI_TXT (" Hz)"));
                check (std::abs (written.frequencyHz - sectionGrid.frequencyForYForTest (writeStart.y)) > 0.5,
                       NI_TXT ("the row quantized the pitch instead of taking the pointer's frequency"));
            }

            // A vertical drag is a rubber band, exactly like the drum sectionGrid and the
            // reference lines: no note may appear.
            const auto notesBeforeBand = countNotes();
            const auto bandStart = sectionGrid.pointForSemitoneForTest (57, 3840);
            const auto bandEnd = juce::Point<float> (bandStart.x + 2.0f, bandStart.y + 70.0f);

            sectionGrid.mouseDown (makeMouseEvent (sectionGrid, bandStart, bandStart));
            sectionGrid.mouseDrag (makeMouseEvent (sectionGrid, bandEnd, bandStart));
            sectionGrid.mouseUp (makeMouseEvent (sectionGrid, bandEnd, bandStart));
            pump();

            check (countNotes() == notesBeforeBand,
                   NI_TXT ("a vertical drag rubber-bands instead of writing"));

            // Moving a note up and down lands on the semitone rows as well.
            const auto noteId = document.getProject().tracks.front().clips.front().notes.back().id;
            appState.selectNote (noteId, false);
            pump();

            const auto beforeMove = document.getProject().tracks.front().clips.front().notes.back();
            const auto bodyPoint = sectionGrid.pointFor (beforeMove.frequencyHz,
                                                  beforeMove.startTick
                                                      + document.getProject().tracks.front()
                                                            .clips.front().startTick
                                                      + 120);
            const auto bodyTarget = juce::Point<float> (bodyPoint.x + 3.0f, bodyPoint.y - 40.0f);

            sectionGrid.mouseDown (makeMouseEvent (sectionGrid, bodyPoint, bodyPoint));
            sectionGrid.mouseDrag (makeMouseEvent (sectionGrid, bodyTarget, bodyPoint));
            sectionGrid.mouseUp (makeMouseEvent (sectionGrid, bodyTarget, bodyPoint));
            pump();

            const auto movedNote = document.findNote (document.getProject().tracks.front().id, noteId);

            check (movedNote != nullptr, NI_TXT ("the dragged note is still there"));

            if (movedNote != nullptr)
                checkNear (movedNote->frequencyHz,
                           music::piano::quantize (movedNote->frequencyHz), 1.0e-9,
                           NI_TXT ("a note moved in the piano window lands on a semitone (")
                               + juce::String (movedNote->frequencyHz, 3) + NI_TXT (" Hz)"));

            // Hiding the reference lines leaves the piano roll on its own.
            document.applyEdit ("selftest piano line", [] (Project& project)
            {
                project.referenceLines.push_back (
                    music::makeFixedHzEntry (project.nextReferenceLineId++, 440.0, juce::String()));

                if (! project.referenceLines.empty())
                    project.referenceLines.back().locked = true;
            });
            pump();

            const auto linesShown = countColour (snapshot (sectionGrid), theme::referenceLineManual);

            check (linesShown > 80,
                   NI_TXT ("the piano window draws the reference lines as background (pixels = ")
                       + juce::String (linesShown) + NI_TXT (")"));

            panel.setShowReferenceLinesForTest (false);
            pump();

            const auto linesHidden = countColour (snapshot (sectionGrid), theme::referenceLineManual);

            check (! document.viewState().showReferenceLinesInPianoMode,
                   NI_TXT ("参考线显示 can be switched off"));
            check (linesHidden * 4 < linesShown,
                   NI_TXT ("and the lines really disappear (pixels = ")
                       + juce::String (linesHidden) + NI_TXT (")"));

            check (writeImage (snapshot (component),
                               outputDirectory.getChildFile ("35-piano-roll-no-lines.png")),
                   NI_TXT ("piano roll snapshot without the reference lines"));

            panel.setShowReferenceLinesForTest (true);
            pump();

            // A drum track keeps its pad sectionGrid, so the mode switch is not on offer.
            const auto drumId = sectionLanes.addTrack (TrackKind::drums);
            pump();

            check (drumId != 0 && ! panel.isModeBoxEnabledForTest(),
                   NI_TXT ("a drum track keeps its pad grid: the mode switch is greyed out"));
            check (! sectionGrid.isPianoMode(),
                   NI_TXT ("and the canvas stays the drum grid"));

            sectionLanes.deleteTrackNow (drumId);
            pump();

            check (panel.isModeBoxEnabledForTest(),
                   NI_TXT ("deleting the drum track brings the switch back"));

            // Even at the narrowest window the new controls keep a real size
            // instead of being squeezed to nothing.
            component.setSize (1120, 950);
            pump();

            check (! panel.getModeBoxBoundsForTest().isEmpty(),
                   NI_TXT ("the mode box survives the narrowest window"));

            component.setSize (1460, 950);
            pump();

            check (panel.getModeBoxBoundsForTest().getWidth() > 0
                       && panel.getPianoLinesButtonBoundsForTest().getWidth() > 0,
                   NI_TXT ("and both mode controls have a real size at the usual size"));

            // Back to how the editor started, so nothing after this inherits it.
            panel.selectEditorModeForTest (1);
            pump();

            check (document.viewState().editorMode == EditorMode::reference
                       && ! sectionGrid.isPianoMode(),
                   NI_TXT ("switching back to 参考线模式 restores the frequency axis"));
        }

        // ---- 第二十六批：编辑栏槽位 / 非零起点音符 / 工程切换 / 行头滚动 --------

        beginTest (NI_TXT ("音符编辑栏不再挤压画布"));

        {
            auto& panel = component.getEditorPanel();
            auto& stripGrid = panel.getGrid();
            auto& bar = panel.getNoteEditBar();

            juce::int64 firstNote = 0, secondNote = 0, stripClipId = 0;

            document.applyEdit ("selftest strip setup", [&] (Project& project)
            {
                project.tracks.clear();
                project.referenceLines.clear();
                project.nextTrackId = 1;

                const auto id = addTrack (project, TrackKind::synth);
                auto* track = project.findTrack (id);

                if (track == nullptr)
                    return;

                track->clips.clear();

                auto clip = makeDefaultClip (project);
                clip.startTick = 0;
                clip.lengthTicks = 38400;
                clip.loopLengthTicks = 38400;

                Note a;
                a.id = project.nextNoteId++;
                a.startTick = 0;
                a.durationTicks = 960;
                a.frequencyHz = 220.0;
                a.velocity = 100;

                Note b;
                b.id = project.nextNoteId++;
                b.startTick = 1920;
                b.durationTicks = 960;
                b.frequencyHz = 440.0;
                b.velocity = 100;

                firstNote = a.id;
                secondNote = b.id;
                clip.notes = { a, b };
                stripClipId = clip.id;
                track->clips.push_back (std::move (clip));
            });

            appState.selectTrack (document.getProject().tracks.front().id);
            appState.selectClip (stripClipId);
            appState.setEditorOpen (true);
            setBothTimelines (document, 140.0, 0.0);
            pump();

            const auto noneBounds = stripGrid.getBounds();

            appState.selectNote (firstNote, false);
            pump();
            const auto oneBounds = stripGrid.getBounds();

            appState.selectNote (secondNote, true);
            pump();
            const auto twoBounds = stripGrid.getBounds();

            check (noneBounds == oneBounds && oneBounds == twoBounds,
                   NI_TXT ("the canvas keeps its bounds in every selection state (")
                       + noneBounds.toString() + NI_TXT (" / ") + oneBounds.toString()
                       + NI_TXT (" / ") + twoBounds.toString() + NI_TXT (")"));
            check (NoteEditBar::slotHeight == NoteEditBar::singleNoteHeight,
                   NI_TXT ("the reserved strip is one single-note row"));
            check (bar.getBounds().getHeight() > NoteEditBar::slotHeight,
                   NI_TXT ("with several notes selected the bar hangs below its strip (")
                       + juce::String (bar.getBounds().getHeight()) + NI_TXT (")"));
            check (bar.getBounds().getBottom()
                       > panel.getNoteStripBoundsForTest().getBottom()
                       && bar.getBounds().getBottom() <= panel.getHeight(),
                   NI_TXT ("and the per-note list floats over what is below the strip (")
                       + bar.getBounds().toString() + NI_TXT (" in ")
                       + panel.getNoteStripBoundsForTest().toString() + NI_TXT (")"));
        }

        beginTest (NI_TXT ("片段起点不在零点时，写音符与改长度都对得上"));

        {
            auto& offsetGrid = component.getEditorPanel().getGrid();

            const auto barTicks = (juce::int64) kTicksPerQuarter * 4;
            juce::int64 offsetClipId = 0;

            document.applyEdit ("selftest offset clip setup", [&] (Project& project)
            {
                project.tracks.resize (1);
                project.referenceLines.clear();

                auto& track = project.tracks.front();
                track.clips.clear();

                // A line at 220 Hz is what a note is written on. It is sealed, the
                // way a saved project's lines are: only sealed entries resolve.
                auto line = music::makeFixedHzEntry (project.nextReferenceLineId++, 220.0,
                                                    juce::String());
                line.locked = true;
                project.referenceLines.push_back (line);

                auto clip = makeDefaultClip (project);
                clip.startTick = barTicks * 2;            // the clip's head is at bar 3
                clip.lengthTicks = barTicks * 4;
                clip.loopLengthTicks = clip.lengthTicks;
                offsetClipId = clip.id;
                track.clips.push_back (std::move (clip));
            });

            appState.selectTrack (document.getProject().tracks.front().id);
            appState.selectClip (offsetClipId);
            appState.setEditorOpen (true);
            // The clip's head is at bar 3, so the editor has to be scrolled there
            // for the pointer to be inside the canvas at all.
            setBothTimelines (document, 140.0, (double) barTicks * 2);
            document.viewState().centreHz = 220.0;
            document.viewState().octavesVisible = 4.0;
            offsetGrid.refresh();
            pump();

            const auto writeFrom = barTicks * 2 + 960;      // a beat inside the clip
            const auto writeTo   = writeFrom + 960 * 3;
            const auto from = offsetGrid.pointFor (220.0, writeFrom);
            const auto to   = offsetGrid.pointFor (220.0, writeTo);

            offsetGrid.mouseDown (makeMouseEvent (offsetGrid, from, from));
            offsetGrid.mouseDrag (makeMouseEvent (offsetGrid, to, from));
            pump();

            const auto previewEnd = offsetGrid.getDragPreviewEndTickForTest();
            const auto previewRect = offsetGrid.getDragPreviewNoteRectForTest();

            offsetGrid.mouseUp (makeMouseEvent (offsetGrid, to, from));
            pump();

            const auto* written = document.getProject().findClip (offsetClipId);

            check (written != nullptr && written->notes.size() == 1,
                   NI_TXT ("the drag wrote one note into the offset clip (got ")
                       + juce::String (written != nullptr ? (int) written->notes.size() : -1)
                       + NI_TXT (")"));

            if (written != nullptr && written->notes.size() == 1)
            {
                const auto noteId = written->notes.front().id;
                const auto absoluteStart = written->startTick + written->notes.front().startTick;

                check (absoluteStart == writeFrom,
                       NI_TXT ("the note lands on the beat the pointer was on (")
                           + juce::String (absoluteStart) + NI_TXT (" vs ")
                           + juce::String (writeFrom) + NI_TXT (")"));
                check (previewEnd == writeTo,
                       NI_TXT ("the write preview ends where the pointer is (")
                           + juce::String (previewEnd) + NI_TXT (" vs ")
                           + juce::String (writeTo) + NI_TXT (")"));
                check (absoluteStart + written->notes.front().durationTicks == previewEnd,
                       NI_TXT ("and the committed note matches the preview end"));

                const auto committed = offsetGrid.getNoteRectForTest (noteId);

                check (std::abs (committed.getX() - previewRect.getX()) < 1.5f
                           && std::abs (committed.getRight() - previewRect.getRight()) < 1.5f,
                       NI_TXT ("the note is drawn exactly where its preview was (")
                           + juce::String (previewRect.getX(), 1) + NI_TXT ("..")
                           + juce::String (previewRect.getRight(), 1) + NI_TXT (" vs ")
                           + juce::String (committed.getX(), 1) + NI_TXT ("..")
                           + juce::String (committed.getRight(), 1) + NI_TXT (")"));

                // Now stretch its right edge to a known tick.
                const auto resizeToTick = barTicks * 2 + 960 * 6;
                const auto grab = juce::Point<float> (committed.getRight() - 1.5f,
                                                      committed.getCentreY());
                const auto grabTo = offsetGrid.pointFor (220.0, resizeToTick);

                offsetGrid.mouseDown (makeMouseEvent (offsetGrid, grab, grab));
                offsetGrid.mouseDrag (makeMouseEvent (offsetGrid, grabTo, grab));
                pump();

                const auto resizePreviewEnd = offsetGrid.getDragPreviewEndTickForTest();

                offsetGrid.mouseUp (makeMouseEvent (offsetGrid, grabTo, grab));
                pump();

                const auto* resized = document.getProject().findClip (offsetClipId);

                check (resizePreviewEnd == resizeToTick,
                       NI_TXT ("the resize preview follows the pointer (")
                           + juce::String (resizePreviewEnd) + NI_TXT (" vs ")
                           + juce::String (resizeToTick) + NI_TXT (")"));
                check (resized != nullptr && resized->notes.size() == 1
                           && resized->startTick + resized->notes.front().startTick == writeFrom
                           && resized->startTick + resized->notes.front().endTick() == resizeToTick,
                       NI_TXT ("and the note keeps its start while its end lands on the pointer"));
            }
        }

        beginTest (NI_TXT ("打开工程后行头跟着换，片段选中被清空"));

        {
            auto& reloadLanes = component.getTrackArea();
            const auto reloadFile = outputDirectory.getChildFile ("selftest-reload-a.nprj");
            juce::String reloadError;

            // Project A: an FM track and a drum track.
            document.applyEdit ("selftest project A", [] (Project& project)
            {
                project.tracks.clear();
                project.referenceLines.clear();
                project.nextTrackId = 1;

                const auto synthId = addTrack (project, TrackKind::synth);
                const auto drumId = addTrack (project, TrackKind::drums);

                if (auto* synth = project.findTrack (synthId))
                    synth->name = NI_TXT ("甲");

                if (auto* drums = project.findTrack (drumId))
                    drums->name = NI_TXT ("乙");
            });
            pump();

            check (document.saveToFile (reloadFile, reloadError), reloadError);

            // A different project with the same track ids: the change message alone
            // has to bring the headers along.
            document.applyEdit ("selftest other project", [] (Project& project)
            {
                project.tracks.clear();
                project.nextTrackId = 1;
                addTrack (project, TrackKind::sampler);
                addTrack (project, TrackKind::wavetable);
            });
            pump();

            auto* sampler = reloadLanes.getLaneHeaderForTest (0);
            auto* wavetable = reloadLanes.getLaneHeaderForTest (1);

            check (sampler != nullptr && sampler->getTypeTextForTest() == NI_TXT ("采样器"),
                   NI_TXT ("the headers follow a project change (got '")
                       + (sampler != nullptr ? sampler->getTypeTextForTest() : juce::String())
                       + NI_TXT ("')"));
            check (wavetable != nullptr && wavetable->getTypeTextForTest() == NI_TXT ("波表合成器"),
                   NI_TXT ("and the second one too"));

            // Ctrl+Z does not exist here: load project A back through the same path
            // the file chooser uses.
            appState.selectClip (999);
            pump();

            check (component.loadProjectFile (reloadFile), reloadError);
            pump();

            auto* loadedSynth = reloadLanes.getLaneHeaderForTest (0);
            auto* loadedDrums = reloadLanes.getLaneHeaderForTest (1);

            check (loadedSynth != nullptr && loadedSynth->getTypeTextForTest() == NI_TXT ("FM合成器"),
                   NI_TXT ("loading a project rebuilds the headers (got '")
                       + (loadedSynth != nullptr ? loadedSynth->getTypeTextForTest() : juce::String())
                       + NI_TXT ("')"));
            check (loadedDrums != nullptr && loadedDrums->getTypeTextForTest() == NI_TXT ("鼓机"),
                   NI_TXT ("including the drum track"));
            check (appState.selectedClipId == 0,
                   NI_TXT ("and the clip selection is cleared (was ")
                       + juce::String (appState.selectedClipId) + NI_TXT (")"));
            check (reloadLanes.getLaneScrollForTest() == 0,
                   NI_TXT ("the lanes start at the top again"));
        }

        beginTest (NI_TXT ("音轨栏滚轮纵向滚动，Shift+滚轮横向平移"));

        {
            auto& scrollLanes = component.getTrackArea();

            document.applyEdit ("selftest lane scroll setup", [] (Project& project)
            {
                project.tracks.clear();
                project.nextTrackId = 1;

                // Enough lanes that they cannot all fit at once: the lane area is
                // the whole arrangement row here (the editor is closed), which is
                // about 800 px tall.
                for (int i = 0; i < 14; ++i)
                    addTrack (project, TrackKind::synth);
            });
            appState.selectTrack (document.getProject().tracks.front().id);
            pump();

            check (scrollLanes.getMaxLaneScrollForTest() > 0,
                   NI_TXT ("fourteen tracks do not fit in the lane area (max scroll ")
                       + juce::String (scrollLanes.getMaxLaneScrollForTest()) + NI_TXT (")"));

            const auto centre = juce::Point<float> ((float) scrollLanes.getWidth() * 0.5f,
                                                    (float) scrollLanes.getHeight() * 0.5f);
            const auto noMods = juce::ModifierKeys();
            const auto shift = juce::ModifierKeys (juce::ModifierKeys::shiftModifier);
            const auto alt = juce::ModifierKeys (juce::ModifierKeys::altModifier);
            const juce::MouseWheelDetails down { 0.0f, -0.5f, false, false, false };
            const juce::MouseWheelDetails up { 0.0f, 0.5f, false, false, false };

            const auto horizontalBefore = horizontalOffsetFor (document.viewState(),
                                                               TimelineView::arrangement);
            const auto zoomBefore = pixelsPerQuarterFor (document.viewState(),
                                                         TimelineView::arrangement);

            scrollLanes.mouseWheelMove (makeMouseEvent (scrollLanes, centre, centre, noMods), down);

            const auto scrolled = scrollLanes.getLaneScrollForTest();

            check (scrolled > 0, NI_TXT ("the wheel scrolls the lanes (got ")
                                  + juce::String (scrolled) + NI_TXT (")"));
            check (horizontalOffsetFor (document.viewState(), TimelineView::arrangement)
                       == horizontalBefore,
                   NI_TXT ("and leaves the timeline where it was"));

            for (int i = 0; i < 40; ++i)
                scrollLanes.mouseWheelMove (makeMouseEvent (scrollLanes, centre, centre, noMods),
                                            down);

            check (scrollLanes.getLaneScrollForTest() == scrollLanes.getMaxLaneScrollForTest(),
                   NI_TXT ("scrolling down stops at the last lane (")
                       + juce::String (scrollLanes.getLaneScrollForTest()) + NI_TXT (" of ")
                       + juce::String (scrollLanes.getMaxLaneScrollForTest()) + NI_TXT (")"));

            // The lane under the pointer follows the scroll: the last lane's header
            // is reachable, and clicking it selects that track.
            auto* lastHeader = scrollLanes.getLaneHeaderForTest (13);
            const auto lastHeaderBounds = scrollLanes.getLaneHeaderBoundsForTest (13);

            check (lastHeader != nullptr
                       && lastHeaderBounds.getBottom() <= scrollLanes.getHeight(),
                   NI_TXT ("the last lane header is on screen after scrolling"));

            if (lastHeader != nullptr)
            {
                // Click the lane body (the header column is drawn by the header
                // itself): anywhere in the lane selects that track.
                const auto laneCentre = juce::Point<float> (
                    (float) TrackArea::headerWidth + 80.0f,
                    (float) lastHeaderBounds.getCentreY());

                scrollLanes.mouseDown (makeMouseEvent (scrollLanes, laneCentre, laneCentre));
                scrollLanes.mouseUp (makeMouseEvent (scrollLanes, laneCentre, laneCentre));
                pump();

                check (appState.selectedTrackId == document.getProject().tracks.back().id,
                       NI_TXT ("clicking a scrolled-to lane selects its track"));
            }

            for (int i = 0; i < 40; ++i)
                scrollLanes.mouseWheelMove (makeMouseEvent (scrollLanes, centre, centre, noMods),
                                            up);

            check (scrollLanes.getLaneScrollForTest() == 0,
                   NI_TXT ("and scrolling up stops at the first lane"));

            scrollLanes.mouseWheelMove (makeMouseEvent (scrollLanes, centre, centre, shift), down);

            check (horizontalOffsetFor (document.viewState(), TimelineView::arrangement)
                       != horizontalBefore,
                   NI_TXT ("Shift+wheel pans the timeline"));
            check (scrollLanes.getLaneScrollForTest() == 0,
                   NI_TXT ("and does not scroll the lanes"));

            scrollLanes.mouseWheelMove (makeMouseEvent (scrollLanes, centre, centre, alt), down);

            check (pixelsPerQuarterFor (document.viewState(), TimelineView::arrangement)
                       != zoomBefore,
                   NI_TXT ("Alt+wheel still zooms"));

            setBothTimelines (document, 140.0, 0.0);
        }

        beginTest (NI_TXT ("重命名能应用也能取消（并退出命名状态）"));

        {
            auto& renameLanes = component.getTrackArea();
            auto* header = renameLanes.getLaneHeaderForTest (0);

            check (header != nullptr, NI_TXT ("there is a lane header to rename"));

            if (header != nullptr)
            {
                const auto original = document.getProject().tracks.front().name;

                header->typeNameForTest (NI_TXT ("改过的名字"));
                header->pressRenameReturnForTest();
                pump();

                check (document.getProject().tracks.front().name == NI_TXT ("改过的名字"),
                       NI_TXT ("Return applies the new name (got '")
                           + document.getProject().tracks.front().name + NI_TXT ("')"));
                check (header->getEditedNameForTest() == NI_TXT ("改过的名字"),
                       NI_TXT ("and the editor shows it"));

                // Escape leaves the editor without changing anything.
                header->typeNameForTest (NI_TXT ("不要这个名字"));
                header->pressRenameEscapeForTest();
                pump();

                check (document.getProject().tracks.front().name == NI_TXT ("改过的名字"),
                       NI_TXT ("Escape does not rename the track (got '")
                           + document.getProject().tracks.front().name + NI_TXT ("')"));
                check (header->getEditedNameForTest() == NI_TXT ("改过的名字"),
                       NI_TXT ("and puts the old name back in the editor"));

                // Back to where the run found it.
                header->typeNameForTest (original);
                header->pressRenameReturnForTest();
                pump();

                check (document.getProject().tracks.front().name == original,
                       NI_TXT ("the name can be put back"));
            }
        }

        beginTest (NI_TXT ("轨道滚动时车道不会画到上方工具条上"));

        {
            auto& layerLanes = component.getTrackArea();

            document.applyEdit ("selftest lane layer setup", [] (Project& project)
            {
                project.tracks.clear();
                project.nextTrackId = 1;

                for (int i = 0; i < 14; ++i)
                    addTrack (project, TrackKind::synth);
            });
            pump();

            check (layerLanes.getMaxLaneScrollForTest() > 0, NI_TXT ("the lanes can scroll"));

            const juce::Rectangle<int> chrome (0, 0, layerLanes.getWidth(),
                                               TrackArea::topBarHeight + TrackArea::markerHeight
                                                   + TrackArea::rulerHeight);

            layerLanes.setLaneScrollForTest (0);
            const auto unscrolled = snapshot (layerLanes);

            layerLanes.setLaneScrollForTest (TrackArea::laneHeight);
            const auto scrolled = snapshot (layerLanes);

            auto changed = 0;

            for (int y = chrome.getY(); y < chrome.getBottom(); ++y)
                for (int x = chrome.getX(); x < chrome.getRight(); ++x)
                    if (unscrolled.getPixelAt (x, y) != scrolled.getPixelAt (x, y))
                        ++changed;

            check (changed == 0,
                   NI_TXT ("the + bar, the markers and the ruler are untouched by the scroll (")
                       + juce::String (changed) + NI_TXT (" pixels)"));

            layerLanes.setLaneScrollForTest (0);
            pump();
        }

        beginTest (NI_TXT ("多选音符一起改长度"));

        {
            auto& groupGrid = component.getEditorPanel().getGrid();

            const auto barTicks = (juce::int64) kTicksPerQuarter * 4;
            juce::int64 clipId = 0;
            juce::int64 firstId = 0, secondId = 0;

            document.applyEdit ("selftest group resize setup", [&] (Project& project)
            {
                project.tracks.resize (1);
                project.referenceLines.clear();

                auto line = music::makeFixedHzEntry (project.nextReferenceLineId++, 220.0,
                                                    juce::String());
                line.locked = true;
                project.referenceLines.push_back (line);

                auto& track = project.tracks.front();
                track.clips.clear();

                auto clip = makeDefaultClip (project);
                clip.startTick = 0;
                clip.lengthTicks = barTicks * 4;
                clip.loopLengthTicks = clip.lengthTicks;

                Note a;
                a.id = project.nextNoteId++;
                a.startTick = 0;
                a.durationTicks = 960;
                a.frequencyHz = 220.0;
                a.velocity = 100;

                Note b;
                b.id = project.nextNoteId++;
                b.startTick = 1920;
                b.durationTicks = 960;
                b.frequencyHz = 220.0;
                b.velocity = 100;

                firstId = a.id;
                secondId = b.id;
                clip.notes = { a, b };
                clipId = clip.id;
                track.clips.push_back (std::move (clip));
            });

            appState.selectTrack (document.getProject().tracks.front().id);
            appState.selectClip (clipId);
            appState.setEditorOpen (true);
            setBothTimelines (document, 140.0, 0.0);
            document.viewState().centreHz = 220.0;
            document.viewState().octavesVisible = 4.0;
            groupGrid.refresh();
            appState.selectNote (firstId, false);
            appState.selectNote (secondId, true);
            pump();

            const auto* clip = document.getProject().findClip (clipId);
            const auto firstRect = groupGrid.getNoteRectForTest (firstId);
            const auto secondRect = groupGrid.getNoteRectForTest (secondId);
            const auto grab = juce::Point<float> (secondRect.getRight() - 1.5f,
                                                 secondRect.getCentreY());
            const auto to = groupGrid.pointFor (220.0, 1920 + 960 * 3);

            check (clip != nullptr && clip->notes.size() == 2, NI_TXT ("two notes to resize"));

            groupGrid.mouseDown (makeMouseEvent (groupGrid, grab, grab));
            groupGrid.mouseDrag (makeMouseEvent (groupGrid, to, grab));
            pump();

            const auto previewEnd = groupGrid.getDragPreviewEndTickForTest();

            groupGrid.mouseUp (makeMouseEvent (groupGrid, to, grab));
            pump();

            const auto* after = document.getProject().findClip (clipId);
            const auto findNoteIn = [] (const Clip* c, juce::int64 noteId) -> const Note*
            {
                if (c == nullptr)
                    return nullptr;

                for (const auto& note : c->notes)
                    if (note.id == noteId)
                        return &note;

                return nullptr;
            };

            const auto* resizedFirst = findNoteIn (after, firstId);
            const auto* resizedSecond = findNoteIn (after, secondId);

            check (previewEnd == 1920 + 960 * 3,
                   NI_TXT ("the dragged edge follows the pointer (")
                       + juce::String (previewEnd) + NI_TXT (")"));
            check (resizedFirst != nullptr && resizedFirst->durationTicks == 960 * 3,
                   NI_TXT ("the other selected note grew by the same delta (got ")
                       + juce::String (resizedFirst != nullptr ? resizedFirst->durationTicks : -1)
                       + NI_TXT (")"));
            check (resizedSecond != nullptr && resizedSecond->endTick() == 1920 + 960 * 3,
                   NI_TXT ("and the dragged one ends on the pointer"));
            check (firstRect.getRight() != secondRect.getRight(),
                   NI_TXT ("(the two notes started with different edges)"));

            // One undo step for the whole group.
            document.undo();
            pump();

            const auto* undone = document.getProject().findClip (clipId);
            const auto* undoneFirst = findNoteIn (undone, firstId);

            check (undoneFirst != nullptr && undoneFirst->durationTicks == 960,
                   NI_TXT ("one undo puts the whole group back (")
                       + juce::String (undoneFirst != nullptr ? undoneFirst->durationTicks : -1)
                       + NI_TXT (")"));
        }

        beginTest (NI_TXT ("音轨栏的命中测试：空白车道、行头、推子各归各的"));

        {
            auto& hitTestLanes = component.getTrackArea();

            document.applyEdit ("selftest lane hit setup", [] (Project& project)
            {
                project.tracks.clear();
                project.referenceLines.clear();
                project.nextTrackId = 1;

                // Enough tracks that the lane area can actually scroll: the wheel test
                // below needs somewhere to scroll to.
                for (int i = 0; i < 14; ++i)
                    addTrack (project, TrackKind::synth);
            });
            pump();

            check (hitTestLanes.getMaxLaneScrollForTest() > 0,
                   NI_TXT ("the lanes have somewhere to scroll (max ")
                       + juce::String (hitTestLanes.getMaxLaneScrollForTest()) + NI_TXT (")"));

            check (hitTestLanes.getWidth() > TrackArea::headerWidth + 40 && hitTestLanes.getHeight() > 200,
                   NI_TXT ("the track area is laid out for the hit test (")
                       + juce::String (hitTestLanes.getWidth()) + "x" + juce::String (hitTestLanes.getHeight())
                       + NI_TXT (")"));

            const auto lanePoint = juce::Point<float> ((float) (TrackArea::headerWidth + 40),
                                                       (float) (TrackArea::topBarHeight
                                                                    + TrackArea::markerHeight
                                                                    + TrackArea::rulerHeight + 20));

            // The blank lane must reach the track area itself. While the header holder
            // swallowed it, a click there did nothing at all - and JUCE then handed the
            // keyboard focus to the first lane's name box, which is what "clicking the
            // empty lane jumps into renaming" looked like.
            check (hitTestLanes.getComponentAt (lanePoint) == &hitTestLanes,
                   NI_TXT ("a blank lane hit-tests to the track area itself"));
            check (hitTestLanes.getWantsKeyboardFocus(),
                   NI_TXT ("and the track area is what takes the keyboard focus"));

            auto* header = hitTestLanes.getLaneHeaderForTest (0);

            check (header != nullptr, NI_TXT ("there is a lane header to aim at"));

            if (header != nullptr)
            {
                const auto headerCentre = hitTestLanes.getLocalPoint (header,
                                                               header->getLocalBounds().getCentre());

                check (hitTestLanes.getComponentAt (headerCentre) == header,
                       NI_TXT ("a header's blank part belongs to the header"));
                check (header->getWantsKeyboardFocus(),
                       NI_TXT ("and the header takes the focus itself, not its name box"));

                // The name box still takes its own clicks, so single click rename works.
                const auto nameCentre = hitTestLanes.getLocalPoint (header,
                                                             header->getControlBoundsForTest (0)
                                                                 .getCentre());

                check (hitTestLanes.getComponentAt (nameCentre) != header,
                       NI_TXT ("the name box still gets its own clicks"));

                // The wheel: over the volume fader it has to scroll the lanes, not
                // rewrite the track's level.
                const auto volumeCentre = hitTestLanes.getLocalPoint (header,
                                                               header->getControlBoundsForTest (2)
                                                                   .getCentre());
                auto* hitSlider = hitTestLanes.getComponentAt (volumeCentre);

                check (dynamic_cast<juce::Slider*> (hitSlider) != nullptr,
                       NI_TXT ("the volume fader is under the pointer where it is drawn"));

                if (auto* slider = dynamic_cast<juce::Slider*> (hitSlider))
                {
                    check (! slider->isScrollWheelEnabled(),
                           NI_TXT ("the fader no longer takes the mouse wheel"));

                    const auto volumeBefore = document.getProject().tracks.front().volumeDb;
                    const auto panBefore = document.getProject().tracks.front().pan;
                    const auto scrollBefore = hitTestLanes.getLaneScrollForTest();
                    const juce::MouseWheelDetails down { 0.0f, -0.5f, false, false, false };
                    const auto centre = slider->getLocalBounds().getCentre().toFloat();

                    slider->mouseWheelMove (makeMouseEvent (*slider, centre, centre), down);
                    pump();

                    check (hitTestLanes.getLaneScrollForTest() > scrollBefore,
                           NI_TXT ("the wheel over a fader scrolls the lanes instead (")
                               + juce::String (scrollBefore) + NI_TXT (" -> ")
                               + juce::String (hitTestLanes.getLaneScrollForTest()) + NI_TXT (")"));
                    check (std::abs (document.getProject().tracks.front().volumeDb - volumeBefore)
                                   < 1.0e-4f
                               && std::abs (document.getProject().tracks.front().pan - panBefore)
                                      < 1.0e-4f,
                           NI_TXT ("and the track's volume / pan are left alone"));

                    hitTestLanes.setLaneScrollForTest (0);
                    pump();
                }
            }

            // Clicking empty lane space must not leave the caret in a name box, and
            // must not rename anything.
            juce::StringArray namesBefore;

            for (const auto& track : document.getProject().tracks)
                namesBefore.add (track.name);

            hitTestLanes.mouseDown (makeMouseEvent (hitTestLanes, lanePoint, lanePoint));
            hitTestLanes.mouseUp (makeMouseEvent (hitTestLanes, lanePoint, lanePoint));
            pump();

            const auto* focused = juce::Component::getCurrentlyFocusedComponent();

            check (dynamic_cast<const juce::TextEditor*> (focused) == nullptr,
                   NI_TXT ("clicking a blank lane never leaves the focus in a name box"));

            juce::StringArray namesAfter;

            for (const auto& track : document.getProject().tracks)
                namesAfter.add (track.name);

            check (namesAfter == namesBefore, NI_TXT ("and no track was renamed"));
        }

        // ---- 栏位拖动 / 折叠 + 片段菜单 ---------------------------------------

        beginTest (NI_TXT ("栏位可拖动改宽、可折叠成细栏；片段菜单接了重命名 / 循环 / 重复"));

        {
            document.applyEdit ("selftest panels setup", [] (Project& project)
            {
                project.tracks.clear();
                project.referenceLines.clear();
                project.nextTrackId = 1;
                project.view.referenceGroupPanel = {};
                project.view.presetPanel = {};
                project.view.referenceLinesPanel = {};

                const auto id = addTrack (project, TrackKind::synth);
                auto* track = project.findTrack (id);
                track->clips.front().notes.clear();
            });
            pump();

            const auto& setupTrack = document.getProject().tracks.front();
            appState.selectTrack (setupTrack.id);
            appState.selectClip (setupTrack.clips.front().id);
            appState.setEditorOpen (true);
            pump();

            auto& panel = component.getEditorPanel();
            using Id = EditorPanel::SidePanelId;

            // ---- the defaults are what the layout always used ------------------
            check (panel.getPanelWidthForTest (Id::referenceGroup)
                       == ReferenceGroupPanel::panelWidth,
                   NI_TXT ("参考组栏开局是它一贯的默认宽度"));
            check (panel.getPanelWidthForTest (Id::referenceLines) >= kMinPanelWidth,
                   NI_TXT ("参考线状态栏开局也是默认宽度"));
            check (! panel.isPanelCollapsedForTest (Id::referenceGroup)
                       && ! panel.isPanelCollapsedForTest (Id::referenceLines),
                   NI_TXT ("两栏开局都展开着"));
            check (panel.getGrid().getWidth() >= kMinCanvasWidth,
                   NI_TXT ("画布至少还有 ") + juce::String (kMinCanvasWidth) + NI_TXT (" px"));

            // ---- dragging changes the saved width -----------------------------
            const auto groupBefore = panel.getPanelWidthForTest (Id::referenceGroup);
            panel.dragPanelForTest (Id::referenceGroup, 40);
            pump();

            check (panel.getPanelWidthForTest (Id::referenceGroup) == groupBefore + 40,
                   NI_TXT ("拖分隔条真的把这栏拉宽了（")
                       + juce::String (panel.getPanelWidthForTest (Id::referenceGroup))
                       + NI_TXT ("）"));
            check (document.viewState().referenceGroupPanel.size == groupBefore + 40,
                   NI_TXT ("宽度进了工程状态，不是只画在屏幕上"));
            check (panel.getGrid().getWidth() >= kMinCanvasWidth,
                   NI_TXT ("拉宽之后画布仍然在最小线以上"));

            // ---- folding it into a thin bar ------------------------------------
            panel.togglePanelForTest (Id::referenceGroup);
            pump();

            check (panel.isPanelCollapsedForTest (Id::referenceGroup),
                   NI_TXT ("点箭头之后这一栏折叠了"));
            check (panel.getPanelWidthForTest (Id::referenceGroup) == kCollapsedPanelWidth,
                   NI_TXT ("折叠后是一条 ") + juce::String (kCollapsedPanelWidth)
                       + NI_TXT (" px 的细栏"));
            check (! panel.isPanelVisibleForTest (Id::referenceGroup),
                   NI_TXT ("折叠后栏里的内容看不见了"));
            check (! panel.getPanelToggleBoundsForTest (Id::referenceGroup).isEmpty(),
                   NI_TXT ("细栏里的箭头有真实尺寸（点得到）"));
            check (panel.isPanelToggleVisibleForTest (Id::referenceGroup),
                   NI_TXT ("箭头本身是可见的（不只是有个位置）"));

            panel.togglePanelForTest (Id::referenceGroup);
            pump();

            check (! panel.isPanelCollapsedForTest (Id::referenceGroup)
                       && panel.isPanelVisibleForTest (Id::referenceGroup),
                   NI_TXT ("再点一下又展开了"));
            check (panel.isPanelToggleVisibleForTest (Id::referenceGroup),
                   NI_TXT ("展开之后箭头也还看得见"));
            check (panel.getPanelWidthForTest (Id::referenceGroup) == groupBefore + 40,
                   NI_TXT ("展开回到折叠前的宽度（") 
                       + juce::String (panel.getPanelWidthForTest (Id::referenceGroup))
                       + NI_TXT ("）"));

            // ---- the same for the other two columns ----------------------------
            // The right-hand column's handle is on its left edge: dragging it to the
            // left is what makes that column wider (the user reported the opposite).
            const auto linesBefore = panel.getPanelWidthForTest (Id::referenceLines);
            panel.dragPanelForTest (Id::referenceLines, -30);
            pump();

            check (panel.getPanelWidthForTest (Id::referenceLines) == linesBefore + 30,
                   NI_TXT ("往左拖右侧状态栏的分隔条，它变宽了（")
                       + juce::String (panel.getPanelWidthForTest (Id::referenceLines))
                       + NI_TXT ("）"));
            check (! panel.isPanelCollapsedForTest (Id::referenceLines)
                       && panel.isPanelVisibleForTest (Id::referenceLines),
                   NI_TXT ("拖动之后右侧状态栏仍然是展开的"));

            panel.togglePanelForTest (Id::referenceLines);
            pump();
            check (panel.isPanelCollapsedForTest (Id::referenceLines)
                       && panel.getPanelWidthForTest (Id::referenceLines) == kCollapsedPanelWidth,
                   NI_TXT ("参考线状态栏也能折成细栏"));

            appState.toggleSidePanel (AppState::SidePanel::presets);
            pump();

            check (! panel.getPanelToggleBoundsForTest (Id::presets).isEmpty(),
                   NI_TXT ("预设栏出现时也有自己的箭头"));

            panel.dragPanelForTest (Id::presets, 25);
            panel.togglePanelForTest (Id::presets);
            pump();

            check (panel.isPanelCollapsedForTest (Id::presets)
                       && panel.getPanelWidthForTest (Id::presets) == kCollapsedPanelWidth,
                   NI_TXT ("预设栏能拖动、能折叠"));
            check (document.viewState().presetPanel.size > 0,
                   NI_TXT ("预设栏拖动后的宽度也存了下来（")
                       + juce::String (document.viewState().presetPanel.size) + NI_TXT ("）"));

            // ---- the state travels with the project ----------------------------
            check (writeImage (snapshot (component),
                               outputDirectory.getChildFile ("40-panels-collapsed.png")),
                   NI_TXT ("折叠后的快照"));

            {
                const auto json = projectToJson (document.getProject());
                Project panelsReloaded;
                juce::String panelsError;
                check (projectFromJson (json, panelsReloaded, panelsError), NI_TXT ("存盘再读回"));
                check (panelsReloaded.view.referenceGroupPanel.collapsed == false
                           && panelsReloaded.view.referenceGroupPanel.size == groupBefore + 40,
                       NI_TXT ("参考组栏的宽度与展开状态都跟着工程走"));
                check (panelsReloaded.view.referenceLinesPanel.collapsed
                           && panelsReloaded.view.referenceLinesPanel.size == linesBefore + 30,
                       NI_TXT ("参考线状态栏折叠着读回来（宽度也还记着）"));
                check (panelsReloaded.view.presetPanel.collapsed,
                       NI_TXT ("预设栏也是折叠着读回来"));

                // A project written before the columns could be resized has none of
                // these fields: it opens with the defaults, all unfolded.
                auto withoutColumns = juce::JSON::parse (json);

                if (auto* root = withoutColumns.getDynamicObject())
                    if (auto* viewVar = root->getProperty ("view").getDynamicObject())
                        for (const auto* key : { "panelReferenceGroupSize", "panelReferenceGroupCollapsed",
                                                 "panelPresetSize", "panelPresetCollapsed",
                                                 "panelReferenceLinesSize", "panelReferenceLinesCollapsed" })
                            viewVar->removeProperty (key);

                Project oldProject;
                check (projectFromJson (juce::JSON::toString (withoutColumns, false),
                                        oldProject, panelsError),
                       NI_TXT ("缺字段的旧工程照常打开"));
                check (oldProject.view.referenceGroupPanel.size == 0
                           && ! oldProject.view.referenceGroupPanel.collapsed
                           && ! oldProject.view.referenceLinesPanel.collapsed,
                       NI_TXT ("旧工程：默认宽度、全部展开"));
                check (panelWidthFor (oldProject.view.referenceGroupPanel,
                                      ReferenceGroupPanel::panelWidth)
                           == ReferenceGroupPanel::panelWidth,
                       NI_TXT ("旧工程里参考组栏用的就是默认宽度"));
            }

            // ---- the lane area has the whole width now -------------------------
            check (component.getTrackArea().getRight() >= component.getWidth() - 12,
                   NI_TXT ("音轨栏右侧那一列没有了，音轨栏占满整宽（右边缘 ")
                       + juce::String (component.getTrackArea().getRight()) + NI_TXT ("）"));

            // ---- the clip menu's new commands ----------------------------------
            const auto& actionTrack = document.getProject().tracks.front();
            const auto clipId = actionTrack.clips.front().id;
            auto& panelLanes = component.getTrackArea();

            check (! document.getProject().findClip (clipId)->looped,
                   NI_TXT ("片段一开始没有循环"));
            check (panelLanes.performClipMenuCommandForTest (6, clipId, 0),
                   NI_TXT ("菜单里的「循环」跑通了"));
            pump();

            {
                const auto* looped = document.getProject().findClip (clipId);
                check (looped != nullptr && looped->looped && looped->loopLengthTicks > 0,
                       NI_TXT ("循环打开了，而且有了一个循环周期"));
            }

            const auto loopMenu = panelLanes.getClipMenuItemsForTest (clipId, 0);
            check (loopMenu.size() > 5 && loopMenu[5].ticked,
                   NI_TXT ("菜单里的「循环」现在打着勾"));

            panelLanes.performClipMenuCommandForTest (6, clipId, 0);
            pump();
            check (! document.getProject().findClip (clipId)->looped,
                   NI_TXT ("再点一次就关掉了"));

            const auto nameBefore = document.getProject().findClip (clipId)->name;
            check (panelLanes.performClipMenuCommandForTest (5, clipId, 0),
                   NI_TXT ("菜单里的「重命名」跑通了"));
            pump();
            check (document.getProject().findClip (clipId)->name != nameBefore,
                   NI_TXT ("片段名真的改了（") 
                       + document.getProject().findClip (clipId)->name + NI_TXT ("）"));

            check (panelLanes.performClipMenuCommandForTest (7, clipId, 0),
                   NI_TXT ("菜单里的「重复」跑通了"));
            pump();

            appState.toggleSidePanel (AppState::SidePanel::none);
            document.applyEdit ("selftest panels restore", [] (Project& project)
            {
                project.view.referenceGroupPanel = {};
                project.view.presetPanel = {};
                project.view.referenceLinesPanel = {};
            });
            pump();
        }

        // ---- 混音台 + 空格键全局走带 -----------------------------------------

        beginTest (NI_TXT ("混音台：每轨一根条带，最右边一根钉住的总输出"));

        {
            juce::int64 mixerTrackA = 0, mixerTrackB = 0;

            document.applyEdit ("selftest mixer setup",
                                [&mixerTrackA, &mixerTrackB] (Project& project)
            {
                project.tracks.clear();
                project.referenceLines.clear();
                project.nextTrackId = 1;
                project.masterVolumeDb = 0.0f;

                mixerTrackA = addTrack (project, TrackKind::synth);
                mixerTrackB = addTrack (project, TrackKind::drums);

                if (auto* synth = project.findTrack (mixerTrackA))
                {
                    synth->clips.front().notes.clear();
                    synth->volumeDb = 0.0f;
                    synth->pan = 0.0f;

                    // Two inserts, so the strip's rows and the row menu have
                    // something to show. Their plugins are not there: the mixer does
                    // not care, and a row says what the chain says.
                    EffectSlot first;
                    first.name = NI_TXT ("效果一");
                    first.pluginPath = "missing-first.vst3";
                    synth->effects.push_back (first);

                    EffectSlot second;
                    second.name = NI_TXT ("效果二");
                    second.pluginPath = "missing-second.vst3";
                    synth->effects.push_back (second);
                }
            });
            pump();

            component.openMixerWindow();
            pump();

            auto* mixer = component.getMixerWindowForTest();

            check (mixer != nullptr, NI_TXT ("点「混音台」真的开出一个窗口"));

            if (mixer != nullptr)
            {
                const auto mixerScreen = placement::displayAreaFor (nullptr);

                check (mixerScreen.contains (mixer->getBounds()),
                       NI_TXT ("混音台开在屏幕里（") + mixer->getBounds().toString()
                           + NI_TXT (" in ") + mixerScreen.toString() + NI_TXT ("）"));

                const auto mixerStrips = mixer->getStripCountForTest();
                const auto mixerTracks = (int) document.getProject().tracks.size();

                check (mixerStrips == mixerTracks + 1,
                       NI_TXT ("一条轨道一根条带，最后一根是总输出（")
                           + juce::String (mixerTracks) + NI_TXT (" 轨 → ")
                           + juce::String (mixerStrips) + NI_TXT (" 根）"));

                auto stripsLaidOut = true;
                auto rowsLaidOut = true;
                auto mouseReachesStrips = true;

                using StripPart = MixerWindow::Strip::Part;

                auto stripAt = [mixer] (int index) { return mixer->getStripComponentForTest (index); };

                for (int i = 0; i < mixerStrips; ++i)
                {
                    auto* strip = stripAt (i);
                    const auto fader = mixer->getPartBoundsForTest (i, StripPart::fader);
                    const auto meter = mixer->getPartBoundsForTest (i, StripPart::meter);

                    stripsLaidOut = stripsLaidOut && strip != nullptr
                                        && ! strip->getBounds().isEmpty()
                                        && ! fader.isEmpty() && ! meter.isEmpty();

                    if (i < mixerTracks)
                        rowsLaidOut = rowsLaidOut
                                          && ! mixer->getPartBoundsForTest (i, StripPart::instrument).isEmpty()
                                          && ! mixer->getPartBoundsForTest (i, StripPart::pan).isEmpty()
                                          && ! mixer->getPartBoundsForTest (i, StripPart::mute).isEmpty()
                                          && ! mixer->getPartBoundsForTest (i, StripPart::solo).isEmpty();

                    if (mixer->isMasterStripForTest (i) != (i == mixerTracks))
                        stripsLaidOut = false;

                    // The thing the user hit: does a click on the fader land on the
                    // strip, or does the scrolling container swallow it? The strips
                    // used to be painted by hand behind a viewport that took every
                    // click - which is why nothing on them answered the mouse.
                    if (strip != nullptr && ! fader.isEmpty())
                    {
                        const auto inContent = strip->getBounds().getPosition() + fader.getCentre();

                        mouseReachesStrips = mouseReachesStrips
                                                 && mixer->content->getComponentAt (inContent) == strip;
                    }
                }

                check (stripsLaidOut,
                       NI_TXT ("每根条带都是真实组件，推子与电平表都有真实尺寸"));
                check (rowsLaidOut, NI_TXT ("轨道条带上的乐器 / 声相 / M / S 也排得下"));
                check (mouseReachesStrips,
                       NI_TXT ("点推子落在条带自己身上（不是被滚动容器吃掉）"));
                check (mixer->getStripTrackIdForTest (0) == mixerTrackA
                           && mixer->getStripTrackIdForTest (1) == mixerTrackB,
                       NI_TXT ("条带按音轨栏的顺序排"));

                // ---- the mouse: real press / drag / release on the strip -------
                auto clickAt = [&makeMouseEvent] (juce::Component& strip, juce::Point<int> at)
                {
                    const auto position = at.toFloat();
                    strip.mouseDown (makeMouseEvent (strip, position, position));
                    strip.mouseUp (makeMouseEvent (strip, position, position));
                };

                auto dragTo = [&makeMouseEvent] (juce::Component& strip, juce::Point<int> from,
                                                 juce::Point<int> to)
                {
                    strip.mouseDown (makeMouseEvent (strip, from.toFloat(), from.toFloat()));
                    strip.mouseDrag (makeMouseEvent (strip, to.toFloat(), from.toFloat()));
                    strip.mouseUp (makeMouseEvent (strip, to.toFloat(), from.toFloat()));
                };

                auto* firstStrip = stripAt (0);

                check (firstStrip != nullptr, NI_TXT ("第一根条带拿得到"));

                // ---- clicking a strip is what selects the track ---------------
                appState.selectTrack (mixerTrackB);
                clickAt (*firstStrip, mixer->getPartBoundsForTest (0, StripPart::name).getCentre());
                pump();

                check (appState.selectedTrackId == mixerTrackA,
                       NI_TXT ("点第一根条带，选中的轨道跟着过去"));

                // ---- the fader: one drag, one undo step -----------------------
                const auto faderBounds = mixer->getPartBoundsForTest (0, StripPart::fader);
                const auto volumeBeforeDrag = document.getProject().findTrack (mixerTrackA)->volumeDb;

                dragTo (*firstStrip, faderBounds.getCentre(),
                        { faderBounds.getCentreX(),
                          faderBounds.getCentreY() + faderBounds.getHeight() / 4 });
                pump();

                const auto volumeAfterDrag = document.getProject().findTrack (mixerTrackA)->volumeDb;

                check (volumeAfterDrag < volumeBeforeDrag - 10.0f,
                       NI_TXT ("把推子往下拖，轨道音量真的下去了（")
                           + juce::String (volumeAfterDrag, 1) + NI_TXT (" dB）"));
                check (mixer->getFaderValueDbForTest (0) == volumeAfterDrag,
                       NI_TXT ("条带上读到的就是工程里的那一份"));
                check (document.getUndoDescription() == NI_TXT ("轨道音量"),
                       NI_TXT ("这次拖动记成一条「轨道音量」（")
                           + document.getUndoDescription() + NI_TXT ("）"));

                document.undo();
                pump();

                check (std::abs (document.getProject().findTrack (mixerTrackA)->volumeDb
                                     - volumeBeforeDrag) < 1.0e-4f,
                       NI_TXT ("一次 Ctrl+Z 就把整次拖动收了回去"));

                // ---- the pan slider -------------------------------------------
                const auto panBounds = mixer->getPartBoundsForTest (0, StripPart::pan);

                dragTo (*firstStrip, panBounds.getCentre(),
                        { panBounds.getX() + 1, panBounds.getCentreY() });
                pump();

                check (document.getProject().findTrack (mixerTrackA)->pan < -0.8f,
                       NI_TXT ("把声相条拖到最左，声像真的过去了（")
                           + juce::String (document.getProject().findTrack (mixerTrackA)->pan, 2)
                           + NI_TXT ("）"));

                document.undo();
                pump();

                // ---- mute / solo ---------------------------------------------
                clickAt (*firstStrip, mixer->getPartBoundsForTest (0, StripPart::mute).getCentre());
                pump();
                check (document.getProject().findTrack (mixerTrackA)->muted,
                       NI_TXT ("点 M 就静音（走的是真鼠标）"));
                document.undo();
                pump();
                check (! document.getProject().findTrack (mixerTrackA)->muted,
                       NI_TXT ("撤销一次就不静音了"));

                auto* secondStrip = stripAt (1);
                clickAt (*secondStrip, mixer->getPartBoundsForTest (1, StripPart::solo).getCentre());
                pump();
                check (document.getProject().findTrack (mixerTrackB)->soloed,
                       NI_TXT ("点第二根的 S 就独奏"));
                document.undo();
                pump();

                // ---- the insert chain ----------------------------------------
                check (mixer->getInsertRowCountForTest (0) == 2,
                       NI_TXT ("条带里画出了这条轨道的两个效果器"));
                check (mixer->getInsertRowTextForTest (0, 0) == NI_TXT ("效果一")
                           && mixer->getInsertRowTextForTest (0, 1) == NI_TXT ("效果二"),
                       NI_TXT ("行上的名字就是链里的名字"));
                check (! mixer->getInsertRowBoundsForTest (0, 0).isEmpty(),
                       NI_TXT ("效果器行有真实尺寸（点得到）"));

                // ---- the bypass button at the end of a row --------------------
                const auto bypassBounds = mixer->getInsertBypassBoundsForTest (0, 0);

                check (! bypassBounds.isEmpty()
                           && mixer->getInsertRowBoundsForTest (0, 0).contains (bypassBounds),
                       NI_TXT ("效果器行右边有一个旁通按钮"));
                check (! document.getProject().findTrack (mixerTrackA)->effects.front().bypass,
                       NI_TXT ("效果器一开始是在链上的"));

                clickAt (*firstStrip, bypassBounds.getCentre());
                pump();

                check (document.getProject().findTrack (mixerTrackA)->effects.front().bypass,
                       NI_TXT ("点旁通按钮真的把它旁路了"));
                check (document.getUndoDescription() == NI_TXT ("旁路效果器"),
                       NI_TXT ("旁路记成一条「旁路效果器」（")
                           + document.getUndoDescription() + NI_TXT ("）"));

                clickAt (*firstStrip, bypassBounds.getCentre());
                pump();

                check (! document.getProject().findTrack (mixerTrackA)->effects.front().bypass,
                       NI_TXT ("再点一下又回到链上"));

                mixer->performInsertMenuCommand (mixerTrackA, 1, 1);   // 上移
                pump();

                check (document.getProject().findTrack (mixerTrackA)->effects.front().name
                           == NI_TXT ("效果二"),
                       NI_TXT ("右键菜单的「上移」真的换了顺序"));

                mixer->performInsertMenuCommand (mixerTrackA, 0, 3);   // 删除
                pump();

                check (mixer->getInsertRowCountForTest (0) == 1,
                       NI_TXT ("右键菜单的「删除」真的拿掉了一格"));

                // ---- the master fader ----------------------------------------
                const auto masterIndex = mixerStrips - 1;
                auto* masterStrip = stripAt (masterIndex);
                const auto masterFader = mixer->getPartBoundsForTest (masterIndex, StripPart::fader);

                check (masterStrip != nullptr && mixer->isMasterStripForTest (masterIndex),
                       NI_TXT ("最后一根是总输出条"));
                check (std::abs (document.getProject().masterVolumeDb) < 1.0e-4f,
                       NI_TXT ("总输出开局是 0 dB"));

                dragTo (*masterStrip, masterFader.getCentre(),
                        { masterFader.getCentreX(),
                          masterFader.getCentreY() - masterFader.getHeight() / 4 });
                pump();

                const auto masterAfterDrag = document.getProject().masterVolumeDb;

                check (masterAfterDrag > -14.0f && masterAfterDrag < -6.0f,
                       NI_TXT ("总输出推子写进了工程（") + juce::String (masterAfterDrag, 1)
                           + NI_TXT (" dB）"));
                check (std::abs (mixer->getFaderValueDbForTest (masterIndex) - masterAfterDrag)
                           < 1.0e-4f,
                       NI_TXT ("总输出条上读到的就是工程的 masterVolumeDb"));

                document.undo();
                pump();

                check (std::abs (document.getProject().masterVolumeDb) < 1.0e-4f,
                       NI_TXT ("一次 Ctrl+Z 把总输出推子收了回去"));

                // ---- the scales -------------------------------------------------
                const auto faderScale = mixer->getFaderScaleLabelsForTest (0);

                check (faderScale.size() >= 5 && faderScale[0] == "+6"
                           && faderScale.contains ("0")
                           && faderScale[faderScale.size() - 1] == "-60",
                       NI_TXT ("推子上有 dB 刻度（") + faderScale.joinIntoString (" / ")
                           + NI_TXT ("）"));

                const auto faderTop = mixer->getFaderTickYForTest (0, 6.0);
                const auto faderUnity = mixer->getFaderTickYForTest (0, 0.0);
                const auto faderBottom = mixer->getFaderTickYForTest (0, -60.0);

                check (std::abs (faderTop - faderBounds.getY()) <= 1
                           && std::abs (faderBottom - faderBounds.getBottom()) <= 1
                           && faderTop < faderUnity && faderUnity < faderBottom,
                       NI_TXT ("刻度就落在推子的行程上（+6 在顶、-60 在底、0 在中间）"));

                const auto meterBounds = mixer->getPartBoundsForTest (0, StripPart::meter);
                const auto meterTop = mixer->getMeterTickYForTest (0, 0.0);
                const auto meterHalf = mixer->getMeterTickYForTest (0, -6.0);

                check (std::abs (meterTop - meterBounds.getY()) <= 1
                           && std::abs (meterHalf
                                        - (meterBounds.getY() + meterBounds.getHeight() / 2)) <= 2,
                       NI_TXT ("电平表的刻度按幅度算：0 dB 在顶、-6 dB 正好在一半（")
                           + juce::String (meterHalf) + NI_TXT (" vs ")
                           + juce::String (meterBounds.getY() + meterBounds.getHeight() / 2)
                           + NI_TXT ("）"));

                // ---- the meter's colours change with the level -----------------
                const auto quietColour = MixerWindow::getMeterColourForTest (-40.0);
                const auto midColour = MixerWindow::getMeterColourForTest (-10.0);
                const auto hotColour = MixerWindow::getMeterColourForTest (0.0);

                check (quietColour.getGreen() > quietColour.getRed() + 60
                           && midColour.getRed() > quietColour.getRed() + 60
                           && hotColour.getRed() > midColour.getRed() + 20
                           && hotColour.getGreen() < midColour.getGreen(),
                       NI_TXT ("电平表的颜色随电平走：低处绿 → 中间黄 → 顶红（")
                           + quietColour.toDisplayString (false) + " / "
                           + midColour.toDisplayString (false) + " / "
                           + hotColour.toDisplayString (false) + NI_TXT ("）"));

                const auto barWidth = mixer->getPartBoundsForTest (0, StripPart::meter).getWidth();
                const auto faderWidth = mixer->getPartBoundsForTest (0, StripPart::fader).getWidth();

                check (barWidth > faderWidth,
                       NI_TXT ("电平表比推子宽（") + juce::String (barWidth) + NI_TXT (" vs ")
                           + juce::String (faderWidth) + NI_TXT ("）"));

                check (writeImage (snapshot (*mixer),
                                   outputDirectory.getChildFile ("40-mixer.png")),
                       NI_TXT ("混音台快照"));
            }
        }

        beginTest (NI_TXT ("播放时电平表动起来，左右两条各读自己的声道"));

        {
            auto* mixer = component.getMixerWindowForTest();
            auto& mixerAudio = component.getAudioEngine();

            check (mixer != nullptr, NI_TXT ("混音台还开着"));

            if (mixer != nullptr)
            {
                const auto meterTrackId = document.getProject().tracks.front().id;

                document.applyEdit ("selftest mixer tone", [meterTrackId] (Project& project)
                {
                    // A loop left over from an earlier section would move the
                    // transport past this note the moment play starts.
                    project.loop.enabled = false;

                    if (auto* track = project.findTrack (meterTrackId))
                    {
                        Note note;
                        note.id = project.nextNoteId++;
                        note.startTick = 0;
                        note.durationTicks = kTicksPerQuarter * 8;
                        note.frequencyHz = 440.0;
                        note.velocity = 110;
                        track->clips.front().notes = { note };
                        track->muted = false;
                        track->soloed = false;
                        track->pan = 0.0f;
                    }
                });
                pump();

                // The change listener marks the schedule dirty and the 30 Hz timer
                // rebuilds it; asking directly keeps this from depending on timing.
                mixerAudio.rebuildSchedule();
                mixerAudio.returnToZero();
                mixerAudio.togglePlay();
                pump();

                check (mixerAudio.isPlaying(), NI_TXT ("走带转起来了"));

                // What the engine is about to play: two tracks, and a note on the
                // first one. Without this the meters below would only say "nothing
                // came out" without saying whether anything went in.
                const auto meterSchedule = buildSchedule (document.getProject());

                check (meterSchedule.tracks.size() == 2
                           && ! meterSchedule.tracks.front().events.empty(),
                       NI_TXT ("调度里有 ") + juce::String ((int) meterSchedule.tracks.size())
                           + NI_TXT (" 条轨道 / ")
                           + juce::String (meterSchedule.tracks.empty()
                                               ? 0 : (int) meterSchedule.tracks.front().events.size())
                           + NI_TXT (" 个音符事件"));

                // The device callback does this every block. The offscreen run
                // cannot count on a device being open, so the same call is made by
                // hand - the meters read whatever the engine published.
                auto renderAndTick = [&mixerAudio, mixer] (int blocks)
                {
                    for (int i = 0; i < blocks; ++i)
                    {
                        mixerAudio.renderBlockForTest (512);
                        mixer->tickMetersForTest();
                    }
                };

                renderAndTick (8);

                const auto centred = mixer->getMeterLevelsForTest (0);

                check (centred.x > 0.001f && centred.y > 0.001f,
                       NI_TXT ("声像居中时左右两条都有读数（")
                           + juce::String (centred.x, 4) + " / " + juce::String (centred.y, 4)
                           + NI_TXT ("）"));

                const auto masterLevels = mixer->getMeterLevelsForTest (mixer->getStripCountForTest() - 1);

                check (masterLevels.x > 0.001f && masterLevels.y > 0.001f,
                       NI_TXT ("总输出的左右两条也都有读数（")
                           + juce::String (masterLevels.x, 4) + " / "
                           + juce::String (masterLevels.y, 4) + NI_TXT ("）"));

                check (writeImage (snapshot (*mixer),
                                   outputDirectory.getChildFile ("41-mixer-playing.png")),
                       NI_TXT ("播放中的混音台快照"));

                // ---- hard left: the right bar has to fall away -----------------
                document.applyEdit ("selftest mixer pan", [meterTrackId] (Project& project)
                {
                    if (auto* track = project.findTrack (meterTrackId))
                        track->pan = -1.0f;
                });
                pump();
                mixerAudio.rebuildSchedule();

                // Two seconds of ticks: long enough for the right bar to fall to
                // nothing at 20 dB/s while the left one keeps being refilled. (A
                // meter that never falls is not a meter - the bar is not just the
                // last block's peak.)
                renderAndTick (60);

                const auto panned = mixer->getMeterLevelsForTest (0);

                check (panned.x > 0.05f && panned.y < 0.01f,
                       NI_TXT ("声像全左时左边还在、右边落下去（")
                           + juce::String (panned.x, 4) + " / " + juce::String (panned.y, 4)
                           + NI_TXT ("）"));

                document.undo();          // the pan goes back to the centre
                pump();

                mixerAudio.togglePlay();
                pump();

                check (! mixerAudio.isPlaying(), NI_TXT ("再按一次就停下来"));

                // Nothing is coming in any more, so the bars let go again.
                renderAndTick (40);

                const auto quiet = mixer->getMeterLevelsForTest (0);

                check (quiet.x < 0.2f && quiet.y < 0.2f,
                       NI_TXT ("停下来之后电平条落回去了（")
                           + juce::String (quiet.x, 4) + " / " + juce::String (quiet.y, 4)
                           + NI_TXT ("）"));

                // ---- 再按播放会把上一次停止留下的尾巴掐断 -------------------
                // 从停着开始播放 = 新的一遍，上一次的尾巴要掐断；正在播的时候
                // 再点播放不是重开，所以什么也不掐。引擎里那个"掐断了几次"的
                // 计数就是这件事的照妖镜（消息线程置标记、音频线程执行）。
                const auto cutsBeforePlay = mixerAudio.getTailCutCountForTest();

                mixerAudio.play();
                pump();
                mixerAudio.renderBlockForTest (512);

                check (mixerAudio.getTailCutCountForTest() == cutsBeforePlay + 1,
                       NI_TXT ("从停着再按播放，尾巴被掐断一次（")
                           + juce::String (cutsBeforePlay) + NI_TXT (" → ")
                           + juce::String (mixerAudio.getTailCutCountForTest()) + NI_TXT ("）"));

                mixerAudio.play();          // already running: not a restart
                pump();
                mixerAudio.renderBlockForTest (512);

                check (mixerAudio.getTailCutCountForTest() == cutsBeforePlay + 1,
                       NI_TXT ("正在播的时候再点播放不掐断任何东西（还是 ")
                           + juce::String (mixerAudio.getTailCutCountForTest()) + NI_TXT ("）"));

                mixerAudio.stop();
                pump();
                mixerAudio.renderBlockForTest (512);

                check (mixerAudio.getTailCutCountForTest() == cutsBeforePlay + 1,
                       NI_TXT ("停止本身不掐断（尾巴留着响）"));
            }
        }

        // ---- 控制器栏：力度竖条 + CC 折线 -----------------------------------

        beginTest (NI_TXT ("控制器栏：力度竖条、可画的 CC 折线、可拖高可折叠"));

        {
            juce::int64 curveTrackId = 0, curveClipId = 0, curveNoteA = 0, curveNoteB = 0;

            document.applyEdit ("selftest controller lane setup",
                                [&curveTrackId, &curveClipId, &curveNoteA, &curveNoteB] (Project& project)
            {
                project.tracks.clear();
                project.referenceLines.clear();
                project.nextTrackId = 1;
                project.view.controllerLane = {};

                curveTrackId = addTrack (project, TrackKind::synth);

                if (auto* track = project.findTrack (curveTrackId))
                {
                    auto& clip = track->clips.front();
                    clip.notes.clear();
                    clip.startTick = 0;
                    clip.lengthTicks = kTicksPerQuarter * 4;
                    clip.loopLengthTicks = clip.lengthTicks;
                    clip.looped = false;
                    curveClipId = clip.id;

                    Note first;
                    first.id = project.nextNoteId++;
                    first.startTick = 0;
                    first.durationTicks = kTicksPerQuarter / 2;
                    first.frequencyHz = 220.0;
                    first.velocity = 100;
                    curveNoteA = first.id;
                    clip.notes.push_back (first);

                    Note second = first;
                    second.id = project.nextNoteId++;
                    second.startTick = kTicksPerQuarter;
                    second.velocity = 40;
                    curveNoteB = second.id;
                    clip.notes.push_back (second);
                }
            });
            pump();

            appState.selectTrack (curveTrackId);
            appState.selectClip (curveClipId);
            appState.setEditorOpen (true);
            appState.setLaneSignal (true, appState.laneController);
            pump();

            auto& lanePanel = component.getEditorPanel();
            auto& lane = lanePanel.getControllerLane();

            check (lanePanel.isControllerLaneVisibleForTest() && lane.getHeight() > 0,
                   NI_TXT ("控制器栏排在编辑栏底部（高 ") + juce::String (lane.getHeight())
                       + NI_TXT (" px）"));
            check (! lane.getPlotBoundsForTest().isEmpty(),
                   NI_TXT ("画布有尺寸（") + lane.getPlotBoundsForTest().toString() + NI_TXT ("）"));
            check (! lanePanel.getControllerLaneToggleBoundsForTest().isEmpty()
                       && lanePanel.isControllerLaneToggleVisibleForTest(),
                   NI_TXT ("折叠箭头有尺寸也真的可见"));

            // ---- 与画布同一列（两种模式都要对） -----------------------------
            // The lane shares the canvas's horizontal mapping, so the playhead lines
            // the two draw have to land in the same column. In the piano window the
            // canvas reserves a key column, and this used to be applied twice - which
            // is what "the editor sometimes grows extra red lines" looked like: the
            // lane's playhead sat 88 px away from the canvas's.
            auto laneLinesUpWithCanvas = [&] (const juce::String& what)
            {
                const music::TempoMap tempoMap (document.getProject().tempoMarkers);
                const auto tick = tempoMap.tickAtSeconds (component.getAudioEngine().getPositionSeconds());
                auto& grid = lanePanel.getGrid();
                const auto gridX = (float) grid.getX() + grid.pointFor (440.0, tick).x;
                const auto laneX = (float) lane.getX() + (float) lane.getXForTickForTest ((double) tick);

                check (std::abs (gridX - laneX) <= 1.0f,
                       what + NI_TXT ("（画布 ") + juce::String (gridX, 1)
                           + NI_TXT (" / 控制器栏 ") + juce::String (laneX, 1) + NI_TXT ("）"));
            };

            laneLinesUpWithCanvas (NI_TXT ("参考线模式下，控制器栏的播放头与画布同一列"));

            lanePanel.selectEditorModeForTest (2);      // 钢琴窗模式
            pump();
            laneLinesUpWithCanvas (NI_TXT ("钢琴窗模式下也对得上（键盘列只让一次）"));

            lanePanel.selectEditorModeForTest (1);      // back to 参考线模式
            pump();

            // ---- 力度：每个音符一根竖条 -----------------------------------
            auto bars = lane.getVelocityBarsForTest();

            check (bars.size() == 2,
                   NI_TXT ("两个音符画两根竖条（") + juce::String ((int) bars.size()) + NI_TXT ("）"));
            check (bars.size() == 2 && bars[0].getHeight() > bars[1].getHeight(),
                   NI_TXT ("力度大的那根更高（") + juce::String (bars[0].getHeight())
                       + NI_TXT (" vs ") + juce::String (bars.size() > 1 ? bars[1].getHeight() : 0)
                       + NI_TXT ("）"));

            auto noteVelocity = [&document] (juce::int64 noteId)
            {
                if (const auto* clip = document.getProject().findClip (0))
                    juce::ignoreUnused (clip);

                for (const auto& track : document.getProject().tracks)
                    for (const auto& clip : track.clips)
                        for (const auto& note : clip.notes)
                            if (note.id == noteId)
                                return note.velocity;

                return -1;
            };

            auto dragOnLane = [&makeMouseEvent, &lane] (juce::Point<int> from, juce::Point<int> to,
                                                        bool shift = false)
            {
                const auto mods = shift ? juce::ModifierKeys().withFlags (juce::ModifierKeys::shiftModifier)
                                        : juce::ModifierKeys();
                lane.mouseDown (makeMouseEvent (lane, from.toFloat(), from.toFloat(), mods));
                lane.mouseDrag (makeMouseEvent (lane, to.toFloat(), from.toFloat(), mods));
                lane.mouseUp (makeMouseEvent (lane, to.toFloat(), from.toFloat(), mods));
            };

            const auto velocityBefore = noteVelocity (curveNoteA);

            dragOnLane (bars[0].getCentre(), bars[0].getCentre() + juce::Point<int> (0, 30));
            pump();

            check (noteVelocity (curveNoteA) < velocityBefore,
                   NI_TXT ("往下拖竖条，音符力度真的降了（") + juce::String (velocityBefore)
                       + NI_TXT (" → ") + juce::String (noteVelocity (curveNoteA)) + NI_TXT ("）"));
            check (document.getUndoDescription() == NI_TXT ("调整力度"),
                   NI_TXT ("这次拖动记成一条「调整力度」（") + document.getUndoDescription()
                       + NI_TXT ("）"));

            document.undo();
            pump();

            check (noteVelocity (curveNoteA) == velocityBefore,
                   NI_TXT ("一次撤销把力度收回原值"));

            // Selecting several notes makes one drag move them together.
            appState.setSelectedNotes ({ curveNoteA, curveNoteB });
            pump();

            const auto baseA = noteVelocity (curveNoteA);
            const auto baseB = noteVelocity (curveNoteB);

            bars = lane.getVelocityBarsForTest();
            dragOnLane (bars[0].getCentre(), bars[0].getCentre() + juce::Point<int> (0, 21));
            pump();

            check (noteVelocity (curveNoteA) < baseA && noteVelocity (curveNoteB) < baseB,
                   NI_TXT ("多选时一起改（") + juce::String (baseA) + "/" + juce::String (baseB)
                       + NI_TXT (" → ") + juce::String (noteVelocity (curveNoteA)) + "/"
                       + juce::String (noteVelocity (curveNoteB)) + NI_TXT ("）"));

            document.undo();
            pump();
            appState.clearNotes();
            pump();

            // ---- CC：折线 --------------------------------------------------
            lane.selectSignalForTest (false, 11);
            pump();

            check (lane.getSignalComboIdForTest() == 100 + 11,
                   NI_TXT ("下拉切到 CC11 表情（id ") + juce::String (lane.getSignalComboIdForTest())
                       + NI_TXT ("）"));
            check (lane.getCurvePointsForTest().empty(),
                   NI_TXT ("新曲线一个折点都没有"));

            const auto plot = lane.getPlotBoundsForTest();
            const auto pencilPoint = juce::Point<int> (plot.getX() + 60, plot.getY() + 25);

            lane.mouseDown (makeMouseEvent (lane, pencilPoint.toFloat(), pencilPoint.toFloat()));
            lane.mouseUp (makeMouseEvent (lane, pencilPoint.toFloat(), pencilPoint.toFloat()));
            pump();

            auto curvePoints = lane.getCurvePointsForTest();

            check (curvePoints.size() == 1,
                   NI_TXT ("按一下就画出一个折点（") + juce::String ((int) curvePoints.size())
                       + NI_TXT ("）"));
            check (document.getUndoDescription() == NI_TXT ("画 CC 曲线"),
                   NI_TXT ("一次手势一条撤销（") + document.getUndoDescription() + NI_TXT ("）"));

            // The tick landed on the editor's snap grid (1 beat = 960 ticks).
            {
                const auto* curve = controllers::findLane (
                    *document.getProject().findClip (curveClipId), 11);

                check (curve != nullptr && ! curve->points.empty()
                           && curve->points.front().tick % kTicksPerQuarter == 0,
                       NI_TXT ("落点按吸附量化（tick ")
                           + juce::String (curve != nullptr && ! curve->points.empty()
                                               ? curve->points.front().tick : -1)
                           + NI_TXT ("）"));
            }

            // Dragging on empty space keeps painting the line.
            dragOnLane ({ plot.getX() + 200, plot.getY() + 40 },
                        { plot.getX() + 320, plot.getY() + 70 });
            pump();

            curvePoints = lane.getCurvePointsForTest();

            check (curvePoints.size() >= 2,
                   NI_TXT ("按住拖动会一路加折点（") + juce::String ((int) curvePoints.size())
                       + NI_TXT ("）"));

            // Dragging a knee moves it.
            const auto kneeBefore = lane.getCurvePointsForTest().front();

            dragOnLane (kneeBefore, { kneeBefore.x, kneeBefore.y + 25 });
            pump();

            const auto kneeAfter = lane.getCurvePointsForTest().front();

            check (kneeAfter.y > kneeBefore.y,
                   NI_TXT ("拖折点把它拖下去了（") + juce::String (kneeBefore.y) + NI_TXT (" → ")
                       + juce::String (kneeAfter.y) + NI_TXT ("）"));

            // Right-clicking a knee deletes it.
            {
                const auto target = lane.getCurvePointsForTest().front();
                const auto mods = juce::ModifierKeys().withFlags (juce::ModifierKeys::popupMenuClickModifier);
                const auto before = lane.getCurvePointsForTest().size();

                lane.mouseDown (makeMouseEvent (lane, target.toFloat(), target.toFloat(), mods));
                lane.mouseUp (makeMouseEvent (lane, target.toFloat(), target.toFloat(), mods));
                pump();

                check (lane.getCurvePointsForTest().size() == before - 1,
                       NI_TXT ("右键折点把它删了（") + juce::String ((int) before) + NI_TXT (" → ")
                           + juce::String ((int) lane.getCurvePointsForTest().size()) + NI_TXT ("）"));
            }

            // Shift-dragging draws one straight line between the two ends.
            {
                const auto before = lane.getCurvePointsForTest().size();

                dragOnLane ({ plot.getX() + 500, plot.getY() + 80 },
                            { plot.getX() + 700, plot.getY() + 20 }, true);
                pump();

                const auto after = lane.getCurvePointsForTest().size();
                const auto* curve = controllers::findLane (
                    *document.getProject().findClip (curveClipId), 11);

                check (curve != nullptr && curve->points.size() >= 2,
                       NI_TXT ("Shift 拖出一条直线（折点 ") + juce::String (before) + NI_TXT (" → ")
                           + juce::String ((int) after) + NI_TXT ("）"));
                check (document.getUndoDescription() == NI_TXT ("画 CC 直线"),
                       NI_TXT ("直线也记成一条撤销（") + document.getUndoDescription() + NI_TXT ("）"));
            }

            // ---- 自定义 CC 号，保留号被拒绝 --------------------------------
            lane.setCustomControllerForTest (20);
            pump();

            check (appState.laneController == 20 && ! appState.laneShowsVelocity,
                   NI_TXT ("自定义 CC 号生效（CC ") + juce::String (appState.laneController)
                       + NI_TXT ("）"));
            check (lane.getSignalComboTextForTest().contains ("20"),
                   NI_TXT ("下拉框显示得出来（") + lane.getSignalComboTextForTest() + NI_TXT ("）"));

            lane.setCustomControllerForTest (101);
            pump();

            check (appState.laneController == 20,
                   NI_TXT ("宿主自己用的 CC 号被拒绝（仍然是 CC ")
                       + juce::String (appState.laneController) + NI_TXT ("）"));

            lane.setCustomControllerForTest (11);
            pump();

            // The drawn curve reaches the engine's schedule.
            {
                const auto schedule = buildSchedule (document.getProject());
                const auto events = schedule.tracks.empty()
                                        ? 0 : (int) schedule.tracks.front().controllers.size();

                check (events > 0, NI_TXT ("画出来的曲线进了调度（") + juce::String (events)
                                       + NI_TXT (" 个 CC 事件）"));
            }

            // ---- 左右移动编辑栏时这一栏要跟着走 ---------------------------
            {
                lane.selectSignalForTest (true, appState.laneController);
                pump();

                // Start from a known place, so "what moved" is unambiguous.
                document.viewState().horizontalOffsetTicks = 0.0;
                lanePanel.refreshPlayhead();
                pump();

                const auto barsBefore = lane.getVelocityBarsForTest();
                const auto refreshesBefore = lane.getViewRefreshesForTest();
                const auto pixelsPerBeat = (int) std::lround (document.viewState().pixelsPerQuarter);

                // Scroll the editor one beat to the right, the way its scroll bar or
                // the wheel does, and run the panel's follow-up pass.
                document.viewState().horizontalOffsetTicks = (double) kTicksPerQuarter;
                lanePanel.refreshPlayhead();
                pump();

                const auto barsAfter = lane.getVelocityBarsForTest();

                // The second note is now where the first one was: the lane really did
                // move with the view (this is the half that used to stay stale).
                check (barsBefore.size() == 2 && barsAfter.size() == 1
                           && barsAfter.front().getX() == barsBefore[1].getX() - pixelsPerBeat,
                       NI_TXT ("编辑栏向右移一拍，竖条跟着左移 ") + juce::String (pixelsPerBeat)
                           + NI_TXT (" px（")
                           + juce::String (barsBefore.size() > 1 ? barsBefore[1].getX() : -1)
                           + NI_TXT (" → ")
                           + juce::String (barsAfter.empty() ? -1 : barsAfter.front().getX())
                           + NI_TXT ("）"));
                check (lane.getViewRefreshesForTest() > refreshesBefore,
                       NI_TXT ("而且这一栏走的是「跟着视图重画一遍」那条路（")
                           + juce::String (refreshesBefore) + NI_TXT (" → ")
                           + juce::String (lane.getViewRefreshesForTest()) + NI_TXT ("）"));

                // Back where it was, so the snapshot below is the usual view.
                document.viewState().horizontalOffsetTicks = 0.0;
                lanePanel.refreshPlayhead();
                pump();
            }

            check (writeImage (snapshot (component),
                               outputDirectory.getChildFile ("43-controller-lane-cc.png")),
                   NI_TXT ("画着 CC 折线的控制器栏快照"));

            // ---- 高度与折叠 ------------------------------------------------
            const auto openedHeight = lanePanel.getControllerLaneHeightForTest();

            lanePanel.toggleControllerLaneForTest();
            pump();

            check (lanePanel.isControllerLaneCollapsedForTest()
                       && ! lanePanel.isControllerLaneVisibleForTest(),
                   NI_TXT ("点箭头折成细栏"));
            check (lanePanel.getControllerLaneHeightForTest() == kCollapsedPanelWidth,
                   NI_TXT ("细栏 18 px（") + juce::String (lanePanel.getControllerLaneHeightForTest())
                       + NI_TXT ("）"));
            check (! lanePanel.getControllerLaneToggleBoundsForTest().isEmpty(),
                   NI_TXT ("细栏里的箭头点得到"));

            lanePanel.toggleControllerLaneForTest();
            pump();

            check (! lanePanel.isControllerLaneCollapsedForTest()
                       && lanePanel.isControllerLaneVisibleForTest(),
                   NI_TXT ("再点一下展开"));
            check (lanePanel.getControllerLaneHeightForTest() == openedHeight,
                   NI_TXT ("展开回到折叠前的高度（")
                       + juce::String (lanePanel.getControllerLaneHeightForTest()) + NI_TXT ("）"));

            check (! lanePanel.getControllerLaneResizerBoundsForTest().isEmpty(),
                   NI_TXT ("上边缘的拖动条有尺寸"));

            lanePanel.dragControllerLaneForTest (-40);      // dragging upwards is taller
            pump();

            check (lanePanel.getControllerLaneHeightForTest() == openedHeight + 40,
                   NI_TXT ("往上拖 40 px 就高 40 px（")
                       + juce::String (lanePanel.getControllerLaneHeightForTest()) + NI_TXT ("）"));
            check (document.viewState().controllerLane.size == openedHeight + 40,
                   NI_TXT ("高度进了工程状态，不是只画在屏幕上"));

            check (writeImage (snapshot (component),
                               outputDirectory.getChildFile ("42-controller-lane.png")),
                   NI_TXT ("控制器栏（力度）快照"));

            // ---- 随工程保存 -------------------------------------------------
            {
                const auto json = projectToJson (document.getProject());
                Project curveReloaded;
                juce::String curveError;

                check (projectFromJson (json, curveReloaded, curveError), NI_TXT ("存盘再读回"));
                check (curveReloaded.view.controllerLane.size == openedHeight + 40,
                       NI_TXT ("控制器栏的高度跟着工程走"));
                check (! curveReloaded.tracks.empty()
                           && ! curveReloaded.tracks.front().clips.front().controllerCurves.empty(),
                       NI_TXT ("画出来的曲线也在工程里"));

                auto stripped = juce::JSON::parse (json);

                if (auto* root = stripped.getDynamicObject())
                    if (auto* view = root->getProperty ("view").getDynamicObject())
                    {
                        view->removeProperty ("controllerLaneSize");
                        view->removeProperty ("controllerLaneCollapsed");
                    }

                Project old;
                check (projectFromJson (juce::JSON::toString (stripped, false), old, curveError),
                       NI_TXT ("缺字段的旧工程照常打开"));
                check (old.view.controllerLane.size == 0
                           && ! old.view.controllerLane.collapsed,
                       NI_TXT ("旧工程：控制器栏用默认高度、展开着"));
            }

            // Put the lane back the way it was found for the sections that follow.
            document.applyEdit ("selftest controller lane restore", [] (Project& project)
            {
                project.view.controllerLane = {};
            });
            appState.setLaneSignal (true, 11);
            pump();
        }

        beginTest (NI_TXT ("空格键在所有窗口里都控制走带，例外只有文本框与插件界面"));

        {
            auto& keyAudio = component.getAudioEngine();
            const juce::KeyPress spaceKey (juce::KeyPress::spaceKey);

            // Presses it where a window is listening, checks the transport really
            // moved, and puts the transport back where it was found.
            auto runSpaceCheck = [&check, &component, &pump, &keyAudio, &spaceKey]
                                 (juce::Component* where, const juce::String& what)
            {
                const auto wasPlaying = keyAudio.isPlaying();

                check (component.sendKeyPressForTest (spaceKey, where),
                       what + NI_TXT ("：空格被接住了"));
                pump();

                const auto toggled = keyAudio.isPlaying() != wasPlaying;

                check (toggled, what + NI_TXT ("：空格切换了走带"));

                if (toggled)
                {
                    component.sendKeyPressForTest (spaceKey, where);
                    pump();
                }
            };

            runSpaceCheck (&component, NI_TXT ("主窗口"));

            if (auto* mixer = component.getMixerWindowForTest(); mixer != nullptr)
                runSpaceCheck (mixer, NI_TXT ("混音台"));

            // The sound window, opened the way the editor opens it.
            component.openSynthWindow (document.getProject().tracks.front().id);
            pump();

            if (auto* synth = component.getSynthWindowForTest(); synth != nullptr)
                runSpaceCheck (synth, NI_TXT ("音色窗口"));

            // A rename box keeps the space bar: that is typing, not transport.
            {
                auto& laneArea = component.getTrackArea();
                auto* header = laneArea.getLaneHeaderForTest (0);
                juce::TextEditor* nameBox = nullptr;

                if (header != nullptr)
                {
                    const auto nameCentre = laneArea.getLocalPoint (
                        header, header->getControlBoundsForTest (0).getCentre());

                    nameBox = dynamic_cast<juce::TextEditor*> (laneArea.getComponentAt (nameCentre));
                }

                check (nameBox != nullptr, NI_TXT ("找得到轨道名输入框"));

                if (nameBox != nullptr)
                {
                    // The editor never goes on screen in this run, so the desktop
                    // cannot be asked to focus the box - the rule is asked with the
                    // box handed in instead. It is the same call the real key press
                    // makes, with whatever has the keyboard as the argument.
                    check (! component.isTransportSpaceKeyForTest (spaceKey, nameBox),
                           NI_TXT ("改名时空格归文本框，不归走带"));
                    check (component.isTransportSpaceKeyForTest (spaceKey, &laneArea),
                           NI_TXT ("而在轨道栏空白处，空格还是走带"));
                }
            }

            // The plugin's own window: the window itself still drives the transport,
            // but a plugin that has taken the keyboard keeps the key - Kontakt and
            // Serum audition with it.
            if (testPlugin.exists())
            {
                auto& host = component.getPluginHost();

                juce::String scanError;
                host.scanFolderNow (testPlugin.getParentDirectory(), scanError);

                VstParams liveParams;
                liveParams.name = "HM Test Instrument";
                liveParams.pluginPath = testPlugin.getFullPathName();

                juce::int64 liveTrackId = 0;

                document.applyEdit ("selftest mixer vst track",
                                    [&liveTrackId, &liveParams] (Project& project)
                {
                    project.tracks.clear();
                    project.nextTrackId = 1;

                    liveTrackId = addTrack (project, TrackKind::vst);

                    if (auto* track = project.findTrack (liveTrackId))
                        track->vst = liveParams;
                });
                pump();

                host.requestInstrument (liveTrackId, liveParams);

                for (int attempt = 0; attempt < 150 && host.pluginFor (liveTrackId) == nullptr;
                     ++attempt)
                    pump();

                check (host.pluginFor (liveTrackId) != nullptr,
                       NI_TXT ("测试插件在宿主里起来了"));

                component.openSynthWindow (liveTrackId);
                pump();

                auto* pluginWin = component.getPluginWindowForTest();

                check (pluginWin != nullptr && pluginWin->isVisible(),
                       NI_TXT ("插件窗口开着"));

                if (pluginWin != nullptr && pluginWin->isVisible())
                {
                    runSpaceCheck (pluginWin, NI_TXT ("插件窗口本身"));

                    if (auto* pluginEditor = pluginWin->getEditorComponent();
                        pluginEditor != nullptr)
                    {
                        // The desktop only routes keys to a component that asks for
                        // the keyboard. The test plugin's own interface does not (the
                        // real ones do), so the test asks on its behalf and then
                        // checks the rule that keeps such keys away from HM.
                        pluginEditor->setWantsKeyboardFocus (true);
                        pluginEditor->grabKeyboardFocus();
                        pump();

                        const auto* focusNow = juce::Component::getCurrentlyFocusedComponent();
                        const auto focusIsInside = focusNow != nullptr
                                                       && (focusNow == pluginEditor
                                                           || pluginEditor->isParentOf (focusNow));

                        check (focusIsInside,
                               NI_TXT ("焦点落在插件界面里面（Kontakt / Serum 那类插件的常态）"));

                        if (focusIsInside)
                        {
                            const auto wasPlaying = keyAudio.isPlaying();

                            check (! component.isTransportSpaceKeyForTest (spaceKey, pluginEditor),
                                   NI_TXT ("插件界面里的空格不归 HM 的走带"));

                            // The plugin's own window handles the key first (JUCE's
                            // VST3 wrapper does), and either way the transport does
                            // not move.
                            component.sendKeyPressForTest (spaceKey, pluginEditor);
                            pump();

                            check (keyAudio.isPlaying() == wasPlaying,
                                   NI_TXT ("所以插件试听音色时走带不会跟着动"));
                        }
                    }
                }
            }
            else
            {
                log.add (NI_TXT ("      (没有 --vst3，插件窗口那一段跳过了)"));
            }
        }

        beginTest ("note head / tail pitch, tilt and one-attack links");

        {
            auto& pitchPanel = component.getEditorPanel();
            auto& pitchGrid = pitchPanel.getGrid();
            auto& pitchBar = pitchPanel.getNoteEditBar();

            juce::int64 pitchTrackId = 0;
            juce::int64 pitchClipId = 0;
            juce::int64 pitchFirstId = 0;
            juce::int64 pitchSecondId = 0;

            document.applyEdit ("selftest pitch shape setup", [&] (Project& project)
            {
                project.tracks.clear();
                project.referenceLines.clear();
                project.nextTrackId = 1;

                pitchTrackId = addTrack (project, TrackKind::synth);
                auto* track = project.findTrack (pitchTrackId);

                if (track == nullptr)
                    return;

                track->clips.clear();
                auto clip = makeDefaultClip (project);
                clip.startTick = 0;
                clip.lengthTicks = 19200;
                clip.loopLengthTicks = 19200;
                clip.notes.clear();

                Note a;
                a.id = project.nextNoteId++;
                a.startTick = 0;
                a.durationTicks = 960;
                a.frequencyHz = 220.0;
                a.tailFrequencyHz = 220.0;
                a.velocity = 100;

                Note b;
                b.id = project.nextNoteId++;
                b.startTick = 960;
                b.durationTicks = 960;
                b.frequencyHz = 330.0;
                b.tailFrequencyHz = 330.0;
                b.velocity = 100;

                pitchFirstId = a.id;
                pitchSecondId = b.id;
                clip.notes = { a, b };
                pitchClipId = clip.id;
                track->clips.push_back (std::move (clip));
            });

            appState.selectTrack (pitchTrackId);
            appState.selectClip (pitchClipId);
            appState.selectNote (pitchFirstId, false);
            appState.setEditorOpen (true);
            setBothTimelines (document, 140.0, 0.0);
            pitchPanel.selectSnapForTest (4);
            pump();

            const auto shapeBounds = pitchGrid.getNoteShapeBoundsForTest (pitchFirstId);
            const auto headHandle = pitchGrid.getHeadHandleBoundsForTest (pitchFirstId);
            const auto tailHandle = pitchGrid.getTailHandleBoundsForTest (pitchFirstId);

            check (! shapeBounds.isEmpty() && shapeBounds.contains (headHandle)
                       && shapeBounds.contains (tailHandle),
                   NI_TXT ("both hollow endpoint handles sit inside the note"));
            check (! pitchBar.getTailFrequencyBoundsForTest().isEmpty()
                       && ! pitchBar.getEndpointToggleBoundsForTest().isEmpty(),
                   NI_TXT ("the tail frequency field and head/tail ratio target are visible"));
            check (! pitchBar.isTailEndpointSelectedForTest(),
                   NI_TXT ("the ratio target starts on the head"));

            pitchBar.toggleEndpointForTest();
            pump();
            check (pitchBar.isTailEndpointSelectedForTest(),
                   NI_TXT ("the ratio target switches to the tail"));

            appState.selectNote (pitchSecondId, false);
            pump();
            check (pitchBar.isTailEndpointSelectedForTest(),
                   NI_TXT ("the head/tail target is inherited across notes"));

            appState.selectNote (pitchFirstId, false);
            pump();

            pitchBar.setTailFrequencyForTest (440.0);
            pump();

            const auto* tailEdited = document.findNote (pitchTrackId, pitchFirstId);
            check (tailEdited != nullptr
                       && std::abs (tailEdited->effectiveTailHz() - 440.0) < 1.0e-6,
                   NI_TXT ("the tail frequency field edits the note tail"));
            check (pitchBar.isShapeRowVisibleForTest()
                       && ! pitchBar.getCurveBoundsForTest().isEmpty()
                       && ! pitchBar.getTiltStartBoundsForTest().isEmpty()
                       && ! pitchBar.getTiltEndBoundsForTest().isEmpty(),
                   NI_TXT ("a non-horizontal note shows the floating shape row"));

            pitchBar.setPitchShapeForTest (0.5f, 0.2f, 0.8f);
            pump();

            const auto* shaped = document.findNote (pitchTrackId, pitchFirstId);
            check (shaped != nullptr
                       && std::abs (shaped->pitchCurve - 0.5f) < 1.0e-5f
                       && std::abs (shaped->tiltStart - 0.2f) < 1.0e-5f
                       && std::abs (shaped->tiltEnd - 0.8f) < 1.0e-5f,
                   NI_TXT ("curve and tilt percentages reach the selected notes"));

            const auto ratioBeforeBody = shaped->effectiveTailHz() / shaped->frequencyHz;
            const auto bodyPoint = pitchGrid.pointFor (
                music::pitchAt (*shaped, 0.5),
                pitchClipId == 0 ? shaped->startTick + shaped->durationTicks / 2
                                 : document.getProject().findClip (pitchClipId)->startTick
                                       + shaped->startTick + shaped->durationTicks / 2);
            const auto bodyTarget = bodyPoint.translated (0.0f, -24.0f);
            pitchGrid.mouseDown (makeMouseEvent (pitchGrid, bodyPoint, bodyPoint));
            pitchGrid.mouseDrag (makeMouseEvent (pitchGrid, bodyTarget, bodyPoint));
            pitchGrid.mouseUp (makeMouseEvent (pitchGrid, bodyTarget, bodyPoint));
            pump();

            const auto* bodyMoved = document.findNote (pitchTrackId, pitchFirstId);
            check (bodyMoved != nullptr
                       && std::abs (bodyMoved->effectiveTailHz()
                                        / bodyMoved->frequencyHz - ratioBeforeBody) < 1.0e-4,
                   NI_TXT ("moving the body preserves the head/tail interval"));

            const auto startMarker = pitchGrid.getTiltStartMarkerForTest (pitchFirstId);
            const auto markerFrom = juce::Point<float> (startMarker.getCentreX(),
                                                        startMarker.getY() + 1.0f);
            const auto markerTo = markerFrom.translated (36.0f, 0.0f);
            pitchGrid.mouseDown (makeMouseEvent (pitchGrid, markerFrom, markerFrom));
            pitchGrid.mouseDrag (makeMouseEvent (pitchGrid, markerTo, markerFrom));
            pitchGrid.mouseUp (makeMouseEvent (pitchGrid, markerTo, markerFrom));
            pump();

            const auto* markerMoved = document.findNote (pitchTrackId, pitchFirstId);
            check (markerMoved != nullptr && markerMoved->tiltStart > 0.2f,
                   NI_TXT ("the tilt-start marker drags on the beat grid"));
            check (writeImage (snapshot (component),
                               outputDirectory.getChildFile ("58-note-tilt.png")),
                   NI_TXT ("tilted note snapshot"));

            // Put the two notes back end-to-end, then drag A's tail handle onto
            // B's head handle. The join moves only A's tail and makes B inherit the
            // first attack.
            document.applyEdit ("selftest prepare link", [&] (Project& project)
            {
                if (auto* clip = project.findClip (pitchClipId))
                {
                    for (auto& note : clip->notes)
                    {
                        if (note.id == pitchFirstId)
                        {
                            note.startTick = 0;
                            note.durationTicks = 960;
                            note.frequencyHz = 220.0;
                            note.tailFrequencyHz = 330.0;
                        }
                        else if (note.id == pitchSecondId)
                        {
                            note.startTick = 960;
                            note.durationTicks = 960;
                            note.frequencyHz = 330.0;
                            note.tailFrequencyHz = 440.0;
                            note.tieFromNoteId = 0;
                        }
                    }
                }
            });
            pump();

            const auto linkFrom = pitchGrid.getTailHandleBoundsForTest (pitchFirstId).getCentre();
            const auto linkTo = pitchGrid.getHeadHandleBoundsForTest (pitchSecondId).getCentre();
            pitchGrid.mouseDown (makeMouseEvent (pitchGrid, linkFrom, linkFrom));
            pitchGrid.mouseDrag (makeMouseEvent (pitchGrid, linkTo, linkFrom));
            pitchGrid.mouseUp (makeMouseEvent (pitchGrid, linkTo, linkFrom));
            pump();

            const auto* linkedFirst = document.findNote (pitchTrackId, pitchFirstId);
            const auto* linkedSecond = document.findNote (pitchTrackId, pitchSecondId);
            check (linkedFirst != nullptr && linkedSecond != nullptr
                       && linkedSecond->tieFromNoteId == pitchFirstId
                       && linkedFirst->endTick() == linkedSecond->startTick,
                   NI_TXT ("tail-to-head dragging joins the two notes"));
            check (linkedFirst != nullptr && linkedSecond != nullptr
                       && std::abs (linkedFirst->effectiveTailHz()
                                        - linkedSecond->frequencyHz) < 1.0e-6,
                   NI_TXT ("the join also matches the pitch at the seam"));
            check (! pitchGrid.getLinkBadgeBoundsForTest (pitchSecondId).isEmpty(),
                   NI_TXT ("the connected note shows its inverted-triangle badge"));

            const auto linkedSchedule = buildSchedule (document.getProject());
            int linkedAttacks = 0;

            if (! linkedSchedule.tracks.empty())
                for (const auto& event : linkedSchedule.tracks.front().events)
                    if (event.isNoteOn && ! event.isPitch)
                        ++linkedAttacks;

            check (linkedAttacks == 1,
                   NI_TXT ("the connected pair has one note attack (got ")
                       + juce::String (linkedAttacks) + NI_TXT (")"));
            check (writeImage (snapshot (component),
                               outputDirectory.getChildFile ("59-note-link.png")),
                   NI_TXT ("connected notes snapshot"));

            const auto badge = pitchGrid.getLinkBadgeBoundsForTest (pitchSecondId);
            const auto keepFirst = *linkedFirst;
            const auto keepSecond = *linkedSecond;
            const auto rightClick = juce::ModifierKeys().withFlags (
                juce::ModifierKeys::popupMenuClickModifier);
            pitchGrid.mouseDown (makeMouseEvent (pitchGrid, badge.getCentre(),
                                                 badge.getCentre(), rightClick));
            pump();

            const auto* afterUnlinkSecond = document.findNote (pitchTrackId, pitchSecondId);
            const auto* afterUnlinkFirst = document.findNote (pitchTrackId, pitchFirstId);
            check (afterUnlinkSecond != nullptr && afterUnlinkSecond->tieFromNoteId == 0,
                   NI_TXT ("right-clicking the triangle removes the connection"));
            check (afterUnlinkFirst != nullptr && afterUnlinkSecond != nullptr
                       && afterUnlinkFirst->startTick == keepFirst.startTick
                       && afterUnlinkFirst->durationTicks == keepFirst.durationTicks
                       && afterUnlinkSecond->startTick == keepSecond.startTick
                       && std::abs (afterUnlinkFirst->effectiveTailHz()
                                        - keepFirst.effectiveTailHz()) < 1.0e-6
                       && std::abs (afterUnlinkSecond->frequencyHz
                                        - keepSecond.frequencyHz) < 1.0e-6,
                   NI_TXT ("unlinking leaves all time and pitch positions unchanged"));

            document.applyEdit ("selftest vst note shape label", [&] (Project& project)
            {
                if (auto* track = project.findTrack (pitchTrackId))
                    track->kind = TrackKind::vst;
            });
            appState.selectNote (pitchFirstId, false);
            pump();
            check (pitchBar.getTargetTextForTest().contains ("VST"),
                   NI_TXT ("a VST track explains that its tilt is display-only"));
        }

        beginTest ("linked notes, piano geometry and per-area snap");

        {
            auto& linkedPanel = component.getEditorPanel();
            auto& linkedGrid = linkedPanel.getGrid();
            auto& linkedLane = linkedPanel.getControllerLane();
            auto& linkedLanes = component.getTrackArea();

            juce::int64 linkedTrackId = 0;
            juce::int64 linkedClipId = 0;
            juce::int64 linkedAId = 0;
            juce::int64 linkedBId = 0;

            document.applyEdit ("selftest linked movement setup", [&] (Project& project)
            {
                project.tracks.clear();
                project.referenceLines.clear();
                project.nextTrackId = 1;

                linkedTrackId = addTrack (project, TrackKind::synth);
                auto* track = project.findTrack (linkedTrackId);

                if (track == nullptr)
                    return;

                track->clips.clear();
                auto clip = makeDefaultClip (project);
                clip.startTick = 0;
                clip.lengthTicks = 19200;
                clip.loopLengthTicks = 19200;
                clip.notes.clear();

                Note a;
                a.id = project.nextNoteId++;
                a.startTick = 0;
                a.durationTicks = 960;
                a.frequencyHz = 220.0;
                a.tailFrequencyHz = 330.0;
                a.pitchCurve = 0.25f;
                a.velocity = 100;

                Note b;
                b.id = project.nextNoteId++;
                b.startTick = 960;
                b.durationTicks = 960;
                b.frequencyHz = 330.0;
                b.tailFrequencyHz = 440.0;
                b.tieFromNoteId = a.id;
                b.velocity = 100;

                linkedAId = a.id;
                linkedBId = b.id;
                clip.notes = { a, b };
                linkedClipId = clip.id;
                track->clips.push_back (std::move (clip));
            });

            appState.selectTrack (linkedTrackId);
            appState.selectClip (linkedClipId);
            appState.selectNote (linkedAId, false);
            appState.setEditorOpen (true);
            linkedPanel.selectEditorModeForTest (2);
            linkedPanel.selectSnapForTest (4);
            setBothTimelines (document, 140.0, 0.0);
            pump();

            check (! linkedLanes.getSnapControlForTest().getToggleBoundsForTest().isEmpty()
                       && ! linkedPanel.getSnapControlForTest().getToggleBoundsForTest().isEmpty()
                       && ! linkedLane.getSnapControlForTest().getToggleBoundsForTest().isEmpty(),
                   NI_TXT ("track, editor and controller lanes each show a snap control"));
            check (linkedPanel.getSnapControlForTest().getComboItemCountForTest() == 5
                       && linkedPanel.getSnapControlForTest()
                              .getComboItemTextForTest (6)
                              .contains (NI_TXT ("1/n")),
                   NI_TXT ("the original snap menu keeps its choices and adds one 1/n item"));

            linkedLanes.getSnapControlForTest().setDenominatorForTest (3);
            linkedPanel.getSnapControlForTest().setDenominatorForTest (8);
            linkedLane.getSnapControlForTest().setDenominatorForTest (5);
            pump();

            check (document.viewState().trackSnap.denominator == 3
                       && document.viewState().editorSnap.denominator == 8
                       && document.viewState().controllerSnap.denominator == 5,
                   NI_TXT ("the three snap settings are independent"));
            check (linkedLanes.getSnapControlForTest().isCustomEditorVisibleForTest()
                       && linkedPanel.getSnapControlForTest().isCustomEditorVisibleForTest()
                       && linkedLane.getSnapControlForTest().isCustomEditorVisibleForTest(),
                   NI_TXT ("the 1/n item reveals the denominator editors"));
            linkedPanel.getSnapControlForTest().setDenominatorForTest (4);
            pump();
            check (! linkedPanel.getSnapControlForTest().isCustomEditorVisibleForTest()
                       && linkedPanel.getSnapControlForTest().getComboBoundsForTest().getWidth()
                              == linkedPanel.getSnapControlForTest().getWidth() - 2,
                   NI_TXT ("an ordinary subdivision keeps the original dropdown-only style"));
            linkedPanel.getSnapControlForTest().setDenominatorForTest (8);
            pump();

            linkedPanel.getSnapControlForTest().setEnabledForTest (false);
            pump();
            check (! document.viewState().editorSnap.enabled
                       && document.viewState().trackSnap.enabled
                       && document.viewState().controllerSnap.enabled,
                   NI_TXT ("turning off editor snap leaves the other areas on"));
            linkedPanel.getSnapControlForTest().setEnabledForTest (true);
            pump();

            const auto pianoRow = linkedGrid.getPianoRowRectForTest (60);
            const auto pianoPoint = linkedGrid.pointForSemitoneForTest (60, 0);
            check (std::abs (pianoRow.getCentreY() - pianoPoint.y) < 0.75f,
                   NI_TXT ("the key row and note row use the same semitone geometry"));

            const auto pianoImage = snapshot (linkedGrid);
            const auto keys = linkedGrid.getPianoKeysBoundsForTest();
            const auto sampleX = juce::jlimit (keys.getRight() + 20, linkedGrid.getWidth() - 8,
                                               linkedGrid.getWidth() - 30);
            const auto whitePixel = pianoImage.getPixelAt (
                sampleX, (int) std::lround (linkedGrid.getPianoRowRectForTest (60).getCentreY()));
            const auto blackPixel = pianoImage.getPixelAt (
                sampleX, (int) std::lround (linkedGrid.getPianoRowRectForTest (61).getCentreY()));
            check (whitePixel.getBrightness() > blackPixel.getBrightness(),
                   NI_TXT ("white-key rows are lighter than black-key rows"));
            const auto emptyRowY = (int) std::lround (
                linkedGrid.getPianoRowRectForTest (72).getCentreY());
            const auto leftRowPixel = pianoImage.getPixelAt (
                keys.getRight() + 20, emptyRowY);
            const auto innerRowPixel = pianoImage.getPixelAt (
                keys.getRight() + 60, emptyRowY);
            check (std::abs (leftRowPixel.getBrightness() - innerRowPixel.getBrightness())
                       < 0.02f,
                   NI_TXT ("the piano rows start at the first canvas pixel with no blank inset"));

            const auto laneLeft = linkedGrid.getX()
                                  + linkedGrid.getPianoKeysBoundsForTest().getWidth();
            check (linkedLane.getX() == laneLeft
                       && linkedLane.getRight() == linkedGrid.getRight() - 12,
                   NI_TXT ("velocity and CC lanes share the canvas' left and right edges"));

            check (countColourInRect (pianoImage, juce::Colours::white,
                                      linkedGrid.getHeadHandleBoundsForTest (linkedAId)
                                          .toNearestInt().expanded (2),
                                      24) > 0,
                   NI_TXT ("the endpoint handle is drawn with a white ring"));

            const auto chainBefore = *document.findNote (linkedTrackId, linkedAId);
            const auto secondBefore = *document.findNote (linkedTrackId, linkedBId);
            const auto bodyPoint = linkedGrid.pointFor (
                music::pitchAt (chainBefore, 0.5),
                document.getProject().findClip (linkedClipId)->startTick
                    + chainBefore.startTick + chainBefore.durationTicks / 2);
            const auto bodyTarget = bodyPoint.translated (24.0f, 0.0f);
            linkedGrid.mouseDown (makeMouseEvent (linkedGrid, bodyPoint, bodyPoint));
            linkedGrid.mouseDrag (makeMouseEvent (linkedGrid, bodyTarget, bodyPoint));
            linkedGrid.mouseUp (makeMouseEvent (linkedGrid, bodyTarget, bodyPoint));
            pump();

            const auto chainMoved = *document.findNote (linkedTrackId, linkedAId);
            const auto secondMoved = *document.findNote (linkedTrackId, linkedBId);
            check (chainMoved.startTick > chainBefore.startTick
                       && chainMoved.endTick() == secondMoved.startTick
                       && secondMoved.endTick() == secondBefore.endTick(),
                   NI_TXT ("moving the first linked body carries only the next head"));

            document.undo();
            pump();

            appState.selectNote (linkedBId, false);
            const auto firstFixed = *document.findNote (linkedTrackId, linkedAId);
            const auto secondBodyBefore = *document.findNote (linkedTrackId, linkedBId);
            const auto secondBodyPoint = linkedGrid.pointFor (
                music::pitchAt (secondBodyBefore, 0.5),
                document.getProject().findClip (linkedClipId)->startTick
                    + secondBodyBefore.startTick + secondBodyBefore.durationTicks / 2);
            const auto secondBodyTarget = secondBodyPoint.translated (24.0f, 0.0f);
            linkedGrid.mouseDown (
                makeMouseEvent (linkedGrid, secondBodyPoint, secondBodyPoint));
            linkedGrid.mouseDrag (
                makeMouseEvent (linkedGrid, secondBodyTarget, secondBodyPoint));
            linkedGrid.mouseUp (
                makeMouseEvent (linkedGrid, secondBodyTarget, secondBodyPoint));
            pump();

            const auto firstFollowed = *document.findNote (linkedTrackId, linkedAId);
            const auto secondBodyMoved = *document.findNote (linkedTrackId, linkedBId);
            check (firstFollowed.startTick == firstFixed.startTick
                       && firstFollowed.frequencyHz == firstFixed.frequencyHz
                       && firstFollowed.endTick() == secondBodyMoved.startTick
                       && secondBodyMoved.startTick > secondBodyBefore.startTick,
                   NI_TXT ("moving the second linked body carries only the previous tail"));

            document.undo();
            pump();
            appState.selectNote (linkedAId, false);

            const auto seamBefore = *document.findNote (linkedTrackId, linkedAId);
            const auto tailHandle = linkedGrid.getTailHandleBoundsForTest (linkedAId);
            const auto seamTargetY = linkedGrid.yForPianoRowForTest (69);
            const auto seamFrom = tailHandle.getCentre();
            const auto seamTo = juce::Point<float> (seamFrom.x - 12.0f, seamTargetY);
            linkedGrid.mouseDown (makeMouseEvent (linkedGrid, seamFrom, seamFrom));
            linkedGrid.mouseDrag (makeMouseEvent (linkedGrid, seamTo, seamFrom));
            linkedGrid.mouseUp (makeMouseEvent (linkedGrid, seamTo, seamFrom));
            pump();

            const auto seamA = *document.findNote (linkedTrackId, linkedAId);
            const auto seamB = *document.findNote (linkedTrackId, linkedBId);
            check (std::abs (seamA.effectiveTailHz() - seamB.frequencyHz) < 1.0e-4,
                   NI_TXT ("moving a seam endpoint moves both sides to the same pitch"));
            check (seamA.endTick() == seamB.startTick
                       && seamA.endTick() < seamBefore.endTick(),
                   NI_TXT ("an endpoint circle moves both the seam time and pitch"));

            const auto beforeEdgeSecond = seamB;
            const auto rightEdge = linkedGrid.getNoteShapeBoundsForTest (linkedAId).getRight() - 1.0f;
            const auto edgeY = linkedGrid.pointFor (
                music::pitchAt (seamA, 0.5),
                document.getProject().findClip (linkedClipId)->startTick
                    + seamA.startTick + seamA.durationTicks / 2).y;
            const auto edgeFrom = juce::Point<float> (rightEdge, edgeY);
            const auto edgeTo = edgeFrom.translated (24.0f, 0.0f);
            linkedGrid.mouseDown (makeMouseEvent (linkedGrid, edgeFrom, edgeFrom));
            linkedGrid.mouseDrag (makeMouseEvent (linkedGrid, edgeTo, edgeFrom));
            linkedGrid.mouseUp (makeMouseEvent (linkedGrid, edgeTo, edgeFrom));
            pump();

            const auto edgeA = *document.findNote (linkedTrackId, linkedAId);
            const auto edgeB = *document.findNote (linkedTrackId, linkedBId);
            check (edgeA.endTick() == edgeB.startTick
                       && edgeB.startTick > beforeEdgeSecond.startTick,
                   NI_TXT ("resizing a linked tail carries the following note with it"));

            check (writeImage (snapshot (component),
                               outputDirectory.getChildFile ("60-note-chain-follow.png")),
                   NI_TXT ("linked-note follow snapshot"));

            // The controller lane's velocity bars use an absolute pointer height.
            appState.selectNote (linkedAId, false);
            linkedLane.selectSignalForTest (true, 0);
            pump();
            const auto bars = linkedLane.getVelocityBarsForTest();
            check (! bars.empty(), NI_TXT ("the velocity lane has a bar to drag"));

            if (! bars.empty())
            {
                const auto bar = bars.front();
                const auto velocityPoint = juce::Point<float> (
                    (float) bar.getCentreX(),
                    (float) linkedLane.getYForValueForTest (80.0));
                linkedLane.mouseDown (makeMouseEvent (linkedLane, velocityPoint, velocityPoint));
                linkedLane.mouseUp (makeMouseEvent (linkedLane, velocityPoint, velocityPoint));
                pump();

                const auto* afterVelocity = document.findNote (linkedTrackId, linkedAId);
                check (afterVelocity != nullptr && std::abs (afterVelocity->velocity - 80) <= 1,
                       NI_TXT ("pressing a velocity bar jumps to the pointer height"));

                const auto lower = juce::Point<float> (
                    (float) bar.getCentreX(),
                    (float) linkedLane.getYForValueForTest (36.0));
                linkedLane.mouseDown (makeMouseEvent (linkedLane, velocityPoint, velocityPoint));
                linkedLane.mouseDrag (makeMouseEvent (linkedLane, lower, velocityPoint));
                linkedLane.mouseUp (makeMouseEvent (linkedLane, lower, velocityPoint));
                pump();

                const auto* draggedVelocity = document.findNote (linkedTrackId, linkedAId);
                check (draggedVelocity != nullptr && std::abs (draggedVelocity->velocity - 36) <= 1,
                       NI_TXT ("holding and moving the pointer follows the absolute velocity"));
            }

            // Free CC drawing is sampled and then simplified on release.
            linkedLane.getSnapControlForTest().setEnabledForTest (false);
            linkedLane.selectSignalForTest (false, 11);
            pump();
            const auto plot = linkedLane.getPlotBoundsForTest();
            const auto start = juce::Point<float> ((float) plot.getX() + 50.0f,
                                                   (float) plot.getCentreY());
            linkedLane.mouseDown (makeMouseEvent (linkedLane, start, start));

            for (int i = 1; i <= 90; ++i)
            {
                const auto p = juce::Point<float> (
                    start.x + (float) i * 2.0f,
                    (float) plot.getCentreY() + std::sin ((float) i * 0.22f) * 26.0f);
                linkedLane.mouseDrag (makeMouseEvent (linkedLane, p, start));
            }

            linkedLane.mouseUp (makeMouseEvent (
                linkedLane,
                juce::Point<float> (start.x + 180.0f,
                                    (float) plot.getCentreY() + std::sin (90.0f * 0.22f) * 26.0f),
                start));
            pump();

            const auto curvePointCount = (int) linkedLane.getCurvePointsForTest().size();
            check (curvePointCount >= 2 && curvePointCount < 30,
                   NI_TXT ("free CC drawing is simplified to manageable knees (")
                       + juce::String (curvePointCount) + NI_TXT (")"));

            linkedLane.getSnapControlForTest().setEnabledForTest (true);
            linkedPanel.getSnapControlForTest().setDenominatorForTest (3);
            linkedLanes.getSnapControlForTest().setDenominatorForTest (4);
            linkedLane.getSnapControlForTest().setDenominatorForTest (8);
            pump();

            check (writeImage (snapshot (component),
                               outputDirectory.getChildFile ("61-snap-grids.png")),
                   NI_TXT ("per-area snap grid snapshot"));
        }

        // The channel-spread scenes run last: they need the test plugin, and the
        // snapshots above must not be taken with the project state they build.
        if (runChannelScenes)
            runChannelScenes();

        std::cout << (failures == 0 ? "SELFTEST PASSED" : "SELFTEST FAILED") << std::endl;

        log.add (failures == 0 ? "SELFTEST PASSED" : "SELFTEST FAILED");
        outputDirectory.getChildFile ("selftest.log").replaceWithText (log.joinIntoString ("\n"));

        return failures == 0 ? 0 : 1;
    }
}

} // namespace nidaw

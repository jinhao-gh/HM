#include "UI/AppLookAndFeel.h"
#include "UI/MainComponent.h"
#include "UI/Theme.h"

#include "Audio/Exporter.h"
#include "Audio/PluginHost.h"
#include "Audio/PluginScanChild.h"
#include "Core/FmMath.h"
#include "Core/WindowPlacement.h"
#include "UI/PluginPicker.h"
#include "UI/PluginWindow.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include <cstdlib>
#include <iostream>
#include <memory>
#include <vector>

#if JUCE_WINDOWS
 #include <windows.h>
#endif

#include "SelfTest/SelfTest.h"
#include "SelfTest/VstPitchProbe.h"

namespace nidaw
{

class MainWindow : public juce::DocumentWindow
{
public:
    explicit MainWindow (const juce::String& name)
        : juce::DocumentWindow (name, theme::windowBackground, juce::DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar (true);
        setContentOwned (new MainComponent(), false);
        setResizable (true, false);

        // Open at the design size, but never bigger than the screen: a window that
        // hangs off the bottom hides its own transport bar (音频设置 / 导出 / 播放)
        // with no way to get at it, and a resize minimum larger than the work area
        // would put the window right back there. Both give way to the display.
        const auto area = placement::displayAreaFor (nullptr);
        const auto minimum = placement::fitMinimum ({ placement::kMainWindowMinimumWidth,
                                                      placement::kMainWindowMinimumHeight },
                                                    area, 0);

        setResizeLimits (minimum.x, minimum.y, 6000, 3000);
        setBounds (placement::placeWindow ({ placement::kMainWindowWidth,
                                             placement::kMainWindowHeight },
                                           area, 0));
        setVisible (true);
    }

    void closeButtonPressed() override
    {
        // Give the editor a chance to ask about unsaved changes first.
        if (auto* component = dynamic_cast<MainComponent*> (getContentComponent()))
            if (! component->confirmQuit())
                return;

        juce::JUCEApplication::getInstance()->quit();
    }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
};

class NiDawApplication : public juce::JUCEApplication
{
public:
    NiDawApplication() = default;

    const juce::String getApplicationName() override    { return NI_TXT ("HM"); }
    const juce::String getApplicationVersion() override { return "0.1.58"; }
    bool moreThanOneInstanceAllowed() override          { return false; }

    void initialise (const juce::String& commandLine) override
    {
        if (commandLine.contains ("--selftest"))
        {
            // Read the arguments as tokens: "--selftest <folder>" and an optional
            // "--vst3 <path>" that points the hosting checks at a plugin. Taking
            // the rest of the line for the folder would swallow the second one.
            //
            // The array comes from CommandLineToArgvW, which is the same parse the
            // C runtime does: quoted arguments arrive as single entries with their
            // quotes already gone.
            const auto tokens = juce::JUCEApplicationBase::getCommandLineParameterArray();

            auto outputDirectory = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                       .getChildFile ("nidaw-selftest");
            auto testPlugin = juce::File();
            auto testEffect = juce::File();

            for (int i = 0; i < tokens.size(); ++i)
            {
                if (i + 1 >= tokens.size() || tokens[i + 1].startsWith ("--"))
                    continue;

                if (tokens[i] == "--selftest")
                    outputDirectory = juce::File (tokens[i + 1]);
                else if (tokens[i] == "--vst3")
                    testPlugin = juce::File (tokens[i + 1]);
                else if (tokens[i] == "--vst3fx")
                    testEffect = juce::File (tokens[i + 1]);
            }

            setApplicationReturnValue (selftest::run (outputDirectory, testPlugin, testEffect));
            quit();
            return;
        }

        if (commandLine.contains ("--vstprobe"))
        {
            // What one external instrument does with per-note pitch bend (see
            // VstPitchProbe.h). Diagnostic: `--vstprobe <plugin.vst3> <output folder>`.
            const auto tokens = juce::JUCEApplicationBase::getCommandLineParameterArray();
            juce::File pluginFile, outputFolder = juce::File::getSpecialLocation (
                                                      juce::File::tempDirectory)
                                                      .getChildFile ("nidaw-vstprobe");
            juce::StringArray setExactly, setLike;

            for (int i = 0; i < tokens.size(); ++i)
            {
                if (i + 1 >= tokens.size() || tokens[i + 1].startsWith ("--"))
                    continue;

                if (tokens[i] == "--vstprobe")
                    pluginFile = juce::File (tokens[i + 1]);
                else if (tokens[i] == "--out")
                    outputFolder = juce::File (tokens[i + 1]);
                else if (tokens[i] == "--set")
                    setExactly.add (tokens[i + 1]);
                else if (tokens[i] == "--set-like")
                    setLike.add (tokens[i + 1]);
            }

            if (pluginFile == juce::File() || ! pluginFile.exists())
            {
                std::cout << "usage: HM.exe --vstprobe <plugin.vst3> [--out <folder>]" << std::endl;
                setApplicationReturnValue (2);
                quit();
                return;
            }

            const auto report = selftest::runVstPitchProbe (pluginFile, outputFolder,
                                                            setExactly, setLike);

            std::cout << report << std::endl;
            juce::Logger::writeToLog (report);
            setApplicationReturnValue (0);
            quit();
            return;
        }

        lookAndFeel = std::make_unique<AppLookAndFeel>();
        juce::LookAndFeel::setDefaultLookAndFeel (lookAndFeel.get());
        mainWindow = std::make_unique<MainWindow> (NI_TXT ("HM 58"));
    }

    void shutdown() override
    {
        mainWindow.reset();
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
        lookAndFeel.reset();
    }

    void systemRequestedQuit() override
    {
        // Also covers a Windows shutdown or log-off, which arrives here rather
        // than through the window's close button.
        if (mainWindow != nullptr)
            if (auto* component = dynamic_cast<MainComponent*> (mainWindow->getContentComponent()))
                if (! component->confirmQuit())
                    return;

        quit();
    }

    void anotherInstanceStarted (const juce::String&) override
    {
        // HM is single-instance. Windows routes a second launch here, so bring the
        // existing window back instead of silently doing nothing while a hidden or
        // minimised instance owns the application lock.
        if (mainWindow != nullptr)
        {
            mainWindow->setVisible (true);
            mainWindow->toFront (true);
        }
    }

private:
    std::unique_ptr<AppLookAndFeel> lookAndFeel;
    std::unique_ptr<MainWindow> mainWindow;
};

} // namespace nidaw

// The usual START_JUCE_APPLICATION expansion, with one thing in front of it: a copy
// of HM started as a plugin-scan child must never build a window, an audio device or
// a splash screen - it loads one plugin and leaves. Handling it here, before JUCE's
// application object exists, is what keeps the scan quiet and lets the child die
// with a plugin without taking the editor with it.
juce::JUCEApplicationBase* juce_CreateApplication()
{
    return new nidaw::NiDawApplication();
}

JUCE_MAIN_FUNCTION
{
    if (const auto childExitCode = nidaw::pluginscan::runChildIfRequested (
            juce::JUCEApplicationBase::getCommandLineParameterArray()))
        std::_Exit (*childExitCode);

    juce::JUCEApplicationBase::createInstance = &juce_CreateApplication;
    return juce::JUCEApplicationBase::main (JUCE_MAIN_FUNCTION_ARGS);
}

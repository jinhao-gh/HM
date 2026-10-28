#include "TestSupport.h"

int main (int argc, char** argv)
{
    ::AddVectoredExceptionHandler (1, testCrashHandler);

    // Keep a crash from popping up a Windows error dialog that would leave the
    // process alive forever in an unattended run.
    ::SetErrorMode (SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);

    // An optional argument selects one test group, and --list prints the names.
    // The wrapper script runs a group per process so that a hang or crash in one
    // group cannot hide the results of the others.
    const juce::String filter = argc > 1 ? juce::String (argv[1]) : juce::String();

    if (filter == "--list")
    {
        for (auto* test : juce::UnitTest::getAllTests())
            if (test->getCategory() == "nidaw")
                std::cout << test->getName() << std::endl;

        return 0;
    }

    juce::UnitTestRunner runner;
    runner.setAssertOnFailure (false);

    // Only run this project's own tests: JUCE's built-in suite is not part of
    // this build and some of its tests are not headless-safe.
    juce::Array<juce::UnitTest*> ownTests;

    for (auto* test : juce::UnitTest::getAllTests())
        if (test->getCategory() == "nidaw" && (filter.isEmpty() || test->getName() == filter))
            ownTests.add (test);

    std::cout << "running " << ownTests.size() << " test group(s)" << std::endl;

    runner.runTests (ownTests);

    int failures = 0;

    for (int i = 0; i < runner.getNumResults(); ++i)
    {
        if (auto* result = runner.getResult (i))
        {
            failures += result->failures;

            std::cout << "  " << result->unitTestName.toStdString()
                      << " / " << result->subcategoryName.toStdString()
                      << " : " << result->passes << " passed, "
                      << result->failures << " failed" << std::endl;

            // Printing what actually failed saves a round of guesswork every time
            // a check goes red; the runner does not do it for us.
            if (result->failures > 0)
                for (const auto& message : result->messages)
                    std::cout << "      " << message.toStdString() << std::endl;
        }
    }

    std::cout << (failures == 0 ? "ALL TESTS PASSED" : "SOME TESTS FAILED") << std::endl;
    return failures == 0 ? 0 : 1;
}

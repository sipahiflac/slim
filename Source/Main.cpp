#include <juce_gui_basics/juce_gui_basics.h>
#include "MainComponent.h"

class FakePhoneScreenApplication : public juce::JUCEApplication
{
public:
    FakePhoneScreenApplication() {}

    const juce::String getApplicationName() override       { return "2G"; }
    const juce::String getApplicationVersion() override    { return "1.0.2"; }
    bool moreThanOneInstanceAllowed() override             { return true; }

    void initialise(const juce::String&) override
    {
        mainWindow = std::make_unique<MainWindow>(getApplicationName());
    }

    void shutdown() override
    {
        mainWindow = nullptr;
    }

    void systemRequestedQuit() override
    {
        quit();
    }

    void anotherInstanceStarted(const juce::String&) override
    {
    }

    class MainWindow : public juce::DocumentWindow
    {
    public:
        explicit MainWindow(juce::String name)
            : DocumentWindow(name,
                             juce::Colour(0xFF141414),
                             DocumentWindow::minimiseButton | DocumentWindow::closeButton)
        {
            setUsingNativeTitleBar(true);

            auto* comp = new MainComponent();
            setContentOwned(comp, true);

            setResizable(false, false);
            centreWithSize(getWidth(), getHeight());
            setVisible(true);
        }

        void closeButtonPressed() override
        {
            JUCEApplication::getInstance()->systemRequestedQuit();
        }

        // Coming back to the window (alt-tab, clicking the title bar) puts the keyboard straight into the page.
        void activeWindowStatusChanged() override
        {
            DocumentWindow::activeWindowStatusChanged();
            if (isActiveWindow())
                if (auto* content = dynamic_cast<MainComponent*>(getContentComponent()))
                    content->focusWebView();
        }

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
    };

private:
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION(FakePhoneScreenApplication)

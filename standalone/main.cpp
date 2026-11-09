// rktuner — the chromatic tuner as an app, on the Media Kit or on JACK.
//
// One window with the tuner's own display, an Audio menu (Media Kit or JACK,
// the input to tune, whether to hear it) and a status line. The tuner is the
// same rktuner LV2 plug-in a DAW loads, run here by the LV2-renku hosting
// core; the backend, input, A4 reference and window position are restored
// from the last run.

#include "mainwindow.h"
#include "settings.h"

#include <Alert.h>
#include <Application.h>

#ifndef RKTUNER_VERSION
#define RKTUNER_VERSION "0.0.0"
#endif

namespace
{

const char *kSignature = "application/x-vnd.rations-rktuner";

class RktunerApp : public BApplication
{
public:
    RktunerApp() : BApplication(kSignature)
    {
    }

    void ReadyToRun() override
    {
        // The BApplication exists, so the window may open the audio client.
        rktuner::MainWindow *window = new rktuner::MainWindow(rktuner::LoadSettings());
        window->Show();
    }

    void AboutRequested() override
    {
        BAlert *about = new BAlert(
            "About rktuner",
            "rktuner " RKTUNER_VERSION "\n\n"
            "A chromatic tuner for RenkuOS: plug a guitar or bass into your sound card, "
            "choose its input in the Audio menu and tune. Runs on the Media Kit or JACK. "
            "The same rktuner as the LV2 plug-in.\n\n"
            "(C) 2026 rations, MIT.",
            "OK");
        about->SetFlags(about->Flags() | B_CLOSE_ON_ESCAPE);
        about->Go(nullptr);
    }
};

} // namespace

int main()
{
    RktunerApp app;
    app.Run();
    return 0;
}

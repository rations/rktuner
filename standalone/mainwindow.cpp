// MainWindow implementation. See mainwindow.h for the threading contract.

#include "mainwindow.h"

#include "settings.h"

#include <lv2host/lv2ui_host.h>

#include <Alert.h>
#include <Application.h>
#include <Menu.h>
#include <MenuBar.h>
#include <MenuItem.h>
#include <MessageRunner.h>
#include <Messenger.h>
#include <StringView.h>
#include <View.h>

#include <stdio.h>
#include <string.h>

namespace rktuner
{

const char *const kPluginUri = "https://github.com/rations/rktuner";

namespace
{

enum : uint32 {
    kMsgBackend = 'rkBk',   // "backend": "mediakit" | "jack"
    kMsgInput = 'rkIn',     // "source": a source name, "" for none
    kMsgHear = 'rkHr',      // toggle passing the input through
    kMsgTick = 'rkTk',      // ~30 Hz: control changes to the UI
    kMsgStatus = 'rkSt',    // refresh the status line
    kMsgAudioInfo = 'rkAI', // show the audio status in an alert
};

// The window's size before the tuner's view is known (and without one).
const float kDefaultWidth = 480;
const float kDefaultHeight = 160;

void Tell(const char *title, const char *text)
{
    BAlert *alert =
        new BAlert(title, text, "OK", nullptr, nullptr, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
    alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
    alert->Go(nullptr); // asynchronous: the window keeps running
}

// The Media Kit lists sources as "media:<device> / <output> / <channel>".
const char *SourceLabel(const char *source)
{
    return strncmp(source, "media:", 6) == 0 ? source + 6 : source;
}

} // namespace

//------------------------------------------------------------------------
MainWindow::MainWindow(const BMessage &settings)
    : BWindow(BRect(100, 100, 100 + kDefaultWidth - 1, 100 + kDefaultHeight - 1), "rktuner",
              B_TITLED_WINDOW,
              B_NOT_RESIZABLE | B_NOT_ZOOMABLE | B_ASYNCHRONOUS_CONTROLS | B_QUIT_ON_WINDOW_CLOSE),
      fSettings(settings)
{
    memset(&fHost, 0, sizeof fHost);

    // Menus
    BMenuBar *bar = new BMenuBar(BRect(0, 0, kDefaultWidth - 1, 0), "menu");
    BMenu *app = new BMenu("rktuner");
    app->AddItem(new BMenuItem("About rktuner" B_UTF8_ELLIPSIS, new BMessage(B_ABOUT_REQUESTED)));
    app->AddSeparatorItem();
    app->AddItem(new BMenuItem("Quit", new BMessage(B_QUIT_REQUESTED), 'Q'));
    app->SetTargetForItems(this);
    bar->AddItem(app);

    BMenu *audio = new BMenu("Audio");
    BMessage *mk = new BMessage(kMsgBackend);
    mk->AddString("backend", "mediakit");
    audio->AddItem(fMediaKitItem = new BMenuItem("Media Kit", mk));
    BMessage *jack = new BMessage(kMsgBackend);
    jack->AddString("backend", "jack");
    audio->AddItem(fJackItem = new BMenuItem("JACK", jack));
    audio->AddSeparatorItem();
    fInputMenu = new BMenu("Input");
    audio->AddItem(fInputMenu);
    audio->AddItem(fHearItem = new BMenuItem("Hear the input", new BMessage(kMsgHear)));
    audio->AddSeparatorItem();
    audio->AddItem(new BMenuItem("Audio status" B_UTF8_ELLIPSIS, new BMessage(kMsgAudioInfo)));
    bar->AddItem(audio);
    AddChild(bar);
    fMenuHeight = bar->Frame().Height() + 1;

    // The tuner's UI, below the menu bar
    fUiParent =
        new BView(BRect(0, fMenuHeight, kDefaultWidth - 1, fMenuHeight + kDefaultHeight - 1), "ui",
                  B_FOLLOW_NONE, 0);
    fUiParent->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
    AddChild(fUiParent);

    // The status line, below the UI (FitToContent places and sizes it)
    fStatus = new BStringView(BRect(4, 2, kDefaultWidth - 5, 2), "status", "");
    fStatus->ResizeToPreferred(); // the height of one line of the font
    fStatusHeight = fStatus->Frame().Height() + 5;
    BView *statusBack = new BView(BRect(0, 0, kDefaultWidth - 1, fStatusHeight - 1), "status-back",
                                  B_FOLLOW_NONE, 0);
    statusBack->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
    statusBack->AddChild(fStatus);
    AddChild(statusBack);
    FitToContent(kDefaultWidth, kDefaultHeight);

    BRect frame;
    if (fSettings.FindRect("frame", &frame) == B_OK)
        MoveTo(frame.LeftTop());
    MoveOnScreen(B_MOVE_IF_PARTIALLY_OFFSCREEN);

    fHear = fSettings.GetBool("hear", false);

    BMessage tick(kMsgTick);
    fTick = new BMessageRunner(BMessenger(this), &tick, kLv2UiTickUsec);
    BMessage status(kMsgStatus);
    fStatusPump = new BMessageRunner(BMessenger(this), &status, 1000000);

    // A new BWindow is locked by the thread that made it until Show(), so the
    // UI is attached under the looper lock, as the HaikuUI contract says.
    StartAudio(fSettings.GetString("backend", "mediakit"));
}

//------------------------------------------------------------------------
MainWindow::~MainWindow()
{
    delete fTick;
    delete fStatusPump;
    StopAudio();
}

//------------------------------------------------------------------------
void MainWindow::FitToContent(float width, float height)
{
    fUiParent->ResizeTo(width - 1, height - 1);
    BView *statusBack = FindView("status-back");
    if (statusBack) {
        statusBack->MoveTo(0, fMenuHeight + height);
        statusBack->ResizeTo(width - 1, fStatusHeight - 1);
        fStatus->ResizeTo(width - 9, fStatusHeight - 5);
    }
    ResizeTo(width - 1, fMenuHeight + height + fStatusHeight - 1);
}

//------------------------------------------------------------------------
void MainWindow::StartAudio(const char *backend)
{
    StopAudio();

    const bool jack = strcmp(backend, "jack") == 0;
    Lv2hOpenOptions opt = {jack ? "jack" : "mediakit", "rktuner", "RKTUNER_BACKEND_DIR"};
    if (lv2host_open(&fHost, kPluginUri, "rktuner", &opt)) {
        fOpen = true;
        fBackend = opt.backend;
    } else {
        BString error(fHost.error);
        // JACK not running (or not installed): fall back to the Media Kit so
        // the tuner still works, and say why.
        if (jack) {
            opt.backend = "mediakit";
            if (lv2host_open(&fHost, kPluginUri, "rktuner", &opt)) {
                fOpen = true;
                fBackend = opt.backend;
                error << "\n\nTuning on the Media Kit instead.";
            }
        }
        Tell("Audio", error.String());
    }

    if (fOpen) {
        fSettings.SetString("backend", fBackend.String());
        RestoreParams();
        lv2host_set_passthrough(&fHost, fHear);
        if (fHear)
            lv2host_connect_outputs(&fHost);

        // The input last chosen on this backend if it is still there, else
        // the first input of the card that runs the clock.
        BString key("input:");
        key << fBackend;
        BString want;
        bool saved = fSettings.FindString(key.String(), &want) == B_OK;
        bool found = false;
        if (saved && want.Length() > 0) {
            const char **src = lv2host_get_sources(&fHost);
            for (int i = 0; src && src[i]; i++)
                if (want == src[i])
                    found = true;
            lv2host_free_sources(&fHost, src);
        }
        if (!found && !(saved && want.Length() == 0)) {
            char def[256];
            want = lv2host_default_source(&fHost, def, sizeof def) ? def : "";
        }
        if (want.Length() > 0 && lv2host_connect_input(&fHost, want.String(), 0) == 0)
            fInput = want;

        // The tuner's own UI
        fUi = new Lv2UiHost(&fHost);
        if (fUi->Open(kPluginUri) && fUi->IsEmbedded()) {
            // A HaikuUI view may be made for a layout (a zero frame and an
            // explicit size, as the tuner's is): size it to what it prefers.
            BView *view = fUi->View();
            BSize size = view->PreferredSize();
            if (size.width <= 0 || size.height <= 0)
                size = BSize(view->Bounds().Width(), view->Bounds().Height());
            view->MoveTo(0, 0);
            view->ResizeTo(size.width, size.height);
            fUiParent->AddChild(view);
            FitToContent(size.width + 1, size.height + 1);
        } else {
            Tell("rktuner", fUi->Error()[0] ? fUi->Error() : "The tuner has no display.");
            delete fUi;
            fUi = nullptr;
        }
    }
    UpdateMenus();
    UpdateStatus();
}

//------------------------------------------------------------------------
void MainWindow::StopAudio()
{
    if (!fOpen)
        return;
    SaveParams();
    if (fUi) {
        fUi->Close(); // the UI removes and deletes its view
        delete fUi;   // then its binary is unloaded
        fUi = nullptr;
    }
    lv2host_close(&fHost);
    fOpen = false;
    fInput = "";
}

//------------------------------------------------------------------------
void MainWindow::SaveParams()
{
    if (!fOpen)
        return;
    for (uint32_t i = 0; i < fHost.n_params; i++) {
        BString key("param:");
        key << fHost.params[i].name;
        fSettings.SetFloat(key.String(), lv2host_get_control(&fHost, fHost.params[i].port_index));
    }
}

//------------------------------------------------------------------------
void MainWindow::RestoreParams()
{
    for (uint32_t i = 0; i < fHost.n_params; i++) {
        BString key("param:");
        key << fHost.params[i].name;
        float value;
        if (fSettings.FindFloat(key.String(), &value) == B_OK)
            lv2host_set_param(&fHost, i, value); // clamped to the port's range
    }
}

//------------------------------------------------------------------------
void MainWindow::ChooseInput(const char *source)
{
    if (!fOpen)
        return;
    if (fInput.Length() > 0) {
        lv2host_disconnect_input(&fHost, fInput.String(), 0);
        fInput = "";
    }
    if (source[0]) {
        if (lv2host_connect_input(&fHost, source, 0) == 0) {
            fInput = source;
        } else {
            BString text("Could not connect ");
            text << SourceLabel(source) << ".";
            Tell("Input", text.String());
        }
    }
    BString key("input:");
    key << fBackend;
    fSettings.SetString(key.String(), fInput.String());
    UpdateStatus();
}

//------------------------------------------------------------------------
void MainWindow::SetHear(bool hear)
{
    fHear = hear;
    fSettings.SetBool("hear", hear);
    if (fOpen) {
        lv2host_set_passthrough(&fHost, hear);
        if (hear)
            lv2host_connect_outputs(&fHost);
    }
    UpdateMenus();
}

//------------------------------------------------------------------------
void MainWindow::UpdateMenus()
{
    const bool jack = fOpen && fBackend == "jack";
    fMediaKitItem->SetMarked(fOpen && !jack);
    fJackItem->SetMarked(jack);
    fHearItem->SetMarked(fHear);
}

//------------------------------------------------------------------------
void MainWindow::MenusBeginning()
{
    // The sources change as devices and programs come and go: list them
    // afresh.
    while (BMenuItem *item = fInputMenu->RemoveItem((int32)0))
        delete item;
    BMessage *none = new BMessage(kMsgInput);
    none->AddString("source", "");
    BMenuItem *noneItem = new BMenuItem("None", none);
    noneItem->SetMarked(fInput.Length() == 0);
    fInputMenu->AddItem(noneItem);
    const char **src = fOpen ? lv2host_get_sources(&fHost) : nullptr;
    if (src && src[0])
        fInputMenu->AddSeparatorItem();
    for (int i = 0; src && src[i]; i++) {
        BMessage *message = new BMessage(kMsgInput);
        message->AddString("source", src[i]);
        BMenuItem *item = new BMenuItem(SourceLabel(src[i]), message);
        item->SetMarked(fInput == src[i]);
        fInputMenu->AddItem(item);
    }
    if (src)
        lv2host_free_sources(&fHost, src);
    fInputMenu->SetTargetForItems(this);
    fInputMenu->SetEnabled(fOpen);
    UpdateMenus();
    BWindow::MenusBeginning();
}

//------------------------------------------------------------------------
void MainWindow::UpdateStatus()
{
    BString text;
    if (!fOpen) {
        text = "No audio: choose Media Kit or JACK in the Audio menu.";
    } else if (fHost.backend_gone) {
        text = "The audio system stopped. Choose it again in the Audio menu.";
    } else {
        text.SetToFormat("%s, %.0f Hz, %u frames", lv2host_backend_name(&fHost), fHost.sample_rate,
                         (unsigned)lv2host_period(&fHost));
        text << ". Input: " << (fInput.Length() > 0 ? SourceLabel(fInput.String()) : "none");
    }
    fStatus->SetText(text.String());
}

//------------------------------------------------------------------------
bool MainWindow::QuitRequested()
{
    SaveParams();
    fSettings.SetRect("frame", Frame());
    if (SaveSettings(fSettings) != B_OK)
        fprintf(stderr, "rktuner: could not save the settings\n");
    delete fTick;
    fTick = nullptr;
    delete fStatusPump;
    fStatusPump = nullptr;
    StopAudio(); // on this thread with the looper locked, before the views go
    return true;
}

//------------------------------------------------------------------------
void MainWindow::MessageReceived(BMessage *message)
{
    switch (message->what) {
        case kMsgTick:
            if (fUi)
                fUi->Tick();
            break;
        case kMsgStatus:
            UpdateStatus();
            break;
        case kMsgBackend: {
            const char *backend = message->GetString("backend", "mediakit");
            if (!fOpen || fBackend != backend || fHost.backend_gone)
                StartAudio(backend);
            break;
        }
        case kMsgInput:
            ChooseInput(message->GetString("source", ""));
            break;
        case kMsgHear:
            SetHear(!fHear);
            break;
        case kMsgAudioInfo: {
            BString text;
            if (fOpen) {
                text.SetToFormat("%s\n%.0f Hz, period %u frames\nxruns: %u",
                                 lv2host_backend_name(&fHost), fHost.sample_rate,
                                 (unsigned)lv2host_period(&fHost), fHost.n_xruns);
                text << "\nInput: "
                     << (fInput.Length() > 0 ? SourceLabel(fInput.String()) : "none");
                text << "\nHearing the input: " << (fHear ? "yes" : "no");
            } else {
                text = "No audio.";
            }
            Tell("Audio status", text.String());
            break;
        }
        case B_ABOUT_REQUESTED:
            be_app->PostMessage(B_ABOUT_REQUESTED);
            break;
        default:
            BWindow::MessageReceived(message);
            break;
    }
}

} // namespace rktuner

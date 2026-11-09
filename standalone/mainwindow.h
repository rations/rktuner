// MainWindow — the rktuner app's one window: the tuner's own UI, an Audio
// menu (Media Kit or JACK, the input to tune, whether to hear it) and a status
// line.
//
// Threading: the window's looper owns the plug-in host and its UI. Audio is
// started and stopped on this thread (the Media Kit needs the BApplication,
// which exists before the window), the UI is ticked here ~30 Hz with the
// looper locked, as the HaikuUI contract requires, and the plug-in itself
// runs on the audio backend's real-time thread, reached only through the
// hosting core's lock-free control ring.

#pragma once

#include <Message.h>
#include <String.h>
#include <Window.h>

#include <lv2host/lv2host_core.h>

class BMenu;
class BMenuItem;
class BMessageRunner;
class BStringView;
class BView;
class Lv2UiHost;

namespace rktuner
{

// The tuner plug-in this app runs.
extern const char *const kPluginUri;

class MainWindow : public BWindow
{
public:
    // `settings` holds the saved backend, inputs, parameters and frame; the
    // window updates it and saves it when it closes.
    explicit MainWindow(const BMessage &settings);
    ~MainWindow() override;

    void MenusBeginning() override;
    void MessageReceived(BMessage *message) override;
    bool QuitRequested() override;

private:
    void StartAudio(const char *backend);
    void StopAudio();
    void ChooseInput(const char *source);
    void SetHear(bool hear);
    void SaveParams();
    void RestoreParams();
    void FitToContent(float width, float height);
    void UpdateMenus();
    void UpdateStatus();

    BMessage fSettings;
    Lv2Host fHost;
    bool fOpen = false;
    BString fBackend; // "mediakit" or "jack" while open
    BString fInput;   // the source connected to in_1, "" for none
    bool fHear = false;
    Lv2UiHost *fUi = nullptr;

    BView *fUiParent = nullptr;
    BStringView *fStatus = nullptr;
    BMenu *fInputMenu = nullptr;
    BMenuItem *fMediaKitItem = nullptr;
    BMenuItem *fJackItem = nullptr;
    BMenuItem *fHearItem = nullptr;
    BMessageRunner *fTick = nullptr;
    BMessageRunner *fStatusPump = nullptr;
    float fMenuHeight = 0;
    float fStatusHeight = 0;
};

} // namespace rktuner

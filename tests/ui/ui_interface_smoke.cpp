// Interface smoke test for the SubRec.Ui framework (UI_BATCH_PLAN.md §5).
// 1. Contract: every QML-facing backend class still exposes the frozen property / method /
//    signal names (a rename breaks this test, not the QML pages at runtime).
// 2. Defaults: unattached objects return the documented defaults.
// 3. Injection: setInstance() objects are what QML singletons resolve to.
// 4. Load: AppWindow instantiates offscreen with stub backends and keeps the main.cpp /
//    AutoTest contract (8 root signals, handleSettingItemClick, previewView, folderDialog).
// 5. Batch-2 shell stubs link and return their "not implemented" values; batch-2 members
//    added to batch-1 shell classes exist.
#include "shell/floating_bar_policy.h"
#include "shell/global_hotkeys.h"
#include "shell/key_chord.h"
#include "shell/shell_settings.h"
#include "shell/taskbar_overlay.h"
#include "shell/tray_controller.h"
#include "shell/window_controller.h"
#include "ui/backend/app_info.h"
#include "ui/backend/audio_level_monitor.h"
#include "ui/backend/desktop_actions.h"
#include "ui/backend/floating_bar_view.h"
#include "ui/backend/hotkey_controller.h"
#include "ui/backend/model_catalog.h"
#include "ui/backend/output_naming.h"
#include "ui/backend/output_naming_logic.h"
#include "ui/backend/recorder_view.h"
#include "ui/backend/recording_library.h"
#include "ui/backend/shell_view.h"
#include "ui/backend/subtitle_view.h"
#include "ui/backend/theme_controller.h"
#include "ui/backend/app_preferences.h"
#include "ui/backend/settings_store.h"

#include <QApplication>
#include <QFile>
#include <QMetaMethod>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlExpression>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtQml/qqmlextensionplugin.h>
#include <cstdio>
#include <initializer_list>
#include <type_traits>

Q_IMPORT_QML_PLUGIN(SubRec_UiPlugin)

namespace {

int g_failures = 0;

void check(bool ok, const char *what)
{
    if (!ok) {
        ++g_failures;
        std::fprintf(stderr, "FAIL: %s\n", what);
    }
}

void checkMembers(const QMetaObject &mo, std::initializer_list<const char *> properties,
                  std::initializer_list<const char *> methods)
{
    for (const char *p : properties) {
        const bool ok = mo.indexOfProperty(p) >= 0;
        if (!ok) std::fprintf(stderr, "FAIL: %s has no property %s\n", mo.className(), p);
        g_failures += ok ? 0 : 1;
    }
    for (const char *m : methods) {
        bool found = false;
        for (int i = 0; i < mo.methodCount(); ++i)
            found = found || mo.method(i).name() == m;
        if (!found) std::fprintf(stderr, "FAIL: %s has no method/signal %s\n", mo.className(), m);
        g_failures += found ? 0 : 1;
    }
}

// See singleton_provider.h: a default constructor would make Qt bypass create().
template <typename... T>
constexpr bool noneDefaultConstructible = (!std::is_default_constructible_v<T> && ...);
static_assert(noneDefaultConstructible<
                  subrec::ui::ThemeController, subrec::ui::AppPreferences, subrec::ui::RecorderView,
                  subrec::ui::ShellView, subrec::ui::OutputNaming, subrec::ui::RecordingLibrary,
                  subrec::ui::AudioLevelMonitor, subrec::ui::SubtitleView, subrec::ui::ModelCatalog,
                  subrec::ui::HotkeyController, subrec::ui::FloatingBarView,
                  subrec::ui::DesktopActions, subrec::ui::AppInfo>,
              "QML singletons must not be default-constructible");

// Stand-in for FFGLItem: main.cpp registers the real one as GLPreview/GLView.
class FakeGLView : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(bool keepRatio MEMBER keepRatio_)
public:
    using QQuickItem::QQuickItem;
private:
    bool keepRatio_ = false;
};

void checkContracts()
{
    using namespace subrec::ui;
    checkMembers(ThemeController::staticMetaObject, {"mode", "dark", "systemDark", "reduceMotion"}, {});
    checkMembers(AppPreferences::staticMetaObject, {"afterRecordAction", "windowGeometry", "windowMaximized"},
                 {"saveWindowState"});
    checkMembers(RecorderView::staticMetaObject,
                 {"captureState", "canStartCapture", "captureError", "captureRemediation",
                  "captureTimeText", "outputPath", "outputFileName", "videoResolution",
                  "selectedVideoEncoder", "selectedVideoEncoderIsHardware",
                  "selectedVideoEncoderDetail", "audioMode", "audioMixingAvailable",
                  "selectedMicrophoneDeviceId", "mixMicrophoneDeviceId", "captureDevices",
                  "selectedVideoDeviceId", "selectedAudioDeviceId", "screenCaptureBackend",
                  "screenCaptureBackendFromEnvironment", "audioSampler", "uiState", "capturing",
                  "hasVideoDevice", "selectedVideoKind", "elapsedText", "currentOutputFile"},
                 {"devicesOfKind", "refreshCaptureDevices", "selectVideoCaptureDevice",
                  "selectAudioCaptureDevice", "selectMicrophoneCaptureDevice", "setAudioMode",
                  "setScreenCaptureBackend", "openSource", "dismissCaptureError",
                  "recordingSaved", "recordingFailed"});
    // START INVARIANT: the recorder facade must never grow a start entry point.
    for (const char *forbidden : {"requestStart", "startRecord", "retryCapture"}) {
        bool found = false;
        const QMetaObject &mo = RecorderView::staticMetaObject;
        for (int i = 0; i < mo.methodCount(); ++i)
            found = found || mo.method(i).name() == forbidden;
        check(!found, "RecorderView must not expose a start/retry method");
    }
    checkMembers(ShellView::staticMetaObject,
                 {"windowShown", "trayAvailable", "trayEnabled", "minimizeToTray", "closeAction",
                  "startMinimized", "floatingBar", "excludeFromCapture", "hideOnRecordStart",
                  "confirmExitWhileRecording", "excludeSupported"},
                 {"requestClose", "requestMinimize", "resolveCloseChoice", "confirmExit",
                  "cancelExit", "requestStart", "requestStop", "requestPause", "requestResume",
                  "requestStartStop", "requestPauseResume", "showMainWindow",
                  "closeChoiceRequested", "exitConfirmRequested", "settingsPageRequested",
                  "startRejected", "pageRequested"});
    checkMembers(OutputNaming::staticMetaObject,
                 {"outputDir", "outputDirError", "fileNameTemplate", "templateError", "previewFileName"},
                 {"validateTemplate", "refreshPreview", "refreshDirStatus"});
    checkMembers(RecordingLibrary::staticMetaObject,
                 {"folder", "folderStatus", "totalCount", "totalBytes", "summaryText", "all", "recent"},
                 {"refresh", "moveToTrash"});
    checkMembers(RecordingListModel::staticMetaObject,
                 {"count", "filterText", "sortKey", "sortDescending", "limit"}, {"get"});
    checkMembers(AudioLevelMonitor::staticMetaObject,
                 {"active", "mixLevelDb", "perSourceAvailable", "systemLevelDb", "microphoneLevelDb"}, {});
    checkMembers(SubtitleView::staticMetaObject,
                 {"serviceAttached", "available", "state", "progress", "lagSeconds", "lastSrtPath", "lastError", "mode",
                  "enabled", "modelId", "maxCharsPerLine", "threads", "srtBom", "keepSpool",
                  "modelDir", "uiState", "settingsLocked", "currentJobVideoPath"},
                 {"openLastSrtFolder", "openModelDir", "modelDownloadPromptRequested",
                  "subtitleDone", "subtitleFailed"});
    checkMembers(ModelCatalog::staticMetaObject, {"models", "selectedModelReady", "anyDownloading"},
                 {"modelInfo", "requestDownload", "acceptLicenseAndDownload", "cancelDownload",
                  "remove", "licenseRequired", "downloadFinished", "downloadFailed"});
    checkMembers(HotkeyController::staticMetaObject,
                 {"toggleRecordText", "togglePauseText", "toggleWindowText", "toggleRecordStatus",
                  "togglePauseStatus", "toggleWindowStatus", "capturingAction", "captureText"},
                 {"beginCapture", "cancelCapture", "clear", "restoreDefaults", "shortcutLabel",
                  "captureFinished"});
    checkMembers(FloatingBarView::staticMetaObject,
                 {"shown", "barState", "elapsedText", "captureError", "subtitleIndicator", "hintVisible"},
                 {"attachWindow", "pauseResume", "stop", "openMainWindow", "dismiss",
                  "acknowledgeHint", "preferredPosition", "savePosition"});
    checkMembers(DesktopActions::staticMetaObject, {},
                 {"openFile", "revealInFolder", "openFolder", "openUrl", "copyText"});
    checkMembers(AppInfo::staticMetaObject,
                 {"version", "gitHash", "versionText", "logFilePath", "licenseFilePath",
                  "thirdPartyLicensesPath"}, {});

    // Batch-2 additions to the batch-1 shell classes (module H4 / FB1). Also forces the
    // harness shell_module (window_controller.cpp and its dependencies) to link.
    using namespace subrec::shell;
    checkMembers(WindowController::staticMetaObject, {"windowShown"},
                 {"onHotkeyConflicts", "startRejected", "pageRequested", "toggleWindow",
                  "requestStartStop", "requestPauseResume"});
    checkMembers(ShellSettings::staticMetaObject, {"floatingBar"}, {});
    checkMembers(TrayController::staticMetaObject, {}, {"floatingBarToggled"});
}

void checkDefaults()
{
    using namespace subrec::ui;
    RecorderView recorder(nullptr);
    check(recorder.uiState() == QLatin1String("noDevice"), "RecorderView unattached uiState is noDevice");
    check(!recorder.capturing(), "RecorderView unattached is not capturing");
    ShellView shell(nullptr);
    check(shell.closeAction() == QLatin1String("ask"), "ShellView default closeAction ask (C3)");
    check(shell.trayEnabled(), "ShellView default trayEnabled (C1)");
    check(shell.floatingBar() == QLatin1String("whenHidden"), "ShellView default floatingBar (C5)");
    check(shell.excludeFromCapture() == QLatin1String("recording"), "ShellView default exclude (C6)");
    SubtitleView subtitle(nullptr);
    check(subtitle.state() == QLatin1String("off") && !subtitle.enabled(), "Subtitle off by default");
    check(subtitle.maxCharsPerLine() == 18 && subtitle.threads() == 1, "asr defaults 18 / 1");
    OutputNaming naming(nullptr);
    check(naming.fileNameTemplate() == naming::defaultTemplate(), "default file-name template");
    RecordingLibrary library(nullptr);
    check(library.all() && library.recent(), "library models are never null");
    ModelCatalog catalog(nullptr);
    check(catalog.models() != nullptr, "model list is never null");

    // Batch-2 shell stubs.
    check(subrec::shell::validateChord({}) == subrec::shell::ChordValidity::Empty, "empty chord");
    check(!subrec::shell::floatingBarShouldShow({}), "floating bar hidden by default");
    subrec::shell::GlobalHotkeys hotkeys;
    check(hotkeys.status(subrec::shell::HotkeyAction::ToggleWindow) == subrec::shell::RegisterResult::Unset,
          "unbound toggle-window hotkey is Unset");
    subrec::shell::ShellSettings shellSettings(false);
    check(shellSettings.floatingBarMode() == subrec::shell::FloatingBarMode::Never,
          "floating bar Never when capture exclusion is unsupported (C5)");
    check(!shellSettings.floatingBarPosition(QStringLiteral("screen0")).has_value(),
          "no remembered floating bar position");
    subrec::shell::FloatingBarPolicy policy;
    policy.setSettings(&shellSettings);
    check(!policy.shouldShow(), "policy hidden by default");
    subrec::shell::TaskbarOverlay overlay;
    overlay.setKind(subrec::shell::TrayIconKind::Recording);
}

bool hasMethod(const QObject *object, const char *name)
{
    const QMetaObject *mo = object->metaObject();
    for (int i = 0; i < mo->methodCount(); ++i)
        if (mo->method(i).name() == name) return true;
    return false;
}

// U2 acceptance (UI_BATCH_PLAN.md §2.2): the component gallery must load offscreen and is
// the PC3 screenshot reference. Additive check only — no contract change.
void checkGallery()
{
    // Source path injected by tests/ui/CMakeLists.txt, so the check does not depend on
    // where the build directory sits relative to the sources.
    const QString galleryPath = QStringLiteral(SUBREC_UI_SOURCE_DIR)
        + QStringLiteral("/gallery/ComponentGallery.qml");
    if (!QFile::exists(galleryPath)) {
        check(false, "ComponentGallery.qml found next to the sources");
        return;
    }
    QQmlApplicationEngine engine;
    engine.load(QUrl::fromLocalFile(galleryPath));
    check(engine.rootObjects().size() == 1, "ComponentGallery loads offscreen");
    if (engine.rootObjects().isEmpty()) return;
    const QList<QObject *> all = engine.rootObjects().first()->findChildren<QObject *>();
    // Instantiated component set: one representative of every U2 group (recursive: the
    // dialog bodies and delegates hold some of them).
    for (const char *type : {"AppButton", "IconButton", "AppTextField", "AppComboBox", "AppSwitch",
                             "SegmentedControl", "AppCheckBox", "AppRadioButton", "AppProgressBar",
                             "Card", "NavItem", "InlineAlert", "StatusPill", "SubtitleBadge",
                             "LevelMeter", "RecordMainButton", "RecordPauseButton", "EmptyState",
                             "FormGroupTitle", "FormRow", "AppDialog", "ToastHost", "Spinner",
                             "AppIcon"}) {
        bool found = false;
        for (QObject *child : all)
            found = found || QString::fromLatin1(child->metaObject()->className()).contains(type);
        if (!found) std::fprintf(stderr, "FAIL: gallery has no %s\n", type);
        check(found, "gallery contains one of every U2 component");
    }
}

void checkLoad()
{
    using namespace subrec::ui;
    // Injected instances (what main.cpp will do in module INT-A).
    RecorderView recorder(nullptr);
    ShellView shell(nullptr);
    RecorderView::setInstance(&recorder);
    ShellView::setInstance(&shell);

    qmlRegisterType<FakeGLView>("GLPreview", 1, 0, "GLView");
    {
        QQmlApplicationEngine engine;
        engine.loadFromModule("SubRec.Ui", "AppWindow");
        check(engine.rootObjects().size() == 1, "AppWindow loads");
        if (engine.rootObjects().isEmpty()) return;
        QObject *root = engine.rootObjects().first();
        check(qobject_cast<QQuickWindow *>(root) != nullptr, "AppWindow root is a QQuickWindow");
        for (const char *signal : {"startRecording", "stopRecording", "pauseRecording", "readyRecording",
                                   "openScreen", "openCamera", "openSystemAudio", "openMicrophone",
                                   "handleSettingItemClick"})
            check(hasMethod(root, signal), signal);
        check(root->findChild<QObject *>(QStringLiteral("previewView")) != nullptr, "previewView exists");
        check(root->findChild<QObject *>(QStringLiteral("folderDialog")) != nullptr, "folderDialog exists");

        check(engine.singletonInstance<RecorderView *>("SubRec.Ui", "RecorderView") == &recorder,
              "RecorderView singleton is the injected instance");
        check(engine.singletonInstance<ShellView *>("SubRec.Ui", "ShellView") == &shell,
              "ShellView singleton is the injected instance");
        auto *fallback = engine.singletonInstance<SubtitleView *>("SubRec.Ui", "SubtitleView");
        check(fallback != nullptr, "un-injected singleton falls back to a default instance");

        QObject *nav = engine.singletonInstance<QObject *>("SubRec.Ui", "Navigation");
        check(nav != nullptr, "Navigation singleton");
        QMetaObject::invokeMethod(root, "handleSettingItemClick",
                                  Q_ARG(QVariant, QVariant(2)), Q_ARG(QVariant, QVariant(QStringLiteral("video-list"))));
        check(nav && nav->property("page").toString() == QLatin1String("library"),
              "handleSettingItemClick(2) opens the library");
        QMetaObject::invokeMethod(nav, "open", Q_ARG(QString, QStringLiteral("settings/hotkeys")));
        check(nav && nav->property("settingsPage").toString() == QLatin1String("hotkeys"),
              "Navigation.open(settings/hotkeys)");

        // PC3 1112 #1 / #3 (P3-a): the record page cannot paint over the title bar, the
        // title bar stays above every page, the resize frame stays above the title bar,
        // and the 1 px window outline is drawn while windowed. Structural only (no grabs).
        QObject *recordPage = root->findChild<QObject *>(QStringLiteral("recordPage"));
        check(recordPage != nullptr, "recordPage exists");
        if (recordPage) {
            check(recordPage->property("clip").toBool(), "RecordPage Flickable clips its content");
            QQmlExpression stopAtBounds(qmlContext(recordPage), recordPage,
                                        QStringLiteral("boundsBehavior === Flickable.StopAtBounds"));
            check(stopAtBounds.evaluate().toBool(), "RecordPage Flickable stops at bounds");
        }
        auto *titleBar = root->findChild<QQuickItem *>(QStringLiteral("titleBar"));
        auto *pageStack = root->findChild<QQuickItem *>(QStringLiteral("pageStack"));
        auto *outline = root->findChild<QQuickItem *>(QStringLiteral("windowOutline"));
        auto *resizeFrame = root->findChild<QQuickItem *>(QStringLiteral("windowResizeFrame"));
        check(titleBar && pageStack && outline && resizeFrame,
              "titleBar / pageStack / windowOutline / windowResizeFrame exist");
        if (titleBar && pageStack && outline && resizeFrame) {
            check(titleBar->z() > pageStack->z(), "title bar stacks above the pages");
            check(resizeFrame->z() > titleBar->z(), "resize frame stacks above the title bar");
            check(resizeFrame->z() > outline->z(), "resize frame stacks above the window outline");
            check(titleBar->findChild<QObject *>(QStringLiteral("titleBarClose")) != nullptr,
                  "close button lives in the title bar");
            auto *window = qobject_cast<QQuickWindow *>(root);
            const bool windowed = window && window->visibility() == QWindow::Windowed;
            check(outline->isVisible() == windowed, "window outline shown only while windowed");
        }
    }
    RecorderView::setInstance(nullptr);
    ShellView::setInstance(nullptr);
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QTemporaryDir store;
    subrec::ui::setUiSettingsIniPath(store.filePath(QStringLiteral("ui.ini")));

    checkContracts();
    checkDefaults();
    checkLoad();
    checkGallery();

    if (g_failures == 0) std::printf("ui_interface_smoke: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}

#include "ui_interface_smoke.moc"

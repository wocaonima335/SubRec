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
#include <QElapsedTimer>
#include <QFile>
#include <QMetaMethod>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlExpression>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtQml/qqmlextensionplugin.h>
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <memory>
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

// Spins the event loop until done() (polish / frame driven updates) or ~2 s pass. This
// target does not link QtTest, so no QTRY_*.
template <typename Done>
bool waitFor(Done done)
{
    QElapsedTimer timer;
    timer.start();
    while (!done() && timer.elapsed() < 2000)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    return done();
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

// Lets polish / positioners / the dialog enter transition run (no QtTest here).
void settle(int ms)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
}

QRectF sceneRect(const QQuickItem *item)
{
    return item->mapRectToScene(QRectF(0, 0, item->width(), item->height()));
}

QString rectText(const QRectF &rect)
{
    return QStringLiteral("(%1,%2 %3x%4)").arg(rect.x()).arg(rect.y()).arg(rect.width()).arg(rect.height());
}

// UI 1114 #3 (PC3 7d2539f: red button above the card's top edge at 970, over its right edge
// at 800, gone behind the source panel at <= 720, timer past the card at 560): the record
// buttons, the record caption, the timer, the save area and the folder button lie
// completely inside the ControlBar card (rects via mapRectToItem), visible and full size;
// the buttons stay clear of the timer, the save area and the folder button; the timer text
// fits its box. `where` names the width step in the failure output.
void checkControlBarContents(QQuickItem *bar, const QString &where)
{
    const QRectF card(0, 0, bar->width(), bar->height());
    const auto rectOf = [&](const char *name) -> QRectF {
        auto *item = bar->findChild<QQuickItem *>(QString::fromLatin1(name));
        if (!item) {
            std::fprintf(stderr, "FAIL: %s: ControlBar has no %s\n", qPrintable(where), name);
            ++g_failures;
            return {};
        }
        const QRectF rect = item->mapRectToItem(bar, QRectF(0, 0, item->width(), item->height()));
        const bool sized = item->isVisible() && item->width() > 0 && item->height() > 0;
        const bool inside = card.adjusted(-0.5, -0.5, 0.5, 0.5).contains(rect);
        if (!sized || !inside)
            std::fprintf(stderr, "  %s: %s %s (visible %d) not inside card %s\n", qPrintable(where), name,
                         qPrintable(rectText(rect)), item->isVisible(), qPrintable(rectText(card)));
        check(sized, "ControlBar item is visible and has a size");
        check(inside, "ControlBar item lies completely inside the card");
        return rect;
    };
    const QRectF record = rectOf("recordMainButton"), pause = rectOf("recordPauseButton"),
                 caption = rectOf("recordMainCaption"), timer = rectOf("recordTimer"),
                 save = rectOf("saveArea"), folder = rectOf("openOutputFolderButton");
    if (auto *main = bar->findChild<QQuickItem *>(QStringLiteral("recordMainButton"))) {
        check(main->width() == 64 && main->height() == 64, "record button keeps its 64x64 size");
        check(main->opacity() > 0, "record button is not transparent");
    }
    const bool clear = !record.intersects(timer) && !pause.intersects(timer) && !caption.intersects(timer)
        && !record.intersects(save) && !pause.intersects(save) && !caption.intersects(save)
        && !record.intersects(folder) && !pause.intersects(folder) && !caption.intersects(folder)
        && !timer.intersects(save);
    if (!clear)
        std::fprintf(stderr, "  %s: overlap: record %s pause %s caption %s timer %s save %s folder %s\n",
                     qPrintable(where), qPrintable(rectText(record)), qPrintable(rectText(pause)),
                     qPrintable(rectText(caption)), qPrintable(rectText(timer)), qPrintable(rectText(save)),
                     qPrintable(rectText(folder)));
    check(clear, "record buttons clear of the timer / save area / folder button");
    if (auto *timerText = bar->findChild<QQuickItem *>(QStringLiteral("recordTimer"))) {
        const qreal painted = timerText->property("contentWidth").toReal();
        if (painted > timerText->width() + 0.5)
            std::fprintf(stderr, "  %s: timer text %g wide in a %g box\n", qPrintable(where), painted,
                         timerText->width());
        check(painted > 0 && painted <= timerText->width() + 0.5, "timer text fits inside its box");
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

        // UI 1114 #1 (T2): ControlBar width = window - nav rail - 344 (sources 280, margins
        // 2×24, gap 16), and the source panel's bottom edge stays level with the ControlBar
        // when it switches to the 148 px compact layout. Only the current StackLayout page
        // is sized, so go back to the record page first.
        auto *window = qobject_cast<QQuickWindow *>(root);
        auto *controlBar = root->findChild<QQuickItem *>(QStringLiteral("controlBar"));
        auto *sourcePanel = root->findChild<QQuickItem *>(QStringLiteral("sourcePanel"));
        check(window && nav && recordPage && controlBar && sourcePanel, "controlBar / sourcePanel exist");
        if (window && nav && recordPage && controlBar && sourcePanel) {
            QMetaObject::invokeMethod(nav, "open", Q_ARG(QString, QStringLiteral("record")));
            if (!window->isVisible())
                window->show(); // offscreen: no real window; resize needs a platform window
            for (int w : {960, 1099, 1100, 1247, 1264, 1280}) {
                window->resize(w, 800);
                const qreal expected = w - (w < 1100 ? 64 : 200) - 344;
                // StackLayout re-lays out its current page on polish (next frame).
                waitFor([&] { return controlBar->width() == expected; });
                if (controlBar->width() != expected)
                    std::fprintf(stderr, "  window %d: controlBar width %g, expected %g\n", w,
                                 controlBar->width(), expected);
                check(controlBar->width() == expected, "ControlBar width follows the window width");
                check(controlBar->property("compact").toBool() == (expected < 720),
                      "ControlBar compact below 720");
                const qreal previewHeight = recordPage->property("previewHeight").toReal();
                check(std::abs(sourcePanel->height()
                               - qMax(previewHeight + 16 + controlBar->height(), qreal(534))) < 0.5,
                      "source panel bottom edge level with the ControlBar");
                checkControlBarContents(controlBar, QStringLiteral("window %1").arg(w));
            }

            // UI 1114 #3: the PC3 7d2539f screenshot widths (shots 04_win_dpi / 10_narrow_*).
            // These are window widths, the same convention as above: the frameless window has
            // no frame, so window width = content width = nav rail (64 below 1100) + page;
            // card = window - 64 - 344. 970 is above Theme.minWindowWidth (960); PC3 still
            // reached 900..560, so lower the minimum here to lay those widths out too.
            window->setMinimumWidth(560);
            for (int w : {970, 900, 800, 720, 640, 560}) {
                window->resize(w, 800);
                const qreal expected = w - 64 - 344;
                const QString where = QStringLiteral("window %1").arg(w);
                waitFor([&] { return window->width() == w && controlBar->width() == expected; });
                if (window->width() != w || controlBar->width() != expected)
                    std::fprintf(stderr, "  %s: window width %d, controlBar width %g, expected %g\n",
                                 qPrintable(where), window->width(), controlBar->width(), expected);
                check(window->width() == w, "window resizes below the 960 minimum when allowed");
                check(controlBar->width() == expected, "ControlBar width follows the window width");
                settle(50); // anything laid out on polish (save line Row) settles too
                checkControlBarContents(controlBar, where);

                // The card itself: inside the page viewport horizontally (the page scrolls
                // only vertically) and never under the source panel, which paints after it.
                const QRectF card = sceneRect(controlBar), sources = sceneRect(sourcePanel),
                             page = sceneRect(qobject_cast<QQuickItem *>(recordPage));
                const bool cardPlaced = card.left() >= page.left() - 0.5 && card.right() <= page.right() + 0.5
                    && !card.intersects(sources);
                if (!cardPlaced)
                    std::fprintf(stderr, "  %s: card %s page %s source panel %s\n", qPrintable(where),
                                 qPrintable(rectText(card)), qPrintable(rectText(page)),
                                 qPrintable(rectText(sources)));
                check(cardPlaced, "record card inside the page and clear of the source panel");
                const qreal previewHeight = recordPage->property("previewHeight").toReal();
                check(std::abs(sourcePanel->height()
                               - qMax(previewHeight + 16 + controlBar->height(), qreal(534))) < 0.5,
                      "source panel bottom edge level with the ControlBar");
            }
        }
    }
    RecorderView::setInstance(nullptr);
    ShellView::setInstance(nullptr);
}

// UI 1114 #1 (T1): the save area must never cover the record buttons or the folder button
// at any card width. Widths: 552 / 556 = narrowest card with the nav rail collapsed /
// expanded, 703/704 = old overlap edge, 719/720 = compact switch, 736 = 1280×800 window.
// Geometry only (no grabs).
void checkControlBarLayout()
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.loadFromModule("SubRec.Ui", "ControlBar");
    std::unique_ptr<QQuickItem> bar(qobject_cast<QQuickItem *>(component.create()));
    check(bar != nullptr, "ControlBar loads standalone");
    if (!bar) {
        std::fprintf(stderr, "  %s\n", qPrintable(component.errorString()));
        return;
    }
    auto rectOf = [&](const char *name) {
        auto *item = bar->findChild<QQuickItem *>(QString::fromLatin1(name));
        if (!item) std::fprintf(stderr, "FAIL: ControlBar has no %s\n", name);
        g_failures += item ? 0 : 1;
        return item ? item->mapRectToItem(bar.get(), QRectF(0, 0, item->width(), item->height())) : QRectF();
    };
    for (qreal w : {552.0, 556.0, 615.0, 691.0, 703.0, 704.0, 719.0, 720.0, 736.0, 1376.0}) {
        bar->setWidth(w);
        bar->setHeight(bar->implicitHeight());
        const QRectF record = rectOf("recordMainButton"), pause = rectOf("recordPauseButton"),
                     save = rectOf("saveArea"), folder = rectOf("openOutputFolderButton");
        if (save.intersects(record) ||save.intersects(pause) || save.intersects(folder))
            std::fprintf(stderr, "  width %g: save area overlaps a button\n", w);
        check(save.width() > 0 && save.height() > 0, "save area has a size");
        check(!save.intersects(record) &&!save.intersects(pause), "save area never overlaps the record buttons");
        check(!save.intersects(folder), "save area never overlaps the open-folder button");
        check(save.right() <= w && folder.right() <= w, "save area / folder button stay inside the card");
        check(save.bottom() <= bar->height() && folder.bottom() <= bar->height(),
              "save area / folder button stay inside the card height");
        check(bar->property("compact").toBool() == (w < 720), "ControlBar switches to compact below 720");
        check(bar->implicitHeight() == (w < 720 ? 148 : 104), "ControlBar height 104 wide / 148 compact");
        checkControlBarContents(bar.get(), QStringLiteral("card %1").arg(w));
    }
    // UI 1114 #3: cards of the PC3 7d2539f screenshot windows (card = window - 408 with the
    // nav rail collapsed): 970 -> 562, 900 -> 492, 800 -> 392, 720 -> 312, 640 -> 232,
    // 560 -> 152. Below the width where timer and buttons fit side by side the card stacks
    // timer, buttons and save line and grows instead.
    for (qreal w : {562.0, 492.0, 392.0, 312.0, 232.0, 152.0}) {
        bar->setWidth(w);
        bar->setHeight(bar->implicitHeight());
        const QString where = QStringLiteral("card %1 (window %2)").arg(w).arg(w + 408);
        check(bar->property("compact").toBool(), "ControlBar compact below 720");
        check(bar->implicitHeight() >= 148, "ControlBar never shorter than the compact 148");
        if (!bar->property("stacked").toBool())
            check(bar->implicitHeight() == 148, "side-by-side compact layout stays 148 high");
        checkControlBarContents(bar.get(), where);
    }
    // Design baseline (§3.2.2 C): 736 (1280×800 window) keeps the old coordinates.
    bar->setWidth(736);
    bar->setHeight(bar->implicitHeight());
    const QRectF save = rectOf("saveArea");
    check(save.x() == 440 && save.y() == 16 && save.width() == 224 && bar->implicitHeight() == 104,
          "wide layout unchanged at 736");
}

// Visible AppRadioButton / AppCheckBox instances that carry text (ModelRow's text-less
// radio is only the dot).
void collectChoiceControls(QQuickItem *item, QList<QQuickItem *> &out)
{
    if (!item || !item->isVisible()) return;
    const QString type = QString::fromLatin1(item->metaObject()->className());
    if (type.contains(QLatin1String("AppRadioButton")) || type.contains(QLatin1String("AppCheckBox"))) {
        if (!item->property("text").toString().isEmpty()) out.append(item);
        return;
    }
    for (QQuickItem *child : item->childItems())
        collectChoiceControls(child, out);
}

// The indicator must stay clear of the text area (contentItem minus its indicator reserve)
// and of every visible Text in it, with the text on the far side of `spacing`.
void checkChoiceControl(QQuickItem *control, const char *where)
{
    auto *indicator = control->property("indicator").value<QQuickItem *>();
    auto *content = control->property("contentItem").value<QQuickItem *>();
    if (!indicator || !content) {
        std::fprintf(stderr, "FAIL: %s: %s has no indicator / contentItem\n", where,
                     qPrintable(control->property("text").toString()));
        ++g_failures;
        return;
    }
    const bool mirrored = control->property("mirrored").toBool();
    const qreal spacing = control->property("spacing").toReal();
    const QRectF box = sceneRect(control);
    const QRectF ind = sceneRect(indicator);
    const QRectF area = sceneRect(content).adjusted(content->property("leftPadding").toReal(), 0,
                                                    -content->property("rightPadding").toReal(), 0);
    QRectF text;
    QQuickItem *firstLine = nullptr;
    for (QQuickItem *child : content->childItems()) {
        if (!child->isVisible() || !child->inherits("QQuickText") || child->width() <= 0) continue;
        text |= sceneRect(child);
        if (!firstLine) firstLine = child;
    }
    const bool clear = !text.isEmpty() && !ind.intersects(text) && !ind.intersects(area)
        && (mirrored ? text.right() <= ind.left() - spacing + 0.5 && area.right() <= ind.left() + 0.5
                     : text.left() >= ind.right() + spacing - 0.5 && area.left() >= ind.right() - 0.5);
    const bool inside = box.contains(ind);
    const bool textInside = text.left() >= box.left() - 0.5 && text.right() <= box.right() + 0.5;
    const QRectF line = firstLine ? sceneRect(firstLine) : QRectF();
    const bool level = firstLine && std::abs(line.center().y() - ind.center().y()) <= 2;
    if (!clear || !inside || !textInside || !level) {
        const auto r = [](const QRectF &rect) {
            return QStringLiteral("(%1,%2 %3x%4)").arg(rect.x()).arg(rect.y()).arg(rect.width()).arg(rect.height());
        };
        std::fprintf(stderr, "  %s: %s \"%s\"%s control %s indicator %s text %s first line %s area %s\n",
                     where, control->metaObject()->className(),
                     qPrintable(control->property("text").toString()), mirrored ? " (mirrored)" : "",
                     qPrintable(r(box)), qPrintable(r(ind)), qPrintable(r(text)), qPrintable(r(line)),
                     qPrintable(r(area)));
    }
    check(clear, "AppRadioButton / AppCheckBox text never overlaps the indicator");
    check(inside, "indicator stays inside the control");
    check(textInside, "AppRadioButton / AppCheckBox text stays inside the control");
    check(level, "indicator is level with the first text line");
}

// UI 1114 #2: every place that shows AppRadioButton / AppCheckBox with text, opened for real
// (popups lay out only while open). The components used to inherit Basic's padding 6 with
// the indicator at x = 0, so the text started 10 px inside the dot / box. Geometry only.
void checkChoiceControlLayout()
{
    struct Scenario {
        const char *name;
        int expected; // visible AppRadioButton / AppCheckBox with text
        const char *body;
    };
    // `dialog` (optional) is opened through openDialog(); hosts mirror ShellDialogs (an Item
    // filling the window holds the popup).
    const Scenario scenarios[] = {
        {"CloseWindowDialog", 3,
         "Item { anchors.fill: parent; CloseWindowDialog { id: dlg } }\n"
         "readonly property QtObject dialog: dlg\n"
         "function openDialog() { dlg.openFor(false, \"\") }\n"},
        {"SubtitleSettings", 2, "SubtitleSettings { width: parent.width }\n"},
        {"DeleteRecordingDialog", 1,
         "Item { anchors.fill: parent; DeleteRecordingDialog { id: dlg } }\n"
         "readonly property QtObject dialog: dlg\n"
         "function openDialog() { dlg.openFor(\"C:/rec/a.mp4\", \"C:/rec/a.srt\") }\n"},
        {"ModelLicenseDialog", 1,
         "Item { anchors.fill: parent; ModelLicenseDialog { id: dlg } }\n"
         "readonly property QtObject dialog: dlg\n"
         "function openDialog() { dlg.openFor(\"\", false) }\n"},
        // Right-to-left: the indicator moves to the right edge, the text stays clear of it.
        {"mirrored", 2,
         "Column {\n"
         "    LayoutMirroring.enabled: true; LayoutMirroring.childrenInherit: true\n"
         "    AppRadioButton { text: \"Quit\"; description: \"Stops and saves a running recording\" }\n"
         "    AppCheckBox { topPadding: 8; text: \"Remember my choice\"; secondaryText: \"(Settings)\" }\n"
         "}\n"},
        // A first line shorter than the 16-px indicator, on every platform. The Windows
        // offscreen CI job (no fonts for its FreeType database) lays bodyFont out under 16 px
        // and the radio dot used to sit above the control there.
        {"smallFont", 2,
         "Column {\n"
         "    AppRadioButton { font: Qt.font({ pixelSize: 9 }); text: \"Quit\"; description: \"Saves\" }\n"
         "    AppCheckBox { font: Qt.font({ pixelSize: 9 }); text: \"Remember my choice\" }\n"
         "}\n"},
    };
    for (const Scenario &scenario : scenarios) {
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData(QByteArray("import QtQuick\nimport SubRec.Ui\n"
                                     "Window {\n width: 960; height: 1200; visible: true\n")
                              + scenario.body + "}\n",
                          QUrl::fromLocalFile(QStringLiteral(SUBREC_UI_SOURCE_DIR "/choice_host_%1.qml")
                                                  .arg(QLatin1String(scenario.name))));
        std::unique_ptr<QObject> host(component.create());
        auto *window = qobject_cast<QQuickWindow *>(host.get());
        check(window != nullptr, "choice-control host window loads");
        if (!window) {
            std::fprintf(stderr, "  %s: %s\n", scenario.name, qPrintable(component.errorString()));
            continue;
        }
        if (auto *dialog = window->property("dialog").value<QObject *>()) {
            QMetaObject::invokeMethod(window, "openDialog");
            waitFor([&] { return dialog->property("opened").toBool(); });
            check(dialog->property("opened").toBool(), "dialog opens");
        }
        settle(200);
        QList<QQuickItem *> controls;
        collectChoiceControls(window->contentItem(), controls);
        if (controls.size() != scenario.expected)
            std::fprintf(stderr, "  %s: %lld AppRadioButton / AppCheckBox found, expected %d\n",
                         scenario.name, static_cast<long long>(controls.size()), scenario.expected);
        check(controls.size() == scenario.expected, "every AppRadioButton / AppCheckBox instance found");
        for (QQuickItem *control : controls) {
            check(control->property("mirrored").toBool() == (qstrcmp(scenario.name, "mirrored") == 0),
                  "LayoutMirroring reaches the choice controls");
            checkChoiceControl(control, scenario.name);
        }
    }
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
    checkControlBarLayout();
    checkChoiceControlLayout();
    checkGallery();

    if (g_failures == 0) std::printf("ui_interface_smoke: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}

#include "ui_interface_smoke.moc"

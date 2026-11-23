#include <QtTest>
#include "ui/backend/floating_bar_view.h"
#include "ui/backend/model_catalog.h"
#include "ui/backend/recorder_view.h"
#include "ui/backend/settings_store.h"
#include "ui/backend/subtitle_view.h"
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QVariantList>
#include <QtQml/QQmlComponent>
#include <QtQml/QQmlContext>
#include <QtQml/QQmlEngine>
#include <QtQml/qqmlextensionplugin.h>
Q_IMPORT_QML_PLUGIN(SubRec_UiPlugin)
using namespace subrec::ui;

// Work package F (design-live-v2.md §6.2–6.3, §12): the live-subtitle UI — forwarded
// current-line / degrade properties, the badge degrade texts (录完后生成 / 恢复中), the two
// 录制中显示 switches, and the ModelCatalog acceptLicense → requestDownload order with the
// selected model locked against deletion while recording. Service/manager are stubbed the
// way subtitle_view_tests.cpp stubs them; the floating-bar line itself only exists in
// SUBREC_HAS_SUBTITLE builds, so this build (which does not define it) checks the safe
// fallback: no line, 264-wide bar, no QML warnings.

namespace {
// The frozen SubtitleService surface F reads (subtitle/service/subtitle_service.h).
class LiveService : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString state READ state NOTIFY changed)
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(QString currentLine READ line NOTIFY currentLineChanged)
    Q_PROPERTY(QString currentConfirmed READ confirmed NOTIFY currentLineChanged)
    Q_PROPERTY(QString currentTentative READ tentative NOTIFY currentLineChanged)
    Q_PROPERTY(int degradeLevel READ level NOTIFY changed)
    Q_PROPERTY(QString degradeReason READ reason NOTIFY changed)
public:
    QString stateValue = "live";
    bool availableValue = true;
    QString confirmedText = "已确认的字";
    QString tentativeText = "未确认的字";
    int levelValue = 0;
    QString reasonText;
    QString state() const { return stateValue; }
    bool available() const { return availableValue; }
    QString line() const { return confirmedText + tentativeText; }
    QString confirmed() const { return confirmedText; }
    QString tentative() const { return tentativeText; }
    int level() const { return levelValue; }
    QString reason() const { return reasonText; }
    void push() { emit changed(); }
signals:
    void changed();
    void currentLineChanged();
};

// ModelManager stub whose invokables record the call order (acceptLicense → requestDownload).
class OrderManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList models READ models NOTIFY changed)
public:
    QString statusValue = "installed";
    QStringList calls;
    QVariantList models() const
    {
        return {QVariantMap{{"modelId", "sensevoice-small"}, {"status", statusValue},
                            {"inUse", false}, {"licenseVersion", "v1"},
                            {"licenseText", "Test licence text"}}};
    }
    Q_INVOKABLE void acceptLicense(const QString &, const QString &version)
    {
        calls << QStringLiteral("acceptLicense:") + version;
    }
    Q_INVOKABLE void requestDownload(const QString &)
    {
        calls << QStringLiteral("requestDownload");
        statusValue = QStringLiteral("downloading");
        emit changed();
    }
    Q_INVOKABLE QString remove(const QString &)
    {
        statusValue = QStringLiteral("notInstalled");
        emit changed();
        return {};
    }
signals:
    void changed();
};

// The recorder facade shape ControlBar reads (as in recorder_view_tests.cpp).
class FakeRecorder : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString captureState MEMBER captureState NOTIFY captureStateChanged)
    Q_PROPERTY(QString captureTimeText MEMBER captureTimeText NOTIFY captureTimeTextChanged)
    Q_PROPERTY(QString outputPath MEMBER outputPath NOTIFY outputPathChanged)
    Q_PROPERTY(QString outputFileName MEMBER outputFileName NOTIFY outputFileNameChanged)
    Q_PROPERTY(QVariantList captureDevices MEMBER captureDevices NOTIFY captureDevicesChanged)
public:
    QString captureState = "Running";
    QString captureTimeText = "00:12";
    QString outputPath = "/tmp";
    QString outputFileName = "session.mp4";
    QVariantList captureDevices = {QVariantMap{{"kind", "screen"}, {"available", true},
                                               {"name", "Screen"}}};
signals:
    void captureStateChanged();
    void captureTimeTextChanged();
    void outputPathChanged();
    void outputFileNameChanged();
    void captureDevicesChanged();
};
} // namespace

class SubtitleLiveUiTests : public QObject {
    Q_OBJECT
private slots:
    void forwardsLineAndDegradeProperties()
    {
        QTemporaryDir directory;
        setUiSettingsIniPath(directory.filePath("ui.ini"));
        SubtitleView view(nullptr);
        // Unattached: empty strings / 0, whatever the (not yet existing) service says.
        QCOMPARE(view.currentLine(), QString());
        QCOMPARE(view.currentConfirmed(), QString());
        QCOMPARE(view.currentTentative(), QString());
        QCOMPARE(view.degradeLevel(), 0);
        QCOMPARE(view.degradeReason(), QString());

        LiveService service;
        QSignalSpy lineSpy(&view, &SubtitleView::currentLineChanged);
        QSignalSpy serviceSpy(&view, &SubtitleView::serviceChanged);
        view.attach(&service, {});
        QCOMPARE(view.currentLine(), QStringLiteral("已确认的字未确认的字"));
        QCOMPARE(view.currentConfirmed(), QStringLiteral("已确认的字"));
        QCOMPARE(view.currentTentative(), QStringLiteral("未确认的字"));
        QCOMPARE(view.degradeLevel(), 0);
        QVERIFY(view.degradeReason().isEmpty());

        emit service.currentLineChanged();
        QCOMPARE(lineSpy.count(), 2); // one at attach, one from the signal just emitted
        service.levelValue = 3;
        service.reasonText = QStringLiteral("录完后生成");
        service.push();
        QCOMPARE(view.degradeLevel(), 3);
        QCOMPARE(view.degradeReason(), QStringLiteral("录完后生成"));
        QVERIFY(serviceSpy.count() >= 1);
        service.confirmedText.clear();
        service.tentativeText.clear();
        emit service.currentLineChanged();
        QVERIFY(view.currentLine().isEmpty());

        view.attach(nullptr, {});
        QVERIFY(view.currentLine().isEmpty());
        QCOMPARE(view.degradeLevel(), 0);
        QVERIFY(view.degradeReason().isEmpty());
        setUiSettingsIniPath({});
    }

    void displaySwitchDefaultsAndPersistence()
    {
        QTemporaryDir directory;
        setUiSettingsIniPath(directory.filePath("ui.ini"));
        SubtitleView view(nullptr);
        QVERIFY(view.showControlBarLine());
        QVERIFY(!view.showFloatingLine());
        QSignalSpy settingsSpy(&view, &SubtitleView::settingsChanged);
        view.setShowControlBarLine(false);
        view.setShowFloatingLine(true);
        QVERIFY(!view.showControlBarLine());
        QVERIFY(view.showFloatingLine());
        view.setShowControlBarLine(false); // no-op: value unchanged, no extra NOTIFY
        QCOMPARE(settingsSpy.count(), 2);
        // Still writable while locked: they only toggle what the UI shows (§12.3).
        view.setCapturing(true);
        QVERIFY(view.settingsLocked());
        view.setShowFloatingLine(false);
        QVERIFY(!view.showFloatingLine());
        setUiSettingsIniPath({});
    }

    void badgeDegradeTexts()
    {
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.loadFromModule("SubRec.Ui", QStringLiteral("SubtitleBadge"));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        std::unique_ptr<QObject> badge(component.create());
        QVERIFY(badge != nullptr);
        const auto label = [&badge] { return badge->property("__label").toString(); };

        badge->setProperty("uiState", QStringLiteral("live"));
        badge->setProperty("degradeLevel", 0);
        QCOMPARE(label(), QStringLiteral("字幕：识别中"));
        badge->setProperty("mode", QStringLiteral("after"));
        QCOMPARE(label(), QStringLiteral("字幕：录后生成"));

        badge->setProperty("mode", QStringLiteral("live"));
        badge->setProperty("degradeLevel", 3);
        badge->setProperty("degradeReason", QStringLiteral("优先保证录制流畅"));
        QCOMPARE(label(), QStringLiteral("字幕：录完后生成"));

        // §6.3: L3 never rises on its own — live+L3 → lagging is restoreLive() (恢复中)
        // until the level leaves L2 again.
        badge->setProperty("uiState", QStringLiteral("lagging"));
        badge->setProperty("degradeLevel", 2);
        QCOMPARE(label(), QStringLiteral("字幕：恢复中"));
        badge->setProperty("lagSeconds", 42.0);
        QCOMPARE(label(), QStringLiteral("字幕：恢复中"));
        badge->setProperty("degradeLevel", 1);
        badge->setProperty("uiState", QStringLiteral("live"));
        QCOMPARE(label(), QStringLiteral("字幕：识别中"));
        badge->setProperty("uiState", QStringLiteral("lagging"));
        badge->setProperty("degradeLevel", 2);
        QCOMPARE(label(), QStringLiteral("字幕：落后 42 秒"));
        // L3 → L4 keeps the failed wording, not 恢复中.
        badge->setProperty("degradeLevel", 3);
        badge->setProperty("uiState", QStringLiteral("live"));
        badge->setProperty("degradeLevel", 4);
        badge->setProperty("uiState", QStringLiteral("failed"));
        QCOMPARE(label(), QStringLiteral("字幕：失败"));
    }

    void controlBarLineFollowsSwitchPauseAndDegrade()
    {
        QTemporaryDir directory;
        setUiSettingsIniPath(directory.filePath("ui.ini"));
        LiveService service;
        SubtitleView view(nullptr);
        view.attach(&service, {});
        SubtitleView::setInstance(&view);
        FakeRecorder recorder;
        RecorderView recorderView(nullptr);
        recorderView.attach(&recorder, {});
        RecorderView::setInstance(&recorderView);
        QCOMPARE(recorderView.uiState(), QStringLiteral("running"));

        QQmlEngine engine;
        QSignalSpy warnings(&engine, &QQmlEngine::warnings);
        QQmlComponent component(&engine);
        component.loadFromModule("SubRec.Ui", QStringLiteral("ControlBar"));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        std::unique_ptr<QObject> object(component.create());
        QVERIFY(object != nullptr);
        QQuickWindow window;
        window.resize(1000, 800);
        auto *bar = qobject_cast<QQuickItem *>(object.get());
        bar->setParentItem(window.contentItem());
        bar->setWidth(656); // wide layout, no UI 1114 reflow involved
        bar->setHeight(bar->implicitHeight());
        auto *row = bar->findChild<QQuickItem *>(QStringLiteral("subtitleLineRow"));
        auto *confirmed = bar->findChild<QQuickItem *>(QStringLiteral("subtitleLineConfirmed"));
        auto *tentative = bar->findChild<QQuickItem *>(QStringLiteral("subtitleLineTentative"));
        QVERIFY(row && confirmed && tentative);
        QTRY_VERIFY(row->isVisible());
        QCOMPARE(confirmed->property("text").toString(), QStringLiteral("已确认的字"));
        QCOMPARE(tentative->property("text").toString(), QStringLiteral("未确认的字"));
        // The row sits under the timer block and clear of the buttons; heights unchanged.
        QVERIFY(row->mapRectToItem(bar, QRectF(0, 0, row->width(), row->height())).bottom()
                <= bar->height() + 0.5);
        QCOMPARE(bar->implicitHeight(), 104.0);

        view.setShowControlBarLine(false);
        QTRY_VERIFY(!row->isVisible());
        view.setShowControlBarLine(true);
        QTRY_VERIFY(row->isVisible());

        service.levelValue = 2; // §6.2: no current line from L2 on
        service.push();
        QTRY_VERIFY(!row->isVisible());
        service.levelValue = 0;
        service.push();
        QTRY_VERIFY(row->isVisible());

        recorder.captureState = QStringLiteral("Paused"); // 暂停期间清空（§12.2）
        emit recorder.captureStateChanged();
        QTRY_VERIFY(!row->isVisible());
        recorder.captureState = QStringLiteral("Running");
        emit recorder.captureStateChanged();
        QTRY_VERIFY(row->isVisible());

        service.confirmedText.clear();
        service.tentativeText.clear();
        emit service.currentLineChanged();
        QTRY_VERIFY(!row->isVisible());
        QCOMPARE(warnings.count(), 0);
        RecorderView::setInstance(nullptr);
        SubtitleView::setInstance(nullptr);
        setUiSettingsIniPath({});
    }

    void floatingBarLineFallsBackWhenUndefined()
    {
        // This build does not define SUBREC_HAS_SUBTITLE, so FloatingBarView has no
        // subtitleLine property: FloatingBar.qml's dynamic read must yield "" — bar stays
        // 264 wide, the line text hidden and zero-width, no QML warnings.
        FloatingBarView barView(nullptr);
        FloatingBarView::setInstance(&barView);
        QQmlEngine engine;
        QSignalSpy warnings(&engine, &QQmlEngine::warnings);
        QQmlComponent component(&engine);
        component.loadFromModule("SubRec.Ui", QStringLiteral("FloatingBar"));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        std::unique_ptr<QObject> object(component.create());
        QVERIFY(object != nullptr);
        auto *window = qobject_cast<QQuickWindow *>(object.get());
        QVERIFY(window != nullptr);
        QCOMPARE(window->width(), 264.0);
        auto *line = window->findChild<QQuickItem *>(QStringLiteral("subtitleLineText"));
        QVERIFY(line != nullptr);
        QVERIFY(!line->isVisible());
        QCOMPARE(line->width(), 0.0);
        QVERIFY(!window->property("subtitleLine").toString().isEmpty() == false);
        QCOMPARE(window->property("subtitleLine").toString(), QString());
        QCOMPARE(warnings.count(), 0);
        FloatingBarView::setInstance(nullptr);
    }

    void licenseOrderAndInUseLock()
    {
        QTemporaryDir directory;
        setUiSettingsIniPath(directory.filePath("ui.ini"));
        ModelCatalog catalog(nullptr);
        OrderManager manager;
        catalog.attach(&manager);
        catalog.acceptLicenseAndDownload(QStringLiteral("sensevoice-small"));
        // §10.3 E reflection contract: acceptance is recorded before the download starts.
        const QStringList expectedCalls{QStringLiteral("acceptLicense:v1"),
                                        QStringLiteral("requestDownload")};
        QCOMPARE(manager.calls, expectedCalls);

        LiveService service;
        service.stateValue = QStringLiteral("idle");
        SubtitleView view(nullptr);
        view.attach(&service, {});
        view.setModelCatalog(&catalog); // ignored once settings are locked
        manager.statusValue = QStringLiteral("installed");
        emit manager.changed();
        QVERIFY(catalog.selectedModelReady());
        QVERIFY(!view.settingsLocked());
        QVERIFY(!catalog.modelInfo(QStringLiteral("sensevoice-small"))
                     .value(QStringLiteral("inUse")).toBool());

        view.setCapturing(true); // recording: the selected model is in use, delete grayed
        QVERIFY(view.settingsLocked());
        QVERIFY(catalog.modelInfo(QStringLiteral("sensevoice-small"))
                    .value(QStringLiteral("inUse")).toBool());
        QVERIFY(!catalog.remove(QStringLiteral("sensevoice-small")).isEmpty());

        view.setCapturing(false);
        QVERIFY(!view.settingsLocked());
        QVERIFY(!catalog.modelInfo(QStringLiteral("sensevoice-small"))
                     .value(QStringLiteral("inUse")).toBool());
        QVERIFY(catalog.remove(QStringLiteral("sensevoice-small")).isEmpty());
        setUiSettingsIniPath({});
    }
};

QTEST_MAIN(SubtitleLiveUiTests)
#include "subtitle_live_ui_tests.moc"

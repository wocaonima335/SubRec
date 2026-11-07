#include <QtTest>
#include "ui/backend/subtitle_view.h"
#include "ui/backend/model_catalog.h"
#include "ui/backend/settings_store.h"
#include <QTemporaryDir>
#include <QQmlEngine>
#include <QQmlComponent>
#include <QQuickItem>
#include <QQuickWindow>
#include <QtQml/QQmlExtensionPlugin>
Q_IMPORT_QML_PLUGIN(SubRec_UiPlugin)
using namespace subrec::ui;

class Service : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString state MEMBER state NOTIFY changed)
    Q_PROPERTY(bool available MEMBER available NOTIFY availableChanged)
    Q_PROPERTY(QString lastSrtPath MEMBER path NOTIFY changed)
    Q_PROPERTY(QString lastError MEMBER error NOTIFY changed)
public:
    QString state = "idle";
    bool available = true;
    QString path = "/tmp/output.srt";
    QString error = "识别失败";
    int reloads = 0;
signals:
    void changed(QString value);
    void availableChanged(bool value);
};
class Manager : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList models READ models NOTIFY changed)
public:
    QString status = "notInstalled";
    bool inUse = false;
    QString licenseVersion = "v1";
    int downloads = 0;
    int cancels = 0;
    QVariantList models() const
    {
        return {QVariantMap{{"modelId", "sensevoice-small"}, {"status", status}, {"inUse", inUse},
                            {"licenseVersion", licenseVersion}, {"licenseText", "Test licence text"}}};
    }
    void setStatus(const QString &value)
    {
        status = value;
        emit changed();
    }
    Q_INVOKABLE void requestDownload(const QString &)
    {
        ++downloads;
        setStatus("downloading");
    }
    Q_INVOKABLE void cancelDownload(const QString &)
    {
        ++cancels;
    }
    Q_INVOKABLE QString remove(const QString &)
    {
        setStatus("notInstalled");
        return {};
    }
signals:
    void changed();
};

class SubtitleViewTests : public QObject {
    Q_OBJECT
private slots:
    void settingsAndDialogsLoad()
    {
        QTemporaryDir directory;
        setUiSettingsIniPath(directory.filePath("ui.ini"));
        Service service;
        ModelCatalog catalog(nullptr);
        SubtitleView view(nullptr);
        view.attach(&service, {});
        view.setModelCatalog(&catalog);
        ModelCatalog::setInstance(&catalog);
        SubtitleView::setInstance(&view);
        QQmlEngine engine;
        QSignalSpy warnings(&engine, &QQmlEngine::warnings);
        QQuickWindow window;
        window.resize(1280, 900);
        for (const auto *name : {"SettingsPage", "SubtitleSettings", "HotkeySettings",
                                 "ModelDownloadPromptDialog", "ModelLicenseDialog", "DeleteModelDialog"}) {
            QQmlComponent component(&engine);
            component.loadFromModule("SubRec.Ui", QString::fromLatin1(name));
            QVERIFY2(component.isReady(), qPrintable(component.errorString()));
            std::unique_ptr<QObject> object(component.create());
            QVERIFY2(object != nullptr, qPrintable(component.errorString()));
            if (auto *item = qobject_cast<QQuickItem *>(object.get())) {
                item->setParentItem(window.contentItem());
                item->setSize(QSizeF(1200, 800));
            }
            QCoreApplication::processEvents();
        }
        QCOMPARE(warnings.count(), 0);
        setUiSettingsIniPath({});
    }

    void defaultsSettingsAndStates()
    {
        QTemporaryDir directory;
        setUiSettingsIniPath(directory.filePath("ui.ini"));
        SubtitleView view(nullptr);
        QCOMPARE(view.uiState(), QString("off"));
        QVERIFY(!view.serviceAttached());
        QCOMPARE(view.maxCharsPerLine(), 18);
        QCOMPARE(view.threads(), 1);
        QVERIFY(view.srtBom());
        QVERIFY(!view.keepSpool());
        Service service;
        view.attach(&service, [&] { ++service.reloads; });
        view.setMode("after");
        view.setEnabled(false);
        view.setEnabled(true);
        QCOMPARE(view.mode(), QString("after"));
        view.setMaxCharsPerLine(24);
        view.setThreads(3);
        view.setSrtBom(false);
        view.setKeepSpool(true);
        QCOMPARE(view.maxCharsPerLine(), 24);
        QCOMPARE(view.threads(), 3);
        QVERIFY(!view.srtBom());
        QVERIFY(view.keepSpool());
        QVERIFY(service.reloads >= 7);
        QSignalSpy done(&view, &SubtitleView::subtitleDone);
        QSignalSpy failed(&view, &SubtitleView::subtitleFailed);
        for (const auto &state : {"idle", "live", "lagging", "finishing", "done", "failed", "unavailable"}) {
            service.state = state;
            emit service.changed(state);
            QCOMPARE(view.uiState(), state == QString("idle") ? QString("ready") : QString(state));
            if (view.settingsLocked()) {
                view.setThreads(1);
                QCOMPARE(view.threads(), 3);
            }
        }
        emit service.changed("unavailable");
        QCOMPARE(done.count(), 1);
        QCOMPARE(failed.count(), 1);
        service.state = "idle";
        service.available = false;
        emit service.changed("idle");
        QCOMPARE(view.uiState(), QString("unavailable"));
        view.setCapturing(true);
        view.setEnabled(false);
        QVERIFY(view.enabled());
        view.setCapturing(false);
        view.attach(nullptr, {});
        QCOMPARE(view.uiState(), QString("off"));
        setUiSettingsIniPath({});
    }
    void modelPromptLicenseDownloadChain()
    {
        QTemporaryDir directory;
        setUiSettingsIniPath(directory.filePath("ui.ini"));
        ModelCatalog catalog(nullptr);
        QSignalSpy completed(&catalog, &ModelCatalog::downloadFinished);
        catalog.acceptLicenseAndDownload("sensevoice-small");
        QVERIFY(!catalog.selectedModelReady());
        QCOMPARE(completed.count(), 0);
        Manager manager;
        manager.status = "installed";
        catalog.attach(&manager);
        QCOMPARE(completed.count(), 0);
        manager.setStatus("notInstalled");
        Service service;
        SubtitleView view(nullptr);
        view.attach(&service, {});
        view.setModelCatalog(&catalog);
        QSignalSpy prompt(&view, &SubtitleView::modelDownloadPromptRequested);
        QSignalSpy license(&catalog, &ModelCatalog::licenseRequired);
        view.setEnabled(true);
        QCOMPARE(prompt.count(), 1);
        QCOMPARE(view.uiState(), QString("needModel"));
        catalog.requestDownload(prompt.at(0).at(0).toString());
        QCOMPARE(license.count(), 1);
        QCOMPARE(manager.downloads, 0);
        catalog.acceptLicenseAndDownload(license.at(0).at(0).toString());
        QCOMPARE(manager.downloads, 1);
        QCOMPARE(view.uiState(), QString("downloading"));
        QCOMPARE(completed.count(), 0);
        manager.setStatus("installed");
        QCOMPARE(completed.count(), 1);
        QVERIFY(catalog.selectedModelReady());
        QCOMPARE(view.uiState(), QString("ready"));
        manager.setStatus("installed");
        QCOMPARE(completed.count(), 1);
        manager.inUse = true;
        manager.setStatus("installed");
        QVERIFY(!catalog.remove("sensevoice-small").isEmpty());
        catalog.cancelDownload("sensevoice-small");
        QCOMPARE(manager.cancels, 1);
        manager.licenseVersion = "v2";
        manager.setStatus("notInstalled");
        catalog.requestDownload("sensevoice-small");
        QCOMPARE(license.count(), 2);
        QCOMPARE(manager.downloads, 1);
        setUiSettingsIniPath({});
    }

};

QTEST_MAIN(SubtitleViewTests)
#include "subtitle_view_tests.moc"

#include <QtTest>
#include "ui/backend/output_naming.h"
#include "ui/backend/output_naming_logic.h"
#include "ui/backend/settings_store.h"
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
using namespace subrec::ui;

class OutputNamingTests : public QObject {
    Q_OBJECT
private slots:

    void templateRules()
    {
        const QDateTime when(QDate(2026, 11, 4), QTime(9, 8, 7));
        QCOMPARE(naming::expandTemplate("{日期}_{时间}_{来源}_{未知}", when, "屏/幕"), QString("20261104_090807_屏_幕_{未知}"));
        QVERIFY(naming::validateTemplate("{未知}").isEmpty());
        QVERIFY(!naming::validateTemplate("  ").isEmpty());
        for (const auto character : QString("\\/:*?\"<>|"))
            QVERIFY(!naming::validateTemplate(QString("test") + character).isEmpty());
        QStringList taken{"/tmp/test.mp4", "/tmp/test_2.mp4"};
        QCOMPARE(naming::uniqueFileName("/tmp", "test", ".mp4", [&](const QString &p) { return taken.contains(p); }), QString("test_3.mp4"));
    }
    void persistenceCollisionFailureAndAutotest()
    {
        QTemporaryDir directory;
        setUiSettingsIniPath(directory.filePath("ui.ini"));
        QObject recorder;
        recorder.setProperty("outputPath", directory.path());
        recorder.setProperty("outputFileName", "original.mp4");
        recorder.setProperty("captureState", "Idle");
        OutputNaming naming(nullptr);
        naming.setOutputDir(directory.path());
        naming.setFileNameTemplate("example");
        naming.attach(&recorder);
        QFile subtitle(directory.filePath("example.srt"));
        QVERIFY(subtitle.open(QIODevice::WriteOnly));
        subtitle.close();
        QCOMPARE(naming.prepareNextFile("屏幕"), directory.filePath("example_2.mp4"));
        OutputNaming restored(nullptr);
        QCOMPARE(restored.fileNameTemplate(), QString("example"));
        QCOMPARE(restored.outputDir(), directory.path());
        recorder.setProperty("captureState", "Running");
        naming.setFileNameTemplate("locked");
        QCOMPARE(naming.fileNameTemplate(), QString("example"));
        QVERIFY(naming.prepareNextFile("屏幕").isEmpty());
        recorder.setProperty("captureState", "Idle");
        naming.setFileNameTemplate("bad/name");
        QVERIFY(!naming.templateError().isEmpty());
        QCOMPARE(naming.fileNameTemplate(), QString("example"));
        naming.setOutputDir(subtitle.fileName());
        const auto beforePath = recorder.property("outputPath");
        const auto beforeName = recorder.property("outputFileName");
        QVERIFY(naming.prepareNextFile("屏幕").isEmpty());
        QCOMPARE(recorder.property("outputPath"), beforePath);
        QCOMPARE(recorder.property("outputFileName"), beforeName);
        qputenv("SUBREC_AUTOTEST_OUTPUT", directory.filePath("auto.mp4").toUtf8());
        recorder.setProperty("outputPath", directory.path());
        recorder.setProperty("outputFileName", "auto.mp4");
        restored.attach(&recorder);
        QCOMPARE(restored.prepareNextFile("屏幕"), directory.filePath("auto.mp4"));
        qunsetenv("SUBREC_AUTOTEST_OUTPUT");
        setUiSettingsIniPath({});
    }

};

QTEST_MAIN(OutputNamingTests)
#include "output_naming_tests.moc"

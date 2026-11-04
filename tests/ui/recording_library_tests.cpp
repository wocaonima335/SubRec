#include <QtTest>
#include "ui/backend/recording_library.h"
#include <QTemporaryDir>
#include <QFile>
#include <QQmlEngine>
#include <QQmlComponent>
#include <QQuickItem>
#include <QQuickWindow>
#include <QtQml/QQmlExtensionPlugin>
Q_IMPORT_QML_PLUGIN(SubRec_UiPlugin)
using namespace subrec::ui;

class RecordingLibraryTests : public QObject {
    Q_OBJECT
private slots:
    void qmlSortAndRowActions()
    {
        QQmlEngine engine;
        QQuickWindow window;
        window.resize(1100, 160);
        QQmlComponent headerComponent(&engine);
        headerComponent.loadFromModule("SubRec.Ui", "RecordingTableHeader");
        QVERIFY2(headerComponent.isReady(), qPrintable(headerComponent.errorString()));
        std::unique_ptr<QObject> header(headerComponent.create());
        QVERIFY(header);
        auto *headerItem = qobject_cast<QQuickItem *>(header.get());
        headerItem->setParentItem(window.contentItem());
        headerItem->setSize(QSizeF(1100, 40));
        QSignalSpy sorted(header.get(), SIGNAL(sortRequested(QString)));
        QQmlComponent rowComponent(&engine);
        rowComponent.loadFromModule("SubRec.Ui", "RecordingRow");
        QVERIFY2(rowComponent.isReady(), qPrintable(rowComponent.errorString()));
        std::unique_ptr<QObject> row(rowComponent.create());
        QVERIFY(row);
        auto *rowItem = qobject_cast<QQuickItem *>(row.get());
        rowItem->setParentItem(window.contentItem());
        rowItem->setPosition(QPointF(0, 60));
        rowItem->setSize(QSizeF(1100, 56));
        row->setProperty("selected", true);
        QSignalSpy play(row.get(), SIGNAL(playRequested()));
        QSignalSpy reveal(row.get(), SIGNAL(revealRequested()));
        QSignalSpy remove(row.get(), SIGNAL(deleteRequested()));
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto click = [&](QObject *owner, const char *name) {
            auto *item = owner->findChild<QQuickItem *>(QString::fromLatin1(name));
            QVERIFY(item);
            const auto center = item->mapToScene(QPointF(item->width() / 2, item->height() / 2));
            QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, center.toPoint());
        };
        click(header.get(), "sortName");
        QCOMPARE(sorted.last().at(0).toString(), QString("name"));
        click(header.get(), "sortModified");
        QCOMPARE(sorted.last().at(0).toString(), QString("modified"));
        click(header.get(), "sortSize");
        QCOMPARE(sorted.last().at(0).toString(), QString("size"));
        click(header.get(), "subtitleColumn");
        QCOMPARE(sorted.count(), 3);
        click(row.get(), "rowPlay");
        click(row.get(), "rowReveal");
        click(row.get(), "rowDelete");
        QCOMPARE(play.count(), 1);
        QCOMPARE(reveal.count(), 1);
        QCOMPARE(remove.count(), 1);
        row->setProperty("isRecording", true);
        click(row.get(), "rowPlay");
        click(row.get(), "rowReveal");
        click(row.get(), "rowDelete");
        QCOMPARE(play.count(), 1);
        QCOMPARE(reveal.count(), 2);
        QCOMPARE(remove.count(), 1);
    }

    void scanSortFilterAndActive()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        for (int n = 0; n < 1000; ++n) {
            QFile file(directory.filePath(QString("video%1.mp4").arg(n, 4, 10, QChar('0'))));
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(QByteArray(n % 7 + 1, 'x'));
        }
        RecordingLibrary library(nullptr);
        library.setFolder(directory.path());
        // Publication is asynchronous: the caller keeps the GUI event loop available.
        QCOMPARE(library.totalCount(), 0);
        QTRY_COMPARE_WITH_TIMEOUT(library.totalCount(), 1000, 10000);
        QCOMPARE(library.recent()->count(), 3);
        const auto recent = library.recent()->get(0);
        library.all()->setSortKey("name");
        library.all()->setSortDescending(false);
        QCOMPARE(library.all()->get(0).value("fileName").toString(), QString("video0000.mp4"));
        QCOMPARE(library.recent()->get(0), recent);
        library.all()->setFilterText("VIDEO000");
        QCOMPARE(library.all()->count(), 10);
        QCOMPARE(library.totalCount(), 1000);
        library.all()->setLimit(2);
        QCOMPARE(library.all()->count(), 2);
        QVERIFY(library.all()->get(-1).isEmpty());
        QVERIFY(library.all()->get(2).isEmpty());
        const auto path = library.all()->get(0).value("filePath").toString();
        library.setActiveRecording(path);
        QVERIFY(library.all()->get(0).value("isRecording").toBool());
        QCOMPARE(library.all()->get(0).value("sizeBytes").toLongLong(), -1);
        QVERIFY(!library.moveToTrash(path, true).isEmpty());
        library.setActiveRecording("");
        QVERIFY(!library.all()->get(0).value("isRecording").toBool());
        library.setSubtitleJob(path, "generating", 44);
        QCOMPARE(library.all()->get(0).value("subtitleProgress").toInt(), 44);
        library.all()->setSortKey("size");
        library.all()->setFilterText("");
        library.all()->setLimit(0);
        QCOMPARE(library.all()->get(0).value("sizeBytes").toLongLong(), 1);
    }
    void watcherAndStaleScan()
    {
        QTemporaryDir directory;
        RecordingLibrary library(nullptr);
        library.setFolder(directory.path());
        QSignalSpy changed(&library, &RecordingLibrary::contentsChanged);
        QTRY_VERIFY(!changed.isEmpty());
        QFile file(directory.filePath("new.mp4"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("video");
        file.close();
        QFile subtitle(directory.filePath("new.srt"));
        QVERIFY(subtitle.open(QIODevice::WriteOnly));
        subtitle.close();
        QTRY_COMPARE(library.totalCount(), 1);
        QCOMPARE(library.all()->get(0).value("subtitleState").toString(), QString("has"));
        library.refresh();
        library.setFolder(directory.filePath("missing"));
        QTRY_COMPARE(library.totalCount(), 0);
        QCOMPARE(library.folderStatus(), QString("missing"));
        RecordingListModel unowned;
        QCOMPARE(unowned.rowCount(), 0);
    }

};

QTEST_MAIN(RecordingLibraryTests)
#include "recording_library_tests.moc"

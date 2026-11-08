// U2 control regressions with real clicks in an offscreen window (PC3 report 2026-11-08,
// abc-verify/pc3-2769ea7/ui-diag-0328/DIAG.md). Each control is hosted in a tiny QML wrapper
// that binds it to a "backend" property the way the settings pages do. The ui.* settings
// store is redirected to a temporary INI file (the Theme singleton reads ThemeController).
#include <QtTest>
#include "ui/backend/settings_store.h"
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtQml/QQmlExtensionPlugin>
#include <memory>
Q_IMPORT_QML_PLUGIN(SubRec_UiPlugin)

namespace {

// Creates `qml` (a root Item) inside `window` and shows it; returns the root object.
std::unique_ptr<QObject> createHosted(QQmlEngine &engine, QQuickWindow &window, const QByteArray &qml)
{
    QQmlComponent component(&engine);
    component.setData(qml, QUrl(QStringLiteral("qrc:/ui_controls_tests/Host.qml")));
    if (!component.isReady()) {
        qWarning().noquote() << component.errorString();
        return {};
    }
    std::unique_ptr<QObject> root(component.create());
    if (auto *item = qobject_cast<QQuickItem *>(root.get()))
        item->setParentItem(window.contentItem());
    return root;
}

QPoint centerOf(const QQuickItem *item)
{
    return item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint();
}

} // namespace

class UiControlsTests : public QObject {
    Q_OBJECT
    QTemporaryDir settingsDir_;

private slots:
    void initTestCase()
    {
        QVERIFY(settingsDir_.isValid());
        subrec::ui::setUiSettingsIniPath(settingsDir_.filePath(QStringLiteral("ui.ini")));
    }
    void cleanupTestCase() { subrec::ui::setUiSettingsIniPath({}); }

    // AppComboBox: the popup must list the options and a click must select one.
    void comboBoxPopupListsAndSelects()
    {
        QQmlEngine engine;
        QQuickWindow window;
        window.resize(400, 400);
        auto host = createHosted(engine, window, R"(
import QtQuick
import SubRec.Ui
Item {
    id: host
    width: 400; height: 400
    property string backend: "b"
    property int activations: 0
    AppComboBox {
        objectName: "combo"
        x: 20; y: 20
        model: [ { text: "Alpha", value: "a" }, { text: "Beta", value: "b" }, { text: "Gamma", value: "c" } ]
        currentIndex: ["a", "b", "c"].indexOf(host.backend)
        onActivated: function (index) { host.activations++; host.backend = model[index].value }
    }
}
)");
        QVERIFY(host);
        auto *combo = host->findChild<QQuickItem *>(QStringLiteral("combo"));
        QVERIFY(combo);
        QCOMPARE(combo->property("currentIndex").toInt(), 1);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        auto *popup = combo->property("popup").value<QObject *>();
        QVERIFY(popup);
        QVERIFY(!popup->property("visible").toBool());
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, centerOf(combo));
        QTRY_VERIFY(popup->property("visible").toBool());

        auto *list = popup->property("contentItem").value<QQuickItem *>();
        QVERIFY(list);
        QCOMPARE(list->objectName(), QStringLiteral("comboPopupList"));
        QTRY_COMPARE(list->property("count").toInt(), 3);

        QQuickItem *row = nullptr;
        QTRY_VERIFY(QMetaObject::invokeMethod(list, "itemAtIndex", Q_RETURN_ARG(QQuickItem *, row),
                                              Q_ARG(int, 2)) && row && row->height() > 0);
        QCOMPARE(row->property("modelData").toMap().value(QStringLiteral("value")).toString(),
                 QStringLiteral("c"));

        QSignalSpy activated(combo, SIGNAL(activated(int)));
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, centerOf(row));
        QTRY_COMPARE(activated.count(), 1);
        QCOMPARE(activated.at(0).at(0).toInt(), 2);
        QCOMPARE(host->property("activations").toInt(), 1); // emitted once, not twice
        QCOMPARE(host->property("backend").toString(), QStringLiteral("c"));
        QCOMPARE(combo->property("currentIndex").toInt(), 2);
        QTRY_VERIFY(!popup->property("visible").toBool());

        // The owner's currentIndex binding survives the user pick.
        host->setProperty("backend", QStringLiteral("a"));
        QCOMPARE(combo->property("currentIndex").toInt(), 0);
    }
};

QTEST_MAIN(UiControlsTests)
#include "ui_controls_tests.moc"

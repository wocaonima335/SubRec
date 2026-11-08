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

    // AppSwitch: a click must reach the backend with the requested state (toggledTo), the
    // knob must follow the backend, a rejected write snaps back, and a locked switch
    // reports nothing. `legacyRead` records what `checked` reads inside an owner's plain
    // onToggled - the pattern GeneralSettings used before - as evidence for the handler
    // order (logged, not asserted: the order is exactly what must not be relied on).
    void switchClickReachesBackend()
    {
        QQmlEngine engine;
        QQuickWindow window;
        window.resize(200, 100);
        auto host = createHosted(engine, window, R"(
import QtQuick
import SubRec.Ui
Item {
    id: host
    width: 200; height: 100
    property bool backend: false
    property bool reject: false
    property bool legacyRead: false
    property int requests: 0
    AppSwitch {
        objectName: "sw"
        x: 20; y: 20
        backendChecked: host.backend
        onToggled: host.legacyRead = checked
        onToggledTo: function (value) { host.requests++; if (!host.reject) host.backend = value }
    }
}
)");
        QVERIFY(host);
        auto *sw = host->findChild<QQuickItem *>(QStringLiteral("sw"));
        QVERIFY(sw);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QSignalSpy toggledTo(sw, SIGNAL(toggledTo(bool)));

        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, centerOf(sw));
        QTRY_COMPARE(toggledTo.count(), 1);
        QCOMPARE(toggledTo.at(0).at(0).toBool(), true);
        QCOMPARE(host->property("backend").toBool(), true);
        QCOMPARE(sw->property("checked").toBool(), true);
        qInfo("legacy owner onToggled read checked=%d after an off->on click (1 = new state, "
              "0 = the component re-bound first)", int(host->property("legacyRead").toBool()));

        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, centerOf(sw));
        QTRY_COMPARE(toggledTo.count(), 2);
        QCOMPARE(toggledTo.at(1).at(0).toBool(), false);
        QCOMPARE(host->property("backend").toBool(), false);
        QCOMPARE(sw->property("checked").toBool(), false);

        // Backend refuses: the knob snaps back to the backend value.
        host->setProperty("reject", true);
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, centerOf(sw));
        QTRY_COMPARE(toggledTo.count(), 3);
        QCOMPARE(host->property("backend").toBool(), false);
        QCOMPARE(sw->property("checked").toBool(), false);

        // Locked: no request reaches the owner, the knob stays on the backend value.
        host->setProperty("reject", false);
        sw->setProperty("locked", true);
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, centerOf(sw));
        QTest::qWait(50);
        QCOMPARE(toggledTo.count(), 3);
        QCOMPARE(host->property("requests").toInt(), 3);
        QCOMPARE(host->property("backend").toBool(), false);
        QCOMPARE(sw->property("checked").toBool(), false);
    }
};

QTEST_MAIN(UiControlsTests)
#include "ui_controls_tests.moc"

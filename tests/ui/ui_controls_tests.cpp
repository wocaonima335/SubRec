// U2 control regressions with real clicks in an offscreen window (PC3 report 2026-11-08,
// abc-verify/pc3-2769ea7/ui-diag-0328/DIAG.md). Each control is hosted in a tiny QML wrapper
// that binds it to a "backend" property the way the settings pages do. The ui.* settings
// store is redirected to a temporary INI file (the Theme singleton reads ThemeController).
#include <QtTest>
#include "ui/backend/font_policy.h"
#include "ui/backend/recorder_view.h"
#include "ui/backend/settings_store.h"
#include <QFile>
#include <QJSValue>
#include <QScreen>
#include <QSvgRenderer>
#include <cmath>
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

// AppComboBox `model` (a JS array assigned from QML) as a list of maps.
QVariantList modelRows(const QObject *combo)
{
    QVariant value = combo->property("model");
    if (value.canConvert<QJSValue>())
        value = value.value<QJSValue>().toVariant();
    return value.toList();
}

QVariantMap device(const QString &id, const QString &kind, bool available = true,
                   const QString &reason = {})
{
    return {{QStringLiteral("id"), id}, {QStringLiteral("displayName"), id},
            {QStringLiteral("kind"), kind}, {QStringLiteral("available"), available},
            {QStringLiteral("unavailableReason"), reason}};
}

} // namespace

// Minimal device source in the shape RecorderView reads from FFRecorder (property names
// and maps as in capture/capture_types.cpp CaptureDevice::toVariantMap); same idea as the
// FakeRecorder of recorder_view_tests.cpp, reduced to the device properties.
class FakeDeviceSource : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList captureDevices MEMBER captureDevices NOTIFY captureDevicesChanged)
    Q_PROPERTY(QString selectedVideoDeviceId MEMBER selectedVideoDeviceId NOTIFY captureDevicesChanged)
    Q_PROPERTY(QString selectedAudioDeviceId MEMBER selectedAudioDeviceId NOTIFY captureDevicesChanged)
    Q_PROPERTY(QString selectedMicrophoneDeviceId MEMBER selectedMicrophoneDeviceId NOTIFY captureDevicesChanged)
public:
    QVariantList captureDevices;
    QString selectedVideoDeviceId = QStringLiteral("screen-1");
    QString selectedAudioDeviceId = QStringLiteral("wasapi:default");
    QString selectedMicrophoneDeviceId = QStringLiteral("mic-1");
signals:
    void captureDevicesChanged();
};

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

    // DeviceSettings: the three device lists follow captureDevicesChanged and label the
    // second line with the Chinese kind name (or the unavailable reason).
    void deviceListsFollowCaptureDevices()
    {
        FakeDeviceSource source;
        source.captureDevices = {device("screen-1", "screen"), device("wasapi:default", "system-audio"),
                                 device("mic-1", "microphone")};
        subrec::ui::RecorderView view(nullptr);
        view.attach(&source, {});
        subrec::ui::RecorderView::setInstance(&view);
        {
            QQmlEngine engine;
            QQuickWindow window;
            window.resize(900, 700);
            QQmlComponent component(&engine);
            component.loadFromModule("SubRec.Ui", "DeviceSettings");
            QVERIFY2(component.isReady(), qPrintable(component.errorString()));
            std::unique_ptr<QObject> page(component.create());
            QVERIFY(page);
            auto *pageItem = qobject_cast<QQuickItem *>(page.get());
            QVERIFY(pageItem);
            pageItem->setParentItem(window.contentItem());
            pageItem->setWidth(800);

            auto *video = page->findChild<QObject *>(QStringLiteral("videoDeviceCombo"));
            auto *system = page->findChild<QObject *>(QStringLiteral("systemAudioCombo"));
            auto *microphone = page->findChild<QObject *>(QStringLiteral("microphoneCombo"));
            QVERIFY(video && system && microphone);
            QCOMPARE(video->property("count").toInt(), 1);
            QCOMPARE(system->property("count").toInt(), 1);
            QCOMPARE(microphone->property("count").toInt(), 1);
            const auto secondLine = [](QObject *combo, int row) {
                return modelRows(combo).value(row).toMap().value(QStringLiteral("secondaryText")).toString();
            };
            QCOMPARE(secondLine(video, 0), QStringLiteral("屏幕"));
            QCOMPARE(secondLine(system, 0), QStringLiteral("系统声音"));
            QCOMPARE(secondLine(microphone, 0), QStringLiteral("麦克风"));

            // A refresh that finds more devices must reach the lists without reloading the page.
            source.captureDevices = {device("camera-1", "camera"), device("screen-1", "screen"),
                                     device("wasapi:default", "system-audio"), device("mic-1", "microphone"),
                                     device("mic-2", "microphone", false, QStringLiteral("设备被占用"))};
            emit source.captureDevicesChanged();
            QTRY_COMPARE(microphone->property("count").toInt(), 2);
            QCOMPARE(video->property("count").toInt(), 2);
            QCOMPARE(system->property("count").toInt(), 1);
            // Screens first, then cameras (page order), whatever the source order.
            QCOMPARE(modelRows(video).value(0).toMap().value(QStringLiteral("value")).toString(),
                     QStringLiteral("screen-1"));
            QCOMPARE(secondLine(video, 1), QStringLiteral("摄像头"));
            QCOMPARE(secondLine(microphone, 1), QStringLiteral("设备被占用"));
            QCOMPARE(modelRows(microphone).value(1).toMap().value(QStringLiteral("enabled")).toBool(), false);
        }
        subrec::ui::RecorderView::setInstance(nullptr);
    }

    // AppComboBox with options in the {text, value} shape the pages build: every popup row
    // shows its text and only the current value's row shows the check mark. (Raw device
    // maps without text/value gave empty rows, each checked - record page, PC3 report.)
    void comboRowsShowTextAndSingleCheck()
    {
        QQmlEngine engine;
        QQuickWindow window;
        window.resize(400, 400);
        auto host = createHosted(engine, window, R"(
import QtQuick
import SubRec.Ui
Item {
    width: 400; height: 400
    AppComboBox {
        objectName: "combo"
        x: 20; y: 20
        model: [ { text: "Screen 1", value: "screen-1", enabled: true },
                 { text: "Screen 2", value: "screen-2", enabled: true },
                 { text: "Screen 3", value: "screen-3", enabled: true } ]
        currentIndex: 1
    }
}
)");
        QVERIFY(host);
        auto *combo = host->findChild<QQuickItem *>(QStringLiteral("combo"));
        QVERIFY(combo);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QCOMPARE(combo->property("currentValue").toString(), QStringLiteral("screen-2"));
        QCOMPARE(combo->property("currentText").toString(), QStringLiteral("Screen 2"));
        auto *popup = combo->property("popup").value<QObject *>();
        QVERIFY(popup);
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, centerOf(combo));
        QTRY_VERIFY(popup->property("visible").toBool());
        auto *list = popup->property("contentItem").value<QQuickItem *>();
        QVERIFY(list);
        QTRY_COMPARE(list->property("count").toInt(), 3);
        int checked = 0;
        for (int i = 0; i < 3; ++i) {
            QQuickItem *row = nullptr;
            QTRY_VERIFY(QMetaObject::invokeMethod(list, "itemAtIndex", Q_RETURN_ARG(QQuickItem *, row),
                                                  Q_ARG(int, i)) && row);
            auto *text = row->findChild<QQuickItem *>(QStringLiteral("comboOptionText"));
            auto *mark = row->findChild<QQuickItem *>(QStringLiteral("comboCheckMark"));
            QVERIFY(text && mark);
            QCOMPARE(text->property("text").toString(), QStringLiteral("Screen %1").arg(i + 1));
            QCOMPARE(mark->isVisible(), i == 1);
            checked += mark->isVisible() ? 1 : 0;
        }
        QCOMPARE(checked, 1);
    }

    // SourcePanel (record page) maps RecorderView's raw device maps to {text, value} options
    // from the captureDevices property: names visible, current index on the selected id,
    // and the lists follow captureDevicesChanged.
    void sourcePanelMapsDevices()
    {
        FakeDeviceSource source;
        source.captureDevices = {device("screen-1", "screen"), device("screen-2", "screen"),
                                 device("wasapi:default", "system-audio"), device("mic-1", "microphone")};
        source.selectedVideoDeviceId = QStringLiteral("screen-2");
        subrec::ui::RecorderView view(nullptr);
        view.attach(&source, {});
        subrec::ui::RecorderView::setInstance(&view);
        {
            QQmlEngine engine;
            QQuickWindow window;
            window.resize(400, 900);
            QQmlComponent component(&engine);
            component.loadFromModule("SubRec.Ui", "SourcePanel");
            QVERIFY2(component.isReady(), qPrintable(component.errorString()));
            std::unique_ptr<QObject> panel(component.create());
            QVERIFY(panel);
            auto *panelItem = qobject_cast<QQuickItem *>(panel.get());
            QVERIFY(panelItem);
            panelItem->setParentItem(window.contentItem());
            panelItem->setWidth(280);

            auto *video = panel->findChild<QObject *>(QStringLiteral("sourceVideoCombo"));
            auto *system = panel->findChild<QObject *>(QStringLiteral("sourceSystemAudioCombo"));
            auto *microphone = panel->findChild<QObject *>(QStringLiteral("sourceMicrophoneCombo"));
            QVERIFY(video && system && microphone);
            QCOMPARE(video->property("count").toInt(), 2);
            const QVariantList rows = modelRows(video);
            QCOMPARE(rows.value(0).toMap().value(QStringLiteral("text")).toString(), QStringLiteral("screen-1"));
            QCOMPARE(rows.value(1).toMap().value(QStringLiteral("value")).toString(), QStringLiteral("screen-2"));
            QCOMPARE(video->property("currentIndex").toInt(), 1);
            QCOMPARE(video->property("currentText").toString(), QStringLiteral("screen-2"));
            QCOMPARE(system->property("currentText").toString(), QStringLiteral("wasapi:default"));
            QCOMPARE(microphone->property("currentText").toString(), QStringLiteral("mic-1"));

            source.captureDevices.append(device("mic-2", "microphone"));
            source.selectedMicrophoneDeviceId = QStringLiteral("mic-2");
            emit source.captureDevicesChanged();
            QTRY_COMPARE(microphone->property("count").toInt(), 2);
            QTRY_COMPARE(microphone->property("currentIndex").toInt(), 1);
            QCOMPARE(microphone->property("currentText").toString(), QStringLiteral("mic-2"));
        }
        subrec::ui::RecorderView::setInstance(nullptr);
    }

    // AppIcon rasterises its SVG at size x device pixel ratio (offscreen: ratio 1).
    void iconRasterisesAtDevicePixels()
    {
        QQmlEngine engine;
        QQuickWindow window;
        window.resize(100, 100);
        auto host = createHosted(engine, window, R"(
import QtQuick
import SubRec.Ui
Item {
    width: 100; height: 100
    AppIcon { objectName: "icon"; name: "settings"; size: 20 }
}
)");
        QVERIFY(host);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *icon = host->findChild<QQuickItem *>(QStringLiteral("icon"));
        QVERIFY(icon);
        QCOMPARE(icon->implicitWidth(), 20.0);
        const qreal ratio = qMax<qreal>(1.0, window.screen() ? window.screen()->devicePixelRatio() : 1.0);
        const int expected = int(std::ceil(20 * ratio));
        QTRY_COMPARE(icon->property("pixelSize").toInt(), expected);
        auto *image = icon->findChild<QQuickItem *>(QStringLiteral("appIconImage"));
        QVERIFY(image);
        QCOMPARE(image->property("sourceSize").toSize(), QSize(expected, expected));
        QVERIFY(image->property("smooth").toBool());
        QTRY_COMPARE(image->property("status").toInt(), 1); // Image.Ready: the SVG loaded
    }

    // Icon resources (ui/icons/ICONS_SOURCE.md): the 27 icons (25 iconfont + 2 project-drawn),
    // each a valid SVG with the 1024 viewBox, drawn in currentColor with no
    // DOCTYPE; the 11 removed Lucide-only names are gone (no fallback path at run time).
    void iconResourcesComplete()
    {
        const QString dir = QStringLiteral(":/qt/qml/SubRec/Ui/icons/");
        const QStringList used{
            QStringLiteral("video"), QStringLiteral("library"), QStringLiteral("settings"),
            QStringLiteral("minus"), QStringLiteral("maximize-2"), QStringLiteral("minimize-2"),
            QStringLiteral("x"), QStringLiteral("check"), QStringLiteral("chevron-down"),
            QStringLiteral("circle-alert"), QStringLiteral("triangle-alert"), QStringLiteral("circle-check"),
            QStringLiteral("info"), QStringLiteral("captions"), QStringLiteral("pause"),
            QStringLiteral("play"), QStringLiteral("app-window"), QStringLiteral("folder-open"),
            QStringLiteral("monitor-off"), QStringLiteral("monitor"), QStringLiteral("camera"),
            QStringLiteral("volume-2"), QStringLiteral("mic"), QStringLiteral("mic-off"),
            QStringLiteral("download"),
            // Drawn by the project (title bar maximize / restore).
            QStringLiteral("window-maximize"), QStringLiteral("window-restore")};
        QCOMPARE(used.size(), 27);
        for (const QString &name : used) {
            const QString path = dir + name + QStringLiteral(".svg");
            QFile file(path);
            QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(path));
            const QByteArray data = file.readAll();
            QVERIFY2(data.contains("fill=\"currentColor\""), qPrintable(path));
            QVERIFY2(!data.contains("<!DOCTYPE"), qPrintable(path));
            QSvgRenderer renderer(path);
            QVERIFY2(renderer.isValid(), qPrintable(path));
            QCOMPARE(renderer.viewBoxF(), QRectF(0, 0, 1024, 1024));
        }
        for (const char *removed : {"circle", "external-link", "file-text", "grip-vertical", "keyboard",
                                    "power", "refresh-cw", "search", "sliders-horizontal", "square",
                                    "trash-2"}) {
            const QString path = dir + QLatin1String(removed) + QStringLiteral(".svg");
            QVERIFY2(!QFile::exists(path), qPrintable(path));
        }
    }

    // §2.2: fallback lists via substitutions, application font families, Bold-only weights.
    // Last test: it changes process-wide font state (restored at the end).
    void fontPolicyAndThemeWeights()
    {
        const QFont before = QGuiApplication::font();
        subrec::ui::applyUiFontPolicy();
        // Qt 6.8.0 qtbase src/gui/text/qfont.cpp: insertSubstitutions() stores substitute
        // names lower-cased (:2014-2026) and substitutes() looks them up lower-cased
        // (:1975-1979), so the expected values are the fixed fallbacks in lower case.
        QCOMPARE(QFont::substitutes(QStringLiteral("Microsoft YaHei UI")), QStringList({QStringLiteral("segoe ui"), QStringLiteral("microsoft yahei")}));
        QCOMPARE(QFont::substitutes(QStringLiteral("Cascadia Mono")), QStringList({QStringLiteral("consolas")}));
        QCOMPARE(QGuiApplication::font().families(), subrec::ui::uiFontFamilies());

        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData(R"(
import QtQuick
import SubRec.Ui
QtObject {
    property string family: Theme.fontFamily
    property int h1: Theme.h1Font.weight
    property int h2: Theme.h2Font.weight
    property int strong: Theme.bodyStrongFont.weight
    property int badge: Theme.badgeFont.weight
    property int timer: Theme.timerFont.weight
    property int body: Theme.bodyFont.weight
}
)", QUrl(QStringLiteral("qrc:/ui_controls_tests/Fonts.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        std::unique_ptr<QObject> fonts(component.create());
        QVERIFY(fonts);
        QCOMPARE(fonts->property("family").toString(), subrec::ui::uiFontFamilies().first());
        for (const char *name : {"h1", "h2", "strong", "badge", "timer"})
            QCOMPARE(fonts->property(name).toInt(), int(QFont::Bold));
        QCOMPARE(fonts->property("body").toInt(), int(QFont::Normal));
        QGuiApplication::setFont(before);
    }
};

QTEST_MAIN(UiControlsTests)
#include "ui_controls_tests.moc"

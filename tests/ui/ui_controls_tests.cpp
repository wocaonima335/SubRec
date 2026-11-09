// U2 control regressions with real clicks in an offscreen window (PC3 report 2026-11-08,
// abc-verify/pc3-2769ea7/ui-diag-0328/DIAG.md). Each control is hosted in a tiny QML wrapper
// that binds it to a "backend" property the way the settings pages do. The ui.* settings
// store is redirected to a temporary INI file (the Theme singleton reads ThemeController).
#include <QtTest>
#include "opengl/preview_letterbox.h"
#include "ui/backend/caption_glyph_geometry.h"
#include "ui/backend/font_policy.h"
#include "ui/backend/recorder_view.h"
#include "ui/backend/settings_store.h"
#include <QColor>
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
#include <utility>
#include <vector>
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

// CaptionGlyph `polylines` (a JS array of arrays of Qt.point) as C++ polylines.
std::vector<subrec::ui::caption::Polyline> qmlPolylines(const QObject *glyph)
{
    const auto plain = [](QVariant value) {
        if (value.canConvert<QJSValue>())
            value = value.value<QJSValue>().toVariant();
        return value;
    };
    std::vector<subrec::ui::caption::Polyline> result;
    for (const QVariant &line : plain(glyph->property("polylines")).toList()) {
        subrec::ui::caption::Polyline points;
        for (const QVariant &point : plain(line).toList())
            points.push_back(plain(point).toPointF());
        result.push_back(points);
    }
    return result;
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

    // Settings sub-navigation: dense NavItems (36 px) 4 px apart, selection follows
    // Navigation.settingsPage, a click opens the page.
    void settingsNavIsDense()
    {
        QQmlEngine engine;
        QQuickWindow window;
        window.resize(1100, 800);
        QQmlComponent component(&engine);
        component.loadFromModule("SubRec.Ui", "SettingsPage");
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        std::unique_ptr<QObject> page(component.create());
        QVERIFY(page);
        auto *pageItem = qobject_cast<QQuickItem *>(page.get());
        QVERIFY(pageItem);
        pageItem->setParentItem(window.contentItem());
        pageItem->setSize(QSizeF(1100, 800));
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        // The sub-navigation Column, then its items (plain QML children, so findChild reaches
        // them; Repeater delegates would not be QObject children).
        auto *subNav = page->findChild<QQuickItem *>(QStringLiteral("settingsSubNav"));
        QVERIFY2(subNav, "settingsSubNav not found");
        auto *general = subNav->findChild<QQuickItem *>(QStringLiteral("settingsNav_general"));
        auto *output = subNav->findChild<QQuickItem *>(QStringLiteral("settingsNav_output"));
        QVERIFY2(general, "settingsNav_general not found under settingsSubNav");
        QVERIFY2(output, "settingsNav_output not found under settingsSubNav");
        QCOMPARE(general->property("dense").toBool(), true);
        QTRY_COMPARE(general->height(), 36.0);
        QCOMPARE(output->y() - general->y(), 36.0 + 4.0);

        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, centerOf(output));
        QTRY_VERIFY(output->property("selected").toBool());
        QVERIFY(!general->property("selected").toBool());
    }

    // Preview letterbox (opengl/preview_letterbox.h, used by FFGLRenderer for keepRatio).
    void previewLetterboxGeometry()
    {
        using preview::letterboxRect;
        // No frame yet, or keepRatio off: the whole target (stretch).
        QCOMPARE(letterboxRect(QSize(1600, 900), QSize(), true), QRect(0, 0, 1600, 900));
        QCOMPARE(letterboxRect(QSize(1600, 900), QSize(2560, 1080), false), QRect(0, 0, 1600, 900));
        // Same ratio: full target.
        QCOMPARE(letterboxRect(QSize(1600, 900), QSize(1920, 1080), true), QRect(0, 0, 1600, 900));
        // 21:9 source in 16:9: bars top and bottom, centred.
        QCOMPARE(letterboxRect(QSize(1600, 900), QSize(2560, 1080), true), QRect(0, 112, 1600, 675));
        // Portrait source in a square: bars left and right, centred.
        QCOMPARE(letterboxRect(QSize(900, 900), QSize(1080, 1920), true), QRect(197, 0, 506, 900));
        // Device-pixel target (150 % of 736x414): still fills the width for 16:9.
        QCOMPARE(letterboxRect(QSize(1104, 621), QSize(1920, 1080), true), QRect(0, 0, 1104, 621));
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

    // Caption glyph geometry (ui/backend/caption_glyph_geometry.h): strokes a whole number of
    // device pixels wide, every path coordinate on the device-pixel grid.
    void captionGlyphGeometry()
    {
        namespace cg = subrec::ui::caption;
        using Lines = std::vector<cg::Polyline>;
        using P = QPointF;
        const auto isWhole = [](qreal v) { return qAbs(v - std::round(v)) < 1e-9; };
        const QStringList kinds{QStringLiteral("minimize"), QStringLiteral("maximize"),
                                QStringLiteral("restore"), QStringLiteral("close")};

        // Stroke: the nearest whole device pixel, at least 1; logical width x ratio gives it back.
        const std::vector<std::pair<qreal, int>> strokes{{1.0, 1}, {1.25, 1}, {1.5, 2}, {1.75, 2},
                                                         {2.0, 2}, {2.5, 3}, {3.0, 3}};
        for (const auto &[dpr, px] : strokes) {
            const QString where = QStringLiteral("dpr %1").arg(dpr);
            QVERIFY2(cg::physicalStrokePx(dpr) == px, qPrintable(where));
            const qreal device = cg::logicalStroke(dpr) * dpr;
            QVERIFY2(isWhole(device) && qRound(device) == px && device >= 1.0, qPrintable(where));
        }

        // Snapping: on the grid, idempotent, at most half a device pixel away.
        for (qreal dpr : {1.0, 1.25, 1.5, 1.75, 2.0}) {
            const qreal half = cg::physicalStrokePx(dpr) / 2.0;
            for (int i = 0; i <= 160; ++i) {
                const qreal v = i / 10.0;
                const QString where = QStringLiteral("dpr %1, v %2").arg(dpr).arg(v);
                const qreal edge = cg::snapToPhysicalEdge(v, dpr);
                QVERIFY2(isWhole(edge * dpr), qPrintable(where));
                QVERIFY2(qAbs(edge - v) <= 0.5 / dpr + 1e-9, qPrintable(where));
                QVERIFY2(qAbs(cg::snapToPhysicalEdge(edge, dpr) - edge) < 1e-9, qPrintable(where));
                const qreal centre = cg::snapToPhysicalCenter(v, dpr);
                QVERIFY2(isWhole(centre * dpr - half), qPrintable(where));
                QVERIFY2(qAbs(centre - v) <= 0.5 / dpr + 1e-9, qPrintable(where));
                QVERIFY2(qAbs(cg::snapToPhysicalCenter(centre, dpr) - centre) < 1e-9, qPrintable(where));
            }
        }

        // 100 %: the Windows 11 shapes on a 10 px box at (3, 3) in the 16 px item, 1 px strokes.
        QVERIFY(cg::glyphPolylines(QStringLiteral("minimize"), 1.0) == Lines({{P(3, 8.5), P(13, 8.5)}}));
        QVERIFY(cg::glyphPolylines(QStringLiteral("maximize"), 1.0)
                == Lines({{P(8, 3.5), P(12.5, 3.5), P(12.5, 12.5), P(3.5, 12.5), P(3.5, 3.5), P(8, 3.5)}}));
        QVERIFY(cg::glyphPolylines(QStringLiteral("restore"), 1.0)
                == Lines({{P(7, 5.5), P(10.5, 5.5), P(10.5, 12.5), P(3.5, 12.5), P(3.5, 5.5), P(7, 5.5)},
                          {P(5.5, 5), P(5.5, 3.5), P(12.5, 3.5), P(12.5, 10.5), P(11, 10.5)}}));
        QVERIFY(cg::glyphPolylines(QStringLiteral("close"), 1.0)
                == Lines({{P(3.5, 3.5), P(12.5, 12.5)}, {P(12.5, 3.5), P(3.5, 12.5)}}));
        // 125 %: box edges at device px 4 and 16 (12 px box), stroke 1 device px = 0.8 logical.
        QVERIFY(cg::glyphPolylines(QStringLiteral("minimize"), 1.25) == Lines({{P(3.2, 8.4), P(12.8, 8.4)}}));
        QVERIFY(cg::glyphPolylines(QStringLiteral("maximize"), 1.25)
                == Lines({{P(8, 3.6), P(12.4, 3.6), P(12.4, 12.4), P(3.6, 12.4), P(3.6, 3.6), P(8, 3.6)}}));

        // Every kind at every scale: inside the item, each coordinate a device-pixel edge or a
        // stroke centre; the straight kinds only have horizontal and vertical segments.
        for (qreal dpr : {1.0, 1.25, 1.5, 1.75, 2.0}) {
            const qreal half = cg::physicalStrokePx(dpr) / 2.0;
            for (const QString &kind : kinds) {
                const QString where = QStringLiteral("%1 @ %2").arg(kind).arg(dpr);
                const Lines lines = cg::glyphPolylines(kind, dpr);
                QVERIFY2(!lines.empty(), qPrintable(where));
                for (const cg::Polyline &line : lines) {
                    QVERIFY2(line.size() >= 2, qPrintable(where));
                    for (std::size_t i = 0; i < line.size(); ++i) {
                        for (qreal c : {line[i].x(), line[i].y()}) {
                            QVERIFY2(c >= 0 && c <= 16, qPrintable(where));
                            QVERIFY2(isWhole(c * dpr) || isWhole(c * dpr - half), qPrintable(where));
                        }
                        if (kind != QLatin1String("close") && i > 0) {
                            QVERIFY2(qFuzzyCompare(line[i].x(), line[i - 1].x())
                                         || qFuzzyCompare(line[i].y(), line[i - 1].y()),
                                     qPrintable(where));
                        }
                    }
                }
            }
            // Maximize: closed (ends where it starts), outer box within 1 device px of 10 logical px.
            const cg::Polyline box = cg::glyphPolylines(QStringLiteral("maximize"), dpr).front();
            QVERIFY(box.front() == box.back());
            const qreal outer = (box[1].x() - box[3].x()) * dpr + 2 * half;
            QVERIFY2(isWhole(outer) && qAbs(outer - 10 * dpr) <= 1.0, qPrintable(QString::number(dpr)));
            // Restore: the back box starts on the front box's top edge, ends on its right edge,
            // and at least one free device pixel separates the two top strokes.
            const Lines restore = cg::glyphPolylines(QStringLiteral("restore"), dpr);
            QCOMPARE(restore.size(), std::size_t(2));
            const cg::Polyline &front = restore[0];
            const cg::Polyline &back = restore[1];
            QVERIFY(front.front() == front.back());
            QVERIFY(qFuzzyCompare(back.front().y(), front[0].y() - half / dpr));
            QVERIFY(qFuzzyCompare(back.back().x(), front[1].x() + half / dpr));
            QVERIFY((front[0].y() - back[1].y()) * dpr >= 2 * half + 1 - 1e-9);
        }

        // Anything else draws nothing.
        QVERIFY(cg::glyphPolylines(QStringLiteral("x"), 1.0).empty());
    }

    // CaptionGlyph.qml computes the same stroke and paths as the header and hands them to
    // its ShapePath unchanged (flat caps, miter joins, no fill, `color` as the stroke).
    void captionGlyphMatchesHeader()
    {
        namespace cg = subrec::ui::caption;
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.loadFromModule("SubRec.Ui", "CaptionGlyph");
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        const QColor stroke(0x12, 0x34, 0x56);
        for (const char *kind : {"minimize", "maximize", "restore", "close"}) {
            for (qreal dpr : {1.0, 1.25, 1.5, 1.75, 2.0}) {
                const QString where = QStringLiteral("%1 @ %2").arg(QLatin1String(kind)).arg(dpr);
                std::unique_ptr<QObject> glyph(component.createWithInitialProperties(
                    {{QStringLiteral("kind"), QString::fromLatin1(kind)},
                     {QStringLiteral("devicePixelRatio"), dpr},
                     {QStringLiteral("color"), stroke}}));
                QVERIFY2(glyph, qPrintable(where));
                QVERIFY2(glyph->property("strokePhysical").toInt() == cg::physicalStrokePx(dpr), qPrintable(where));
                QVERIFY2(qFuzzyCompare(glyph->property("strokeLogical").toReal(), cg::logicalStroke(dpr)),
                         qPrintable(where));

                const std::vector<cg::Polyline> expected = cg::glyphPolylines(QLatin1String(kind), dpr);
                const std::vector<cg::Polyline> actual = qmlPolylines(glyph.get());
                QVERIFY2(actual.size() == expected.size(), qPrintable(where));
                for (std::size_t i = 0; i < expected.size(); ++i) {
                    QVERIFY2(actual[i].size() == expected[i].size(), qPrintable(where));
                    for (std::size_t j = 0; j < expected[i].size(); ++j) {
                        QVERIFY2(qAbs(actual[i][j].x() - expected[i][j].x()) < 1e-9
                                     && qAbs(actual[i][j].y() - expected[i][j].y()) < 1e-9,
                                 qPrintable(where));
                    }
                }

                auto *shape = glyph->findChild<QQuickItem *>(QStringLiteral("captionGlyphShape"));
                QVERIFY2(shape, qPrintable(where));
                QCOMPARE(shape->position(), QPointF(0, 0));
                QCOMPARE(shape->size(), QSizeF(16, 16));
                // Aliased except the diagonal X.
                QCOMPARE(shape->antialiasing(), qstrcmp(kind, "close") == 0);
                auto *path = glyph->findChild<QObject *>(QStringLiteral("captionGlyphPath"));
                QVERIFY2(path, qPrintable(where));
                QVERIFY2(qFuzzyCompare(path->property("strokeWidth").toReal(), cg::logicalStroke(dpr)),
                         qPrintable(where));
                QCOMPARE(path->property("strokeColor").value<QColor>(), stroke);
                QCOMPARE(path->property("fillColor").value<QColor>().alpha(), 0);
                QCOMPARE(path->property("capStyle").toInt(), int(Qt::FlatCap));
                QCOMPARE(path->property("joinStyle").toInt(), int(Qt::MiterJoin));
            }
        }
    }

    // Title bar: the three window buttons show CaptionGlyphs (not AppIcons) at whole logical
    // px, and the maximize button switches maximize <-> restore with the window state. A plain
    // IconButton still shows its AppIcon.
    void titleBarCaptionGlyphs()
    {
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData(R"(
import QtQuick
import SubRec.Ui
Window {
    id: host
    width: 800; height: 300
    TitleBar { window: host; width: host.width }
    IconButton { objectName: "plainButton"; y: 100; iconName: "settings" }
}
)", QUrl(QStringLiteral("qrc:/ui_controls_tests/TitleBarHost.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        std::unique_ptr<QObject> root(component.create());
        auto *window = qobject_cast<QQuickWindow *>(root.get());
        QVERIFY(window);
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window));

        const auto part = [&](const char *button, const char *name) -> QQuickItem * {
            auto *b = root->findChild<QQuickItem *>(QLatin1String(button));
            auto *content = b ? b->property("contentItem").value<QQuickItem *>() : nullptr;
            return content ? content->findChild<QQuickItem *>(QLatin1String(name)) : nullptr;
        };
        const auto glyphOf = [&](const char *button) -> QQuickItem * {
            QQuickItem *loader = part(button, "captionGlyphLoader");
            return loader ? qobject_cast<QQuickItem *>(loader->property("item").value<QObject *>()) : nullptr;
        };

        const std::vector<std::pair<const char *, const char *>> buttons{
            {"titleBarMinimize", "minimize"}, {"titleBarMaximize", "maximize"}, {"titleBarClose", "close"}};
        for (const auto &[name, kind] : buttons) {
            auto *button = root->findChild<QQuickItem *>(QLatin1String(name));
            QVERIFY2(button, name);
            QCOMPARE(button->property("captionKind").toString(), QString::fromLatin1(kind));
            QCOMPARE(button->size(), QSizeF(46, 40));
            QQuickItem *glyph = glyphOf(name);
            QVERIFY2(glyph, name);
            QCOMPARE(glyph->property("kind").toString(), QString::fromLatin1(kind));
            QCOMPARE(glyph->size(), QSizeF(16, 16));
            QCOMPARE(glyph->mapToItem(button, QPointF(0, 0)), QPointF(15, 12));
            QQuickItem *icon = part(name, "iconButtonIcon");
            QVERIFY2(icon, name);
            QVERIFY2(!icon->property("visible").toBool(), name);
            // Same colour rule as the AppIcon branch.
            QCOMPARE(glyph->property("color").value<QColor>(), icon->property("color").value<QColor>());
        }

        auto *maximize = root->findChild<QQuickItem *>(QStringLiteral("titleBarMaximize"));
        QVERIFY(maximize);
        window->showMaximized();
        QTRY_COMPARE(maximize->property("captionKind").toString(), QStringLiteral("restore"));
        QVERIFY(glyphOf("titleBarMaximize"));
        QTRY_COMPARE(glyphOf("titleBarMaximize")->property("kind").toString(), QStringLiteral("restore"));
        window->showNormal();
        QTRY_COMPARE(maximize->property("captionKind").toString(), QStringLiteral("maximize"));
        QTRY_COMPARE(glyphOf("titleBarMaximize")->property("kind").toString(), QStringLiteral("maximize"));

        QVERIFY(root->findChild<QQuickItem *>(QStringLiteral("plainButton")));
        QVERIFY(!glyphOf("plainButton"));
        QQuickItem *plainIcon = part("plainButton", "iconButtonIcon");
        QVERIFY(plainIcon);
        QVERIFY(plainIcon->property("visible").toBool());
        QVERIFY(plainIcon->width() > 0 && plainIcon->height() > 0);
        QCOMPARE(plainIcon->property("name").toString(), QStringLiteral("settings"));
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

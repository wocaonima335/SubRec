// 反射约定：属性/方法名与 ui/backend/model_catalog.cpp 的调用方式完全一致。
// - property("models") + NOTIFY 信号（model_catalog.cpp:103-110、:215）
// - invokeMethod "requestDownload"/"cancelDownload"（:181、:188）
// - invokeMethod "remove" + Q_RETURN_ARG(QString)（:201）
// - acceptLicense(modelId, licenseVersion)：model_catalog.cpp:171-183 的 acceptLicenseAndDownload 流程
//   先记录接受、再 requestDownload（当前 ui 侧写自己的 QSettings，由集成（F/G）补调本方法，
//   见 model_manager.cpp 文件头与工作包文档）。

#include "subtitle_models_tests.h"
#include "test_support.h"

#include <QMetaMethod>
#include <QMetaProperty>

void SubtitleModelsTests::reflection_contract()
{
    const QMetaObject *meta = &ModelManager::staticMetaObject;

    // property("models")，带 NOTIFY（model_catalog.cpp:103-110）
    const int propertyIndex = meta->indexOfProperty("models");
    QVERIFY(propertyIndex >= 0);
    const QMetaProperty property = meta->property(propertyIndex);
    QVERIFY(property.hasNotifySignal());
    QCOMPARE(QByteArray(property.typeName()), QByteArrayLiteral("QVariantList"));

    // invokeMethod 的方法签名（model_catalog.cpp:181/:188/:201；acceptLicense 为 E 的入口）
    const struct {
        const char *signature;
        bool returnsString;
    } methods[] = {
        {"requestDownload(QString)", false},
        {"cancelDownload(QString)", false},
        {"remove(QString)", true},
        {"acceptLicense(QString,QString)", false},
    };
    for (const auto &m : methods) {
        const int index = meta->indexOfMethod(m.signature);
        QVERIFY2(index >= 0, m.signature);
        const QMetaMethod method = meta->method(index);
        // void 方法的返回元类型是 QMetaType::Void（43），QString 返回是 QMetaType::QString
        QCOMPARE(method.returnMetaType().id(),
                 int(m.returnsString ? QMetaType::QString : QMetaType::Void));
    }

    // 实际反射调用一遍（临时根目录 + 本地服务器）
    TestEnv env;
    TestHttpServer server;
    QVERIFY(server.start());
    const QByteArray body = makeBytes(2048, 3);
    server.addRoute({QStringLiteral("/m.bin"), body});
    TestModelSpec spec;
    spec.files.append({QStringLiteral("m.bin"), body, {server.url(QStringLiteral("/m.bin")).toString()}});
    auto manager = env.makeManager(env.writeManifest({spec}));

    QVERIFY(QMetaObject::invokeMethod(manager.get(), "acceptLicense",
                                      Q_ARG(QString, spec.id), Q_ARG(QString, spec.licenseVersion)));
    QSignalSpy finishedSpy(manager.get(), &ModelManager::downloadFinished);
    QSignalSpy failedSpy(manager.get(), &ModelManager::downloadFailed);
    QVERIFY(QMetaObject::invokeMethod(manager.get(), "requestDownload", Q_ARG(QString, spec.id)));
    QVERIFY(waitForGrowth(finishedSpy, failedSpy, 0, 0));
    QCOMPARE(finishedSpy.size(), 1);

    // property("models") 读取路径（model_catalog.cpp:215）
    const QVariantList rows = manager->property("models").toList();
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows.first().toMap().value(QStringLiteral("modelId")).toString(), spec.id);

    QVERIFY(QMetaObject::invokeMethod(manager.get(), "cancelDownload", Q_ARG(QString, spec.id)));
    QString error;
    QVERIFY(QMetaObject::invokeMethod(manager.get(), "remove", Q_RETURN_ARG(QString, error),
                                      Q_ARG(QString, spec.id)));
    QCOMPARE(error, QString());
    QVERIFY(QMetaObject::invokeMethod(manager.get(), "remove", Q_RETURN_ARG(QString, error),
                                      Q_ARG(QString, QStringLiteral("nope"))));
    QVERIFY(!error.isEmpty());
}

void SubtitleModelsTests::models_keys_match_catalog_roles()
{
    // ModelListModel 的全部角色名（来源：ui/backend/model_catalog.cpp:54-60 roleNames()，
    // 与 ui/backend/model_catalog.h:28-48 的 Role 枚举一一对应）。
    const QStringList roleNames = {
        QStringLiteral("modelId"),     QStringLiteral("displayName"),
        QStringLiteral("tag"),          QStringLiteral("summary"),
        QStringLiteral("sizeBytes"),    QStringLiteral("sizeText"),
        QStringLiteral("licenseName"),  QStringLiteral("licenseUrl"),
        QStringLiteral("licenseText"),  QStringLiteral("licenseAccepted"),
        QStringLiteral("status"),       QStringLiteral("progress"),
        QStringLiteral("bytesDone"),    QStringLiteral("bytesTotal"),
        QStringLiteral("errorText"),    QStringLiteral("selected"),
        QStringLiteral("inUse"),
    };

    TestEnv env;
    TestHttpServer server;
    QVERIFY(server.start());
    const QByteArray body = makeBytes(1024, 5);
    TestModelSpec spec;
    spec.files.append({QStringLiteral("m.bin"), body, {server.url(QStringLiteral("/m.bin")).toString()}});
    auto manager = env.makeManager(env.writeManifest({spec}));

    const QVariantList rows = manager->models();
    QCOMPARE(rows.size(), 1);
    const QVariantMap row = rows.first().toMap();
    for (const QString &role : roleNames)
        QVERIFY2(row.contains(role), qPrintable(role));
    // model_catalog.cpp:153 在 modelInfo() 里附加读取 licenseVersion
    QVERIFY(row.contains(QStringLiteral("licenseVersion")));

    // 值形态与 ModelListModel 内置行一致
    QCOMPARE(row.value(QStringLiteral("status")).toString(), QStringLiteral("notInstalled"));
    QVERIFY(!row.value(QStringLiteral("licenseAccepted")).toBool());
    QVERIFY(!row.value(QStringLiteral("selected")).toBool());
    QVERIFY(!row.value(QStringLiteral("inUse")).toBool());
    QCOMPARE(row.value(QStringLiteral("progress")).toDouble(), 0.0);
    QCOMPARE(row.value(QStringLiteral("bytesDone")).toLongLong(), qint64(0));
    QCOMPARE(row.value(QStringLiteral("bytesTotal")).toLongLong(), qint64(0));
    QVERIFY(row.value(QStringLiteral("sizeBytes")).toLongLong() == body.size());
    QVERIFY(!row.value(QStringLiteral("sizeText")).toString().isEmpty());
}

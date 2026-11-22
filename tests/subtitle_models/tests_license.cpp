// 许可确认：未接受不下载（无网络请求）；接受后可下载；版本变化需重新接受；记录跨实例持久。

#include "subtitle_models_tests.h"
#include "test_support.h"

void SubtitleModelsTests::license_gate_blocks_and_accepts()
{
    TestEnv env;
    TestHttpServer server;
    QVERIFY(server.start());
    const QByteArray body = makeBytes(2048, 53);
    server.addRoute({QStringLiteral("/m.bin"), body});
    TestModelSpec spec;
    spec.licenseVersion = QStringLiteral("1.0");
    spec.files.append({QStringLiteral("m.bin"), body,
                       {server.url(QStringLiteral("/m.bin")).toString()}});
    auto manager = env.makeManager(env.writeManifest({spec}));

    // 未接受：requestDownload 不下载，提示失败
    QVERIFY(!findRow(manager->models(), spec.id)
                 .value(QStringLiteral("licenseAccepted"))
                 .toBool());
    QSignalSpy finishedSpy(manager.get(), &ModelManager::downloadFinished);
    QSignalSpy failedSpy(manager.get(), &ModelManager::downloadFailed);
    manager->requestDownload(spec.id);
    QVERIFY(waitForGrowth(finishedSpy, failedSpy, 0, 0));
    QCOMPARE(finishedSpy.size(), 0);
    QCOMPARE(failedSpy.size(), 1);
    // 根本没有网络请求发出
    QCOMPARE(server.requests().size(), 0);
    const QVariantMap blocked = findRow(manager->models(), spec.id);
    QCOMPARE(blocked.value(QStringLiteral("status")).toString(), QStringLiteral("failed"));
    QVERIFY(blocked.value(QStringLiteral("errorText")).toString().contains(
        QStringLiteral("许可")));

    // 接受当前版本后可下载
    manager->acceptLicense(spec.id, spec.licenseVersion);
    QVERIFY(findRow(manager->models(), spec.id)
                .value(QStringLiteral("licenseAccepted"))
                .toBool());
    manager->requestDownload(spec.id);
    QVERIFY(waitForGrowth(finishedSpy, failedSpy, 0, 1));
    QCOMPARE(finishedSpy.size(), 1);
    QCOMPARE(rowStatus(manager.get(), spec.id), QStringLiteral("installed"));
}

void SubtitleModelsTests::license_version_change_requires_reaccept()
{
    TestEnv env;
    TestHttpServer server;
    QVERIFY(server.start());
    const QByteArray body = makeBytes(1024, 59);
    server.addRoute({QStringLiteral("/m.bin"), body});
    TestModelSpec specV1;
    specV1.licenseVersion = QStringLiteral("1.0");
    specV1.files.append({QStringLiteral("m.bin"), body,
                         {server.url(QStringLiteral("/m.bin")).toString()}});
    const QString manifestV1 = env.writeManifest({specV1});
    auto manager1 = env.makeManager(manifestV1);
    manager1->acceptLicense(specV1.id, QStringLiteral("1.0"));

    // 清单许可升版：同一 modelId 需要重新接受
    TestModelSpec specV2 = specV1;
    specV2.licenseVersion = QStringLiteral("2.0");
    const QString manifestV2 = env.writeManifest({specV2});
    auto manager2 = env.makeManager(manifestV2);
    QVERIFY(!findRow(manager2->models(), specV2.id)
                 .value(QStringLiteral("licenseAccepted"))
                 .toBool());

    QSignalSpy failedSpy(manager2.get(), &ModelManager::downloadFailed);
    QSignalSpy finishedSpy(manager2.get(), &ModelManager::downloadFinished);
    manager2->requestDownload(specV2.id);
    QVERIFY(waitForGrowth(finishedSpy, failedSpy, 0, 0));
    QCOMPARE(finishedSpy.size(), 0);
    QCOMPARE(server.requests().size(), 0);   // 仍被拦截，未发请求

    // 接受新版本后放行
    manager2->acceptLicense(specV2.id, QStringLiteral("2.0"));
    manager2->requestDownload(specV2.id);
    QVERIFY(waitForGrowth(finishedSpy, failedSpy, 0, 1));
    QCOMPARE(finishedSpy.size(), 1);
    QCOMPARE(rowStatus(manager2.get(), specV2.id), QStringLiteral("installed"));
}

void SubtitleModelsTests::license_persists_across_instances()
{
    TestEnv env;
    TestHttpServer server;
    QVERIFY(server.start());
    const QByteArray body = makeBytes(1024, 61);
    server.addRoute({QStringLiteral("/m.bin"), body});
    TestModelSpec spec;
    spec.licenseVersion = QStringLiteral("1.0");
    spec.files.append({QStringLiteral("m.bin"), body,
                       {server.url(QStringLiteral("/m.bin")).toString()}});
    const QString manifest = env.writeManifest({spec});

    auto first = env.makeManager(manifest);
    first->acceptLicense(spec.id, spec.licenseVersion);

    // 新实例（同模型根目录）：接受记录持久存在
    auto second = env.makeManager(manifest);
    QVERIFY(findRow(second->models(), spec.id)
                .value(QStringLiteral("licenseAccepted"))
                .toBool());
}

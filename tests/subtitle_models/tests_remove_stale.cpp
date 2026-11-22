// 删除 / stale / engineRequestFor：remove 后 notInstalled 且文件消失；下载中 remove 先取消；
// 安装后篡改或删文件 → stale 且 engineRequestFor 为 nullopt；已安装返回清单的 engineId/modelId/modelDir。

#include "subtitle_models_tests.h"
#include "test_support.h"

#include <QDir>
#include <QFileInfo>

namespace {

// 造一个「已安装」的模型并返回 manager（licenses 已接受）
struct InstalledFixture {
    TestEnv env;
    TestHttpServer server;
    TestModelSpec spec;
    std::unique_ptr<ModelManager> manager;
    QString modelDir;
    QByteArray body;

    bool setup()
    {
        if (!server.start())
            return false;
        body = makeBytes(4096, 67);
        server.addRoute({QStringLiteral("/m.bin"), body});
        spec.files.append({QStringLiteral("m.bin"), body,
                           {server.url(QStringLiteral("/m.bin")).toString()}});
        manager = env.makeManager(env.writeManifest({spec}));
        modelDir = env.rootPath() + QStringLiteral("/m1");
        QSignalSpy finishedSpy(manager.get(), &ModelManager::downloadFinished);
        QSignalSpy failedSpy(manager.get(), &ModelManager::downloadFailed);
        return acceptAndDownload(*manager, spec, finishedSpy, failedSpy);
    }
};

} // namespace

void SubtitleModelsTests::remove_installed()
{
    InstalledFixture fx;
    QVERIFY(fx.setup());
    QCOMPARE(rowStatus(fx.manager.get(), fx.spec.id), QStringLiteral("installed"));

    QCOMPARE(fx.manager->remove(fx.spec.id), QString());
    QCOMPARE(rowStatus(fx.manager.get(), fx.spec.id), QStringLiteral("notInstalled"));
    // 文件与 installed.json 全部消失
    QVERIFY(!QFileInfo::exists(fx.modelDir));
    QVERIFY(!fx.manager->engineRequestFor(fx.spec.id).has_value());

    // 未安装时删除幂等成功；未知模型给错误文案
    QCOMPARE(fx.manager->remove(fx.spec.id), QString());
    QVERIFY(!fx.manager->remove(QStringLiteral("nope")).isEmpty());
}

void SubtitleModelsTests::remove_while_downloading()
{
    TestEnv env;
    TestHttpServer server;
    QVERIFY(server.start());
    const QByteArray body = makeBytes(100000, 71);
    server.addRoute({QStringLiteral("/slow"), body, TestHttpServer::Behavior::Stall});
    TestModelSpec spec;
    spec.files.append({QStringLiteral("m.bin"), body,
                       {server.url(QStringLiteral("/slow")).toString()}});
    auto manager = env.makeManager(env.writeManifest({spec}));

    QSignalSpy finishedSpy(manager.get(), &ModelManager::downloadFinished);
    QSignalSpy failedSpy(manager.get(), &ModelManager::downloadFailed);
    manager->acceptLicense(spec.id, spec.licenseVersion);
    manager->requestDownload(spec.id);
    QTRY_COMPARE_WITH_TIMEOUT(server.requests().size(), 1, 10000);
    QTRY_COMPARE_WITH_TIMEOUT(rowStatus(manager.get(), spec.id),
                              QStringLiteral("downloading"), 10000);

    // 下载中删除：先取消再整目录删除（含 .part），返回成功、不报失败
    QCOMPARE(manager->remove(spec.id), QString());
    QTRY_COMPARE_WITH_TIMEOUT(rowStatus(manager.get(), spec.id),
                              QStringLiteral("notInstalled"), 10000);
    QVERIFY(!QFileInfo::exists(env.rootPath() + QStringLiteral("/m1")));
    QCOMPARE(finishedSpy.size(), 0);
    QCOMPARE(failedSpy.size(), 0);
    QVERIFY(!manager->engineRequestFor(spec.id).has_value());
}

void SubtitleModelsTests::stale_detection()
{
    InstalledFixture fx;
    QVERIFY(fx.setup());

    // 篡改：截短模型文件 → engineRequestFor 返回 nullopt 且状态变 stale
    QFile tampered(fx.modelDir + QStringLiteral("/m.bin"));
    QVERIFY(tampered.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(tampered.write(fx.body.left(100)), qint64(100));
    tampered.close();
    QVERIFY(!fx.manager->engineRequestFor(fx.spec.id).has_value());
    QCOMPARE(rowStatus(fx.manager.get(), fx.spec.id), QStringLiteral("stale"));

    // 删除文件 → 仍是 stale
    QFile::remove(fx.modelDir + QStringLiteral("/m.bin"));
    QVERIFY(!fx.manager->engineRequestFor(fx.spec.id).has_value());
    QCOMPARE(rowStatus(fx.manager.get(), fx.spec.id), QStringLiteral("stale"));

    // 新实例扫描同样得到 stale（installed.json 还在但文件缺失）
    auto second = fx.env.makeManager(fx.env.writeManifest({fx.spec}));
    QCOMPARE(rowStatus(second.get(), fx.spec.id), QStringLiteral("stale"));

    // 重新下载修复
    QSignalSpy finishedSpy(second.get(), &ModelManager::downloadFinished);
    QSignalSpy failedSpy(second.get(), &ModelManager::downloadFailed);
    QVERIFY(acceptAndDownload(*second, fx.spec, finishedSpy, failedSpy));
    QCOMPARE(rowStatus(second.get(), fx.spec.id), QStringLiteral("installed"));
    QVERIFY(second->engineRequestFor(fx.spec.id).has_value());
}

void SubtitleModelsTests::engine_request_installed()
{
    TestEnv env;
    TestHttpServer server;
    QVERIFY(server.start());
    const QByteArray body = makeBytes(2048, 73);
    server.addRoute({QStringLiteral("/engine.bin"), body});
    TestModelSpec spec;
    spec.engine = QStringLiteral("test-engine");
    spec.files.append({QStringLiteral("engine.bin"), body,
                       {server.url(QStringLiteral("/engine.bin")).toString()}});
    auto manager = env.makeManager(env.writeManifest({spec}));

    // 先装好
    QSignalSpy finishedSpy(manager.get(), &ModelManager::downloadFinished);
    QSignalSpy failedSpy(manager.get(), &ModelManager::downloadFailed);
    QVERIFY(acceptAndDownload(*manager, spec, finishedSpy, failedSpy));

    const auto request = manager->engineRequestFor(spec.id);
    QVERIFY(request.has_value());
    QCOMPARE(QString::fromStdString(request->engineId), QStringLiteral("test-engine"));
    QCOMPARE(QString::fromStdString(request->modelId), spec.id);
    const QString modelDir = QString::fromStdWString(request->modelDir.wstring());
    QVERIFY(QFileInfo(modelDir).isAbsolute());
    QVERIFY(QFileInfo(modelDir).isDir());
    QVERIFY(QFile::exists(modelDir + QStringLiteral("/engine.bin")));
    // 其余字段保持默认（语言 zh、单线程、Auto 后端）
    QCOMPARE(QString::fromStdString(request->language), QStringLiteral("zh"));
    QCOMPARE(request->threads, 1);

    // 未知模型 → nullopt
    QVERIFY(!manager->engineRequestFor(QStringLiteral("nope")).has_value());
}

void SubtitleModelsTests::engine_request_missing_or_stale()
{
    InstalledFixture fx;
    QVERIFY(fx.setup());
    QVERIFY(fx.manager->engineRequestFor(fx.spec.id).has_value());

    // 安装记录损坏 → nullopt + stale
    QFile marker(fx.modelDir + QStringLiteral("/installed.json"));
    QVERIFY(marker.open(QIODevice::WriteOnly | QIODevice::Truncate));
    marker.write("not json");
    marker.close();
    QVERIFY(!fx.manager->engineRequestFor(fx.spec.id).has_value());
    QCOMPARE(rowStatus(fx.manager.get(), fx.spec.id), QStringLiteral("stale"));

    // 删除安装记录（文件完好）→ nullopt；若此前显示 installed 则纠正为 stale
    QVERIFY(fx.manager->remove(fx.spec.id) == QString());
    QSignalSpy finishedSpy(fx.manager.get(), &ModelManager::downloadFinished);
    QSignalSpy failedSpy(fx.manager.get(), &ModelManager::downloadFailed);
    QVERIFY(acceptAndDownload(*fx.manager, fx.spec, finishedSpy, failedSpy));
    QVERIFY(QFile::exists(fx.modelDir + QStringLiteral("/m.bin")));
    QVERIFY(QFile::remove(fx.modelDir + QStringLiteral("/installed.json")));
    QVERIFY(!fx.manager->engineRequestFor(fx.spec.id).has_value());
    QCOMPARE(rowStatus(fx.manager.get(), fx.spec.id), QStringLiteral("stale"));

    // 从未安装 → nullopt
    auto fresh = fx.env.makeManager(fx.env.writeManifest({fx.spec}));
    fresh->remove(fx.spec.id);
    QVERIFY(!fresh->engineRequestFor(fx.spec.id).has_value());
    QCOMPARE(rowStatus(fresh.get(), fx.spec.id), QStringLiteral("notInstalled"));
}

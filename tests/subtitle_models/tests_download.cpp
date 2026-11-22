// 下载器用例：Range 续传、断线换镜像、哈希错误（换镜像/全败）、镜像切换（404/拒连）、
// 服务器忽略 Range、大小校验、协议限制（仅 https / 测试钩子回环）、多文件、取消保留 .part。

#include "subtitle_models_tests.h"
#include "test_support.h"

#include <QDir>
#include <QTcpServer>

void SubtitleModelsTests::range_resume_from_part()
{
    TestEnv env;
    TestHttpServer server;
    QVERIFY(server.start());
    const QByteArray body = makeBytes(8192, 7);
    server.addRoute({QStringLiteral("/m.bin"), body});
    TestModelSpec spec;
    spec.files.append({QStringLiteral("m.bin"), body,
                       {server.url(QStringLiteral("/m.bin")).toString()}});
    auto manager = env.makeManager(env.writeManifest({spec}));

    // 模拟上次中断：预置 .part 前 3000 字节
    const QString modelDir = env.rootPath() + QStringLiteral("/m1");
    QVERIFY(QDir().mkpath(modelDir));
    QFile part(modelDir + QStringLiteral("/m.bin.part"));
    QVERIFY(part.open(QIODevice::WriteOnly));
    QCOMPARE(part.write(body.left(3000)), qint64(3000));
    part.close();

    QSignalSpy finishedSpy(manager.get(), &ModelManager::downloadFinished);
    QSignalSpy failedSpy(manager.get(), &ModelManager::downloadFailed);
    QVERIFY(acceptAndDownload(*manager, spec, finishedSpy, failedSpy));
    QCOMPARE(failedSpy.size(), 0);

    // 服务器收到 Range: bytes=3000- 并回 206
    QCOMPARE(server.requests().size(), 1);
    QCOMPARE(server.requests().first().rangeFrom, qint64(3000));
    QCOMPARE(server.requests().first().statusServed, 206);

    // 最终文件 == 完整内容（续传拼接后哈希通过才会改名）
    QCOMPARE(readFileAll(modelDir + QStringLiteral("/m.bin")), body);
    QVERIFY(QFile::exists(modelDir + QStringLiteral("/installed.json")));
    QVERIFY(!QFile::exists(modelDir + QStringLiteral("/m.bin.part")));
    const QVariantMap row = findRow(manager->models(), spec.id);
    QCOMPARE(row.value(QStringLiteral("status")).toString(), QStringLiteral("installed"));
    QCOMPARE(row.value(QStringLiteral("progress")).toDouble(), 1.0);
    QCOMPARE(row.value(QStringLiteral("bytesTotal")).toLongLong(), qint64(body.size()));
}

void SubtitleModelsTests::disconnect_switches_mirror()
{
    TestEnv env;
    TestHttpServer server;
    QVERIFY(server.start());
    const QByteArray body = makeBytes(6000, 11);
    server.addRoute({QStringLiteral("/break"), body, TestHttpServer::Behavior::BreakEarly});
    server.addRoute({QStringLiteral("/ok"), body});
    TestModelSpec spec;
    spec.files.append({QStringLiteral("m.bin"), body,
                       {server.url(QStringLiteral("/break")).toString(),
                        server.url(QStringLiteral("/ok")).toString()}});
    auto manager = env.makeManager(env.writeManifest({spec}));

    QSignalSpy finishedSpy(manager.get(), &ModelManager::downloadFinished);
    QSignalSpy failedSpy(manager.get(), &ModelManager::downloadFailed);
    QVERIFY(acceptAndDownload(*manager, spec, finishedSpy, failedSpy));

    // 镜像 1 发一半断开；镜像 2 收到 Range: bytes=3000-（6000/2）续传，206
    QCOMPARE(server.requests().size(), 2);
    QCOMPARE(server.requests().at(0).path, QStringLiteral("/break"));
    QCOMPARE(server.requests().at(0).statusServed, 0);   // 0 = 连接中断
    QCOMPARE(server.requests().at(1).path, QStringLiteral("/ok"));
    QCOMPARE(server.requests().at(1).rangeFrom, qint64(3000));
    QCOMPARE(server.requests().at(1).statusServed, 206);

    const QString modelDir = env.rootPath() + QStringLiteral("/m1");
    QCOMPARE(readFileAll(modelDir + QStringLiteral("/m.bin")), body);
    QCOMPARE(rowStatus(manager.get(), spec.id), QStringLiteral("installed"));
}

void SubtitleModelsTests::hash_mismatch_switches_mirror()
{
    TestEnv env;
    TestHttpServer server;
    QVERIFY(server.start());
    const QByteArray body = makeBytes(4096, 13);
    server.addRoute({QStringLiteral("/bad"), body, TestHttpServer::Behavior::WrongContent});
    server.addRoute({QStringLiteral("/good"), body});
    TestModelSpec spec;
    spec.files.append({QStringLiteral("m.bin"), body,
                       {server.url(QStringLiteral("/bad")).toString(),
                        server.url(QStringLiteral("/good")).toString()}});
    auto manager = env.makeManager(env.writeManifest({spec}));

    QSignalSpy finishedSpy(manager.get(), &ModelManager::downloadFinished);
    QSignalSpy failedSpy(manager.get(), &ModelManager::downloadFailed);
    QVERIFY(acceptAndDownload(*manager, spec, finishedSpy, failedSpy));

    // 镜像 1 内容错误：删 .part、换镜像 2 从头重下（无 Range）
    QCOMPARE(server.requests().size(), 2);
    QCOMPARE(server.requests().at(0).path, QStringLiteral("/bad"));
    QCOMPARE(server.requests().at(0).rangeFrom, qint64(-1));
    QCOMPARE(server.requests().at(1).path, QStringLiteral("/good"));
    QCOMPARE(server.requests().at(1).rangeFrom, qint64(-1));
    QCOMPARE(readFileAll(env.rootPath() + QStringLiteral("/m1/m.bin")), body);
    QCOMPARE(rowStatus(manager.get(), spec.id), QStringLiteral("installed"));
    QVERIFY(manager->engineRequestFor(spec.id).has_value());
}

void SubtitleModelsTests::hash_mismatch_all_fail()
{
    TestEnv env;
    TestHttpServer server;
    QVERIFY(server.start());
    const QByteArray body = makeBytes(2048, 17);
    server.addRoute({QStringLiteral("/bad1"), body, TestHttpServer::Behavior::WrongContent});
    server.addRoute({QStringLiteral("/bad2"), body, TestHttpServer::Behavior::WrongContent});
    TestModelSpec spec;
    spec.files.append({QStringLiteral("m.bin"), body,
                       {server.url(QStringLiteral("/bad1")).toString(),
                        server.url(QStringLiteral("/bad2")).toString()}});
    auto manager = env.makeManager(env.writeManifest({spec}));

    QSignalSpy finishedSpy(manager.get(), &ModelManager::downloadFinished);
    QSignalSpy failedSpy(manager.get(), &ModelManager::downloadFailed);
    manager->acceptLicense(spec.id, spec.licenseVersion);
    manager->requestDownload(spec.id);
    QVERIFY(waitForGrowth(finishedSpy, failedSpy, 0, 0));
    QCOMPARE(finishedSpy.size(), 0);
    QCOMPARE(failedSpy.size(), 1);
    QVERIFY(!failedSpy.first().at(1).toString().isEmpty());

    // 全部镜像内容错误：不安装、.part 已删、无 installed.json
    const QString modelDir = env.rootPath() + QStringLiteral("/m1");
    QVERIFY(!QFile::exists(modelDir + QStringLiteral("/m.bin")));
    QVERIFY(!QFile::exists(modelDir + QStringLiteral("/m.bin.part")));
    QVERIFY(!QFile::exists(modelDir + QStringLiteral("/installed.json")));
    const QVariantMap row = findRow(manager->models(), spec.id);
    QCOMPARE(row.value(QStringLiteral("status")).toString(), QStringLiteral("failed"));
    QVERIFY(row.value(QStringLiteral("errorText")).toString().contains(
        QStringLiteral("校验")));
    QVERIFY(!manager->engineRequestFor(spec.id).has_value());
}

void SubtitleModelsTests::mirror_404_then_success()
{
    TestEnv env;
    TestHttpServer server;
    QVERIFY(server.start());
    const QByteArray body = makeBytes(3000, 19);
    server.addRoute({QStringLiteral("/missing"), body, TestHttpServer::Behavior::NotFound});
    server.addRoute({QStringLiteral("/ok"), body});
    TestModelSpec spec;
    spec.files.append({QStringLiteral("m.bin"), body,
                       {server.url(QStringLiteral("/missing")).toString(),
                        server.url(QStringLiteral("/ok")).toString()}});
    auto manager = env.makeManager(env.writeManifest({spec}));

    QSignalSpy finishedSpy(manager.get(), &ModelManager::downloadFinished);
    QSignalSpy failedSpy(manager.get(), &ModelManager::downloadFailed);
    QVERIFY(acceptAndDownload(*manager, spec, finishedSpy, failedSpy));

    QCOMPARE(server.requests().size(), 2);
    QCOMPARE(server.requests().at(0).statusServed, 404);
    QCOMPARE(server.requests().at(1).statusServed, 200);
    QCOMPARE(readFileAll(env.rootPath() + QStringLiteral("/m1/m.bin")), body);
    QCOMPARE(rowStatus(manager.get(), spec.id), QStringLiteral("installed"));
}

void SubtitleModelsTests::mirror_refused_then_success()
{
    TestEnv env;
    TestHttpServer server;
    QVERIFY(server.start());
    // 一个「已关闭的本地端口」：连接会被拒绝
    QTcpServer dead;
    QVERIFY(dead.listen(QHostAddress::LocalHost, 0));
    const quint16 deadPort = dead.serverPort();
    dead.close();

    const QByteArray body = makeBytes(2500, 23);
    server.addRoute({QStringLiteral("/ok"), body});
    TestModelSpec spec;
    spec.files.append({QStringLiteral("m.bin"), body,
                       {QStringLiteral("http://127.0.0.1:%1/x").arg(deadPort),
                        server.url(QStringLiteral("/ok")).toString()}});
    auto manager = env.makeManager(env.writeManifest({spec}));

    QSignalSpy finishedSpy(manager.get(), &ModelManager::downloadFinished);
    QSignalSpy failedSpy(manager.get(), &ModelManager::downloadFailed);
    QVERIFY(acceptAndDownload(*manager, spec, finishedSpy, failedSpy));

    // 第一个 URL 拒连（没到达服务器），第二个成功
    QCOMPARE(server.requests().size(), 1);
    QCOMPARE(server.requests().first().path, QStringLiteral("/ok"));
    QCOMPARE(readFileAll(env.rootPath() + QStringLiteral("/m1/m.bin")), body);
    QCOMPARE(rowStatus(manager.get(), spec.id), QStringLiteral("installed"));
}

void SubtitleModelsTests::server_ignores_range_restarts()
{
    TestEnv env;
    TestHttpServer server;
    QVERIFY(server.start());
    const QByteArray body = makeBytes(4096, 29);
    server.addRoute({QStringLiteral("/full"), body, TestHttpServer::Behavior::IgnoreRange});
    TestModelSpec spec;
    spec.files.append({QStringLiteral("m.bin"), body,
                       {server.url(QStringLiteral("/full")).toString()}});
    auto manager = env.makeManager(env.writeManifest({spec}));

    // 预置 .part 前 1000 字节：客户端会发 Range，但服务器回 200 全量
    const QString modelDir = env.rootPath() + QStringLiteral("/m1");
    QVERIFY(QDir().mkpath(modelDir));
    QFile part(modelDir + QStringLiteral("/m.bin.part"));
    QVERIFY(part.open(QIODevice::WriteOnly));
    QCOMPARE(part.write(body.left(1000)), qint64(1000));
    part.close();

    QSignalSpy finishedSpy(manager.get(), &ModelManager::downloadFinished);
    QSignalSpy failedSpy(manager.get(), &ModelManager::downloadFailed);
    QVERIFY(acceptAndDownload(*manager, spec, finishedSpy, failedSpy));

    QCOMPARE(server.requests().size(), 1);
    QCOMPARE(server.requests().first().rangeFrom, qint64(1000));   // 客户端确实发了 Range
    QCOMPARE(server.requests().first().statusServed, 200);          // 服务器无视

    // 最终内容完整且哈希通过 —— 只有「从头重下并重置哈希」才能通过
    QCOMPARE(readFileAll(modelDir + QStringLiteral("/m.bin")), body);
    QCOMPARE(rowStatus(manager.get(), spec.id), QStringLiteral("installed"));
}

void SubtitleModelsTests::size_mismatch_rejected()
{
    TestEnv env;
    TestHttpServer server;
    QVERIFY(server.start());
    const QByteArray body = makeBytes(4096, 31);
    server.addRoute({QStringLiteral("/short"), body, TestHttpServer::Behavior::ShortBody});
    TestModelSpec spec;
    spec.files.append({QStringLiteral("m.bin"), body,
                       {server.url(QStringLiteral("/short")).toString()}});
    auto manager = env.makeManager(env.writeManifest({spec}));

    QSignalSpy finishedSpy(manager.get(), &ModelManager::downloadFinished);
    QSignalSpy failedSpy(manager.get(), &ModelManager::downloadFailed);
    manager->acceptLicense(spec.id, spec.licenseVersion);
    manager->requestDownload(spec.id);
    QVERIFY(waitForGrowth(finishedSpy, failedSpy, 0, 0));
    QCOMPARE(finishedSpy.size(), 0);
    QCOMPARE(failedSpy.size(), 1);

    // 比清单少 10 字节：不安装、.part 删除
    const QString modelDir = env.rootPath() + QStringLiteral("/m1");
    QVERIFY(!QFile::exists(modelDir + QStringLiteral("/m.bin")));
    QVERIFY(!QFile::exists(modelDir + QStringLiteral("/m.bin.part")));
    QVERIFY(!QFile::exists(modelDir + QStringLiteral("/installed.json")));
    QCOMPARE(rowStatus(manager.get(), spec.id), QStringLiteral("failed"));
}

void SubtitleModelsTests::http_scheme_rejected_without_loopback()
{
    TestEnv env;
    TestHttpServer server;   // 不 start 也行：请求根本不该发出
    QVERIFY(server.start());
    TestModelSpec spec;
    spec.files.append({QStringLiteral("m.bin"), makeBytes(1024, 37),
                       {QStringLiteral("http://example.invalid/m.bin")}});
    auto manager = env.makeManager(env.writeManifest({spec}));

    QSignalSpy finishedSpy(manager.get(), &ModelManager::downloadFinished);
    QSignalSpy failedSpy(manager.get(), &ModelManager::downloadFailed);
    manager->acceptLicense(spec.id, spec.licenseVersion);
    manager->requestDownload(spec.id);
    QVERIFY(waitForGrowth(finishedSpy, failedSpy, 0, 0));
    QCOMPARE(finishedSpy.size(), 0);
    QCOMPARE(failedSpy.size(), 1);

    // 非回环 http 被拒绝（即使测试钩子打开也只放行 127.0.0.1；生产只允许 https）
    QVERIFY(findRow(manager->models(), spec.id)
                .value(QStringLiteral("errorText"))
                .toString()
                .contains(QStringLiteral("https")));
    QCOMPARE(server.requests().size(), 0);
    QVERIFY(!QFile::exists(env.rootPath() + QStringLiteral("/m1/m.bin")));
}

void SubtitleModelsTests::multi_file_download()
{
    TestEnv env;
    TestHttpServer server;
    QVERIFY(server.start());
    const QByteArray bodyA = makeBytes(5000, 41);
    const QByteArray bodyB = makeBytes(3000, 43);
    server.addRoute({QStringLiteral("/a.bin"), bodyA});
    server.addRoute({QStringLiteral("/b.bin"), bodyB});
    TestModelSpec spec;
    spec.files.append({QStringLiteral("a.bin"), bodyA,
                       {server.url(QStringLiteral("/a.bin")).toString()}});
    spec.files.append({QStringLiteral("b.bin"), bodyB,
                       {server.url(QStringLiteral("/b.bin")).toString()}});
    auto manager = env.makeManager(env.writeManifest({spec}));

    QSignalSpy finishedSpy(manager.get(), &ModelManager::downloadFinished);
    QSignalSpy failedSpy(manager.get(), &ModelManager::downloadFailed);
    QVERIFY(acceptAndDownload(*manager, spec, finishedSpy, failedSpy));

    const QString modelDir = env.rootPath() + QStringLiteral("/m1");
    QCOMPARE(readFileAll(modelDir + QStringLiteral("/a.bin")), bodyA);
    QCOMPARE(readFileAll(modelDir + QStringLiteral("/b.bin")), bodyB);
    QVERIFY(QFile::exists(modelDir + QStringLiteral("/installed.json")));
    const QVariantMap row = findRow(manager->models(), spec.id);
    QCOMPARE(row.value(QStringLiteral("status")).toString(), QStringLiteral("installed"));
    QCOMPARE(row.value(QStringLiteral("bytesTotal")).toLongLong(),
             qint64(bodyA.size() + bodyB.size()));
    const auto request = manager->engineRequestFor(spec.id);
    QVERIFY(request.has_value());
    QVERIFY(QFile::exists(QString::fromStdWString(request->modelDir.wstring())
                          + QStringLiteral("/a.bin")));
    QVERIFY(QFile::exists(QString::fromStdWString(request->modelDir.wstring())
                          + QStringLiteral("/b.bin")));
}

void SubtitleModelsTests::cancel_keeps_part()
{
    TestEnv env;
    TestHttpServer server;
    QVERIFY(server.start());
    const QByteArray body = makeBytes(100000, 47);
    server.addRoute({QStringLiteral("/slow"), body, TestHttpServer::Behavior::Stall});
    TestModelSpec spec;
    spec.files.append({QStringLiteral("m.bin"), body,
                       {server.url(QStringLiteral("/slow")).toString()}});
    auto manager = env.makeManager(env.writeManifest({spec}));

    QSignalSpy finishedSpy(manager.get(), &ModelManager::downloadFinished);
    QSignalSpy failedSpy(manager.get(), &ModelManager::downloadFailed);
    manager->acceptLicense(spec.id, spec.licenseVersion);
    manager->requestDownload(spec.id);
    // 等请求到达挂起的服务器、状态进入 downloading
    QTRY_COMPARE_WITH_TIMEOUT(server.requests().size(), 1, 10000);
    QTRY_COMPARE_WITH_TIMEOUT(rowStatus(manager.get(), spec.id),
                              QStringLiteral("downloading"), 10000);

    manager->cancelDownload(spec.id);
    QTRY_COMPARE_WITH_TIMEOUT(rowStatus(manager.get(), spec.id),
                              QStringLiteral("notInstalled"), 10000);
    // 中止后保留 .part 供续传；不发完成/失败
    QVERIFY(QFile::exists(env.rootPath() + QStringLiteral("/m1/m.bin.part")));
    QCOMPARE(finishedSpy.size(), 0);
    QCOMPARE(failedSpy.size(), 0);
    QVERIFY(!manager->engineRequestFor(spec.id).has_value());
}

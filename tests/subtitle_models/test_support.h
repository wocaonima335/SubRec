#pragma once
// 工作包 E 测试支撑：测试钩子环境变量隔离（SUBREC_MODELS_TEST_HOOKS/ROOT/MANIFEST，
// 见 subtitle/models/model_manager.cpp 文件头）、测试清单生成、通用等待/查找工具。
// 所有网络交互仅限 127.0.0.1 上的 TestHttpServer；所有落盘都在 QTemporaryDir。

#include "subtitle/models/model_manager.h"
#include "test_http_server.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

inline QByteArray sha256HexOf(const QByteArray &data)
{
    return QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex();
}

inline QByteArray makeBytes(int size, uchar seed)
{
    QByteArray bytes(size, Qt::Uninitialized);
    for (int i = 0; i < size; ++i)
        bytes[i] = char((seed + uchar(i) * 7) & 0xff);
    return bytes;
}

struct TestFileSpec {
    QString name;
    QByteArray content;
    QStringList urls;
};

struct TestModelSpec {
    QString id = QStringLiteral("m1");
    QString engine = QStringLiteral("test-engine");
    QString licenseName = QStringLiteral("测试许可协议");
    QString licenseVersion = QStringLiteral("1.0");
    QString licenseUrl = QStringLiteral("https://example.invalid/license");
    QList<TestFileSpec> files;
};

// 按生产 schema 生成测试清单（sha256 由内容计算）
inline bool writeTestManifest(const QString &path, const QList<TestModelSpec> &models)
{
    QJsonArray modelArray;
    for (const auto &m : models) {
        QJsonArray files;
        for (const auto &f : m.files) {
            files.append(QJsonObject{
                {QStringLiteral("name"), f.name},
                {QStringLiteral("size"), f.content.size()},
                {QStringLiteral("sha256"), QString::fromLatin1(sha256HexOf(f.content))},
                {QStringLiteral("urls"), QJsonArray::fromStringList(f.urls)}});
        }
        modelArray.append(QJsonObject{
            {QStringLiteral("modelId"), m.id},
            {QStringLiteral("engine"), m.engine},
            {QStringLiteral("displayName"), m.id},
            {QStringLiteral("tag"), QStringLiteral("recommended")},
            {QStringLiteral("summary"), QStringLiteral("测试模型")},
            {QStringLiteral("license"), QJsonObject{
                {QStringLiteral("name"), m.licenseName},
                {QStringLiteral("version"), m.licenseVersion},
                {QStringLiteral("url"), m.licenseUrl},
                {QStringLiteral("text"), QString()}}},
            {QStringLiteral("files"), files}});
    }
    const QJsonObject root{{QStringLiteral("manifestVersion"), 1},
                           {QStringLiteral("models"), modelArray}};
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(QJsonDocument(root).toJson());
    return true;
}

// 环境隔离：构造时打开测试钩子，析构时全部清掉（不影响其他用例/默认实例）
class EnvHooks {
public:
    EnvHooks() { qputenv("SUBREC_MODELS_TEST_HOOKS", QByteArrayLiteral("1")); }
    ~EnvHooks()
    {
        qunsetenv("SUBREC_MODELS_TEST_HOOKS");
        qunsetenv("SUBREC_MODELS_ROOT");
        qunsetenv("SUBREC_MODELS_MANIFEST");
    }
};

struct TestEnv {
    EnvHooks hooks;
    QTemporaryDir root;     // 模型根目录（SUBREC_MODELS_ROOT）
    QTemporaryDir work;     // 清单等杂项文件
    int manifestCounter = 0;

    QString rootPath() const { return root.path(); }

    std::unique_ptr<ModelManager> makeManager(const QString &manifestPath = QString())
    {
        qputenv("SUBREC_MODELS_ROOT", rootPath().toUtf8());
        if (manifestPath.isEmpty())
            qunsetenv("SUBREC_MODELS_MANIFEST");
        else
            qputenv("SUBREC_MODELS_MANIFEST", manifestPath.toUtf8());
        return std::make_unique<ModelManager>();
    }

    QString writeManifest(const QList<TestModelSpec> &models)
    {
        const QString path = work.filePath(
            QStringLiteral("manifest-%1.json").arg(++manifestCounter));
        if (!writeTestManifest(path, models))
            return QString();
        return path;
    }
};

inline QVariantMap findRow(const QVariantList &rows, const QString &modelId)
{
    for (const auto &value : rows) {
        const QVariantMap map = value.toMap();
        if (map.value(QStringLiteral("modelId")).toString() == modelId)
            return map;
    }
    return {};
}

inline QString rowStatus(ModelManager *manager, const QString &modelId)
{
    return findRow(manager->models(), modelId).value(QStringLiteral("status")).toString();
}

inline QByteArray readFileAll(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

// 等任一 spy 相对基数增长。基数必须在触发动作之前取：downloadFailed 可能在
// requestDownload 内同步发出（许可未接受/未知模型），也可能要过事件循环（网络）。
inline bool waitForGrowth(QSignalSpy &finishedSpy, QSignalSpy &failedSpy,
                          int finishedBefore, int failedBefore, int timeoutMs = 30000)
{
    return QTest::qWaitFor(
        [&] { return finishedSpy.size() > finishedBefore || failedSpy.size() > failedBefore; },
        timeoutMs);
}

// 常规下载路径：接受许可 → 请求 → 等完成。返回 downloadFinished 是否到达。
inline bool acceptAndDownload(ModelManager &manager, const TestModelSpec &spec,
                              QSignalSpy &finishedSpy, QSignalSpy &failedSpy)
{
    const int finishedBefore = finishedSpy.size();
    const int failedBefore = failedSpy.size();
    manager.acceptLicense(spec.id, spec.licenseVersion);
    manager.requestDownload(spec.id);
    const bool fired = waitForGrowth(finishedSpy, failedSpy, finishedBefore, failedBefore);
    return fired && finishedSpy.size() > finishedBefore;
}

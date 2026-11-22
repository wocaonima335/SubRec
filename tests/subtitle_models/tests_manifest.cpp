// 内置 models.json 自检：能解析、每文件 sha256 为 64 位十六进制、URL 全 https、
// displayName/tag/summary 与 ui/backend/model_catalog.cpp 的内置行一致。

#include "subtitle_models_tests.h"
#include "test_support.h"

#include <QJsonArray>
#include <QJsonObject>

void SubtitleModelsTests::manifest_builtin_selfcheck()
{
    QFile file(QStringLiteral(":/subtitle/models/models.json"));
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    QVERIFY(doc.isObject());
    const QJsonObject root = doc.object();
    QCOMPARE(root.value(QStringLiteral("manifestVersion")).toInt(), 1);
    const QJsonArray models = root.value(QStringLiteral("models")).toArray();
    QCOMPARE(models.size(), 2);

    // 与 ui/backend/model_catalog.cpp:16-25 的内置两行一致（modelId 现值）
    struct Expectation {
        QString id;
        QString displayName;
        QString tag;
        QString summary;
        QString engine;
        QString milestone;
    };
    const Expectation expectations[] = {
        {QStringLiteral("sensevoice-small"), QStringLiteral("SenseVoice Small"),
         QStringLiteral("recommended"), QStringLiteral("中文、英文、日语、韩语、粤语 · 自带标点"),
         QStringLiteral("sherpa-onnx"), QStringLiteral("M1")},
        {QStringLiteral("whisper-small"), QStringLiteral("Whisper Small"),
         QStringLiteral("alternative"), QStringLiteral("多语言 · 中文可能出现繁体，标点不稳定"),
         QStringLiteral("whisper.cpp"), QStringLiteral("M2")},
    };
    for (const Expectation &expectation : expectations) {
        bool found = false;
        for (const QJsonValue &value : models) {
            const QJsonObject model = value.toObject();
            if (model.value(QStringLiteral("modelId")).toString() != expectation.id)
                continue;
            found = true;
            QCOMPARE(model.value(QStringLiteral("displayName")).toString(), expectation.displayName);
            QCOMPARE(model.value(QStringLiteral("tag")).toString(), expectation.tag);
            QCOMPARE(model.value(QStringLiteral("summary")).toString(), expectation.summary);
            QCOMPARE(model.value(QStringLiteral("engine")).toString(), expectation.engine);
            QCOMPARE(model.value(QStringLiteral("milestone")).toString(), expectation.milestone);
            const QJsonObject license = model.value(QStringLiteral("license")).toObject();
            QVERIFY(!license.value(QStringLiteral("name")).toString().isEmpty());
            QVERIFY(license.value(QStringLiteral("url")).toString().startsWith(
                QStringLiteral("https://")));
            const QJsonArray files = model.value(QStringLiteral("files")).toArray();
            QVERIFY(files.size() >= 1);
            for (const QJsonValue &fileValue : files) {
                const QJsonObject file = fileValue.toObject();
                const QString name = file.value(QStringLiteral("name")).toString();
                QVERIFY(!name.isEmpty());
                QVERIFY(file.value(QStringLiteral("size")).toInteger() > 0);
                // 内置清单不使用占位：sha256 必须是 64 位十六进制
                const QString sha = file.value(QStringLiteral("sha256")).toString();
                QCOMPARE(sha.size(), 64);
                for (const QChar &c : sha)
                    QVERIFY((c >= QLatin1Char('0') && c <= QLatin1Char('9'))
                            || (c >= QLatin1Char('a') && c <= QLatin1Char('f'))
                            || (c >= QLatin1Char('A') && c <= QLatin1Char('F')));
                // URL 全部 https（绝不编造 http/ftp 源）
                const QJsonArray urls = file.value(QStringLiteral("urls")).toArray();
                QVERIFY(urls.size() >= 1);
                for (const QJsonValue &urlValue : urls)
                    QVERIFY(urlValue.toString().startsWith(QStringLiteral("https://")));
            }
        }
        QVERIFY2(found, qPrintable(expectation.id));
    }

    // 默认实例（无测试钩子、不落盘）按内置清单给出两行
    EnvHooks cleanup;   // 确保其他用例留下的环境变量不影响默认实例
    Q_UNUSED(cleanup);
    ModelManager manager;
    const QVariantList rows = manager.models();
    QCOMPARE(rows.size(), 2);
    QCOMPARE(rows.first().toMap().value(QStringLiteral("modelId")).toString(),
             QStringLiteral("sensevoice-small"));
    QCOMPARE(rows.last().toMap().value(QStringLiteral("modelId")).toString(),
             QStringLiteral("whisper-small"));
    for (const QVariant &value : rows) {
        const QVariantMap row = value.toMap();
        QVERIFY(row.value(QStringLiteral("sizeBytes")).toLongLong() > 0);
        QVERIFY(!row.value(QStringLiteral("sizeText")).toString().isEmpty());
        // 默认根目录干净：未安装
        QCOMPARE(row.value(QStringLiteral("status")).toString(), QStringLiteral("notInstalled"));
    }
}

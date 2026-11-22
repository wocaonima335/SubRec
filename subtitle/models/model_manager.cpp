// 工作包 E：ModelManager 实现（清单 models.json、下载/校验/断点续传、许可确认、删除、engineRequestFor）。
// 设计依据：design-verified.md §5.2/§5.3/§11.3 E、design-live-v2.md §10.3 E、docs/subtitle/work-packages.md §1/§3 E。
//
// 结构：model_manager.h 是 M0 冻结头，实现状态全部放在本文件的 ModelManager::ModelManagerPrivate
//（d 指针在头文件追加的 private 段声明）。内部类型不新增 subtitle/models/*.h：
// m0_check 用 GLOB 把本目录新头当纯 C++17 编译，而这里需要 Qt Network/Json。
//
// 行为要点（§5.3）：
// - GUI 线程异步下载（QNetworkAccessManager）；写 <file>.part、增量 Sha256；
//   Range: bytes=<已有>- 续传（续传前对 .part 重算哈希前缀）；服务器回 200（无视 Range）时从头重下。
// - 断线/超时/HTTP 错误/哈希不符 → 切换下一个镜像 URL；全部失败 → failed + errorText。
//   哈希不符时删除 .part 后再换镜像；断线保留 .part 供下一镜像续传。
// - 全部文件通过后原子改名并写 installed.json（清单版本 + 每文件哈希）；
//   加载前检查文件存在与大小，缺失/不符 → stale（engineRequestFor 返回 nullopt）。
// - 只允许 https；许可未接受（或清单版本号变化）时 requestDownload 不下载并 downloadFailed。
// - QNetworkProxyFactory::setUseSystemConfiguration(true)（系统代理）。
//
// 测试钩子（生产默认关闭；见 tests/subtitle_models）：环境变量 SUBREC_MODELS_TEST_HOOKS=1 时
//   - 额外允许 http://127.0.0.1:<port>（仅回环）；
//   - SUBREC_MODELS_ROOT 覆盖模型根目录；SUBREC_MODELS_MANIFEST 覆盖清单文件路径。

#include "subtitle/models/model_manager.h"

#include <QByteArrayView>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkProxyFactory>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>

#include <algorithm>
#include <memory>

// Q_INIT_RESOURCE 不能在命名空间内展开，包一层文件级全局函数（资源见 subtitle_models.qrc）。
static void ensureBuiltinModelsResource()
{
    Q_INIT_RESOURCE(subtitle_models);
}

namespace {

constexpr int kTransferTimeoutMs = 15000;
constexpr auto kPartSuffix = ".part";
constexpr auto kInstalledMarker = "installed.json";
constexpr auto kLicenseStoreFile = "license-accepted.json";

bool isSha256Hex(const QString &text)
{
    if (text.size() != 64)
        return false;
    for (const QChar &c : text) {
        const char l = c.toLower().toLatin1();
        if (!((l >= '0' && l <= '9') || (l >= 'a' && l <= 'f')))
            return false;
    }
    return true;
}

QString formatSizeText(qint64 bytes)
{
    if (bytes <= 0)
        return QString();
    double value = double(bytes);
    const char *units[] = {"B", "KB", "MB", "GB", "TB"};
    int unit = 0;
    while (value >= 1000.0 && unit < 4) {
        value /= 1000.0;
        ++unit;
    }
    if (unit == 0)
        return QString::number(qint64(value)) + QStringLiteral(" B");
    return QString::number(value, 'f', 1) + QLatin1Char(' ') + QLatin1String(units[unit]);
}

// ---- 清单（models.json）----

struct ManifestFile {
    QString name;
    qint64 size = 0;
    QString sha256;        // 小写十六进制；placeholder=true 时为空
    QStringList urls;      // 镜像顺序：依次尝试
    bool placeholder = false;   // 显式占位（无哈希，仅按大小校验；内置清单未使用）
};

struct ManifestLicense {
    QString name, version, url, text;
};

struct ManifestModel {
    QString modelId, engine, displayName, tag, summary, milestone;
    ManifestLicense license;
    QList<ManifestFile> files;

    qint64 totalSize() const
    {
        qint64 total = 0;
        for (const auto &f : files)
            total += f.size;
        return total;
    }
};

struct ModelManifest {
    int version = 0;
    QList<ManifestModel> models;

    const ManifestModel *find(const QString &id) const
    {
        for (const auto &m : models)
            if (m.modelId == id)
                return &m;
        return nullptr;
    }
};

// 忽略所有 "_" 开头的说明字段（占位/来源标注）。
bool parseManifest(const QByteArray &raw, ModelManifest *out, QString *error)
{
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        *error = QStringLiteral("JSON 无效");
        return false;
    }
    const QJsonObject root = doc.object();
    out->version = root.value(QStringLiteral("manifestVersion")).toInt(0);
    const QJsonValue modelsValue = root.value(QStringLiteral("models"));
    if (!modelsValue.isArray()) {
        *error = QStringLiteral("缺少 models 数组");
        return false;
    }
    QStringList seenIds;
    for (const QJsonValue &modelValue : modelsValue.toArray()) {
        const QJsonObject modelObject = modelValue.toObject();
        ManifestModel m;
        m.modelId = modelObject.value(QStringLiteral("modelId")).toString();
        m.engine = modelObject.value(QStringLiteral("engine")).toString();
        m.displayName = modelObject.value(QStringLiteral("displayName")).toString();
        m.tag = modelObject.value(QStringLiteral("tag")).toString();
        m.summary = modelObject.value(QStringLiteral("summary")).toString();
        m.milestone = modelObject.value(QStringLiteral("milestone")).toString();
        if (m.modelId.isEmpty() || m.engine.isEmpty()) {
            *error = QStringLiteral("modelId/engine 缺失");
            return false;
        }
        if (seenIds.contains(m.modelId)) {
            *error = QStringLiteral("modelId 重复：") + m.modelId;
            return false;
        }
        seenIds.append(m.modelId);
        const QJsonObject licenseObject = modelObject.value(QStringLiteral("license")).toObject();
        m.license.name = licenseObject.value(QStringLiteral("name")).toString();
        m.license.version = licenseObject.value(QStringLiteral("version")).toString();
        m.license.url = licenseObject.value(QStringLiteral("url")).toString();
        m.license.text = licenseObject.value(QStringLiteral("text")).toString();

        const QJsonValue filesValue = modelObject.value(QStringLiteral("files"));
        if (!filesValue.isArray()) {
            *error = QStringLiteral("缺少 files 数组：") + m.modelId;
            return false;
        }
        QStringList seenFiles;
        for (const QJsonValue &fileValue : filesValue.toArray()) {
            const QJsonObject fileObject = fileValue.toObject();
            ManifestFile f;
            f.name = fileObject.value(QStringLiteral("name")).toString();
            f.size = fileObject.value(QStringLiteral("size")).toInteger(-1);
            f.sha256 = fileObject.value(QStringLiteral("sha256")).toString().toLower();
            f.placeholder = fileObject.value(QStringLiteral("placeholder")).toBool(false);
            if (f.name.isEmpty() || f.size < 0) {
                *error = QStringLiteral("文件 name/size 无效：") + f.name;
                return false;
            }
            if (seenFiles.contains(f.name)) {
                *error = QStringLiteral("文件名重复：") + f.name;
                return false;
            }
            seenFiles.append(f.name);
            const QJsonValue urlsValue = fileObject.value(QStringLiteral("urls"));
            if (!urlsValue.isArray()) {
                *error = QStringLiteral("缺少 urls 数组：") + f.name;
                return false;
            }
            for (const QJsonValue &urlValue : urlsValue.toArray()) {
                const QUrl url(urlValue.toString());
                if (!url.isValid() || url.scheme().isEmpty() || url.host().isEmpty()) {
                    *error = QStringLiteral("URL 无效：") + urlValue.toString();
                    return false;
                }
                f.urls.append(urlValue.toString());
            }
            if (f.urls.isEmpty()) {
                *error = QStringLiteral("缺少下载源：") + f.name;
                return false;
            }
            if (!f.placeholder && !isSha256Hex(f.sha256)) {
                *error = QStringLiteral("sha256 必须是 64 位十六进制（或显式 placeholder）：") + f.name;
                return false;
            }
            m.files.append(f);
        }
        if (m.files.isEmpty()) {
            *error = QStringLiteral("模型没有文件：") + m.modelId;
            return false;
        }
        out->models.append(m);
    }
    if (out->models.isEmpty()) {
        *error = QStringLiteral("清单为空");
        return false;
    }
    return true;
}

// installed.json（清单版本 + 每文件哈希）
bool writeInstalledMarker(const QString &path, int manifestVersion, const ManifestModel &m, QString *error)
{
    QJsonArray files;
    for (const auto &f : m.files)
        files.append(QJsonObject{{QStringLiteral("name"), f.name},
                                 {QStringLiteral("size"), f.size},
                                 {QStringLiteral("sha256"), f.sha256}});
    const QJsonObject root{{QStringLiteral("manifestVersion"), manifestVersion},
                           {QStringLiteral("modelId"), m.modelId},
                           {QStringLiteral("files"), files}};
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        *error = QStringLiteral("无法写入 %1").arg(path);
        return false;
    }
    file.write(QJsonDocument(root).toJson());
    if (!file.commit()) {
        *error = QStringLiteral("提交失败 %1").arg(path);
        return false;
    }
    return true;
}

// installed.json 内容可解析且 modelId 对得上（内容损坏 → stale）
bool installedMarkerValid(const QString &path, const ManifestModel &m)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject())
        return false;
    const QJsonObject root = doc.object();
    return root.value(QStringLiteral("modelId")).toString() == m.modelId
           && root.value(QStringLiteral("files")).isArray();
}

// 已安装文件健康检查（§5.3：加载前检查大小，文件缺失/大小不符 → stale）
bool installedFilesHealthy(const ManifestModel &m, const QString &dir, QString *why)
{
    for (const auto &f : m.files) {
        const QFileInfo info(dir + QLatin1Char('/') + f.name);
        if (!info.exists() || !info.isFile()) {
            *why = QStringLiteral("文件缺失：%1").arg(f.name);
            return false;
        }
        if (f.size > 0 && info.size() != f.size) {
            *why = QStringLiteral("文件大小不符：%1").arg(f.name);
            return false;
        }
    }
    return true;
}

// 把已有 .part 的前 prefix 字节喂进哈希（续传前重算哈希前缀）
bool hashPrefix(QFile *part, qint64 prefix, QCryptographicHash *hash)
{
    char buffer[65536];
    qint64 hashed = 0;
    while (hashed < prefix) {
        const qint64 want = qMin<qint64>(qint64(sizeof(buffer)), prefix - hashed);
        const qint64 got = part->read(buffer, want);
        if (got <= 0)
            return false;
        hash->addData(QByteArrayView(buffer, got));
        hashed += got;
    }
    return true;
}

// ---- 下载运行状态 ----

struct DownloadState {
    int fileIndex = 0;          // 清单内当前文件
    int mirrorIndex = -1;       // 当前镜像
    QString partPath, finalPath;
    qint64 resumeOffset = 0;    // 本次请求 Range 起点（0 = 全新）
    qint64 fileDone = 0;        // 本文件累计字节（含续传前缀）
    bool rangeResolved = false; // 已按 206/200 决定续传或重下
    bool cancelRequested = false;    // 用户取消（保留 .part）
    bool finishedHandled = false;
    QString lastError;          // 最近一次镜像失败原因（全部用尽时上报）
    std::unique_ptr<QFile> part;
    std::unique_ptr<QCryptographicHash> hash;
    QPointer<QNetworkReply> reply;
};

struct ModelRuntime {
    QString status = QStringLiteral("notInstalled");   // notInstalled|downloading|verifying|installed|failed|stale
    double progress = 0.0;
    qint64 bytesDone = 0, bytesTotal = 0;
    QString errorText;
    std::unique_ptr<DownloadState> dl;   // 仅下载期间非空
};

// 许可确认存储：<root>/license-accepted.json，{ modelId: licenseVersion }。
// 放在模型根目录下，随 SUBREC_MODELS_ROOT 一起被测试隔离。
struct LicenseStore {
    QString path;
    QHash<QString, QString> accepted;

    void load()
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return;
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        if (!doc.isObject())
            return;
        const QJsonObject object = doc.object();
        for (auto it = object.begin(); it != object.end(); ++it)
            accepted.insert(it.key(), it.value().toString());
    }

    bool save() const
    {
        QJsonObject object;
        for (auto it = accepted.begin(); it != accepted.end(); ++it)
            object.insert(it.key(), it.value());
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly))
            return false;
        file.write(QJsonDocument(object).toJson());
        return file.commit();
    }
};

} // namespace

// ---- 实现类（QObject；无自有信号，全部经 m_owner 转发，不需要 Q_OBJECT/moc）----
class ModelManager::ModelManagerPrivate : public QObject {
public:
    // 不挂 QObject 父对象：由 ModelManager 析构函数显式 delete（避免 ~QObject 二次删除）
    explicit ModelManagerPrivate(ModelManager *owner);
    ~ModelManagerPrivate() override;

    QVariantList models() const;
    void requestDownload(const QString &modelId);
    void acceptLicense(const QString &modelId, const QString &licenseVersion);
    void cancelDownload(const QString &modelId);
    QString remove(const QString &modelId);
    std::optional<subtitle::EngineRequest> engineRequestFor(const QString &modelId);

private:
    QString modelDirOf(const ManifestModel &m) const
    {
        return m_root + QLatin1Char('/') + m.modelId;
    }

    ModelRuntime *state(const QString &modelId) { return m_state.value(modelId); }
    const ModelRuntime *state(const QString &modelId) const { return m_state.value(modelId); }

    bool licenseAcceptedFor(const ManifestModel &m) const
    {
        const auto it = m_licenses.accepted.constFind(m.modelId);
        if (it == m_licenses.accepted.constEnd())
            return false;
        if (m.license.version.isEmpty())
            return true;
        return *it == m.license.version;
    }

    void emitChanged() { emit m_owner->modelsChanged(); }

    void loadManifest();
    void scanInstalledState();
    void markStale(const QString &modelId, const QString &why);
    qint64 computeBytesDone(const ManifestModel &m, const ModelRuntime &st) const;
    void updateProgress(const QString &modelId);
    void advanceFile(const QString &modelId);
    void tryMirror(const QString &modelId, int mirrorIndex);
    void closePart(DownloadState &dl);
    void onReplyReady(const QString &modelId, QNetworkReply *reply);
    void onReplyFinished(const QString &modelId, QNetworkReply *reply);
    void verifyDownloadedFile(const QString &modelId, const QString &digestHex);
    void finalizeModel(const QString &modelId);
    void failDownload(const QString &modelId, const QString &error, bool keepPart);
    bool urlAllowed(const QUrl &url) const;
    QString replyErrorText(QNetworkReply *reply) const;

    ModelManager *m_owner = nullptr;
    QString m_root;
    QString m_manifestPath;      // 空 = 内置 :/subtitle/models/models.json
    bool m_hooks = false;
    ModelManifest m_manifest;
    LicenseStore m_licenses;
    QHash<QString, ModelRuntime *> m_state;   // 清单内每个模型一条；析构统一删除
    QNetworkAccessManager *m_nam = nullptr;
};

ModelManager::ModelManagerPrivate::ModelManagerPrivate(ModelManager *owner)
    : QObject(nullptr), m_owner(owner)
{
    // 系统代理（§5.3）。应用级配置，进程内做一次即可。
    static const bool proxyConfigured = [] {
        QNetworkProxyFactory::setUseSystemConfiguration(true);
        return true;
    }();
    Q_UNUSED(proxyConfigured);

    m_hooks = qgetenv("SUBREC_MODELS_TEST_HOOKS") == QByteArrayLiteral("1");
    if (m_hooks) {
        const QByteArray root = qgetenv("SUBREC_MODELS_ROOT");
        if (!root.isEmpty())
            m_root = QString::fromUtf8(root);
        const QByteArray manifest = qgetenv("SUBREC_MODELS_MANIFEST");
        if (!manifest.isEmpty())
            m_manifestPath = QString::fromUtf8(manifest);
    }
    if (m_root.isEmpty())
        m_root = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                 + QStringLiteral("/models");

    m_nam = new QNetworkAccessManager(this);
    if (m_hooks)
        m_nam->setProxy(QNetworkProxy(QNetworkProxy::NoProxy));   // 测试直连本地回环

    m_licenses.path = m_root + QLatin1Char('/') + QLatin1String(kLicenseStoreFile);
    m_licenses.load();
    loadManifest();
    for (const auto &m : m_manifest.models)
        m_state.insert(m.modelId, new ModelRuntime);
    scanInstalledState();
}

ModelManager::ModelManagerPrivate::~ModelManagerPrivate()
{
    qDeleteAll(m_state);
}

void ModelManager::ModelManagerPrivate::loadManifest()
{
    QByteArray raw;
    QString source;
    if (!m_manifestPath.isEmpty()) {
        QFile file(m_manifestPath);
        if (!file.open(QIODevice::ReadOnly)) {
            qWarning("ModelManager: 清单文件无法打开：%s", qPrintable(m_manifestPath));
            return;
        }
        raw = file.readAll();
        source = m_manifestPath;
    } else {
        ensureBuiltinModelsResource();
        QFile file(QStringLiteral(":/subtitle/models/models.json"));
        if (!file.open(QIODevice::ReadOnly)) {
            qWarning("ModelManager: 内置清单缺失");
            return;
        }
        raw = file.readAll();
        source = QStringLiteral("内置清单");
    }
    ModelManifest parsed;
    QString error;
    if (!parseManifest(raw, &parsed, &error)) {
        qWarning("ModelManager: 清单解析失败（%s）：%s", qPrintable(source), qPrintable(error));
        return;
    }
    m_manifest = std::move(parsed);
}

void ModelManager::ModelManagerPrivate::scanInstalledState()
{
    for (const auto &m : m_manifest.models) {
        ModelRuntime *st = state(m.modelId);
        if (!st || st->status == QLatin1String("downloading")
                 || st->status == QLatin1String("verifying"))
            continue;
        const QString dir = modelDirOf(m);
        const QString marker = dir + QLatin1Char('/') + QLatin1String(kInstalledMarker);
        if (!QFileInfo::exists(marker)) {
            st->status = QStringLiteral("notInstalled");
            st->progress = 0.0;
            st->bytesDone = 0;
            st->errorText.clear();
            continue;
        }
        QString why;
        if (installedMarkerValid(marker, m) && installedFilesHealthy(m, dir, &why)) {
            st->status = QStringLiteral("installed");
            st->progress = 1.0;
            st->bytesTotal = m.totalSize();
            st->bytesDone = st->bytesTotal;
            st->errorText.clear();
        } else {
            st->status = QStringLiteral("stale");
            st->errorText = why.isEmpty() ? QStringLiteral("安装记录无效") : why;
        }
    }
}

QVariantList ModelManager::ModelManagerPrivate::models() const
{
    QVariantList rows;
    for (const auto &m : m_manifest.models) {
        const ModelRuntime *st = state(m.modelId);
        QVariantMap row;
        row.insert(QStringLiteral("modelId"), m.modelId);
        row.insert(QStringLiteral("displayName"), m.displayName);
        row.insert(QStringLiteral("tag"), m.tag);
        row.insert(QStringLiteral("summary"), m.summary);
        row.insert(QStringLiteral("sizeBytes"), m.totalSize());
        row.insert(QStringLiteral("sizeText"), formatSizeText(m.totalSize()));
        row.insert(QStringLiteral("licenseName"), m.license.name);
        row.insert(QStringLiteral("licenseUrl"), m.license.url);
        row.insert(QStringLiteral("licenseText"), m.license.text);
        row.insert(QStringLiteral("licenseVersion"), m.license.version);
        row.insert(QStringLiteral("licenseAccepted"), licenseAcceptedFor(m));
        row.insert(QStringLiteral("status"), st ? st->status : QStringLiteral("notInstalled"));
        row.insert(QStringLiteral("progress"), st ? st->progress : 0.0);
        row.insert(QStringLiteral("bytesDone"), st ? st->bytesDone : qint64(0));
        row.insert(QStringLiteral("bytesTotal"), st ? st->bytesTotal : qint64(0));
        row.insert(QStringLiteral("errorText"), st ? st->errorText : QString());
        // selected/inUse 由 UI 侧决定（ModelCatalog::refreshCatalog 用 asr/modelId 覆盖 selected）；
        // E 不掌握设置与录制状态，给中性的 false（键名必须存在，见 model_catalog.cpp:54-60）。
        row.insert(QStringLiteral("selected"), false);
        row.insert(QStringLiteral("inUse"), false);
        rows.append(row);
    }
    return rows;
}

bool ModelManager::ModelManagerPrivate::urlAllowed(const QUrl &url) const
{
    const QString scheme = url.scheme().toLower();
    if (scheme == QLatin1String("https"))
        return true;
    // 仅测试钩子：允许本地回环 http（CI 的 QTcpServer 用例）；生产只走 https
    if (m_hooks && scheme == QLatin1String("http") && url.host() == QLatin1String("127.0.0.1"))
        return true;
    return false;
}

QString ModelManager::ModelManagerPrivate::replyErrorText(QNetworkReply *reply) const
{
    const int httpCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    QString text;
    switch (reply->error()) {
    case QNetworkReply::ContentNotFoundError:
        text = QStringLiteral("下载源不存在"); break;
    case QNetworkReply::ConnectionRefusedError:
        text = QStringLiteral("连接被拒绝"); break;
    case QNetworkReply::RemoteHostClosedError:
        text = QStringLiteral("连接中断"); break;
    case QNetworkReply::TimeoutError:
        text = QStringLiteral("下载超时"); break;
    default:
        text = QStringLiteral("网络错误（%1）").arg(int(reply->error())); break;
    }
    if (httpCode >= 400)
        text += QStringLiteral("（HTTP %1）").arg(httpCode);
    return text;
}

void ModelManager::ModelManagerPrivate::requestDownload(const QString &modelId)
{
    const ManifestModel *m = m_manifest.find(modelId);
    if (!m) {
        emit m_owner->downloadFailed(modelId, QStringLiteral("未知模型：%1").arg(modelId));
        return;
    }
    ModelRuntime *st = state(modelId);
    if (!st)
        return;
    if (st->status == QLatin1String("downloading") || st->status == QLatin1String("verifying"))
        return;   // 已在进行中
    if (!licenseAcceptedFor(*m)) {
        st->status = QStringLiteral("failed");
        st->errorText = QStringLiteral("请先阅读并同意模型许可协议（%1）").arg(m->license.name);
        st->progress = 0.0;
        st->bytesDone = 0;
        emitChanged();
        emit m_owner->downloadFailed(modelId, st->errorText);
        return;
    }
    st->status = QStringLiteral("downloading");
    st->errorText.clear();
    st->bytesTotal = m->totalSize();
    st->progress = 0.0;
    st->bytesDone = 0;
    st->dl.reset();
    updateProgress(modelId);
    emitChanged();
    advanceFile(modelId);
}

qint64 ModelManager::ModelManagerPrivate::computeBytesDone(const ManifestModel &m,
                                                           const ModelRuntime &st) const
{
    qint64 done = 0;
    const QString dir = modelDirOf(m);
    for (int i = 0; i < m.files.size(); ++i) {
        if (st.dl && st.dl->fileIndex == i) {
            done += st.dl->fileDone;
            break;
        }
        const QFileInfo info(dir + QLatin1Char('/') + m.files.at(i).name);
        if (info.isFile() && (m.files.at(i).size == 0 || info.size() == m.files.at(i).size))
            done += m.files.at(i).size;
        else
            break;   // 之后的文件尚未开始
    }
    return done;
}

void ModelManager::ModelManagerPrivate::updateProgress(const QString &modelId)
{
    const ManifestModel *m = m_manifest.find(modelId);
    ModelRuntime *st = state(modelId);
    if (!m || !st)
        return;
    st->bytesDone = computeBytesDone(*m, *st);
    st->progress = st->bytesTotal > 0 ? double(st->bytesDone) / double(st->bytesTotal) : 0.0;
    emitChanged();
}

void ModelManager::ModelManagerPrivate::advanceFile(const QString &modelId)
{
    const ManifestModel *m = m_manifest.find(modelId);
    ModelRuntime *st = state(modelId);
    if (!m || !st)
        return;
    const int nextIndex = st->dl ? st->dl->fileIndex + 1 : 0;
    if (st->dl)
        closePart(*st->dl);
    int index = nextIndex;
    const QString dir = modelDirOf(*m);
    while (index < m->files.size()) {
        const ManifestFile &f = m->files.at(index);
        const QFileInfo info(dir + QLatin1Char('/') + f.name);
        if (info.isFile() && (f.size == 0 || info.size() == f.size))
            ++index;   // 上次尝试已完成的文件（按清单大小跳过）
        else
            break;
    }
    if (index >= m->files.size()) {
        finalizeModel(modelId);
        return;
    }
    st->dl = std::make_unique<DownloadState>();
    st->dl->fileIndex = index;
    st->status = QStringLiteral("downloading");
    updateProgress(modelId);
    tryMirror(modelId, 0);
}

void ModelManager::ModelManagerPrivate::closePart(DownloadState &dl)
{
    if (!dl.part)
        return;
    if (dl.part->isOpen()) {
        dl.part->flush();
        dl.part->close();
    }
    dl.part.reset();
}

void ModelManager::ModelManagerPrivate::tryMirror(const QString &modelId, int mirrorIndex)
{
    const ManifestModel *m = m_manifest.find(modelId);
    ModelRuntime *st = state(modelId);
    if (!m || !st || !st->dl)
        return;
    DownloadState &dl = *st->dl;
    const ManifestFile &f = m->files.at(dl.fileIndex);
    if (mirrorIndex >= f.urls.size()) {
        const QString reason = dl.lastError.isEmpty() ? QStringLiteral("无可用下载源") : dl.lastError;
        failDownload(modelId, QStringLiteral("全部下载源均失败：%1").arg(reason), /*keepPart=*/true);
        return;
    }
    dl.mirrorIndex = mirrorIndex;
    const QUrl url(f.urls.at(mirrorIndex));
    if (!urlAllowed(url)) {
        dl.lastError = QStringLiteral("不允许的下载地址（仅 https）：%1").arg(url.toString());
        tryMirror(modelId, mirrorIndex + 1);
        return;
    }
    const QString dir = modelDirOf(*m);
    if (!QDir().mkpath(dir)) {
        failDownload(modelId, QStringLiteral("无法创建模型目录：%1").arg(dir), false);
        return;
    }
    st->status = QStringLiteral("downloading");
    dl.partPath = dir + QLatin1Char('/') + f.name + QLatin1String(kPartSuffix);
    dl.finalPath = dir + QLatin1Char('/') + f.name;
    closePart(dl);
    dl.hash = std::make_unique<QCryptographicHash>(QCryptographicHash::Sha256);
    dl.rangeResolved = false;
    dl.cancelRequested = false;
    dl.finishedHandled = false;
    dl.fileDone = 0;
    dl.resumeOffset = 0;

    // 已有 .part：先按清单大小截断越界部分，再重算哈希前缀（§5.3 续传）
    qint64 have = QFileInfo(dl.partPath).size();
    if (have > f.size) {
        QFile::remove(dl.partPath);
        have = 0;
    }
    if (have > 0) {
        QFile existing(dl.partPath);
        if (!existing.open(QIODevice::ReadOnly) || !hashPrefix(&existing, have, dl.hash.get())) {
            existing.close();
            QFile::remove(dl.partPath);
            have = 0;
            dl.hash->reset();
        }
    }
    if (have == f.size) {
        // .part 已完整：本地直接校验，不发网络请求
        dl.fileDone = have;
        verifyDownloadedFile(modelId, QString::fromLatin1(dl.hash->result().toHex()));
        return;
    }

    const bool resume = have > 0;
    auto part = std::make_unique<QFile>(dl.partPath);
    if (!part->open(resume ? (QIODevice::WriteOnly | QIODevice::Append)
                           : (QIODevice::WriteOnly | QIODevice::Truncate))) {
        failDownload(modelId, QStringLiteral("无法写入临时文件：%1").arg(dl.partPath), false);
        return;
    }
    dl.part = std::move(part);
    dl.resumeOffset = have;
    dl.fileDone = have;

    QNetworkRequest request(url);
    if (resume)
        request.setRawHeader(QByteArrayLiteral("Range"),
                             QByteArrayLiteral("bytes=") + QByteArray::number(have) + '-');
    request.setTransferTimeout(kTransferTimeoutMs);
    QNetworkReply *reply = m_nam->get(request);
    dl.reply = reply;
    connect(reply, &QNetworkReply::readyRead, this,
            [this, modelId, reply] { onReplyReady(modelId, reply); });
    connect(reply, &QNetworkReply::finished, this,
            [this, modelId, reply] { onReplyFinished(modelId, reply); });
    updateProgress(modelId);
}

void ModelManager::ModelManagerPrivate::onReplyReady(const QString &modelId, QNetworkReply *reply)
{
    ModelRuntime *st = state(modelId);
    if (!st || !st->dl || st->dl->reply != reply)
        return;
    DownloadState &dl = *st->dl;
    const ManifestModel *m = m_manifest.find(modelId);
    if (!m)
        return;

    if (!dl.rangeResolved) {
        const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (code == 0)
            return;   // 响应头还没到，等下一个 readyRead
        dl.rangeResolved = true;
        if (code >= 400) {
            return;   // 交给 finished 的错误分支换镜像（不消费错误响应体）
        }
        if (code == 206) {
            // 续传确认：Content-Range 起点必须与 Range 请求一致
            const QByteArray range = reply->rawHeader(QByteArrayLiteral("Content-Range"));
            if (!range.isEmpty()
                && !range.startsWith(QByteArrayLiteral("bytes ") + QByteArray::number(dl.resumeOffset) + '-')) {
                dl.lastError = QStringLiteral("服务器返回了错误的 Content-Range");
                reply->abort();
                return;
            }
        } else if (code == 200 && dl.resumeOffset > 0) {
            // 服务器忽略 Range：从头重下（重置哈希与 .part）
            dl.lastError.clear();
            dl.resumeOffset = 0;
            dl.fileDone = 0;
            dl.hash->reset();
            closePart(dl);
            QFile::remove(dl.partPath);
            auto part = std::make_unique<QFile>(dl.partPath);
            if (!part->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                reply->abort();
                return;
            }
            dl.part = std::move(part);
        }
        // 200 且无续传前缀：正常全新下载，直接追加
    }

    const QByteArray chunk = reply->readAll();
    if (chunk.isEmpty())
        return;
    const ManifestFile &f = m->files.at(dl.fileIndex);
    if (dl.fileDone + chunk.size() > f.size) {
        dl.lastError = QStringLiteral("下载数据超出清单大小：%1").arg(f.name);
        reply->abort();
        return;
    }
    if (!dl.part || dl.part->write(chunk) != chunk.size()) {
        dl.lastError = QStringLiteral("写入临时文件失败：%1").arg(f.name);
        reply->abort();
        return;
    }
    dl.hash->addData(chunk);
    dl.fileDone += chunk.size();
    updateProgress(modelId);
}

void ModelManager::ModelManagerPrivate::onReplyFinished(const QString &modelId, QNetworkReply *reply)
{
    ModelRuntime *st = state(modelId);
    if (!st || !st->dl || st->dl->reply != reply)
        return;   // 已被取消或已推进到别的镜像/文件
    DownloadState &dl = *st->dl;
    if (dl.finishedHandled)
        return;
    dl.finishedHandled = true;

    if (dl.cancelRequested) {
        // 用户取消：保留 .part 供续传；状态已由 cancelDownload 置回
        closePart(dl);
        reply->deleteLater();
        dl.reply.clear();
        st->dl.reset();
        return;
    }

    if (reply->error() != QNetworkReply::NoError) {
        dl.lastError = replyErrorText(reply);
        const int next = dl.mirrorIndex + 1;
        reply->deleteLater();
        dl.reply.clear();
        tryMirror(modelId, next);   // tryMirror 内部用尽镜像时统一 failDownload
        return;
    }

    onReplyReady(modelId, reply);   // 排空可能残留的数据（一般已在 readyRead 消费完）
    closePart(dl);
    verifyDownloadedFile(modelId, QString::fromLatin1(dl.hash->result().toHex()));
}

void ModelManager::ModelManagerPrivate::verifyDownloadedFile(const QString &modelId,
                                                              const QString &digestHex)
{
    const ManifestModel *m = m_manifest.find(modelId);
    ModelRuntime *st = state(modelId);
    if (!m || !st || !st->dl)
        return;
    DownloadState &dl = *st->dl;
    const ManifestFile &f = m->files.at(dl.fileIndex);
    st->status = QStringLiteral("verifying");
    emitChanged();

    bool ok = dl.fileDone == f.size;
    if (ok && !f.placeholder)
        ok = digestHex == f.sha256;
    if (ok) {
        closePart(dl);
        QFile::remove(dl.finalPath);   // 旧文件（若有）先移除再改名
        if (!QFile::rename(dl.partPath, dl.finalPath)) {
            failDownload(modelId, QStringLiteral("无法保存文件：%1").arg(f.name), false);
            return;
        }
        advanceFile(modelId);
        return;
    }

    // 大小/哈希不符：删 .part，换下一个镜像；全部用尽则失败
    closePart(dl);
    QFile::remove(dl.partPath);
    dl.lastError = QStringLiteral("文件校验失败：%1").arg(f.name);
    tryMirror(modelId, dl.mirrorIndex + 1);
}

void ModelManager::ModelManagerPrivate::finalizeModel(const QString &modelId)
{
    const ManifestModel *m = m_manifest.find(modelId);
    ModelRuntime *st = state(modelId);
    if (!m || !st)
        return;
    st->status = QStringLiteral("verifying");
    emitChanged();
    const QString marker = modelDirOf(*m) + QLatin1Char('/') + QLatin1String(kInstalledMarker);
    QString error;
    if (!writeInstalledMarker(marker, m_manifest.version, *m, &error)) {
        failDownload(modelId, QStringLiteral("写入安装记录失败：%1").arg(error), false);
        return;
    }
    st->status = QStringLiteral("installed");
    st->progress = 1.0;
    st->bytesTotal = m->totalSize();
    st->bytesDone = st->bytesTotal;
    st->errorText.clear();
    st->dl.reset();
    emitChanged();
    emit m_owner->downloadFinished(modelId);
}

void ModelManager::ModelManagerPrivate::failDownload(const QString &modelId, const QString &error,
                                                     bool keepPart)
{
    ModelRuntime *st = state(modelId);
    if (!st)
        return;
    if (st->dl) {
        if (st->dl->reply) {
            st->dl->reply->disconnect(this);
            st->dl->reply->abort();
            st->dl->reply->deleteLater();
            st->dl->reply.clear();
        }
        closePart(*st->dl);
        if (!keepPart && !st->dl->partPath.isEmpty())
            QFile::remove(st->dl->partPath);
        st->dl.reset();
    }
    st->status = QStringLiteral("failed");
    st->errorText = error;
    st->progress = 0.0;
    st->bytesDone = 0;
    emitChanged();
    emit m_owner->downloadFailed(modelId, error);
}

void ModelManager::ModelManagerPrivate::acceptLicense(const QString &modelId,
                                                       const QString &licenseVersion)
{
    if (!m_manifest.find(modelId))
        return;
    m_licenses.accepted.insert(modelId, licenseVersion);
    if (!m_licenses.save())
        qWarning("ModelManager: 许可记录写入失败：%s", qPrintable(m_licenses.path));
    emitChanged();
}

void ModelManager::ModelManagerPrivate::cancelDownload(const QString &modelId)
{
    ModelRuntime *st = state(modelId);
    if (!st || !st->dl)
        return;   // 没有进行中的下载
    DownloadState &dl = *st->dl;
    dl.cancelRequested = true;
    closePart(dl);   // 保留 .part 供续传
    if (dl.reply)
        dl.reply->abort();   // finished（同步或异步到达）走取消分支清理
    st->status = QStringLiteral("notInstalled");
    st->progress = 0.0;
    st->bytesDone = 0;
    st->errorText.clear();
    emitChanged();
}

QString ModelManager::ModelManagerPrivate::remove(const QString &modelId)
{
    const ManifestModel *m = m_manifest.find(modelId);
    if (!m)
        return QStringLiteral("未知模型：%1").arg(modelId);
    ModelRuntime *st = state(modelId);
    if (st && (st->status == QLatin1String("downloading") || st->status == QLatin1String("verifying")))
        cancelDownload(modelId);   // 先取消进行中的下载，再整目录删除（含 .part）
    const QString dir = modelDirOf(*m);
    if (QFileInfo::exists(dir) && !QDir(dir).removeRecursively())
        return QStringLiteral("删除模型文件失败：%1").arg(dir);
    if (st) {
        st->status = QStringLiteral("notInstalled");
        st->progress = 0.0;
        st->bytesDone = 0;
        st->bytesTotal = 0;
        st->errorText.clear();
        st->dl.reset();
    }
    emitChanged();
    return QString();
}

void ModelManager::ModelManagerPrivate::markStale(const QString &modelId, const QString &why)
{
    ModelRuntime *st = state(modelId);
    if (!st || st->status == QLatin1String("stale")) {
        if (st && st->errorText != why)
            st->errorText = why;
        return;
    }
    st->status = QStringLiteral("stale");
    st->errorText = why;
    st->progress = 0.0;
    st->bytesDone = 0;
    emitChanged();
}

std::optional<subtitle::EngineRequest> ModelManager::ModelManagerPrivate::engineRequestFor(
    const QString &modelId)
{
    const ManifestModel *m = m_manifest.find(modelId);
    if (!m)
        return std::nullopt;
    const QString dir = modelDirOf(*m);
    const QString marker = dir + QLatin1Char('/') + QLatin1String(kInstalledMarker);
    if (!QFileInfo::exists(marker)) {
        const ModelRuntime *st = state(modelId);
        if (st && st->status == QLatin1String("installed"))
            markStale(modelId, QStringLiteral("缺少安装记录"));
        return std::nullopt;
    }
    if (!installedMarkerValid(marker, *m)) {
        markStale(modelId, QStringLiteral("安装记录无效"));
        return std::nullopt;
    }
    QString why;
    if (!installedFilesHealthy(*m, dir, &why)) {
        markStale(modelId, why);
        return std::nullopt;
    }
    subtitle::EngineRequest request;
    request.engineId = m->engine.toStdString();
    request.modelId = m->modelId.toStdString();
    request.modelDir = std::filesystem::path(QDir(dir).absolutePath().toStdWString());
    return request;
}

// ---- ModelManager（冻结接口转发）----

ModelManager::ModelManager(QObject *parent)
    : QObject(parent), d(new ModelManagerPrivate(this)) {}

ModelManager::~ModelManager()
{
    delete d;
    d = nullptr;
}

QVariantList ModelManager::models() const { return d->models(); }

void ModelManager::requestDownload(const QString &modelId) { d->requestDownload(modelId); }

void ModelManager::acceptLicense(const QString &modelId, const QString &licenseVersion)
{
    d->acceptLicense(modelId, licenseVersion);
}

void ModelManager::cancelDownload(const QString &modelId) { d->cancelDownload(modelId); }

QString ModelManager::remove(const QString &modelId) { return d->remove(modelId); }

std::optional<subtitle::EngineRequest> ModelManager::engineRequestFor(const QString &modelId) const
{
    return d->engineRequestFor(modelId);
}

// 显式包含 model_manager.h 的 moc 产物（AUTOMOC 约定：moc_<basename>.cpp 对应同名头）。
// 本仓库默认依赖它的目标（subtitle_m0_link_check）不引用 ModelManager 符号，自动
// mocs_compilation 路径在部分工具链组合下不会生成；显式包含保证静态库被链接时 moc 必在。
#include "moc_model_manager.cpp"

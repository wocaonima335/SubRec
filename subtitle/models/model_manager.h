#pragma once
// M0 冻结：ModelManager（design-live-v2.md §10.3 E「沿用上版」；design-verified.md §11.3 E）。
// 接口归属：M0；实现归工作包 E。CMake 目标 subtitle_models（实现阶段 Qt Core + Network；M0 桩只用 Core）。
//
// - GUI 线程对象。属性/方法名必须符合 ui/backend/model_catalog.cpp 的反射调用约定：
//   property("models")、invokeMethod("requestDownload"/"cancelDownload"/"remove")，不得改名。
// - models 的每一项是 QVariantMap，键名与 ModelListModel 的角色名一致（E 对照 model_catalog.cpp 定稿）。
// - M1 只需 SenseVoice 单模型；Silero VAD v6 随安装包分发（D22），不经下载。
// - 支持本地导入并校验哈希（D4），导入接口由 E 在实现时追加（只追加，不改下列签名）。

#include "subtitle/subtitle_types.h"

#include <QObject>
#include <QString>
#include <QVariantList>

#include <optional>

class ModelManager : public QObject { Q_OBJECT                  // GUI 线程
    Q_PROPERTY(QVariantList models READ models NOTIFY modelsChanged) // 每项含 ModelListModel 的角色名键
public:
    explicit ModelManager(QObject *parent = nullptr);
    ~ModelManager() override;
    QVariantList models() const;
    Q_INVOKABLE void requestDownload(const QString &modelId);
    Q_INVOKABLE void acceptLicense(const QString &modelId, const QString &licenseVersion);
    Q_INVOKABLE void cancelDownload(const QString &modelId);
    Q_INVOKABLE QString remove(const QString &modelId);           // "" 或错误文案
    std::optional<subtitle::EngineRequest> engineRequestFor(const QString &modelId) const;
signals:
    void modelsChanged();
    void downloadFinished(QString modelId);  void downloadFailed(QString modelId, QString error);
private:
    // 私有成员归实现方，可自由调整（工作包 E 追加的 d 指针；上方公共接口与信号仍按 M0 冻结）
    class ModelManagerPrivate;
    ModelManagerPrivate *d;
};

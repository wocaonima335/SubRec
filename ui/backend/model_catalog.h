#ifndef SUBREC_UI_MODEL_CATALOG_H
#define SUBREC_UI_MODEL_CATALOG_H

#include <QAbstractListModel>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantMap>
#include <QVector>
#include <QtQml/qqmlregistration.h>

QT_BEGIN_NAMESPACE
class QQmlEngine;
class QJSEngine;
QT_END_NAMESPACE

namespace subrec::ui {

// Module U8. Rows of P3-4 "识别模型" (ModelRow.qml, states M1–M6). Order = catalogue order
// (SenseVoice Small first, Whisper second).
class ModelListModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by ModelCatalog")
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role {
        ModelIdRole = Qt::UserRole + 1,
        DisplayNameRole,     // "SenseVoice Small" / "Whisper"
        TagRole,             // "recommended" | "alternative"
        SummaryRole,         // one-line description incl. "约 {size}"
        SizeBytesRole,       // qint64, 0 until the manifest is filled
        SizeTextRole,        // "205 MB" or "" when unknown
        LicenseNameRole,
        LicenseUrlRole,
        LicenseTextRole,     // "" when the manifest only has a URL (D3 "请在浏览器中阅读许可原文")
        LicenseAcceptedRole, // bool
        // "notInstalled" (M1) | "downloading" (M2) | "verifying" (M3) | "installed" (M4) |
        // "failed" (M5) | "stale" (M6)
        StatusRole,
        ProgressRole,        // 0..1 (M2)
        BytesDoneRole,
        BytesTotalRole,
        ErrorTextRole,       // M5 reason (P3-4 error wording table)
        SelectedRole,        // == SubtitleView.modelId
        InUseRole,           // recording or finishing with it: delete disabled
    };
    Q_ENUM(Role)

    explicit ModelListModel(QObject *parent = nullptr);
    ~ModelListModel() override;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    int count() const;

signals:
    void countChanged();

private:
    QVector<QVariantMap> rows_;
    friend class ModelCatalog;
};

// Module U8. GUI thread. QML singleton `ModelCatalog`: model download/licence/delete actions.
// Backed by the subtitle ModelManager (module E of AI_SUBTITLE_DESIGN, QML context name
// "subtitleModels") once it exists; its QML API is not frozen by M0, so this view defines
// what the UI needs and U8 adapts to whatever E ships. Unattached: two rows from the
// built-in catalogue, both "notInstalled", actions no-ops.
class ModelCatalog : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(subrec::ui::ModelListModel *models READ models CONSTANT)
    // SubtitleView.modelId is installed and verified.
    Q_PROPERTY(bool selectedModelReady READ selectedModelReady NOTIFY catalogChanged)
    Q_PROPERTY(bool anyDownloading READ anyDownloading NOTIFY catalogChanged)

public:
    explicit ModelCatalog(QObject *parent); // no default: see singleton_provider.h
    ~ModelCatalog() override;

    static void setInstance(ModelCatalog *instance);
    static ModelCatalog *create(QQmlEngine *engine, QJSEngine *);

    void attach(QObject *modelManager); // borrowed; nullptr detaches

    // Call after changing asr/modelId so selection roles update immediately.
    void refreshSelection();

    ModelListModel *models() const;
    bool selectedModelReady() const;
    bool anyDownloading() const;

    // Row as a map (role names as keys) for dialog texts D2/D3/D4; {} if unknown.
    Q_INVOKABLE QVariantMap modelInfo(const QString &modelId) const;
    // M1/U2/D2 "下载". Licence not yet accepted -> emits licenseRequired (open D3), else starts.
    Q_INVOKABLE void requestDownload(const QString &modelId);
    // D3 "同意并下载": records acceptance for this model (and licence version if available).
    Q_INVOKABLE void acceptLicenseAndDownload(const QString &modelId);
    Q_INVOKABLE void cancelDownload(const QString &modelId);
    // D4 confirmed. Returns "" or the error text (ModelBusy while in use).
    Q_INVOKABLE QString remove(const QString &modelId);

signals:
    void catalogChanged();
    void licenseRequired(const QString &modelId); // open D3 in accept mode
    void downloadFinished(const QString &modelId); // T5
    void downloadFailed(const QString &modelId, const QString &error); // T6, M5

private slots:
    void refreshCatalog();

private:
    ModelListModel *models_ = nullptr; // child, never null
    QPointer<QObject> manager_;
};

} // namespace subrec::ui

#endif // SUBREC_UI_MODEL_CATALOG_H

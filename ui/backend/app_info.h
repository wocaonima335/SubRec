#ifndef SUBREC_UI_APP_INFO_H
#define SUBREC_UI_APP_INFO_H

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

QT_BEGIN_NAMESPACE
class QQmlEngine;
class QJSEngine;
QT_END_NAMESPACE

namespace subrec::ui {

// Module U7. GUI thread. QML singleton `AppInfo`: constant facts for the nav-rail version
// line (P0), P3-6 log file row and P3-7 About. Values are set by main.cpp before QML loads
// (setters below); version/hash default to the compile definitions SUBREC_VERSION /
// SUBREC_GIT_HASH when ui/CMakeLists.txt provides them, else "0.0.0" / "".
class AppInfo : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QString version READ version CONSTANT)          // "0.1.0"
    Q_PROPERTY(QString gitHash READ gitHash CONSTANT)          // short hash or ""
    Q_PROPERTY(QString versionText READ versionText CONSTANT)  // "版本 0.1.0 · abc1234"
    // Actual log file: SUBREC_LOG_FILE or {程序目录}\SubRec.log (main.cpp logging::configure).
    Q_PROPERTY(QString logFilePath READ logFilePath CONSTANT)
    Q_PROPERTY(QString licenseFilePath READ licenseFilePath CONSTANT)        // LICENSE next to the exe
    Q_PROPERTY(QString thirdPartyLicensesPath READ thirdPartyLicensesPath CONSTANT)

public:
    explicit AppInfo(QObject *parent); // no default: see singleton_provider.h
    ~AppInfo() override;

    static void setInstance(AppInfo *instance);
    static AppInfo *create(QQmlEngine *engine, QJSEngine *);

    // main.cpp, before setInstance()/QML load (properties are CONSTANT).
    void setLogFilePath(const QString &path);
    void setLicensePaths(const QString &license, const QString &thirdParty);

    QString version() const;
    QString gitHash() const;
    QString versionText() const;
    QString logFilePath() const;
    QString licenseFilePath() const;
    QString thirdPartyLicensesPath() const;

private:
    QString logFilePath_;
    QString licenseFilePath_;
    QString thirdPartyLicensesPath_;
};

} // namespace subrec::ui

#endif // SUBREC_UI_APP_INFO_H

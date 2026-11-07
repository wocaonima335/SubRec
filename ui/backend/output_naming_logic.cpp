#include "ui/backend/output_naming_logic.h"

#include <QDir>
#include <QRegularExpression>

namespace subrec::ui::naming {

// TODO(B3): implement and cover with tests/ui output_naming tests.
QString expandTemplate(const QString &templateText, const QDateTime &when,
                       const QString &sourceLabel)
{
    QString result = templateText;
    result.replace(QStringLiteral("{日期}"), when.toString(QStringLiteral("yyyyMMdd")));
    result.replace(QStringLiteral("{时间}"), when.toString(QStringLiteral("HHmmss")));
    auto source = sourceLabel;
    source.replace(QRegularExpression(QStringLiteral(R"([\\/:*?"<>|])")), QStringLiteral("_"));
    result.replace(QStringLiteral("{来源}"), source);
    return result;
}

QString validateTemplate(const QString &templateText)
{
    if (templateText.trimmed().isEmpty()
        || templateText.contains(QRegularExpression(QStringLiteral(R"([\\/:*?"<>|])"))))
        return QStringLiteral("文件名不能为空，也不能包含 \\ / : * ? \" < > |");
    return {};
}

QString uniqueFileName(const QString &directory, const QString &baseName, const QString &extension,
                       const std::function<bool(const QString &absolutePath)> &exists)
{
    for (int suffix = 1; suffix < 100000; ++suffix) {
        const auto name = suffix == 1
            ? baseName + extension
            : baseName + QStringLiteral("_") + QString::number(suffix) + extension;
        if (!exists(QDir(directory).filePath(name)))
            return name;
    }
    return {};
}

} // namespace subrec::ui::naming

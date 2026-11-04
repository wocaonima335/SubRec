#ifndef SUBREC_UI_OUTPUT_NAMING_LOGIC_H
#define SUBREC_UI_OUTPUT_NAMING_LOGIC_H

#include <QDateTime>
#include <QString>
#include <functional>

// Module B3. Pure functions for the file-name template (UI_REDESIGN Q27, P3-2, R1).
// No Qt object, no file system access except through the injected `exists` callback,
// so tests/ui can cover every rule on Linux.
namespace subrec::ui::naming {

// Default template, output/fileNameTemplate. Extension ".mp4" is fixed and never part of it.
inline const QString &defaultTemplate()
{
    static const QString t = QStringLiteral("SubRec_{日期}_{时间}");
    return t;
}

// Tokens: {日期} -> yyyyMMdd, {时间} -> HHmmss (local time of `when`),
// {来源} -> sourceLabel ("屏幕" / "摄像头"), sanitised for file names.
// Unknown {xxx} sequences are kept literally (validateTemplate rejects nothing for them).
QString expandTemplate(const QString &tmpl, const QDateTime &when, const QString &sourceLabel);

// "" when valid; otherwise the exact P3-2 error text:
// "文件名不能为空，也不能包含 \ / : * ? \" < > |". Checked on the template, before expansion.
QString validateTemplate(const QString &tmpl);

// First of base.ext, base_2.ext, base_3.ext, ... for which exists(dir/name) is false.
// Returns the file NAME (no directory). ext includes the dot (".mp4").
// Only the video path is passed to `exists`; whether a leftover same-name .srt also counts
// as taken is an open decision (UI_BATCH_PLAN.md §6, recommended: yes, caller checks both).
QString uniqueFileName(const QString &dir, const QString &baseName, const QString &ext,
                       const std::function<bool(const QString &absolutePath)> &exists);

} // namespace subrec::ui::naming

#endif // SUBREC_UI_OUTPUT_NAMING_LOGIC_H

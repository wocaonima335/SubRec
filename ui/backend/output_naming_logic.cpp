#include "ui/backend/output_naming_logic.h"

namespace subrec::ui::naming {

// TODO(B3): implement and cover with tests/ui output_naming tests.
QString expandTemplate(const QString &tmpl, const QDateTime &, const QString &) { return tmpl; }
QString validateTemplate(const QString &) { return {}; }
QString uniqueFileName(const QString &, const QString &baseName, const QString &ext,
                       const std::function<bool(const QString &)> &)
{
    return baseName + ext;
}

} // namespace subrec::ui::naming

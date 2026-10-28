#pragma once
#include "log_options.h"
#include <QDebug>
#include <QLoggingCategory>

namespace subrec::logging {
// Inline accessor shares one category across the application's static modules.
// Only frame/packet traces use this category: lifecycle, warnings, errors and
// statistics keep their original severity, text, and routing to SubRec.log.
inline QLoggingCategory &frameLog() {
    static QLoggingCategory category("subrec.frames", QtInfoMsg);
    return category;
}
inline bool configure(int argc, const char *const *argv) {
    const bool debug = debugRequested(argc, argv);
    // An explicit application setting also takes precedence over QT_LOGGING_RULES
    // for this category. Leave all other Qt categories untouched.
    frameLog().setEnabled(QtDebugMsg, debug);
    return debug;
}
inline void reportLevel(bool debug) {
    qInfo().noquote() << levelMessage(debug);
}
}

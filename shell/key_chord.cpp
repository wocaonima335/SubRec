#include "shell/key_chord.h"

namespace subrec::shell {

// TODO(H1): implement and cover in tests/ui/key_chord_tests.cpp (tests/ui/tests_codex.cmake).
KeyChord parseChord(const QString &) { return {}; }
QString storageText(const KeyChord &) { return {}; }
QString displayText(const KeyChord &) { return {}; }
QString compactText(const KeyChord &) { return {}; }
ChordValidity validateChord(const KeyChord &chord)
{
    return chord.isEmpty() ? ChordValidity::Empty : ChordValidity::Ok;
}
QString keyName(quint32) { return {}; }

} // namespace subrec::shell

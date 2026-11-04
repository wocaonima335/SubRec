#include "shell/key_chord.h"

#include <QStringList>

namespace subrec::shell {

namespace {
struct KeyDefinition {
    const char *name;
    quint32 code;
};

const KeyDefinition keyDefinitions[] = {
    {"Esc", 0x1B}, {"Tab", 0x09}, {"Enter", 0x0D}, {"Space", 0x20},
    {"Backspace", 0x08}, {"Delete", 0x2E}, {"Insert", 0x2D},
    {"Home", 0x24}, {"End", 0x23}, {"PageUp", 0x21}, {"PageDown", 0x22},
    {"Up", 0x26}, {"Down", 0x28}, {"Left", 0x25}, {"Right", 0x27},
    {"Num0", 0x60}, {"Num1", 0x61}, {"Num2", 0x62}, {"Num3", 0x63},
    {"Num4", 0x64}, {"Num5", 0x65}, {"Num6", 0x66}, {"Num7", 0x67},
    {"Num8", 0x68}, {"Num9", 0x69},
    {"Numpad0", 0x60}, {"Numpad1", 0x61}, {"Numpad2", 0x62}, {"Numpad3", 0x63},
    {"Numpad4", 0x64}, {"Numpad5", 0x65}, {"Numpad6", 0x66}, {"Numpad7", 0x67},
    {"Numpad8", 0x68}, {"Numpad9", 0x69},
    {"F1", 0x70}, {"F2", 0x71}, {"F3", 0x72}, {"F4", 0x73},
    {"F5", 0x74}, {"F6", 0x75}, {"F7", 0x76}, {"F8", 0x77},
    {"F9", 0x78}, {"F10", 0x79}, {"F11", 0x7A}, {"F12", 0x7B},
    {"F13", 0x7C}, {"F14", 0x7D}, {"F15", 0x7E}, {"F16", 0x7F},
    {"F17", 0x80}, {"F18", 0x81}, {"F19", 0x82}, {"F20", 0x83},
    {"F21", 0x84}, {"F22", 0x85}, {"F23", 0x86}, {"F24", 0x87},
};

QString canonicalKeyName(quint32 code)
{
    if (code >= 'A' && code <= 'Z')
        return QString(QChar(code));
    if (code >= '0' && code <= '9')
        return QString(QChar(code));
    if (code >= 0x60 && code <= 0x69)
        return QStringLiteral("Num%1").arg(code - 0x60);
    if (code >= 0x70 && code <= 0x87)
        return QStringLiteral("F%1").arg(code - 0x6F);
    for (const auto &definition : keyDefinitions) {
        if (definition.code == code
            && !QString::fromLatin1(definition.name).startsWith(QStringLiteral("Numpad")))
            return QString::fromLatin1(definition.name);
    }
    return {};
}

quint32 parseKey(QString text)
{
    text = text.trimmed();
    if (text.size() == 1) {
        const auto character = text.at(0).toUpper();
        if ((character >= QLatin1Char('A') && character <= QLatin1Char('Z'))
            || (character >= QLatin1Char('0') && character <= QLatin1Char('9')))
            return character.unicode();
    }
    for (const auto &definition : keyDefinitions) {
        if (text.compare(QString::fromLatin1(definition.name), Qt::CaseInsensitive) == 0)
            return definition.code;
    }
    if (text.startsWith(QStringLiteral("VK_"), Qt::CaseInsensitive))
        return parseKey(text.mid(3));
    return 0;
}
} // namespace

// TODO(H1): implement and cover in tests/ui/key_chord_tests.cpp (tests/ui/tests_codex.cmake).
KeyChord parseChord(const QString &input)
{
    KeyChord result;
    const auto parts = input.trimmed().split('+', Qt::KeepEmptyParts);
    if (input.trimmed().isEmpty() || parts.isEmpty())
        return result;

    for (int index = 0; index < parts.size() - 1; ++index) {
        const auto modifier = parts.at(index).trimmed();
        if (modifier.compare(QStringLiteral("Ctrl"), Qt::CaseInsensitive) == 0
            || modifier.compare(QStringLiteral("Control"), Qt::CaseInsensitive) == 0)
            result.modifiers |= ModCtrl;
        else if (modifier.compare(QStringLiteral("Alt"), Qt::CaseInsensitive) == 0)
            result.modifiers |= ModAlt;
        else if (modifier.compare(QStringLiteral("Shift"), Qt::CaseInsensitive) == 0)
            result.modifiers |= ModShift;
        else if (modifier.compare(QStringLiteral("Win"), Qt::CaseInsensitive) == 0
                 || modifier.compare(QStringLiteral("Meta"), Qt::CaseInsensitive) == 0
                 || modifier.compare(QStringLiteral("Windows"), Qt::CaseInsensitive) == 0)
            result.modifiers |= ModWin;
        else
            return {};
    }

    result.virtualKey = parseKey(parts.last());
    if (!result.virtualKey)
        return {};
    return result;
}

QString keyName(quint32 key)
{
    return canonicalKeyName(key);
}

QString storageText(const KeyChord &chord)
{
    if (chord.isEmpty())
        return {};
    const auto key = keyName(chord.virtualKey);
    if (key.isEmpty())
        return {};

    QStringList parts;
    if (chord.modifiers & ModCtrl)
        parts << QStringLiteral("Ctrl");
    if (chord.modifiers & ModAlt)
        parts << QStringLiteral("Alt");
    if (chord.modifiers & ModShift)
        parts << QStringLiteral("Shift");
    if ((chord.modifiers & ~(ModCtrl | ModAlt | ModShift)) || keyName(chord.virtualKey).isEmpty())
        parts << QStringLiteral("Win");
    parts << key;
    return parts.join('+');
}

QString displayText(const KeyChord &chord)
{
    const auto storage = storageText(chord);
    if (storage.isEmpty())
        return {};
    auto parts = storage.split('+');
    if (chord.virtualKey >= 0x60 && chord.virtualKey <= 0x69)
        parts.last() = QStringLiteral("小键盘 ") + QString::number(chord.virtualKey - 0x60);
    return parts.join(QStringLiteral(" + "));
}

QString compactText(const KeyChord &chord)
{
    return storageText(chord);
}

ChordValidity validateChord(const KeyChord &chord)
{
    if (chord.isEmpty())
        return ChordValidity::Empty;
    if ((chord.modifiers & ~(ModCtrl | ModAlt | ModShift)) || keyName(chord.virtualKey).isEmpty())
        return ChordValidity::Forbidden;

    const auto key = chord.virtualKey;
    if (key == 0x7B || key == 0x1B || key == 0x09 || key == 0x0D
        || key == 0x20 || key == 0x08 || key == 0x2E)
        return ChordValidity::Forbidden;
    if (key >= 0x70 && key <= 0x87) {
        if (key == 0x7B)
            return ChordValidity::Forbidden;
        return (chord.modifiers & (ModCtrl | ModAlt))
            ? ChordValidity::Ok : (chord.modifiers == ModNone ? ChordValidity::OkFunctionKeyAlone : ChordValidity::Ok);
    }
    return (chord.modifiers & (ModCtrl | ModAlt))
        ? ChordValidity::Ok : ChordValidity::NeedsCtrlOrAlt;
}

} // namespace subrec::shell

#include <QtTest>
#include "shell/key_chord.h"
using namespace subrec::shell;

class KeyChordTests : public QObject {
    Q_OBJECT
private slots:

    void defaultsAndStorage()
    {
        for (const auto &text : {defaultToggleRecordText(), defaultTogglePauseText(), defaultToggleWindowText()})
            QCOMPARE(storageText(parseChord(text)), text);
        QCOMPARE(storageText(parseChord(" alt + CTRL + r ")), QString("Ctrl+Alt+R"));
        QCOMPARE(displayText(parseChord("Ctrl+Alt+R")), QString("Ctrl + Alt + R"));
        QCOMPARE(compactText(parseChord("Ctrl+Alt+R")), QString("Ctrl+Alt+R"));
        QCOMPARE(displayText(parseChord("Alt+Num5")), QString("Alt + 小键盘 5"));
        QVERIFY(parseChord("Ctrl+garbage").isEmpty());
    }
    void validation()
    {
        QCOMPARE(validateChord({}), ChordValidity::Empty);
        for (quint32 key = 'A'; key <= 'Z'; ++key) {
            QCOMPARE(validateChord({ModNone, key}), ChordValidity::NeedsCtrlOrAlt);
            QCOMPARE(validateChord({ModShift, key}), ChordValidity::NeedsCtrlOrAlt);
            QCOMPARE(validateChord({ModCtrl, key}), ChordValidity::Ok);
            QCOMPARE(validateChord({ModAlt, key}), ChordValidity::Ok);
            QCOMPARE(validateChord({ModWin | ModCtrl, key}), ChordValidity::Forbidden);
        }
        for (quint32 key = 0x70; key <= 0x87; ++key) {
            QCOMPARE(validateChord({ModNone, key}), key == 0x7b ? ChordValidity::Forbidden : ChordValidity::OkFunctionKeyAlone);
            QCOMPARE(validateChord({ModCtrl, key}), key == 0x7b ? ChordValidity::Forbidden : ChordValidity::Ok);
            QCOMPARE(parseChord(storageText({ModAlt, key})), (KeyChord{ModAlt, key}));
        }
        for (quint32 key : {0x1bU, 0x09U, 0x0dU, 0x20U, 0x08U})
            QCOMPARE(validateChord({ModCtrl, key}), ChordValidity::Forbidden);
    }

};

QTEST_MAIN(KeyChordTests)
#include "key_chord_tests.moc"

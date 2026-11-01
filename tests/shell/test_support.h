#pragma once
#include <iostream>
#include <stdexcept>
#include <QString>
#include <QSettings>
#include <QTemporaryDir>
#define CHECK(x) do { if (!(x)) throw std::runtime_error(std::string(__FILE__) + ":" + std::to_string(__LINE__) + ": " #x); } while (false)

// No user settings (including system fallbacks) may enter these tests.
struct IsolatedSettings {
    QTemporaryDir directory;
    IsolatedSettings() {
        CHECK(directory.isValid());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory.path());
        QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, directory.path());
    }
};

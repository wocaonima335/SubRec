#ifndef SUBREC_UI_SINGLETON_PROVIDER_H
#define SUBREC_UI_SINGLETON_PROVIDER_H

#include <QJSEngine>
#include <QQmlEngine>

namespace subrec::ui::detail {

// FROZEN (UI_BATCH_PLAN.md §4). Shared by every QML singleton in SubRec.Ui.
//
// Injection pattern: main.cpp creates each backend object on the GUI thread, calls
// X::setInstance(&obj) BEFORE engine.loadFromModule("SubRec.Ui", "AppWindow"), and keeps
// the object alive longer than the engine. QML's create() hands that instance out with
// CppOwnership, so the engine never deletes it.
//
// Fallback: with nothing injected (tests/ui smoke test, qml previews, a page opened before
// integration lands) create() builds an engine-parented instance, so every class must return
// sensible defaults while unattached.
//
// Every singleton class MUST NOT be default-constructible: declare `explicit X(QObject *parent);`
// without "= nullptr". Qt 6.8 (qqmlprivate.h singletonConstructionMode) prefers a default
// constructor over create(), which would silently ignore the injected instance.
// tests/ui/ui_interface_smoke.cpp static_asserts this for every class.
template <typename T>
T *provideSingleton(T *injected, QQmlEngine *engine)
{
    if (injected) {
        QJSEngine::setObjectOwnership(injected, QJSEngine::CppOwnership);
        return injected;
    }
    auto *fallback = new T(engine);
    QJSEngine::setObjectOwnership(fallback, QJSEngine::CppOwnership);
    return fallback;
}

} // namespace subrec::ui::detail

#endif // SUBREC_UI_SINGLETON_PROVIDER_H

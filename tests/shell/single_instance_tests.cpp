// In-process protocol test for SubRec SingleInstance (S4, MINIMIZE_DESIGN.md §8.2).
// Needs Qt6::Core + Qt6::Network. Uses no mutex semantics beyond acquire() in one
// process, so it is CI-safe on Windows. The secondary side runs on a worker thread
// because notifyPrimary() blocks while the primary needs the GUI event loop to reply.
#include "shell/single_instance.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QLocalServer>
#include <QTimer>

#include <atomic>
#include <cstdio>
#include <thread>

using subrec::shell::SingleInstance;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

// Runs notifyPrimary on a worker thread while spinning the event loop here.
static bool notifyFromWorker(SingleInstance::Request request, int timeoutMs, qint64 *elapsedMs = nullptr)
{
    std::atomic<bool> done{false};
    bool result = false;
    QElapsedTimer timer;
    timer.start();
    std::thread worker([&] {
        SingleInstance secondary;
        result = secondary.notifyPrimary(request, timeoutMs);
        done = true;
    });
    while (!done) QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    worker.join();
    QCoreApplication::processEvents(); // deliver queued showRequested
    if (elapsedMs) *elapsedMs = timer.elapsed();
    return result;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    qputenv("USERNAME", QByteArray("subrec_test_") + QByteArray::number(QCoreApplication::applicationPid()));

    // 1. No primary: notify fails within the timeout (+ slack).
    {
        qint64 ms = 0;
        CHECK(!notifyFromWorker(SingleInstance::Request::Show, 300, &ms));
        CHECK(ms < 2000);
    }

    SingleInstance primary;
    CHECK(primary.acquire() == SingleInstance::Role::Primary);
#ifdef _WIN32
    // 2. A second acquire in another object sees the existing mutex.
    {
        SingleInstance other;
        CHECK(other.acquire() == SingleInstance::Role::Secondary);
    }
#endif
    CHECK(primary.listen());
    int shows = 0;
    QObject::connect(&primary, &SingleInstance::showRequested, [&] { ++shows; });

    // 3. Show is acknowledged and emits showRequested once.
    CHECK(notifyFromWorker(SingleInstance::Request::Show, 1000));
    CHECK(shows == 1);

    // 4. Noop is acknowledged and emits nothing.
    CHECK(notifyFromWorker(SingleInstance::Request::Noop, 1000));
    CHECK(shows == 1);

    // 5. Repeated requests keep working (server handles several connections).
    CHECK(notifyFromWorker(SingleInstance::Request::Show, 1000));
    CHECK(shows == 2);

    // 6. listen() is idempotent.
    CHECK(primary.listen());

    if (failures == 0) std::printf("single_instance_tests: all passed\n");
    return failures == 0 ? 0 : 1;
}

#include "capture/ddagrab_screen_source.h"
#include "queue/ffvframequeue.h"
#include "thread/ffscreengrabthread.h"
#include <cstdio>
#include <cstdlib>
#include <stdexcept>

extern "C" {
#include <libavutil/error.h>
}

#define CHECK(value) do { if (!(value)) throw std::runtime_error(#value); } while (false)
namespace {
void environment(const char *name, const char *value) {
#ifdef _WIN32
    _putenv_s(name, value ? value : "");
#else
    if (value) setenv(name, value, 1); else unsetenv(name);
#endif
}
}

// Only the production worker is linked. The fake native source makes this a
// deterministic gate test without ddagrab, DXGI, or a real device.
DdagrabScreenSource::DdagrabScreenSource() = default;
DdagrabScreenSource::~DdagrabScreenSource() = default;
CaptureStartResult DdagrabScreenSource::open(const InputRequest &request, bool)
{
    CaptureError error;
    error.stage = CaptureErrorStage::InputOpen;
    error.role = CaptureDeviceKind::Screen;
    error.backend = CaptureBackend::DesktopDuplication;
    error.userMessage = QStringLiteral("fake native open");
    error.deviceId = request.deviceId;
    return CaptureStartResult::failure(std::move(error));
}
int DdagrabScreenSource::read(AVFrame *) { return AVERROR(EIO); }
void DdagrabScreenSource::close() {}
bool DdagrabScreenSource::isAvailable() { return false; }
QStringList DdagrabScreenSource::d3d11FrameEncoders() { return {}; }
QString DdagrabScreenSource::describe() const { return {}; }
QString captureDeviceKindName(CaptureDeviceKind) { return {}; }

int main()
{
    try {
        FFVFrameQueue queue;
        queue.start();
        FFScreenGrabThread screen;
        InputRequest request;
        request.kind = CaptureDeviceKind::Screen;

        environment("SUBREC_TEST_VIDEO_OPEN_TIMEOUT", "1");
        environment("SUBREC_AUTOTEST_SCENARIO", nullptr);
        auto ordinary = screen.prepareOpenAndWait(request, &queue, nullptr, 500, {});
        CHECK(!ordinary.ok && ordinary.error
              && ordinary.error->userMessage.toStdString() == "fake native open");

        environment("SUBREC_AUTOTEST_SCENARIO", "record_seconds");
        auto hooked = screen.prepareOpenAndWait(request, &queue, nullptr, 20, {});
        CHECK(!hooked.ok && hooked.error && hooked.error->userMessage.toStdString()
              == "Timed out waiting for Desktop Duplication to start.");
        screen.requestAbort();
        CHECK(screen.waitFor(std::chrono::seconds(1)));
        screen.close();
        environment("SUBREC_TEST_VIDEO_OPEN_TIMEOUT", nullptr);
        environment("SUBREC_AUTOTEST_SCENARIO", nullptr);
        std::puts("PASS runtime app hook gate: normal open ignores hook, AutoTest enables it");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "FAIL %s\n", error.what());
        return 1;
    }
}

#include "fftimer.h"

FFTimer::FFTimer()
    : m_stop(false)
    , seekFlag(false)
    , pauseFlag(false)
    , speedFlag(false)
    , speed(1.0f)
    , speedFactor(1.0f)
{}

void FFTimer::init(FFVFrameQueue *frmQueue_, FFVRender *vRender_, FFCapWindow *capWindow_)
{
    frmQueue = frmQueue_;
    vRender = vRender_;
    capWindow = capWindow_;
}

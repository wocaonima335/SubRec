#include "ffarender.h"

FFARender::FFARender()
    : readyFlag(false)
    , seekFlag(false)
    , pauseFlag(false)
    , speedFlag(false)
    , speed(1)
{}

FFARender::~FFARender()
{
    stop();
    close();

    if (playerCtx) {
        delete playerCtx;
        playerCtx = nullptr;
    }
    if (abufOut) {
        av_freep(&abufOut);
        abufOut = nullptr;
    }
}

void FFARender::close()
{
    maxBufSize = -1;
    if (aOutput) {
        aOutput->deleteLater();
        aOutput = nullptr;
    }
    if (aPars) {
        delete aPars;
        aPars = nullptr;
    }
    if (abuf) {
        av_freep(&abuf);
        abuf = nullptr;
    }
    if (sonicCtx) {
        sonicCtx = nullptr;
    }

    readyFlag = false;
    seekFlag = false;
    pauseFlag = false;
    speedFlag = false;
    speed = 1;
}

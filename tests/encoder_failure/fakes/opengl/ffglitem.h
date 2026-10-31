#pragma once
struct AVFrame;
// Preview rendering is outside this worker/error-contract test.
class PreviewBridge
{
public:
    static PreviewBridge &instance() { static PreviewBridge bridge; return bridge; }
    void submitFrame(AVFrame *) {}
};

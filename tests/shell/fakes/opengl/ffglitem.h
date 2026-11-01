#pragma once
// TEST ONLY: records the W1/W2/W3 preview contract, without a graphics context.
class PreviewBridge {
    bool wanted_ = true;
public:
    static PreviewBridge &instance() { static PreviewBridge bridge; return bridge; }
    void setPreviewWanted(bool value) noexcept { wanted_ = value; }
    bool previewWanted() const noexcept { return wanted_; }
};

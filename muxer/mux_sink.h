#pragma once

#include <chrono>
#include <cstdint>

extern "C" {
#include <libavcodec/packet.h>
}

// Write-side contract of the recording output (RECORDING_ENGINE_REDESIGN.md §3.4, §3.5).
// FFMuxer implements it in-process; a subprocess writer could implement it later
// without changing the mux worker.

enum class MuxWriteResult : uint8_t {
    Ok,       // written
    Skipped,  // this packet was rejected; recording continues
    Fatal     // output is unusable; stop accepting packets and finalize
};

enum class FinalizeState : uint8_t {
    NotFinalized,  // finalize() not called yet
    Finalized,     // trailer and close succeeded: regular MP4
    Recoverable,   // packets were written but trailer/close failed or timed out:
                   // fragments already on disk remain playable
    FailedNoData   // header never written, or not a single packet written
};

inline const char *finalizeStateName(FinalizeState state)
{
    switch (state) {
    case FinalizeState::NotFinalized: return "not_finalized";
    case FinalizeState::Finalized: return "finalized";
    case FinalizeState::Recoverable: return "recoverable";
    case FinalizeState::FailedNoData: return "failed_no_data";
    }
    return "unknown";
}

class IMuxSink
{
public:
    virtual ~IMuxSink() = default;
    // Timestamps are in the encoder time base of the packet's stream.
    virtual MuxWriteResult write(AVPacket *packet) = 0;
    // Write the trailer and close within budget; I/O is interrupted only after
    // the budget expires. Repeated calls return the first result.
    virtual FinalizeState finalize(std::chrono::milliseconds budget) = 0;
    virtual FinalizeState finalizeState() const = 0;
    virtual uint64_t writtenPackets() const = 0;
};

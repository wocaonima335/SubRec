#pragma once
// TEST ONLY: no operating-system calls.
namespace subrec::shell {
class PowerRequest {
    bool active_ = false;
public:
    void setActive(bool value) { active_ = value; }
    bool active() const { return active_; }
};
}

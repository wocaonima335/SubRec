#pragma once

// Keeps the first fatal capture error for one recording session. The
// string-like type is generic so this policy can be tested without Qt while
// FFRecorder uses it with QString.
template <typename String>
class CaptureErrorState
{
public:
    bool setFirst(const String &message, const String &remediation)
    {
        if (hasError_) return false;
        message_ = message;
        remediation_ = remediation;
        hasError_ = true;
        return true;
    }

    bool isEmpty() const { return !hasError_; }
    const String &message() const { return message_; }
    const String &remediation() const { return remediation_; }

    void clear()
    {
        message_ = String{};
        remediation_ = String{};
        hasError_ = false;
    }

private:
    String message_;
    String remediation_;
    bool hasError_ = false;
};

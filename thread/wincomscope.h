#ifndef WINCOMSCOPE_H
#define WINCOMSCOPE_H

#ifdef _WIN32

#include <QLibrary>

class ScopedWinComInitializer
{
public:
    explicit ScopedWinComInitializer(const char *tag = nullptr)
        : m_tag(tag)
    {
        m_ole32.setFileName("ole32");
        if (!m_ole32.load()) {
            return;
        }

        m_initialize = reinterpret_cast<CoInitializeExFn>(m_ole32.resolve("CoInitializeEx"));
        m_uninitialize = reinterpret_cast<CoUninitializeFn>(m_ole32.resolve("CoUninitialize"));
        if (!m_initialize) {
            return;
        }

        constexpr unsigned long kCoinitMultithreaded = 0x0;
        m_result = m_initialize(nullptr, kCoinitMultithreaded);
        m_called = true;
        m_needUninitialize = (m_result >= 0) && (m_result != kRpcEChangedMode) && m_uninitialize;
    }

    ~ScopedWinComInitializer()
    {
        if (m_needUninitialize && m_uninitialize) {
            m_uninitialize();
        }
    }

    bool tried() const
    {
        return m_called;
    }

    bool ready() const
    {
        return m_result >= 0 || m_result == kRpcEChangedMode;
    }

    long result() const
    {
        return m_result;
    }

    const char *tag() const
    {
        return m_tag;
    }

private:
    using CoInitializeExFn = long(__stdcall *)(void *, unsigned long);
    using CoUninitializeFn = void(__stdcall *)();

    static constexpr long kRpcEChangedMode = static_cast<long>(0x80010106L);

    const char *m_tag = nullptr;
    QLibrary m_ole32;
    CoInitializeExFn m_initialize = nullptr;
    CoUninitializeFn m_uninitialize = nullptr;
    long m_result = static_cast<long>(0x80004005L);
    bool m_called = false;
    bool m_needUninitialize = false;
};

#else

class ScopedWinComInitializer
{
public:
    explicit ScopedWinComInitializer(const char *tag = nullptr)
        : m_tag(tag)
    {}

    bool tried() const
    {
        return false;
    }

    bool ready() const
    {
        return true;
    }

    long result() const
    {
        return 0;
    }

    const char *tag() const
    {
        return m_tag;
    }

private:
    const char *m_tag = nullptr;
};

#endif

#endif // WINCOMSCOPE_H

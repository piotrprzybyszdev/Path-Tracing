#pragma once

#include <array>
#include <atomic>
#include <span>
#include <thread>

namespace PathTracing
{

template<typename I, uint8_t ReservedThreadCount = 16> class ThreadDispatch
{
public:
    ThreadDispatch(size_t threadCount);

    template<typename F> void DispatchBlocking(size_t inputCount, F &&process);
    template<typename F> void Dispatch(size_t inputCount, F &&process);

    void Cancel();

private:
    const uint32_t m_ThreadCount;
    std::array<std::jthread, ReservedThreadCount> m_Threads;
    std::atomic<I> m_InputIndex = 0;

private:
    std::span<std::jthread> GetThreads();
};

template<typename I, uint8_t ReservedThreadCount>
template<typename F>
inline void ThreadDispatch<I, ReservedThreadCount>::DispatchBlocking(size_t inputCount, F &&process)
{
    Dispatch(inputCount, std::move(process));

    for (auto &thread : GetThreads())
        thread.join();
}

template<typename I, uint8_t ReservedThreadCount>
template<typename F>
inline void ThreadDispatch<I, ReservedThreadCount>::Dispatch(size_t inputCount, F &&process)
{
    m_InputIndex = 0;

    for (uint32_t threadId = 0; threadId < GetThreads().size(); threadId++)
    {
        auto &thread = GetThreads()[threadId];
        thread = std::jthread([process, inputCount, threadId, this](std::stop_token stopToken) {
            while (!stopToken.stop_requested() && m_InputIndex < inputCount)
                process(threadId, m_InputIndex++);
        });
    }
}

template<typename I, uint8_t ReservedThreadCount>
inline ThreadDispatch<I, ReservedThreadCount>::ThreadDispatch(size_t threadCount) : m_ThreadCount(threadCount)
{
    assert(m_ThreadCount <= ReservedThreadCount);
}

template<typename I, uint8_t ReservedThreadCount> inline void ThreadDispatch<I, ReservedThreadCount>::Cancel()
{
    if (!GetThreads().front().joinable())
        return;

    for (auto &thread : GetThreads())
        thread.request_stop();

    for (auto &thread : GetThreads())
        thread.join();
}

template<typename I, uint8_t ReservedThreadCount>
inline std::span<std::jthread> ThreadDispatch<I, ReservedThreadCount>::GetThreads()
{
    return std::span<std::jthread>(m_Threads.data(), m_ThreadCount);
}

}

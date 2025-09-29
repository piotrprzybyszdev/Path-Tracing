#pragma once

#include <spdlog/spdlog.h>

#include <chrono>
#include <format>
#include <map>
#include <source_location>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>

namespace PathTracing
{

namespace logger = spdlog;

class Stats
{
public:
    template<typename... Args>
    static void AddStat(const std::string &statName, std::format_string<Args...> format, Args &&...args);

    static void Clear();
    static void FlushTimers();
    static void ResetMax();

    static const std::map<std::string, std::string> &GetStats();

    static void LogStat(const std::string &stat);
    static void LogStats();

private:
    static std::map<std::string, std::string> s_Stats;
    static std::map<std::string, std::chrono::nanoseconds> s_Measurements;
    static std::map<std::string, std::chrono::nanoseconds> s_MaxMeasurements;

    friend class Timer;
    friend class MaxTimer;
};

template<typename... Args>
void Stats::AddStat(const std::string &statName, const std::format_string<Args...> format, Args &&...args)
{
    logger::trace(std::vformat(format.get(), std::make_format_args(args...)));
    s_Stats[statName] = std::vformat(format.get(), std::make_format_args(args...));
}

class Timer
{
public:
    explicit Timer(std::string &&name);
    ~Timer();

private:
    std::string m_Name;

    std::chrono::time_point<std::chrono::high_resolution_clock> m_Start;
};

class MaxTimer
{
public:
    explicit MaxTimer(std::string &&name);
    ~MaxTimer();

private:
    std::string m_Name;
    std::chrono::time_point<std::chrono::high_resolution_clock> m_Start;
};

class error : public std::runtime_error
{
public:
    explicit error(const std::string &message);
    explicit error(const char *message);
};

template<typename T, T (*checkError)(void), T success, const char *(*getErrorMessage)(T) = nullptr>
struct Assert
{
    explicit Assert(std::source_location location = std::source_location::current())
    {
#ifndef NDEBUG
        T status = checkError();

        if (status != success)
        {
            if constexpr (getErrorMessage == nullptr)
            {
                throw error(std::format(
                    "Assertion failed at {}({}:{}): {}", location.file_name(), location.line(),
                    location.column(), location.function_name()
                ));
            }
            else
            {
                throw error(std::format(
                    "Assertion failed at {}({}:{}): {}: {}", location.file_name(), location.line(),
                    location.column(), location.function_name(), getErrorMessage(status)
                ));
            }
        }
#endif
    }
};

namespace
{

template<typename T1, typename T2> T2 TrivialCopyUnsafe(const T1 &in)
{
    T2 out;
    memcpy(reinterpret_cast<void *>(&out), reinterpret_cast<const void *>(&in), sizeof(T2));
    return out;
}

template<typename T1, typename T2>
T2 TrivialCopy(const T1 &in) requires std::is_trivially_copyable_v<T1> && std::is_trivially_copyable_v<T2>
{
    return TrivialCopyUnsafe<T1, T2>(in);
}

template<typename T>
std::span<std::byte> ToByteSpan(T &value)
    requires std::is_trivially_copyable_v<T> && std::is_trivially_copyable_v<T>
{
    return std::span<std::byte>(reinterpret_cast<std::byte *>(&value), sizeof(T));
}

template<typename T>
std::span<const std::byte> ToByteSpan(const T &value)
    requires std::is_trivially_copyable_v<T> && std::is_trivially_copyable_v<T>
{
    return std::span<const std::byte>(reinterpret_cast<const std::byte *>(&value), sizeof(T));
}

template<typename T> std::span<const std::byte> ToByteSpan(T &&value) = delete;

template<typename T1, typename T2>
std::span<T2> SpanCast(std::span<T1> span)
    requires std::is_standard_layout_v<T1> && std::is_standard_layout_v<T2>
{
    return std::span(reinterpret_cast<T2 *>(span.data()), span.size() * sizeof(T1) / sizeof(T2));
}

}

#include <thread>
#include <atomic>

template<typename I, uint8_t ThreadCount>
class ThreadDispatch
{
public:
    template<typename F> void DispatchBlocking(size_t inputCount, F &&process);
    template<typename F> void Dispatch(size_t inputCount, F &&process);

    void Cancel();

private:
    std::array<std::jthread, ThreadCount> m_Threads;
    std::atomic<I> m_InputIndex = 0;
};

template<typename I, uint8_t ThreadCount>
template<typename F>
inline void ThreadDispatch<I, ThreadCount>::DispatchBlocking(size_t inputCount, F &&process)
{
    Dispatch(inputCount, std::move(process));

    for (auto &thread : m_Threads)
        thread.join();
}

template<typename I, uint8_t ThreadCount>
template<typename F>
inline void ThreadDispatch<I, ThreadCount>::Dispatch(size_t inputCount, F &&process)
{
    m_InputIndex = 0;

    for (uint32_t threadId = 0; threadId < m_Threads.size(); threadId++)
    {
        auto &thread = m_Threads[threadId];
        thread = std::jthread([process, inputCount, threadId, this](std::stop_token stopToken) {
            while (!stopToken.stop_requested() && m_InputIndex < inputCount)
                process(threadId, m_InputIndex++);
        });
    }
}

template<typename I, uint8_t ThreadCount>
inline void ThreadDispatch<I, ThreadCount>::Cancel()
{
    if (!m_Threads.front().joinable())
        return;

    for (auto &thread : m_Threads)
        thread.request_stop();

    for (auto &thread : m_Threads)
        thread.join();
}

}

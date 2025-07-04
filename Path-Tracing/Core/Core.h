#pragma once

#pragma warning(push, 0)
#include <spdlog/spdlog.h>
#pragma warning(pop)

#include <chrono>
#include <format>
#include <map>
#include <source_location>
#include <string>
#include <string_view>

namespace PathTracing
{

namespace logger = spdlog;

class Stats
{
public:
    template<typename... Args>
    static void AddStat(std::string_view statName, std::format_string<Args...> format, Args &&...args);

    static void AddMeasurement(std::string_view timer, std::chrono::nanoseconds measurement);

    static void Clear();
    static void FlushTimers();

    static const std::map<std::string, std::string> &GetStats();

    static void LogStat(std::string_view stat);
    static void LogStats();

private:
    static std::map<std::string, std::string> s_Stats;
    static std::map<std::string, std::chrono::nanoseconds> s_Measurements;
};

template<typename... Args>
void Stats::AddStat(std::string_view statName, const std::format_string<Args...> format, Args &&...args)
{
    logger::trace(std::vformat(format.get(), std::make_format_args(args...)));
    s_Stats[statName.data()] = std::vformat(format.get(), std::make_format_args(args...));
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

}

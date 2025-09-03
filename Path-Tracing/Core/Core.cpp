#include "Core.h"

namespace PathTracing
{

std::map<std::string, std::string> Stats::s_Stats = {};
std::map<std::string, std::chrono::nanoseconds> Stats::s_Measurements = {};
std::map<std::string, std::chrono::nanoseconds> Stats::s_MaxMeasurements = {};

void Stats::Clear()
{
    s_Stats.clear();
    s_Measurements.clear();
}

void Stats::FlushTimers()
{
    for (auto &[timer, maxMeasurement] : s_MaxMeasurements)
    {
        maxMeasurement = std::max(maxMeasurement, s_Measurements[timer]);

        Stats::AddStat(
            std::format("Max: {}", timer), "Max {}: {:.3f} ms", timer,
            static_cast<float>(std::chrono::duration_cast<std::chrono::microseconds>(maxMeasurement).count()
            ) / 1000.0f
        );
    }

    for (const auto &[timer, measurement] : s_Measurements)
    {
        Stats::AddStat(
            timer, "{}: {:.3f} ms", timer,
            static_cast<float>(std::chrono::duration_cast<std::chrono::microseconds>(measurement).count()) /
                1000.0f
        );
    }
    s_Measurements.clear();
}

void Stats::ResetMax()
{
    s_MaxMeasurements.clear();
}

const std::map<std::string, std::string> &Stats::GetStats()
{
    return s_Stats;
}

void Stats::LogStat(const std::string &stat)
{
    logger::info(s_Stats[stat]);
}

void Stats::LogStats()
{
    for (const auto &[name, value] : s_Stats)
    {
        logger::info(value);
    }
}

Timer::Timer(std::string &&name) : m_Name(name), m_Start(std::chrono::high_resolution_clock::now())
{
}

Timer::~Timer()
{
    Stats::s_Measurements[m_Name] += std::chrono::high_resolution_clock::now() - m_Start;
}

MaxTimer::MaxTimer(std::string &&name) : m_Name(name), m_Start(std::chrono::high_resolution_clock::now())
{
}

MaxTimer::~MaxTimer()
{
    Stats::s_Measurements[m_Name] += std::chrono::high_resolution_clock::now() - m_Start;
    Stats::s_MaxMeasurements.try_emplace(m_Name, 0);
}

error::error(const std::string &message) : std::runtime_error(message)
{
    logger::error(message);
}

error::error(const char *message) : std::runtime_error(message)
{
    logger::error(message);
}

}
#include "Core.h"

namespace PathTracing
{

std::map<std::string, std::string> Stats::s_Stats = {};
std::map<std::string, std::chrono::nanoseconds> Stats::s_Measurements = {};

void Stats::AddMeasurement(std::string_view timer, std::chrono::nanoseconds measurement)
{
    s_Measurements[timer.data()] += measurement;
}

void Stats::Clear()
{
    s_Stats.clear();
    s_Measurements.clear();
}

void Stats::FlushTimers()
{
    for (const auto &[timer, measurement] : s_Measurements)
    {
        Stats::AddStat(
            timer, "{}: {:.3f} ms", timer,
            static_cast<float>(std::chrono::duration_cast<std::chrono::microseconds>(measurement).count()) / 1000.0f
        );
    }
    s_Measurements.clear();
}

const std::map<std::string, std::string> &Stats::GetStats()
{
    return s_Stats;
}

void Stats::LogStat(std::string_view stat)
{
    logger::info(s_Stats[stat.data()]);
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
    Stats::AddMeasurement(m_Name, std::chrono::high_resolution_clock::now() - m_Start);
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
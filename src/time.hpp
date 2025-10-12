#pragma once
#include <chrono>
#include <thread>

using Clock = std::chrono::steady_clock;
using TimePoint = Clock::time_point;
using Duration = Clock::duration;

inline TimePoint
time_now()
{
	return Clock::now();
}

inline float
time_elapsed_seconds(TimePoint start)
{
	return std::chrono::duration<float>(time_now() - start).count();
}

inline float
time_delta_seconds(TimePoint from, TimePoint to)
{
	return std::chrono::duration<float>(to - from).count();
}

inline int
time_delta_milliseconds(TimePoint from, TimePoint to)
{
	return (int)std::chrono::duration_cast<std::chrono::milliseconds>(to - from).count();
}

inline int
time_elapsed_milliseconds(TimePoint start)
{
	return time_delta_milliseconds(start, time_now());
}

inline void
sleep_microseconds(int us)
{
	std::this_thread::sleep_for(std::chrono::microseconds(us));
}

inline void
sleep_milliseconds(int ms)
{
	std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

inline void
sleep_seconds(float seconds)
{
	std::this_thread::sleep_for(std::chrono::duration<float>(seconds));
}

inline Duration
microseconds(int us)
{
	return std::chrono::microseconds(us);
}

inline Duration
milliseconds(int ms)
{
	return std::chrono::milliseconds(ms);
}

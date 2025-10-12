#pragma once
#include "time.hpp"
#include "containers.hpp"

#define MAX_ZONES	  64
#define MAX_ZONE_NAME 32

struct ZoneStats
{
	fixed_string<MAX_ZONE_NAME> name;
	uint32_t					hit_count;
	float						sum_time_ms;
	float						sum_squared_ms;
	float						min_time_ms;
	float						max_time_ms;
	float						avg_time_ms;
	float						variance_ms;
};

struct Profiler
{
	fixed_map<uint32_t, ZoneStats, MAX_ZONES> zones;
	TimePoint								  frame_start;
	uint32_t								  frame_count;
	bool									  enabled;
};

struct ProfileZone
{
	Profiler   *profiler;
	uint32_t	zone_id;
	TimePoint	start;
	const char *zone_name;
};

void
profiler_init(Profiler *p);

void
profiler_begin_frame(Profiler *p);

void
profiler_print_report(Profiler *p);

void
profiler_reset_stats(Profiler *p);

void
profiler_set_enabled(Profiler *p, bool enabled);

void
profiler_zone_begin(ProfileZone* zone, Profiler* p, const char* name);

void
profiler_zone_end(ProfileZone* zone);

#define PROFILE_ZONE(profiler, name) \
    ProfileZone _zone_##__LINE__; \
    profiler_zone_begin(&_zone_##__LINE__, profiler, name)

#define PROFILE_ZONE_END(profiler) \
    profiler_zone_end(&_zone_##__LINE__)

#include "profiler.hpp"
#include <cstdio>
#include <cstring>

void
profiler_init(Profiler *p)
{
	*p = {};
	p->enabled = true;
}

void
profiler_begin_frame(Profiler *p)
{
	p->frame_start = time_now();
	p->frame_count++;
}

void
profiler_print_report(Profiler *p)
{
	if (!p->enabled)
	{
		return;
	}

	printf("\n========== PROFILER REPORT (Frame %u) ==========\n", p->frame_count);
	printf("%-30s %8s %8s %8s %8s\n", "Zone", "Avg", "Min", "Max", "Var");
	printf("----------------------------------------------------------------\n");

	for (uint32_t i = 0; i < p->zones.table_capacity(); i++)
	{
		auto &entry = p->zones.data()[i];
		if (entry.state != 1 || entry.value.hit_count == 0)
		{
			continue;
		}

		printf("%-30s %7.2fms %7.2fms %7.2fms %7.2fms\n", entry.value.name.c_str(), entry.value.avg_time_ms,
			   entry.value.min_time_ms, entry.value.max_time_ms, entry.value.variance_ms);
	}

	printf("================================================\n\n");
}

void
profiler_reset_stats(Profiler *p)
{
	for (uint32_t i = 0; i < p->zones.table_capacity(); i++)
	{
		auto &entry = p->zones.data()[i];
		if (entry.state != 1)
		{
			continue;
		}

		entry.value.hit_count = 0;
		entry.value.sum_time_ms = 0.0f;
		entry.value.sum_squared_ms = 0.0f;
		entry.value.min_time_ms = 0.0f;
		entry.value.max_time_ms = 0.0f;
		entry.value.avg_time_ms = 0.0f;
		entry.value.variance_ms = 0.0f;
	}
}

static void
record_zone_time(Profiler *p, uint32_t zone_id, const char *name, float time_ms)
{
	ZoneStats *stats = p->zones.get(zone_id);

	if (!stats)
	{
		ZoneStats new_stats = {};
		new_stats.name.set(name);
		new_stats.min_time_ms = 999999.0f;
		stats = p->zones.insert(zone_id, new_stats);
	}

	stats->hit_count++;
	stats->sum_time_ms += time_ms;
	stats->sum_squared_ms += time_ms * time_ms;

	if (time_ms < stats->min_time_ms)
	{
		stats->min_time_ms = time_ms;
	}

	if (time_ms > stats->max_time_ms)
	{
		stats->max_time_ms = time_ms;
	}

	stats->avg_time_ms = stats->sum_time_ms / stats->hit_count;

	float mean_of_squares = stats->sum_squared_ms / stats->hit_count;
	float square_of_mean = stats->avg_time_ms * stats->avg_time_ms;
	stats->variance_ms = mean_of_squares - square_of_mean;
}

void
profiler_set_enabled(Profiler *p, bool enabled)
{
	p->enabled = enabled;
}

void
profiler_zone_begin(ProfileZone *zone, Profiler *p, const char *name)
{
	zone->profiler = p;
	zone->zone_name = name;
	zone->zone_id = 0;

	if (!p || !p->enabled)
	{
		return;
	}

	zone->zone_id = hash_bytes(name, strlen(name));
	zone->start = time_now();
}

void
profiler_zone_end(ProfileZone *zone)
{
	if (!zone->profiler || !zone->profiler->enabled || zone->zone_id == 0)
	{
		return;
	}

	float elapsed_ms = time_elapsed_seconds(zone->start) * 1000.0f;
	record_zone_time(zone->profiler, zone->zone_id, zone->zone_name, elapsed_ms);
}

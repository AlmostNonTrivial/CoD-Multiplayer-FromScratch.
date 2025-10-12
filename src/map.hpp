#pragma once
#include "containers.hpp"
#include "math.hpp"

#define MAP_BOUNDS_MIN          -40.0f
#define MAP_BOUNDS_MAX          40.0f

struct Map
{
	fixed_array<OBB, 256> obb_geometry;
};

Map
generate_map();

bool
is_intersecting_map(glm::vec3 pos, Map&map);
bool
has_line_of_sight(glm::vec3 from, glm::vec3 to, Map & map);

glm::vec3
get_spawn_point(Map &map);

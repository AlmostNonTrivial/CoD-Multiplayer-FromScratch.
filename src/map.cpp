#include "map.hpp"
#include "containers.hpp"
#include "game_types.hpp"
#include "map.hpp"
#include "containers.hpp"
#include "math.hpp"

#define SPAWN_ATTEMPT_COUNT	   50
#define SPAWN_RANDOM_RANGE	   60
#define SPAWN_RANDOM_OFFSET	   20
#define SPAWN_TEST_HEIGHT	   2.0f
#define SPAWN_RAYCAST_DISTANCE 20.0f
#define SPAWN_GROUND_OFFSET	   1.0f
#define SPAWN_DEFAULT_POSITION glm::vec3(0, 2, 0)

OBB
add_rotated_box(glm::vec3 center, glm::vec3 half_extents, glm::vec3 axis, float angle_degrees)
{
	glm::quat rot = glm::angleAxis(glm::radians(angle_degrees), glm::normalize(axis));
	return obb_from_center_size_rotation(center, half_extents, rot);
}

Map
generate_map()
{

	Map map;

	map.obb_geometry.push(obb_from_center_size(glm::vec3(0, -1.0f, 0), glm::vec3(60, 0.5f, 60)));

	map.obb_geometry.push(obb_from_center_size(glm::vec3(0, 4.0f, -60), glm::vec3(60, 8.0f, 0.5f)));
	map.obb_geometry.push(obb_from_center_size(glm::vec3(0, 4.0f, 60), glm::vec3(60, 8.0f, 0.5f)));
	map.obb_geometry.push(obb_from_center_size(glm::vec3(-60, 4.0f, 0), glm::vec3(0.5f, 8.0f, 60)));
	map.obb_geometry.push(obb_from_center_size(glm::vec3(60, 4.0f, 0), glm::vec3(0.5f, 8.0f, 60)));

	map.obb_geometry.push(obb_from_center_size(glm::vec3(-20, 3.0f, 30), glm::vec3(15.0f, 6.0f, 0.5f)));
	map.obb_geometry.push(obb_from_center_size(glm::vec3(20, 3.0f, 30), glm::vec3(15.0f, 6.0f, 0.5f)));
	map.obb_geometry.push(obb_from_center_size(glm::vec3(-20, 3.0f, -30), glm::vec3(15.0f, 6.0f, 0.5f)));
	map.obb_geometry.push(obb_from_center_size(glm::vec3(20, 3.0f, -30), glm::vec3(15.0f, 6.0f, 0.5f)));
	map.obb_geometry.push(obb_from_center_size(glm::vec3(-30, 3.0f, 20), glm::vec3(0.5f, 6.0f, 15.0f)));
	map.obb_geometry.push(obb_from_center_size(glm::vec3(-30, 3.0f, -20), glm::vec3(0.5f, 6.0f, 15.0f)));
	map.obb_geometry.push(obb_from_center_size(glm::vec3(30, 3.0f, 20), glm::vec3(0.5f, 6.0f, 15.0f)));
	map.obb_geometry.push(obb_from_center_size(glm::vec3(30, 3.0f, -20), glm::vec3(0.5f, 6.0f, 15.0f)));

	map.obb_geometry.push(obb_from_center_size(glm::vec3(40, 3.0f, 35), glm::vec3(8.0f, 6.0f, 0.5f)));
	map.obb_geometry.push(obb_from_center_size(glm::vec3(45, 3.0f, 40), glm::vec3(0.5f, 6.0f, 8.0f)));
	map.obb_geometry.push(obb_from_center_size(glm::vec3(-40, 3.0f, 35), glm::vec3(8.0f, 6.0f, 0.5f)));
	map.obb_geometry.push(obb_from_center_size(glm::vec3(-45, 3.0f, 40), glm::vec3(0.5f, 6.0f, 8.0f)));
	map.obb_geometry.push(obb_from_center_size(glm::vec3(40, 3.0f, -35), glm::vec3(8.0f, 6.0f, 0.5f)));
	map.obb_geometry.push(obb_from_center_size(glm::vec3(45, 3.0f, -40), glm::vec3(0.5f, 6.0f, 8.0f)));
	map.obb_geometry.push(obb_from_center_size(glm::vec3(-40, 3.0f, -35), glm::vec3(8.0f, 6.0f, 0.5f)));
	map.obb_geometry.push(obb_from_center_size(glm::vec3(-45, 3.0f, -40), glm::vec3(0.5f, 6.0f, 8.0f)));

	map.obb_geometry.push(obb_from_center_size(glm::vec3(-12, 2.5f, 0), glm::vec3(0.5f, 5.0f, 18.0f)));
	map.obb_geometry.push(obb_from_center_size(glm::vec3(12, 2.5f, 0), glm::vec3(0.5f, 5.0f, 18.0f)));

	map.obb_geometry.push(obb_from_center_size(glm::vec3(25, 3.5f, 15), glm::vec3(2.0f, 7.0f, 2.0f)));
	map.obb_geometry.push(obb_from_center_size(glm::vec3(-25, 3.5f, 15), glm::vec3(2.0f, 7.0f, 2.0f)));
	map.obb_geometry.push(obb_from_center_size(glm::vec3(25, 3.5f, -15), glm::vec3(2.0f, 7.0f, 2.0f)));
	map.obb_geometry.push(obb_from_center_size(glm::vec3(-25, 3.5f, -15), glm::vec3(2.0f, 7.0f, 2.0f)));

	map.obb_geometry.push(obb_from_center_size(glm::vec3(0, 1.5f, 10), glm::vec3(6.0f, 3.0f, 0.5f)));
	map.obb_geometry.push(obb_from_center_size(glm::vec3(0, 1.5f, -10), glm::vec3(6.0f, 3.0f, 0.5f)));

	map.obb_geometry.push(obb_from_center_size(glm::vec3(35, 2.0f, 0), glm::vec3(0.5f, 4.0f, 8.0f)));
	map.obb_geometry.push(obb_from_center_size(glm::vec3(-35, 2.0f, 0), glm::vec3(0.5f, 4.0f, 8.0f)));
	map.obb_geometry.push(obb_from_center_size(glm::vec3(0, 2.0f, 40), glm::vec3(8.0f, 4.0f, 0.5f)));
	map.obb_geometry.push(obb_from_center_size(glm::vec3(0, 2.0f, -40), glm::vec3(8.0f, 4.0f, 0.5f)));

	map.obb_geometry.push(obb_from_center_size(glm::vec3(15, 1.0f, 25), glm::vec3(2.0f, 2.0f, 2.0f)));
	map.obb_geometry.push(obb_from_center_size(glm::vec3(-15, 1.0f, 25), glm::vec3(2.0f, 2.0f, 2.0f)));
	map.obb_geometry.push(obb_from_center_size(glm::vec3(15, 1.0f, -25), glm::vec3(2.0f, 2.0f, 2.0f)));
	map.obb_geometry.push(obb_from_center_size(glm::vec3(-15, 1.0f, -25), glm::vec3(2.0f, 2.0f, 2.0f)));

	map.obb_geometry.push(
		add_rotated_box(glm::vec3(0, 1.0f, 20), glm::vec3(5.0f, 0.5f, 8.0f), glm::vec3(1, 0, 0), 30.0f));
	map.obb_geometry.push(
		add_rotated_box(glm::vec3(0, 1.0f, -20), glm::vec3(5.0f, 0.5f, 8.0f), glm::vec3(1, 0, 0), -30.0f));

	return map;
}

bool
has_line_of_sight(glm::vec3 from, glm::vec3 to, Map &map)
{
	glm::vec3 delta = to - from;
	float	  dist = glm::length(delta);
	if (dist < 0.001f)
	{
		return true;
	}

	Ray ray = {from, delta / dist, dist};

	for (OBB &box : map.obb_geometry)
	{
		RayHit hit;
		if (raycast_obb(ray, box, &hit) && hit.distance < dist - 0.5f)

		{
			return false;
		}
	}

	return true;
}

bool
is_intersecting_map(glm::vec3 pos, Map &map)
{
	Sphere	test = {pos, PLAYER_RADIUS};
	Contact contact;
	for (OBB &box : map.obb_geometry)
	{
		if (sphere_vs_obb(test, box, &contact))
		{
			return false;
		}
	}
	return true;
}
glm::vec3
get_spawn_point(Map &map)
{
	for (int attempts = 0; attempts < SPAWN_ATTEMPT_COUNT; attempts++)
	{
		float	  x = (rand() % SPAWN_RANDOM_RANGE) - SPAWN_RANDOM_OFFSET;
		float	  z = (rand() % SPAWN_RANDOM_RANGE) - SPAWN_RANDOM_OFFSET;
		glm::vec3 pos(x, SPAWN_TEST_HEIGHT, z);

		Sphere test_sphere = {pos, PLAYER_RADIUS};

		bool collides = false;
		for (OBB &obb : map.obb_geometry)
		{
			Contact contact;
			if (sphere_vs_obb(test_sphere, obb, &contact))
			{
				collides = true;
				break;
			}
		}

		if (!collides)
		{
			Ray	  down_ray = {pos, glm::vec3(0, -1, 0), SPAWN_RAYCAST_DISTANCE};
			float closest_ground = SPAWN_RAYCAST_DISTANCE;

			for (OBB &obb : map.obb_geometry)
			{
				RayHit hit;
				if (raycast_obb(down_ray, obb, &hit) && hit.distance < closest_ground)
				{
					closest_ground = hit.distance;
				}
			}

			pos.y -= (closest_ground - PLAYER_RADIUS - SPAWN_GROUND_OFFSET);
			return pos;
		}
	}

	return SPAWN_DEFAULT_POSITION;
}

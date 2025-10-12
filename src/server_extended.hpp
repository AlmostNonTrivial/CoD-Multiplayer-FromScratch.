/*
 * Like the client and client_extended, I've tried to keep server.cpp streamlined
 */

#include "game_types.hpp"
#include "map.hpp"

inline Shot
create_shot(Player *shooter)
{
	Shot shot;
	shot.shooter_idx = shooter->player_idx;
	shot.ray.origin = shooter->position + glm::vec3(0, PLAYER_EYE_HEIGHT, 0);

	glm::vec3 forward(cos(shooter->yaw) * cos(shooter->pitch), sin(shooter->pitch),
					  sin(shooter->yaw) * cos(shooter->pitch));

	shot.ray.direction = glm::normalize(forward);
	shot.ray.length = MAX_SHOOT_RANGE;

	return shot;
}

inline bool
trace_shot(Shot &shot, Map &map, fixed_array<Player, MAX_PLAYERS> &players, int8_t *hit_player_idx,
		   glm::vec3 *hit_point)

{

	fixed_array<OBB, MAX_OBSTACLES> &obstacles = map.obb_geometry;
	float							 closest_dist = shot.ray.length;
	*hit_player_idx = -1;

	for (OBB &box : obstacles)
	{
		RayHit hit;

		if (raycast_obb(shot.ray, box, &hit) && hit.distance < closest_dist)
		{
			closest_dist = hit.distance;
			*hit_point = hit.point;
			shot.ray.length = closest_dist;
		}
	}

	for (Player &player : players)
	{
		if (player.player_idx == shot.shooter_idx)
		{
			continue;
		}

		RayHit hit;

		if (raycast_sphere(shot.ray, player.position, PLAYER_RADIUS, &hit) && hit.distance < closest_dist)
		{
			closest_dist = hit.distance;
			*hit_player_idx = player.player_idx;
			*hit_point = hit.point;
			shot.ray.length = closest_dist;
			return true;
		}
	}

	return false;
}

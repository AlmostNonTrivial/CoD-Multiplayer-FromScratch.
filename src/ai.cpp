/*
 * Rule based AI driven client
 *
 * This file implements a basic agent interacting with the server in the same
 * way as a real player would, aka, via a network client.
 *
 * This feature is primarily for testing rather than for **its** own sake. Further,
 * no friends were willing to help.
 *
 * Using the map geometry, we generate some basic spatial data to give us a set of points
 * on the map that NPCs can walk between, helpers to determine which points are within line
 * of **sight** from a given point. And where, given a position and an **enemy's** position, would
 * provide cover.
 *
 * There is some duplicate client logic here, but in an effort to keep the client.cpp as streamlined
 * as possible for educational purposes I've accepted this.
 */

#include "ai.hpp"
#include "game_types.hpp"
#include "math.hpp"
#include "network_client.hpp"
#include "map.hpp"
#include "quantization.hpp"
#include "time.hpp"
#include <cstdio>
#include <glm/glm.hpp>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <thread>
#include <vector>

#define MAX_WAYPOINTS	 64
#define MAX_COVER_POINTS 64

#define MOVE_SPEED_SLOW	  0.5f
#define MOVE_SPEED_NORMAL 0.8f
#define MOVE_SPEED_FAST	  1.0f

#define DIST_WAYPOINT_REACHED 3.0f
#define DIST_WAYPOINT_MIN	  6.0f
#define DIST_ENGAGE_CLOSE	  7.0f
#define DIST_ENGAGE_FAR		  12.0f
#define DIST_SEARCH_RADIUS	  30.0f
#define DIST_COVER_REACHED	  2.0f

#define TIME_WANDER_MAX		 12.0f
#define TIME_STUCK_THRESHOLD 1.5f
#define TIME_SHOOT_BASE		 0.25f
#define TIME_SHOOT_RETREAT	 0.3f
#define TIME_SHOOT_VARIANCE	 0.4f

#define HEALTH_RETREAT_THRESHOLD  40.0f
#define HEALTH_RECOVER_MULTIPLIER 1.5f

#define AIM_ERROR_NONE	 0.0f
#define AIM_ERROR_SMALL	 0.25f
#define AIM_ERROR_MEDIUM 0.3f

#define MAP_WAYPOINT_SPACING 3.0f

#define COVER_MIN_HEIGHT	 2.0f
#define COVER_TANGENT_OFFSET 0.3f
#define COVER_PROTECTION_DOT -0.3f

#define CONNECT_WAIT_MS 10

#define COVER_STANDOFF_MULT	 1.5f
#define LOS_BUFFER_DIST		 1.0f
#define STUCK_MOVE_THRESHOLD 0.1f
#define ERROR_RANGE			 200
#define ERROR_DIVISOR		 100.0f

enum NPCState : uint8_t
{
	NPC_WANDER,
	NPC_ENGAGE,
	NPC_RETREAT
};

struct CoverPoint
{
	glm::vec3 position;
	glm::vec3 protected_direction;
};

struct SpatialData
{
	glm::vec3 waypoints[MAX_WAYPOINTS];
	uint32_t  waypoint_count;

	CoverPoint cover_points[MAX_COVER_POINTS];
	uint32_t   cover_count;
};

struct TargetInfo
{
	int8_t	  player_idx;
	glm::vec3 position;
	float	  distance;
};

static glm::vec3
apply_aim_error(glm::vec3 target_pos, float error_radius)
{
	float error_x = ((rand() % ERROR_RANGE - (ERROR_RANGE / 2)) / ERROR_DIVISOR) * error_radius;
	float error_y = ((rand() % ERROR_RANGE - (ERROR_RANGE / 2)) / ERROR_DIVISOR) * error_radius;
	float error_z = ((rand() % ERROR_RANGE - (ERROR_RANGE / 2)) / ERROR_DIVISOR) * error_radius;

	return target_pos + glm::vec3(error_x, error_y, error_z);
}

static void
calculate_aim_angles(glm::vec3 from_pos, glm::vec3 to_pos, float &yaw, float &pitch)
{
	glm::vec3 delta = to_pos - from_pos;
	yaw = atan2(delta.z, delta.x);
	pitch = atan2(delta.y, glm::length(glm::vec2(delta.x, delta.z)));
}

static bool
check_line_of_sight(glm::vec3 from_pos, glm::vec3 to_pos, fixed_array<OBB, MAX_OBSTACLES> &obbs)
{
	float dist = glm::length(to_pos - from_pos);
	Ray	  sight = {from_pos + glm::vec3(0, PLAYER_EYE_HEIGHT, 0), glm::normalize(to_pos - from_pos), dist};

	for (OBB &box : obbs)
	{
		RayHit hit;
		if (raycast_obb(sight, box, &hit) && hit.distance < dist - LOS_BUFFER_DIST)
		{
			return false;
		}
	}
	return true;
}

static float
generate_shoot_cooldown(bool is_retreating)
{
	float base_time = is_retreating ? TIME_SHOOT_RETREAT : TIME_SHOOT_BASE;
	float variance = (rand() % (int32_t)(TIME_SHOOT_VARIANCE * 1000)) / 1000.0f;
	return base_time + variance;
}

/*
 * Based on the map data, we generate additional data that the NPCs can use
 * to make decisions on a frame by frame basis.
 *
 * We can define points on the map that are (at least theoretically) reachable,
 * with mechansisms to determine things like 'does the NPC have line of sight of said waypoint'
 * Would a point grant cover from the enemy at a given position? etc
 */
static SpatialData
generate_spatial_data(Map &map)
{
	SpatialData data = {};

	/* Place points across the map where a player could be */
	for (float x = MAP_BOUNDS_MIN; x <= MAP_BOUNDS_MAX; x += MAP_WAYPOINT_SPACING)
	{
		for (float z = MAP_BOUNDS_MIN; z <= MAP_BOUNDS_MAX; z += MAP_WAYPOINT_SPACING)
		{
			if (data.waypoint_count >= MAX_WAYPOINTS)
			{
				break;
			}

			glm::vec3 pos(x, PLAYER_RADIUS + PLAYER_EYE_HEIGHT, z);
			if (is_intersecting_map(pos, map))
			{
				data.waypoints[data.waypoint_count++] = pos;
			}
		}
	}

	for (OBB &box : map.obb_geometry)
	{
		if (data.cover_count >= MAX_COVER_POINTS)
		{
			break;
		}

		glm::vec3 center = (box.center);
		glm::vec3 size = box.half_extents * 2.0f;

		if (size.y < COVER_MIN_HEIGHT)
		{
			continue;
		}

		struct
		{
			glm::vec3 normal;
			glm::vec3 tangent;
		} faces[] = {{{1, 0, 0}, {0, 0, 1}}, {{-1, 0, 0}, {0, 0, 1}}, {{0, 0, 1}, {1, 0, 0}}, {{0, 0, -1}, {1, 0, 0}}};

		for (auto &face : faces)
		{
			for (int32_t i = -1; i <= 1; i++)
			{
				if (data.cover_count >= MAX_COVER_POINTS)
				{
					break;
				}

				float	  t = i * COVER_TANGENT_OFFSET;
				glm::vec3 offset = face.tangent * t * glm::length(size * face.tangent);
				glm::vec3 face_center = center + face.normal * size * 0.5f;
				glm::vec3 sample_pos = face_center + offset;

				sample_pos += face.normal * (PLAYER_RADIUS * COVER_STANDOFF_MULT);
				sample_pos.y = PLAYER_RADIUS + PLAYER_EYE_HEIGHT;

				if (is_intersecting_map(sample_pos, map))
				{
					CoverPoint &cp = data.cover_points[data.cover_count++];
					cp.position = sample_pos;
					cp.protected_direction = face.normal;
				}
			}
		}
	}

	return data;
}

static int32_t
find_random_visible_waypoint(SpatialData &data, glm::vec3 from_pos, Map &map, float max_distance)
{
	int32_t visible[MAX_WAYPOINTS];
	int32_t visible_count = 0;

	for (uint32_t i = 0; i < data.waypoint_count; i++)
	{
		float dist = glm::length(data.waypoints[i] - from_pos);

		if (dist < DIST_WAYPOINT_MIN)
		{
			continue;
		}

		if (dist > max_distance)
		{
			continue;
		}

		if (has_line_of_sight(from_pos, data.waypoints[i], map))
		{
			visible[visible_count++] = i;
		}
	}

	if (visible_count == 0)
	{
		int32_t closest_idx = -1;
		float	closest_dist = 999999.0f;

		for (uint32_t i = 0; i < data.waypoint_count; i++)
		{
			float dist = glm::length(data.waypoints[i] - from_pos);
			if (dist > DIST_WAYPOINT_MIN && dist < closest_dist)
			{
				closest_dist = dist;
				closest_idx = i;
			}
		}

		return closest_idx;
	}

	int32_t selected = visible[rand() % visible_count];
	return selected;
}

static int32_t
find_best_cover(SpatialData &data, glm::vec3 from_pos, glm::vec3 threat_direction, Map &map)
{
	int32_t best = -1;
	float	best_score = -999999.0f;

	for (uint32_t i = 0; i < data.cover_count; i++)
	{
		CoverPoint &cp = data.cover_points[i];

		/* Would this block the threat? */
		if (glm::dot(cp.protected_direction, threat_direction) > COVER_PROTECTION_DOT)
		{
			continue;
		}

		if (!has_line_of_sight(from_pos, cp.position, map))
		{
			continue;
		}

		float dist = glm::length(cp.position - from_pos);
		float score = -dist;

		if (score > best_score)
		{
			best = i;
			best_score = score;
		}
	}

	return best;
}

static TargetInfo
find_closest_visible_enemy(fixed_array<Player, MAX_PLAYERS> &players, int8_t my_idx, glm::vec3 my_pos,
						   fixed_array<OBB, MAX_OBSTACLES> &obbs)
{
	TargetInfo target = {-1, glm::vec3(0), DIST_SEARCH_RADIUS};

	for (Player &p : players)
	{
		if (p.player_idx == my_idx || p.health == 0)
		{
			continue;
		}

		float dist = glm::length(p.position - my_pos);
		if (dist >= target.distance)
		{
			continue;
		}

		if (check_line_of_sight(my_pos, p.position, obbs))
		{
			target.player_idx = p.player_idx;
			target.position = p.position;
			target.distance = dist;
		}
	}

	return target;
}

/*
 * Interacts with the server the same way our user controlled client does, but
 * rather polling window input, the inputs are generated by the decision making.
 */
static void
run_npc(const char *server_ip, const char *npc_name, int32_t bind_port)
{
	NetworkClient network = {};
	if (!network_init(&network, nullptr, bind_port))
	{
		printf("NPC failed to initialize\n");
		return;
	}

	uint32_t server_peer_id = network_add_peer(&network, server_ip, SERVER_PORT);

	SendPacket<ConnectRequest> connect_req = {};
	connect_req.payload.type = MSG_CONNECT_REQUEST;
	strncpy(connect_req.payload.player_name, npc_name, 31);
	network_send_reliable(&network, server_peer_id, connect_req);

	int8_t	  my_idx = -1;
	glm::vec3 my_pos(0);
	glm::vec3 last_pos(0);
	uint8_t	  my_health = 100;
	float	  yaw = 0, pitch = 0;
	float	  shoot_cooldown = 0;
	float	  server_time = 0;
	uint32_t  input_seq = 0;

	NPCState state = NPC_WANDER;

	glm::vec3 target_position(0);
	bool	  has_target_position = false;
	float	  stuck_timer = 0;
	float	  state_timer = 0;

	Map								 map = generate_map();
	fixed_array<OBB, MAX_OBSTACLES>	 obbs = map.obb_geometry;
	fixed_array<Player, MAX_PLAYERS> players;

	SpatialData spatial = generate_spatial_data(map);

	while (1)
	{
		TimePoint frame_start = time_now();

		network_update(&network, TICK_TIME);

		Polled polled;
		while (network_poll(&network, polled))
		{
			if (polled.size < 1)
			{
				network_release_buffer(&network, polled.buffer_index);
				continue;
			}

			uint8_t msg_type = polled.buffer[0];

			if (msg_type == MSG_CONNECT_ACCEPT)
			{
				ConnectAccept *accept = (ConnectAccept *)polled.buffer;
				my_idx = accept->player_index;
				server_time = accept->server_time;
				printf("%s connected as player index %d\n", npc_name, my_idx);
			}
			else if (msg_type == MSG_SERVER_SNAPSHOT)
			{
				SnapshotMessage *snap = (SnapshotMessage *)polled.buffer;
				server_time = snap->server_time;

				players.clear();
				for (uint8_t i = 0; i < snap->player_count; i++)
				{
					Player p = dequantize(snap->players[i]);
					players.push(p);
					if (p.player_idx == my_idx)
					{
						my_pos = p.position;
						my_health = p.health;
					}
				}
			}

			network_release_buffer(&network, polled.buffer_index);
		}

		/* waiting to connect */
		if (my_idx < 0)
		{
			sleep_milliseconds(CONNECT_WAIT_MS);
			continue;
		}

		server_time += TICK_TIME;
		shoot_cooldown -= TICK_TIME;
		state_timer += TICK_TIME;

		float movement = glm::length(my_pos - last_pos);
		if (movement < STUCK_MOVE_THRESHOLD * TICK_RATE && has_target_position)
		{
			stuck_timer += TICK_RATE;
			if (stuck_timer > TIME_STUCK_THRESHOLD)
			{
				has_target_position = false;
				stuck_timer = 0;
			}
		}
		else
		{
			stuck_timer = 0;
		}
		last_pos = my_pos;

		TargetInfo target = find_closest_visible_enemy(players, my_idx, my_pos, obbs);

		NPCState new_state = state;

		if (target.player_idx >= 0)
		{
			if (my_health < HEALTH_RETREAT_THRESHOLD)
			{
				new_state = NPC_RETREAT;
			}
			else
			{
				new_state = NPC_ENGAGE;
			}
		}
		else
		{
			if (state == NPC_RETREAT && my_health > HEALTH_RETREAT_THRESHOLD * HEALTH_RECOVER_MULTIPLIER)
			{
				new_state = NPC_WANDER;
			}
			else if (state == NPC_ENGAGE)
			{
				new_state = NPC_WANDER;
			}
		}

		if (new_state != state)
		{
			state = new_state;
			state_timer = 0;
			has_target_position = false;

			if (state == NPC_RETREAT && target.player_idx >= 0)
			{
				glm::vec3 threat_dir = glm::normalize(target.position - my_pos);
				int32_t	  cover_idx = find_best_cover(spatial, my_pos, threat_dir, map);

				if (cover_idx >= 0)
				{
					target_position = spatial.cover_points[cover_idx].position;
					has_target_position = true;
					printf("%s retreating to cover\n", npc_name);
				}
			}
		}

		float	move_x = 0;
		float	move_z = 0;
		uint8_t buttons = 0;

		/*
		 * Simple state machine, 'retreat' might be superfluous here
		 */

		switch (state)
		{
		case NPC_WANDER: {
			if (!has_target_position || state_timer > TIME_WANDER_MAX)
			{
				int32_t wp = find_random_visible_waypoint(spatial, my_pos, map, DIST_SEARCH_RADIUS);
				if (wp >= 0)
				{
					target_position = spatial.waypoints[wp];
					has_target_position = true;
					state_timer = 0;
				}
			}

			if (has_target_position)
			{
				glm::vec3 to_target = target_position - my_pos;
				float	  dist = glm::length(to_target);

				if (dist < DIST_WAYPOINT_REACHED)
				{
					has_target_position = false;
				}
				else
				{
					glm::vec3 aim_point = apply_aim_error(target_position, AIM_ERROR_NONE);
					calculate_aim_angles(my_pos, aim_point, yaw, pitch);
					move_z = -MOVE_SPEED_NORMAL;
				}
			}
			break;
		}

		case NPC_ENGAGE: {
			glm::vec3 aim_point = apply_aim_error(target.position, AIM_ERROR_SMALL);
			calculate_aim_angles(my_pos, aim_point, yaw, pitch);

			if (target.distance > DIST_ENGAGE_FAR)
			{
				move_z = -MOVE_SPEED_FAST;
			}
			else if (target.distance < DIST_ENGAGE_CLOSE)
			{
				move_z = MOVE_SPEED_FAST;
			}
			else
			{
				move_x = (rand() % 2 == 0) ? MOVE_SPEED_SLOW : -MOVE_SPEED_SLOW;
			}

			if (shoot_cooldown <= 0)
			{
				buttons |= 1;
				shoot_cooldown = generate_shoot_cooldown(false);
			}
			break;
		}

		case NPC_RETREAT: {
			if (has_target_position)
			{
				glm::vec3 to_cover = target_position - my_pos;
				float	  dist = glm::length(to_cover);

				if (dist < DIST_COVER_REACHED)
				{
					move_z = 0;
				}
				else
				{
					glm::vec3 aim_point = apply_aim_error(target_position, AIM_ERROR_NONE);
					calculate_aim_angles(my_pos, aim_point, yaw, pitch);
					move_z = -MOVE_SPEED_FAST;
				}
			}

			if (target.player_idx >= 0)
			{
				glm::vec3 aim_point = apply_aim_error(target.position, AIM_ERROR_MEDIUM);
				calculate_aim_angles(my_pos, aim_point, yaw, pitch);

				if (shoot_cooldown <= 0)
				{
					buttons |= 1;
					shoot_cooldown = generate_shoot_cooldown(true);
				}
			}
			break;
		}
		}

		SendPacket<InputMessage> input = {};
		input.payload.type = MSG_CLIENT_INPUT;
		input.payload.sequence_num = input_seq++;
		input.payload.move_x = move_x;
		input.payload.move_z = move_z;
		input.payload.look_yaw = yaw;
		input.payload.look_pitch = pitch;
		input.payload.buttons = buttons;
		input.payload.shot_time = (buttons & 1) ? server_time : 0;
		input.payload.time = server_time;

		network_send_unreliable(&network, server_peer_id, input);
		float frame_time = time_elapsed_seconds(frame_start);
		float sleep_time = TICK_TIME - frame_time;

		if (sleep_time > 0.001f)
		{
			sleep_seconds(sleep_time);
		}
	}

	network_shutdown(&network);
}

void
ai_run_npcs(const char *server_ip, const char *base_name, int32_t count)
{
	std::vector<std::thread> threads;
	threads.reserve(count);

	for (int32_t i = 0; i < count; i++)
	{
		char npc_name[64];
		snprintf(npc_name, sizeof(npc_name), "%s_%d", base_name, i);

		threads.emplace_back([server_ip, npc_name]() { run_npc(server_ip, npc_name, 0); });
	}

	printf("Waiting for %zu NPC threads \n", threads.size());

	for (auto &thread : threads)
	{
		if (thread.joinable())
		{
			thread.join();
		}
	}
}

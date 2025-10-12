/*
 * Key game types shared between client and server.
 *
 * Client and Server share types because they both need to maintain the same state
 * (for example, they both need the map geometry) and they need to share networking types.
 */

#pragma once

#include "containers.hpp"
#include <glm/glm.hpp>
#include "math.hpp"
#include <cstdint>

#define SERVER_PORT 7777

#define TICK_RATE		60.0f
#define TICK_TIME		(1.0f / TICK_RATE)
#define SNAPSHOT_COUNT	32
#define MAX_PLAYERS		10
#define MAX_SHOTS		16
#define MAX_OBSTACLES	256
#define MAX_JUMPS		2
#define MAX_SHOOT_RANGE 100.0f

#define INPUT_BUTTON_SHOOT 0x01
#define INPUT_BUTTON_JUMP  0x02

#define PLAYER_RADIUS	  1.0f
#define PLAYER_EYE_HEIGHT 0.5f

struct Player
{
	int8_t	  player_idx; /* -1 = inactive */
	uint32_t  last_processed_seq;
	glm::vec3 position;
	glm::vec3 velocity;
	float	  yaw, pitch;
	bool	  on_ground;
	int8_t	  health;
	bool	  wall_running;
	glm::vec3 wall_normal;
	int16_t	  wall_index;	   /* Which wall we're on */
	uint8_t	  jumps_remaining; /* for double jump */

	bool
	active()
	{
		return player_idx != -1;
	}
	bool
	alive()
	{
		return health > 0;
	}
};

struct Shot
{
	int8_t	 shooter_idx;
	Ray		 ray;
	uint32_t spawn_time;
};

/*
 * The recorded state of each player at a given time, only the server
 * creates these, but both client and server store the most recent N snapshots
 */
struct Snapshot
{
	float							 timestamp;
	fixed_array<Player, MAX_PLAYERS> players;
};

enum MessageType : uint8_t
{
	/* unreliable*/
	MSG_SERVER_SNAPSHOT = 1,
	MSG_CLIENT_INPUT,
	/* reliable */
	MSG_PLAYER_LEFT,
	MSG_PLAYER_DIED,
	MSG_CONNECT_REQUEST,
	MSG_CONNECT_ACCEPT,
};

#pragma pack(push, 1)

struct ConnectRequest
{
	uint8_t type;
	char	player_name[32];
};

struct ConnectAccept
{
	uint8_t type;
	float	server_time;
	int8_t	player_index;
};

struct InputMessage
{
	uint8_t	 type;
	uint32_t sequence_num;
	float	 move_x, move_z;
	float	 look_yaw, look_pitch;
	uint8_t	 buttons;
	float	 shot_time;
	float	 time;
};

struct QuantizedPlayer
{
	int8_t	 player_idx;
	int16_t	 pos_x, pos_y, pos_z;
	int8_t	 vel_x, vel_y, vel_z;
	uint8_t	 yaw;
	int8_t	 pitch;
	uint8_t	 health;
	uint8_t	 flags;
	uint32_t last_processed_seq;
};
struct QuantizedShot
{
	int8_t	shooter_idx;
	int16_t origin_x, origin_y, origin_z;
	int8_t	dir_x, dir_y, dir_z;
	uint8_t length;
};

struct SnapshotMessage
{
	uint8_t			type;
	float			server_time;
	uint8_t			player_count;
	uint8_t			shot_count;
	QuantizedPlayer players[MAX_PLAYERS];
	QuantizedShot	shots[MAX_SHOTS];
};

struct PlayerLeftEvent
{
	uint8_t type;
	int8_t	player_idx;
};

struct PlayerKilledEvent
{
	uint8_t type;
	int8_t	killer_idx;
	int8_t	killed_idx;
};

#pragma pack(pop)

inline ConnectAccept
make_connect_accept(uint32_t client_id, float server_time, int8_t player_index)
{
	ConnectAccept msg = {};
	msg.type = MSG_CONNECT_ACCEPT;
	msg.server_time = server_time;
	msg.player_index = player_index;
	return msg;
}

inline PlayerKilledEvent
make_kill_event(int8_t killer_idx, int8_t killed_idx)
{
	return {MSG_PLAYER_DIED, killer_idx, killed_idx};
}

inline PlayerLeftEvent
make_leave_event(int8_t player_idx)
{
	return {MSG_PLAYER_LEFT, player_idx};
}

inline InputMessage
make_input_message(uint32_t seq, float move_x, float move_z, float yaw, float pitch, uint8_t buttons,
				   float shot_time = 0)
{
	InputMessage msg = {};
	msg.type = MSG_CLIENT_INPUT;
	msg.sequence_num = seq;
	msg.move_x = move_x;
	msg.move_z = move_z;
	msg.look_yaw = yaw;
	msg.look_pitch = pitch;
	msg.buttons = buttons;
	msg.shot_time = shot_time;
	return msg;
}

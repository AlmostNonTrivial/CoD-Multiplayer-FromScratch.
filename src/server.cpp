/*
 * The server is where the authoritative game update occurs, with a period
 * snapshot being broadcast to all players
 */
#include "server.hpp"
#include "containers.hpp"
#include "game_types.hpp"
#include "map.hpp"
#include "network_client.hpp"
#include "physics.hpp"
#include "profiler.hpp"
#include "quantization.hpp"
#include "time.hpp"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <glm/glm.hpp>
#include "server_extended.hpp"

#define SNAPSHOT_RATE  20.0f
#define CLIENT_TIMEOUT 5.0f
#define HISTORY_SIZE   64
#define RESPAWN_TIME   1.5f

#define SNAPSHOT_TIME			(1.0f / SNAPSHOT_RATE)
#define NETWORK_UPDATE_INTERVAL (1.0f / 60.0f * 6.0f)
#define MAX_DELTA_TIME			0.1f

#define RESPAWN_INTERVAL TICK_TIME / 20

#define BULLET_DAMAGE	  10
#define STARTING_HEALTH	  100
#define INPUT_BUFFER_SIZE 12
#define MAP_GEOMETRY_SIZE 256
#define LOOP_SLEEP_MS	  1

struct ClientConnection
{
	/*
	 * Buffer the inputs, some might arrive out of order or bunched
	 */
	fixed_queue<InputMessage, INPUT_BUFFER_SIZE> input_buffer;
	/*
	 *  Server: 'This is the last input I have processed, and here is your position'
	 *  Client: 'Okay, here + all the inputs you haven't processed yet is where I predict I am'
	 */
	uint32_t		 last_processed;
	fixed_string<32> player_name;
	uint32_t		 peer_id; /* 0 = inactive slot */

	bool
	active()
	{
		return peer_id != 0;
	}
};

struct Respawn
{
	int8_t player_index;
	float  respawn_time;
};

static struct
{
	NetworkClient network;
	Map			  map;
	/*
	 * Accumulated for each snapshot
	 */
	fixed_array<Shot, MAX_SHOTS> new_shots;
	float						 time;
	TimePoint					 start_time;
	/*
	 * History for doing lag-compensated shots,
	 * when player 1 shot, it was at time x, where was everyone at x?
	 *
	 * This makes it fair for everyone despite variations in latency
	 */
	ring_buffer<Snapshot, HISTORY_SIZE>		   history;
	Snapshot								   frame;
	fixed_queue<Respawn, MAX_PLAYERS>		   dead_players;
	fixed_array<ClientConnection, MAX_PLAYERS> clients;
} SERVER = {};

Player *
get_player(int8_t player_idx)
{
	assert(player_idx >= 0 && player_idx < MAX_PLAYERS);
	Player *p = &SERVER.frame.players[player_idx];
	return p;
}

ClientConnection *
get_client(int8_t player_idx)
{
	assert(player_idx >= 0 && player_idx < MAX_PLAYERS);
	ClientConnection *c = &SERVER.clients[player_idx];
	return c;
}

int8_t
find_player_index_for_peer(uint32_t peer_id)
{
	for (int8_t i = 0; i < MAX_PLAYERS; i++)
	{
		if (SERVER.clients[i].peer_id == peer_id)
		{
			return i;
		}
	}
	return -1;
}

float
get_time()
{
	return time_elapsed_seconds(SERVER.start_time);
}

bool
history_get_frame_at_time(float time, Snapshot **out)
{
	for (size_t i = SERVER.history.size(); i > 0; i--)
	{
		Snapshot *frame = SERVER.history.at(i - 1);
		if (frame->timestamp <= time)
		{
			*out = frame;
			return true;
		}
	}
	return false;
}

void
update_respawns(float current_time)
{

	while (!SERVER.dead_players.empty())
	{
		Respawn *respawn = SERVER.dead_players.front();

		/* A queue, so we can exit */
		if (respawn->respawn_time > current_time)
		{
			break;
		}

		Player *entity = get_player(respawn->player_index);
		if (entity)
		{
			entity->position = get_spawn_point(SERVER.map);
			entity->health = STARTING_HEALTH;
			printf("Respawned player %d\n", respawn->player_index);
		}

		SERVER.dead_players.pop();
	}
}

void
perform_lag_compensated_shot(Player *shooter, int8_t shooter_idx, float shot_time)
{
	/*
	 * We could get the exact position by interpolating, but because each shot has a different
	 * time step, we'd have to calculate it anew for each shot.
	 *
	 * This test will be the least accurate when a player is moving at high speed in a single
	 * direction.
	 */
	Snapshot *historical;
	if (!history_get_frame_at_time(shot_time, &historical))
	{
		historical = &SERVER.frame;
	}

	Player *historical_shooter = &historical->players[shooter_idx];

	if (!historical_shooter->active())
	{
		return;
	}

	Shot	  shot = create_shot(historical_shooter);
	glm::vec3 hit_point;
	int8_t	  hit_player = -1;

	trace_shot(shot, SERVER.map, SERVER.frame.players, &hit_player, &hit_point);

	SERVER.new_shots.push(shot);

	if (hit_player == -1)
	{
		return;
	}

	Player *target = get_player(hit_player);
	target->health = std::max(target->health - BULLET_DAMAGE, 0);

	if (target->alive())
	{
		return;
	}

	Respawn respawn = {.player_index = hit_player, .respawn_time = SERVER.time + RESPAWN_TIME};
	SERVER.dead_players.push(respawn);

	SendPacket<PlayerKilledEvent> evt = {.payload = make_kill_event(shooter_idx, hit_player)};

	for (int8_t i = 0; i < MAX_PLAYERS; i++)
	{
		if (SERVER.clients[i].active())
		{
			network_send_reliable(&SERVER.network, SERVER.clients[i].peer_id, evt);
		}
	}
}

void
tick(float dt)
{

	for (int8_t player_idx = 0; player_idx < MAX_PLAYERS; player_idx++)
	{
		ClientConnection *client = get_client(player_idx);
		if (!client->active())
		{
			continue;
		}

		Player *entity = get_player(player_idx);
		if (!entity->alive())
		{
			continue;
		}

		/*
		 * Network conditions might mean we have 0 inputs one frame
		 * and 2 the next. Only processing ones with a larger sequence number
		 * stops this buffer from processing stale data.
		 */
		while (client->input_buffer.size())
		{
			auto input = client->input_buffer.pop();

			if (input->sequence_num <= client->last_processed)
			{
				continue;
			}

			client->last_processed = input->sequence_num;

			if ((input->buttons & INPUT_BUTTON_SHOOT))
			{
				perform_lag_compensated_shot(entity, player_idx, input->shot_time);
			}

			apply_player_input(entity, input, dt);
			apply_player_physics(entity, SERVER.map, SERVER.frame.players, dt);
		}
	}

	SERVER.frame.timestamp = get_time();
	SERVER.history.push(SERVER.frame);
}

void
remove_client(uint32_t peer_id)
{
	int8_t player_idx = find_player_index_for_peer(peer_id);
	if (player_idx < 0)
	{
		return;
	}

	memset(&SERVER.clients[player_idx], 0, sizeof(ClientConnection));

	Player *p = &SERVER.frame.players[player_idx];
	p->player_idx = -1;
	p->health = 0;

	SendPacket<PlayerLeftEvent> event = {.payload = make_leave_event(player_idx)};
	for (int8_t i = 0; i < MAX_PLAYERS; i++)
	{
		if (SERVER.clients[i].active())
		{
			network_send_reliable(&SERVER.network, SERVER.clients[i].peer_id, event);
		}
	}

	printf("Player %d disconnected (peer_id: %u)\n", player_idx, peer_id);
}

void
handle_connect_request(uint32_t peer_id, ConnectRequest *req)
{
	if (find_player_index_for_peer(peer_id) >= 0)
	{
		return;
	}

	int8_t player_idx = find_player_index_for_peer(0); /*  find free */
	if (player_idx < 0)
	{
		printf("No free player slots\n");
		return;
	}

	ClientConnection *client = &SERVER.clients[player_idx];
	client->peer_id = peer_id;
	client->last_processed = 0;
	client->player_name.set(req->player_name);

	Player *entity = &SERVER.frame.players[player_idx];
	memset(entity, 0, sizeof(Player));
	entity->player_idx = player_idx;
	entity->position = get_spawn_point(SERVER.map);
	entity->health = STARTING_HEALTH;

	printf("Player %d connected (peer_id: %u, name: %s)\n", player_idx, peer_id, req->player_name);

	SendPacket<ConnectAccept> msg = {.payload = make_connect_accept(peer_id, get_time(), player_idx)};

	network_send_reliable(&SERVER.network, peer_id, msg);
}

void
handle_client_input(int8_t player_idx, InputMessage *input)
{
	ClientConnection *client = get_client(player_idx);
	if (!client->active())
	{
		return;
	}

	Player *player = get_player(player_idx);

	client->input_buffer.push(*input);
}

void
server_process_packets()
{
	Polled polled;

	while (network_poll(&SERVER.network, polled))
	{

		assert(polled.size >= 1);
		uint8_t msg_type = polled.buffer[0];

		switch (msg_type)
		{
		case MSG_CONNECT_REQUEST:
			handle_connect_request(polled.from, (ConnectRequest *)polled.buffer);
			break;

		case MSG_CLIENT_INPUT: {
			int8_t player_idx = find_player_index_for_peer(polled.from);
			if (player_idx >= 0)
			{
				handle_client_input(player_idx, (InputMessage *)polled.buffer);
			}
			break;
		}
		default:
			assert(false && "Unhandled Message\n");
		}

		network_release_buffer(&SERVER.network, polled.buffer_index);
	}
}

void
broadcast_snapshot()
{
	SendPacket<SnapshotMessage> msg = {};
	msg.payload.type = MSG_SERVER_SNAPSHOT;
	msg.payload.server_time = get_time();
	msg.payload.player_count = 0;

	for (int8_t i = 0; i < MAX_PLAYERS; i++)
	{
		Player *entity = &SERVER.frame.players[i];
		if (!entity->active())
		{
			continue;
		}

		ClientConnection *client = get_client(i);
		if (client)
		{
			entity->last_processed_seq = client->last_processed;
		}

		msg.payload.players[msg.payload.player_count++] = quantize(*entity);
	}

	msg.payload.shot_count = std::min((uint32_t)MAX_SHOTS, SERVER.new_shots.size());
	for (uint8_t i = 0; i < msg.payload.shot_count; i++)
	{
		msg.payload.shots[i] = quantize(SERVER.new_shots[i]);
	}

	for (int8_t i = 0; i < MAX_PLAYERS; i++)
	{
		if (SERVER.clients[i].peer_id != 0)
		{
			network_send_unreliable(&SERVER.network, SERVER.clients[i].peer_id, msg);
		}
	}

	SERVER.new_shots.clear();
}
void
server_loop()
{
	Profiler profiler;
	profiler_init(&profiler);

	float update_accumulator = 0.0f;
	float respawn_accumulator = 0.0f;
	float snapshot_accumulator = 0.0f;

	while (1)
	{
		profiler_begin_frame(&profiler);
		TimePoint frame_start = time_now();
		SERVER.time += TICK_TIME;

		{
			PROFILE_ZONE(&profiler, "process_packets");
			server_process_packets();
			PROFILE_ZONE_END(profiler);
		}

		{
			PROFILE_ZONE(&profiler, "simulation_tick");
			tick(TICK_TIME);
			PROFILE_ZONE_END(profiler);
		}

		snapshot_accumulator += TICK_TIME;
		if (snapshot_accumulator >= SNAPSHOT_TIME)
		{
			PROFILE_ZONE(&profiler, "broadcast_snapshot");
			broadcast_snapshot();
			snapshot_accumulator = 0.0f;
			PROFILE_ZONE_END(profiler);
		}

		update_accumulator += TICK_TIME;
		if (update_accumulator >= NETWORK_UPDATE_INTERVAL)
		{
			PROFILE_ZONE(&profiler, "network_update");
			network_update(&SERVER.network, update_accumulator);
			update_accumulator = 0.0f;
			PROFILE_ZONE_END(profiler);
		}

		respawn_accumulator += TICK_TIME;
		if (respawn_accumulator >= RESPAWN_INTERVAL)
		{
			update_respawns(SERVER.time);
			respawn_accumulator = 0.0f;
		}

		if (profiler.frame_count % 300 == 0)
		{
			profiler_print_report(&profiler);
			profiler_reset_stats(&profiler);
		}
		float frame_time = time_elapsed_seconds(frame_start);
		float sleep_time = TICK_TIME - frame_time;
		if (sleep_time > 0.001f)
		{
			sleep_seconds(sleep_time);
		}
	}
}

bool
add_unrecognised(sockaddr_in address)
{
	return network_add_peer(&SERVER.network, address) != 0;
}

void
run_server()
{

	for (int8_t i = 0; i < MAX_PLAYERS; i++)
	{
		Player p = {};
		p.player_idx = -1;
		SERVER.frame.players.push(p);
	}

	if (!network_init(&SERVER.network, "0.0.0.0", SERVER_PORT))
	{
		printf("Failed to initialize network on port %u\n", SERVER_PORT);
		return;
	}

	SERVER.map = generate_map();
	SERVER.start_time = time_now();

	SERVER.network.on_peer_removed = remove_client;
	SERVER.network.on_unrecognised = add_unrecognised;

	printf("Started on port %u\n", SERVER_PORT);

	server_loop();

	network_shutdown(&SERVER.network);
	printf("Shutdown complete\n");
}

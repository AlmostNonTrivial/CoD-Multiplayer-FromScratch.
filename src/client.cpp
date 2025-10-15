/*
 * FPS Networked Client
 *
 * After initialization, within the game loop the client will:
 * Gather the user's input
 * Send it to the server (every frame)
 * Apply the input using functions shared between client and server
 * Store the input in a queue
 * Interpolate between two snapshots of the game state it has received from the server
 * Render
 *
 * When it receives snapshots from the server it will:
 * Add them to a rolling history
 * Resolve where the user's player is given the inputs the user has processed
 *
 * The main idea is that the poll input, update, render pattern in a typical
 * game loop is split such that the update occurs on the server. The input and updates
 * have to be sent between client and server respectively.
 *
 * Because of bandwidth constraints, there are fewer snapshots arriving than the framerate, so
 * the client must render some time in the past, and interpolate between snapshots for the game to
 * look smooth.
 *
 * Because of latency, waiting for snapshots to drive the player would subject the player to 50~200ms
 * latency, which feels unbearable, so we run the simulation on the client, just for our
 * player using the inputs we send to the server.
 *
 */
#include "client.hpp"
#include "client_extended.hpp"
#include "containers.hpp"
#include "game_types.hpp"
#include "map.hpp"
#include "math.hpp"
#include "network_client.hpp"
#include "physics.hpp"
#include "profiler.hpp"
#include "quantization.hpp"
#include "renderer.hpp"
#include "time.hpp"
#include "window.hpp"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

#define TIME_SYNC_LARGE_CORRECTION 0.1f

#define MIN_DELAY	 0.02f
#define MAX_DELAY	 0.15f
#define IDEAL_BUFFER 0.05f

#define TIME_CORRECTION_LARGE	4.0f
#define TIME_CORRECTION_MEDIUM	1.0f
#define INTERP_TRANSITION_SPEED 2.0f

#define NETWORK_UPDATE_TIMER 0.0f

#define TELEPORT_THRESHOLD 10.0f

/*
 * before player 1: pos(0,0,0)
 * after  player 1: pos(0,0,1)
 * t = 0.9
 *
 * rendered player 1: pos(0,0,0.9)
 */
struct InterpolatedSnapshot
{
	Snapshot *before;
	Snapshot *after;
	float	  t;
};

static struct
{

	NetworkClient net;
	uint32_t	  server_peer_id;
	/*
	 * The index of this player in the snapshot QuantizedPlayers[] that comes back,
	 * this doesn't change for the player whilst they're connected
	 */
	int8_t player_idx;
	bool   connected;
	/*
	 * Both client and server will have a copy of this in
	 * order to do physics
	 */
	Map map;
	/*
	 * When we connect, we set our time as the server time and incremented it with
	 * dt, so our server time will be roughly half the rtt behind the servers actual time
	 */
	float server_time;
	/*
	 * We keep the last n snapshots and render some time before
	 * the server_time, which allows us to interpolate between
	 * snapshots over several frames.
	 */
	ring_buffer<Snapshot, SNAPSHOT_COUNT> snapshots;
	/*
	 * The render time is the server_time minus some variable delay.
	 * We can't render at the actual server_time, because we don't have the snapshots
	 * to interpolate.
	 *
	 * How far back from the server time should we render?
	 * We don't want to run out of snapshots before more arrive.
	 * So we dynamically assess how many snapshots we have in excess
	 * The more we have, then by extension the faster the RTT, and the closer
	 * to the server time we can render
	 */
	float render_time;
	float target_delay;
	float current_delay;

	/*
	 * Save the last n inputs, each with an incrementing sequence number,
	 * The server will back the sequence number of the last input it processed,
	 * Take that position, then reapply all the inputs after that sequence, the player
	 * should be in the same position
	 */
	ring_buffer<InputMessage, 64> input_history;
	uint32_t					  input_sequence; /* increments per input aka per frame */
	Player						  local_player;

	Window	 window;
	Renderer renderer;

	ClientRenderState				 visuals;
	fixed_array<Player, MAX_PLAYERS> frame;

} CLIENT;

Player *
find_local(fixed_array<Player, MAX_PLAYERS> *players)
{
	if (CLIENT.player_idx < 0 && CLIENT.player_idx >= MAX_PLAYERS)
	{
		return nullptr;
	}
	return &players->data[CLIENT.player_idx];
}
Player *
find_local(Snapshot *snapshot)
{
	return find_local(&snapshot->players);
}

void
process_connect_accept(ConnectAccept *msg)
{
	CLIENT.player_idx = msg->player_index;
	CLIENT.server_time = msg->server_time;
	CLIENT.connected = true;
	CLIENT.map = generate_map();

	printf("Connected player index: %d\n", CLIENT.player_idx);
}

void
process_snapshot(SnapshotMessage *snap)
{
	/* ideally 0.0f, but will drift over time */
	float time_diff = snap->server_time = CLIENT.server_time;
	if (std::abs(time_diff) > TIME_SYNC_LARGE_CORRECTION)
	{
		CLIENT.server_time = snap->server_time;
	}

	Snapshot snapshot = {.timestamp = snap->server_time};

	for (int32_t i = 0; i < snap->player_count; i++)
	{
		snapshot.players.push(dequantize(snap->players[i]));
	}

	CLIENT.snapshots.push(snapshot);

	Player *local = find_local(&snapshot);

	if (!local)
	{
		return;
	}

	/* Not sent over*/
	bool wall_running = CLIENT.local_player.wall_running;
	bool is_grounded = CLIENT.local_player.on_ground;
	auto wall_normal = CLIENT.local_player.wall_normal;
	auto wall_index = CLIENT.local_player.wall_index;

	glm::vec3 predicted_position = CLIENT.local_player.position;
	Player	  corrected_state = *local;

	uint32_t replayed = 0;

	for (uint32_t i = 0; i < CLIENT.input_history.size(); i++)
	{
		InputMessage *input = CLIENT.input_history.at(i);

		/*
		 * For every input that the server has not processed, reapply, hopefully
		 * we're in the same position
		 */
		if (input->sequence_num > local->last_processed_seq)
		{
			apply_player_input(&corrected_state, input, TICK_TIME);
			apply_player_physics(&corrected_state, CLIENT.map, CLIENT.snapshots.end().data->players, TICK_TIME);
			replayed++;
		}
	}

	float error = glm::length(predicted_position - corrected_state.position);
	/*
	 * Not every field is sent over, the ones that aern't are recalculated ad hoc
	 */

	CLIENT.local_player = corrected_state;

	CLIENT.local_player.on_ground = is_grounded;
	CLIENT.local_player.wall_running = wall_running;
	CLIENT.local_player.wall_normal = wall_normal;
	CLIENT.local_player.wall_index = wall_index;

	if (error >= 0.4f)
	{
		printf("Correction error: %.3f, replayed %u/%zu inputs\n", error, replayed, CLIENT.input_history.size());
	}

	for (uint8_t i = 0; i < snap->shot_count; i++)
	{
		Shot shot = dequantize(snap->shots[i]);
		/* To our client, the rays are just visuals, so we can adjust them */
		if (shot.shooter_idx == CLIENT.player_idx)
		{
			shot.ray.origin = calculate_gun_position(CLIENT.visuals.camera, CLIENT.visuals.gun);
		}

		add_shot_trail(&CLIENT.visuals.effects, shot, CLIENT.server_time);
	}
}

void
client_process_packets()
{
	/*
	 * Gets pointer to internal packet pool,
	 * so call release_buffer(buffer_index) when done
	 *
	 * Only polls packets with explicitly accepted peer
	 */
	Polled polled;
	while (network_poll(&CLIENT.net, polled))
	{
		assert(polled.size >= 1);
		uint8_t msg_type = polled.buffer[0];

		switch (msg_type)
		{
		case MSG_SERVER_SNAPSHOT:
			process_snapshot((SnapshotMessage *)polled.buffer);
			break;
		case MSG_PLAYER_DIED: {
			PlayerKilledEvent *event = (PlayerKilledEvent *)polled.buffer;
			ui_add_kill(&CLIENT.visuals.ui, event->killer_idx, event->killed_idx, CLIENT.server_time);
			break;
		}
		case MSG_PLAYER_LEFT: {
			PlayerLeftEvent *e = (PlayerLeftEvent *)polled.buffer;
			ui_add_player_left(&CLIENT.visuals.ui, e->player_idx, CLIENT.server_time);
			break;
		}
		case MSG_CONNECT_ACCEPT:
			process_connect_accept((ConnectAccept *)polled.buffer);
			break;
		default:
			assert(false && "Unhandled Message\n");
		}

		/* We've done what we need to with the packet data, release it for reuse */
		network_release_buffer(&CLIENT.net, polled.buffer_index);
	}
}

InterpolatedSnapshot
get_interpolated_snapshot(float render_time)
{
	InterpolatedSnapshot				   result = {};
	ring_buffer<Snapshot, SNAPSHOT_COUNT> &snapshots = CLIENT.snapshots;

	if (snapshots.size() < 2)
	{
		return result;
	}

	for (uint32_t i = 0; i < snapshots.size() - 1; i++)
	{
		Snapshot *current = snapshots.at(i);
		Snapshot *next = snapshots.at(i + 1);

		if (next->timestamp >= render_time && current->timestamp <= render_time)
		{
			/*
			 * Find two snapshots for before and after our render_time (server_time - some time),
			 * such that we can calculate t which will increment frame by frame.
			 *
			 * the two snapshots selected will be the same for several frames, with t growing from
			 *  > 0, to < 1, then the snapshots will change, with the next becoming current if
			 * everything is running smoothly.
			 *
			 */
			result.before = current;
			result.after = next;

			float duration = next->timestamp - current->timestamp;
			if (duration > 0.001f)
			{
				result.t = glm::clamp((render_time - current->timestamp) / duration, 0.0f, 1.0f);
			}
			break;
		}
	}

	return result;
}

void
set_interpolated_players(InterpolatedSnapshot *interp)
{
	CLIENT.frame = {};
	if (!interp->after || !interp->after)
	{
		return;
	}

	for (uint32_t i = 0; i < interp->before->players.size(); i++)
	{
		Player *before = &interp->before->players[i];
		Player *after = &interp->after->players[i];

		if (!before->active() || !after->active())
		{
			continue;
		}

		if (before->player_idx != after->player_idx)
		{
			continue;
		}

		Player interpolated = {};
		float  t = interp->t;

		interpolated.player_idx = before->player_idx;
		/*
		 * The idea is that snapshots are frequent enough that interpolating
		 * between them doesn't actually paint a false picture of what happened.
		 *
		 * What meaningful change in position could there be in a fraction of a second
		 * that can't be abridged by a single directional change?
		 */

		/*
		 * To interpolating between death and respawn locations
		 */
		float position_delta = glm::length(after->position - before->position);
		if (position_delta > TELEPORT_THRESHOLD || before->health == 0 || after->health > before->health)
		{
			interpolated.position = after->position;
			interpolated.velocity = after->velocity;
			interpolated.yaw = after->yaw;
			interpolated.pitch = after->pitch;
		}
		else
		{
			interpolated.position = glm::mix(before->position, after->position, t);
			interpolated.velocity = glm::mix(before->velocity, after->velocity, t);

			float yaw_diff = after->yaw - before->yaw;
			if (yaw_diff > M_PI)
			{
				yaw_diff -= 2 * M_PI;
			}
			if (yaw_diff < -M_PI)
			{
				yaw_diff += 2 * M_PI;
			}
			interpolated.yaw = before->yaw + yaw_diff * t;

			interpolated.pitch = glm::mix(before->pitch, after->pitch, t);
		}

		interpolated.health = after->health;
		interpolated.on_ground = after->on_ground;

		CLIENT.frame.push(interpolated);
	}
}
void
update_render_time()
{

	if (CLIENT.snapshots.size() < 2)
	{
		return;
	}

	/*
	 * How much 'future' do we have buffered.
	 * If we have lots, it means network conditions are good,
	 * and we can set our render time closer to the server time
	 * because we're confident the snapshots will arrive reliably
	 *
	 * If we don't have a lot of 'future' buffered, we want to set the
	 * render time further in the past so we have time for packets to arrive.
	 *
	 * Here the future_buffer acts as an indicator for network quality
	 */

	float newest_time = CLIENT.snapshots.back()->timestamp;
	float future_buffer = newest_time - CLIENT.render_time;

	if (future_buffer < MIN_DELAY)
	{
		CLIENT.target_delay += 0.01f;
	}
	else if (future_buffer > MAX_DELAY)
	{
		CLIENT.target_delay -= 0.01f;
	}

	CLIENT.target_delay = glm::clamp(CLIENT.target_delay, MIN_DELAY, MAX_DELAY);
}

void
sync_render_time(float dt)
{
	CLIENT.render_time += dt;

	float delay_diff = CLIENT.target_delay - CLIENT.current_delay;
	CLIENT.current_delay += delay_diff * INTERP_TRANSITION_SPEED * dt;

	float target_render_time = CLIENT.server_time - CLIENT.current_delay;
	float error = target_render_time - CLIENT.render_time;

	if (fabs(error) > 1.0f)
	{
		CLIENT.render_time = target_render_time;
	}
	else if (fabs(error) > 0.001f)
	{
		float correction_speed = (fabs(error) > 0.1f) ? 4.0f : 1.0f;
		CLIENT.render_time += error * correction_speed * dt;
	}
}

void
apply_input(float move_x, float move_z, uint8_t buttons)
{
	SendPacket<InputMessage> input;
	input.payload.type = MSG_CLIENT_INPUT;
	input.payload.sequence_num = CLIENT.input_sequence++;
	input.payload.move_x = move_x;
	input.payload.move_z = move_z;
	input.payload.look_yaw = CLIENT.visuals.camera.yaw;
	input.payload.look_pitch = CLIENT.visuals.camera.pitch;
	input.payload.buttons = buttons;
	input.payload.time = CLIENT.render_time;

	/*
	 * Older games would buffer inputs and send them in batches 1-4 frames,
	 * the main problem with this is if a packet is lost you're more likely to
	 * feel it.
	 */
	network_send_unreliable(&CLIENT.net, CLIENT.server_peer_id, input);

	/* Sent to server, but immediately apply it */
	CLIENT.input_history.push(input.payload);

	/* Functions shared with the server */
	apply_player_input(&CLIENT.local_player, &input.payload, TICK_TIME);
	apply_player_physics(&CLIENT.local_player, CLIENT.map, CLIENT.snapshots.end().data->players, TICK_TIME);
}

void
update(float dt)
{
	sync_render_time(dt);
	ui_update(&CLIENT.visuals.ui, CLIENT.server_time);
	CLIENT.server_time += dt;

	PlayerInput input = gather_player_input(&CLIENT.window);

	if (input.unlock_cursor)
	{
		window_set_cursor_lock(&CLIENT.window, false);
	}

	apply_input(input.move_x, input.move_z, input.buttons);
	client_process_packets();

	update_camera(&CLIENT.visuals.camera, CLIENT.local_player.position, input.mouse_dx, input.mouse_dy, input.move_x,
				  dt, CLIENT.local_player.wall_running, CLIENT.local_player.wall_normal);

	bool is_moving = input.move_x != 0 || input.move_z != 0;
	bool shooting = input.buttons & INPUT_BUTTON_SHOOT;
	update_gun_animation(&CLIENT.visuals.gun, input.mouse_dx, input.mouse_dy, shooting, is_moving, dt);

	update_visual_effects(&CLIENT.visuals.effects, CLIENT.server_time);

	InterpolatedSnapshot snap = get_interpolated_snapshot(CLIENT.render_time);
	set_interpolated_players(&snap);
}
void
render()
{
	renderer_begin_frame(&CLIENT.renderer);
	if (!CLIENT.local_player.alive())
	{
		render_death_screen(&CLIENT.renderer);
	}
	else
	{
		renderer_update_text_projection(&CLIENT.renderer);
		render_setup_camera(&CLIENT.renderer, CLIENT.visuals.camera);
		render_space_skybox(&CLIENT.renderer);
		render_world(&CLIENT.renderer, CLIENT.map);
		render_entities(&CLIENT.renderer, CLIENT.frame, CLIENT.player_idx);
		render_shot_trails(&CLIENT.renderer, CLIENT.visuals.effects, CLIENT.server_time);
		render_first_person_gun(&CLIENT.renderer, CLIENT.visuals.camera, CLIENT.visuals.gun);
	}

	renderer_end_frame(&CLIENT.renderer);
	render_ui(&CLIENT.renderer, CLIENT.visuals.ui, CLIENT.server_time);
}

bool
init(const char *server_ip, const char *player_name, int port)
{
	CLIENT.player_idx = -1;

	render_state_init(&CLIENT.visuals);

	CLIENT.target_delay = 0.1f;
	CLIENT.current_delay = 0.1f;
	CLIENT.render_time = CLIENT.server_time - CLIENT.current_delay;

	window_set_cursor_lock(&CLIENT.window, true);
	window_set_mouse_sensitivity(&CLIENT.window, 1.0f);

	if (!network_init(&CLIENT.net, nullptr, port))
	{
		printf("Failed to initialize network\n");
		return false;
	}

	CLIENT.server_peer_id = network_add_peer(&CLIENT.net, server_ip, 7777);

	SendPacket<ConnectRequest> req = {};
	req.payload.type = MSG_CONNECT_REQUEST;
	strncpy(req.payload.player_name, player_name, 31);
	printf("Connecting to %s:7777 \n", server_ip);
	network_send_reliable(&CLIENT.net, CLIENT.server_peer_id, req);

	TimePoint start_time = time_now();
	while (!CLIENT.connected)
	{
		client_process_packets();

		if (time_elapsed_seconds(start_time) > 5.0f)
		{
			printf("Connection timeout\n");
			return false;
		}

		sleep_milliseconds(10);
	}

	return true;
}

void
shutdown()
{
	network_shutdown(&CLIENT.net);
}

void
run_client(const char *server_ip, const char *player_name, int port, int x, int y, int width, int height)
{
	if (!window_init(&CLIENT.window, width, height, "Game Client"))
	{
		printf("Failed to initialize window\n");
		return;
	}

	window_set_position(&CLIENT.window, x, y);

	if (!renderer_init(&CLIENT.renderer, window_get_handle(&CLIENT.window)))
	{
		printf("Failed to initialize renderer\n");
		window_shutdown(&CLIENT.window);
		return;
	}

	renderer_set_light(&CLIENT.renderer, glm::vec3(0, 20, 0), glm::vec3(1, 1, 1), 1.0f);

	if (!init(server_ip, player_name, port))
	{
		printf("Failed to connect to server\n");
		renderer_shutdown(&CLIENT.renderer);
		window_shutdown(&CLIENT.window);
		return;
	}

	float adjustment_timer = 0.0f;

	Profiler profiler;
	profiler_init(&profiler);

	while (!window_should_close(&CLIENT.window))
	{
		profiler_begin_frame(&profiler);

		TimePoint frame_start = time_now();

		window_begin_frame(&CLIENT.window);
		window_poll_events(&CLIENT.window);

		adjustment_timer += TICK_TIME;
		if (adjustment_timer > NETWORK_UPDATE_TIMER)
		{
			update_render_time();
			network_update(&CLIENT.net, adjustment_timer);
			adjustment_timer = 0.0f;
		}
		{
			PROFILE_ZONE(&profiler, "Update");
			update(TICK_TIME);
			PROFILE_ZONE_END(profiler);
		}
		{
			PROFILE_ZONE(&profiler, "Rendering");
			render();
			PROFILE_ZONE_END(profiler);
		}

		window_swap_buffers(&CLIENT.window);

		if (window_key(&CLIENT.window, GLFW_KEY_ESCAPE))
		{
			break;
		}

		float frame_time = time_elapsed_seconds(frame_start);
		float sleep_time = TICK_TIME - frame_time;

		if (profiler.frame_count % 300 == 0)
		{
			profiler_print_report(&profiler);
			profiler_reset_stats(&profiler);
		}
		if (sleep_time > 0.001f)
		{
			sleep_seconds(sleep_time);
		}
	}

	shutdown();
	renderer_shutdown(&CLIENT.renderer);
	window_shutdown(&CLIENT.window);
}

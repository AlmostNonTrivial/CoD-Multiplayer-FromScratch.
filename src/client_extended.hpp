/*
 * In the attempt to keep the client.cpp streamlined, any functionality the nature of which
 * is trivially inferred by the function name and/or merely a visual effect totally unrelated
 * to the networking has been dumped here.
 * It's header only explicitly because this is just an extension of the client.cpp
 */

#include "renderer.hpp"
#include "game_types.hpp"
#include "map.hpp"
#include "window.hpp"
#include <cstdint>
#include <cstring>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#define UI_EVENT_LIFETIME	3.0f
#define SHOT_TRAIL_LIFETIME 1.0f

struct CameraState
{
	glm::vec3 position;
	glm::vec3 forward;
	float	  yaw;
	float	  pitch;
	float	  roll;
	float	  target_roll;
	float	  shake_intensity;
	float	  shake_time;
	float	  fov;
	float	  target_fov;
};

struct PlayerInput
{
	float	move_x, move_z;
	float	move_y;
	float	mouse_dx, mouse_dy;
	uint8_t buttons;
	bool	toggle_free_camera;
	bool	toggle_prediction;
	bool	unlock_cursor;
};

struct GunVisuals
{
	float recoil_offset;
	float sway_x, sway_y;
	float bob_phase;
	float last_shot_time;
	float gun_fov;
};

struct UIEvent
{
	char  text[128];
	float spawn_time;
};

struct UIState
{
	ring_buffer<UIEvent, 8> events;
};

struct RenderEffects
{
	fixed_queue<Shot, 128> shot_trails;
};

struct ClientRenderState
{
	CameraState	  camera;
	GunVisuals	  gun;
	UIState		  ui;
	RenderEffects effects;
};

inline void
render_state_init(ClientRenderState *state)
{
	*state = {};

	state->camera.fov = 75.0f;
	state->camera.target_fov = 75.0f;
	state->camera.position = glm::vec3(0, 5, 10);
	state->camera.forward = glm::vec3(0, 0, -1);

	state->gun.gun_fov = 50.0f;
}

inline void
update_camera(CameraState *cam, glm::vec3 &player_pos, float mouse_dx, float mouse_dy, float move_x, float dt,
			  bool wall_running, glm::vec3 wall_normal)
{

	cam->yaw += mouse_dx * 0.002f;
	cam->pitch = glm::clamp(cam->pitch - mouse_dy * 0.002f, -1.5f, 1.5f);

	cam->forward = glm::vec3(cos(cam->yaw) * cos(cam->pitch), sin(cam->pitch), sin(cam->yaw) * cos(cam->pitch));

	if (wall_running)
	{
		glm::vec3 camera_right = glm::cross(cam->forward, glm::vec3(0, 1, 0));
		float	  side = glm::dot(wall_normal, camera_right);
		cam->target_roll = side > 0 ? 0.4f : -0.4f;
	}
	else
	{
		cam->target_roll = move_x * 0.08f;
	}

	cam->roll += (cam->target_roll - cam->roll) * 8.0f * dt;

	if (cam->shake_intensity > 0)
	{
		cam->shake_intensity = glm::max(0.0f, cam->shake_intensity - dt * 3.0f);
	}

	cam->fov += (cam->target_fov - cam->fov) * 10.0f * dt;

	cam->position = player_pos + glm::vec3(0, 1.5f, 0);

	if (cam->shake_intensity > 0)
	{
		float shake = cam->shake_intensity * 0.1f;
		cam->position.x += (rand() % 100 - 50) * 0.01f * shake;
		cam->position.y += (rand() % 100 - 50) * 0.01f * shake;
	}
}

inline void
update_gun_animation(GunVisuals *gun, float mouse_dx, float mouse_dy, bool shooting, bool moving, float dt)
{
	if (gun->recoil_offset > 0)
	{
		gun->recoil_offset = glm::max(0.0f, gun->recoil_offset - dt * 8.0f);
	}

	gun->sway_x += (-mouse_dx * 0.5f - gun->sway_x) * dt * 10.0f;
	gun->sway_y += (mouse_dy * 0.5f - gun->sway_y) * dt * 10.0f;

	gun->sway_x = glm::clamp(gun->sway_x, -1.0f, 1.0f);
	gun->sway_y = glm::clamp(gun->sway_y, -1.0f, 1.0f);

	if (moving)
	{
		gun->bob_phase += dt * 8.0f;
	}

	if (shooting)
	{
		gun->recoil_offset = 1.0f;
		gun->last_shot_time = 0;
	}
}

inline void
update_visual_effects(RenderEffects *fx, float current_time)
{
	while (!fx->shot_trails.empty())
	{
		Shot *oldest = fx->shot_trails.front();
		if (current_time - oldest->spawn_time <= SHOT_TRAIL_LIFETIME)
		{
			break;
		}
		fx->shot_trails.pop();
	}
}

inline void
ui_update(UIState *ui, float current_time)
{
	while (!ui->events.empty())
	{
		UIEvent *oldest = ui->events.front();
		if (current_time - oldest->spawn_time <= UI_EVENT_LIFETIME)
		{
			break;
		}
		ui->events.pop();
	}
}

inline void
ui_add_event(UIState *ui, const char *text, float time)
{
	UIEvent event;
	strncpy(event.text, text, sizeof(event.text) - 1);
	event.text[sizeof(event.text) - 1] = '\0';
	event.spawn_time = time;
	ui->events.push(event);
}

inline void
ui_add_kill(UIState *ui, int8_t killer_idx, int8_t killed_idx, float time)
{
	char event_text[128];
	if (killer_idx == killed_idx)
	{
		snprintf(event_text, sizeof(event_text), "Player %d died", killed_idx);
	}
	else
	{
		snprintf(event_text, sizeof(event_text), "Player %d killed Player %d", killer_idx, killed_idx);
	}

	ui_add_event(ui, event_text, time);
}

inline void
ui_add_player_left(UIState *ui, int8_t player_idx, float time)
{
	char event_text[128];
	snprintf(event_text, sizeof(event_text), "Player %d left", player_idx);
	ui_add_event(ui, event_text, time);
}

inline void
add_shot_trail(RenderEffects *fx, Shot &shot, float time)
{
	shot.spawn_time = time;
	fx->shot_trails.push(shot);
}

inline glm::vec3
calculate_gun_position(CameraState &cam, GunVisuals &gun)
{
	glm::vec3 gun_offset(0.3f, -0.2f, 0.5f);
	gun_offset.z -= gun.recoil_offset * 0.1f;
	gun_offset.x += gun.sway_x * 0.02f + sin(gun.bob_phase) * 0.01f;
	gun_offset.y += gun.sway_y * 0.02f + abs(cos(gun.bob_phase)) * 0.01f;

	glm::vec3 camera_right = glm::cross(cam.forward, glm::vec3(0, 1, 0));
	glm::vec3 camera_up = glm::cross(camera_right, cam.forward);

	return cam.position + camera_right * gun_offset.x + camera_up * gun_offset.y + cam.forward * gun_offset.z;
}

inline void
render_setup_camera(Renderer *r, CameraState &cam)
{
	glm::vec3 target = cam.position + cam.forward;

	glm::vec3 world_up(0, 1, 0);
	glm::vec3 camera_right = glm::normalize(glm::cross(cam.forward, world_up));
	glm::vec3 camera_up = world_up * cos(cam.roll) + camera_right * sin(cam.roll);

	r->camera.position = cam.position;
	r->camera.target = target;
	r->camera.up = camera_up;
	r->camera.fov = cam.fov;

	r->view_matrix = glm::lookAt(cam.position, target, camera_up);
	r->proj_matrix = glm::perspective(glm::radians(r->camera.fov), (float)r->width / (float)r->height,
									  r->camera.near_plane, r->camera.far_plane);
}

inline void
render_world(Renderer *r, Map &map)
{
	for (OBB &obb : map.obb_geometry)
	{
		glm::vec3 euler = glm::eulerAngles(obb.rotation);
		glm::vec3 size = obb.half_extents * 2.0f;
		renderer_draw_cube(r, obb.center, euler, size, glm::vec4(0.5f, 0.5f, 0.6f, 1.0f));
	}
}

inline void
render_entities(Renderer *r, fixed_array<Player, MAX_PLAYERS> &players, int8_t local_idx)
{
	for (Player &player : players)
	{
		if (player.player_idx == local_idx)
		{
			continue;
		}

		glm::vec4 color;
		if (player.health > 80)
		{
			color = glm::vec4(0.2f, 0.8f, 0.2f, 1.0f);
		}
		else if (player.health > 40)
		{
			color = glm::vec4(0.5f, 0.5f, 0.1f, 1.0f);
		}
		else if (player.health > 0)
		{
			color = glm::vec4(0.8f, 0.2f, 0.2f, 1.0f);
		}
		else
		{
			color = glm::vec4(0.5f, 0.5f, 0.5f, 1.0f);
		}

		renderer_draw_sphere(r, player.position, 1.0f, color);
	}
}

static inline glm::mat4
make_camera_aligned_transform(glm::vec3 position, glm::vec3 right, glm::vec3 up, glm::vec3 forward)
{
	glm::mat4 transform = glm::mat4(1.0f);
	transform[0] = glm::vec4(right, 0.0f);
	transform[1] = glm::vec4(up, 0.0f);
	transform[2] = glm::vec4(forward, 0.0f);
	transform[3] = glm::vec4(position, 1.0f);
	return transform;
}

inline void
render_first_person_gun(Renderer *r, CameraState &cam, GunVisuals &gun)
{

	constexpr float GUN_LENGTH = 0.6f;
	constexpr float GUN_WIDTH = 0.12f;
	constexpr float GUN_HEIGHT = 0.15f;

	glm::vec3 gun_position = calculate_gun_position(cam, gun);
	glm::vec3 camera_right = glm::cross(cam.forward, glm::vec3(0, 1, 0));
	glm::vec3 camera_up = glm::cross(camera_right, cam.forward);

	glm::mat4 gun_transform = make_camera_aligned_transform(gun_position, camera_right, camera_up, cam.forward);
	gun_transform = gun_transform * glm::scale(glm::mat4(1.0f), glm::vec3(GUN_WIDTH, GUN_HEIGHT, GUN_LENGTH));
	renderer_draw_mesh(r, &r->meshes[MESH_CUBE], gun_transform, glm::vec4(0.25f, 0.25f, 0.28f, 1.0f));

	glm::vec3 grip_pos = gun_position - camera_up * (GUN_HEIGHT * 0.8f) - cam.forward * (GUN_LENGTH * 0.2f);
	glm::mat4 grip_transform = make_camera_aligned_transform(grip_pos, camera_right, camera_up, cam.forward);
	grip_transform =
		grip_transform * glm::scale(glm::mat4(1.0f), glm::vec3(GUN_WIDTH * 0.8f, GUN_HEIGHT * 0.6f, GUN_LENGTH * 0.3f));
	renderer_draw_mesh(r, &r->meshes[MESH_CUBE], grip_transform, glm::vec4(0.15f, 0.12f, 0.1f, 1.0f));
}

inline void
render_shot_trails(Renderer *r, RenderEffects &fx, float current_time)
{
	for (Shot &shot : fx.shot_trails)
	{
		float age = current_time - shot.spawn_time;
		float alpha = 1.0f - (age / SHOT_TRAIL_LIFETIME);
		renderer_draw_ray(r, shot.ray.origin, shot.ray.direction, shot.ray.length, glm::vec4(1, 0, 0, alpha));
	}
}

inline void
render_ui(Renderer *r, UIState &ui, float current_time)
{
	text_batch_begin(r);

	for (size_t i = 0; i < ui.events.size(); i++)
	{
		UIEvent &event = *ui.events.at(i);
		float	 age = current_time - event.spawn_time;
		float	 alpha = (age > 2.0f) ? (1.0f - ((age - 2.0f) / 1.0f)) : 1.0f;
		text_batch_add_string(r, event.text, 25.0f, 100.0f + i * 25.0f, 0.5f, glm::vec3(1.0f, 1.0f, alpha));
	}

	text_batch_flush(r);
}

inline void
render_death_screen(Renderer *renderer)
{
	renderer_update_text_projection(renderer);
	float center_x = renderer->width * 0.5f - 150.0f;
	float center_y = renderer->height * 0.5f;
	text_batch_begin(renderer);
	text_batch_add_string(renderer, "YOU DIED", center_x, center_y + 50, 1.2f, glm::vec3(0.8f, 0.1f, 0.1f));
	text_batch_add_string(renderer, "Respawning...", center_x + 30, center_y - 20, 0.7f, glm::vec3(0.7f, 0.7f, 0.7f));
	text_batch_flush(renderer);
}

inline PlayerInput
gather_player_input(Window *w)
{
	PlayerInput input = {};
	if (window_key(w, GLFW_KEY_A))
	{
		input.move_x -= 1.0f;
	}
	if (window_key(w, GLFW_KEY_D))
	{
		input.move_x += 1.0f;
	}
	if (window_key(w, GLFW_KEY_W))
	{
		input.move_z -= 1.0f;
	}
	if (window_key(w, GLFW_KEY_S))
	{
		input.move_z += 1.0f;
	}
	if (window_key(w, GLFW_KEY_SPACE))
	{
		input.move_y += 1.0f;
	}
	if (window_key(w, GLFW_KEY_LEFT_SHIFT))
	{
		input.move_y -= 1.0f;
	}
	float mag = sqrt(input.move_x * input.move_x + input.move_z * input.move_z);
	if (mag > 1.0f)
	{
		input.move_x /= mag;
		input.move_z /= mag;
	}
	window_get_mouse_delta(w, &input.mouse_dx, &input.mouse_dy);
	if (window_mouse_button(w, GLFW_MOUSE_BUTTON_LEFT))
	{
		input.buttons |= INPUT_BUTTON_SHOOT;
	}
	if (window_key_pressed(w, GLFW_KEY_SPACE))
	{
		input.buttons |= INPUT_BUTTON_JUMP;
	}
	input.toggle_free_camera = window_key_pressed(w, GLFW_KEY_F);
	input.toggle_prediction = window_key_pressed(w, GLFW_KEY_P);
	input.unlock_cursor = window_key(w, GLFW_KEY_L);
	return input;
}

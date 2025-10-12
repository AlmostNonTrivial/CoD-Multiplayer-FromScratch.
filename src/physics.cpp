/*
 * Physics that is shared between between client and server
 *
 * FPS multiplayer games usually don't have very advanced physics, with movement coming from
 * velocity based integration rather than using higher derivatives, and the only thing really modelled
 * is the projectile motion of a grenade, and gravity.
 *
 * So the job of physics is basic and fast collision checking and resolution rather than simulating dynamics.
 * Things that move of their own accord, like an NPC helicopter will do so by moving along a defined path.
 */
#include "containers.hpp"
#include <cstdint>
#include <glm/glm.hpp>
#include <cmath>
#include "game_types.hpp"
#include "map.hpp"
#include "math.hpp"
#include "physics.hpp"

#define GRAVITY				 20.0f
#define MOVE_SPEED			 80.0f
#define JUMP_VELOCITY		 14.0f
#define DOUBLE_JUMP_VELOCITY 14.0f
#define GROUND_SPEED		 25.0f
#define GROUND_ACCEL		 35.0f

#define WALLRUN_MIN_SPEED 15.0f
#define WALLRUN_SPEED	  22.0f
#define WALLRUN_GRAVITY	  0.0f
#define WALLRUN_JUMP_OUT  15.0f
#define WALLRUN_JUMP_UP	  10.0f

void
apply_player_input(Player *player, InputMessage *input, float dt)
{
	player->yaw = input->look_yaw;
	player->pitch = input->look_pitch;

	glm::vec3 forward(cos(player->yaw), 0, sin(player->yaw));
	glm::vec3 right(-forward.z, 0, forward.x);
	glm::vec3 move = forward * (-input->move_z) + right * input->move_x;

	if (glm::length(move) > 0.001f)
	{
		move = glm::normalize(move);
	}

	if (player->wall_running)
	{

		glm::vec3 current_dir = glm::vec3(player->velocity.x, 0, player->velocity.z);
		float	  speed = glm::length(current_dir);

		if (speed > 0.1f)
		{
			current_dir = glm::normalize(current_dir);

			player->velocity.x = current_dir.x * WALLRUN_SPEED;
			player->velocity.z = current_dir.z * WALLRUN_SPEED;
		}

		player->velocity.x += move.x * 2.0f;
		player->velocity.z += move.z * 2.0f;
	}
	else
	{
		glm::vec3 target_vel = move * GROUND_SPEED;
		glm::vec3 vel_diff = target_vel - glm::vec3(player->velocity.x, 0, player->velocity.z);

		player->velocity.x += vel_diff.x * GROUND_ACCEL * dt;
		player->velocity.z += vel_diff.z * GROUND_ACCEL * dt;
	}

	if ((input->buttons & INPUT_BUTTON_JUMP))
	{
		if (player->on_ground)
		{
			player->velocity.y = JUMP_VELOCITY;
			player->jumps_remaining = MAX_JUMPS - 1;
		}
		else if (player->wall_running)
		{

			player->velocity = player->wall_normal * WALLRUN_JUMP_OUT;
			player->velocity.y = WALLRUN_JUMP_UP;

			player->wall_running = false;

			player->jumps_remaining = MAX_JUMPS - 1;
		}
		else if (player->jumps_remaining > 0)
		{

			player->velocity.y = DOUBLE_JUMP_VELOCITY;
			player->jumps_remaining--;
		}
	}
}

static bool
is_wall_surface(glm::vec3 normal)
{
	return fabs(normal.y) < 0.3f;
}


void
apply_player_physics(Player *player, Map &map, fixed_array<Player, MAX_PLAYERS> &all_players, float dt)
{
	if (player->position.y <= PLAYER_RADIUS)
	{
		player->position.y = PLAYER_RADIUS;
		player->on_ground = true;
		player->jumps_remaining = MAX_JUMPS;
		player->wall_running = false;

		if (player->velocity.y < 0)
		{
			player->velocity.y = 0;
		}
	}
	else
	{
		if (player->wall_running)
		{
			player->on_ground = false;
			player->velocity.y = 0;
		}
		else
		{
			player->on_ground = false;
			player->velocity.y -= GRAVITY * dt;
		}
	}

	fixed_array<OBB, MAX_OBSTACLES> &obstacles = map.obb_geometry;

	if (player->wall_running)
	{
		assert(player->wall_index != -1 && "If wallrunning, wall index should be set");

		/*
		* We determine if we're still wallrunning if we're colliding with the given wall.
		* Because of the collision resolution this can cause the wall_running to ocillate
		* between attaching and detaching. A simple fix expand our players radius that we do
		* this check with.
		*/
		float expanded_radius = PLAYER_RADIUS * 1.2;
		Sphere	current_sphere = {player->position, expanded_radius};
		OBB	   &box = *obstacles.get(player->wall_index);
		Contact contact;
		if (!sphere_vs_obb(current_sphere, box, &contact))
		{
			player->wall_running = false;
		}
	}

	glm::vec3 movement = player->velocity * dt;
	glm::vec3 new_position = player->position;

	glm::vec3 axes[3] = {{movement.x, 0, 0}, {0, 0, movement.z}, {0, movement.y, 0}};
	int		  vel_indices[3] = {0, 2, 1};

	for (int i = 0; i < 3; i++)
	{
		glm::vec3 test_pos = new_position + axes[i];
		Sphere	  test_sphere = {test_pos, PLAYER_RADIUS};

		bool collided = false;

		Contact collision_contact = {};
		int32_t index = -1;
		for (OBB &box : obstacles)
		{
			index++;
			Contact contact;
			if (!sphere_vs_obb(test_sphere, box, &contact))
			{
				continue;
			}

			collided = true;
			collision_contact = contact;

			if (!player->on_ground && !player->wall_running && is_wall_surface(contact.normal))
			{
				glm::vec2 horiz_vel(player->velocity.x, player->velocity.z);
				float	  horiz_speed = glm::length(horiz_vel);

				if (horiz_speed < WALLRUN_MIN_SPEED)
				{
					continue;
				}

				player->wall_running = true;
				player->wall_index = index;
				player->wall_normal = contact.normal;
				player->velocity.y = 0.0f;

				player->jumps_remaining = MAX_JUMPS;

				glm::vec2 wall_normal_2d(player->wall_normal.x, player->wall_normal.z);
				float	  into_wall = glm::dot(horiz_vel, wall_normal_2d);
				glm::vec2 along_wall = horiz_vel - wall_normal_2d * into_wall;

				if (glm::length(along_wall) > 0.1f)
				{
					along_wall = glm::normalize(along_wall) * WALLRUN_SPEED;
				}
				else
				{

					glm::vec3 up(0, 1, 0);
					glm::vec3 wall_right = glm::cross(up, player->wall_normal);

					if (glm::dot(glm::vec3(horiz_vel.x, 0, horiz_vel.y), wall_right) < 0)
					{
						wall_right = -wall_right;
					}
					along_wall = glm::vec2(wall_right.x, wall_right.z) * WALLRUN_SPEED;
				}

				player->velocity.x = along_wall.x;
				player->velocity.z = along_wall.y;
			}
		}

		if (!collided)
		{
			new_position = test_pos;
		}
		else
		{

			bool is_walkable = collision_contact.normal.y > 0.25f;

			if (is_walkable && i < 2)
			{
				float	  axis_length = glm::length(axes[i]);
				glm::vec3 move_dir = axes[i] / axis_length;
				float	  into_surface = glm::dot(move_dir, collision_contact.normal);

				if (into_surface < 0)
				{

					glm::vec3 projected = (move_dir - collision_contact.normal * into_surface) * axis_length;

					glm::vec3 slope_test_pos = new_position + projected;
					Sphere	  slope_test_sphere = {slope_test_pos, PLAYER_RADIUS};

					bool	slope_blocked = false;
					Contact contact;
					for (OBB &box : obstacles)
					{
						if (sphere_vs_obb(slope_test_sphere, box, &contact))
						{
							slope_blocked = true;
							break;
						}
					}

					if (!slope_blocked)
					{
						new_position = slope_test_pos;
						collided = false;
					}
				}
			}

			if (collided)
			{
				player->velocity[vel_indices[i]] = 0;

				if (i == 2 && movement.y < 0)
				{
					player->on_ground = true;
					player->wall_running = false;
				}
			}
		}
	}

	player->position = new_position;

	Sphere s1 = {player->position, PLAYER_RADIUS};

	for (Player &other : all_players)
	{
		if (other.player_idx == player->player_idx)
		{
			continue;
		}

		Sphere	s2 = {other.position, PLAYER_RADIUS};
		Contact contact;

		if (sphere_vs_sphere(s1, s2, &contact))
		{
			player->position -= contact.normal * contact.depth;
		}
	}
}

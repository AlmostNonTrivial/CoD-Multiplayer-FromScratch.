#pragma once

#include "containers.hpp"
#include "game_types.hpp"
#include "map.hpp"
#include <glm/glm.hpp>




void
apply_player_input(Player *player, InputMessage *input, float dt);

bool
check_collision_at(glm::vec3 position, fixed_array<OBB, MAX_OBSTACLES> &obstacles);

void
apply_player_physics(Player *player, Map &map, fixed_array<Player, MAX_PLAYERS> &all_players, float dt);

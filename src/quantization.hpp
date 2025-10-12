#pragma once
#include "game_types.hpp"

QuantizedPlayer
quantize(Player &e);

Player
dequantize(QuantizedPlayer &q);

QuantizedShot
quantize(Shot &shot);

Shot
dequantize(QuantizedShot &q);

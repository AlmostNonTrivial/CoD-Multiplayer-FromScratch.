/*
 * Snapshot packet compression via lossy integer encoding.
 *
 * We sacrifice precision to save bandwidth without noticeable quality loss. The dominant
 * source of visual error is (by a significant margin) latency/jitter.
 * Position compressed from float (4 bytes) to int16_t (2 bytes) by scaling by 500, which
 * gives ±65m range at 2mm precision, which isn't noticeable.
 *
 * We could go A LOT further on space savings, either by quantizing input packets, or by implementing
 * delta encoding. The latter is where each snapshot no longer has the full game state, but only what has
 * changed from the last snapshot, Requiring change tracking and stateful decoding
 * on both sides.
 *
 * To clarify, quantization doesn't make any difference whatsoever here, but bandwidth reduction is
 * something that proper systems do, so it's worth implementing at least a lite version.
 */

#include "quantization.hpp"
QuantizedPlayer
quantize(Player &e)
{
	QuantizedPlayer q;
	q.player_idx = e.player_idx;
	q.last_processed_seq = e.last_processed_seq;

	q.pos_x = (int16_t)glm::clamp(e.position.x * 500.0f, -32768.0f, 32767.0f);
	q.pos_y = (int16_t)glm::clamp(e.position.y * 500.0f, -32768.0f, 32767.0f);
	q.pos_z = (int16_t)glm::clamp(e.position.z * 500.0f, -32768.0f, 32767.0f);

	q.vel_x = (int8_t)glm::clamp(e.velocity.x * 10.0f, -128.0f, 127.0f);
	q.vel_y = (int8_t)glm::clamp(e.velocity.y * 10.0f, -128.0f, 127.0f);
	q.vel_z = (int8_t)glm::clamp(e.velocity.z * 10.0f, -128.0f, 127.0f);

	float normalized_yaw = e.yaw / (2.0f * M_PI);
	normalized_yaw = normalized_yaw - floor(normalized_yaw);
	q.yaw = (uint8_t)(normalized_yaw * 255.0f);

	q.pitch = (int8_t)glm::clamp((float)(e.pitch * (128.0f / M_PI)), -128.0f, 127.0f);

	q.health = e.health;
	q.flags = (e.on_ground ? 0x01 : 0) | (e.wall_running ? 0x02 : 0) | ((e.jumps_remaining & 0x03) << 2);

	return q;
}

Player
dequantize(QuantizedPlayer &q)
{
	Player e = {};
	e.player_idx = q.player_idx;
	e.last_processed_seq = q.last_processed_seq;

	e.position.x = q.pos_x * 0.002f;
	e.position.y = q.pos_y * 0.002f;
	e.position.z = q.pos_z * 0.002f;

	e.velocity.x = q.vel_x * 0.1f;
	e.velocity.y = q.vel_y * 0.1f;
	e.velocity.z = q.vel_z * 0.1f;

	e.yaw = q.yaw * (2.0f * M_PI / 255.0f);
	e.pitch = q.pitch * (M_PI / 128.0f);

	e.health = q.health;
	e.on_ground = (q.flags & 0x01) != 0;
	e.wall_running = (q.flags & 0x02) != 0;
	e.jumps_remaining = (q.flags >> 2) & 0x03;

	return e;
}

QuantizedShot
quantize(Shot &shot)
{
	QuantizedShot q;
	q.shooter_idx = shot.shooter_idx;

	q.origin_x = (int16_t)glm::clamp(shot.ray.origin.x * 500.0f, -32768.0f, 32767.0f);
	q.origin_y = (int16_t)glm::clamp(shot.ray.origin.y * 500.0f, -32768.0f, 32767.0f);
	q.origin_z = (int16_t)glm::clamp(shot.ray.origin.z * 500.0f, -32768.0f, 32767.0f);

	glm::vec3 norm_dir = glm::normalize(shot.ray.direction);
	q.dir_x = (int8_t)(norm_dir.x * 127.0f);
	q.dir_y = (int8_t)(norm_dir.y * 127.0f);
	q.dir_z = (int8_t)(norm_dir.z * 127.0f);

	q.length = (uint8_t)glm::clamp(shot.ray.length, 0.0f, 255.0f);

	return q;
}

Shot
dequantize(QuantizedShot &q)
{
	Shot shot;
	shot.shooter_idx = q.shooter_idx;

	shot.ray.origin.x = q.origin_x * 0.002f;
	shot.ray.origin.y = q.origin_y * 0.002f;
	shot.ray.origin.z = q.origin_z * 0.002f;

	glm::vec3 dir;
	dir.x = q.dir_x / 127.0f;
	dir.y = q.dir_y / 127.0f;
	dir.z = q.dir_z / 127.0f;
	shot.ray.direction = glm::normalize(dir);

	shot.ray.length = (float)q.length;

	return shot;
}

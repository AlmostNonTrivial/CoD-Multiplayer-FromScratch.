#pragma once

#include <bitset>
#include <cstdint>
#include <stdbool.h>
#include "../lib/glad.h"
#include <GLFW/glfw3.h>

struct GLFWwindow;

#define INPUT_MAX_KEYS			512
#define INPUT_MAX_MOUSE_BUTTONS 8

struct Window
{

	GLFWwindow *handle;
	uint32_t	width;
	uint32_t	height;
	const char *title;

	std::bitset<INPUT_MAX_KEYS> keys;
	std::bitset<INPUT_MAX_KEYS> keys_pressed;
	std::bitset<INPUT_MAX_KEYS> keys_released;

	uint8_t mouse_buttons;
	uint8_t mouse_buttons_pressed;
	uint8_t mouse_buttons_released;

	float mouse_x;
	float mouse_y;
	float mouse_dx;
	float mouse_dy;
	float last_mouse_x;
	float last_mouse_y;
	bool  first_mouse;

	float scroll_x;
	float scroll_y;

	bool  cursor_locked;
	float mouse_sensitivity;
};

bool
window_init(Window *w, uint32_t width, uint32_t height, const char *title);
void
window_shutdown(Window *w);
bool
window_should_close(Window *w);
void
window_poll_events(Window *w);
void
window_swap_buffers(Window *w);

void
window_begin_frame(Window *w);

uint32_t
window_get_width(Window *w);
uint32_t
window_get_height(Window *w);
GLFWwindow *
window_get_handle(Window *w);

bool
window_key(Window *w, int key);
bool
window_key_pressed(Window *w, int key);
bool
window_key_released(Window *w, int key);

bool
window_mouse_button(Window *w, int button);
bool
window_mouse_button_pressed(Window *w, int button);
bool
window_mouse_button_released(Window *w, int button);
void
window_get_mouse_position(Window *w, float *x, float *y);
void
window_get_mouse_delta(Window *w, float *dx, float *dy);
void
window_get_scroll(Window *w, float *x, float *y);

void
window_set_cursor_lock(Window *w, bool locked);
void
window_set_mouse_sensitivity(Window *w, float sensitivity);
void
window_set_position(Window *w, int x, int y);

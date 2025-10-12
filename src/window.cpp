/*
 * Window and input abstraction
 */

#include "window.hpp"
#include <cstring>
#include <stdio.h>

static void
key_callback(GLFWwindow *window, int key, int scancode, int action, int mods)
{
	Window *w = (Window *)glfwGetWindowUserPointer(window);
	if (!w || key < 0 || key >= INPUT_MAX_KEYS)
	{
		return;
	}

	if (action == GLFW_PRESS)
	{
		w->keys.set(key);
		w->keys_pressed.set(key);
	}
	else if (action == GLFW_RELEASE)
	{
		w->keys.reset(key);
		w->keys_released.set(key);
	}
}

static void
mouse_button_callback(GLFWwindow *window, int button, int action, int mods)
{
	Window *w = (Window *)glfwGetWindowUserPointer(window);
	if (!w || button < 0 || button >= INPUT_MAX_MOUSE_BUTTONS)
	{
		return;
	}

	if (action == GLFW_PRESS)
	{
		w->mouse_buttons |= (1 << button);
		w->mouse_buttons_pressed |= (1 << button);
	}
	else if (action == GLFW_RELEASE)
	{
		w->mouse_buttons &= ~(1 << button);
		w->mouse_buttons_released |= (1 << button);
	}
}

static void
cursor_position_callback(GLFWwindow *window, double xpos, double ypos)
{
	Window *w = (Window *)glfwGetWindowUserPointer(window);
	if (!w)
	{
		return;
	}

	float x = (float)xpos;
	float y = (float)ypos;

	if (w->first_mouse)
	{
		w->last_mouse_x = x;
		w->last_mouse_y = y;
		w->first_mouse = false;
	}

	w->mouse_dx = (x - w->last_mouse_x) * w->mouse_sensitivity;
	w->mouse_dy = (y - w->last_mouse_y) * w->mouse_sensitivity;

	w->last_mouse_x = x;
	w->last_mouse_y = y;
	w->mouse_x = x;
	w->mouse_y = y;
}

static void
scroll_callback(GLFWwindow *window, double xoffset, double yoffset)
{
	Window *w = (Window *)glfwGetWindowUserPointer(window);
	if (!w)
	{
		return;
	}

	w->scroll_x += (float)xoffset;
	w->scroll_y += (float)yoffset;
}

static void
framebuffer_size_callback(GLFWwindow *window, int width, int height)
{
	Window *w = (Window *)glfwGetWindowUserPointer(window);
	if (!w)
	{
		return;
	}

	w->width = (uint32_t)width;
	w->height = (uint32_t)height;

	glViewport(0, 0, width, height);
}

bool
window_init(Window *w, uint32_t width, uint32_t height, const char *title)
{
	memset(w, 0, sizeof(Window));

	w->width = width;
	w->height = height;
	w->title = title;
	w->mouse_sensitivity = 1.0f;
	w->first_mouse = true;

	if (!glfwInit())
	{
		printf("Failed to initialize GLFW\n");
		return false;
	}

	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

#ifdef __APPLE__
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

	w->handle = glfwCreateWindow(width, height, title, NULL, NULL);
	if (!w->handle)
	{
		printf("Failed to initialize GLFW window\n");
		glfwTerminate();
		return false;
	}

	glfwMakeContextCurrent(w->handle);

	if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
	{
		printf("Failed to initialize GLAD\n");
		glfwDestroyWindow(w->handle);
		glfwTerminate();
		return false;
	}

	int fb_width, fb_height;
	glfwGetFramebufferSize(w->handle, &fb_width, &fb_height);
	w->width = (uint32_t)fb_width;
	w->height = (uint32_t)fb_height;

	glViewport(0, 0, fb_width, fb_height);

	glfwSetWindowUserPointer(w->handle, w);

	glfwSetKeyCallback(w->handle, key_callback);
	glfwSetMouseButtonCallback(w->handle, mouse_button_callback);
	glfwSetCursorPosCallback(w->handle, cursor_position_callback);
	glfwSetScrollCallback(w->handle, scroll_callback);
	glfwSetFramebufferSizeCallback(w->handle, framebuffer_size_callback);

	double mx, my;
	glfwGetCursorPos(w->handle, &mx, &my);
	w->mouse_x = (float)mx;
	w->mouse_y = (float)my;
	w->last_mouse_x = w->mouse_x;
	w->last_mouse_y = w->mouse_y;

	printf("OpenGL Version: %s\n", glGetString(GL_VERSION));
	printf("GLSL Version: %s\n", glGetString(GL_SHADING_LANGUAGE_VERSION));
	printf("Renderer: %s\n", glGetString(GL_RENDERER));
	printf("Framebuffer Size: %u x %u\n", w->width, w->height);

	return true;
}

void
window_shutdown(Window *w)
{
	if (w->handle)
	{
		glfwDestroyWindow(w->handle);
		w->handle = NULL;
	}
	glfwTerminate();
}

bool
window_should_close(Window *w)
{
	return glfwWindowShouldClose(w->handle);
}

void
window_poll_events(Window *w)
{
	glfwPollEvents();
}

void
window_swap_buffers(Window *w)
{
	glfwSwapBuffers(w->handle);
}

void
window_begin_frame(Window *w)
{
	w->keys_pressed.reset();
	w->keys_released.reset();
	w->mouse_buttons_pressed = 0;
	w->mouse_buttons_released = 0;

	w->mouse_dx = 0.0f;
	w->mouse_dy = 0.0f;
	w->scroll_x = 0.0f;
	w->scroll_y = 0.0f;
}

uint32_t
window_get_width(Window *w)
{
	return w->width;
}

uint32_t
window_get_height(Window *w)
{
	return w->height;
}

struct GLFWwindow *
window_get_handle(Window *w)
{
	return w->handle;
}

bool
window_key(Window *w, int key)
{
	if (key < 0 || key >= INPUT_MAX_KEYS)
	{
		return false;
	}
	return w->keys.test(key);
}

bool
window_key_pressed(Window *w, int key)
{
	if (key < 0 || key >= INPUT_MAX_KEYS)
	{
		return false;
	}
	return w->keys_pressed.test(key);
}

bool
window_key_released(Window *w, int key)
{
	if (key < 0 || key >= INPUT_MAX_KEYS)
	{
		return false;
	}
	return w->keys_released.test(key);
}

bool
window_mouse_button(Window *w, int button)
{
	if (button < 0 || button >= INPUT_MAX_MOUSE_BUTTONS)
	{
		return false;
	}
	return (w->mouse_buttons & (1 << button)) != 0;
}

bool
window_mouse_button_pressed(Window *w, int button)
{
	if (button < 0 || button >= INPUT_MAX_MOUSE_BUTTONS)
	{
		return false;
	}
	return (w->mouse_buttons_pressed & (1 << button)) != 0;
}

bool
window_mouse_button_released(Window *w, int button)
{
	if (button < 0 || button >= INPUT_MAX_MOUSE_BUTTONS)
	{
		return false;
	}
	return (w->mouse_buttons_released & (1 << button)) != 0;
}

void
window_get_mouse_position(Window *w, float *x, float *y)
{
	if (x)
	{
		*x = w->mouse_x;
	}
	if (y)
	{
		*y = w->mouse_y;
	}
}

void
window_get_mouse_delta(Window *w, float *dx, float *dy)
{
	if (dx)
	{
		*dx = w->mouse_dx;
	}
	if (dy)
	{
		*dy = w->mouse_dy;
	}
}

void
window_get_scroll(Window *w, float *x, float *y)
{
	if (x)
	{
		*x = w->scroll_x;
	}
	if (y)
	{
		*y = w->scroll_y;
	}
}

void
window_set_cursor_lock(Window *w, bool locked)
{
	w->cursor_locked = locked;

	if (locked)
	{
		glfwSetInputMode(w->handle, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
		w->first_mouse = true;
	}
	else
	{
		glfwSetInputMode(w->handle, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
		glfwSetCursor(w->handle, NULL);

		double mx, my;
		glfwGetCursorPos(w->handle, &mx, &my);
		w->last_mouse_x = (float)mx;
		w->last_mouse_y = (float)my;
		w->first_mouse = true;
	}
}

void
window_set_mouse_sensitivity(Window *w, float sensitivity)
{
	w->mouse_sensitivity = sensitivity;
}

void
window_set_position(Window *w, int x, int y)
{
	if (!w || !w->handle)
	{
		return;
	}
	glfwSetWindowPos(w->handle, x, y);
}

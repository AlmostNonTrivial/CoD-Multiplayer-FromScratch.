#pragma once

#include <cstdint>
#include <stdint.h>
#include <stdbool.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <string_view>
#include "containers.hpp"

struct GLFWwindow;

enum VertexAttribute
{
	VERTEX_ATTR_POSITION = 0,
	VERTEX_ATTR_NORMAL = 1,
	VERTEX_ATTR_UV = 2,
	VERTEX_ATTR_COLOR = 3
};

struct VertexLayout
{
	uint32_t attributes;
	uint32_t stride;
};

struct ShaderProgram
{
	uint32_t id;

	int u_mvp;
	int u_model;
	int u_view;
	int u_projection;
	int u_color;
	int u_light_pos;
	int u_light_color;
	int u_view_pos;
};

struct Mesh
{
	uint32_t vao;
	uint32_t vbo;
	uint32_t ebo;
	uint32_t index_count;
	uint32_t vertex_count;
	uint32_t primitive_type;
};

enum MeshType
{
	MESH_PLANE = 0,
	MESH_CUBE,
	MESH_SPHERE,
	MESH_LINE,
	MESH_SKYBOX,
	MESH_COUNT,
};

#define MAX_TEXT_CHARS 256
struct TextBatch
{
	float	  vertices[MAX_TEXT_CHARS * 24];
	uint32_t  texture_ids[MAX_TEXT_CHARS];
	glm::vec3 colors[MAX_TEXT_CHARS];
	uint32_t  char_count;
};

struct Light
{
	glm::vec3 position;
	glm::vec3 color;
	float	  intensity;
};

struct Camera
{
	glm::vec3 position;
	glm::vec3 target;
	glm::vec3 up;
	float	  fov;
	float	  near_plane;
	float	  far_plane;
};

struct DrawCommand
{
	Mesh	 *mesh;
	glm::mat4 transform;
	glm::vec4 color;
};

struct Character
{
	uint32_t   TextureID;
	glm::ivec2 Size;
	glm::ivec2 Bearing;
	uint32_t   Advance;
};

struct Renderer
{
	struct GLFWwindow *window;
	uint32_t		   width;
	uint32_t		   height;

	ShaderProgram default_shader;
	ShaderProgram space_shader;

	Mesh meshes[MESH_COUNT];

	Camera	  camera;
	glm::mat4 view_matrix;
	glm::mat4 proj_matrix;

	Light light;

	fixed_array<DrawCommand, 300> commands;

	fixed_map<char, Character, 128> characters;
	uint32_t						text_vao;
	uint32_t						text_vbo;
	ShaderProgram					text_shader;

	TextBatch text_batch;
};

bool
renderer_init(Renderer *r, struct GLFWwindow *window);

void
renderer_shutdown(Renderer *r);

void
renderer_begin_frame(Renderer *r);

void
renderer_end_frame(Renderer *r);

void
renderer_resize(Renderer *r, uint32_t width, uint32_t height);

void
renderer_set_camera(Renderer *r, glm::vec3 position, glm::vec3 target);

void
renderer_update_matrices(Renderer *r);

void
renderer_set_light(Renderer *r, glm::vec3 position, glm::vec3 color, float intensity);

void
renderer_draw_mesh(Renderer *r, Mesh *mesh, glm::mat4 transform, glm::vec4 color);

void
renderer_draw_plane(Renderer *r, glm::vec3 position, glm::vec3 scale, glm::vec4 color);

void
renderer_draw_cube(Renderer *r, glm::vec3 position, glm::vec3 rotation, glm::vec3 scale, glm::vec4 color);

void
renderer_draw_sphere(Renderer *r, glm::vec3 position, float radius, glm::vec4 color);

void
renderer_draw_ray(Renderer *r, glm::vec3 origin, glm::vec3 direction, float length, glm::vec4 color);

ShaderProgram
shader_create(const char *vertex_src, const char *fragment_src);

ShaderProgram
shader_create_from_files(const char *vertex_path, const char *fragment_path);

void
shader_destroy(ShaderProgram *shader);

void
shader_use(ShaderProgram *shader);

void
shader_set_mat4(ShaderProgram *shader, const char *name, glm::mat4 *value);
void
shader_set_vec3(ShaderProgram *shader, const char *name, glm::vec3 *value);
void
shader_set_vec4(ShaderProgram *shader, const char *name, glm::vec4 *value);
void
shader_set_float(ShaderProgram *shader, const char *name, float value);
void
shader_set_int(ShaderProgram *shader, const char *name, int value);

Mesh
mesh_create(float *vertices, uint32_t vertex_count, uint32_t *indices, uint32_t index_count, VertexLayout layout,
			uint32_t primitive_type);

void
mesh_destroy(Mesh *mesh);

Mesh
mesh_create_plane(float size);

Mesh
mesh_create_cube(float size);

Mesh
mesh_create_sphere(float radius, uint32_t sectors, uint32_t stacks);

void
text_renderer_init(Renderer *r, const char *font_path);

void
text_renderer_shutdown(Renderer *r);

void
renderer_draw_text(Renderer *r, std::string_view text, float x, float y, float scale, glm::vec3 color);

void
renderer_update_text_projection(Renderer *r);

void
render_space_skybox(Renderer *r);

void
text_batch_begin(Renderer *r);

void
text_batch_add_string(Renderer *r, std::string_view text, float x, float y, float scale, glm::vec3 color);

void
text_batch_flush(Renderer *r);

/*
 * This project could have used Raylib but I went with a minimal OpenGL renderer to
 * tick it off the bucket list.
 *
 * Visuals being low on the priority, I've tried to be pragmatic by supporting
 * a single light source, basic Phong lighting, and hardcoding the material properties
 * in the shader, and a proceudural space skybox
 *
 * To be clear, this is not a good renderer.
 */

#include "renderer.hpp"
#include "../lib/glad.h"
#include <GLFW/glfw3.h>
#include "time.hpp"
#include <cstdint>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include <ft2build.h>
#include FT_FREETYPE_H

const char *space_vertex_shader = R"(
#version 330 core
layout (location = 0) in vec3 aPos;

out vec3 FragPos;
out vec3 TexCoords;

uniform mat4 projection;
uniform mat4 view;

void main() {
    TexCoords = aPos;
    vec4 pos = projection * mat4(mat3(view)) * vec4(aPos, 1.0);
    gl_Position = pos.xyww;
    FragPos = aPos;
}
)";

const char *space_fragment_shader = R"(
#version 330 core
out vec4 FragColor;

in vec3 TexCoords;
in vec3 FragPos;

uniform float time;
uniform vec3 sunDirection;


float hash(vec3 p) {
    p = fract(p * vec3(443.8975, 397.2973, 491.1871));
    p += dot(p, p.yxz + 19.19);
    return fract((p.x + p.y) * p.z);
}


float noise(vec3 p) {
    vec3 i = floor(p);
    vec3 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);

    return mix(mix(mix(hash(i + vec3(0,0,0)), hash(i + vec3(1,0,0)), f.x),
                   mix(hash(i + vec3(0,1,0)), hash(i + vec3(1,1,0)), f.x), f.y),
               mix(mix(hash(i + vec3(0,0,1)), hash(i + vec3(1,0,1)), f.x),
                   mix(hash(i + vec3(0,1,1)), hash(i + vec3(1,1,1)), f.x), f.y), f.z);
}


float fbm(vec3 p) {
    float value = 0.0;
    float amplitude = 0.5;

    for (int i = 0; i < 4; i++) {
        value += amplitude * noise(p);
        p *= 2.0;
        amplitude *= 0.5;
    }

    return value;
}

void main() {
    vec3 dir = normalize(TexCoords);


    vec3 spaceColor = vec3(0.02, 0.01, 0.05);


    float stars = 0.0;
    float star_density = 500.0;
    vec3 star_pos = dir * star_density;

    for(int i = 0; i < 3; i++) {
        float h = hash(floor(star_pos));
        vec3 f = fract(star_pos) - 0.5;
        float d = length(f);
        float star = 1.0 - smoothstep(0.0, 0.05 / (float(i) + 1.0), d);
        star *= h;
        stars += star;
        star_pos *= 2.3;
    }


    float field = hash(dir * 1000.0);
    field = pow(field, 40.0) * 2.0;
    stars += field;


    float nebula = fbm(dir * 3.0 + time * 0.01);
    nebula = pow(nebula, 2.0);

    vec3 nebula_color1 = vec3(0.4, 0.1, 0.6);
    vec3 nebula_color2 = vec3(0.1, 0.3, 0.7);
    vec3 nebula_color3 = vec3(0.6, 0.1, 0.3);

    float n2 = fbm(dir * 4.0 - time * 0.007);
    vec3 nebula_color = mix(nebula_color1, nebula_color2, n2);
    nebula_color = mix(nebula_color, nebula_color3, fbm(dir * 5.0));


    float galaxy = 0.0;
    vec3 galaxy_plane = vec3(dir.x, dir.y * 3.0, dir.z);
    float dist_to_plane = 1.0 - abs(galaxy_plane.y);
    if (dist_to_plane > 0.0) {
        galaxy = pow(dist_to_plane, 3.0) * noise(dir * 50.0);
        galaxy *= 0.3;
    }


    vec3 color = spaceColor;
    color += vec3(stars) * vec3(0.9, 0.95, 1.0);
    color += nebula * nebula_color * 0.3;
    color += galaxy * vec3(0.4, 0.3, 0.5);


    float sun = max(0.0, dot(dir, sunDirection));
    sun = pow(sun, 200.0) * 2.0;
    color += sun * vec3(1.0, 0.9, 0.7);


    color = color / (1.0 + color);
    color = pow(color, vec3(1.0/2.2));

    FragColor = vec4(color, 1.0);
}
)";

Mesh
create_skybox_mesh();

static const char *default_vertex_shader = "#version 330 core\n"
										   "layout (location = 0) in vec3 aPos;\n"
										   "layout (location = 1) in vec3 aNormal;\n"
										   "\n"
										   "out vec3 FragPos;\n"
										   "out vec3 Normal;\n"
										   "\n"
										   "uniform mat4 mvp;\n"
										   "uniform mat4 model;\n"
										   "\n"
										   "void main() {\n"
										   "    FragPos = vec3(model * vec4(aPos, 1.0));\n"
										   "    Normal = mat3(transpose(inverse(model))) * aNormal;\n"
										   "    gl_Position = mvp * vec4(aPos, 1.0);\n"
										   "}\n";

static const char *default_fragment_shader = "#version 330 core\n"
											 "out vec4 FragColor;\n"
											 "\n"
											 "in vec3 FragPos;\n"
											 "in vec3 Normal;\n"
											 "\n"
											 "uniform vec4 objectColor;\n"
											 "uniform vec3 lightPos;\n"
											 "uniform vec3 lightColor;\n"
											 "uniform vec3 viewPos;\n"
											 "\n"
											 "void main() {\n"
											 "    float ambientStrength = 0.1;\n"
											 "    vec3 ambient = ambientStrength * lightColor;\n"
											 "    vec3 norm = normalize(Normal);\n"
											 "    vec3 lightDir = normalize(lightPos - FragPos);\n"
											 "    float diff = max(dot(norm, lightDir), 0.0);\n"
											 "    vec3 diffuse = diff * lightColor;\n"
											 "\n"
											 "    float specularStrength = 0.5;\n"
											 "    vec3 viewDir = normalize(viewPos - FragPos);\n"
											 "    vec3 reflectDir = reflect(-lightDir, norm);\n"
											 "    float spec = pow(max(dot(viewDir, reflectDir), 0.0), 32);\n"
											 "    vec3 specular = specularStrength * spec * lightColor;\n"
											 "\n"
											 "    vec3 result = (ambient + diffuse + specular) * objectColor.rgb;\n"
											 "    FragColor = vec4(result, objectColor.a);\n"
											 "}\n";

static const char *text_vertex_shader = "#version 330 core\n"
										"layout (location = 0) in vec4 vertex;\n"
										"out vec2 TexCoords;\n"
										"uniform mat4 projection;\n"
										"void main() {\n"
										"    gl_Position = projection * vec4(vertex.xy, 0.0, 1.0);\n"
										"    TexCoords = vertex.zw;\n"
										"}\n";

static const char *text_fragment_shader = "#version 330 core\n"
										  "in vec2 TexCoords;\n"
										  "out vec4 color;\n"
										  "uniform sampler2D text;\n"
										  "uniform vec3 textColor;\n"
										  "void main() {\n"
										  "    vec4 sampled = vec4(1.0, 1.0, 1.0, texture(text, TexCoords).r);\n"
										  "    color = vec4(textColor, 1.0) * sampled;\n"
										  "}\n";

static uint32_t
compile_shader_stage(const char *source, GLenum type)
{
	uint32_t shader = glCreateShader(type);
	glShaderSource(shader, 1, &source, NULL);
	glCompileShader(shader);

	int success;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
	if (!success)
	{
		char info_log[512];
		glGetShaderInfoLog(shader, 512, NULL, info_log);
		printf("Shader compilation failed: %s\n", info_log);
		return 0;
	}

	return shader;
}

ShaderProgram
shader_create(const char *vertex_src, const char *fragment_src)
{
	ShaderProgram shader = {};

	uint32_t vertex = compile_shader_stage(vertex_src, GL_VERTEX_SHADER);
	uint32_t fragment = compile_shader_stage(fragment_src, GL_FRAGMENT_SHADER);

	if (!vertex || !fragment)
	{
		if (vertex)
		{
			glDeleteShader(vertex);
		}
		if (fragment)
		{
			glDeleteShader(fragment);
		}
		return shader;
	}

	shader.id = glCreateProgram();
	glAttachShader(shader.id, vertex);
	glAttachShader(shader.id, fragment);
	glLinkProgram(shader.id);

	int success;
	glGetProgramiv(shader.id, GL_LINK_STATUS, &success);
	if (!success)
	{
		char info_log[512];
		glGetProgramInfoLog(shader.id, 512, NULL, info_log);
		printf("Shader linking failed: %s\n", info_log);
		glDeleteProgram(shader.id);
		shader.id = 0;
	}

	glDeleteShader(vertex);
	glDeleteShader(fragment);

	shader.u_mvp = glGetUniformLocation(shader.id, "mvp");
	shader.u_model = glGetUniformLocation(shader.id, "model");
	shader.u_view = glGetUniformLocation(shader.id, "view");
	shader.u_projection = glGetUniformLocation(shader.id, "projection");
	shader.u_color = glGetUniformLocation(shader.id, "objectColor");
	shader.u_light_pos = glGetUniformLocation(shader.id, "lightPos");
	shader.u_light_color = glGetUniformLocation(shader.id, "lightColor");
	shader.u_view_pos = glGetUniformLocation(shader.id, "viewPos");

	return shader;
}

void
shader_destroy(ShaderProgram *shader)
{
	if (shader->id)
	{
		glDeleteProgram(shader->id);
		shader->id = 0;
	}
}

void
shader_use(ShaderProgram *shader)
{
	glUseProgram(shader->id);
}

void
shader_set_mat4(ShaderProgram *shader, const char *name, glm::mat4 *value)
{
	glUniformMatrix4fv(glGetUniformLocation(shader->id, name), 1, GL_FALSE, glm::value_ptr(*value));
}

void
shader_set_vec3(ShaderProgram *shader, const char *name, glm::vec3 *value)
{
	glUniform3fv(glGetUniformLocation(shader->id, name), 1, glm::value_ptr(*value));
}

void
shader_set_vec4(ShaderProgram *shader, const char *name, glm::vec4 *value)
{
	glUniform4fv(glGetUniformLocation(shader->id, name), 1, glm::value_ptr(*value));
}

void
shader_set_float(ShaderProgram *shader, const char *name, float value)
{
	glUniform1f(glGetUniformLocation(shader->id, name), value);
}

void
shader_set_int(ShaderProgram *shader, const char *name, int value)
{
	glUniform1i(glGetUniformLocation(shader->id, name), value);
}

Mesh
mesh_create(float *vertices, uint32_t vertex_count, uint32_t *indices, uint32_t index_count, VertexLayout layout,
			uint32_t primitive_type)
{
	Mesh mesh;
	mesh.primitive_type = primitive_type;
	mesh.vertex_count = vertex_count;
	mesh.index_count = index_count;

	glGenVertexArrays(1, &mesh.vao);
	glGenBuffers(1, &mesh.vbo);
	glGenBuffers(1, &mesh.ebo);

	glBindVertexArray(mesh.vao);

	glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
	glBufferData(GL_ARRAY_BUFFER, vertex_count * layout.stride, vertices, GL_STATIC_DRAW);

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.ebo);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, index_count * sizeof(uint32_t), indices, GL_STATIC_DRAW);

	uint32_t offset = 0;

	if (layout.attributes & (1 << VERTEX_ATTR_POSITION))
	{
		glVertexAttribPointer(VERTEX_ATTR_POSITION, 3, GL_FLOAT, GL_FALSE, layout.stride, (void *)(size_t)offset);
		glEnableVertexAttribArray(VERTEX_ATTR_POSITION);
		offset += 3 * sizeof(float);
	}

	if (layout.attributes & (1 << VERTEX_ATTR_NORMAL))
	{
		glVertexAttribPointer(VERTEX_ATTR_NORMAL, 3, GL_FLOAT, GL_FALSE, layout.stride, (void *)(size_t)offset);
		glEnableVertexAttribArray(VERTEX_ATTR_NORMAL);
		offset += 3 * sizeof(float);
	}

	if (layout.attributes & (1 << VERTEX_ATTR_UV))
	{
		glVertexAttribPointer(VERTEX_ATTR_UV, 2, GL_FLOAT, GL_FALSE, layout.stride, (void *)(size_t)offset);
		glEnableVertexAttribArray(VERTEX_ATTR_UV);
		offset += 2 * sizeof(float);
	}

	if (layout.attributes & (1 << VERTEX_ATTR_COLOR))
	{
		glVertexAttribPointer(VERTEX_ATTR_COLOR, 4, GL_FLOAT, GL_FALSE, layout.stride, (void *)(size_t)offset);
		glEnableVertexAttribArray(VERTEX_ATTR_COLOR);
		offset += 4 * sizeof(float);
	}

	glBindVertexArray(0);

	return mesh;
}

void
mesh_destroy(Mesh *mesh)
{
	if (mesh->vao)
	{
		glDeleteVertexArrays(1, &mesh->vao);
	}
	if (mesh->vbo)
	{
		glDeleteBuffers(1, &mesh->vbo);
	}
	if (mesh->ebo)
	{
		glDeleteBuffers(1, &mesh->ebo);
	}
	memset(mesh, 0, sizeof(Mesh));
}

Mesh
mesh_create_plane(float size)
{
	float half = size * 0.5f;
	float vertices[] = {-half, 0.0f, half,	0.0f, 1.0f, 0.0f, half,	 0.0f, half,  0.0f, 1.0f, 0.0f,
						half,  0.0f, -half, 0.0f, 1.0f, 0.0f, -half, 0.0f, -half, 0.0f, 1.0f, 0.0f};

	uint32_t indices[] = {0, 1, 2, 2, 3, 0};

	VertexLayout layout = {.attributes = (1 << VERTEX_ATTR_POSITION) | (1 << VERTEX_ATTR_NORMAL),
						   .stride = 6 * sizeof(float)};

	return mesh_create(vertices, 4, indices, 6, layout, GL_TRIANGLES);
}

Mesh
mesh_create_cube(float size)
{
	float half = size * 0.5f;
	float vertices[] = {

		-half, -half, half,	 0.0f,	0.0f,  1.0f,  half,	 -half, half,  0.0f,  0.0f,	 1.0f,
		half,  half,  half,	 0.0f,	0.0f,  1.0f,  -half, half,	half,  0.0f,  0.0f,	 1.0f,

		half,  -half, -half, 0.0f,	0.0f,  -1.0f, -half, -half, -half, 0.0f,  0.0f,	 -1.0f,
		-half, half,  -half, 0.0f,	0.0f,  -1.0f, half,	 half,	-half, 0.0f,  0.0f,	 -1.0f,

		-half, half,  half,	 0.0f,	1.0f,  0.0f,  half,	 half,	half,  0.0f,  1.0f,	 0.0f,
		half,  half,  -half, 0.0f,	1.0f,  0.0f,  -half, half,	-half, 0.0f,  1.0f,	 0.0f,

		-half, -half, -half, 0.0f,	-1.0f, 0.0f,  half,	 -half, -half, 0.0f,  -1.0f, 0.0f,
		half,  -half, half,	 0.0f,	-1.0f, 0.0f,  -half, -half, half,  0.0f,  -1.0f, 0.0f,

		half,  -half, half,	 1.0f,	0.0f,  0.0f,  half,	 -half, -half, 1.0f,  0.0f,	 0.0f,
		half,  half,  -half, 1.0f,	0.0f,  0.0f,  half,	 half,	half,  1.0f,  0.0f,	 0.0f,

		-half, -half, -half, -1.0f, 0.0f,  0.0f,  -half, -half, half,  -1.0f, 0.0f,	 0.0f,
		-half, half,  half,	 -1.0f, 0.0f,  0.0f,  -half, half,	-half, -1.0f, 0.0f,	 0.0f};

	uint32_t indices[] = {0,  1,  2,  2,  3,  0,  4,  5,  6,  6,  7,  4,  8,  9,  10, 10, 11, 8,
						  12, 13, 14, 14, 15, 12, 16, 17, 18, 18, 19, 16, 20, 21, 22, 22, 23, 20};

	VertexLayout layout = {.attributes = (1 << VERTEX_ATTR_POSITION) | (1 << VERTEX_ATTR_NORMAL),
						   .stride = 6 * sizeof(float)};

	return mesh_create(vertices, 24, indices, 36, layout, GL_TRIANGLES);
}

Mesh
mesh_create_sphere(float radius, uint32_t sectors, uint32_t stacks)
{
	std::vector<float>	  vertices;
	std::vector<uint32_t> indices;

	float sector_step = 2 * M_PI / sectors;
	float stack_step = M_PI / stacks;

	for (uint32_t i = 0; i <= stacks; ++i)
	{
		float stack_angle = M_PI / 2 - i * stack_step;
		float xy = radius * cosf(stack_angle);
		float z = radius * sinf(stack_angle);

		for (uint32_t j = 0; j <= sectors; ++j)
		{
			float sector_angle = j * sector_step;
			float x = xy * cosf(sector_angle);
			float y = xy * sinf(sector_angle);

			vertices.push_back(x);
			vertices.push_back(y);
			vertices.push_back(z);

			float len = sqrtf(x * x + y * y + z * z);
			vertices.push_back(x / len);
			vertices.push_back(y / len);
			vertices.push_back(z / len);
		}
	}

	for (uint32_t i = 0; i < stacks; ++i)
	{
		uint32_t k1 = i * (sectors + 1);
		uint32_t k2 = k1 + sectors + 1;

		for (uint32_t j = 0; j < sectors; ++j, ++k1, ++k2)
		{
			if (i != 0)
			{
				indices.push_back(k1);
				indices.push_back(k2);
				indices.push_back(k1 + 1);
			}

			if (i != (stacks - 1))
			{
				indices.push_back(k1 + 1);
				indices.push_back(k2);
				indices.push_back(k2 + 1);
			}
		}
	}

	VertexLayout layout = {.attributes = (1 << VERTEX_ATTR_POSITION) | (1 << VERTEX_ATTR_NORMAL),
						   .stride = 6 * sizeof(float)};

	return mesh_create(vertices.data(), vertices.size() / 6, indices.data(), indices.size(), layout, GL_TRIANGLES);
}

Mesh
mesh_create_line(void)
{
	float vertices[] = {0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f};

	uint32_t indices[] = {0, 1};

	VertexLayout layout = {.attributes = (1 << VERTEX_ATTR_POSITION) | (1 << VERTEX_ATTR_NORMAL),
						   .stride = 6 * sizeof(float)};

	return mesh_create(vertices, 2, indices, 2, layout, GL_LINES);
}

Mesh
create_skybox_mesh()
{
	float vertices[] = {
		-1, -1, -1, 1, -1, -1, 1, 1, -1, -1, 1, -1, -1, -1, 1, 1, -1, 1, 1, 1, 1, -1, 1, 1,
	};

	uint32_t indices[] = {0, 1, 2, 2, 3, 0, 4, 5, 6, 6, 7, 4, 0, 4, 7, 7, 3, 0,
						  1, 5, 6, 6, 2, 1, 3, 7, 6, 6, 2, 3, 0, 4, 5, 5, 1, 0};

	VertexLayout layout = {.attributes = (1 << VERTEX_ATTR_POSITION), .stride = 3 * sizeof(float)};

	return mesh_create(vertices, 8, indices, 36, layout, GL_TRIANGLES);
}

void
renderer_draw_ray(Renderer *r, glm::vec3 origin, glm::vec3 direction, float length, glm::vec4 color)
{
	glm::vec3 dir = glm::normalize(direction);

	glm::mat4 transform = glm::mat4(1.0f);
	transform = glm::translate(transform, origin);

	glm::vec3 forward = dir;
	glm::vec3 up = glm::vec3(0, 1, 0);

	if (glm::abs(glm::dot(forward, up)) > 0.999f)
	{
		up = glm::vec3(1, 0, 0);
	}

	glm::vec3 right = glm::normalize(glm::cross(up, forward));
	up = glm::cross(forward, right);

	glm::mat4 rotation = glm::mat4(1.0f);
	rotation[0] = glm::vec4(right, 0.0f);
	rotation[1] = glm::vec4(up, 0.0f);
	rotation[2] = glm::vec4(forward, 0.0f);

	transform = transform * rotation;
	transform = glm::scale(transform, glm::vec3(1.0f, 1.0f, length));

	if (!r->commands.push({&r->meshes[MESH_LINE], transform, color}))
	{
		printf("Command buffer full (300 max)\n");
	}
}

bool
renderer_init(Renderer *r, struct GLFWwindow *window)
{
	memset(r, 0, sizeof(Renderer));
	r->window = window;

	int width, height;
	glfwGetFramebufferSize(window, &width, &height);
	r->width = (uint32_t)width;
	r->height = (uint32_t)height;

	glEnable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	glViewport(0, 0, width, height);

	r->default_shader = shader_create(default_vertex_shader, default_fragment_shader);
	if (!r->default_shader.id)
	{
		printf("Failed to create default shader\n");
		return false;
	}

	r->space_shader = shader_create(space_vertex_shader, space_fragment_shader);
	if (!r->space_shader.id)
	{
		printf("Failed to create space shader\n");
		return false;
	}

	r->meshes[MESH_PLANE] = mesh_create_plane(100.0f);
	r->meshes[MESH_CUBE] = mesh_create_cube(1.0f);
	r->meshes[MESH_SPHERE] = mesh_create_sphere(1.0f, 16, 16);
	r->meshes[MESH_LINE] = mesh_create_line();
	r->meshes[MESH_SKYBOX] = create_skybox_mesh();

	r->commands.clear();

	r->camera.position = glm::vec3(10.0f, 10.0f, 10.0f);
	r->camera.target = glm::vec3(0.0f, 0.0f, 0.0f);
	r->camera.up = glm::vec3(0.0f, 1.0f, 0.0f);
	r->camera.fov = 45.0f;
	r->camera.near_plane = 0.1f;
	r->camera.far_plane = 1000.0f;

	renderer_update_matrices(r);

	renderer_set_light(r, glm::vec3(10.0f, 10.0f, 10.0f), glm::vec3(1.0f, 1.0f, 1.0f), 1.0f);

	text_renderer_init(r, "../Antonio-Bold.ttf");
	return true;
}

void
renderer_shutdown(Renderer *r)
{
	for (int i = 0; i < MESH_COUNT; i++)
	{
		mesh_destroy(&r->meshes[i]);
	}

	shader_destroy(&r->default_shader);
	shader_destroy(&r->space_shader);
	text_renderer_shutdown(r);

	memset(r, 0, sizeof(Renderer));
}

void
renderer_begin_frame(Renderer *r)
{
	glClearColor(0.1f, 0.1f, 0.15f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	r->commands.clear();
}

void
renderer_end_frame(Renderer *r)
{
	shader_use(&r->default_shader);

	glUniform3fv(r->default_shader.u_light_pos, 1, glm::value_ptr(r->light.position));
	glm::vec3 light_color = r->light.color * r->light.intensity;
	glUniform3fv(r->default_shader.u_light_color, 1, glm::value_ptr(light_color));

	glUniform3fv(r->default_shader.u_view_pos, 1, glm::value_ptr(r->camera.position));

	for (uint32_t i = 0; i < r->commands.size(); i++)
	{
		DrawCommand *cmd = &r->commands[i];

		glm::mat4 mvp = r->proj_matrix * r->view_matrix * cmd->transform;

		glUniformMatrix4fv(r->default_shader.u_mvp, 1, GL_FALSE, glm::value_ptr(mvp));
		glUniformMatrix4fv(r->default_shader.u_model, 1, GL_FALSE, glm::value_ptr(cmd->transform));
		glUniform4fv(r->default_shader.u_color, 1, glm::value_ptr(cmd->color));

		glBindVertexArray(cmd->mesh->vao);
		glDrawElements(cmd->mesh->primitive_type, cmd->mesh->index_count, GL_UNSIGNED_INT, 0);
	}

	glBindVertexArray(0);
}

void
renderer_resize(Renderer *r, uint32_t width, uint32_t height)
{
	r->width = width;
	r->height = height;
	glViewport(0, 0, width, height);
	renderer_update_matrices(r);
}

void
renderer_draw_mesh(Renderer *r, Mesh *mesh, glm::mat4 transform, glm::vec4 color)
{
	if (!r->commands.push({mesh, transform, color}))
	{
		printf("Command buffer full (300 max)\n");
	}
}

void
renderer_draw_plane(Renderer *r, glm::vec3 position, glm::vec3 scale, glm::vec4 color)
{
	glm::mat4 transform = glm::mat4(1.0f);
	transform = glm::translate(transform, position);
	transform = glm::scale(transform, scale);

	renderer_draw_mesh(r, &r->meshes[MESH_PLANE], transform, color);
}

void
renderer_draw_cube(Renderer *r, glm::vec3 position, glm::vec3 rotation, glm::vec3 scale, glm::vec4 color)
{
	glm::mat4 transform = glm::mat4(1.0f);
	transform = glm::translate(transform, position);
	transform = glm::rotate(transform, rotation.x, glm::vec3(1.0f, 0.0f, 0.0f));
	transform = glm::rotate(transform, rotation.y, glm::vec3(0.0f, 1.0f, 0.0f));
	transform = glm::rotate(transform, rotation.z, glm::vec3(0.0f, 0.0f, 1.0f));
	transform = glm::scale(transform, scale);

	renderer_draw_mesh(r, &r->meshes[MESH_CUBE], transform, color);
}

void
renderer_draw_sphere(Renderer *r, glm::vec3 position, float radius, glm::vec4 color)
{
	glm::mat4 transform = glm::mat4(1.0f);
	transform = glm::translate(transform, position);
	transform = glm::scale(transform, glm::vec3(radius));

	renderer_draw_mesh(r, &r->meshes[MESH_SPHERE], transform, color);
}

void
renderer_set_light(Renderer *r, glm::vec3 position, glm::vec3 color, float intensity)
{
	r->light.position = position;
	r->light.color = color;
	r->light.intensity = intensity;
}

void
renderer_set_camera(Renderer *r, glm::vec3 position, glm::vec3 target)
{
	r->camera.position = position;
	r->camera.target = target;
	renderer_update_matrices(r);
}

void
renderer_update_matrices(Renderer *r)
{
	r->view_matrix = glm::lookAt(r->camera.position, r->camera.target, r->camera.up);
	r->proj_matrix = glm::perspective(glm::radians(r->camera.fov), (float)r->width / (float)r->height,
									  r->camera.near_plane, r->camera.far_plane);
}

void
text_renderer_init(Renderer *r, const char *font_path)
{
	r->text_shader = shader_create(text_vertex_shader, text_fragment_shader);
	if (!r->text_shader.id)
	{
		printf("Failed to create text shader\n");
		return;
	}

	glm::mat4 projection = glm::ortho(0.0f, (float)r->width, 0.0f, (float)r->height);
	shader_use(&r->text_shader);
	glUniformMatrix4fv(glGetUniformLocation(r->text_shader.id, "projection"), 1, GL_FALSE, glm::value_ptr(projection));

	FT_Library ft;
	if (FT_Init_FreeType(&ft))
	{
		printf("Could not initialize FreeType Library\n");
		return;
	}

	FT_Face face;
	if (FT_New_Face(ft, font_path, 0, &face))
	{
		printf("Failed to load font from %s\n", font_path);
		FT_Done_FreeType(ft);
		return;
	}

	FT_Set_Pixel_Sizes(face, 0, 48);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

	for (unsigned char c = 0; c < 128; c++)
	{
		if (FT_Load_Char(face, c, FT_LOAD_RENDER))
		{
			printf("Failed to load Glyph %c\n", c);
			continue;
		}

		uint32_t texture;
		glGenTextures(1, &texture);
		glBindTexture(GL_TEXTURE_2D, texture);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, face->glyph->bitmap.width, face->glyph->bitmap.rows, 0, GL_RED,
					 GL_UNSIGNED_BYTE, face->glyph->bitmap.buffer);

		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

		Character character = {texture, glm::ivec2(face->glyph->bitmap.width, face->glyph->bitmap.rows),
							   glm::ivec2(face->glyph->bitmap_left, face->glyph->bitmap_top),
							   static_cast<uint32_t>(face->glyph->advance.x)};
		r->characters.insert(c, character);
	}
	glBindTexture(GL_TEXTURE_2D, 0);

	FT_Done_Face(face);
	FT_Done_FreeType(ft);

	glGenVertexArrays(1, &r->text_vao);
	glGenBuffers(1, &r->text_vbo);
	glBindVertexArray(r->text_vao);
	glBindBuffer(GL_ARRAY_BUFFER, r->text_vbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 6 * 4, NULL, GL_DYNAMIC_DRAW);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), 0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindVertexArray(0);
}

void
text_renderer_shutdown(Renderer *r)
{
	for (uint32_t i = 0; i < r->characters.table_capacity(); i++)
	{
		auto &entry = r->characters.data()[i];
		if (entry.state == 1)
		{
			glDeleteTextures(1, &entry.value.TextureID);
		}
	}
	r->characters.clear();

	if (r->text_vao)
	{
		glDeleteVertexArrays(1, &r->text_vao);
	}
	if (r->text_vbo)
	{
		glDeleteBuffers(1, &r->text_vbo);
	}

	shader_destroy(&r->text_shader);

	r->text_vao = 0;
	r->text_vbo = 0;
}

void
renderer_update_text_projection(Renderer *r)
{
	glm::mat4 projection = glm::ortho(0.0f, (float)r->width, 0.0f, (float)r->height);
	shader_use(&r->text_shader);
	glUniformMatrix4fv(glGetUniformLocation(r->text_shader.id, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
}

void
render_space_skybox(Renderer *r)
{
	static TimePoint start_time = time_now();

	GLboolean cull_was_enabled = glIsEnabled(GL_CULL_FACE);

	glDepthFunc(GL_LEQUAL);
	glDepthMask(GL_FALSE);
	if (cull_was_enabled)
	{
		glDisable(GL_CULL_FACE);
	}

	shader_use(&r->space_shader);

	glm::mat4 view_no_translation = glm::mat4(glm::mat3(r->view_matrix));

	shader_set_mat4(&r->space_shader, "view", &view_no_translation);
	shader_set_mat4(&r->space_shader, "projection", &r->proj_matrix);

	float time = time_elapsed_seconds(start_time);
	shader_set_float(&r->space_shader, "time", time);

	glm::vec3 sun_dir = glm::normalize(glm::vec3(1.0f, 0.3f, 0.5f));
	shader_set_vec3(&r->space_shader, "sunDirection", &sun_dir);

	glBindVertexArray(r->meshes[MESH_SKYBOX].vao);
	glDrawElements(GL_TRIANGLES, r->meshes[MESH_SKYBOX].index_count, GL_UNSIGNED_INT, 0);

	glDepthMask(GL_TRUE);
	glDepthFunc(GL_LESS);
	if (cull_was_enabled)
	{
		glEnable(GL_CULL_FACE);
	}

	glBindVertexArray(0);
}

void
text_batch_begin(Renderer *r)
{
	r->text_batch.char_count = 0;
}

void
text_batch_add_string(Renderer *r, std::string_view text, float x, float y, float scale, glm::vec3 color)
{
	for (char c : text)
	{
		if (r->text_batch.char_count >= MAX_TEXT_CHARS)
		{
			printf("Text batch full, increase MAX_TEXT_CHARS\n");
			return;
		}

		Character *ch = r->characters.get(c);
		if (!ch)
		{
			continue;
		}

		float xpos = x + ch->Bearing.x * scale;
		float ypos = y - (ch->Size.y - ch->Bearing.y) * scale;
		float w = ch->Size.x * scale;
		float h = ch->Size.y * scale;

		uint32_t idx = r->text_batch.char_count;
		uint32_t base = idx * 24;

		r->text_batch.vertices[base + 0] = xpos;
		r->text_batch.vertices[base + 1] = ypos + h;
		r->text_batch.vertices[base + 2] = 0.0f;
		r->text_batch.vertices[base + 3] = 0.0f;

		r->text_batch.vertices[base + 4] = xpos;
		r->text_batch.vertices[base + 5] = ypos;
		r->text_batch.vertices[base + 6] = 0.0f;
		r->text_batch.vertices[base + 7] = 1.0f;

		r->text_batch.vertices[base + 8] = xpos + w;
		r->text_batch.vertices[base + 9] = ypos;
		r->text_batch.vertices[base + 10] = 1.0f;
		r->text_batch.vertices[base + 11] = 1.0f;

		r->text_batch.vertices[base + 12] = xpos;
		r->text_batch.vertices[base + 13] = ypos + h;
		r->text_batch.vertices[base + 14] = 0.0f;
		r->text_batch.vertices[base + 15] = 0.0f;

		r->text_batch.vertices[base + 16] = xpos + w;
		r->text_batch.vertices[base + 17] = ypos;
		r->text_batch.vertices[base + 18] = 1.0f;
		r->text_batch.vertices[base + 19] = 1.0f;

		r->text_batch.vertices[base + 20] = xpos + w;
		r->text_batch.vertices[base + 21] = ypos + h;
		r->text_batch.vertices[base + 22] = 1.0f;
		r->text_batch.vertices[base + 23] = 0.0f;

		r->text_batch.texture_ids[idx] = ch->TextureID;
		r->text_batch.colors[idx] = color;
		r->text_batch.char_count++;

		x += (ch->Advance >> 6) * scale;
	}
}

void
text_batch_flush(Renderer *r)
{
	if (r->text_batch.char_count == 0)
	{
		return;
	}

	GLboolean blend_enabled = glIsEnabled(GL_BLEND);
	GLboolean depth_enabled = glIsEnabled(GL_DEPTH_TEST);

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDisable(GL_DEPTH_TEST);

	shader_use(&r->text_shader);
	glActiveTexture(GL_TEXTURE0);
	glBindVertexArray(r->text_vao);
	glBindBuffer(GL_ARRAY_BUFFER, r->text_vbo);

	size_t total_size = r->text_batch.char_count * 24 * sizeof(float);
	glBufferData(GL_ARRAY_BUFFER, total_size, r->text_batch.vertices, GL_DYNAMIC_DRAW);

	uint32_t current_texture = r->text_batch.texture_ids[0];
	glBindTexture(GL_TEXTURE_2D, current_texture);
	glUniform3f(glGetUniformLocation(r->text_shader.id, "textColor"), r->text_batch.colors[0].x,
				r->text_batch.colors[0].y, r->text_batch.colors[0].z);

	uint32_t batch_start = 0;

	for (uint32_t i = 1; i <= r->text_batch.char_count; i++)
	{
		bool texture_changed = (i < r->text_batch.char_count && r->text_batch.texture_ids[i] != current_texture);
		bool color_changed =
			(i < r->text_batch.char_count && r->text_batch.colors[i] != r->text_batch.colors[batch_start]);
		bool at_end = (i == r->text_batch.char_count);

		if (texture_changed || color_changed || at_end)
		{
			uint32_t batch_count = i - batch_start;
			glDrawArrays(GL_TRIANGLES, batch_start * 6, batch_count * 6);

			if (i < r->text_batch.char_count)
			{
				batch_start = i;
				if (texture_changed)
				{
					current_texture = r->text_batch.texture_ids[i];
					glBindTexture(GL_TEXTURE_2D, current_texture);
				}
				if (color_changed)
				{
					glUniform3f(glGetUniformLocation(r->text_shader.id, "textColor"), r->text_batch.colors[i].x,
								r->text_batch.colors[i].y, r->text_batch.colors[i].z);
				}
			}
		}
	}

	glBindVertexArray(0);
	glBindTexture(GL_TEXTURE_2D, 0);

	if (!blend_enabled)
	{
		glDisable(GL_BLEND);
	}
	if (depth_enabled)
	{
		glEnable(GL_DEPTH_TEST);
	}
}

void
renderer_draw_text(Renderer *r, std::string_view text, float x, float y, float scale, glm::vec3 color)
{
	text_batch_begin(r);
	text_batch_add_string(r, text, x, y, scale, color);
	text_batch_flush(r);
}

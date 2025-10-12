#pragma once
#include <glm/gtc/quaternion.hpp>
#include <glm/glm.hpp>

struct Sphere
{
	glm::vec3 center;
	float	  radius;
};

struct AABB
{
	glm::vec3 min;
	glm::vec3 max;
};

struct OBB
{
	glm::vec3 center;
	glm::vec3 half_extents;
	glm::quat rotation;
	float	  bounds_radius; /* precomputed for broadphase */
};

struct Ray
{
	glm::vec3 origin;
	glm::vec3 direction;
	float	  length;
};

struct Contact
{
	glm::vec3 point;
	glm::vec3 normal;
	float	  depth;
};

struct RayHit
{
	glm::vec3 point;
	glm::vec3 normal;
	float	  distance;
};

bool
sphere_vs_sphere(Sphere &a, Sphere &b, Contact *out_contact);

bool
sphere_vs_aabb(Sphere &sphere, AABB &box, Contact *out_contact);

bool
sphere_vs_obb(Sphere &sphere, OBB &obb, Contact *out_contact);
bool raycast_sphere(Ray &ray, glm::vec3 &position, float radius, RayHit *out_hit);
bool raycast_obb(Ray &ray, OBB &obb, RayHit *out_hit);



OBB
obb_from_center_size_rotation(glm::vec3 center, glm::vec3 half_extents, glm::quat rotation = glm::quat(1, 0, 0, 0));

inline OBB
obb_from_center_size(glm::vec3 center, glm::vec3 half_extents)
{
	return obb_from_center_size_rotation(center, half_extents);
}

inline OBB
obb_from_center_size_rotation(glm::vec3 center, glm::vec3 half_extents, glm::quat rotation)
{
	OBB obb;
	obb.center = center;
	obb.half_extents = half_extents;
	obb.rotation = rotation;
	obb.bounds_radius = glm::length(half_extents);
	return obb;
}

/*
 * Relevant 3D math
 *
 * Hit tests have broadphases built in
 */
#include "math.hpp"

static bool
sphere_vs_aabb_local(Sphere &sphere, AABB &box, Contact *out_contact)
{
	glm::vec3 closest = glm::clamp(sphere.center, box.min, box.max);
	glm::vec3 delta = closest - sphere.center;
	float	  dist_sq = glm::dot(delta, delta);

	if (dist_sq > sphere.radius * sphere.radius)
	{
		return false;
	}

	glm::vec3 to_min = sphere.center - box.min;
	glm::vec3 to_max = box.max - sphere.center;

	float distances[6] = {to_min.x, to_min.y, to_min.z, to_max.x, to_max.y, to_max.z};

	int	  min_axis = 0;
	float min_dist = distances[0];
	for (int i = 1; i < 6; i++)
	{
		if (distances[i] < min_dist)
		{
			min_dist = distances[i];
			min_axis = i;
		}
	}

	out_contact->normal = glm::vec3(0);
	if (min_axis < 3)
	{
		out_contact->normal[min_axis] = -1.0f;
		out_contact->point = sphere.center;
		out_contact->point[min_axis] = box.min[min_axis];
	}
	else
	{
		int axis = min_axis - 3;
		out_contact->normal[axis] = 1.0f;
		out_contact->point = sphere.center;
		out_contact->point[axis] = box.max[axis];
	}
	out_contact->depth = min_dist + sphere.radius;

	return true;
}

bool
sphere_vs_sphere(Sphere &a, Sphere &b, Contact *out_contact)
{
	glm::vec3 delta = b.center - a.center;
	float	  dist_sq = glm::dot(delta, delta);
	float	  radius_sum = a.radius + b.radius;

	if (dist_sq > radius_sum * radius_sum)
	{
		return false;
	}

	float dist = sqrt(dist_sq);

	out_contact->normal = delta / dist;
	out_contact->depth = radius_sum - dist;
	out_contact->point = a.center + out_contact->normal * a.radius;

	return true;
}

bool
sphere_vs_obb(Sphere &sphere, OBB &obb, Contact *out_contact)
{

	glm::vec3 delta = obb.center - sphere.center;
	float	  dist_sq = glm::dot(delta, delta);
	float	  radius_sum = sphere.radius + obb.bounds_radius;

	if (dist_sq >= radius_sum * radius_sum)
	{
		return false;
	}

	glm::mat3 rot_inv = glm::transpose(glm::mat3_cast(obb.rotation));
	Sphere	  local_sphere;
	local_sphere.center = rot_inv * (sphere.center - obb.center);
	local_sphere.radius = sphere.radius;

	AABB	local_box = {-obb.half_extents, obb.half_extents};
	Contact local_contact;
	bool	hit = sphere_vs_aabb_local(local_sphere, local_box, &local_contact);

	if (hit)
	{

		glm::mat3 rot = glm::mat3_cast(obb.rotation);
		out_contact->normal = rot * local_contact.normal;
		out_contact->point = rot * local_contact.point + obb.center;
		out_contact->depth = local_contact.depth;
	}

	return hit;
}
static bool
raycast_aabb(Ray &ray, AABB &box, RayHit *out_hit)
{
	glm::vec3 inv_dir = glm::vec3(1.0f) / ray.direction;
	glm::vec3 t_min = (box.min - ray.origin) * inv_dir;
	glm::vec3 t_max = (box.max - ray.origin) * inv_dir;

	glm::vec3 t1 = glm::min(t_min, t_max);
	glm::vec3 t2 = glm::max(t_min, t_max);

	float t_near = std::fmax(std::fmax(t1.x, t1.y), t1.z);
	float t_far = std::fmin(std::fmin(t2.x, t2.y), t2.z);

	if (t_near > t_far || t_far < 0 || t_near > ray.length)
	{
		return false;
	}

	float t = t_near > 0 ? t_near : t_far;

	out_hit->distance = t;
	out_hit->point = ray.origin + ray.direction * t;

	int near_axis = (t1.x > t1.y) ? ((t1.x > t1.z) ? 0 : 2) : ((t1.y > t1.z) ? 1 : 2);
	out_hit->normal = glm::vec3(0);
	out_hit->normal[near_axis] = (inv_dir[near_axis] > 0) ? -1.0f : 1.0f;

	return true;
}

bool
raycast_sphere(Ray &ray, glm::vec3 &position, float radius, RayHit *out_hit)
{

	glm::vec3 to_sphere = position - ray.origin;
	float	  proj = glm::dot(to_sphere, ray.direction);

	glm::vec3 closest = ray.origin + ray.direction * proj;
	float	  dist_sq = glm::dot(closest - position, closest - position);

	if (dist_sq > radius * radius)
	{
		return false;
	}

	float half_chord = sqrt(radius * radius - dist_sq);
	float t = proj - half_chord;

	if (t < 0 || t > ray.length)
	{
		return false;
	}

	out_hit->distance = t;
	out_hit->point = ray.origin + ray.direction * t;
	out_hit->normal = glm::normalize(out_hit->point - position);

	return true;
}

bool
raycast_obb(Ray &ray, OBB &obb, RayHit *out_hit)
{
	glm::vec3 to_obb = obb.center - ray.origin;
	float	  proj = glm::dot(to_obb, ray.direction);

	if (proj < -obb.bounds_radius)
	{
		return false;
	}

	if (proj > ray.length + obb.bounds_radius)
	{
		return false;
	}

	glm::vec3 closest = ray.origin + ray.direction * proj;
	float	  dist_sq = glm::dot(closest - obb.center, closest - obb.center);

	if (dist_sq >= obb.bounds_radius * obb.bounds_radius)
	{
		return false;
	}

	glm::mat3 rot_inv = glm::transpose(glm::mat3_cast(obb.rotation));
	Ray		  local_ray;
	local_ray.origin = rot_inv * (ray.origin - obb.center);
	local_ray.direction = rot_inv * ray.direction;
	local_ray.length = ray.length;

	AABB local_box = {-obb.half_extents, obb.half_extents};

	if (!raycast_aabb(local_ray, local_box, out_hit))
	{
		return false;
	}

	glm::mat3 rot = glm::mat3_cast(obb.rotation);
	out_hit->point = rot * out_hit->point + obb.center;
	out_hit->normal = rot * out_hit->normal;

	return true;
}

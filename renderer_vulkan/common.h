#pragma once

#include <glm/vec3.hpp>
#include <glm/common.hpp>
#include <vector>

struct RenderImage
{
	int width;
	int height;
	void* front;
	void* back;
	int length;
};

struct RenderParameters
{
	int width;
	int height;
	int depth;

	uint16_t* volumeData;

	double spacingX;
	double spacingY;
	double spacingZ;

	int windowCenter;
	int windowWidth;
};

struct Vec3 {
	float x;
	float y;
	float z;
};

struct SliceDisplayMapping {
	float centerU;
	float centerV;
	float halfU;
	float halfV;
};

struct SliceDesc {
	glm::vec3 origin;
	glm::vec3 axisU;
	glm::vec3 axisV;
	SliceDisplayMapping mapping;

	SliceDesc() {

	}

	SliceDesc(glm::vec3 o, glm::vec3 u, glm::vec3 v, SliceDisplayMapping m) {
		origin = o;
		axisU = u;
		axisV = v;
		mapping = m;
	}

	//void setOrigin(Vec3 o) {
	//	this->origin = glm::vec3(o.x, o.y, o.z);
	//}

	//void setAxisU(Vec3 u) {
	//	glm::vec3 axis(u.x, u.y, u.z);
	//	this->axisU = glm::normalize(axis);
	//}

	//void setAxisV(Vec3 v) {
	//	glm::vec3 axis(v.x, v.y, v.z);
	//	this->axisV = glm::normalize(axis);
	//}
};

struct Line2 {
	glm::vec3 p1;
	glm::vec3 p2;

	Line2(glm::vec3 p1, glm::vec3 p2) {
		this->p1 = p1;
		this->p2 = p2;
	}
};

struct AABB {
	glm::vec3 min;
	glm::vec3 max;

	static AABB generateAABB(std::vector<glm::vec3> points) {
		if (points.empty()) {
			return AABB{};
		}

		glm::vec3 min = points[0];
		glm::vec3 max = points[0];
		for (auto p : points)
		{
			min = glm::min(p, min);
			max = glm::max(p, max);
		}
		return AABB{ min, max };
	}
};

struct RGBA {
	float r;
	float g;
	float b;
	float a;

	RGBA():r(0),g(0),b(0),a(0)
	{ }

	RGBA(float r, float g, float b, float a)
		: r(r), g(g), b(b), a(a)
	{
	}
};

struct ColorPoint {
	float r;
	float g;
	float b;
	float gray;

	ColorPoint() :r(0), g(0), b(0), gray(0)
	{
	}

	ColorPoint(float r, float g, float b, float a)
		: r(r), g(g), b(b), gray(a)
	{
	}
};

struct AlphaPoint {
	float alpha;
	float gray;

	AlphaPoint(float alpha, float gray)
		: alpha(alpha), gray(gray)
	{
	}
};




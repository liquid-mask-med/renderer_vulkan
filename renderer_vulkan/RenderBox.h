#pragma once
#include <vector>
#include <glm/ext/vector_float3.hpp>
#include "common.h"

#define BOX_VERTEX_COUNT 24
#define BOX_INDEX_COUNT 36
#define BOX_EDGE_COUNT 12
#define BOX_POINTS_COUNT 8


class RenderBox
{
public:
	RenderBox();
	RenderBox(float x, float y, float z);

private:
	float length;
	float width;
	float height;

	float vertices[BOX_VERTEX_COUNT];
	unsigned int indices[BOX_INDEX_COUNT];

	std::vector<glm::vec3> points;
	std::vector<Line2> edges;

public:
	float* getVertices() { return vertices; }

	unsigned int* getIndices() { return indices; }

	const std::vector<glm::vec3>& getPoints() { return points; }
	const std::vector<Line2>& getEdges() { return edges; }

};


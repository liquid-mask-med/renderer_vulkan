#include "RenderBox.h"
#include <cstring>

static bool almostEqual(float a, float b, float eps = 1e-5f) {
	return std::abs(a - b) < eps;
}

//static bool samePoint(const glm::vec3& a, const glm::vec3& b) {
//	return almostEqual(a.x, b.x) && almostEqual(a.y, b.y) && almostEqual(a.z, b.z);
//}

static bool isBoxEdge(const glm::vec3& a, const glm::vec3& b) {
	int sameCount = 0;
	if (almostEqual(a.x, b.x)) {
		sameCount++;
	}
	if (almostEqual(a.y, b.y)) {
		sameCount++;
	}
	if (almostEqual(a.z, b.z)) {
		sameCount++;
	}

	return sameCount == 2;
}

RenderBox::RenderBox(float x, float y, float z)
{
	length = x;
	width = y;
	height = z;

	/*
	 *       4©¤©¤©¤©¤©¤©¤©¤5
	 *     ¨u©¦			 ¨u©¦
	 *    7©¤©¤©¤©¤©¤©¤©¤6 ©¦   Y
	 *    ©¦ 0©¤©¤©¤©¤©¤©¦©¤1   ¡ø  Z
	 *    ©¦¨u			©¦¨u    ©¦ ¨u
	 *    3©¤©¤©¤©¤©¤©¤©¤2      ©¸©¤> X
	 */

	float localVertices[] = {
		-x/2, -y/2, z/2,		//0
		x/2, -y/2, z/2,			//1
		x/2, -y/2, -z/2,		//2
		-x/2, -y/2, -z/2,		//3
		-x/2, y/2, z/2,			//4
		x/2, y/2, z/2,			//5
		x/2, y/2, -z/2,			//6
		-x/2, y/2, -z/2,		//7
	};

	unsigned int localIndices[] = {
		0,3,2,
		2,1,0,

		7,3,2,
		2,6,7,

		4,7,6,
		6,5,4,

		4,5,1,
		1,0,4,

		5,6,2,
		2,1,5,

		7,4,0,
		0,3,7
	};

	std::memcpy(this->vertices, localVertices, sizeof(localVertices));
	std::memcpy(this->indices, localIndices, sizeof(localIndices));

	points.clear();
	for (int i = 0; i < BOX_VERTEX_COUNT; i += 3) {
		points.push_back(glm::vec3(vertices[i], vertices[i + 1], vertices[i + 2]));
	}

	edges.clear();
	for (int i = 0; i < points.size(); i++) {
		for (int j = 0; j < points.size(); j++) {
			if (isBoxEdge(points[i], points[j])) {
				edges.push_back(Line2{ points[i], points[j] });
			}
		}
	}

	//std::vector<Line2> roughEdges;
	//for (int i = 0; i < BOX_POINTS_COUNT; i+=2) {
	//	roughEdges.push_back(Line2{ points[i], points[i + 1] });
	//}

	//std::vector<Line2> filteredEdges;
	//for (int i = 0; i < roughEdges.size(); i++) {
	//	Line2 line = roughEdges[i];
	//	if (line.p1.x != line.p2.x && line.p1.y != line.p2.y && line.p1.z != line.p2.z) {
	//		continue;
	//	}
	//	filteredEdges.push_back(line);
	//}

	//std::vector<Line2> finalFilteredEdges;
	//finalFilteredEdges.push_back(filteredEdges[0]);
	//for (int i=1; i<filteredEdges.size(); i++)
	//{
	//	Line2 line = filteredEdges[i];
	//	bool repeated = false;
	//	for (const Line2& e : finalFilteredEdges)
	//	{
	//		if (e.p1 == line.p2 && e.p2 == line.p1) {
	//			repeated = true;
	//		}
	//	}

	//	if(!repeated) {
	//		finalFilteredEdges.push_back(line);
	//	}
	//}

	//for (int i=0; i<finalFilteredEdges.size();i++)
	//{
	//	edges.push_back(finalFilteredEdges[i]);
	//}

}

RenderBox::RenderBox()
{

}


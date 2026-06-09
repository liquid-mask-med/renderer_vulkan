#pragma once
#include "common.h"
#include <array>
#include <algorithm>

class TransferFuction
{
public:
	inline static ColorPoint rgbTable[8] = {
		{ 0.0,            0.0,          0.0,           0.0 },
		{ 196.0 / 255.0, 25.0 / 255.0,  15.0 / 255.0,  1028.0 },
		{ 204.0 / 255.0, 0.0,           0.0,             1148.0 },
		{ 233.0 / 255.0, 185.0 / 255.0, 110.0 / 255.0, 1197.0 },
		{ 229.0 / 255.0, 185.0 / 255.0, 142.0 / 255.0, 1603.0 },
		{ 180.0 / 255.0, 180.0 / 255.0, 180.0 / 255.0, 2433.0 },
		{ 247.0 / 255.0, 247.0 / 255.0, 247.0 / 255.0, 3072.0 },
		{ 247.0 / 255.0, 247.0 / 255.0, 247.0 / 255.0, 4095.0 }
	};

	inline static AlphaPoint alphaTable[8] = {
		{0.0,                0.0 },
		{0.0,                1028.0 },
		{0.07853403,         1148.0 },
		{0.2670157,          1197.0 },
		{0.5759162306785583, 1603.0 },
		{1.0,                2433.0 },
		{1.0,                3072.0 },
		{1.0,                4095.0 }
	};

	static std::array<RGBA, 4096> BuildRGBA_LUT()
	{
		std::array<RGBA, 4096> lut{};
		const int maxIndex = 4095;

		for (int i = 0; i <= maxIndex; ++i)
		{
			float value = static_cast<float>(i);

			// 查找 rgbTable 对应区间
			int rIdx = 0;
			while (rIdx < 7 && !(value >= rgbTable[rIdx].gray && value <= rgbTable[rIdx + 1].gray))
			{
				++rIdx;
			}
			// 边界保护
			int rNext = std::min(rIdx + 1, 7);
			float x0 = rgbTable[rIdx].gray;
			float x1 = rgbTable[rNext].gray;
			float t = (x1 == x0) ? 0.0f : (value - x0) / (x1 - x0);

			float r = rgbTable[rIdx].r + t * (rgbTable[rNext].r - rgbTable[rIdx].r);
			float g = rgbTable[rIdx].g + t * (rgbTable[rNext].g - rgbTable[rIdx].g);
			float b = rgbTable[rIdx].b + t * (rgbTable[rNext].b - rgbTable[rIdx].b);

			// 查找 alphaTable 对应区间并插值 alpha
			int aIdx = 0;
			while (aIdx < 7 && !(value >= alphaTable[aIdx].gray && value <= alphaTable[aIdx + 1].gray))
			{
				++aIdx;
			}
			int aNext = std::min(aIdx + 1, 7);
			float xa0 = alphaTable[aIdx].gray;
			float xa1 = alphaTable[aNext].gray;
			float ta = (xa1 == xa0) ? 0.0f : (value - xa0) / (xa1 - xa0);

			float a = alphaTable[aIdx].alpha + ta * (alphaTable[aNext].alpha - alphaTable[aIdx].alpha);

			lut[i] = { r, g, b, a };
		}

		return lut;
	}
};


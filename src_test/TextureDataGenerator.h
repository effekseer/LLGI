#pragma once

#include "test.h"

#include <stdint.h>
#include <vector>

class TextureDataGenerator
{
public:
	static std::vector<uint8_t> CreateDummyTextureData(LLGI::Vec2I size, LLGI::TextureFormatType format, int32_t mipLevelCount = 1);

	static void WriteDummyTexture(LLGI::Texture* texture);

	static void WriteDummyTexture(LLGI::Color8* data, LLGI::Vec2I size);
};

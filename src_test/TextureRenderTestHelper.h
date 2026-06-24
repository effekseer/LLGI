#pragma once

#include "RenderTestHelper.h"

struct TextureFormatRenderTestCase
{
	const char* Name;
	LLGI::TextureFormatType Format;
	int32_t MipLevelCount;
};

bool IsTextureFormatRenderTestSupported(LLGI::DeviceType deviceType, LLGI::TextureFormatType format);

LLGI::Texture* CreateRawDataTexture(LLGI::Graphics* graphics, const TextureFormatRenderTestCase& testCase);

void RunTextureFormatRawDataScreenRenderTest(LLGI::DeviceType deviceType, const TextureFormatRenderTestCase& testCase);

void RunTextureFormatRawDataOffscreenRenderTest(LLGI::DeviceType deviceType, const TextureFormatRenderTestCase& testCase);

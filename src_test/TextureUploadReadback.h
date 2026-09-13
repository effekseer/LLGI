#pragma once

#include "test.h"
#include <vector>

// Test-only readback of one RGBA8 Z plane or array layer at the requested mip.
std::vector<uint8_t> ReadTextureUploadPlane(LLGI::Graphics* graphics,
										  LLGI::DeviceType device,
										  LLGI::Texture* texture,
										  int32_t mip,
										  int32_t layer,
										  const LLGI::Vec2I& size,
										  bool isArray);

#ifdef ENABLE_METAL
std::vector<uint8_t> ReadTextureUploadPlaneMetal(
	LLGI::Texture* texture, int32_t mip, int32_t layer, const LLGI::Vec2I& size, bool isArray);
#endif

#ifdef ENABLE_WEBGPU
std::vector<uint8_t> ReadTextureUploadPlaneWebGPU(
	LLGI::Graphics* graphics, LLGI::Texture* texture, int32_t mip, int32_t layer, const LLGI::Vec2I& size);
#endif


#pragma once

#include "../LLGI.Shader.h"
#include "../LLGI.CommandList.h"
#include "LLGI.BaseDX12.h"
#include "LLGI.GraphicsDX12.h"

using namespace DirectX;

namespace LLGI
{

class ShaderDX12 : public Shader
{
private:
	std::vector<uint8_t> data_;
	std::array<bool, NumComputeBuffer> byteAddressSRVs_ = {};
	std::array<bool, NumComputeBuffer> byteAddressUAVs_ = {};

public:
	ShaderDX12() = default;
	~ShaderDX12() override = default;

	bool Initialize(DataStructure* data, int32_t count);

	const std::vector<uint8_t>& GetData() { return data_; }
	const std::array<bool, NumComputeBuffer>& GetByteAddressSRVs() const { return byteAddressSRVs_; }
	const std::array<bool, NumComputeBuffer>& GetByteAddressUAVs() const { return byteAddressUAVs_; }
};

} // namespace LLGI

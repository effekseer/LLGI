#include "LLGI.CompilerWebGPU.h"

#include <cstring>

namespace LLGI
{
void CompilerWebGPU::Compile(CompilerResult& result, const char* code, ShaderStageType shaderStage)
{
	const size_t sourceSize = std::strlen(code) + 1;
	result.Binary.resize(1);
	auto& binary = result.Binary.front();
	binary.resize(WgslCodeHeader.size() + sourceSize);
	std::memcpy(binary.data(), WgslCodeHeader.data(), WgslCodeHeader.size());
	std::memcpy(binary.data() + WgslCodeHeader.size(), code, sourceSize);
}
} // namespace LLGI

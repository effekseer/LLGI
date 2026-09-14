
#include "LLGI.PipelineState.h"
#include "LLGI.Graphics.h"
#include <limits>

namespace LLGI
{

PipelineState::PipelineState()
{
	VertexLayoutSemantics.fill(0);
	VertexLayoutOffsets.fill(-1);
	VertexLayoutLocations.fill(-1);
}

void PipelineState::SetShader(ShaderStageType stage, Shader* shader) {}

RenderPassPipelineState* PipelineState::GetRenderPassPipelineState() const { return renderPassPipelineState_.get(); }

void PipelineState::SetRenderPassPipelineState(RenderPassPipelineState* renderPassPipelineState)
{
	SafeAddRef(renderPassPipelineState);
	renderPassPipelineState_ = CreateSharedPtr(renderPassPipelineState);
}

bool PipelineState::Compile() { return false; }

bool PipelineState::ValidateVertexLayout() const
{
	if (VertexLayoutCount < 0 || VertexLayoutCount > VertexLayoutMax || VertexBufferStride < 0) return false;
	std::array<bool, VertexLayoutMax> locations{};
	int64_t offset = 0;
	for (int32_t i = 0; i < VertexLayoutCount; ++i)
	{
		if (VertexLayoutOffsets[i] < -1 || VertexLayoutLocations[i] < -1) return false;
		const int32_t location = VertexLayoutLocations[i] >= 0 ? VertexLayoutLocations[i] : i;
		if (location >= VertexLayoutMax || locations[location]) return false;
		locations[location] = true;
		if (VertexLayoutOffsets[i] >= 0) offset = VertexLayoutOffsets[i];
		int32_t size = 0;
		switch (VertexLayouts[i])
		{
		case VertexLayoutFormat::R32_FLOAT:
		case VertexLayoutFormat::R8G8B8A8_UNORM:
		case VertexLayoutFormat::R8G8B8A8_UINT:
		case VertexLayoutFormat::R16G16_UNORM: size = 4; break;
		case VertexLayoutFormat::R32G32_FLOAT: size = 8; break;
		case VertexLayoutFormat::R32G32B32_FLOAT: size = 12; break;
		case VertexLayoutFormat::R32G32B32A32_FLOAT: size = 16; break;
		default: return false;
		}
		offset += size;
		if (offset > std::numeric_limits<int32_t>::max() || (VertexBufferStride > 0 && offset > VertexBufferStride)) return false;
	}
	return true;
}

} // namespace LLGI

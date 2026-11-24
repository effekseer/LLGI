#include "LLGI.BaseWebGPU.h"

namespace LLGI
{

wgpu::BlendOperation Convert(BlendEquationType type)
{
	if (type == BlendEquationType::Add)
		return wgpu::BlendOperation::Add;
	if (type == BlendEquationType::Max)
		return wgpu::BlendOperation::Max;
	if (type == BlendEquationType::Min)
		return wgpu::BlendOperation::Min;
	if (type == BlendEquationType::ReverseSub)
		return wgpu::BlendOperation::ReverseSubtract;
	if (type == BlendEquationType::Sub)
		return wgpu::BlendOperation::Subtract;

	throw "Not implemented";
}

wgpu::BlendFactor Convert(BlendFuncType type)
{
	if (type == BlendFuncType::Zero)
		return wgpu::BlendFactor::Zero;
	if (type == BlendFuncType::One)
		return wgpu::BlendFactor::One;
	if (type == BlendFuncType::SrcColor)
		return wgpu::BlendFactor::Src;
	if (type == BlendFuncType::OneMinusSrcColor)
		return wgpu::BlendFactor::OneMinusSrc;
	if (type == BlendFuncType::SrcAlpha)
		return wgpu::BlendFactor::SrcAlpha;
	if (type == BlendFuncType::OneMinusSrcAlpha)
		return wgpu::BlendFactor::OneMinusSrcAlpha;
	if (type == BlendFuncType::DstAlpha)
		return wgpu::BlendFactor::DstAlpha;
	if (type == BlendFuncType::OneMinusDstAlpha)
		return wgpu::BlendFactor::OneMinusDstAlpha;
	if (type == BlendFuncType::DstColor)
		return wgpu::BlendFactor::Dst;
	if (type == BlendFuncType::OneMinusDstColor)
		return wgpu::BlendFactor::OneMinusDst;

	throw "Not implemented";
}

wgpu::PrimitiveTopology Convert(TopologyType type)
{
	if (type == TopologyType::Point)
		return wgpu::PrimitiveTopology::PointList;

	if (type == TopologyType::Line)
		return wgpu::PrimitiveTopology::LineList;

	if (type == TopologyType::Triangle)
		return wgpu::PrimitiveTopology::TriangleList;

	throw "Not implemented";
}

namespace
{
template <typename Compare> wgpu::CompareFunction ConvertComparison(Compare type)
{
	switch (type)
	{
	case Compare::Never:
		return wgpu::CompareFunction::Never;
	case Compare::Less:
		return wgpu::CompareFunction::Less;
	case Compare::Equal:
		return wgpu::CompareFunction::Equal;
	case Compare::LessEqual:
		return wgpu::CompareFunction::LessEqual;
	case Compare::Greater:
		return wgpu::CompareFunction::Greater;
	case Compare::NotEqual:
		return wgpu::CompareFunction::NotEqual;
	case Compare::GreaterEqual:
		return wgpu::CompareFunction::GreaterEqual;
	case Compare::Always:
		return wgpu::CompareFunction::Always;
	}
	throw "Not implemented";
}
} // namespace

wgpu::CompareFunction Convert(CompareFuncType type) { return ConvertComparison(type); }
wgpu::CompareFunction Convert(DepthFuncType type) { return ConvertComparison(type); }

wgpu::CullMode Convert(CullingMode mode)
{
	if (mode == CullingMode::Clockwise)
		return wgpu::CullMode::Back;

	if (mode == CullingMode::CounterClockwise)
		return wgpu::CullMode::Front;

	if (mode == CullingMode::DoubleSide)
		return wgpu::CullMode::None;

	throw "Not implemented";
}

wgpu::VertexFormat Convert(VertexLayoutFormat format)
{
	if (format == VertexLayoutFormat::R32_FLOAT)
		return wgpu::VertexFormat::Float32;

	if (format == VertexLayoutFormat::R32G32_FLOAT)
		return wgpu::VertexFormat::Float32x2;

	if (format == VertexLayoutFormat::R32G32B32_FLOAT)
		return wgpu::VertexFormat::Float32x3;

	if (format == VertexLayoutFormat::R32G32B32A32_FLOAT)
		return wgpu::VertexFormat::Float32x4;

	if (format == VertexLayoutFormat::R8G8B8A8_UNORM)
		return wgpu::VertexFormat::Unorm8x4;

	if (format == VertexLayoutFormat::R8G8B8A8_UINT)
		return wgpu::VertexFormat::Uint8x4;

	if (format == VertexLayoutFormat::R16G16_UNORM)
		return wgpu::VertexFormat::Unorm16x2;

	throw "Not implemented";
}

wgpu::StencilOperation Convert(StencilOperatorType type)
{
	if (type == StencilOperatorType::Keep)
		return wgpu::StencilOperation::Keep;

	if (type == StencilOperatorType::Zero)
		return wgpu::StencilOperation::Zero;

	if (type == StencilOperatorType::Replace)
		return wgpu::StencilOperation::Replace;

	if (type == StencilOperatorType::Invert)
		return wgpu::StencilOperation::Invert;

	if (type == StencilOperatorType::IncClamp)
		return wgpu::StencilOperation::IncrementClamp;

	if (type == StencilOperatorType::DecClamp)
		return wgpu::StencilOperation::DecrementClamp;

	if (type == StencilOperatorType::IncRepeat)
		return wgpu::StencilOperation::IncrementWrap;

	if (type == StencilOperatorType::DecRepeat)
		return wgpu::StencilOperation::DecrementWrap;

	throw "Not implemented";
}

namespace
{
constexpr std::pair<TextureFormatType, wgpu::TextureFormat> TextureFormats[] = {
	{TextureFormatType::R8G8B8A8_UNORM, wgpu::TextureFormat::RGBA8Unorm},
	{TextureFormatType::B8G8R8A8_UNORM, wgpu::TextureFormat::BGRA8Unorm},
	{TextureFormatType::RG11B10_UFLOAT, wgpu::TextureFormat::RG11B10Ufloat},
	{TextureFormatType::R16G16B16A16_FLOAT, wgpu::TextureFormat::RGBA16Float},
	{TextureFormatType::R32G32B32A32_FLOAT, wgpu::TextureFormat::RGBA32Float},
	{TextureFormatType::R8G8B8A8_UNORM_SRGB, wgpu::TextureFormat::RGBA8UnormSrgb},
	{TextureFormatType::B8G8R8A8_UNORM_SRGB, wgpu::TextureFormat::BGRA8UnormSrgb},
	{TextureFormatType::R16_FLOAT, wgpu::TextureFormat::R16Float},
	{TextureFormatType::R32_FLOAT, wgpu::TextureFormat::R32Float},
	{TextureFormatType::R32G32_FLOAT, wgpu::TextureFormat::RG32Float},
	{TextureFormatType::R16G16_FLOAT, wgpu::TextureFormat::RG16Float},
	{TextureFormatType::R8_UNORM, wgpu::TextureFormat::R8Unorm},
	{TextureFormatType::BC1, wgpu::TextureFormat::BC1RGBAUnorm},
	{TextureFormatType::BC2, wgpu::TextureFormat::BC2RGBAUnorm},
	{TextureFormatType::BC3, wgpu::TextureFormat::BC3RGBAUnorm},
	{TextureFormatType::BC7, wgpu::TextureFormat::BC7RGBAUnorm},
	{TextureFormatType::BC1_SRGB, wgpu::TextureFormat::BC1RGBAUnormSrgb},
	{TextureFormatType::BC2_SRGB, wgpu::TextureFormat::BC2RGBAUnormSrgb},
	{TextureFormatType::BC3_SRGB, wgpu::TextureFormat::BC3RGBAUnormSrgb},
	{TextureFormatType::BC7_SRGB, wgpu::TextureFormat::BC7RGBAUnormSrgb},
	{TextureFormatType::D32, wgpu::TextureFormat::Depth32Float},
	{TextureFormatType::D24S8, wgpu::TextureFormat::Depth24PlusStencil8},
	{TextureFormatType::D32S8, wgpu::TextureFormat::Depth32FloatStencil8},
	{TextureFormatType::Unknown, wgpu::TextureFormat::Undefined},
};
} // namespace

wgpu::TextureFormat ConvertFormat(TextureFormatType format)
{
	for (const auto& [llgi, webgpu] : TextureFormats)
		if (llgi == format)
			return webgpu;
	throw "Not implemented";
}

TextureFormatType ConvertFormat(wgpu::TextureFormat format)
{
	for (const auto& [llgi, webgpu] : TextureFormats)
		if (webgpu == format)
			return llgi;
	throw "Not implemented";
}

wgpu::Extent3D GetTextureCopyExtent(TextureFormatType format, Vec3I size)
{
	// Copies of compressed tail mips cover whole blocks, even when the logical mip is smaller.
	if (IsBlockCompressedFormat(format))
	{
		size.X = (size.X + 3) / 4 * 4;
		size.Y = (size.Y + 3) / 4 * 4;
	}
	return {static_cast<uint32_t>(size.X), static_cast<uint32_t>(size.Y), static_cast<uint32_t>(size.Z)};
}

int32_t GetSize(VertexLayoutFormat format)
{
	if (format == VertexLayoutFormat::R32G32B32_FLOAT)
	{
		return sizeof(float) * 3;
	}
	else if (format == VertexLayoutFormat::R32G32B32A32_FLOAT)
	{
		return sizeof(float) * 4;
	}
	else if (format == VertexLayoutFormat::R32_FLOAT)
	{
		return sizeof(float) * 1;
	}
	else if (format == VertexLayoutFormat::R32G32_FLOAT)
	{
		return sizeof(float) * 2;
	}
	else if (format == VertexLayoutFormat::R8G8B8A8_UINT || format == VertexLayoutFormat::R8G8B8A8_UNORM ||
			 format == VertexLayoutFormat::R16G16_UNORM)
	{
		return 4;
	}
	else
	{
		Log(LogType::Error, "Unimplemented VertexLoayoutFormat");
		return 0;
	}
}

} // namespace LLGI

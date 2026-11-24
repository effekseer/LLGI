#include "TestHelper.h"
#include "TextureDataGenerator.h"
#include "TextureRenderTestHelper.h"
#include "TextureUploadReadback.h"
#include "test.h"

#include <Utils/LLGI.CommandListPool.h>
#include <WebGPU/LLGI.BufferWebGPU.h>
#include <WebGPU/LLGI.CommandListWebGPU.h>
#include <WebGPU/LLGI.GraphicsWebGPU.h>
#include <WebGPU/LLGI.PipelineStateWebGPU.h>
#include <WebGPU/LLGI.RenderPassWebGPU.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <numeric>

namespace
{
struct BrowserComputeInput
{
	float value1;
	float value2;
};

struct BrowserComputeOutput
{
	float value;
};

struct BrowserConstant
{
	float values[4];
};

void configureSimpleVertexLayout(LLGI::PipelineState* pipelineState)
{
	pipelineState->VertexLayouts[0] = LLGI::VertexLayoutFormat::R32G32B32_FLOAT;
	pipelineState->VertexLayouts[1] = LLGI::VertexLayoutFormat::R32G32_FLOAT;
	pipelineState->VertexLayouts[2] = LLGI::VertexLayoutFormat::R8G8B8A8_UNORM;
	pipelineState->VertexLayoutNames[0] = "POSITION";
	pipelineState->VertexLayoutNames[1] = "UV";
	pipelineState->VertexLayoutNames[2] = "COLOR";
	pipelineState->VertexLayoutCount = 3;
}

void test_webgpu_browser_offscreen_render(LLGI::DeviceType deviceType)
{
	VERIFY(deviceType == LLGI::DeviceType::WebGPU);

	LLGI::PlatformParameter pp;
	pp.Device = deviceType;
	pp.WaitVSync = false;

	auto platform = LLGI::CreateSharedPtr(LLGI::CreatePlatform(pp, nullptr));
	VERIFY(platform != nullptr);

	auto graphics = LLGI::CreateSharedPtr(platform->CreateGraphics());
	VERIFY(graphics != nullptr);

	LLGI::RenderTextureInitializationParameter renderTextureParam;
	renderTextureParam.Size = LLGI::Vec2I(64, 64);
	renderTextureParam.Format = LLGI::TextureFormatType::R8G8B8A8_UNORM;
	auto renderTexture = LLGI::CreateSharedPtr(graphics->CreateRenderTexture(renderTextureParam));
	VERIFY(renderTexture != nullptr);

	auto renderPass = LLGI::CreateSharedPtr(graphics->CreateRenderPass(renderTexture.get(), nullptr, nullptr, nullptr));
	VERIFY(renderPass != nullptr);
	renderPass->SetClearColor(LLGI::Color8(32, 64, 96, 255));
	renderPass->SetIsColorCleared(true);

	std::shared_ptr<LLGI::Shader> shaderVS = nullptr;
	std::shared_ptr<LLGI::Shader> shaderPS = nullptr;
	TestHelper::CreateShader(graphics.get(), deviceType, "simple_rectangle.vert", "simple_rectangle.frag", shaderVS, shaderPS);
	VERIFY(shaderVS != nullptr);
	VERIFY(shaderPS != nullptr);

	std::shared_ptr<LLGI::Buffer> vertexBuffer;
	std::shared_ptr<LLGI::Buffer> indexBuffer;
	TestHelper::CreateRectangle(graphics.get(),
								LLGI::Vec3F(-0.5f, 0.5f, 0.5f),
								LLGI::Vec3F(0.5f, -0.5f, 0.5f),
								LLGI::Color8(255, 255, 255, 255),
								LLGI::Color8(0, 255, 0, 255),
								vertexBuffer,
								indexBuffer);
	VERIFY(vertexBuffer != nullptr);
	VERIFY(indexBuffer != nullptr);

	auto renderPassPipelineState = LLGI::CreateSharedPtr(graphics->CreateRenderPassPipelineState(renderPass.get()));
	VERIFY(renderPassPipelineState != nullptr);

	auto pipelineState = LLGI::CreateSharedPtr(graphics->CreatePiplineState());
	VERIFY(pipelineState != nullptr);
	configureSimpleVertexLayout(pipelineState.get());
	pipelineState->SetShader(LLGI::ShaderStageType::Vertex, shaderVS.get());
	pipelineState->SetShader(LLGI::ShaderStageType::Pixel, shaderPS.get());
	pipelineState->SetRenderPassPipelineState(renderPassPipelineState.get());
	VERIFY(pipelineState->Compile());

	auto sfMemoryPool = LLGI::CreateSharedPtr(graphics->CreateSingleFrameMemoryPool(1024 * 1024, 16));
	VERIFY(sfMemoryPool != nullptr);
	sfMemoryPool->NewFrame();

	auto commandList = LLGI::CreateSharedPtr(graphics->CreateCommandList(sfMemoryPool.get()));
	VERIFY(commandList != nullptr);
	const std::array<uint32_t, 3> indices = {0, 1, 2};
	auto wideIndices =
		LLGI::CreateSharedPtr(graphics->CreateBuffer(LLGI::BufferUsageType::Index | LLGI::BufferUsageType::MapWrite, sizeof(indices)));
	VERIFY(wideIndices != nullptr);
	std::memcpy(wideIndices->Lock(), indices.data(), sizeof(indices));
	wideIndices->Unlock();

	auto verifyTriangle = [&]()
	{
		const auto pixels = graphics->CaptureRenderTarget(renderTexture.get());
		VERIFY(pixels.size() == 64 * 64 * 4);
		VERIFY(pixels[(22 * 64 + 42) * 4 + 1] == 255);
	};

	commandList->Begin();
	commandList->BeginRenderPass(renderPass.get());
	commandList->SetVertexBuffer(vertexBuffer.get(), sizeof(SimpleVertex), 0);
	commandList->SetPipelineState(pipelineState.get());
	// The same bytes encode a degenerate triangle as uint16 and a visible one as uint32.
	commandList->SetIndexBuffer(wideIndices.get(), 2);
	commandList->Draw(1);
	commandList->SetIndexBuffer(wideIndices.get(), 4);
	commandList->Draw(1);
	commandList->EndRenderPass();
	commandList->End();
	graphics->Execute(commandList.get());
	verifyTriangle();

	auto* web = static_cast<LLGI::GraphicsWebGPU*>(graphics.get());
	auto encoder = web->GetDevice().CreateCommandEncoder();
	auto retainedEncoder = encoder;
	VERIFY(commandList->BeginWithPlatform(retainedEncoder.MoveToCHandle()));
	auto* webRenderPass = static_cast<LLGI::RenderPassWebGPU*>(renderPass.get());
	webRenderPass->RefreshDescriptor();
	auto pass = encoder.BeginRenderPass(&webRenderPass->GetDescriptor());
	auto retainedPass = pass;
	VERIFY(commandList->BeginRenderPassWithPlatformPtr(retainedPass.MoveToCHandle()));
	commandList->SetVertexBuffer(vertexBuffer.get(), sizeof(SimpleVertex), 0);
	commandList->SetIndexBuffer(indexBuffer.get(), 2);
	commandList->SetPipelineState(pipelineState.get());
	commandList->SetScissor(0, 0, 1, 1);
	commandList->Draw(2);
	pass.SetIndexBuffer(static_cast<LLGI::BufferWebGPU*>(wideIndices.get())->GetBuffer(), wgpu::IndexFormat::Uint16);
	commandList->SetScissor(0, 0, 64, 64);
	commandList->Draw(1);
	VERIFY(commandList->EndRenderPassWithPlatformPtr());
	pass.End();
	commandList->EndWithPlatform();
	auto buffer = encoder.Finish();
	web->GetQueue().Submit(1, &buffer);
	verifyTriangle();
}

void test_webgpu_browser_texture_and_constant_render(LLGI::DeviceType deviceType)
{
	VERIFY(deviceType == LLGI::DeviceType::WebGPU);

	LLGI::PlatformParameter pp;
	pp.Device = deviceType;
	pp.WaitVSync = false;

	auto platform = LLGI::CreateSharedPtr(LLGI::CreatePlatform(pp, nullptr));
	VERIFY(platform != nullptr);

	auto graphics = LLGI::CreateSharedPtr(platform->CreateGraphics());
	VERIFY(graphics != nullptr);

	LLGI::TextureInitializationParameter textureParam;
	textureParam.Size = LLGI::Vec2I(256, 256);
	textureParam.Format = LLGI::TextureFormatType::R8G8B8A8_UNORM;
	auto texture = LLGI::CreateSharedPtr(graphics->CreateTexture(textureParam));
	VERIFY(texture != nullptr);
	TextureDataGenerator::WriteDummyTexture(texture.get());

	LLGI::RenderTextureInitializationParameter renderTextureParam;
	renderTextureParam.Size = LLGI::Vec2I(128, 128);
	renderTextureParam.Format = LLGI::TextureFormatType::R8G8B8A8_UNORM;
	auto renderTexture = LLGI::CreateSharedPtr(graphics->CreateRenderTexture(renderTextureParam));
	VERIFY(renderTexture != nullptr);

	auto renderPass = LLGI::CreateSharedPtr(graphics->CreateRenderPass(renderTexture.get(), nullptr, nullptr, nullptr));
	VERIFY(renderPass != nullptr);
	renderPass->SetClearColor(LLGI::Color8(8, 16, 24, 255));
	renderPass->SetIsColorCleared(true);

	std::shared_ptr<LLGI::Shader> textureVS = nullptr;
	std::shared_ptr<LLGI::Shader> texturePS = nullptr;
	TestHelper::CreateShader(graphics.get(), deviceType, "simple_texture_rectangle.vert", "simple_texture_rectangle.frag", textureVS, texturePS);
	VERIFY(textureVS != nullptr);
	VERIFY(texturePS != nullptr);

	std::shared_ptr<LLGI::Shader> constantVS = nullptr;
	std::shared_ptr<LLGI::Shader> constantPS = nullptr;
	TestHelper::CreateShader(graphics.get(), deviceType, "simple_constant_rectangle.vert", "simple_constant_rectangle.frag", constantVS, constantPS);
	VERIFY(constantVS != nullptr);
	VERIFY(constantPS != nullptr);

	std::shared_ptr<LLGI::Buffer> vertexBuffer;
	std::shared_ptr<LLGI::Buffer> indexBuffer;
	TestHelper::CreateRectangle(graphics.get(),
								LLGI::Vec3F(-0.8f, 0.8f, 0.5f),
								LLGI::Vec3F(0.8f, -0.8f, 0.5f),
								LLGI::Color8(255, 255, 255, 255),
								LLGI::Color8(0, 255, 0, 255),
								vertexBuffer,
								indexBuffer);
	VERIFY(vertexBuffer != nullptr);
	VERIFY(indexBuffer != nullptr);

	auto renderPassPipelineState = LLGI::CreateSharedPtr(graphics->CreateRenderPassPipelineState(renderPass.get()));
	VERIFY(renderPassPipelineState != nullptr);

	auto texturePipeline = LLGI::CreateSharedPtr(graphics->CreatePiplineState());
	VERIFY(texturePipeline != nullptr);
	configureSimpleVertexLayout(texturePipeline.get());
	texturePipeline->SetShader(LLGI::ShaderStageType::Vertex, textureVS.get());
	texturePipeline->SetShader(LLGI::ShaderStageType::Pixel, texturePS.get());
	texturePipeline->SetRenderPassPipelineState(renderPassPipelineState.get());
	VERIFY(texturePipeline->Compile());

	auto constantPipeline = LLGI::CreateSharedPtr(graphics->CreatePiplineState());
	VERIFY(constantPipeline != nullptr);
	configureSimpleVertexLayout(constantPipeline.get());
	constantPipeline->SetShader(LLGI::ShaderStageType::Vertex, constantVS.get());
	constantPipeline->SetShader(LLGI::ShaderStageType::Pixel, constantPS.get());
	constantPipeline->SetRenderPassPipelineState(renderPassPipelineState.get());
	VERIFY(constantPipeline->Compile());

	auto vertexConstantBuffer = LLGI::CreateSharedPtr(
		graphics->CreateBuffer(LLGI::BufferUsageType::Constant | LLGI::BufferUsageType::MapWrite, sizeof(BrowserConstant)));
	auto pixelConstantBuffer = LLGI::CreateSharedPtr(
		graphics->CreateBuffer(LLGI::BufferUsageType::Constant | LLGI::BufferUsageType::MapWrite, sizeof(BrowserConstant)));
	VERIFY(vertexConstantBuffer != nullptr);
	VERIFY(pixelConstantBuffer != nullptr);

	{
		auto data = static_cast<BrowserConstant*>(vertexConstantBuffer->Lock());
		data->values[0] = 0.15f;
		data->values[1] = -0.10f;
		data->values[2] = 0.0f;
		data->values[3] = 0.0f;
		vertexConstantBuffer->Unlock();
	}
	{
		auto data = static_cast<BrowserConstant*>(pixelConstantBuffer->Lock());
		data->values[0] = 0.05f;
		data->values[1] = 0.10f;
		data->values[2] = 0.15f;
		data->values[3] = 0.0f;
		pixelConstantBuffer->Unlock();
	}

	auto sfMemoryPool = LLGI::CreateSharedPtr(graphics->CreateSingleFrameMemoryPool(1024 * 1024, 16));
	VERIFY(sfMemoryPool != nullptr);
	sfMemoryPool->NewFrame();

	auto commandList = LLGI::CreateSharedPtr(graphics->CreateCommandList(sfMemoryPool.get()));
	VERIFY(commandList != nullptr);
	commandList->Begin();
	commandList->BeginRenderPass(renderPass.get());
	commandList->SetVertexBuffer(vertexBuffer.get(), sizeof(SimpleVertex), 0);
	commandList->SetIndexBuffer(indexBuffer.get(), 2);
	commandList->SetPipelineState(texturePipeline.get());
	commandList->SetTexture(texture.get(), LLGI::TextureWrapMode::Clamp, LLGI::TextureMinMagFilter::Linear, 0);
	commandList->Draw(2);
	commandList->ResetTextures();
	commandList->SetPipelineState(constantPipeline.get());
	commandList->SetConstantBuffer(vertexConstantBuffer.get(), 0);
	commandList->SetConstantBuffer(pixelConstantBuffer.get(), 1);
	commandList->Draw(2);
	commandList->EndRenderPass();
	commandList->End();

	graphics->Execute(commandList.get());
	graphics->WaitFinish();

	const auto data = graphics->CaptureRenderTarget(renderTexture.get());
	VERIFY(data.size() == 128 * 128 * 4);

	const auto verifyPixel = [&data](int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
		const auto* pixel = data.data() + (x + y * 128) * 4;
		const bool matched = std::abs(static_cast<int>(pixel[0]) - static_cast<int>(r)) <= 1 &&
							 std::abs(static_cast<int>(pixel[1]) - static_cast<int>(g)) <= 1 &&
							 std::abs(static_cast<int>(pixel[2]) - static_cast<int>(b)) <= 1 &&
							 std::abs(static_cast<int>(pixel[3]) - static_cast<int>(a)) <= 1;
		if (!matched)
		{
			std::cout << "Pixel mismatch at " << x << "," << y << " actual=" << static_cast<int>(pixel[0]) << ","
					  << static_cast<int>(pixel[1]) << "," << static_cast<int>(pixel[2]) << "," << static_cast<int>(pixel[3])
					  << " expected=" << static_cast<int>(r) << "," << static_cast<int>(g) << "," << static_cast<int>(b) << ","
					  << static_cast<int>(a) << std::endl;
		}
		VERIFY(matched);
	};

	verifyPixel(64, 64, 155, 255, 188, 255);
	verifyPixel(2, 2, 8, 16, 24, 255);
}

void test_webgpu_browser_texture_format_raw_data(LLGI::DeviceType deviceType, const TextureFormatRenderTestCase& testCase)
{
	VERIFY(deviceType == LLGI::DeviceType::WebGPU);
	RunTextureFormatRawDataOffscreenRenderTest(deviceType, testCase);
}

void test_webgpu_browser_compressed_array_readback(LLGI::DeviceType deviceType)
{
	LLGI::PlatformParameter pp;
	pp.Device = deviceType;
	auto platform = LLGI::CreateSharedPtr(LLGI::CreatePlatform(pp, nullptr));
	VERIFY(platform);
	auto graphics = LLGI::CreateSharedPtr(platform->CreateGraphics());
	VERIFY(graphics);
	if (!static_cast<LLGI::GraphicsWebGPU*>(graphics.get())->GetDevice().HasFeature(wgpu::FeatureName::TextureCompressionBC))
	{
		std::cout << "Skipping compressed readback: texture-compression-bc unavailable." << std::endl;
		return;
	}
	LLGI::TextureParameter desc;
	desc.Usage = LLGI::TextureUsageType::Array;
	desc.Size = {12, 8, 2};
	desc.Format = LLGI::TextureFormatType::BC1;
	auto texture = LLGI::CreateSharedPtr(graphics->CreateTexture(desc));
	VERIFY(texture);

	// Each layer occupies two rows of three 8-byte blocks.
	std::vector<uint8_t> expected(2 * 2 * 3 * 8);
	std::iota(expected.begin(), expected.end(), uint8_t{0});
	auto* destination = texture->Lock();
	VERIFY(destination);
	std::memcpy(destination, expected.data(), expected.size());
	texture->Unlock();
	std::vector<uint8_t> actual;
	VERIFY(texture->GetData(actual) && actual == expected);
}

void test_webgpu_browser_compute_compile(LLGI::DeviceType deviceType)
{
	VERIFY(deviceType == LLGI::DeviceType::WebGPU);

	LLGI::PlatformParameter pp;
	pp.Device = deviceType;
	pp.WaitVSync = false;

	auto platform = LLGI::CreateSharedPtr(LLGI::CreatePlatform(pp, nullptr));
	VERIFY(platform != nullptr);

	auto graphics = LLGI::CreateSharedPtr(platform->CreateGraphics());
	VERIFY(graphics != nullptr);

	std::shared_ptr<LLGI::Shader> shaderCS = nullptr;
	TestHelper::CreateComputeShader(graphics.get(), deviceType, "basic.comp", shaderCS);
	VERIFY(shaderCS != nullptr);

	auto pipelineState = LLGI::CreateSharedPtr(graphics->CreatePiplineState());
	VERIFY(pipelineState != nullptr);
	auto* pipeline = static_cast<LLGI::PipelineStateWebGPU*>(pipelineState.get());
	std::shared_ptr<LLGI::Shader> vertex, fragment;
	TestHelper::CreateShader(
		graphics.get(), deviceType, "simple_texture_rectangle.vert", "simple_texture_rectangle.frag", vertex, fragment);
	VERIFY(vertex && fragment);
	LLGI::RenderPassPipelineStateKey key;
	key.RenderTargetFormats.resize(1);
	key.RenderTargetFormats.at(0) = LLGI::TextureFormatType::R8G8B8A8_UNORM;
	auto renderPassState = LLGI::CreateSharedPtr(graphics->CreateRenderPassPipelineState(key));
	VERIFY(renderPassState != nullptr);
	configureSimpleVertexLayout(pipeline);
	pipeline->SetRenderPassPipelineState(renderPassState.get());
	pipeline->SetShader(LLGI::ShaderStageType::Vertex, vertex.get());
	pipeline->SetShader(LLGI::ShaderStageType::Pixel, fragment.get());
	VERIFY(pipeline->Compile());
	VERIFY(pipeline->HasBinding(1, 0, LLGI::ShaderResourceType::Texture));

	pipeline->SetShader(LLGI::ShaderStageType::Compute, shaderCS.get());
	VERIFY(pipeline->Compile());
	VERIFY(!pipeline->GetRenderPipeline() && pipeline->GetComputePipeline());
	VERIFY(!pipeline->HasBinding(1, 0, LLGI::ShaderResourceType::Texture));
	VERIFY(pipeline->HasBinding(2, 0, LLGI::ShaderResourceType::StorageBuffer, LLGI::ShaderResourceAccess::ReadWrite));

	pipeline->SetShader(LLGI::ShaderStageType::Compute, nullptr);
	pipeline->BlendEquationRGB = LLGI::BlendEquationType::Min;
	pipeline->BlendEquationAlpha = LLGI::BlendEquationType::Max;
	VERIFY(pipeline->Compile());
	VERIFY(pipeline->GetRenderPipeline() && !pipeline->GetComputePipeline());
	VERIFY(pipeline->HasBinding(1, 0, LLGI::ShaderResourceType::Texture));

	const std::string writeFragment = "@group(1) @binding(0) var<storage, read_write> value: u32;"
									  "@fragment fn main() -> @location(0) vec4f { value = 7u; return vec4f(1); }";
	LLGI::DataStructure code{writeFragment.data(), static_cast<int32_t>(writeFragment.size())};
	auto storageFragment = LLGI::CreateSharedPtr(graphics->CreateShader(&code, 1));
	VERIFY(storageFragment);
	pipeline->SetShader(LLGI::ShaderStageType::Pixel, storageFragment.get());
	pipeline->VertexLayouts[1] = LLGI::VertexLayoutFormat::R16G16_UNORM;
	VERIFY(pipeline->Compile());
	pipeline->VertexLayouts[0] = LLGI::VertexLayoutFormat::R8G8B8A8_UINT;
	VERIFY(!pipeline->Compile());
	VERIFY(!pipeline->GetRenderPipeline());
}

void test_webgpu_browser_multisample(LLGI::DeviceType deviceType)
{
	LLGI::PlatformParameter pp;
	pp.Device = deviceType;
	auto platform = LLGI::CreateSharedPtr(LLGI::CreatePlatform(pp, nullptr));
	VERIFY(platform);
	auto graphics = LLGI::CreateSharedPtr(platform->CreateGraphics());
	VERIFY(graphics);
	LLGI::RenderTextureInitializationParameter desc;
	desc.Format = LLGI::TextureFormatType::R8G8B8A8_UNORM;
	desc.Size = {4, 4};
	desc.SamplingCount = 4;
	auto source = LLGI::CreateSharedPtr(graphics->CreateRenderTexture(desc));
	desc.SamplingCount = 1;
	auto resolved = LLGI::CreateSharedPtr(graphics->CreateRenderTexture(desc));
	desc.Size = {2, 2};
	auto wrongSize = LLGI::CreateSharedPtr(graphics->CreateRenderTexture(desc));
	VERIFY(source && resolved && wrongSize);
	VERIFY(graphics->CreateRenderPass(source.get(), wrongSize.get(), nullptr, nullptr) == nullptr);
	auto pass = LLGI::CreateSharedPtr(graphics->CreateRenderPass(source.get(), resolved.get(), nullptr, nullptr));
	VERIFY(pass);
	pass->SetClearColor({255, 0, 0, 255});
	pass->SetIsColorCleared(true);
	auto commands = LLGI::CreateSharedPtr(graphics->CreateCommandList(nullptr));
	VERIFY(commands);
	commands->Begin();
	commands->BeginRenderPass(pass.get());
	commands->EndRenderPass();
	commands->End();
	graphics->Execute(commands.get());
	std::vector<uint8_t> pixels;
	VERIFY(source->GetData(pixels));
	VERIFY(pixels.size() == 4 * 4 * 4 && pixels[0] == 255 && pixels == graphics->CaptureRenderTarget(resolved.get()));

	const std::string code = "@group(1) @binding(0) var image: texture_multisampled_2d<f32>;"
							 "@group(2) @binding(0) var<storage, read_write> red: f32;"
							 "@compute @workgroup_size(1) fn main() { red = textureLoad(image, vec2i(0), 0).r; }";
	LLGI::DataStructure data{code.data(), static_cast<int32_t>(code.size())};
	auto shader = LLGI::CreateSharedPtr(graphics->CreateShader(&data, 1));
	auto pipeline = LLGI::CreateSharedPtr(graphics->CreatePiplineState());
	VERIFY(shader && pipeline);
	pipeline->SetShader(LLGI::ShaderStageType::Compute, shader.get());
	VERIFY(pipeline->Compile());
	auto output =
		LLGI::CreateSharedPtr(graphics->CreateBuffer(LLGI::BufferUsageType::StorageWrite | LLGI::BufferUsageType::CopySrc, sizeof(float)));
	auto readback =
		LLGI::CreateSharedPtr(graphics->CreateBuffer(LLGI::BufferUsageType::MapRead | LLGI::BufferUsageType::CopyDst, sizeof(float)));
	VERIFY(output && readback);
	commands->Begin();
	commands->SetPipelineState(pipeline.get());
	commands->SetTexture(source.get(), LLGI::TextureWrapMode::Clamp, LLGI::TextureMinMagFilter::Nearest, 0);
	commands->SetStorageBuffer(output.get(), sizeof(float), 0, LLGI::ShaderResourceAccess::ReadWrite);
	commands->Dispatch(1, 1, 1, 1, 1, 1);
	commands->CopyBuffer(output.get(), readback.get());
	commands->End();
	graphics->Execute(commands.get());
	const auto red = static_cast<const float*>(readback->Lock());
	VERIFY(red && *red == 1.0f);
	readback->Unlock();
}

std::shared_ptr<LLGI::PipelineState> createComputePipeline(LLGI::Graphics& graphics, const std::string& code)
{
	LLGI::DataStructure data{code.data(), static_cast<int32_t>(code.size())};
	auto shader = LLGI::CreateSharedPtr(graphics.CreateShader(&data, 1));
	auto pipeline = LLGI::CreateSharedPtr(graphics.CreatePiplineState());
	VERIFY(shader && pipeline);
	pipeline->SetShader(LLGI::ShaderStageType::Compute, shader.get());
	VERIFY(pipeline->Compile());
	return pipeline;
}

void test_webgpu_browser_binding_lifetime(LLGI::DeviceType deviceType)
{
	LLGI::PlatformParameter pp;
	pp.Device = deviceType;
	auto platform = LLGI::CreateSharedPtr(LLGI::CreatePlatform(pp, nullptr));
	VERIFY(platform);
	auto graphics = LLGI::CreateSharedPtr(platform->CreateGraphics());
	VERIFY(graphics);
	auto pipeline = createComputePipeline(*graphics, R"(
		alias Image = texture_2d<f32>;
		const ResultBase = 0x1u;
		const ResultSlot = 1u + 2u * ResultBase;
		@group(1) @binding(0) var image: Image;
		@group(0) @binding(0) var<uniform> secondaryOnly: u32;
		@group((1u + 1u)) @binding(ResultSlot) var<storage, read_write> result: atomic<u32>;
		@compute @workgroup_size(1) fn secondary() { atomicAdd(&result, secondaryOnly); }
		fn loadImage() -> u32 {
			for (var image = 0; image < 1; image++) {}
			return u32(textureLoad(image, vec2i(0), 1).r);
		}
		@compute @workgroup_size(1) fn main() { atomicAdd(&result, loadImage()); }
	)");
	LLGI::TextureParameter desc;
	desc.Size = {2, 2, 1};
	desc.MipLevelCount = 2;
	desc.IsMipmapGenerationEnabled = true;
	desc.Format = LLGI::TextureFormatType::R32_FLOAT;
	auto image = LLGI::CreateSharedPtr(graphics->CreateTexture(desc));
	VERIFY(image);
	auto pixel = static_cast<float*>(image->Lock());
	VERIFY(pixel);
	const std::array<float, 4> texels{70, 72, 74, 76};
	std::copy(texels.begin(), texels.end(), pixel);
	image->Unlock();
	auto output = LLGI::CreateSharedPtr(graphics->CreateBuffer(LLGI::BufferUsageType::StorageWrite | LLGI::BufferUsageType::CopySrc, 4));
	auto readback = LLGI::CreateSharedPtr(graphics->CreateBuffer(LLGI::BufferUsageType::MapRead | LLGI::BufferUsageType::CopyDst, 4));
	auto commands = LLGI::CreateSharedPtr(graphics->CreateCommandList(nullptr));
	VERIFY(output && readback && commands);
	commands->Begin();
	commands->SetPipelineState(pipeline.get());
	commands->SetTexture(image.get(), LLGI::TextureWrapMode::Clamp, LLGI::TextureMinMagFilter::Nearest, 0);
	commands->SetStorageBuffer(output.get(), 4, 3, LLGI::ShaderResourceAccess::ReadWrite);
	commands->Dispatch(1, 1, 1, 1, 1, 1);
	commands->SetStorageBuffer(nullptr, 4, 3, LLGI::ShaderResourceAccess::ReadWrite);
	commands->Dispatch(1, 1, 1, 1, 1, 1);
	commands->SetStorageBuffer(output.get(), 4, 3, LLGI::ShaderResourceAccess::ReadWrite);
	commands->Dispatch(1, 1, 1, 1, 1, 1);
	commands->CopyBuffer(output.get(), readback.get());
	commands->End();
	graphics->Execute(commands.get());
	auto result = static_cast<const uint32_t*>(readback->Lock());
	VERIFY(result && *result == 146);
	readback->Unlock();
}

void test_webgpu_browser_depth_comparison(LLGI::DeviceType deviceType)
{
	LLGI::PlatformParameter pp;
	pp.Device = deviceType;
	auto platform = LLGI::CreateSharedPtr(LLGI::CreatePlatform(pp, nullptr));
	VERIFY(platform);
	auto graphics = LLGI::CreateSharedPtr(platform->CreateGraphics());
	VERIFY(graphics);
	auto pipeline = createComputePipeline(*graphics, R"(
		@group(1) @binding(0) var depth: texture_depth_2d;
		@group(2) @binding(0) var compareDepth: sampler_comparison;
		@group(1) @binding(1) var threadOutput: texture_storage_2d<rgba8unorm, write>;
		@compute @workgroup_size(1) fn main() {
			let low = textureSampleCompareLevel(depth, compareDepth, vec2f(0.5), 0.25);
			let high = textureSampleCompareLevel(depth, compareDepth, vec2f(0.5), 0.75);
			textureStore(threadOutput, vec2u(0), vec4f(low, high, 0, 1));
		}
	)");
	LLGI::DepthTextureInitializationParameter depthDesc;
	depthDesc.Size = {1, 1};
	depthDesc.Mode = LLGI::DepthTextureMode::DepthStencil;
	auto depth = LLGI::CreateSharedPtr(graphics->CreateDepthTexture(depthDesc));
	LLGI::TextureParameter desc;
	desc.Size = {1, 1, 1};
	desc.Format = LLGI::TextureFormatType::R8G8B8A8_UNORM;
	desc.Usage = LLGI::TextureUsageType::RenderTarget | LLGI::TextureUsageType::Storage;
	auto output = LLGI::CreateSharedPtr(graphics->CreateTexture(desc));
	VERIFY(depth && output);
	auto pass = LLGI::CreateSharedPtr(graphics->CreateRenderPass(output.get(), nullptr, depth.get(), nullptr));
	auto commands = LLGI::CreateSharedPtr(graphics->CreateCommandList(nullptr));
	VERIFY(pass && commands);
	pass->SetIsDepthCleared(true);
	pass->SetClearDepth(0.5f);
	commands->Begin();
	commands->BeginRenderPass(pass.get());
	commands->EndRenderPass();
	commands->SetPipelineState(pipeline.get());
	commands->SetTexture(depth.get(), LLGI::TextureWrapMode::Clamp, LLGI::TextureMinMagFilter::Nearest, 0);
	commands->SetTexture(output.get(), LLGI::TextureWrapMode::Clamp, LLGI::TextureMinMagFilter::Nearest, 1);
	commands->Dispatch(1, 1, 1, 1, 1, 1);
	commands->SetTexture(nullptr, LLGI::TextureWrapMode::Clamp, LLGI::TextureMinMagFilter::Nearest, 0);
	commands->Dispatch(1, 1, 1, 1, 1, 1);
	commands->End();
	graphics->Execute(commands.get());
	std::vector<uint8_t> pixels;
	const std::vector<uint8_t> expected{255, 0, 0, 255};
	VERIFY(output->GetData(pixels) && pixels == expected);
}

void test_webgpu_browser_volume_mips(LLGI::DeviceType deviceType)
{
	LLGI::PlatformParameter pp;
	pp.Device = deviceType;
	auto platform = LLGI::CreateSharedPtr(LLGI::CreatePlatform(pp, nullptr));
	VERIFY(platform);
	auto graphics = LLGI::CreateSharedPtr(platform->CreateGraphics());
	VERIFY(graphics);
	LLGI::TextureParameter desc;
	desc.Size = {4, 2, 4};
	desc.Format = LLGI::TextureFormatType::R32_FLOAT;
	desc.MipLevelCount = 3;
	desc.IsMipmapGenerationEnabled = true;
	desc.Usage = LLGI::TextureUsageType::Array;
	VERIFY(!LLGI::CreateSharedPtr(graphics->CreateTexture(desc)));
	desc.Usage = LLGI::TextureUsageType::NoneFlag;
	desc.Dimension = 3;
	auto volume = LLGI::CreateSharedPtr(graphics->CreateTexture(desc));
	VERIFY(volume);
	auto texels = static_cast<float*>(volume->Lock());
	VERIFY(texels);
	for (int z = 0; z < desc.Size.Z; ++z)
		for (int y = 0; y < desc.Size.Y; ++y)
			for (int x = 0; x < desc.Size.X; ++x)
				*texels++ = static_cast<float>(100 * z + 10 * y + x);
	volume->Unlock();

	auto pipeline = createComputePipeline(*graphics, R"(
		@group(1) @binding(0) var volume: texture_3d<f32>;
		@group(2) @binding(0) var<storage, read_write> samples: array<f32, 3>;
		@compute @workgroup_size(1) fn main() {
			samples[0] = textureLoad(volume, vec3i(0, 0, 0), 1).r;
			samples[1] = textureLoad(volume, vec3i(1, 0, 1), 1).r;
			samples[2] = textureLoad(volume, vec3i(0), 2).r;
		}
	)");
	const std::array<float, 3> expected{55.5f, 257.5f, 156.5f};
	auto output = LLGI::CreateSharedPtr(
		graphics->CreateBuffer(LLGI::BufferUsageType::StorageWrite | LLGI::BufferUsageType::CopySrc, sizeof(expected)));
	auto readback =
		LLGI::CreateSharedPtr(graphics->CreateBuffer(LLGI::BufferUsageType::MapRead | LLGI::BufferUsageType::CopyDst, sizeof(expected)));
	auto commands = LLGI::CreateSharedPtr(graphics->CreateCommandList(nullptr));
	VERIFY(output && readback && commands);
	commands->Begin();
	commands->SetPipelineState(pipeline.get());
	commands->SetTexture(volume.get(), LLGI::TextureWrapMode::Clamp, LLGI::TextureMinMagFilter::Nearest, 0);
	commands->SetStorageBuffer(output.get(), sizeof(float), 0, LLGI::ShaderResourceAccess::ReadWrite);
	commands->Dispatch(1, 1, 1, 1, 1, 1);
	commands->CopyBuffer(output.get(), readback.get());
	commands->End();
	graphics->Execute(commands.get());
	auto samples = static_cast<const float*>(readback->Lock());
	VERIFY(samples && std::equal(expected.begin(), expected.end(), samples));
	readback->Unlock();
}

void test_webgpu_browser_constant_buffer_growth(LLGI::DeviceType deviceType)
{
	LLGI::PlatformParameter pp;
	pp.Device = deviceType;
	auto platform = LLGI::CreateSharedPtr(LLGI::CreatePlatform(pp, nullptr));
	VERIFY(platform);
	auto graphics = LLGI::CreateSharedPtr(platform->CreateGraphics());
	VERIFY(graphics);
	auto pool = LLGI::CreateSharedPtr(graphics->CreateSingleFrameMemoryPool(1024, 1));
	VERIFY(pool);
	pool->NewFrame();
	auto first = LLGI::CreateSharedPtr(pool->CreateConstantBuffer(16));
	pool->NewFrame();
	auto grown = LLGI::CreateSharedPtr(pool->CreateConstantBuffer(64));
	pool->NewFrame();
	auto reused = LLGI::CreateSharedPtr(pool->CreateConstantBuffer(64));
	VERIFY(first && grown && reused && grown->GetSize() >= 64 && grown.get() == reused.get());
	reused.reset();
	pool.reset();
	VERIFY(first->GetRef() == 1 && grown->GetRef() == 1);
}

void test_webgpu_browser_mip_filter(LLGI::DeviceType deviceType)
{
	LLGI::PlatformParameter pp;
	pp.Device = deviceType;
	auto platform = LLGI::CreateSharedPtr(LLGI::CreatePlatform(pp, nullptr));
	VERIFY(platform != nullptr);
	auto graphics = LLGI::CreateSharedPtr(platform->CreateGraphics());
	VERIFY(graphics != nullptr);

	LLGI::TextureParameter desc;
	desc.Size = {2, 2, 1};
	desc.Format = LLGI::TextureFormatType::R8G8B8A8_UNORM;
	desc.MipLevelCount = 2;
	desc.Usage = LLGI::TextureUsageType::Storage;
	desc.IsMipmapGenerationEnabled = false;
	auto texture = LLGI::CreateSharedPtr(graphics->CreateTexture(desc));
	VERIFY(texture != nullptr);
	auto* pixels = static_cast<uint8_t*>(texture->Lock());
	VERIFY(pixels != nullptr);
	std::fill_n(pixels, 16, uint8_t{0});
	// Level 0 has black and red columns. Level 1 is white.
	pixels[12] = 255;
	std::fill_n(pixels + 16, 4, uint8_t{255});
	texture->Unlock();

	std::shared_ptr<LLGI::Shader> shader;
	TestHelper::CreateComputeShader(graphics.get(), deviceType, "mip_filter.comp", shader);
	auto pipeline = LLGI::CreateSharedPtr(graphics->CreatePiplineState());
	VERIFY(shader && pipeline);
	pipeline->SetShader(LLGI::ShaderStageType::Compute, shader.get());
	VERIFY(pipeline->Compile());
	auto output =
		LLGI::CreateSharedPtr(graphics->CreateBuffer(LLGI::BufferUsageType::StorageWrite | LLGI::BufferUsageType::CopySrc, sizeof(float)));
	auto readback =
		LLGI::CreateSharedPtr(graphics->CreateBuffer(LLGI::BufferUsageType::MapRead | LLGI::BufferUsageType::CopyDst, sizeof(float)));

	VERIFY(output && readback);
	auto commands = LLGI::CreateSharedPtr(graphics->CreateCommandList(nullptr));
	VERIFY(commands != nullptr);
	const std::string storageSource = "@binding(0) @group(1) var dst:texture_storage_2d<rgba8unorm,write>;"
									  "@compute @workgroup_size(1) fn main(){textureStore(dst,vec2i(1,0),vec4f(1,0,0,1));}";
	LLGI::DataStructure storageCode{storageSource.data(), static_cast<int32_t>(storageSource.size())};
	auto storageShader = LLGI::CreateSharedPtr(graphics->CreateShader(&storageCode, 1));
	auto storagePipeline = LLGI::CreateSharedPtr(graphics->CreatePiplineState());
	VERIFY(storageShader && storagePipeline);
	storagePipeline->SetShader(LLGI::ShaderStageType::Compute, storageShader.get());
	VERIFY(storagePipeline->Compile());
	commands->Begin();
	commands->SetPipelineState(storagePipeline.get());
	commands->SetTexture(texture.get(), LLGI::TextureWrapMode::Clamp, LLGI::TextureMinMagFilter::Nearest, 0);
	commands->Dispatch(1, 1, 1, 1, 1, 1);
	commands->EndComputePass();
	commands->End();
	graphics->Execute(commands.get());
	for (auto filter : {LLGI::TextureMinMagFilter::Nearest, LLGI::TextureMinMagFilter::Linear})
	{
		commands->Begin();
		commands->SetPipelineState(pipeline.get());
		commands->SetTexture(texture.get(), LLGI::TextureWrapMode::Clamp, filter, 9);
		commands->SetStorageBuffer(output.get(), sizeof(float), 1, LLGI::ShaderResourceAccess::ReadWrite);
		commands->Dispatch(1, 1, 1, 1, 1, 1);
		commands->CopyBuffer(output.get(), readback.get());
		commands->End();
		graphics->Execute(commands.get());
		// At u=3/8, level 0 gives red 0 (nearest) or 1/4 (linear). LOD 1/4 mixes in 1/4 of level 1.
		const float expected = filter == LLGI::TextureMinMagFilter::Nearest ? 0.25f : 0.4375f;
		auto* value = static_cast<const float*>(readback->Lock());
		VERIFY(value && std::abs(*value - expected) < 0.01f);
		readback->Unlock();
	}
}

void test_webgpu_browser_partial_buffer_copy(LLGI::DeviceType deviceType)
{
	LLGI::PlatformParameter pp;
	pp.Device = deviceType;
	auto platform = LLGI::CreateSharedPtr(LLGI::CreatePlatform(pp, nullptr));
	VERIFY(platform);
	auto graphics = LLGI::CreateSharedPtr(platform->CreateGraphics());
	VERIFY(graphics);

	using Usage = LLGI::BufferUsageType;
	auto indices = LLGI::CreateSharedPtr(graphics->CreateBuffer(Usage::MapWrite | Usage::Index | Usage::CopySrc, 6));
	auto destination = LLGI::CreateSharedPtr(graphics->CreateBuffer(Usage::MapWrite | Usage::CopyDst | Usage::CopySrc, 9));
	auto readback = LLGI::CreateSharedPtr(graphics->CreateBuffer(Usage::MapRead | Usage::CopyDst, 9));
	auto commands = LLGI::CreateSharedPtr(graphics->CreateCommandList(nullptr));
	VERIFY(indices && destination && readback && commands);
	const uint8_t original[] = {1, 2, 3, 4, 5, 6};
	std::memcpy(indices->Lock(), original, sizeof(original));
	indices->Unlock();
	std::memset(destination->Lock(), 99, 9);
	destination->Unlock();

	commands->Begin();
	commands->CopyBuffer(indices.get(), destination.get());
	commands->End();
	graphics->Execute(commands.get());
	// Preserve the adjacent GPU-copied bytes on both sides of a partial CPU write.
	std::memset(destination->Lock(2, 3), 42, 3);
	destination->Unlock();
	commands->Begin();
	commands->CopyBuffer(destination.get(), readback.get());
	commands->End();
	graphics->Execute(commands.get());
	const uint8_t expected[] = {1, 2, 42, 42, 42, 6, 99, 99, 99};
	const auto* result = readback->Lock();
	VERIFY(result && std::memcmp(result, expected, sizeof(expected)) == 0);
	readback->Unlock();
}

void test_webgpu_browser_external_submission(LLGI::DeviceType deviceType)
{
	LLGI::PlatformParameter pp;
	pp.Device = deviceType;
	auto platform = LLGI::CreateSharedPtr(LLGI::CreatePlatform(pp, nullptr));
	VERIFY(platform != nullptr);
	auto graphics = LLGI::CreateSharedPtr(platform->CreateGraphics());
	VERIFY(graphics != nullptr);
	auto* web = static_cast<LLGI::GraphicsWebGPU*>(graphics.get());
	auto commands = LLGI::CreateSharedPtr(web->CreateCommandList(nullptr));
	VERIFY(commands != nullptr);
	commands->Begin();
	commands->End();
	web->Execute(commands.get());
	commands->WaitUntilCompleted();
	bool completed = false;
	VERIFY(commands->TryGetCompleted(completed) && completed);

	for (bool externalEncoder : {false, true})
	{
		wgpu::CommandBuffer buffer;
		if (externalEncoder)
		{
			auto encoder = web->GetDevice().CreateCommandEncoder();
			auto borrowed = encoder;
			VERIFY(commands->BeginWithPlatform(borrowed.MoveToCHandle()));
			commands->EndWithPlatform();
			buffer = encoder.Finish();
		}
		else
		{
			commands->Begin();
			commands->End();
			buffer = static_cast<LLGI::CommandListWebGPU*>(commands.get())->GetCommandBuffer();
		}
		web->GetQueue().Submit(1, &buffer);
		VERIFY(!commands->TryGetCompleted(completed) && !completed);
		auto done = std::make_shared<std::atomic<bool>>(false);
		web->GetQueue().OnSubmittedWorkDone(wgpu::CallbackMode::AllowSpontaneous,
											[done](wgpu::QueueWorkDoneStatus status, wgpu::StringView)
											{ done->store(status == wgpu::QueueWorkDoneStatus::Success); });
		commands->WaitUntilCompleted();
		VERIFY(done->load());
	}
}

void test_webgpu_browser_compute_dispatch(LLGI::DeviceType deviceType)
{
	VERIFY(deviceType == LLGI::DeviceType::WebGPU);

	LLGI::PlatformParameter pp;
	pp.Device = deviceType;
	pp.WaitVSync = false;

	auto platform = LLGI::CreateSharedPtr(LLGI::CreatePlatform(pp, nullptr));
	VERIFY(platform != nullptr);

	auto graphics = LLGI::CreateSharedPtr(platform->CreateGraphics());
	VERIFY(graphics != nullptr);

	std::shared_ptr<LLGI::Shader> shaderCS = nullptr;
	TestHelper::CreateComputeShader(graphics.get(), deviceType, "basic.comp", shaderCS);
	VERIFY(shaderCS != nullptr);

	auto pipelineState = LLGI::CreateSharedPtr(graphics->CreatePiplineState());
	VERIFY(pipelineState != nullptr);
	pipelineState->SetShader(LLGI::ShaderStageType::Compute, shaderCS.get());
	VERIFY(pipelineState->Compile());

	const int dataSize = 32;
	auto uploadBuffer = LLGI::CreateSharedPtr(
		graphics->CreateBuffer(LLGI::BufferUsageType::MapWrite | LLGI::BufferUsageType::CopySrc, sizeof(BrowserComputeInput) * dataSize));
	auto inputBuffer = LLGI::CreateSharedPtr(
		graphics->CreateBuffer(LLGI::BufferUsageType::StorageWrite | LLGI::BufferUsageType::CopyDst, sizeof(BrowserComputeInput) * dataSize));
	auto outputBuffer = LLGI::CreateSharedPtr(
		graphics->CreateBuffer(LLGI::BufferUsageType::StorageWrite | LLGI::BufferUsageType::CopySrc, sizeof(BrowserComputeOutput) * dataSize));
	auto readbackTarget = LLGI::CreateSharedPtr(
		graphics->CreateBuffer(LLGI::BufferUsageType::MapRead | LLGI::BufferUsageType::CopyDst, sizeof(BrowserComputeOutput) * dataSize));
	auto constantBuffer = LLGI::CreateSharedPtr(
		graphics->CreateBuffer(LLGI::BufferUsageType::Constant | LLGI::BufferUsageType::MapWrite, sizeof(float)));
	VERIFY(uploadBuffer != nullptr);
	VERIFY(inputBuffer != nullptr);
	VERIFY(outputBuffer != nullptr);
	VERIFY(readbackTarget != nullptr);
	VERIFY(constantBuffer != nullptr);

	{
		auto data = static_cast<BrowserComputeInput*>(uploadBuffer->Lock());
		for (int i = 0; i < dataSize; i++)
		{
			data[i].value1 = static_cast<float>(i + 1);
			data[i].value2 = static_cast<float>(i + 3);
		}
		uploadBuffer->Unlock();
	}
	{
		auto data = static_cast<float*>(constantBuffer->Lock());
		*data = 7.0f;
		constantBuffer->Unlock();
	}

	auto sfMemoryPool = LLGI::CreateSharedPtr(graphics->CreateSingleFrameMemoryPool(1024 * 1024, 16));
	VERIFY(sfMemoryPool != nullptr);
	sfMemoryPool->NewFrame();

	auto commandList = LLGI::CreateSharedPtr(graphics->CreateCommandList(sfMemoryPool.get()));
	VERIFY(commandList != nullptr);
	commandList->Begin();
	commandList->CopyBuffer(uploadBuffer.get(), inputBuffer.get());
	commandList->BeginComputePass();
	commandList->SetPipelineState(pipelineState.get());
	commandList->SetStorageBuffer(inputBuffer.get(), sizeof(BrowserComputeInput), 0, LLGI::ShaderResourceAccess::ReadWrite);
	commandList->SetStorageBuffer(outputBuffer.get(), sizeof(BrowserComputeOutput), 1, LLGI::ShaderResourceAccess::ReadWrite);
	commandList->SetConstantBuffer(constantBuffer.get(), 0);
	commandList->Dispatch(1, 1, 1, 1, 1, 1);
	commandList->EndComputePass();
	commandList->BeginComputePass();
	commandList->Dispatch(dataSize, 1, 1, 1, 1, 1);
	commandList->EndComputePass();
	commandList->CopyBuffer(outputBuffer.get(), readbackTarget.get());
	commandList->End();

	graphics->Execute(commandList.get());
	graphics->WaitFinish();

	auto result = static_cast<const BrowserComputeOutput*>(readbackTarget->Lock());
	VERIFY(result != nullptr);
	for (int i = 0; i < dataSize; i++)
	{
		const float expected = static_cast<float>(i + 1) * static_cast<float>(i + 3) + 7.0f;
		VERIFY(std::fabs(result[i].value - expected) < 0.001f);
	}
	readbackTarget->Unlock();
}

void test_webgpu_browser_render_readback(LLGI::DeviceType deviceType)
{
	VERIFY(deviceType == LLGI::DeviceType::WebGPU);

	LLGI::PlatformParameter pp;
	pp.Device = deviceType;
	pp.WaitVSync = false;

	auto platform = LLGI::CreateSharedPtr(LLGI::CreatePlatform(pp, nullptr));
	VERIFY(platform != nullptr);

	auto graphics = LLGI::CreateSharedPtr(platform->CreateGraphics());
	VERIFY(graphics != nullptr);

	LLGI::TextureParameter renderTextureParam;
	renderTextureParam.Size = {64, 2, 1};
	renderTextureParam.Usage = LLGI::TextureUsageType::RenderTarget;
	renderTextureParam.MipLevelCount = 2;
	renderTextureParam.IsMipmapGenerationEnabled = true;
	renderTextureParam.Format = LLGI::TextureFormatType::R16G16B16A16_FLOAT;
	auto renderTexture = LLGI::CreateSharedPtr(graphics->CreateTexture(renderTextureParam));
	VERIFY(renderTexture != nullptr);
	auto* upload = renderTexture->Lock();
	VERIFY(upload != nullptr);
	std::memset(upload, 0, LLGI::GetTextureMemorySize(renderTextureParam.Format, renderTextureParam.Size, 2, false));
	renderTexture->Unlock();

	auto renderPass = LLGI::CreateSharedPtr(graphics->CreateRenderPass(renderTexture.get(), nullptr, nullptr, nullptr));
	VERIFY(renderPass != nullptr);
	renderPass->SetClearColor(LLGI::Color8(255, 0, 0, 255));
	renderPass->SetIsColorCleared(true);

	auto sfMemoryPool = LLGI::CreateSharedPtr(graphics->CreateSingleFrameMemoryPool(1024 * 1024, 16));
	VERIFY(sfMemoryPool != nullptr);
	sfMemoryPool->NewFrame();

	auto commandList = LLGI::CreateSharedPtr(graphics->CreateCommandList(sfMemoryPool.get()));
	VERIFY(commandList != nullptr);
	commandList->Begin();
	commandList->BeginRenderPass(renderPass.get());
	commandList->EndRenderPass();
	commandList->GenerateMipMap(renderTexture.get());
	commandList->End();

	graphics->Execute(commandList.get());
	graphics->WaitFinish();

	const auto data = graphics->CaptureRenderTarget(renderTexture.get());
	const std::array<uint16_t, 4> red = {0x3c00, 0, 0, 0x3c00}; // RGBA16F (1, 0, 0, 1)
	VERIFY(data.size() == 64 * 2 * sizeof(red));
	for (size_t offset = 0; offset < data.size(); offset += sizeof(red))
		VERIFY(std::memcmp(data.data() + offset, red.data(), sizeof(red)) == 0);

	// Read back level 1 after the clear replaced the uploaded black texels with red.
	const auto mip = ReadTextureUploadPlaneWebGPU(graphics.get(), renderTexture.get(), 1, 0, {32, 1});
	VERIFY(mip.size() == 32 * sizeof(red));
	for (size_t offset = 0; offset < mip.size(); offset += sizeof(red))
		VERIFY(std::memcmp(mip.data() + offset, red.data(), sizeof(red)) == 0);
}

void test_webgpu_browser_screen_presentation(LLGI::DeviceType deviceType)
{
	VERIFY(deviceType == LLGI::DeviceType::WebGPU);

	LLGI::PlatformParameter pp;
	pp.Device = deviceType;
	pp.WaitVSync = false;

	auto window = std::unique_ptr<LLGI::Window>(LLGI::CreateWindow("WebGPU Browser", LLGI::Vec2I(640, 360)));
	VERIFY(window != nullptr);

	auto platform = LLGI::CreateSharedPtr(LLGI::CreatePlatform(pp, window.get()));
	VERIFY(platform != nullptr);

	auto graphics = LLGI::CreateSharedPtr(platform->CreateGraphics());
	VERIFY(graphics != nullptr);

	auto sfMemoryPool = LLGI::CreateSharedPtr(graphics->CreateSingleFrameMemoryPool(1024 * 1024, 16));
	VERIFY(sfMemoryPool != nullptr);

	std::shared_ptr<LLGI::Shader> shaderVS = nullptr;
	std::shared_ptr<LLGI::Shader> shaderPS = nullptr;
	TestHelper::CreateShader(graphics.get(), deviceType, "simple_rectangle.vert", "simple_rectangle.frag", shaderVS, shaderPS);
	VERIFY(shaderVS != nullptr);
	VERIFY(shaderPS != nullptr);

	std::shared_ptr<LLGI::Buffer> vertexBuffer;
	std::shared_ptr<LLGI::Buffer> indexBuffer;
	TestHelper::CreateRectangle(graphics.get(),
								LLGI::Vec3F(-0.6f, 0.6f, 0.5f),
								LLGI::Vec3F(0.6f, -0.6f, 0.5f),
								LLGI::Color8(255, 255, 255, 255),
								LLGI::Color8(0, 255, 96, 255),
								vertexBuffer,
								indexBuffer);
	VERIFY(vertexBuffer != nullptr);
	VERIFY(indexBuffer != nullptr);

	const LLGI::Color8 color(28, 96, 180, 255);
	VERIFY(platform->NewFrame());
	auto screen = platform->GetCurrentScreen(color, true, false);
	VERIFY(screen);
	auto renderPassPipelineState = LLGI::CreateSharedPtr(graphics->CreateRenderPassPipelineState(screen));
	auto pipelineState = LLGI::CreateSharedPtr(graphics->CreatePiplineState());
	VERIFY(renderPassPipelineState && pipelineState);
	configureSimpleVertexLayout(pipelineState.get());
	pipelineState->SetShader(LLGI::ShaderStageType::Vertex, shaderVS.get());
	pipelineState->SetShader(LLGI::ShaderStageType::Pixel, shaderPS.get());
	pipelineState->SetRenderPassPipelineState(renderPassPipelineState.get());
	// Compilation can yield to the browser and expire the current canvas texture.
	VERIFY(pipelineState->Compile());

	for (int frame = 0; frame < 3; frame++)
	{
		VERIFY(platform->NewFrame());
		sfMemoryPool->NewFrame();
		auto renderPass = platform->GetCurrentScreen(color, true, false);
		VERIFY(renderPass);
		auto commandList = LLGI::CreateSharedPtr(graphics->CreateCommandList(sfMemoryPool.get()));
		VERIFY(commandList != nullptr);
		commandList->Begin();
		commandList->BeginRenderPass(renderPass);
		commandList->SetVertexBuffer(vertexBuffer.get(), sizeof(SimpleVertex), 0);
		commandList->SetIndexBuffer(indexBuffer.get(), 2);
		commandList->SetPipelineState(pipelineState.get());
		commandList->Draw(2);
		commandList->EndRenderPass();
		commandList->End();

		graphics->Execute(commandList.get());
		platform->Present();
		graphics->WaitFinish();
	}
}
} // namespace

TestRegister WebGPUBrowser_OffscreenRender(
	"WebGPUBrowser.OffscreenRender",
	[](LLGI::DeviceType device) -> void { test_webgpu_browser_offscreen_render(device); });

TestRegister WebGPUBrowser_PartialBufferCopy("WebGPUBrowser.PartialBufferCopy", test_webgpu_browser_partial_buffer_copy);

TestRegister WebGPUBrowser_CompressedArrayReadback("WebGPUBrowser.CompressedArrayReadback", test_webgpu_browser_compressed_array_readback);

TestRegister WebGPUBrowser_Multisample("WebGPUBrowser.Multisample", test_webgpu_browser_multisample);

TestRegister WebGPUBrowser_BindingLifetime("WebGPUBrowser.BindingLifetime", test_webgpu_browser_binding_lifetime);

TestRegister WebGPUBrowser_DepthComparison("WebGPUBrowser.DepthComparison", test_webgpu_browser_depth_comparison);

TestRegister WebGPUBrowser_ConstantBufferGrowth("WebGPUBrowser.ConstantBufferGrowth", test_webgpu_browser_constant_buffer_growth);

TestRegister WebGPUBrowser_FloatTextures(
	"WebGPUBrowser.FloatTextures",
	[](LLGI::DeviceType device)
	{
		for (auto format : {LLGI::TextureFormatType::R16_FLOAT, LLGI::TextureFormatType::R32_FLOAT, LLGI::TextureFormatType::R32G32_FLOAT})
			test_webgpu_browser_texture_format_raw_data(device, {LLGI::to_string(format).c_str(), format, 1});
	});

TestRegister WebGPUBrowser_VolumeMips("WebGPUBrowser.VolumeMips", test_webgpu_browser_volume_mips);

TestRegister WebGPUBrowser_MipFilter("WebGPUBrowser.MipFilter", test_webgpu_browser_mip_filter);

TestRegister WebGPUBrowser_ExternalSubmission("WebGPUBrowser.ExternalSubmission", test_webgpu_browser_external_submission);

TestRegister WebGPUBrowser_ComputeCompile(
	"WebGPUBrowser.ComputeCompile",
	[](LLGI::DeviceType device) -> void { test_webgpu_browser_compute_compile(device); });

TestRegister WebGPUBrowser_TextureAndConstantRender(
	"WebGPUBrowser.TextureAndConstantRender",
	[](LLGI::DeviceType device) -> void { test_webgpu_browser_texture_and_constant_render(device); });

TestRegister WebGPUBrowser_TextureRawData_RGBA8(
	"WebGPUBrowser.TextureRawData_RGBA8",
	[](LLGI::DeviceType device) -> void {
		test_webgpu_browser_texture_format_raw_data(device, {"TextureRawData_RGBA8", LLGI::TextureFormatType::R8G8B8A8_UNORM, 1});
	});

TestRegister WebGPUBrowser_TextureRawData_R8(
	"WebGPUBrowser.TextureRawData_R8",
	[](LLGI::DeviceType device) -> void {
		test_webgpu_browser_texture_format_raw_data(device, {"TextureRawData_R8", LLGI::TextureFormatType::R8_UNORM, 1});
	});

TestRegister WebGPUBrowser_TextureRawData_RG11B10_UFLOAT(
	"WebGPUBrowser.TextureRawData_RG11B10_UFLOAT",
	[](LLGI::DeviceType device) -> void {
		test_webgpu_browser_texture_format_raw_data(device, {"TextureRawData_RG11B10_UFLOAT", LLGI::TextureFormatType::RG11B10_UFLOAT, 1});
	});

TestRegister WebGPUBrowser_TextureRawData_RGBA8_MipMap(
	"WebGPUBrowser.TextureRawData_RGBA8_MipMap",
	[](LLGI::DeviceType device) -> void {
		test_webgpu_browser_texture_format_raw_data(device, {"TextureRawData_RGBA8_MipMap", LLGI::TextureFormatType::R8G8B8A8_UNORM, 4});
	});

TestRegister WebGPUBrowser_ComputeDispatch(
	"WebGPUBrowser.ComputeDispatch",
	[](LLGI::DeviceType device) -> void { test_webgpu_browser_compute_dispatch(device); });

TestRegister WebGPUBrowser_RenderReadback(
	"WebGPUBrowser.RenderReadback",
	[](LLGI::DeviceType device) -> void { test_webgpu_browser_render_readback(device); });

TestRegister WebGPUBrowser_ScreenPresentation(
	"WebGPUBrowser.ScreenPresentation",
	[](LLGI::DeviceType device) -> void { test_webgpu_browser_screen_presentation(device); });

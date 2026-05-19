
#include "TestHelper.h"
#include "test.h"

#include <Utils/LLGI.CommandListPool.h>
#include <array>
#include <fstream>
#include <iostream>
#include <map>

enum class SingleRectangleTestMode
{
	Triangle,
	Line,
	Point,
};

enum class SimpleTextureRectangleTestMode
{
	RGBA8,
	RGBA32F,
	R8,
};

void test_simple_rectangle(LLGI::DeviceType deviceType, SingleRectangleTestMode mode)
{
	if (mode == SingleRectangleTestMode::Point)
	{
		if (deviceType == LLGI::DeviceType::Default)
			return;
		if (deviceType == LLGI::DeviceType::Vulkan)
			return;
	}

	int count = 0;

	LLGI::PlatformParameter pp;
	pp.Device = deviceType;
	pp.WaitVSync = true;
	auto window = std::unique_ptr<LLGI::Window>(LLGI::CreateWindow("SimpleRectangle", LLGI::Vec2I(1280, 720)));
	auto platform = LLGI::CreatePlatform(pp, window.get());

	LLGI::SafeAddRef(platform);

	auto graphics = platform->CreateGraphics();
	graphics->SetDisposed([platform]() -> void { platform->Release(); });

	auto sfMemoryPool = graphics->CreateSingleFrameMemoryPool(1024 * 1024, 128);

	auto commandListPool = std::make_shared<LLGI::CommandListPool>(graphics, sfMemoryPool, 3);

	std::shared_ptr<LLGI::Shader> shader_vs = nullptr;
	std::shared_ptr<LLGI::Shader> shader_ps = nullptr;

	TestHelper::CreateShader(graphics, deviceType, "simple_rectangle.vert", "simple_rectangle.frag", shader_vs, shader_ps);

	std::shared_ptr<LLGI::Buffer> vb;
	std::shared_ptr<LLGI::Buffer> ib;
	TestHelper::CreateRectangle(graphics,
								LLGI::Vec3F(-0.5, 0.5, 0.5),
								LLGI::Vec3F(0.5, -0.5, 0.5),
								LLGI::Color8(255, 255, 255, 255),
								LLGI::Color8(0, 255, 0, 255),
								vb,
								ib);

	std::map<std::shared_ptr<LLGI::RenderPassPipelineState>, std::shared_ptr<LLGI::PipelineState>> pips;

	while (count < 60)
	{
		if (!platform->NewFrame())
			break;

		sfMemoryPool->NewFrame();

		LLGI::Color8 color;
		color.R = count % 255;
		color.G = 0;
		color.B = 0;
		color.A = 255;

		auto renderPass = platform->GetCurrentScreen(color, true, false); // TODO: isDepthClear is false, because it fails with dx12.
		auto renderPassPipelineState = LLGI::CreateSharedPtr(graphics->CreateRenderPassPipelineState(renderPass));

		if (pips.count(renderPassPipelineState) == 0)
		{
			auto pip = graphics->CreatePiplineState();
			pip->VertexLayouts[0] = LLGI::VertexLayoutFormat::R32G32B32_FLOAT;
			pip->VertexLayouts[1] = LLGI::VertexLayoutFormat::R32G32_FLOAT;
			pip->VertexLayouts[2] = LLGI::VertexLayoutFormat::R8G8B8A8_UNORM;
			pip->VertexLayoutNames[0] = "POSITION";
			pip->VertexLayoutNames[1] = "UV";
			pip->VertexLayoutNames[2] = "COLOR";
			pip->VertexLayoutCount = 3;

			if (mode == SingleRectangleTestMode::Triangle)
			{
				pip->Topology = LLGI::TopologyType::Triangle;
			}
			else if (mode == SingleRectangleTestMode::Line)
			{
				pip->Topology = LLGI::TopologyType::Line;
			}
			else if (mode == SingleRectangleTestMode::Point)
			{
				pip->Topology = LLGI::TopologyType::Point;
			}

			pip->SetShader(LLGI::ShaderStageType::Vertex, shader_vs.get());
			pip->SetShader(LLGI::ShaderStageType::Pixel, shader_ps.get());
			pip->SetRenderPassPipelineState(renderPassPipelineState.get());
			if (!pip->Compile())
			{
				abort();
			}

			pips[renderPassPipelineState] = LLGI::CreateSharedPtr(pip);
		}

		auto commandList = commandListPool->Get();
		commandList->Begin();
		commandList->BeginRenderPass(renderPass);
		commandList->SetVertexBuffer(vb.get(), sizeof(SimpleVertex), 0);
		commandList->SetIndexBuffer(ib.get(), 2);
		commandList->SetPipelineState(pips[renderPassPipelineState].get());
		commandList->Draw(2);
		commandList->EndRenderPass();
		commandList->End();

		graphics->Execute(commandList);

		platform->Present();
		count++;

		if (TestHelper::GetIsCaptureRequired() && count == 30)
		{
			commandListPool->WaitUntilCompleted();
			auto texture = platform->GetCurrentScreen(LLGI::Color8(), true)->GetRenderTexture(0);
			auto data = graphics->CaptureRenderTarget(texture);

			if (mode == SingleRectangleTestMode::Triangle)
			{
				Bitmap2D(data, texture->GetSizeAs2D().X, texture->GetSizeAs2D().Y, texture->GetFormat())
					.Save("SimpleRender.BasicTriangle_" + TestHelper::GetDeviceName(deviceType) + ".png");
			}
			else if (mode == SingleRectangleTestMode::Line)
			{
				Bitmap2D(data, texture->GetSizeAs2D().X, texture->GetSizeAs2D().Y, texture->GetFormat())
					.Save("SimpleRender.BasicLine_" + TestHelper::GetDeviceName(deviceType) + ".png");
			}
			else if (mode == SingleRectangleTestMode::Point)
			{
				Bitmap2D(data, texture->GetSizeAs2D().X, texture->GetSizeAs2D().Y, texture->GetFormat())
					.Save("SimpleRender.BasicPoint_" + TestHelper::GetDeviceName(deviceType) + ".png");
			}
			break;
		}
	}

	commandListPool->WaitUntilCompleted();

	pips.clear();

	graphics->WaitFinish();
	LLGI::SafeRelease(sfMemoryPool);
	LLGI::SafeRelease(graphics);
	LLGI::SafeRelease(platform);
}

void test_index_offset(LLGI::DeviceType deviceType)
{
	int count = 0;

	LLGI::PlatformParameter pp;
	pp.Device = deviceType;
	pp.WaitVSync = true;
	auto window = std::unique_ptr<LLGI::Window>(LLGI::CreateWindow("IndexOffset", LLGI::Vec2I(1280, 720)));
	auto platform = LLGI::CreatePlatform(pp, window.get());
	LLGI::SafeAddRef(platform);

	auto graphics = platform->CreateGraphics();
	graphics->SetDisposed([platform]() -> void { platform->Release(); });

	auto sfMemoryPool = graphics->CreateSingleFrameMemoryPool(1024 * 1024, 128);

	auto commandListPool = std::make_shared<LLGI::CommandListPool>(graphics, sfMemoryPool, 3);

	std::shared_ptr<LLGI::Shader> shader_vs = nullptr;
	std::shared_ptr<LLGI::Shader> shader_ps = nullptr;

	TestHelper::CreateShader(graphics, deviceType, "simple_rectangle.vert", "simple_rectangle.frag", shader_vs, shader_ps);

	std::shared_ptr<LLGI::Buffer> vb;
	std::shared_ptr<LLGI::Buffer> ib;
	TestHelper::CreateRectangle(graphics,
								LLGI::Vec3F(-0.5, 0.5, 0.5),
								LLGI::Vec3F(0.5, -0.5, 0.5),
								LLGI::Color8(255, 255, 255, 255),
								LLGI::Color8(0, 255, 0, 255),
								vb,
								ib);

	std::map<std::shared_ptr<LLGI::RenderPassPipelineState>, std::shared_ptr<LLGI::PipelineState>> pips;

	while (count < 60)
	{
		if (!platform->NewFrame())
			break;

		sfMemoryPool->NewFrame();

		LLGI::Color8 color;
		color.R = count % 255;
		color.G = 0;
		color.B = 0;
		color.A = 255;

		auto renderPass = platform->GetCurrentScreen(color, true, false); // TODO: isDepthClear is false, because it fails with dx12.
		auto renderPassPipelineState = LLGI::CreateSharedPtr(graphics->CreateRenderPassPipelineState(renderPass));

		if (pips.count(renderPassPipelineState) == 0)
		{
			auto pip = graphics->CreatePiplineState();
			pip->VertexLayouts[0] = LLGI::VertexLayoutFormat::R32G32B32_FLOAT;
			pip->VertexLayouts[1] = LLGI::VertexLayoutFormat::R32G32_FLOAT;
			pip->VertexLayouts[2] = LLGI::VertexLayoutFormat::R8G8B8A8_UNORM;
			pip->VertexLayoutNames[0] = "POSITION";
			pip->VertexLayoutNames[1] = "UV";
			pip->VertexLayoutNames[2] = "COLOR";
			pip->VertexLayoutCount = 3;

			pip->SetShader(LLGI::ShaderStageType::Vertex, shader_vs.get());
			pip->SetShader(LLGI::ShaderStageType::Pixel, shader_ps.get());
			pip->SetRenderPassPipelineState(renderPassPipelineState.get());
			pip->Compile();

			pips[renderPassPipelineState] = LLGI::CreateSharedPtr(pip);
		}

		auto commandList = commandListPool->Get();
		commandList->Begin();
		commandList->BeginRenderPass(renderPass);
		commandList->SetVertexBuffer(vb.get(), sizeof(SimpleVertex), 0);
		commandList->SetIndexBuffer(ib.get(), 2, 3 * 2);
		commandList->SetPipelineState(pips[renderPassPipelineState].get());
		commandList->Draw(1);
		commandList->EndRenderPass();
		commandList->End();

		graphics->Execute(commandList);

		platform->Present();
		count++;

		if (TestHelper::GetIsCaptureRequired() && count == 30)
		{
			commandListPool->WaitUntilCompleted();
			auto texture = platform->GetCurrentScreen(LLGI::Color8(), true)->GetRenderTexture(0);
			auto data = graphics->CaptureRenderTarget(texture);
			Bitmap2D(data, texture->GetSizeAs2D().X, texture->GetSizeAs2D().Y, texture->GetFormat())
				.Save("SimpleRender.IndexOffset_" + TestHelper::GetDeviceName(deviceType) + ".png");
			break;
		}
	}

	commandListPool->WaitUntilCompleted();

	pips.clear();

	graphics->WaitFinish();

	LLGI::SafeRelease(sfMemoryPool);
	LLGI::SafeRelease(graphics);
	LLGI::SafeRelease(platform);
}

void test_simple_constant_rectangle(LLGI::ConstantBufferType type, LLGI::DeviceType deviceType)
{
	auto code_gl_vs = R"(
#version 440 core
layout(location = 0) in vec3 a_position;
layout(location = 1) in vec2 a_uv;
layout(location = 2) in vec4 a_color;

layout(binding = 0) uniform Block 
{
	vec4 u_offset;
};

out gl_PerVertex
{
	vec4 gl_Position;
};

out vec2 v_uv;
out vec4 v_color;

void main()
{
	gl_Position.x  = a_position.x;
	gl_Position.y  = a_position.y;
	gl_Position.z  = a_position.z;
	gl_Position.w  = 1.0f;
	gl_Position = gl_Position + u_offset;
	v_uv = a_uv;
	v_color = a_color;
}

)";

	auto code_gl_ps = R"(
#version 440 core
#extension GL_NV_gpu_shader5:require

layout(binding = 0) uniform Block 
{
	vec4 u_offset;
};

in vec2 v_uv;
in vec4 v_color;

layout(location = 0) out vec4 color;

void main()
{
    color = v_color + u_offset;
}

)";

	auto compiler = LLGI::CreateCompiler(deviceType);

	int count = 0;

	LLGI::PlatformParameter pp;
	pp.Device = deviceType;
	pp.WaitVSync = true;
	auto window = std::unique_ptr<LLGI::Window>(LLGI::CreateWindow("ConstantRectangle", LLGI::Vec2I(1280, 720)));
	auto platform = LLGI::CreatePlatform(pp, window.get());

	auto graphics = platform->CreateGraphics();
	auto sfMemoryPool = graphics->CreateSingleFrameMemoryPool(1024 * 1024, 128);

	auto commandListPool = std::make_shared<LLGI::CommandListPool>(graphics, sfMemoryPool, 3);

	LLGI::Buffer* cb_vs = nullptr;
	LLGI::Buffer* cb_ps = nullptr;

	LLGI::Shader* shader_vs = nullptr;
	LLGI::Shader* shader_ps = nullptr;

	std::vector<LLGI::DataStructure> data_vs;
	std::vector<LLGI::DataStructure> data_ps;

	if (compiler == nullptr)
	{
		auto binary_vs = TestHelper::LoadData("simple_constant_rectangle.vert.spv");
		auto binary_ps = TestHelper::LoadData("simple_constant_rectangle.frag.spv");

		LLGI::DataStructure d_vs;
		LLGI::DataStructure d_ps;

		d_vs.Data = binary_vs.data();
		d_vs.Size = static_cast<int32_t>(binary_vs.size());
		d_ps.Data = binary_ps.data();
		d_ps.Size = static_cast<int32_t>(binary_ps.size());

		data_vs.push_back(d_vs);
		data_ps.push_back(d_ps);

		shader_vs = graphics->CreateShader(data_vs.data(), static_cast<int32_t>(data_vs.size()));
		shader_ps = graphics->CreateShader(data_ps.data(), static_cast<int32_t>(data_ps.size()));
	}
	else
	{
		LLGI::CompilerResult result_vs;
		LLGI::CompilerResult result_ps;

		if (platform->GetDeviceType() == LLGI::DeviceType::Metal || platform->GetDeviceType() == LLGI::DeviceType::DirectX12 ||
			platform->GetDeviceType() == LLGI::DeviceType::Vulkan || platform->GetDeviceType() == LLGI::DeviceType::WebGPU)
		{
			auto code_vs = TestHelper::LoadData("simple_constant_rectangle.vert");
			auto code_ps = TestHelper::LoadData("simple_constant_rectangle.frag");
			code_vs.push_back(0);
			code_ps.push_back(0);

			compiler->Compile(result_vs, (const char*)code_vs.data(), LLGI::ShaderStageType::Vertex);
			compiler->Compile(result_ps, (const char*)code_ps.data(), LLGI::ShaderStageType::Pixel);
		}
		else
		{
			compiler->Compile(result_vs, code_gl_vs, LLGI::ShaderStageType::Vertex);
			compiler->Compile(result_ps, code_gl_ps, LLGI::ShaderStageType::Pixel);
		}

		for (auto& b : result_vs.Binary)
		{
			LLGI::DataStructure d;
			d.Data = b.data();
			d.Size = static_cast<int32_t>(b.size());
			data_vs.push_back(d);
		}

		for (auto& b : result_ps.Binary)
		{
			LLGI::DataStructure d;
			d.Data = b.data();
			d.Size = static_cast<int32_t>(b.size());
			data_ps.push_back(d);
		}

		shader_vs = graphics->CreateShader(data_vs.data(), static_cast<int32_t>(data_vs.size()));
		shader_ps = graphics->CreateShader(data_ps.data(), static_cast<int32_t>(data_ps.size()));
	}

	std::shared_ptr<LLGI::Buffer> vb;
	std::shared_ptr<LLGI::Buffer> ib;
	TestHelper::CreateRectangle(graphics,
								LLGI::Vec3F(-0.5, 0.5, 0.5),
								LLGI::Vec3F(0.5, -0.5, 0.5),
								LLGI::Color8(255, 255, 255, 255),
								LLGI::Color8(0, 255, 0, 255),
								vb,
								ib);

	if (type == LLGI::ConstantBufferType::LongTime)
	{
		cb_vs = graphics->CreateBuffer(LLGI::BufferUsageType::Constant | LLGI::BufferUsageType::MapWrite, sizeof(float) * 4);
		cb_ps = graphics->CreateBuffer(LLGI::BufferUsageType::Constant | LLGI::BufferUsageType::MapWrite, sizeof(float) * 4);

		auto cb_vs_buf = (float*)cb_vs->Lock();
		cb_vs_buf[0] = 0.2f;
		cb_vs_buf[1] = 0.0f;
		cb_vs_buf[2] = 0.0f;
		cb_vs_buf[3] = 0.0f;
		cb_vs->Unlock();

		auto cb_ps_buf = (float*)cb_ps->Lock();
		cb_ps_buf[0] = 0.0f;
		cb_ps_buf[1] = -1.0f;
		cb_ps_buf[2] = -1.0f;
		cb_ps_buf[3] = 0.0f;
		cb_ps->Unlock();
	}

	std::map<std::shared_ptr<LLGI::RenderPassPipelineState>, std::shared_ptr<LLGI::PipelineState>> pips;

	while (count < 60)
	{
		if (!platform->NewFrame())
		{
			break;
		}

		sfMemoryPool->NewFrame();

		if (type == LLGI::ConstantBufferType::ShortTime)
		{
			cb_vs = sfMemoryPool->CreateConstantBuffer(sizeof(float) * 4);
			cb_ps = sfMemoryPool->CreateConstantBuffer(sizeof(float) * 4);

			auto cb_vs_buf = (float*)cb_vs->Lock();
			cb_vs_buf[0] = (count % 100) / 100.0f;
			cb_vs_buf[1] = 0.0f;
			cb_vs_buf[2] = 0.0f;
			cb_vs_buf[3] = 0.0f;
			cb_vs->Unlock();

			auto cb_ps_buf = (float*)cb_ps->Lock();
			cb_ps_buf[0] = 0.0f;
			cb_ps_buf[1] = -1.0f;
			cb_ps_buf[2] = -1.0f;
			cb_ps_buf[3] = 0.0f;
			cb_ps->Unlock();
		}

		LLGI::Color8 color;
		color.R = count % 255;
		color.G = 0;
		color.B = 0;
		color.A = 255;

		auto renderPass = platform->GetCurrentScreen(color, true, false); // TODO: isDepthClear is false, because it fails with dx12.
		auto renderPassPipelineState = LLGI::CreateSharedPtr(graphics->CreateRenderPassPipelineState(renderPass));

		if (pips.count(renderPassPipelineState) == 0)
		{
			auto pip = graphics->CreatePiplineState();
			pip->VertexLayouts[0] = LLGI::VertexLayoutFormat::R32G32B32_FLOAT;
			pip->VertexLayouts[1] = LLGI::VertexLayoutFormat::R32G32_FLOAT;
			pip->VertexLayouts[2] = LLGI::VertexLayoutFormat::R8G8B8A8_UNORM;
			pip->VertexLayoutNames[0] = "POSITION";
			pip->VertexLayoutNames[1] = "UV";
			pip->VertexLayoutNames[2] = "COLOR";
			pip->VertexLayoutCount = 3;

			pip->SetShader(LLGI::ShaderStageType::Vertex, shader_vs);
			pip->SetShader(LLGI::ShaderStageType::Pixel, shader_ps);
			pip->SetRenderPassPipelineState(renderPassPipelineState.get());
			pip->Compile();

			pips[renderPassPipelineState] = LLGI::CreateSharedPtr(pip);
		}

		auto commandList = commandListPool->Get();
		commandList->Begin();

		commandList->BeginRenderPass(renderPass);
		commandList->SetVertexBuffer(vb.get(), sizeof(SimpleVertex), 0);
		commandList->SetIndexBuffer(ib.get(), 2);
		commandList->SetPipelineState(pips[renderPassPipelineState].get());
		commandList->SetConstantBuffer(cb_vs, 0);
		commandList->SetConstantBuffer(cb_ps, 1);
		commandList->Draw(2);

		commandList->EndRenderPass();
		commandList->End();

		graphics->Execute(commandList);

		platform->Present();
		count++;

		if (type == LLGI::ConstantBufferType::ShortTime)
		{
			LLGI::SafeRelease(cb_vs);
			LLGI::SafeRelease(cb_ps);
		}

		if (TestHelper::GetIsCaptureRequired() && count == 30)
		{
			commandListPool->WaitUntilCompleted();
			auto texture = platform->GetCurrentScreen(LLGI::Color8(), true)->GetRenderTexture(0);
			auto data = graphics->CaptureRenderTarget(texture);

			if (type == LLGI::ConstantBufferType::LongTime)
			{
				Bitmap2D(data, texture->GetSizeAs2D().X, texture->GetSizeAs2D().Y, texture->GetFormat())
					.Save("SimpleRender.ConstantLT_" + TestHelper::GetDeviceName(deviceType) + ".png");
			}
			else
			{
				Bitmap2D(data, texture->GetSizeAs2D().X, texture->GetSizeAs2D().Y, texture->GetFormat())
					.Save("SimpleRender.ConstantST_" + TestHelper::GetDeviceName(deviceType) + ".png");
			}

			break;
		}
	}

	commandListPool->WaitUntilCompleted();
	graphics->WaitFinish();

	pips.clear();

	LLGI::SafeRelease(sfMemoryPool);
	LLGI::SafeRelease(cb_vs);
	LLGI::SafeRelease(cb_ps);
	LLGI::SafeRelease(shader_vs);
	LLGI::SafeRelease(shader_ps);
	LLGI::SafeRelease(graphics);
	LLGI::SafeRelease(platform);

	LLGI::SafeRelease(compiler);
}

void test_simple_texture_rectangle(LLGI::DeviceType deviceType, SimpleTextureRectangleTestMode mode)
{
	auto code_gl_vs = R"(
#version 440 core
layout(location = 0) in vec3 a_position;
layout(location = 1) in vec2 a_uv;
layout(location = 2) in vec4 a_color;

out gl_PerVertex
{
	vec4 gl_Position;
};

out vec2 v_uv;
out vec4 v_color;

void main()
{
	gl_Position.x  = a_position.x;
	gl_Position.y  = a_position.y;
	gl_Position.z  = a_position.z;
	gl_Position.w  = 1.0f;
	gl_Position = gl_Position;
	v_uv = a_uv;
	v_color = a_color;
}

)";

	auto code_gl_ps = R"(
#version 440 core
#extension GL_NV_gpu_shader5:require

in vec2 v_uv;
in vec4 v_color;
layout(binding = 0) uniform sampler2D mainTexture;

layout(location = 0) out vec4 color;

void main()
{
    color = v_color * texture(mainTexture, v_uv);
}

)";

	auto compiler = LLGI::CreateCompiler(deviceType);

	int count = 0;

	LLGI::PlatformParameter pp;
	pp.Device = deviceType;
	pp.WaitVSync = true;
	auto window = std::unique_ptr<LLGI::Window>(LLGI::CreateWindow("TextureRectangle", LLGI::Vec2I(1280, 720)));
	auto platform = LLGI::CreatePlatform(pp, window.get());

	auto graphics = platform->CreateGraphics();
	auto sfMemoryPool = graphics->CreateSingleFrameMemoryPool(1024 * 1024, 128);

	auto commandListPool = std::make_shared<LLGI::CommandListPool>(graphics, sfMemoryPool, 3);

	LLGI::TextureInitializationParameter texParam;

	if (mode == SimpleTextureRectangleTestMode::RGBA8)
	{
		texParam.Format = LLGI::TextureFormatType::R8G8B8A8_UNORM;
	}
	else if (mode == SimpleTextureRectangleTestMode::RGBA32F)
	{
		texParam.Format = LLGI::TextureFormatType::R32G32B32A32_FLOAT;
	}
	else if (mode == SimpleTextureRectangleTestMode::R8)
	{
		texParam.Format = LLGI::TextureFormatType::R8_UNORM;
	}

	texParam.Size = LLGI::Vec2I(256, 256);

	auto textureDrawn = graphics->CreateTexture(texParam);
	assert(textureDrawn->GetType() == LLGI::TextureType::Color);

	TestHelper::WriteDummyTexture(textureDrawn);

	LLGI::Shader* shader_vs = nullptr;
	LLGI::Shader* shader_ps = nullptr;

	std::vector<LLGI::DataStructure> data_vs;
	std::vector<LLGI::DataStructure> data_ps;

	auto dummy_cb = LLGI::CreateSharedPtr(graphics->CreateBuffer(LLGI::BufferUsageType::Constant, 16));

	if (compiler == nullptr)
	{
		auto binary_vs = TestHelper::LoadData("simple_texture_rectangle.vert.spv");
		auto binary_ps = TestHelper::LoadData("simple_texture_rectangle.frag.spv");

		LLGI::DataStructure d_vs;
		LLGI::DataStructure d_ps;

		d_vs.Data = binary_vs.data();
		d_vs.Size = static_cast<int32_t>(binary_vs.size());
		d_ps.Data = binary_ps.data();
		d_ps.Size = static_cast<int32_t>(binary_ps.size());

		data_vs.push_back(d_vs);
		data_ps.push_back(d_ps);

		shader_vs = graphics->CreateShader(data_vs.data(), static_cast<int32_t>(data_vs.size()));
		shader_ps = graphics->CreateShader(data_ps.data(), static_cast<int32_t>(data_ps.size()));
	}
	else
	{
		LLGI::CompilerResult result_vs;
		LLGI::CompilerResult result_ps;

		if (platform->GetDeviceType() == LLGI::DeviceType::Metal || platform->GetDeviceType() == LLGI::DeviceType::DirectX12 ||
			platform->GetDeviceType() == LLGI::DeviceType::Vulkan || platform->GetDeviceType() == LLGI::DeviceType::WebGPU)
		{
			auto code_vs = TestHelper::LoadData("simple_texture_rectangle.vert");
			auto code_ps = TestHelper::LoadData("simple_texture_rectangle.frag");
			code_vs.push_back(0);
			code_ps.push_back(0);

			compiler->Compile(result_vs, (const char*)code_vs.data(), LLGI::ShaderStageType::Vertex);
			compiler->Compile(result_ps, (const char*)code_ps.data(), LLGI::ShaderStageType::Pixel);
		}
		else
		{
			compiler->Compile(result_vs, code_gl_vs, LLGI::ShaderStageType::Vertex);
			compiler->Compile(result_ps, code_gl_ps, LLGI::ShaderStageType::Pixel);
		}

		for (auto& b : result_vs.Binary)
		{
			LLGI::DataStructure d;
			d.Data = b.data();
			d.Size = static_cast<int32_t>(b.size());
			data_vs.push_back(d);
		}

		for (auto& b : result_ps.Binary)
		{
			LLGI::DataStructure d;
			d.Data = b.data();
			d.Size = static_cast<int32_t>(b.size());
			data_ps.push_back(d);
		}

		shader_vs = graphics->CreateShader(data_vs.data(), static_cast<int32_t>(data_vs.size()));
		shader_ps = graphics->CreateShader(data_ps.data(), static_cast<int32_t>(data_ps.size()));
	}

	std::shared_ptr<LLGI::Buffer> vb;
	std::shared_ptr<LLGI::Buffer> ib;
	TestHelper::CreateRectangle(graphics,
								LLGI::Vec3F(-0.5, 0.5, 0.5),
								LLGI::Vec3F(0.5, -0.5, 0.5),
								LLGI::Color8(255, 255, 255, 255),
								LLGI::Color8(0, 255, 0, 255),
								vb,
								ib);

	std::map<std::shared_ptr<LLGI::RenderPassPipelineState>, std::shared_ptr<LLGI::PipelineState>> pips;

	while (count < 60)
	{
		if (!platform->NewFrame())
		{
			break;
		}

		sfMemoryPool->NewFrame();

		LLGI::Color8 color;
		color.R = count % 255;
		color.G = 0;
		color.B = 0;
		color.A = 255;

		auto renderPass = platform->GetCurrentScreen(color, true, false); // TODO: isDepthClear is false, because it fails with dx12.
		auto renderPassPipelineState = LLGI::CreateSharedPtr(graphics->CreateRenderPassPipelineState(renderPass));

		if (pips.count(renderPassPipelineState) == 0)
		{
			auto pip = graphics->CreatePiplineState();
			pip->VertexLayouts[0] = LLGI::VertexLayoutFormat::R32G32B32_FLOAT;
			pip->VertexLayouts[1] = LLGI::VertexLayoutFormat::R32G32_FLOAT;
			pip->VertexLayouts[2] = LLGI::VertexLayoutFormat::R8G8B8A8_UNORM;
			pip->VertexLayoutNames[0] = "POSITION";
			pip->VertexLayoutNames[1] = "UV";
			pip->VertexLayoutNames[2] = "COLOR";
			pip->VertexLayoutCount = 3;

			pip->SetShader(LLGI::ShaderStageType::Vertex, shader_vs);
			pip->SetShader(LLGI::ShaderStageType::Pixel, shader_ps);
			pip->SetRenderPassPipelineState(renderPassPipelineState.get());
			pip->Compile();

			pips[renderPassPipelineState] = LLGI::CreateSharedPtr(pip);
		}

		auto commandList = commandListPool->Get();
		commandList->Begin();
		commandList->BeginRenderPass(renderPass);
		// commandList->SetConstantBuffer(dummy_cb.get(), LLGI::ShaderStageType::Vertex);
		commandList->SetVertexBuffer(vb.get(), sizeof(SimpleVertex), 0);
		commandList->SetIndexBuffer(ib.get(), 2);
		commandList->SetPipelineState(pips[renderPassPipelineState].get());
		commandList->SetTexture(textureDrawn, LLGI::TextureWrapMode::Repeat, LLGI::TextureMinMagFilter::Nearest, 0);
		commandList->Draw(2);
		commandList->EndRenderPass();
		commandList->End();

		graphics->Execute(commandList);

		platform->Present();
		count++;

		if (TestHelper::GetIsCaptureRequired() && count == 30)
		{
			commandListPool->WaitUntilCompleted();
			auto texture = platform->GetCurrentScreen(LLGI::Color8(), true)->GetRenderTexture(0);
			auto data = graphics->CaptureRenderTarget(texture);

			if (mode == SimpleTextureRectangleTestMode::RGBA8)
			{
				Bitmap2D(data, texture->GetSizeAs2D().X, texture->GetSizeAs2D().Y, texture->GetFormat())
					.Save("SimpleRender.TextureRGB8_" + TestHelper::GetDeviceName(deviceType) + ".png");
			}

			if (mode == SimpleTextureRectangleTestMode::RGBA32F)
			{
				Bitmap2D(data, texture->GetSizeAs2D().X, texture->GetSizeAs2D().Y, texture->GetFormat())
					.Save("SimpleRender.TextureRGB32F_" + TestHelper::GetDeviceName(deviceType) + ".png");
			}

			if (mode == SimpleTextureRectangleTestMode::R8)
			{
				Bitmap2D(data, texture->GetSizeAs2D().X, texture->GetSizeAs2D().Y, texture->GetFormat())
					.Save("SimpleRender.TextureR8_" + TestHelper::GetDeviceName(deviceType) + ".png");
			}
			break;
		}
	}

	commandListPool->WaitUntilCompleted();
	graphics->WaitFinish();

	pips.clear();

	LLGI::SafeRelease(sfMemoryPool);
	LLGI::SafeRelease(textureDrawn);
	LLGI::SafeRelease(shader_vs);
	LLGI::SafeRelease(shader_ps);
	LLGI::SafeRelease(graphics);
	LLGI::SafeRelease(platform);

	LLGI::SafeRelease(compiler);
}

void test_instancing(LLGI::DeviceType deviceType)
{
	int count = 0;

	LLGI::PlatformParameter pp;
	pp.Device = deviceType;
	pp.WaitVSync = true;
	auto window = std::unique_ptr<LLGI::Window>(LLGI::CreateWindow("Instancing", LLGI::Vec2I(1280, 720)));
	auto platform = LLGI::CreateSharedPtr(LLGI::CreatePlatform(pp, window.get()));
	auto graphics = LLGI::CreateSharedPtr(platform->CreateGraphics());

	auto sfMemoryPool = LLGI::CreateSharedPtr(graphics->CreateSingleFrameMemoryPool(1024 * 1024, 128));

	auto commandListPool = std::make_shared<LLGI::CommandListPool>(graphics.get(), sfMemoryPool.get(), 3);

	std::shared_ptr<LLGI::Shader> shader_vs = nullptr;
	std::shared_ptr<LLGI::Shader> shader_ps = nullptr;

	TestHelper::CreateShader(graphics.get(), deviceType, "instancing.vert", "simple_rectangle.frag", shader_vs, shader_ps);

	std::shared_ptr<LLGI::Buffer> vb;
	std::shared_ptr<LLGI::Buffer> ib;
	TestHelper::CreateRectangle(graphics.get(),
								LLGI::Vec3F(-0.2f, 0.2f, 0.5f),
								LLGI::Vec3F(0.2f, -0.2f, 0.5f),
								LLGI::Color8(255, 255, 255, 255),
								LLGI::Color8(0, 255, 0, 255),
								vb,
								ib);

	auto cb = LLGI::CreateSharedPtr(
		graphics->CreateBuffer(LLGI::BufferUsageType::Constant | LLGI::BufferUsageType::MapWrite, sizeof(float) * 4 * 10));

	if (auto buf = static_cast<float*>(cb->Lock()))
	{
		for (int32_t i = 0; i < 10; i++)
		{
			buf[i * 4 + 0] = i * 0.1f;
			buf[i * 4 + 1] = i * 0.1f;
			buf[i * 4 + 2] = 0.0f;
			buf[i * 4 + 3] = 0.0f;
		}
		cb->Unlock();
	}

	std::map<std::shared_ptr<LLGI::RenderPassPipelineState>, std::shared_ptr<LLGI::PipelineState>> pips;

	while (count < 60)
	{
		if (!platform->NewFrame())
			break;

		sfMemoryPool->NewFrame();

		LLGI::Color8 color;
		color.R = count % 255;
		color.G = 0;
		color.B = 0;
		color.A = 255;

		auto renderPass = platform->GetCurrentScreen(color, true, false); // TODO: isDepthClear is false, because it fails with dx12.
		auto renderPassPipelineState = LLGI::CreateSharedPtr(graphics->CreateRenderPassPipelineState(renderPass));

		if (pips.count(renderPassPipelineState) == 0)
		{
			auto pip = graphics->CreatePiplineState();
			pip->VertexLayouts[0] = LLGI::VertexLayoutFormat::R32G32B32_FLOAT;
			pip->VertexLayouts[1] = LLGI::VertexLayoutFormat::R32G32_FLOAT;
			pip->VertexLayouts[2] = LLGI::VertexLayoutFormat::R8G8B8A8_UNORM;
			pip->VertexLayoutNames[0] = "POSITION";
			pip->VertexLayoutNames[1] = "UV";
			pip->VertexLayoutNames[2] = "COLOR";
			pip->VertexLayoutCount = 3;

			pip->SetShader(LLGI::ShaderStageType::Vertex, shader_vs.get());
			pip->SetShader(LLGI::ShaderStageType::Pixel, shader_ps.get());
			pip->SetRenderPassPipelineState(renderPassPipelineState.get());
			pip->Compile();

			pips[renderPassPipelineState] = LLGI::CreateSharedPtr(pip);
		}

		auto commandList = commandListPool->Get();
		commandList->Begin();

		commandList->BeginRenderPass(renderPass);
		commandList->SetVertexBuffer(vb.get(), sizeof(SimpleVertex), 0);
		commandList->SetIndexBuffer(ib.get(), 2);
		commandList->SetPipelineState(pips[renderPassPipelineState].get());
		commandList->SetConstantBuffer(cb.get(), 0);
		commandList->Draw(2, 5);
		commandList->EndRenderPass();
		commandList->End();

		graphics->Execute(commandList);

		platform->Present();
		count++;

		if (TestHelper::GetIsCaptureRequired() && count == 30)
		{
			commandListPool->WaitUntilCompleted();
			auto texture = platform->GetCurrentScreen(LLGI::Color8(), true)->GetRenderTexture(0);
			auto data = graphics->CaptureRenderTarget(texture);

			// save
			std::string path = "SimpleRender.Instancing_" + TestHelper::GetDeviceName(deviceType) + ".png";
			Bitmap2D(data, texture->GetSizeAs2D().X, texture->GetSizeAs2D().Y, texture->GetFormat()).Save(path.c_str());
		}
	}

	commandListPool->WaitUntilCompleted();
	graphics->WaitFinish();
	pips.clear();
}

void test_vertex_structured(LLGI::DeviceType deviceType)
{
	int count = 0;

	LLGI::PlatformParameter pp;
	pp.Device = deviceType;
	pp.WaitVSync = true;
	auto window = std::unique_ptr<LLGI::Window>(LLGI::CreateWindow("Vertex_Structured", LLGI::Vec2I(1280, 720)));
	auto platform = LLGI::CreateSharedPtr(LLGI::CreatePlatform(pp, window.get()));
	auto graphics = LLGI::CreateSharedPtr(platform->CreateGraphics());

	auto sfMemoryPool = LLGI::CreateSharedPtr(graphics->CreateSingleFrameMemoryPool(1024 * 1024, 128));

	auto commandListPool = std::make_shared<LLGI::CommandListPool>(graphics.get(), sfMemoryPool.get(), 3);

	std::shared_ptr<LLGI::Shader> shader_vs = nullptr;
	std::shared_ptr<LLGI::Shader> shader_ps = nullptr;

	TestHelper::CreateShader(graphics.get(), deviceType, "vertex_structured.vert", "simple_rectangle.frag", shader_vs, shader_ps);

	std::shared_ptr<LLGI::Buffer> vb;
	std::shared_ptr<LLGI::Buffer> ib;
	TestHelper::CreateRectangle(graphics.get(),
								LLGI::Vec3F(-0.2f, 0.2f, 0.5f),
								LLGI::Vec3F(0.2f, -0.2f, 0.5f),
								LLGI::Color8(255, 255, 255, 255),
								LLGI::Color8(0, 255, 0, 255),
								vb,
								ib);

	struct InputData
	{
		float value1;
		float value2;
	};

	int dataSize = 6;

	std::vector<InputData> inputData;
	inputData.resize(dataSize);
	for (int i = 0; i < dataSize; i++)
	{
		inputData[i].value1 = static_cast<float>(i) * 0.2f;
		inputData[i].value2 = static_cast<float>(i) * 0.3f;
	}

	std::shared_ptr<LLGI::Buffer> inputBuffer;
	inputBuffer = LLGI::CreateSharedPtr(
		graphics->CreateBuffer(LLGI::BufferUsageType::MapWrite | LLGI::BufferUsageType::CopySrc, sizeof(InputData) * dataSize));

	{
		auto data = (InputData*)inputBuffer->Lock();
		for (int i = 0; i < dataSize; i++)
		{
			data[i] = inputData[i];
		}
		inputBuffer->Unlock();
	}

	std::shared_ptr<LLGI::Buffer> inputComputeBuffer;
	inputComputeBuffer = LLGI::CreateSharedPtr(
		graphics->CreateBuffer(LLGI::BufferUsageType::ComputeWrite | LLGI::BufferUsageType::CopyDst, sizeof(InputData) * dataSize));

	std::map<std::shared_ptr<LLGI::RenderPassPipelineState>, std::shared_ptr<LLGI::PipelineState>> pips;

	while (count < 60)
	{
		if (!platform->NewFrame())
			break;

		sfMemoryPool->NewFrame();

		LLGI::Color8 color;
		color.R = count % 255;
		color.G = 0;
		color.B = 0;
		color.A = 255;

		auto renderPass = platform->GetCurrentScreen(color, true, false); // TODO: isDepthClear is false, because it fails with dx12.
		auto renderPassPipelineState = LLGI::CreateSharedPtr(graphics->CreateRenderPassPipelineState(renderPass));

		if (pips.count(renderPassPipelineState) == 0)
		{
			auto pip = graphics->CreatePiplineState();
			pip->VertexLayouts[0] = LLGI::VertexLayoutFormat::R32G32B32_FLOAT;
			pip->VertexLayouts[1] = LLGI::VertexLayoutFormat::R32G32_FLOAT;
			pip->VertexLayouts[2] = LLGI::VertexLayoutFormat::R8G8B8A8_UNORM;
			pip->VertexLayoutNames[0] = "POSITION";
			pip->VertexLayoutNames[1] = "UV";
			pip->VertexLayoutNames[2] = "COLOR";
			pip->VertexLayoutCount = 3;

			pip->SetShader(LLGI::ShaderStageType::Vertex, shader_vs.get());
			pip->SetShader(LLGI::ShaderStageType::Pixel, shader_ps.get());
			pip->SetRenderPassPipelineState(renderPassPipelineState.get());
			pip->Compile();

			pips[renderPassPipelineState] = LLGI::CreateSharedPtr(pip);
		}

		auto commandList = commandListPool->Get();
		commandList->Begin();
		commandList->CopyBuffer(inputBuffer.get(), inputComputeBuffer.get());
		commandList->BeginRenderPass(renderPass);
		commandList->SetVertexBuffer(vb.get(), sizeof(SimpleVertex), 0);
		commandList->SetIndexBuffer(ib.get(), 2);
		commandList->SetPipelineState(pips[renderPassPipelineState].get());
		commandList->SetComputeBuffer(inputComputeBuffer.get(), sizeof(InputData), 0, true);
		commandList->Draw(2, 5);
		commandList->EndRenderPass();
		commandList->End();

		graphics->Execute(commandList);

		platform->Present();
		count++;

		if (TestHelper::GetIsCaptureRequired() && count == 30)
		{
			commandListPool->WaitUntilCompleted();
			auto texture = platform->GetCurrentScreen(LLGI::Color8(), true)->GetRenderTexture(0);
			auto data = graphics->CaptureRenderTarget(texture);

			// save
			std::string path = "SimpleRender.VertexStructured_" + TestHelper::GetDeviceName(deviceType) + ".png";
			Bitmap2D(data, texture->GetSizeAs2D().X, texture->GetSizeAs2D().Y, texture->GetFormat()).Save(path.c_str());
		}
	}

	pips.clear();
}

void test_storage_buffer_compute_to_vertex_raw_layout(LLGI::DeviceType deviceType)
{
	int count = 0;
	bool verified = false;

	LLGI::PlatformParameter pp;
	pp.Device = deviceType;
	pp.WaitVSync = false;
	auto window = std::unique_ptr<LLGI::Window>(LLGI::CreateWindow("StorageBuffer_ComputeToVertexRawLayout", LLGI::Vec2I(1280, 720)));
	auto platform = LLGI::CreateSharedPtr(LLGI::CreatePlatform(pp, window.get()));
	VERIFY(platform != nullptr);
	auto graphics = LLGI::CreateSharedPtr(platform->CreateGraphics());
	VERIFY(graphics != nullptr);

	auto sfMemoryPool = LLGI::CreateSharedPtr(graphics->CreateSingleFrameMemoryPool(1024 * 1024, 128));
	VERIFY(sfMemoryPool != nullptr);
	auto commandListPool = std::make_shared<LLGI::CommandListPool>(graphics.get(), sfMemoryPool.get(), 3);

	std::shared_ptr<LLGI::Shader> shaderVS = nullptr;
	std::shared_ptr<LLGI::Shader> shaderPS = nullptr;
	std::shared_ptr<LLGI::Shader> shaderCS = nullptr;
	TestHelper::CreateShader(graphics.get(), deviceType, "storage_buffer_raw_transform_render.vert", "simple_rectangle.frag", shaderVS, shaderPS);
	TestHelper::CreateComputeShader(graphics.get(), deviceType, "storage_buffer_raw_transform_write.comp", shaderCS);
	VERIFY(shaderVS != nullptr);
	VERIFY(shaderPS != nullptr);
	VERIFY(shaderCS != nullptr);

	std::shared_ptr<LLGI::Buffer> vertexBuffer;
	std::shared_ptr<LLGI::Buffer> indexBuffer;
	TestHelper::CreateRectangle(graphics.get(),
								LLGI::Vec3F(-0.2f, 0.2f, 0.0f),
								LLGI::Vec3F(0.2f, -0.2f, 0.0f),
								LLGI::Color8(255, 255, 255, 255),
								LLGI::Color8(255, 255, 255, 255),
								vertexBuffer,
								indexBuffer);
	VERIFY(vertexBuffer != nullptr);
	VERIFY(indexBuffer != nullptr);

	const int elementCount = 2;
	const int transformStride = 80;
	const int transformBindingStride = deviceType == LLGI::DeviceType::DirectX12 ? sizeof(uint32_t) : transformStride;
	auto transformBuffer = LLGI::CreateSharedPtr(graphics->CreateBuffer(LLGI::BufferUsageType::ComputeWrite, transformStride * elementCount));
	VERIFY(transformBuffer != nullptr);

	auto computePipeline = LLGI::CreateSharedPtr(graphics->CreatePiplineState());
	VERIFY(computePipeline != nullptr);
	computePipeline->SetShader(LLGI::ShaderStageType::Compute, shaderCS.get());
	VERIFY(computePipeline->Compile());

	std::map<std::shared_ptr<LLGI::RenderPassPipelineState>, std::shared_ptr<LLGI::PipelineState>> pips;

	while (count < 60)
	{
		if (!platform->NewFrame())
			break;

		sfMemoryPool->NewFrame();

		auto renderPass = platform->GetCurrentScreen(LLGI::Color8(0, 0, 0, 255), true, false);
		auto renderPassPipelineState = LLGI::CreateSharedPtr(graphics->CreateRenderPassPipelineState(renderPass));

		if (pips.count(renderPassPipelineState) == 0)
		{
			auto renderPipeline = graphics->CreatePiplineState();
			renderPipeline->VertexLayouts[0] = LLGI::VertexLayoutFormat::R32G32B32_FLOAT;
			renderPipeline->VertexLayouts[1] = LLGI::VertexLayoutFormat::R32G32_FLOAT;
			renderPipeline->VertexLayouts[2] = LLGI::VertexLayoutFormat::R8G8B8A8_UNORM;
			renderPipeline->VertexLayoutNames[0] = "POSITION";
			renderPipeline->VertexLayoutNames[1] = "UV";
			renderPipeline->VertexLayoutNames[2] = "COLOR";
			renderPipeline->VertexLayoutCount = 3;
			renderPipeline->SetShader(LLGI::ShaderStageType::Vertex, shaderVS.get());
			renderPipeline->SetShader(LLGI::ShaderStageType::Pixel, shaderPS.get());
			renderPipeline->SetRenderPassPipelineState(renderPassPipelineState.get());
			VERIFY(renderPipeline->Compile());

			pips[renderPassPipelineState] = LLGI::CreateSharedPtr(renderPipeline);
		}

		auto commandList = commandListPool->Get();
		commandList->Begin();
		commandList->SetPipelineState(computePipeline.get());
		commandList->SetComputeBuffer(transformBuffer.get(), transformBindingStride, 0, false);
		commandList->Dispatch(elementCount, 1, 1, 1, 1, 1);
		commandList->BeginRenderPass(renderPass);
		commandList->SetVertexBuffer(vertexBuffer.get(), sizeof(SimpleVertex), 0);
		commandList->SetIndexBuffer(indexBuffer.get(), 2);
		commandList->SetPipelineState(pips[renderPassPipelineState].get());
		commandList->SetComputeBuffer(transformBuffer.get(), transformBindingStride, 0, true);
		commandList->Draw(2, elementCount);
		commandList->EndRenderPass();
		commandList->End();

		graphics->Execute(commandList);

		if (count == 29)
		{
			commandListPool->WaitUntilCompleted();
			auto texture = renderPass->GetRenderTexture(0);
			auto data = graphics->CaptureRenderTarget(texture);
			Bitmap2D bitmap(data, texture->GetSizeAs2D().X, texture->GetSizeAs2D().Y, texture->GetFormat());

			auto findColor = [&bitmap](int xMin, int xMax, bool red) {
				for (int y = 280; y < 440; y++)
				{
					for (int x = xMin; x < xMax; x++)
					{
						const auto c = bitmap.GetPixel(x, y);
						if (red)
						{
							if (c.r > 200 && c.g < 80 && c.b < 80)
							{
								return true;
							}
						}
						else
						{
							if (c.g > 200 && c.r < 80 && c.b < 80)
							{
								return true;
							}
						}
					}
				}
				return false;
			};

			if (TestHelper::GetIsCaptureRequired())
			{
				std::string path = "SimpleRender.StorageBuffer.ComputeToVertexRawLayout_" + TestHelper::GetDeviceName(deviceType) + ".png";
				bitmap.Save(path.c_str());
			}

			VERIFY(findColor(250, 520, true));
			VERIFY(findColor(760, 1030, false));
			verified = true;
		}

		platform->Present();
		count++;
	}

	commandListPool->WaitUntilCompleted();
	graphics->WaitFinish();
	pips.clear();
	VERIFY(verified);
}

void test_storage_buffer_compute_to_vertex_multi_slot_raw_layout(LLGI::DeviceType deviceType)
{
	int count = 0;
	bool verified = false;

	LLGI::PlatformParameter pp;
	pp.Device = deviceType;
	pp.WaitVSync = false;
	auto window = std::unique_ptr<LLGI::Window>(LLGI::CreateWindow("StorageBuffer_ComputeToVertexMultiSlotRawLayout", LLGI::Vec2I(1280, 720)));
	auto platform = LLGI::CreateSharedPtr(LLGI::CreatePlatform(pp, window.get()));
	VERIFY(platform != nullptr);
	auto graphics = LLGI::CreateSharedPtr(platform->CreateGraphics());
	VERIFY(graphics != nullptr);

	auto sfMemoryPool = LLGI::CreateSharedPtr(graphics->CreateSingleFrameMemoryPool(1024 * 1024, 128));
	VERIFY(sfMemoryPool != nullptr);
	auto commandListPool = std::make_shared<LLGI::CommandListPool>(graphics.get(), sfMemoryPool.get(), 3);

	std::shared_ptr<LLGI::Shader> shaderVS = nullptr;
	std::shared_ptr<LLGI::Shader> shaderPS = nullptr;
	std::shared_ptr<LLGI::Shader> shaderCS = nullptr;
	TestHelper::CreateShader(graphics.get(), deviceType, "storage_buffer_multi_slot_raw_render.vert", "simple_rectangle.frag", shaderVS, shaderPS);
	TestHelper::CreateComputeShader(graphics.get(), deviceType, "storage_buffer_multi_slot_raw_write.comp", shaderCS);
	VERIFY(shaderVS != nullptr);
	VERIFY(shaderPS != nullptr);
	VERIFY(shaderCS != nullptr);

	std::shared_ptr<LLGI::Buffer> vertexBuffer;
	std::shared_ptr<LLGI::Buffer> indexBuffer;
	TestHelper::CreateRectangle(graphics.get(),
								LLGI::Vec3F(-0.2f, 0.2f, 0.0f),
								LLGI::Vec3F(0.2f, -0.2f, 0.0f),
								LLGI::Color8(255, 255, 255, 255),
								LLGI::Color8(255, 255, 255, 255),
								vertexBuffer,
								indexBuffer);
	VERIFY(vertexBuffer != nullptr);
	VERIFY(indexBuffer != nullptr);

	const int elementCount = 3;
	const int transformStride = 80;
	const int attributeStride = 16;
	const int transformBindingStride = deviceType == LLGI::DeviceType::DirectX12 ? sizeof(uint32_t) : transformStride;
	const int attributeBindingStride = deviceType == LLGI::DeviceType::DirectX12 ? sizeof(uint32_t) : attributeStride;
	auto transformBuffer = LLGI::CreateSharedPtr(graphics->CreateBuffer(LLGI::BufferUsageType::ComputeWrite, transformStride * elementCount));
	auto attributeBuffer = LLGI::CreateSharedPtr(graphics->CreateBuffer(LLGI::BufferUsageType::ComputeWrite, attributeStride * elementCount));
	VERIFY(transformBuffer != nullptr);
	VERIFY(attributeBuffer != nullptr);

	auto computePipeline = LLGI::CreateSharedPtr(graphics->CreatePiplineState());
	VERIFY(computePipeline != nullptr);
	computePipeline->SetShader(LLGI::ShaderStageType::Compute, shaderCS.get());
	VERIFY(computePipeline->Compile());

	std::map<std::shared_ptr<LLGI::RenderPassPipelineState>, std::shared_ptr<LLGI::PipelineState>> pips;

	while (count < 60)
	{
		if (!platform->NewFrame())
			break;

		sfMemoryPool->NewFrame();

		auto renderPass = platform->GetCurrentScreen(LLGI::Color8(0, 0, 0, 255), true, false);
		auto renderPassPipelineState = LLGI::CreateSharedPtr(graphics->CreateRenderPassPipelineState(renderPass));

		if (pips.count(renderPassPipelineState) == 0)
		{
			auto renderPipeline = graphics->CreatePiplineState();
			renderPipeline->VertexLayouts[0] = LLGI::VertexLayoutFormat::R32G32B32_FLOAT;
			renderPipeline->VertexLayouts[1] = LLGI::VertexLayoutFormat::R32G32_FLOAT;
			renderPipeline->VertexLayouts[2] = LLGI::VertexLayoutFormat::R8G8B8A8_UNORM;
			renderPipeline->VertexLayoutNames[0] = "POSITION";
			renderPipeline->VertexLayoutNames[1] = "UV";
			renderPipeline->VertexLayoutNames[2] = "COLOR";
			renderPipeline->VertexLayoutCount = 3;
			renderPipeline->SetShader(LLGI::ShaderStageType::Vertex, shaderVS.get());
			renderPipeline->SetShader(LLGI::ShaderStageType::Pixel, shaderPS.get());
			renderPipeline->SetRenderPassPipelineState(renderPassPipelineState.get());
			VERIFY(renderPipeline->Compile());

			pips[renderPassPipelineState] = LLGI::CreateSharedPtr(renderPipeline);
		}

		auto commandList = commandListPool->Get();
		commandList->Begin();
		commandList->SetPipelineState(computePipeline.get());
		commandList->SetComputeBuffer(transformBuffer.get(), transformBindingStride, 0, false);
		commandList->SetComputeBuffer(attributeBuffer.get(), attributeBindingStride, 1, false);
		commandList->Dispatch(elementCount, 1, 1, 1, 1, 1);
		commandList->BeginRenderPass(renderPass);
		commandList->SetVertexBuffer(vertexBuffer.get(), sizeof(SimpleVertex), 0);
		commandList->SetIndexBuffer(indexBuffer.get(), 2);
		commandList->SetPipelineState(pips[renderPassPipelineState].get());
		commandList->SetComputeBuffer(transformBuffer.get(), transformBindingStride, 0, true);
		commandList->SetComputeBuffer(attributeBuffer.get(), attributeBindingStride, 1, true);
		commandList->Draw(2, elementCount);
		commandList->EndRenderPass();
		commandList->End();

		graphics->Execute(commandList);

		if (count == 29)
		{
			commandListPool->WaitUntilCompleted();
			auto texture = renderPass->GetRenderTexture(0);
			auto data = graphics->CaptureRenderTarget(texture);
			Bitmap2D bitmap(data, texture->GetSizeAs2D().X, texture->GetSizeAs2D().Y, texture->GetFormat());

			auto findColor = [&bitmap](int xMin, int xMax, int yMin, int yMax, int channel) {
				for (int y = yMin; y < yMax; y++)
				{
					for (int x = xMin; x < xMax; x++)
					{
						const auto c = bitmap.GetPixel(x, y);
						const int values[3] = {c.r, c.g, c.b};
						if (values[channel] > 180 && values[(channel + 1) % 3] < 100 && values[(channel + 2) % 3] < 100)
						{
							return true;
						}
					}
				}
				return false;
			};

			if (TestHelper::GetIsCaptureRequired())
			{
				std::string path = "SimpleRender.StorageBuffer.ComputeToVertexMultiSlotRawLayout_" + TestHelper::GetDeviceName(deviceType) + ".png";
				bitmap.Save(path.c_str());
			}

			VERIFY(findColor(210, 430, 330, 520, 0));
			VERIFY(findColor(520, 760, 250, 470, 1));
			VERIFY(findColor(850, 1070, 170, 390, 2));
			verified = true;
		}

		platform->Present();
		count++;
	}

	commandListPool->WaitUntilCompleted();
	graphics->WaitFinish();
	pips.clear();
	VERIFY(verified);
}

void test_storage_buffer_history_ribbon_vertex_expansion(LLGI::DeviceType deviceType)
{
	int count = 0;
	bool verified = false;

	LLGI::PlatformParameter pp;
	pp.Device = deviceType;
	pp.WaitVSync = false;
	auto window = std::unique_ptr<LLGI::Window>(LLGI::CreateWindow("StorageBuffer_HistoryRibbonVertexExpansion", LLGI::Vec2I(1280, 720)));
	auto platform = LLGI::CreateSharedPtr(LLGI::CreatePlatform(pp, window.get()));
	VERIFY(platform != nullptr);
	auto graphics = LLGI::CreateSharedPtr(platform->CreateGraphics());
	VERIFY(graphics != nullptr);

	auto sfMemoryPool = LLGI::CreateSharedPtr(graphics->CreateSingleFrameMemoryPool(1024 * 1024, 128));
	VERIFY(sfMemoryPool != nullptr);
	auto commandListPool = std::make_shared<LLGI::CommandListPool>(graphics.get(), sfMemoryPool.get(), 3);

	std::shared_ptr<LLGI::Shader> shaderVS = nullptr;
	std::shared_ptr<LLGI::Shader> shaderPS = nullptr;
	std::shared_ptr<LLGI::Shader> shaderCS = nullptr;
	TestHelper::CreateShader(graphics.get(), deviceType, "storage_buffer_history_ribbon_render.vert", "simple_rectangle.frag", shaderVS, shaderPS);
	TestHelper::CreateComputeShader(graphics.get(), deviceType, "storage_buffer_history_ribbon_write.comp", shaderCS);
	VERIFY(shaderVS != nullptr);
	VERIFY(shaderPS != nullptr);
	VERIFY(shaderCS != nullptr);

	constexpr int vertexCount = 8;
	constexpr int indexCount = 18;
	auto vertexBuffer = LLGI::CreateSharedPtr(
		graphics->CreateBuffer(LLGI::BufferUsageType::Vertex | LLGI::BufferUsageType::MapWrite, sizeof(SimpleVertex) * vertexCount));
	auto indexBuffer =
		LLGI::CreateSharedPtr(graphics->CreateBuffer(LLGI::BufferUsageType::Index | LLGI::BufferUsageType::MapWrite, sizeof(uint16_t) * indexCount));
	VERIFY(vertexBuffer != nullptr);
	VERIFY(indexBuffer != nullptr);

	{
		auto vertices = static_cast<SimpleVertex*>(vertexBuffer->Lock());
		for (int i = 0; i < vertexCount; i++)
		{
			vertices[i].Pos = LLGI::Vec3F(0.0f, 0.0f, 0.0f);
			vertices[i].UV = LLGI::Vec2F(0.0f, 0.0f);
			vertices[i].Color = LLGI::Color8(255, 255, 255, 255);
		}
		vertexBuffer->Unlock();

		const uint16_t indices[indexCount] = {
			0, 2, 3, 0, 3, 1,
			2, 4, 5, 2, 5, 3,
			4, 6, 7, 4, 7, 5,
		};
		auto indexData = static_cast<uint16_t*>(indexBuffer->Lock());
		for (int i = 0; i < indexCount; i++)
		{
			indexData[i] = indices[i];
		}
		indexBuffer->Unlock();
	}

	const int historyRecordStride = 32;
	const int ribbonRecordStride = 32;
	const int historyBindingStride = historyRecordStride;
	const int ribbonBindingStride = ribbonRecordStride;
	auto historyBuffer = LLGI::CreateSharedPtr(graphics->CreateBuffer(LLGI::BufferUsageType::ComputeWrite, historyRecordStride * vertexCount));
	auto ribbonBuffer = LLGI::CreateSharedPtr(graphics->CreateBuffer(LLGI::BufferUsageType::ComputeWrite, ribbonRecordStride * 2));
	VERIFY(historyBuffer != nullptr);
	VERIFY(ribbonBuffer != nullptr);

	auto computePipeline = LLGI::CreateSharedPtr(graphics->CreatePiplineState());
	VERIFY(computePipeline != nullptr);
	computePipeline->SetShader(LLGI::ShaderStageType::Compute, shaderCS.get());
	VERIFY(computePipeline->Compile());

	std::map<std::shared_ptr<LLGI::RenderPassPipelineState>, std::shared_ptr<LLGI::PipelineState>> pips;

	while (count < 60)
	{
		if (!platform->NewFrame())
			break;

		sfMemoryPool->NewFrame();

		auto renderPass = platform->GetCurrentScreen(LLGI::Color8(0, 0, 0, 255), true, false);
		auto renderPassPipelineState = LLGI::CreateSharedPtr(graphics->CreateRenderPassPipelineState(renderPass));

		if (pips.count(renderPassPipelineState) == 0)
		{
			auto renderPipeline = graphics->CreatePiplineState();
			renderPipeline->VertexLayouts[0] = LLGI::VertexLayoutFormat::R32G32B32_FLOAT;
			renderPipeline->VertexLayouts[1] = LLGI::VertexLayoutFormat::R32G32_FLOAT;
			renderPipeline->VertexLayouts[2] = LLGI::VertexLayoutFormat::R8G8B8A8_UNORM;
			renderPipeline->VertexLayoutNames[0] = "POSITION";
			renderPipeline->VertexLayoutNames[1] = "UV";
			renderPipeline->VertexLayoutNames[2] = "COLOR";
			renderPipeline->VertexLayoutCount = 3;
			renderPipeline->SetShader(LLGI::ShaderStageType::Vertex, shaderVS.get());
			renderPipeline->SetShader(LLGI::ShaderStageType::Pixel, shaderPS.get());
			renderPipeline->SetRenderPassPipelineState(renderPassPipelineState.get());
			VERIFY(renderPipeline->Compile());

			pips[renderPassPipelineState] = LLGI::CreateSharedPtr(renderPipeline);
		}

		auto commandList = commandListPool->Get();
		commandList->Begin();
		commandList->SetPipelineState(computePipeline.get());
		commandList->SetComputeBuffer(historyBuffer.get(), historyBindingStride, 0, false);
		commandList->SetComputeBuffer(ribbonBuffer.get(), ribbonBindingStride, 1, false);
		commandList->Dispatch(vertexCount, 1, 1, 1, 1, 1);
		commandList->BeginRenderPass(renderPass);
		commandList->SetVertexBuffer(vertexBuffer.get(), sizeof(SimpleVertex), 0);
		commandList->SetIndexBuffer(indexBuffer.get(), 2);
		commandList->SetPipelineState(pips[renderPassPipelineState].get());
		commandList->SetComputeBuffer(historyBuffer.get(), historyBindingStride, 0, true);
		commandList->SetComputeBuffer(ribbonBuffer.get(), ribbonBindingStride, 1, true);
		commandList->Draw(6, 2);
		commandList->EndRenderPass();
		commandList->End();

		graphics->Execute(commandList);

		if (count == 29)
		{
			commandListPool->WaitUntilCompleted();
			auto texture = renderPass->GetRenderTexture(0);
			auto data = graphics->CaptureRenderTarget(texture);
			Bitmap2D bitmap(data, texture->GetSizeAs2D().X, texture->GetSizeAs2D().Y, texture->GetFormat());

			auto findColor = [&bitmap](int xMin, int xMax, int channel) {
				for (int y = 80; y < 640; y++)
				{
					for (int x = xMin; x < xMax; x++)
					{
						const auto c = bitmap.GetPixel(x, y);
						const int values[3] = {c.r, c.g, c.b};
						if (values[channel] > 180 && values[(channel + 1) % 3] < 100 && values[(channel + 2) % 3] < 100)
						{
							return true;
						}
					}
				}
				return false;
			};

			if (TestHelper::GetIsCaptureRequired())
			{
				std::string path = "SimpleRender.StorageBuffer.HistoryRibbonVertexExpansion_" + TestHelper::GetDeviceName(deviceType) + ".png";
				bitmap.Save(path.c_str());
			}

			VERIFY(findColor(80, 620, 0));
			VERIFY(findColor(660, 1220, 1));
			verified = true;
		}

		platform->Present();
		count++;
	}

	commandListPool->WaitUntilCompleted();
	graphics->WaitFinish();
	pips.clear();
	VERIFY(verified);
}

void test_vtf(LLGI::DeviceType deviceType)
{
	int count = 0;

	LLGI::PlatformParameter pp;
	pp.Device = deviceType;
	pp.WaitVSync = true;
	auto window = std::unique_ptr<LLGI::Window>(LLGI::CreateWindow("VTF", LLGI::Vec2I(1280, 720)));
	auto platform = LLGI::CreateSharedPtr(LLGI::CreatePlatform(pp, window.get()));
	auto graphics = LLGI::CreateSharedPtr(platform->CreateGraphics());

	auto sfMemoryPool = LLGI::CreateSharedPtr(graphics->CreateSingleFrameMemoryPool(1024 * 1024, 128));

	auto commandListPool = std::make_shared<LLGI::CommandListPool>(graphics.get(), sfMemoryPool.get(), 3);

	std::shared_ptr<LLGI::Shader> shader_vs = nullptr;
	std::shared_ptr<LLGI::Shader> shader_ps = nullptr;

	TestHelper::CreateShader(graphics.get(), deviceType, "vtf.vert", "simple_rectangle.frag", shader_vs, shader_ps);

	LLGI::TextureInitializationParameter texParam;
	texParam.Size = LLGI::Vec2I(16, 16);
	texParam.Format = LLGI::TextureFormatType::R8G8B8A8_UNORM;
	auto textureDrawn = LLGI::CreateSharedPtr(graphics->CreateTexture(texParam));

	auto texBuf = static_cast<LLGI::Color8*>(textureDrawn->Lock());
	for (size_t i = 0; i < 16 * 16; i++)
	{
		texBuf[i].R = 127;
		texBuf[i].G = 127;
		texBuf[i].B = 127;
		texBuf[i].A = 127;
	}
	textureDrawn->Unlock();

	std::shared_ptr<LLGI::Buffer> vb;
	std::shared_ptr<LLGI::Buffer> ib;
	TestHelper::CreateRectangle(graphics.get(),
								LLGI::Vec3F(-0.2f, 0.2f, 0.5f),
								LLGI::Vec3F(0.2f, -0.2f, 0.5f),
								LLGI::Color8(255, 255, 255, 255),
								LLGI::Color8(255, 255, 255, 255),
								vb,
								ib);

	std::map<std::shared_ptr<LLGI::RenderPassPipelineState>, std::shared_ptr<LLGI::PipelineState>> pips;

	while (count < 60)
	{
		if (!platform->NewFrame())
			break;

		sfMemoryPool->NewFrame();

		LLGI::Color8 color;
		color.R = count % 255;
		color.G = 0;
		color.B = 0;
		color.A = 255;

		auto renderPass = platform->GetCurrentScreen(color, true, false);
		auto renderPassPipelineState = LLGI::CreateSharedPtr(graphics->CreateRenderPassPipelineState(renderPass));

		if (pips.count(renderPassPipelineState) == 0)
		{
			auto pip = graphics->CreatePiplineState();
			pip->VertexLayouts[0] = LLGI::VertexLayoutFormat::R32G32B32_FLOAT;
			pip->VertexLayouts[1] = LLGI::VertexLayoutFormat::R32G32_FLOAT;
			pip->VertexLayouts[2] = LLGI::VertexLayoutFormat::R8G8B8A8_UNORM;
			pip->VertexLayoutNames[0] = "POSITION";
			pip->VertexLayoutNames[1] = "UV";
			pip->VertexLayoutNames[2] = "COLOR";
			pip->VertexLayoutCount = 3;

			pip->SetShader(LLGI::ShaderStageType::Vertex, shader_vs.get());
			pip->SetShader(LLGI::ShaderStageType::Pixel, shader_ps.get());
			pip->SetRenderPassPipelineState(renderPassPipelineState.get());
			pip->Compile();

			pips[renderPassPipelineState] = LLGI::CreateSharedPtr(pip);
		}

		auto commandList = commandListPool->Get();
		commandList->Begin();

		commandList->BeginRenderPass(renderPass);
		commandList->SetVertexBuffer(vb.get(), sizeof(SimpleVertex), 0);
		commandList->SetIndexBuffer(ib.get(), 2);
		commandList->SetTexture(textureDrawn.get(), LLGI::TextureWrapMode::Clamp, LLGI::TextureMinMagFilter::Linear, 0);
		commandList->SetPipelineState(pips[renderPassPipelineState].get());
		commandList->Draw(2, 5);
		commandList->EndRenderPass();
		commandList->End();

		graphics->Execute(commandList);

		platform->Present();
		count++;

		if (TestHelper::GetIsCaptureRequired() && count == 30)
		{
			commandListPool->WaitUntilCompleted();
			auto texture = platform->GetCurrentScreen(LLGI::Color8(), true)->GetRenderTexture(0);
			auto data = graphics->CaptureRenderTarget(texture);

			// save
			std::string path = "SimpleRender.VTF_" + TestHelper::GetDeviceName(deviceType) + ".png";
			Bitmap2D(data, texture->GetSizeAs2D().X, texture->GetSizeAs2D().Y, texture->GetFormat()).Save(path.c_str());
		}
	}

	commandListPool->WaitUntilCompleted();
	graphics->WaitFinish();
	pips.clear();
}

TestRegister SimpleRender_BasicTriangle("SimpleRender.BasicTriangle", [](LLGI::DeviceType device) -> void {
	test_simple_rectangle(device, SingleRectangleTestMode::Triangle);
});

TestRegister SimpleRender_BasicLine("SimpleRender.BasicLine",
									[](LLGI::DeviceType device) -> void { test_simple_rectangle(device, SingleRectangleTestMode::Line); });

TestRegister SimpleRender_BasicPoint("SimpleRender.BasicPoint", [](LLGI::DeviceType device) -> void {
	test_simple_rectangle(device, SingleRectangleTestMode::Point);
});

TestRegister SimpleRender_IndexOffset("SimpleRender.IndexOffset", [](LLGI::DeviceType device) -> void { test_index_offset(device); });

TestRegister SimpleRender_ConstantLT("SimpleRender.ConstantLT", [](LLGI::DeviceType device) -> void {
	test_simple_constant_rectangle(LLGI::ConstantBufferType::LongTime, device);
});

TestRegister SimpleRender_ConstantST("SimpleRender.ConstantST", [](LLGI::DeviceType device) -> void {
	test_simple_constant_rectangle(LLGI::ConstantBufferType::ShortTime, device);
});

TestRegister SimpleRender_Tex_RGBA8("SimpleRender.Texture_RGBA8", [](LLGI::DeviceType device) -> void {
	test_simple_texture_rectangle(device, SimpleTextureRectangleTestMode::RGBA8);
});

TestRegister SimpleRender_Tex_RGBA32F("SimpleRender.Texture_RGBA32F", [](LLGI::DeviceType device) -> void {
	test_simple_texture_rectangle(device, SimpleTextureRectangleTestMode::RGBA32F);
});
TestRegister SimpleRender_Tex_R8("SimpleRender.Texture_R8", [](LLGI::DeviceType device) -> void {
	test_simple_texture_rectangle(device, SimpleTextureRectangleTestMode::R8);
});

TestRegister SimpleRender_Instansing("SimpleRender.Instansing", [](LLGI::DeviceType device) -> void { test_instancing(device); });

TestRegister SimpleRender_VertexStructured("SimpleRender.VertexStructured",
										   [](LLGI::DeviceType device) -> void { test_vertex_structured(device); });

TestRegister SimpleRender_StorageBufferComputeToVertexRawLayout("SimpleRender.StorageBuffer.ComputeToVertexRawLayout",
																[](LLGI::DeviceType device) -> void {
																	test_storage_buffer_compute_to_vertex_raw_layout(device);
																});

TestRegister SimpleRender_StorageBufferComputeToVertexMultiSlotRawLayout("SimpleRender.StorageBuffer.ComputeToVertexMultiSlotRawLayout",
																		 [](LLGI::DeviceType device) -> void {
																			 test_storage_buffer_compute_to_vertex_multi_slot_raw_layout(device);
																		 });

TestRegister SimpleRender_StorageBufferHistoryRibbonVertexExpansion("SimpleRender.StorageBuffer.HistoryRibbonVertexExpansion",
																	[](LLGI::DeviceType device) -> void {
																		test_storage_buffer_history_ribbon_vertex_expansion(device);
																	});

TestRegister SimpleRender_VTF("SimpleRender.VTF", [](LLGI::DeviceType device) -> void { test_vtf(device); });

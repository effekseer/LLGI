#include "TestHelper.h"
#include "test.h"

#include <LLGI.Buffer.h>
#include <Utils/LLGI.CommandListPool.h>
#include <cmath>
#include <cstring>

struct InputData
{
	float value1;
	float value2;
};

struct OutputData
{
	float value;
};

struct StructuredInputData
{
	float position[3];
	uint32_t reserved;
	uint32_t normal;
	uint32_t tangent;
	uint32_t uv;
	uint32_t color;
};

struct StructuredOutputData
{
	float position[3];
	uint32_t normal;
	uint32_t tangent;
	uint32_t color;
	uint32_t padding0;
	uint32_t padding1;
};

struct SlotTransitionData
{
	float position[3];
	uint32_t direction;
};

struct SlotTransitionOutputData
{
	float position[3];
	uint32_t direction;
	uint32_t checksum;
	uint32_t padding0;
	uint32_t padding1;
	uint32_t padding2;
};

struct MultiSlotInputA
{
	float values[3];
	uint32_t tag;
};

struct MultiSlotInputB
{
	uint32_t x;
	uint32_t y;
	uint32_t z;
	uint32_t w;
};

struct MultiSlotOutputA
{
	float value;
	uint32_t tag;
	uint32_t mix;
	uint32_t padding;
};

struct MultiSlotOutputB
{
	float values[3];
	uint32_t checksum;
};

struct StructuredMatrixRecordInput
{
	uint32_t flags;
	float age;
	uint32_t packed[2];
	float transform[4][3];
	float direction[3];
	uint32_t color;
};

struct StructuredMatrixRecordOutput
{
	float translation[3];
	uint32_t flags;
	float direction[3];
	uint32_t color;
	float axis0[3];
	uint32_t checksum;
};

struct MatrixIndexingInput
{
	float clip[4][4];
	float transform[4][3];
	float local[4];
	uint32_t row;
	uint32_t flags;
	uint32_t padding[2];
};

struct MatrixIndexingOutput
{
	float clip[4];
	float indexed[3];
	uint32_t checksum;
};

static bool NearlyEqual(float a, float b, float epsilon = 0.001f)
{
	return std::fabs(a - b) <= epsilon;
}

static uint32_t FloatAsUint(float value)
{
	uint32_t ret = 0;
	std::memcpy(&ret, &value, sizeof(ret));
	return ret;
}

static void TransformPoint(float dst[3], const float transform[4][3], const float local[4])
{
	for (int axis = 0; axis < 3; axis++)
	{
		dst[axis] = local[0] * transform[0][axis] + local[1] * transform[1][axis] + local[2] * transform[2][axis] +
					local[3] * transform[3][axis];
	}
}

static void TransformClip(float dst[4], const float clip[4][4], const float point[4])
{
	for (int row = 0; row < 4; row++)
	{
		dst[row] = clip[row][0] * point[0] + clip[row][1] * point[1] + clip[row][2] * point[2] + clip[row][3] * point[3];
	}
}

void test_compute_shader_compute_buffer(LLGI::DeviceType deviceType, bool is_read_only)
{
	LLGI::PlatformParameter pp;
	pp.Device = deviceType;
	pp.WaitVSync = true;
	auto window = std::unique_ptr<LLGI::Window>(LLGI::CreateWindow("Capture", LLGI::Vec2I(1280, 720)));
	auto platform = LLGI::CreateSharedPtr(LLGI::CreatePlatform(pp, window.get()));
	auto graphics = LLGI::CreateSharedPtr(platform->CreateGraphics());

	auto sfMemoryPool = LLGI::CreateSharedPtr(graphics->CreateSingleFrameMemoryPool(1024 * 1024, 128));

	auto commandListPool = std::make_shared<LLGI::CommandListPool>(graphics.get(), sfMemoryPool.get(), 3);

	std::shared_ptr<LLGI::Shader> shader_cs = nullptr;

	if (is_read_only)
	{
		TestHelper::CreateComputeShader(graphics.get(), deviceType, "readwrite.comp", shader_cs);
	}
	else
	{
		TestHelper::CreateComputeShader(graphics.get(), deviceType, "basic.comp", shader_cs);
	}

	auto pip = LLGI::CreateSharedPtr(graphics->CreatePiplineState());
	pip->SetShader(LLGI::ShaderStageType::Compute, shader_cs.get());
	if (!pip->Compile())
	{
		abort();
	}

	int dataSize = 256;

	std::vector<InputData> inputData;
	inputData.resize(dataSize);
	for (int i = 0; i < dataSize; i++)
	{
		inputData[i].value1 = (float)i * 2;
		inputData[i].value2 = (float)i * 2 + 1;
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

	std::shared_ptr<LLGI::Buffer> outputBuffer;
	outputBuffer = LLGI::CreateSharedPtr(
		graphics->CreateBuffer(LLGI::BufferUsageType::MapRead | LLGI::BufferUsageType::CopyDst, sizeof(OutputData) * dataSize));

	std::shared_ptr<LLGI::Buffer> inputComputeBuffer;
	inputComputeBuffer = LLGI::CreateSharedPtr(
		graphics->CreateBuffer(LLGI::BufferUsageType::ComputeWrite | LLGI::BufferUsageType::CopyDst, sizeof(InputData) * dataSize));

	std::shared_ptr<LLGI::Buffer> outputComputeBuffer;
	outputComputeBuffer = LLGI::CreateSharedPtr(
		graphics->CreateBuffer(LLGI::BufferUsageType::ComputeWrite | LLGI::BufferUsageType::CopySrc, sizeof(OutputData) * dataSize));

	std::shared_ptr<LLGI::Buffer> constantBuffer;
	constantBuffer =
		LLGI::CreateSharedPtr(graphics->CreateBuffer(LLGI::BufferUsageType::Constant | LLGI::BufferUsageType::MapWrite, sizeof(float)));

	const int offsetValue = 100;

	{
		auto data = (float*)constantBuffer->Lock();
		data[0] = offsetValue;
		constantBuffer->Unlock();
	}

	if (!platform->NewFrame())
		return;

	sfMemoryPool->NewFrame();

	auto commandList = commandListPool->Get();
	commandList->Begin();
	commandList->CopyBuffer(inputBuffer.get(), inputComputeBuffer.get());
	commandList->BeginComputePass();
	commandList->SetPipelineState(pip.get());
	commandList->SetComputeBuffer(inputComputeBuffer.get(), sizeof(InputData), 0, is_read_only);
	commandList->SetComputeBuffer(outputComputeBuffer.get(), sizeof(OutputData), 1, false);
	commandList->SetConstantBuffer(constantBuffer.get(), 0);
	commandList->Dispatch(dataSize, 1, 1, 1, 1, 1);
	commandList->EndComputePass();
	commandList->CopyBuffer(outputComputeBuffer.get(), outputBuffer.get());
	commandList->End();

	graphics->Execute(commandList);
	graphics->WaitFinish();

	{
		auto dst = (OutputData*)outputBuffer->Lock();
		if (dst == nullptr)
		{
			abort();
		}

		for (int i = 0; i < dataSize; i++)
		{
			const auto expected = inputData[i].value1 * inputData[i].value2 + offsetValue;
			const auto actual = dst[i].value;
			if (expected != actual)
			{
				abort();
			}
		}

		outputBuffer->Unlock();
	}

	platform->Present();
}

void test_compute_shader_storage_buffer_structured_slot1(LLGI::DeviceType deviceType)
{
	LLGI::PlatformParameter pp;
	pp.Device = deviceType;
	pp.WaitVSync = true;
	auto window = std::unique_ptr<LLGI::Window>(LLGI::CreateWindow("ComputeShaderStorageBufferStructuredSlot1", LLGI::Vec2I(1280, 720)));
	auto platform = LLGI::CreateSharedPtr(LLGI::CreatePlatform(pp, window.get()));
	auto graphics = LLGI::CreateSharedPtr(platform->CreateGraphics());

	auto sfMemoryPool = LLGI::CreateSharedPtr(graphics->CreateSingleFrameMemoryPool(1024 * 1024, 128));
	auto commandListPool = std::make_shared<LLGI::CommandListPool>(graphics.get(), sfMemoryPool.get(), 3);

	std::shared_ptr<LLGI::Shader> shaderCS = nullptr;
	TestHelper::CreateComputeShader(graphics.get(), deviceType, "storage_buffer_structured_slot1.comp", shaderCS);

	auto pipeline = LLGI::CreateSharedPtr(graphics->CreatePiplineState());
	pipeline->SetShader(LLGI::ShaderStageType::Compute, shaderCS.get());
	if (!pipeline->Compile())
	{
		abort();
	}

	const int dataSize = 64;
	std::vector<StructuredInputData> inputData(dataSize);
	for (int i = 0; i < dataSize; i++)
	{
		inputData[i].position[0] = i * 1.25f + 0.5f;
		inputData[i].position[1] = i * -0.75f + 2.0f;
		inputData[i].position[2] = i * 0.5f - 3.0f;
		inputData[i].reserved = 0x11110000u + i;
		inputData[i].normal = 0x00200300u + i * 17u;
		inputData[i].tangent = 0x00100400u + i * 19u;
		inputData[i].uv = 0x01020000u + i * 23u;
		inputData[i].color = 0x80402010u + i * 29u;
	}

	auto uploadBuffer = LLGI::CreateSharedPtr(
		graphics->CreateBuffer(LLGI::BufferUsageType::MapWrite | LLGI::BufferUsageType::CopySrc, sizeof(StructuredInputData) * dataSize));
	auto storageBuffer = LLGI::CreateSharedPtr(
		graphics->CreateBuffer(LLGI::BufferUsageType::ComputeRead | LLGI::BufferUsageType::CopyDst, sizeof(StructuredInputData) * dataSize));
	auto outputComputeBuffer = LLGI::CreateSharedPtr(
		graphics->CreateBuffer(LLGI::BufferUsageType::ComputeWrite | LLGI::BufferUsageType::CopySrc, sizeof(StructuredOutputData) * dataSize));
	auto readbackBuffer = LLGI::CreateSharedPtr(
		graphics->CreateBuffer(LLGI::BufferUsageType::MapRead | LLGI::BufferUsageType::CopyDst, sizeof(StructuredOutputData) * dataSize));

	{
		auto data = static_cast<StructuredInputData*>(uploadBuffer->Lock());
		for (int i = 0; i < dataSize; i++)
		{
			data[i] = inputData[i];
		}
		uploadBuffer->Unlock();
	}

	if (!platform->NewFrame())
	{
		return;
	}

	sfMemoryPool->NewFrame();
	const int32_t bufferStride = deviceType == LLGI::DeviceType::DirectX12 ? sizeof(uint32_t) : sizeof(StructuredInputData);
	const int32_t outputStride = deviceType == LLGI::DeviceType::DirectX12 ? sizeof(uint32_t) : sizeof(StructuredOutputData);

	auto commandList = commandListPool->Get();
	commandList->Begin();
	commandList->CopyBuffer(uploadBuffer.get(), storageBuffer.get());
	commandList->BeginComputePass();
	commandList->SetPipelineState(pipeline.get());
	commandList->SetComputeBuffer(outputComputeBuffer.get(), outputStride, 0, false);
	commandList->SetComputeBuffer(storageBuffer.get(), bufferStride, 1, true);
	commandList->Dispatch(dataSize, 1, 1, 1, 1, 1);
	commandList->EndComputePass();
	commandList->CopyBuffer(outputComputeBuffer.get(), readbackBuffer.get());
	commandList->End();

	graphics->Execute(commandList);
	graphics->WaitFinish();

	auto result = static_cast<const StructuredOutputData*>(readbackBuffer->Lock());
	if (result == nullptr)
	{
		abort();
	}

	for (int i = 0; i < dataSize; i++)
	{
		const auto& expected = inputData[i];
		const auto& actual = result[i];
		if (actual.position[0] != expected.position[0] + 1.0f ||
			actual.position[1] != expected.position[1] + 2.0f ||
			actual.position[2] != expected.position[2] + 3.0f ||
			actual.normal != expected.normal ||
			actual.tangent != expected.tangent ||
			actual.color != (expected.color ^ expected.uv ^ expected.reserved))
		{
			std::cout << "Failed : StructuredSlot1 Mismatch " << i << std::endl;
			abort();
		}
	}

	readbackBuffer->Unlock();
	platform->Present();
}

void test_compute_shader_storage_buffer_slot1_pass_transition(LLGI::DeviceType deviceType)
{
	LLGI::PlatformParameter pp;
	pp.Device = deviceType;
	pp.WaitVSync = true;
	auto window = std::unique_ptr<LLGI::Window>(LLGI::CreateWindow("ComputeShaderStorageBufferSlot1Transition", LLGI::Vec2I(1280, 720)));
	auto platform = LLGI::CreateSharedPtr(LLGI::CreatePlatform(pp, window.get()));
	auto graphics = LLGI::CreateSharedPtr(platform->CreateGraphics());

	auto sfMemoryPool = LLGI::CreateSharedPtr(graphics->CreateSingleFrameMemoryPool(1024 * 1024, 128));
	auto commandListPool = std::make_shared<LLGI::CommandListPool>(graphics.get(), sfMemoryPool.get(), 3);

	std::shared_ptr<LLGI::Shader> writeShader = nullptr;
	std::shared_ptr<LLGI::Shader> readShader = nullptr;
	TestHelper::CreateComputeShader(graphics.get(), deviceType, "storage_buffer_write_slot1.comp", writeShader);
	TestHelper::CreateComputeShader(graphics.get(), deviceType, "storage_buffer_read_slot1.comp", readShader);

	auto writePipeline = LLGI::CreateSharedPtr(graphics->CreatePiplineState());
	writePipeline->SetShader(LLGI::ShaderStageType::Compute, writeShader.get());
	if (!writePipeline->Compile())
	{
		abort();
	}

	auto readPipeline = LLGI::CreateSharedPtr(graphics->CreatePiplineState());
	readPipeline->SetShader(LLGI::ShaderStageType::Compute, readShader.get());
	if (!readPipeline->Compile())
	{
		abort();
	}

	const int dataSize = 64;
	auto storageBuffer = LLGI::CreateSharedPtr(
		graphics->CreateBuffer(LLGI::BufferUsageType::ComputeWrite | LLGI::BufferUsageType::CopySrc, sizeof(SlotTransitionData) * dataSize));
	auto outputComputeBuffer = LLGI::CreateSharedPtr(
		graphics->CreateBuffer(LLGI::BufferUsageType::ComputeWrite | LLGI::BufferUsageType::CopySrc, sizeof(SlotTransitionOutputData) * dataSize));
	auto readbackBuffer = LLGI::CreateSharedPtr(
		graphics->CreateBuffer(LLGI::BufferUsageType::MapRead | LLGI::BufferUsageType::CopyDst, sizeof(SlotTransitionOutputData) * dataSize));

	if (!platform->NewFrame())
	{
		return;
	}

	sfMemoryPool->NewFrame();
	const int32_t bufferStride = deviceType == LLGI::DeviceType::DirectX12 ? sizeof(uint32_t) : sizeof(SlotTransitionData);
	const int32_t outputStride = deviceType == LLGI::DeviceType::DirectX12 ? sizeof(uint32_t) : sizeof(SlotTransitionOutputData);

	auto commandList = commandListPool->Get();
	commandList->Begin();
	commandList->BeginComputePass();
	commandList->SetPipelineState(writePipeline.get());
	commandList->SetComputeBuffer(storageBuffer.get(), bufferStride, 1, false);
	commandList->Dispatch(dataSize, 1, 1, 1, 1, 1);
	commandList->EndComputePass();
	commandList->BeginComputePass();
	commandList->SetPipelineState(readPipeline.get());
	commandList->SetComputeBuffer(outputComputeBuffer.get(), outputStride, 0, false);
	commandList->SetComputeBuffer(storageBuffer.get(), bufferStride, 1, true);
	commandList->Dispatch(dataSize, 1, 1, 1, 1, 1);
	commandList->EndComputePass();
	commandList->CopyBuffer(outputComputeBuffer.get(), readbackBuffer.get());
	commandList->End();

	graphics->Execute(commandList);
	graphics->WaitFinish();

	auto result = static_cast<const SlotTransitionOutputData*>(readbackBuffer->Lock());
	if (result == nullptr)
	{
		abort();
	}

	for (int i = 0; i < dataSize; i++)
	{
		const float x = i * 2.0f + 0.25f;
		const float y = i * -3.0f + 0.5f;
		const float z = i * 4.0f - 0.75f;
		const uint32_t direction = 0x12340000u + i * 31u;
		const uint32_t checksum = direction ^ static_cast<uint32_t>(i * 0x01010101u);
		const auto& actual = result[i];
		if (actual.position[0] != x + 10.0f ||
			actual.position[1] != y + 20.0f ||
			actual.position[2] != z + 30.0f ||
			actual.direction != direction ||
			actual.checksum != checksum)
		{
			std::cout << "Failed : Slot1PassTransition Mismatch " << i << std::endl;
			abort();
		}
	}

	readbackBuffer->Unlock();
	platform->Present();
}

void test_compute_shader_storage_buffer_multi_slot_read_write(LLGI::DeviceType deviceType)
{
	LLGI::PlatformParameter pp;
	pp.Device = deviceType;
	pp.WaitVSync = true;
	auto window = std::unique_ptr<LLGI::Window>(LLGI::CreateWindow("ComputeShaderStorageBufferMultiSlot", LLGI::Vec2I(1280, 720)));
	auto platform = LLGI::CreateSharedPtr(LLGI::CreatePlatform(pp, window.get()));
	auto graphics = LLGI::CreateSharedPtr(platform->CreateGraphics());

	auto sfMemoryPool = LLGI::CreateSharedPtr(graphics->CreateSingleFrameMemoryPool(1024 * 1024, 128));
	auto commandListPool = std::make_shared<LLGI::CommandListPool>(graphics.get(), sfMemoryPool.get(), 3);

	std::shared_ptr<LLGI::Shader> shaderCS = nullptr;
	TestHelper::CreateComputeShader(graphics.get(), deviceType, "storage_buffer_multi_slot_read_write.comp", shaderCS);

	auto pipeline = LLGI::CreateSharedPtr(graphics->CreatePiplineState());
	pipeline->SetShader(LLGI::ShaderStageType::Compute, shaderCS.get());
	if (!pipeline->Compile())
	{
		abort();
	}

	const int dataSize = 64;
	std::vector<MultiSlotInputA> inputA(dataSize);
	std::vector<MultiSlotInputB> inputB(dataSize);
	for (int i = 0; i < dataSize; i++)
	{
		inputA[i].values[0] = i + 0.25f;
		inputA[i].values[1] = i * -2.0f + 0.5f;
		inputA[i].values[2] = i * 3.0f - 0.75f;
		inputA[i].tag = 0x33000000u + i * 7u;
		inputB[i].x = 0x01000000u + i * 11u;
		inputB[i].y = 0x00200000u + i * 13u;
		inputB[i].z = 0x00030000u + i * 17u;
		inputB[i].w = 0x00004000u + i * 19u;
	}

	auto uploadA = LLGI::CreateSharedPtr(
		graphics->CreateBuffer(LLGI::BufferUsageType::MapWrite | LLGI::BufferUsageType::CopySrc, sizeof(MultiSlotInputA) * dataSize));
	auto uploadB = LLGI::CreateSharedPtr(
		graphics->CreateBuffer(LLGI::BufferUsageType::MapWrite | LLGI::BufferUsageType::CopySrc, sizeof(MultiSlotInputB) * dataSize));
	auto inputBufferA = LLGI::CreateSharedPtr(
		graphics->CreateBuffer(LLGI::BufferUsageType::ComputeRead | LLGI::BufferUsageType::CopyDst, sizeof(MultiSlotInputA) * dataSize));
	auto inputBufferB = LLGI::CreateSharedPtr(
		graphics->CreateBuffer(LLGI::BufferUsageType::ComputeRead | LLGI::BufferUsageType::CopyDst, sizeof(MultiSlotInputB) * dataSize));
	auto outputBufferA = LLGI::CreateSharedPtr(
		graphics->CreateBuffer(LLGI::BufferUsageType::ComputeWrite | LLGI::BufferUsageType::CopySrc, sizeof(MultiSlotOutputA) * dataSize));
	auto outputBufferB = LLGI::CreateSharedPtr(
		graphics->CreateBuffer(LLGI::BufferUsageType::ComputeWrite | LLGI::BufferUsageType::CopySrc, sizeof(MultiSlotOutputB) * dataSize));
	auto readbackA = LLGI::CreateSharedPtr(
		graphics->CreateBuffer(LLGI::BufferUsageType::MapRead | LLGI::BufferUsageType::CopyDst, sizeof(MultiSlotOutputA) * dataSize));
	auto readbackB = LLGI::CreateSharedPtr(
		graphics->CreateBuffer(LLGI::BufferUsageType::MapRead | LLGI::BufferUsageType::CopyDst, sizeof(MultiSlotOutputB) * dataSize));

	{
		auto dataA = static_cast<MultiSlotInputA*>(uploadA->Lock());
		auto dataB = static_cast<MultiSlotInputB*>(uploadB->Lock());
		for (int i = 0; i < dataSize; i++)
		{
			dataA[i] = inputA[i];
			dataB[i] = inputB[i];
		}
		uploadA->Unlock();
		uploadB->Unlock();
	}

	if (!platform->NewFrame())
	{
		return;
	}

	sfMemoryPool->NewFrame();
	const int32_t inputStrideA = deviceType == LLGI::DeviceType::DirectX12 ? sizeof(uint32_t) : sizeof(MultiSlotInputA);
	const int32_t inputStrideB = deviceType == LLGI::DeviceType::DirectX12 ? sizeof(uint32_t) : sizeof(MultiSlotInputB);
	const int32_t outputStrideA = deviceType == LLGI::DeviceType::DirectX12 ? sizeof(uint32_t) : sizeof(MultiSlotOutputA);
	const int32_t outputStrideB = deviceType == LLGI::DeviceType::DirectX12 ? sizeof(uint32_t) : sizeof(MultiSlotOutputB);

	auto commandList = commandListPool->Get();
	commandList->Begin();
	commandList->CopyBuffer(uploadA.get(), inputBufferA.get());
	commandList->CopyBuffer(uploadB.get(), inputBufferB.get());
	commandList->BeginComputePass();
	commandList->SetPipelineState(pipeline.get());
	commandList->SetComputeBuffer(outputBufferA.get(), outputStrideA, 0, false);
	commandList->SetComputeBuffer(inputBufferA.get(), inputStrideA, 1, true);
	commandList->SetComputeBuffer(inputBufferB.get(), inputStrideB, 2, true);
	commandList->SetComputeBuffer(outputBufferB.get(), outputStrideB, 3, false);
	commandList->Dispatch(dataSize, 1, 1, 1, 1, 1);
	commandList->EndComputePass();
	commandList->CopyBuffer(outputBufferA.get(), readbackA.get());
	commandList->CopyBuffer(outputBufferB.get(), readbackB.get());
	commandList->End();

	graphics->Execute(commandList);
	graphics->WaitFinish();

	auto resultA = static_cast<const MultiSlotOutputA*>(readbackA->Lock());
	auto resultB = static_cast<const MultiSlotOutputB*>(readbackB->Lock());
	if (resultA == nullptr || resultB == nullptr)
	{
		abort();
	}

	for (int i = 0; i < dataSize; i++)
	{
		const auto mix = inputA[i].tag ^ inputB[i].x ^ inputB[i].y ^ inputB[i].z ^ inputB[i].w;
		const auto expectedValue = inputA[i].values[0] + inputA[i].values[1] + inputA[i].values[2] + static_cast<float>(inputB[i].x & 0xffu);
		if (resultA[i].value != expectedValue ||
			resultA[i].tag != inputA[i].tag ||
			resultA[i].mix != mix ||
			resultB[i].values[0] != inputA[i].values[0] * 2.0f ||
			resultB[i].values[1] != inputA[i].values[1] * 3.0f ||
			resultB[i].values[2] != inputA[i].values[2] * 4.0f ||
			resultB[i].checksum != mix + static_cast<uint32_t>(i))
		{
			std::cout << "Failed : MultiSlotReadWrite Mismatch " << i << std::endl;
			abort();
		}
	}

	readbackA->Unlock();
	readbackB->Unlock();
	platform->Present();
}

void test_compute_shader_storage_buffer_structured_matrix_record(LLGI::DeviceType deviceType)
{
	LLGI::PlatformParameter pp;
	pp.Device = deviceType;
	pp.WaitVSync = true;
	auto window = std::unique_ptr<LLGI::Window>(LLGI::CreateWindow("ComputeShaderStorageBufferStructuredMatrixRecord", LLGI::Vec2I(1280, 720)));
	auto platform = LLGI::CreateSharedPtr(LLGI::CreatePlatform(pp, window.get()));
	auto graphics = LLGI::CreateSharedPtr(platform->CreateGraphics());

	auto sfMemoryPool = LLGI::CreateSharedPtr(graphics->CreateSingleFrameMemoryPool(1024 * 1024, 128));
	auto commandListPool = std::make_shared<LLGI::CommandListPool>(graphics.get(), sfMemoryPool.get(), 3);

	std::shared_ptr<LLGI::Shader> writeShader = nullptr;
	std::shared_ptr<LLGI::Shader> readShader = nullptr;
	TestHelper::CreateComputeShader(graphics.get(), deviceType, "storage_buffer_structured_matrix_record_write.comp", writeShader);
	TestHelper::CreateComputeShader(graphics.get(), deviceType, "storage_buffer_structured_matrix_record.comp", readShader);

	auto writePipeline = LLGI::CreateSharedPtr(graphics->CreatePiplineState());
	writePipeline->SetShader(LLGI::ShaderStageType::Compute, writeShader.get());
	if (!writePipeline->Compile())
	{
		abort();
	}

	auto readPipeline = LLGI::CreateSharedPtr(graphics->CreatePiplineState());
	readPipeline->SetShader(LLGI::ShaderStageType::Compute, readShader.get());
	if (!readPipeline->Compile())
	{
		abort();
	}

	const int dataSize = 32;
	std::vector<StructuredMatrixRecordInput> inputData(dataSize);
	for (int i = 0; i < dataSize; i++)
	{
		auto& input = inputData[i];
		input.flags = 0x12000000u + i * 17u;
		input.age = 0.5f + i * 0.125f;
		input.packed[0] = 0x00100000u + i * 19u;
		input.packed[1] = 0x00020000u + i * 23u;
		for (int row = 0; row < 4; row++)
		{
			for (int column = 0; column < 3; column++)
			{
				input.transform[row][column] = static_cast<float>((i + 1) * (row + 2) + column) * 0.125f;
			}
		}
		input.transform[3][0] = -0.75f + i * 0.03125f;
		input.transform[3][1] = 0.25f - i * 0.015625f;
		input.transform[3][2] = 1.5f + i * 0.0625f;
		input.direction[0] = 0.25f + i * 0.015625f;
		input.direction[1] = -0.5f + i * 0.03125f;
		input.direction[2] = 0.75f - i * 0.0078125f;
		input.color = 0x80402010u + i * 29u;
	}

	auto recordBuffer = LLGI::CreateSharedPtr(
		graphics->CreateBuffer(LLGI::BufferUsageType::ComputeWrite, sizeof(StructuredMatrixRecordInput) * dataSize));
	auto outputComputeBuffer = LLGI::CreateSharedPtr(
		graphics->CreateBuffer(LLGI::BufferUsageType::ComputeWrite | LLGI::BufferUsageType::CopySrc, sizeof(StructuredMatrixRecordOutput) * dataSize));
	auto readbackBuffer = LLGI::CreateSharedPtr(
		graphics->CreateBuffer(LLGI::BufferUsageType::MapRead | LLGI::BufferUsageType::CopyDst, sizeof(StructuredMatrixRecordOutput) * dataSize));

	if (!platform->NewFrame())
	{
		return;
	}

	sfMemoryPool->NewFrame();

	auto commandList = commandListPool->Get();
	commandList->Begin();
	commandList->BeginComputePass();
	commandList->SetPipelineState(writePipeline.get());
	commandList->SetComputeBuffer(recordBuffer.get(), sizeof(StructuredMatrixRecordInput), 1, false);
	commandList->Dispatch(dataSize, 1, 1, 1, 1, 1);
	commandList->EndComputePass();
	commandList->BeginComputePass();
	commandList->SetPipelineState(readPipeline.get());
	commandList->SetComputeBuffer(outputComputeBuffer.get(), sizeof(StructuredMatrixRecordOutput), 0, false);
	commandList->SetComputeBuffer(recordBuffer.get(), sizeof(StructuredMatrixRecordInput), 1, true);
	commandList->Dispatch(dataSize, 1, 1, 1, 1, 1);
	commandList->EndComputePass();
	commandList->CopyBuffer(outputComputeBuffer.get(), readbackBuffer.get());
	commandList->End();

	graphics->Execute(commandList);
	graphics->WaitFinish();

	auto result = static_cast<const StructuredMatrixRecordOutput*>(readbackBuffer->Lock());
	if (result == nullptr)
	{
		abort();
	}

	for (int i = 0; i < dataSize; i++)
	{
		const auto& input = inputData[i];
		const auto& actual = result[i];
		const uint32_t expectedFlags = input.flags ^ FloatAsUint(input.age);
		const uint32_t expectedChecksum = input.flags ^ input.packed[0] ^ input.packed[1] ^ input.color;
		if (actual.flags != expectedFlags || actual.color != input.color || actual.checksum != expectedChecksum)
		{
			std::cout << "Failed : StructuredMatrixRecord Scalar Mismatch " << i << std::endl;
			abort();
		}

		for (int axis = 0; axis < 3; axis++)
		{
			const float expectedDirection = input.direction[axis] + input.transform[2][axis] * 0.25f;
			const float expectedAxis0 = input.transform[0][axis] * input.age + input.direction[axis];
			if (!NearlyEqual(actual.translation[axis], input.transform[3][axis]) ||
				!NearlyEqual(actual.direction[axis], expectedDirection) ||
				!NearlyEqual(actual.axis0[axis], expectedAxis0))
			{
				std::cout << "Failed : StructuredMatrixRecord Float Mismatch " << i << std::endl;
				abort();
			}
		}
	}

	readbackBuffer->Unlock();
	platform->Present();
}

void test_compute_shader_storage_buffer_matrix_indexing(LLGI::DeviceType deviceType)
{
	LLGI::PlatformParameter pp;
	pp.Device = deviceType;
	pp.WaitVSync = true;
	auto window = std::unique_ptr<LLGI::Window>(LLGI::CreateWindow("ComputeShaderStorageBufferMatrixIndexing", LLGI::Vec2I(1280, 720)));
	auto platform = LLGI::CreateSharedPtr(LLGI::CreatePlatform(pp, window.get()));
	auto graphics = LLGI::CreateSharedPtr(platform->CreateGraphics());

	auto sfMemoryPool = LLGI::CreateSharedPtr(graphics->CreateSingleFrameMemoryPool(1024 * 1024, 128));
	auto commandListPool = std::make_shared<LLGI::CommandListPool>(graphics.get(), sfMemoryPool.get(), 3);

	std::shared_ptr<LLGI::Shader> writeShader = nullptr;
	std::shared_ptr<LLGI::Shader> readShader = nullptr;
	TestHelper::CreateComputeShader(graphics.get(), deviceType, "storage_buffer_matrix_indexing_write.comp", writeShader);
	TestHelper::CreateComputeShader(graphics.get(), deviceType, "storage_buffer_matrix_indexing.comp", readShader);

	auto writePipeline = LLGI::CreateSharedPtr(graphics->CreatePiplineState());
	writePipeline->SetShader(LLGI::ShaderStageType::Compute, writeShader.get());
	if (!writePipeline->Compile())
	{
		abort();
	}

	auto readPipeline = LLGI::CreateSharedPtr(graphics->CreatePiplineState());
	readPipeline->SetShader(LLGI::ShaderStageType::Compute, readShader.get());
	if (!readPipeline->Compile())
	{
		abort();
	}

	const int dataSize = 24;
	std::vector<MatrixIndexingInput> inputData(dataSize);
	for (int i = 0; i < dataSize; i++)
	{
		auto& input = inputData[i];
		for (int row = 0; row < 4; row++)
		{
			for (int column = 0; column < 4; column++)
			{
				input.clip[row][column] = (row == column ? 1.0f : 0.0f) + static_cast<float>((i + 1) * (row + 1) - column) * 0.03125f;
			}
		}
		for (int row = 0; row < 4; row++)
		{
			for (int column = 0; column < 3; column++)
			{
				input.transform[row][column] = static_cast<float>((row + 1) * (column + 2)) * 0.125f + i * 0.0078125f;
			}
		}
		input.transform[3][0] = -0.5f + i * 0.046875f;
		input.transform[3][1] = 0.375f - i * 0.0234375f;
		input.transform[3][2] = 0.25f + i * 0.015625f;
		input.local[0] = 0.25f + i * 0.015625f;
		input.local[1] = -0.5f + i * 0.0078125f;
		input.local[2] = 0.75f - i * 0.00390625f;
		input.local[3] = 1.0f;
		input.row = static_cast<uint32_t>(i % 4);
		input.flags = 0x45000000u + i * 31u;
		input.padding[0] = 0;
		input.padding[1] = 0;
	}

	auto recordBuffer = LLGI::CreateSharedPtr(
		graphics->CreateBuffer(LLGI::BufferUsageType::ComputeWrite, sizeof(MatrixIndexingInput) * dataSize));
	auto outputComputeBuffer = LLGI::CreateSharedPtr(
		graphics->CreateBuffer(LLGI::BufferUsageType::ComputeWrite | LLGI::BufferUsageType::CopySrc, sizeof(MatrixIndexingOutput) * dataSize));
	auto readbackBuffer = LLGI::CreateSharedPtr(
		graphics->CreateBuffer(LLGI::BufferUsageType::MapRead | LLGI::BufferUsageType::CopyDst, sizeof(MatrixIndexingOutput) * dataSize));

	if (!platform->NewFrame())
	{
		return;
	}

	sfMemoryPool->NewFrame();

	auto commandList = commandListPool->Get();
	commandList->Begin();
	commandList->BeginComputePass();
	commandList->SetPipelineState(writePipeline.get());
	commandList->SetComputeBuffer(recordBuffer.get(), sizeof(MatrixIndexingInput), 1, false);
	commandList->Dispatch(dataSize, 1, 1, 1, 1, 1);
	commandList->EndComputePass();
	commandList->BeginComputePass();
	commandList->SetPipelineState(readPipeline.get());
	commandList->SetComputeBuffer(outputComputeBuffer.get(), sizeof(MatrixIndexingOutput), 0, false);
	commandList->SetComputeBuffer(recordBuffer.get(), sizeof(MatrixIndexingInput), 1, true);
	commandList->Dispatch(dataSize, 1, 1, 1, 1, 1);
	commandList->EndComputePass();
	commandList->CopyBuffer(outputComputeBuffer.get(), readbackBuffer.get());
	commandList->End();

	graphics->Execute(commandList);
	graphics->WaitFinish();

	auto result = static_cast<const MatrixIndexingOutput*>(readbackBuffer->Lock());
	if (result == nullptr)
	{
		abort();
	}

	for (int i = 0; i < dataSize; i++)
	{
		const auto& input = inputData[i];
		const auto& actual = result[i];
		float world[4] = {};
		TransformPoint(world, input.transform, input.local);
		world[3] = 1.0f;

		float expectedClip[4] = {};
		TransformClip(expectedClip, input.clip, world);

		const uint32_t row = input.row & 3u;
		const uint32_t nextRow = (row + 1u) & 3u;
		const uint32_t expectedChecksum = input.flags ^ input.row ^ FloatAsUint(input.local[3]);
		if (actual.checksum != expectedChecksum)
		{
			std::cout << "Failed : MatrixIndexing Checksum Mismatch " << i << std::endl;
			abort();
		}
		for (int axis = 0; axis < 4; axis++)
		{
			if (!NearlyEqual(actual.clip[axis], expectedClip[axis]))
			{
				std::cout << "Failed : MatrixIndexing Clip Mismatch " << i << " axis=" << axis << " actual=" << actual.clip[axis]
						  << " expected=" << expectedClip[axis] << std::endl;
				abort();
			}
		}
		for (int axis = 0; axis < 3; axis++)
		{
			const float expectedIndexed = input.transform[row][axis] + input.transform[nextRow][axis] * 0.25f;
			if (!NearlyEqual(actual.indexed[axis], expectedIndexed))
			{
				std::cout << "Failed : MatrixIndexing Row Mismatch " << i << std::endl;
				abort();
			}
		}
	}

	readbackBuffer->Unlock();
	platform->Present();
}

void test_compute_shader_texture(LLGI::DeviceType deviceType)
{
	LLGI::PlatformParameter pp;
	pp.Device = deviceType;
	pp.WaitVSync = true;
	auto window = std::unique_ptr<LLGI::Window>(LLGI::CreateWindow("ComputeShaderTexture", LLGI::Vec2I(1280, 720)));
	auto platform = LLGI::CreateSharedPtr(LLGI::CreatePlatform(pp, window.get()));
	auto graphics = LLGI::CreateSharedPtr(platform->CreateGraphics());

	auto sfMemoryPool = LLGI::CreateSharedPtr(graphics->CreateSingleFrameMemoryPool(1024 * 1024, 128));

	auto commandListPool = std::make_shared<LLGI::CommandListPool>(graphics.get(), sfMemoryPool.get(), 3);

	std::shared_ptr<LLGI::Shader> shader_cs = nullptr;

	TestHelper::CreateComputeShader(graphics.get(), deviceType, "readwrite_texture.comp", shader_cs);

	auto pip = LLGI::CreateSharedPtr(graphics->CreatePiplineState());
	pip->SetShader(LLGI::ShaderStageType::Compute, shader_cs.get());
	if (!pip->Compile())
	{
		std::cout << "Failed : Compile" << std::endl;
		abort();
	}

	LLGI::TextureParameter texParamRead1;
	texParamRead1.Size = {1, 1, 1};
	texParamRead1.Format = LLGI::TextureFormatType::R32G32B32A32_FLOAT;
	texParamRead1.Usage = LLGI::TextureUsageType::Storage;
	auto texRead1 = LLGI::CreateSharedPtr(graphics->CreateTexture(texParamRead1));

	if (auto data = texRead1->Lock())
	{
		auto p = static_cast<float*>(data);
		p[0] = 0.25f;
		p[1] = 0.25f;
		p[2] = 0;
		p[3] = 0.25f;
		texRead1->Unlock();
	}

	LLGI::TextureParameter texParamRead2;
	texParamRead2.Size = {1, 1, 1};
	texParamRead2.Format = LLGI::TextureFormatType::R32G32B32A32_FLOAT;
	auto texRead2 = LLGI::CreateSharedPtr(graphics->CreateTexture(texParamRead2));

	if (auto data = texRead2->Lock())
	{
		auto p = static_cast<float*>(data);
		p[0] = 0.25f;
		p[1] = 0;
		p[2] = 0.25f;
		p[3] = 0.25f;
		texRead2->Unlock();
	}

	LLGI::TextureParameter texParamWrite;
	texParamWrite.Size = {1, 1, 1};
	texParamWrite.Usage = LLGI::TextureUsageType::Storage;
	texParamWrite.Format = LLGI::TextureFormatType::R32G32B32A32_FLOAT;
	auto texWrite = LLGI::CreateSharedPtr(graphics->CreateTexture(texParamWrite));

	if (!platform->NewFrame())
		return;

	sfMemoryPool->NewFrame();

	auto commandList = commandListPool->Get();
	commandList->Begin();
	commandList->BeginComputePass();
	commandList->SetPipelineState(pip.get());
	commandList->SetTexture(texWrite.get(), LLGI::TextureWrapMode::Clamp, LLGI::TextureMinMagFilter::Nearest, 0);
	commandList->SetTexture(texRead1.get(), LLGI::TextureWrapMode::Clamp, LLGI::TextureMinMagFilter::Nearest, 1);
	commandList->SetTexture(texRead2.get(), LLGI::TextureWrapMode::Clamp, LLGI::TextureMinMagFilter::Nearest, 2);
	commandList->Dispatch(1, 1, 1, 1, 1, 1);
	commandList->EndComputePass();
	commandList->End();

	graphics->Execute(commandList);
	graphics->WaitFinish();

	std::vector<uint8_t> result;
	if (texWrite->GetData(result))
	{
		const auto p = reinterpret_cast<const float*>(result.data());
		if (!(p[0] == 0.5f && p[1] == 0.25f && p[2] == 0.25f && p[3] == 0.5f))
		{
			std::cout << "Failed : Mismatch" << p[0] << "," << p[1] << "," << p[2] << "," << p[3] << std::endl;
			abort();
		}
	}

	platform->Present();
}

TestRegister ComputeShader_Basic("ComputeShader.ComputeBuffer", [](LLGI::DeviceType device) -> void { test_compute_shader_compute_buffer(device, false); });

TestRegister ComputeShader_Basic_ReadOnly("ComputeShader.ComputeBuffer_ReadOnly",
										  [](LLGI::DeviceType device) -> void { test_compute_shader_compute_buffer(device, true); });

TestRegister ComputeShader_StorageBufferStructuredSlot1("ComputeShader.StorageBuffer.StructuredSlot1",
														[](LLGI::DeviceType device) -> void {
															test_compute_shader_storage_buffer_structured_slot1(device);
														});

TestRegister ComputeShader_StorageBufferSlot1PassTransition("ComputeShader.StorageBuffer.Slot1PassTransition",
															[](LLGI::DeviceType device) -> void {
																test_compute_shader_storage_buffer_slot1_pass_transition(device);
															});

TestRegister ComputeShader_StorageBufferMultiSlotReadWrite("ComputeShader.StorageBuffer.MultiSlotReadWrite",
														   [](LLGI::DeviceType device) -> void {
															   test_compute_shader_storage_buffer_multi_slot_read_write(device);
														   });

TestRegister ComputeShader_StorageBufferStructuredMatrixRecord("ComputeShader.StorageBuffer.StructuredMatrixRecord",
															   [](LLGI::DeviceType device) -> void {
																   test_compute_shader_storage_buffer_structured_matrix_record(device);
															   });

TestRegister ComputeShader_StorageBufferMatrixIndexing("ComputeShader.StorageBuffer.MatrixIndexing",
													   [](LLGI::DeviceType device) -> void {
														   test_compute_shader_storage_buffer_matrix_indexing(device);
													   });

TestRegister ComputeShader_Basic_Texture("ComputeShader.Texture",
										 [](LLGI::DeviceType device) -> void { test_compute_shader_texture(device); });

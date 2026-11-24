#include "LLGI.ShaderWebGPU.h"
#include "LLGI.CompilerWebGPU.h"
#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

#ifdef LLGI_WEBGPU_SPIRV_READER
#include <spirv/unified1/spirv.hpp11>
#endif

namespace LLGI
{
namespace
{
#ifdef LLGI_WEBGPU_SPIRV_READER
// SPIR-V uses Dawn's automatic layout. Keep just the image and sampler declarations
// needed to choose the view and sampler for an LLGI texture slot.
bool ReflectSPIRVTextures(const uint32_t* words, size_t wordCount, std::vector<ShaderBindingWebGPU>& bindings)
{
	if (wordCount < 5)
		return false;
	std::unordered_map<uint32_t, ShaderResourceType> types;
	std::unordered_set<uint32_t> floatTypes, whiteFallbackTypes;
	std::unordered_map<uint32_t, ShaderBindingWebGPU> variables;
	for (size_t offset = 5; offset < wordCount;)
	{
		const auto* inst = words + offset;
		const size_t length = inst[0] >> spv::WordCountShift;
		if (length == 0 || length > wordCount - offset)
			return false;
		switch (static_cast<spv::Op>(inst[0] & spv::OpCodeMask))
		{
		case spv::Op::OpDecorate:
			if (length == 4 && inst[2] == static_cast<uint32_t>(spv::Decoration::DescriptorSet))
				variables[inst[1]].Group = inst[3];
			else if (length == 4 && inst[2] == static_cast<uint32_t>(spv::Decoration::Binding))
				variables[inst[1]].Binding = inst[3];
			break;
		case spv::Op::OpTypeFloat:
			if (length == 3 && inst[2] == 32)
				floatTypes.insert(inst[1]);
			break;
		case spv::Op::OpTypeImage:
			if (length >= 9)
			{
				types[inst[1]] = inst[7] == 2 ? ShaderResourceType::StorageTexture : ShaderResourceType::Texture;
				if (inst[3] == static_cast<uint32_t>(spv::Dim::Dim2D) && inst[4] == 0 && inst[5] == 0 && inst[6] == 0 && inst[7] == 1 &&
					floatTypes.contains(inst[2]))
					whiteFallbackTypes.insert(inst[1]);
			}
			break;
		case spv::Op::OpTypeSampler:
			if (length == 2)
				types[inst[1]] = ShaderResourceType::Sampler;
			break;
		case spv::Op::OpTypePointer:
			if (length == 4 && types.contains(inst[3]))
			{
				types[inst[1]] = types.at(inst[3]);
				if (whiteFallbackTypes.contains(inst[3]))
					whiteFallbackTypes.insert(inst[1]);
			}
			break;
		case spv::Op::OpVariable:
			if (length >= 4 && types.contains(inst[1]))
			{
				variables[inst[2]].Resource.ResourceType = types.at(inst[1]);
				variables[inst[2]].SupportsWhiteFallback = whiteFallbackTypes.contains(inst[1]);
			}
			break;
		default:
			break;
		}
		offset += length;
	}
	for (const auto& [id, variable] : variables)
	{
		if (variable.Resource.ResourceType != ShaderResourceType::Unknown)
			bindings.push_back(variable);
	}
	return true;
}
#endif

// WebGPU infers the pipeline layout. LLGI only needs the slots and resource kinds used by main.
using Tokens = std::vector<std::string_view>;
using TokenSpan = std::span<const std::string_view>;
using Definitions = std::unordered_map<std::string_view, TokenSpan>;

Tokens TokenizeWGSL(std::string_view source)
{
	Tokens tokens;
	size_t blockDepth = 0;
	for (size_t i = 0; i < source.size();)
	{
		const auto pair = source.substr(i, 2);
		if (pair == "/*")
		{
			++blockDepth;
			i += 2;
		}
		else if (blockDepth && pair == "*/")
		{
			--blockDepth;
			i += 2;
		}
		else if (blockDepth || std::isspace(static_cast<unsigned char>(source[i])))
			++i;
		else if (pair == "//")
		{
			i = source.find_first_of("\r\n", i + 2);
			if (i == std::string_view::npos)
				break;
		}
		else
		{
			const auto start = i++;
			auto isWord = [](unsigned char c) { return std::isalnum(c) || c == '_' || c >= 128; };
			if (isWord(source[start]))
				while (i < source.size() && isWord(source[i]))
					++i;
			tokens.emplace_back(source.substr(start, i - start));
		}
	}
	return tokens;
}

TokenSpan ResolveType(TokenSpan type, const Definitions& aliases)
{
	for (size_t depth = 0; !type.empty() && depth <= aliases.size(); ++depth)
	{
		auto found = aliases.find(type.front());
		if (found == aliases.end())
			return type;
		type = found->second;
	}
	return {};
}

std::optional<int64_t> ParseInteger(std::string_view token)
{
	if (token.back() == 'u' || token.back() == 'i')
		token.remove_suffix(1);
	const bool hex = token.starts_with("0x") || token.starts_with("0X");
	if (hex)
		token.remove_prefix(2);
	uint32_t value = 0;
	const auto end = token.data() + token.size();
	auto result = std::from_chars(token.data(), end, value, hex ? 16 : 10);
	if (result.ec != std::errc{} || result.ptr != end)
		return {};
	return value;
}

class BindingIndexParser
{
	const Definitions& constants_;
	std::unordered_map<std::string_view, std::optional<int64_t>> values_;

	std::optional<int64_t> Operand(TokenSpan& tokens)
	{
		if (tokens.empty())
			return {};
		const auto name = tokens.front();
		tokens = tokens.subspan(1);
		if (name == "(")
		{
			auto value = Expression(tokens, 1);
			if (tokens.empty() || tokens.front() != ")")
				return {};
			tokens = tokens.subspan(1);
			return value;
		}
		if (name == "-")
		{
			auto value = Operand(tokens);
			if (value)
				return -*value;
			return {};
		}
		auto it = constants_.find(name);
		if (it == constants_.end())
			return ParseInteger(name);
		auto [cached, inserted] = values_.try_emplace(name);
		if (!inserted)
			return cached->second;

		auto initializer = it->second;
		auto value = Expression(initializer, 1);
		return values_[name] = initializer.empty() ? value : std::nullopt;
	}

	std::optional<int64_t> Expression(TokenSpan& tokens, int minPrecedence)
	{
		auto value = Operand(tokens);
		while (value && !tokens.empty())
		{
			const auto op = tokens.front();
			const int precedence = (op == "+" || op == "-") ? 1 : (op == "*" || op == "/" || op == "%") ? 2 : 0;
			if (precedence < minPrecedence)
				break;
			tokens = tokens.subspan(1);
			auto right = Expression(tokens, precedence + 1);
			if (!right || ((op == "/" || op == "%") && *right == 0))
				return {};
			if (op == "+")
				*value += *right;
			else if (op == "-")
				*value -= *right;
			else if (op == "*")
			{
				if (*right && std::abs(*value) > std::numeric_limits<uint32_t>::max() / std::abs(*right))
					return {};
				*value *= *right;
			}
			else if (op == "/")
				*value /= *right;
			else
				*value %= *right;
			if (std::abs(*value) > std::numeric_limits<uint32_t>::max())
				return {};
		}
		return value;
	}

public:
	explicit BindingIndexParser(const Definitions& constants) : constants_(constants) {}

	bool Parse(TokenSpan tokens, uint32_t& index)
	{
		const auto value = Expression(tokens, 1);
		if (!value || !tokens.empty() || *value < 0 || *value > std::numeric_limits<uint32_t>::max())
			return false;
		index = static_cast<uint32_t>(*value);
		return true;
	}
};

struct FunctionTokens
{
	size_t parameters;
	size_t body;
	size_t end;
};

struct BindingDeclaration
{
	TokenSpan group, binding, type;
	std::string_view addressSpace, access;
	bool hasBindingAttribute = false;
};

using Functions = std::unordered_map<std::string_view, FunctionTokens>;
using BindingDeclarations = std::unordered_map<std::string_view, BindingDeclaration>;

std::unordered_set<std::string_view> FindUsedBindings(const Tokens& tokens, Functions& functions, const BindingDeclarations& globals)
{
	std::vector<std::string_view> pending{"main"};
	std::unordered_set<std::string_view> used;
	while (!pending.empty())
	{
		const auto fn = functions.find(pending.back());
		pending.pop_back();
		if (fn == functions.end())
			continue;
		const auto [parameters, body, end] = fn->second;
		functions.erase(fn);
		std::vector<std::string_view> locals;
		for (size_t i = parameters + 1; i < body; ++i)
			if (tokens[i] == ":")
				locals.push_back(tokens[i - 1]);
		std::vector<size_t> scopes;
		size_t loopScope = std::string_view::npos;
		std::string_view declaredLocal;
		for (size_t i = body + 1; i < end; ++i)
		{
			const auto token = tokens[i];
			if (token == "for")
				loopScope = locals.size();
			else if (token == "{")
			{
				scopes.push_back(loopScope == std::string_view::npos ? locals.size() : loopScope);
				loopScope = std::string_view::npos;
			}
			else if (token == "}" && !scopes.empty())
			{
				locals.resize(scopes.back());
				scopes.pop_back();
			}
			else if (token == "var" || token == "let" || token == "const")
			{
				if (i + 1 < end && tokens[i + 1] == "<")
					while (i + 1 < end && tokens[++i] != ">")
					{
					}
				if (i + 1 < end)
					declaredLocal = tokens[++i];
			}
			else if (token == ";" && !declaredLocal.empty())
			{
				locals.push_back(declaredLocal);
				declaredLocal = {};
			}
			else if (tokens[i - 1] != "." && globals.count(token) && std::find(locals.begin(), locals.end(), token) == locals.end())
				used.insert(token);
			else if (i + 1 < end && tokens[i + 1] == "(" && functions.count(token))
				pending.push_back(token);
		}
	}
	return used;
}

bool ReflectWGSL(std::string_view source, std::vector<ShaderBindingWebGPU>& bindings)
{
	const auto tokens = TokenizeWGSL(source);
	Functions functions;
	BindingDeclarations globals;
	Definitions aliases, constants;
	const TokenSpan all(tokens);
	BindingDeclaration declaration{};
	for (size_t i = 0; i < tokens.size(); ++i)
	{
		if (tokens[i] == "@" && i + 4 < tokens.size() && tokens[i + 2] == "(")
		{
			if (tokens[i + 1] == "group" || tokens[i + 1] == "binding")
			{
				declaration.hasBindingAttribute = true;
				auto& index = tokens[i + 1] == "group" ? declaration.group : declaration.binding;
				const auto start = i + 3;
				size_t depth = 1;
				i = start;
				while (i < tokens.size())
				{
					if (tokens[i] == "(")
						++depth;
					if (tokens[i] == ")" && --depth == 0)
						break;
					++i;
				}
				if (i == tokens.size())
					return false;
				index = all.subspan(start, i - start);
			}
		}
		else if ((tokens[i] == "alias" || tokens[i] == "const") && i + 3 < tokens.size())
		{
			auto& definitions = tokens[i] == "alias" ? aliases : constants;
			const auto name = tokens[i + 1];
			while (i < tokens.size() && tokens[i] != "=" && tokens[i] != ";")
				++i;
			if (i == tokens.size() || tokens[i] != "=")
				return false;
			const auto start = ++i;
			while (i < tokens.size() && tokens[i] != ";")
				++i;
			definitions[name] = all.subspan(start, i - start);
		}
		else if (tokens[i] == "var" && declaration.hasBindingAttribute)
		{
			size_t name = i + 1;
			if (name + 1 < tokens.size() && tokens[name] == "<")
			{
				declaration.addressSpace = tokens[name + 1];
				if (name + 3 < tokens.size() && tokens[name + 2] == ",")
					declaration.access = tokens[name + 3];
				while (name < tokens.size() && tokens[name] != ">")
					++name;
				++name;
			}
			if (name + 2 >= tokens.size() || tokens[name + 1] != ":")
				return false;
			declaration.type = all.subspan(name + 2);
			globals[tokens[name]] = declaration;
		}
		else if (tokens[i] == ";")
			declaration = {};
		else if (tokens[i] == "fn" && i + 2 < tokens.size())
		{
			const auto name = tokens[i + 1];
			const auto parameters = i + 2;
			while (i < tokens.size() && tokens[i] != "{")
				++i;
			if (i == tokens.size())
				return false;
			const auto body = i;
			size_t depth = 1;
			while (depth && ++i < tokens.size())
			{
				if (tokens[i] == "{")
					++depth;
				else if (tokens[i] == "}")
					--depth;
			}
			if (depth)
				return false;
			functions[name] = {parameters, body, i};
			declaration = {};
		}
	}

	BindingIndexParser indices(constants);
	for (const auto name : FindUsedBindings(tokens, functions, globals))
	{
		const auto& var = globals.at(name);
		ShaderBindingWebGPU b{};
		if (!indices.Parse(var.group, b.Group) || !indices.Parse(var.binding, b.Binding))
		{
			Log(LogType::Error, "Unsupported WGSL group or binding index for " + std::string(name));
			return false;
		}
		const auto resolved = ResolveType(var.type, aliases);
		if (resolved.empty())
			return false;
		const auto type = resolved.front();
		if (var.addressSpace == "storage")
		{
			b.Resource.ResourceType = ShaderResourceType::StorageBuffer;
			b.Resource.Access = var.access == "read_write" ? ShaderResourceAccess::ReadWrite : ShaderResourceAccess::ReadOnly;
		}
		else if (var.addressSpace == "uniform")
			b.Resource.ResourceType = ShaderResourceType::UniformBuffer;
		else if (type == "sampler" || type == "sampler_comparison")
		{
			b.Resource.ResourceType = ShaderResourceType::Sampler;
			b.ComparisonSampler = type == "sampler_comparison";
		}
		else if (type.substr(0, 16) == "texture_storage_")
			b.Resource.ResourceType = ShaderResourceType::StorageTexture;
		else if (type.starts_with("texture_"))
		{
			b.Resource.ResourceType = ShaderResourceType::Texture;
			if (type == "texture_2d" && resolved.size() >= 4 && resolved[1] == "<")
			{
				auto component = ResolveType(resolved.subspan(2, 1), aliases);
				b.SupportsWhiteFallback = !component.empty() && component.front() == "f32";
			}
		}
		else
			return false;
		b.Resource.Group = static_cast<int32_t>(b.Group);
		b.Resource.Slot = static_cast<int32_t>(b.Binding);
		bindings.push_back(b);
	}
	return true;
}

} // namespace

bool ShaderBindingWebGPU::operator==(const ShaderBindingWebGPU& rhs) const
{
	return Group == rhs.Group && Binding == rhs.Binding && Resource.ResourceType == rhs.Resource.ResourceType &&
		   Resource.Access == rhs.Resource.Access && ComparisonSampler == rhs.ComparisonSampler &&
		   SupportsWhiteFallback == rhs.SupportsWhiteFallback;
}

ShaderWebGPU::ShaderWebGPU() {}

ShaderWebGPU::~ShaderWebGPU() {}

bool ShaderWebGPU::Initialize(wgpu::Device& device, DataStructure* data, int32_t count)
{
	if (data == nullptr || count == 0)
	{
		return false;
	}

	wgpu::ShaderModuleDescriptor desc = {};

	if (data[0].Data == nullptr || data[0].Size <= 0)
	{
		return false;
	}

	const auto* bytes = static_cast<const uint8_t*>(data[0].Data);
	const bool hasWGSLHeader =
		data[0].Size >= static_cast<int32_t>(WgslCodeHeader.size()) && memcmp(bytes, WgslCodeHeader.data(), WgslCodeHeader.size()) == 0;
	const bool hasSPIRVMagic = data[0].Size >= 4 && bytes[0] == 0x03 && bytes[1] == 0x02 && bytes[2] == 0x23 && bytes[3] == 0x07;

#ifdef LLGI_WEBGPU_SPIRV_READER
	wgpu::ShaderSourceSPIRV spirvDesc{};
	std::vector<uint32_t> spirvWords;
#endif
	wgpu::ShaderSourceWGSL wgslDesc = {};
	std::string wgslCode;

	if (!hasSPIRVMagic)
	{
		const auto codeOffset = hasWGSLHeader ? WgslCodeHeader.size() : 0;
		if (data[0].Size <= static_cast<int32_t>(codeOffset))
		{
			return false;
		}

		wgslCode.assign(reinterpret_cast<const char*>(bytes + codeOffset), static_cast<size_t>(data[0].Size) - codeOffset);
		while (!wgslCode.empty() && wgslCode.back() == '\0')
		{
			wgslCode.pop_back();
		}
		wgslDesc.code = wgpu::StringView(wgslCode.data(), wgslCode.size());
		desc.nextInChain = reinterpret_cast<wgpu::ChainedStruct*>(&wgslDesc);
		if (!ReflectWGSL(wgslCode, bindings_))
			return false;
		hasBindingReflection_ = true;
	}
	else
	{
#ifdef LLGI_WEBGPU_SPIRV_READER
		if (data[0].Size % sizeof(uint32_t) != 0)
			return false;
		spirvWords.resize(data[0].Size / sizeof(uint32_t));
		std::memcpy(spirvWords.data(), bytes, data[0].Size);
		spirvDesc.code = spirvWords.data();
		spirvDesc.codeSize = spirvWords.size();
		if (!ReflectSPIRVTextures(spirvDesc.code, spirvDesc.codeSize, bindings_))
			return false;
		desc.nextInChain = reinterpret_cast<wgpu::ChainedStruct*>(&spirvDesc);
#else
		return false;
#endif
	}

	shaderModule_ = device.CreateShaderModule(&desc);

	return shaderModule_ != nullptr;
}

wgpu::ShaderModule& ShaderWebGPU::GetShaderModule() { return shaderModule_; }

const std::vector<ShaderBindingWebGPU>& ShaderWebGPU::GetBindings() const { return bindings_; }

bool ShaderWebGPU::HasBindingReflection() const { return hasBindingReflection_; }

} // namespace LLGI

#pragma once
#include "GfxShader.h"
#include "GfxInputLayout.h"

namespace adria
{
	struct GfxShaderCompileOutput
	{
		GfxShader shader;
		std::vector<std::string> includes;
		uint64 shader_hash[2];
	};
	using GfxShaderCompileInput = GfxShaderDesc;

	namespace GfxShaderCompiler
	{
		void Initialize();
		void Destroy();
		bool CompileShader(GfxShaderCompileInput const& input, GfxShaderCompileOutput& output, bool bypass_cache);
		void FillInputLayoutDesc(GfxShader const& vertex_shader, GfxInputLayout& input_layout);
		void ReadBlobFromFile(std::string const& filename, GfxShaderBlob& blob);
	}
}
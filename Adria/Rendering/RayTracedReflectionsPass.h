#pragma once
#include "Enums.h"
#include "BlurPass.h"
#include "../Graphics/RayTracingUtil.h"
#include "../Core/Definitions.h"
#include "../RenderGraph/RenderGraphResourceName.h"

namespace adria
{
	class RenderGraph;
	class GraphicsDevice;

	class RayTracedReflectionsPass
	{
	public:
		RayTracedReflectionsPass(GraphicsDevice* gfx, uint32 width, uint32 height);
		void AddPass(RenderGraph& rendergraph);
		void OnResize(uint32 w, uint32 h);
		bool IsSupported() const;

	private:
		GraphicsDevice* gfx;
		BlurPass blur_pass;
		Microsoft::WRL::ComPtr<ID3D12StateObject> ray_traced_reflections;
		uint32 width, height;
		bool is_supported;
		float reflection_roughness_scale = 0.0f;
	private:
		void CreateStateObject();
		void OnLibraryRecompiled(EShaderId shader);
	};
}
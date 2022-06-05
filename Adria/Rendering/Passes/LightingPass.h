#pragma once
#include <optional>
#include "../../Core/Definitions.h"
#include "../../RenderGraph/RenderGraphResourceRef.h"
#include "../../tecs/entity.h"

namespace adria
{
	namespace tecs
	{
		class registry;
	}
	class RenderGraph;
	class DescriptorHeap;
	struct Light;

	struct LightingPassData
	{
	};

	class LightingPass
	{
	public:
		LightingPass(uint32 w, uint32 h) : width(w), height(h) {}

		[[maybe_unused]] LightingPassData const& AddPass(RenderGraph& rendergraph, Light const& light,
			RGTextureRTVRef hdr_rtv,
			RGTextureSRVRef gbuffer_normal_srv,
			RGTextureSRVRef gbuffer_albedo_srv,
			RGTextureSRVRef depth_stencil_srv, std::optional<RGTextureSRVRef> shadow_map_srv = std::nullopt);

		void OnResize(uint32 w, uint32 h)
		{
			width = w, height = h;
		}

	private:
		uint32 width, height;
	};

}
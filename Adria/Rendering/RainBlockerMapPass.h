#pragma once
#include <memory>
#include "Graphics/GfxDescriptor.h"
#include "RenderGraph/RenderGraphResourceId.h"
#include "entt/entity/fwd.hpp"

namespace adria
{
	class RenderGraph;
	class GfxTexture;
	class GfxDevice;
	
	class RainBlockerMapPass
	{
		static constexpr uint32 BLOCKER_DIM = 256;
	public:
		RainBlockerMapPass(entt::registry& reg, GfxDevice* gfx, uint32 w, uint32 h);

		void AddPass(RenderGraph& rendergraph);
		void OnResize(uint32 w, uint32 h);

		int32 GetRainBlockerMapIdx() const;
		Matrix const& GetViewProjection() const { return view_projection; }

	private:
		entt::registry& reg;
		GfxDevice* gfx;
		uint32 width, height;
		Matrix view_projection;
		std::unique_ptr<GfxTexture> blocker_map;
		GfxDescriptor blocker_map_srv;
	};
}
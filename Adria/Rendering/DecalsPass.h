#pragma once
#include <memory>
#include "Core/CoreTypes.h"
#include "RenderGraph/RenderGraphResourceId.h"
#include "entt/entity/fwd.hpp"

namespace adria
{
	class RenderGraph;
	class GfxDevice;
	class GfxBuffer;

	class DecalsPass
	{
	public:
		DecalsPass(entt::registry& reg, uint32 w, uint32 h);

		void AddPass(RenderGraph& rendergraph);

		void OnResize(uint32 w, uint32 h);

		void OnSceneInitialized(GfxDevice* gfx);

	private:
		entt::registry& reg;
		uint32 width, height;
		std::unique_ptr<GfxBuffer>	cube_vb = nullptr;
		std::unique_ptr<GfxBuffer>	cube_ib = nullptr;

	private:
		void CreateCubeBuffers(GfxDevice* gfx);
	};
}
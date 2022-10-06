#pragma once
#include "../Core/Definitions.h"
#include "../RenderGraph/RenderGraphResourceId.h"
#include "../RenderGraph/RenderGraphResourceName.h"

namespace adria
{
	class RenderGraph;
	class Texture;
	class GraphicsDevice;

	class AutomaticExposurePass
	{
	public:
		AutomaticExposurePass(uint32 w, uint32 h);
		void CreateResources(GraphicsDevice* gfx);
		void AddPass(RenderGraph& rg, RGResourceName input);
		void OnResize(uint32 w, uint32 h);
	private:
		std::unique_ptr<Texture> previous_ev100;
		uint32 width, height;

		uint32 luminance_size_x;
		uint32 luminance_size_y;
		uint32 luminance_mips = 0;

		float32 min_luminance = 0.0f;
		float32 max_luminance = 10.0f;
		float32 adaption_speed = 1.5f;
		float32 low_percentile = 0.1f;
		float32 high_percentile = 0.9f;

		bool invalid_history = true;
	};
}



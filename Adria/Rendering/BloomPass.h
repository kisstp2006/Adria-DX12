#pragma once
#include "RenderGraph/RenderGraphResourceName.h"
#include "Graphics/GfxPipelineStatePermutations.h"

namespace adria
{
	class GfxDevice;
	class RenderGraph;

	class BloomPass
	{

	public:
		BloomPass(GfxDevice* gfx, uint32 w, uint32 h);

		RGResourceName AddPass(RenderGraph& rendergraph, RGResourceName input);
		void OnResize(uint32 w, uint32 h);

	private:
		GfxDevice* gfx;
		uint32 width, height;
		GfxComputePipelineStatePermutations<2> downsample_psos;
		std::unique_ptr<GfxComputePipelineState> upsample_pso;

	private:
		void CreatePSOs();

		RGResourceName DownsamplePass(RenderGraph& rendergraph, RGResourceName input, uint32 pass_idx);
		RGResourceName UpsamplePass(RenderGraph& rendergraph, RGResourceName input, RGResourceName, uint32 pass_idx);
	};

	
}
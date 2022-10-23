#pragma once
#include "BlurPass.h"
#include "../Core/Definitions.h"
#include "../RenderGraph/RenderGraphResourceId.h"


namespace adria
{
	class RenderGraph;
	class GraphicsDevice;
	class Texture;


	class HBAOPass
	{
		struct HBAOParams
		{
			float   hbao_power = 1.5f;
			float   hbao_radius = 2.0f;
		};
	public:
		static constexpr uint32 NOISE_DIM = 8;
	public:
		HBAOPass(uint32 w, uint32 h);
		void AddPass(RenderGraph& rendergraph);
		void OnResize(uint32 w, uint32 h);
		void OnSceneInitialized(GraphicsDevice* gfx);
	private:
		uint32 width, height;
		HBAOParams params{};
		std::unique_ptr<Texture> hbao_random_texture;
		BlurPass blur_pass;
	};

}
#pragma once
#include <vector>
#include "../Core/Definitions.h"
#include "../RenderGraph/RenderGraphResourceId.h"


namespace adria
{
	class RenderGraph;
	class GraphicsDevice;
	class TextureManager;

	struct CloudParameters
	{
		float crispiness = 43.0f;
		float curliness = 3.6f;
		float coverage = 0.505f;
		float wind_speed = 5.0f;
		float light_absorption = 0.003f;
		float clouds_bottom_height = 3000.0f;
		float clouds_top_height = 10000.0f;
		float density_factor = 0.015f;
		float cloud_type = 1.0f;
	};

	class VolumetricCloudsPass
	{
	public:
		VolumetricCloudsPass(TextureManager& texture_manager, uint32 w, uint32 h);

		void AddPass(RenderGraph& rendergraph);
		void OnResize(uint32 w, uint32 h);
		void OnSceneInitialized(GraphicsDevice* gfx);

		CloudParameters const& GetParams() const { return params; }

	private:
		TextureManager& texture_manager;
		uint32 width, height;
		std::vector<size_t> cloud_textures;
		CloudParameters params{};
	};

}
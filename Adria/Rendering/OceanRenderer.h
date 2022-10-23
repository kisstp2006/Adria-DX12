#pragma once
#include <memory>
#include "../Core/Definitions.h"
#include "../RenderGraph/RenderGraphResourceId.h"
#include "entt/entity/fwd.hpp"

namespace adria
{
	class RenderGraph;
	class TextureManager;
	class GraphicsDevice;
	class Texture;

	class OceanRenderer
	{
		static constexpr uint32 FFT_RESOLUTION = 512;

	public:
		OceanRenderer(entt::registry& reg, TextureManager& texture_manager, uint32 w, uint32 h);

		void AddPasses(RenderGraph& rendergraph);

		void OnResize(uint32 w, uint32 h);

		void OnSceneInitialized(GraphicsDevice* gfx);

		float const* GetWindDirection() const { return wind_direction; }
		float GetChoppiness() const { return ocean_choppiness; }
	private:
		entt::registry& reg;
		TextureManager& texture_manager;
		size_t foam_handle = -1;
		size_t perlin_handle = -1;
		uint32 width, height;
		std::unique_ptr<Texture> initial_spectrum;
		std::unique_ptr<Texture> ping_pong_phase_textures[2];
		bool pong_phase = false;
		std::unique_ptr<Texture> ping_pong_spectrum_textures[2];
		bool pong_spectrum = false;

		//settings
		bool ocean_wireframe = false;
		bool ocean_tesselation = false;
		float ocean_color[3] = { 0.0123f, 0.3613f, 0.6867f };
		float ocean_choppiness = 1.2f;
		bool ocean_color_changed = false;
		bool recreate_initial_spectrum = true;
		float wind_direction[2] = { 10.0f, 10.0f };
	};
}
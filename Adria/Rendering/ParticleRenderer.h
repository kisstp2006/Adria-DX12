#pragma once
#include <memory>
#include <d3d12.h>
#include <wrl.h>
#include <DirectXMath.h>
#include "../Core/Definitions.h"
#include "../RenderGraph/RenderGraphResourceId.h"
#include "../Utilities/HashMap.h"
#include "../Graphics/CommandSignature.h"
#include "entt/entity/fwd.hpp"

namespace adria
{
	class RenderGraph;
	class TextureManager;
	class GraphicsDevice;
	class Texture;
	class Buffer;
	struct Emitter;

	class ParticleRenderer
	{
		static constexpr size_t MAX_PARTICLES = 100 * 1024;

		struct GPUParticleA
		{
			DirectX::XMFLOAT4	TintAndAlpha;		// The color and opacity
			float		Rotation;					// The rotation angle
			uint32		IsSleeping;					// Whether or not the particle is sleeping (ie, don't update position)
		};
		struct GPUParticleB
		{
			DirectX::XMFLOAT3	Position;		// World space position
			float		Mass;						// Mass of particle

			DirectX::XMFLOAT3	Velocity;		// World space velocity
			float		Lifespan;					// Lifespan of the particle.

			float		DistanceToEye;				// The distance from the particle to the eye
			float		Age;						// The current age counting down from lifespan to zero
			float		StartSize;					// The size at spawn time
			float		EndSize;					// The time at maximum age
		};
		struct EmitterCBuffer
		{
			DirectX::XMFLOAT4	EmitterPosition;
			DirectX::XMFLOAT4	EmitterVelocity;
			DirectX::XMFLOAT4	PositionVariance;

			int32	MaxParticlesThisFrame;
			float	ParticleLifeSpan;
			float	StartSize;
			float	EndSize;

			float	VelocityVariance;
			float	Mass;
			float	ElapsedTime;
			int32 Collisions;

			int32 CollisionThickness;
		};
		struct IndexBufferElement
		{
			float	distance;
			float	index;
		};
		struct ViewSpacePositionRadius
		{
			DirectX::XMFLOAT3 viewspace_position;
			float radius;
		};
		struct SortDispatchInfo
		{
			int32 x, y, z, w;
		};
	public:
		ParticleRenderer(entt::registry& reg, GraphicsDevice* gfx, TextureManager& texture_manager, uint32 w, uint32 h);
		void Update(float dt);
		void AddPasses(RenderGraph& rendergraph);
		void OnResize(uint32 w, uint32 h);
		void OnSceneInitialized();
		void OnEmitterAdded(size_t id);
		void OnEmitterRemoved(size_t id);
	private:
		entt::registry& reg;
		GraphicsDevice* gfx;
		TextureManager& texture_manager;
		uint32 width, height;

		std::unique_ptr<DrawIndexedIndirectSignature>   indirect_render_args_signature;
		std::unique_ptr<DispatchIndirectSignature>		indirect_sort_args_signature;
		std::unique_ptr<Texture> random_texture;
		std::unique_ptr<Buffer> index_buffer;
		std::unique_ptr<Buffer> counter_reset_buffer;

		HashMap<size_t, std::unique_ptr<Buffer>> dead_list_buffer_map;			
		HashMap<size_t, std::unique_ptr<Buffer>> dead_list_buffer_counter_map;
		HashMap<size_t, std::unique_ptr<Buffer>> particle_bufferA_map;			
		HashMap<size_t, std::unique_ptr<Buffer>> particle_bufferB_map;			
		HashMap<size_t, std::unique_ptr<Buffer>> view_space_positions_buffer_map;
		HashMap<size_t, std::unique_ptr<Buffer>> alive_index_buffer_map;
		HashMap<size_t, std::unique_ptr<Buffer>> alive_index_buffer_counter_map;

	private:
		void AddInitializeDeadListPass(RenderGraph& rendergraph, Emitter const& emitter_params, size_t emitter_id);
		void AddResetParticlesPass(RenderGraph& rendergraph, Emitter const& emitter_params, size_t emitter_id);
		void AddEmitPass(RenderGraph& rendergraph, Emitter const& emitter_params, size_t emitter_id);
		void AddSimulatePass(RenderGraph& rendergraph, Emitter const& emitter_params, size_t emitter_id);
		void AddSortPasses(RenderGraph& rendergraph, Emitter const& emitter_params, size_t emitter_id);
		void AddRasterizePass(RenderGraph& rendergraph, Emitter const& emitter_params, size_t emitter_id);
	};
}
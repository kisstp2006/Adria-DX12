#pragma once
#include <unordered_map>
#include <DirectXMath.h>
#include "Enums.h"
#include "Components.h"
#include "../tecs/registry.h"
#include "../Graphics/ConstantBuffer.h"
#include "../Graphics/VertexBuffer.h"
#include "../Graphics/IndexBuffer.h"
#include "../Graphics/StructuredBuffer.h"
#include "../Graphics/ShaderUtility.h"
#include "../Graphics/Texture2D.h"
#include "../Graphics/GraphicsCoreDX12.h"
#include "../Graphics/DescriptorHeap.h"
#include "../Utilities/Random.h"
#include "pix3.h"

#include "../Logging/Logger.h"

namespace adria
{
	//based on AMD GPU Particles Sample: https://github.com/GPUOpen-LibrariesAndSDKs/GPUParticles11

	class ParticleRenderer
	{
		static constexpr size_t MAX_PARTICLES = 400 * 1024;

		struct GPUParticleA
		{
			DirectX::XMFLOAT4	TintAndAlpha;	// The color and opacity
			F32		Rotation;					// The rotation angle
			U32		IsSleeping;					// Whether or not the particle is sleeping (ie, don't update position)
		};
		struct GPUParticleB
		{
			DirectX::XMFLOAT3	Position;		// World space position
			F32		Mass;						// Mass of particle

			DirectX::XMFLOAT3	Velocity;		// World space velocity
			F32		Lifespan;					// Lifespan of the particle.

			F32		DistanceToEye;				// The distance from the particle to the eye
			F32		Age;						// The current age counting down from lifespan to zero
			F32		StartSize;					// The size at spawn time
			F32		EndSize;					// The time at maximum age
		};
		struct EmitterCBuffer
		{
			DirectX::XMFLOAT4	EmitterPosition;
			DirectX::XMFLOAT4	EmitterVelocity;
			DirectX::XMFLOAT4	PositionVariance;

			I32	MaxParticlesThisFrame;
			F32	ParticleLifeSpan;
			F32	StartSize;
			F32	EndSize;

			F32	VelocityVariance;
			F32	Mass;
			F32	ElapsedTime;
			I32 Collisions;

			I32 CollisionThickness;
		};
		struct IndexBufferElement
		{
			F32	distance;
			F32	index;
		};
		struct ViewSpacePositionRadius
		{
			DirectX::XMFLOAT3 viewspace_position;
			F32 radius;
		};
		struct SortDispatchInfo
		{
			I32 x, y, z, w;
		};
	public:
		ParticleRenderer(GraphicsCoreDX12* gfx) : gfx{ gfx },
			dead_list_buffer(gfx->GetDevice(), MAX_PARTICLES, true, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
			particle_bufferA(gfx->GetDevice(), MAX_PARTICLES),
			particle_bufferB(gfx->GetDevice(), MAX_PARTICLES),
			view_space_positions_buffer(gfx->GetDevice(), MAX_PARTICLES),
			alive_index_buffer(gfx->GetDevice(), MAX_PARTICLES, true)
		{
			LoadShaders();
			CreatePipelineStateObjects();
			CreateResources();
		}

		void UploadData()
		{
			ID3D12Device* device = gfx->GetDevice();
			ID3D12GraphicsCommandList* cmd_list = gfx->GetDefaultCommandList();

			ID3D12Resource* particle_upload_texture = nullptr;
			const U64 upload_buffer_size = GetRequiredIntermediateSize(random_texture.Resource(), 0, 1);

			CD3DX12_HEAP_PROPERTIES heap_properties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
			CD3DX12_RESOURCE_DESC resource_desc = CD3DX12_RESOURCE_DESC::Buffer(upload_buffer_size);
			BREAK_IF_FAILED(device->CreateCommittedResource(
				&heap_properties,
				D3D12_HEAP_FLAG_NONE,
				&resource_desc,
				D3D12_RESOURCE_STATE_GENERIC_READ,
				nullptr,
				IID_PPV_ARGS(&particle_upload_texture)));

			RealRandomGenerator rand_float{ 0.0f, 1.0f };
			std::vector<F32> random_texture_data;
			for (U32 i = 0; i < random_texture.Width() * random_texture.Height(); i++)
			{
				random_texture_data.push_back(2.0f * rand_float() - 1.0f);
				random_texture_data.push_back(2.0f * rand_float() - 1.0f);
				random_texture_data.push_back(2.0f * rand_float() - 1.0f);
				random_texture_data.push_back(2.0f * rand_float() - 1.0f);
			}

			D3D12_SUBRESOURCE_DATA data{};
			data.pData = random_texture_data.data();
			data.RowPitch = random_texture.Width() * 4 * sizeof(F32);
			data.SlicePitch = 0;

			UpdateSubresources(cmd_list, random_texture.Resource(), particle_upload_texture, 0, 0, 1, &data);
			CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(random_texture.Resource(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
			cmd_list->ResourceBarrier(1, &barrier);

			gfx->AddToReleaseQueue(particle_upload_texture);

			std::vector<UINT> indices(MAX_PARTICLES * 6);
			UINT base = 0;
			size_t offset = 0;
			for (size_t i = 0; i < MAX_PARTICLES; i++)
			{
				indices[offset + 0] = base + 0;
				indices[offset + 1] = base + 1;
				indices[offset + 2] = base + 2;

				indices[offset + 3] = base + 2;
				indices[offset + 4] = base + 1;
				indices[offset + 5] = base + 3;

				base += 4;
				offset += 6;
			}
			index_buffer = std::make_unique<IndexBuffer>(gfx, indices);
		}

		void SetCBuffersForThisFrame(D3D12_GPU_VIRTUAL_ADDRESS frame_cbuffer_address, D3D12_GPU_VIRTUAL_ADDRESS compute_cbuffer_address)
		{
			this->frame_cbuffer_address = frame_cbuffer_address;
			this->compute_cbuffer_address = compute_cbuffer_address;
		}

		void Update(F32 dt, Emitter& emitter_params)
		{
			emitter_params.elapsed_time += dt;
			if (emitter_params.particles_per_second > 0.0f)
			{
				emitter_params.accumulation += emitter_params.particles_per_second * dt;

				if (emitter_params.accumulation > 1.0f)
				{
					F64 integer_part = 0.0;
					F32 fraction = (F32)modf(emitter_params.accumulation, &integer_part);

					emitter_params.number_to_emit = (I32)integer_part;
					emitter_params.accumulation = fraction;
				}
			}
		}

		void Render(ID3D12GraphicsCommandList* cmd_list, Emitter const& emitter_params, 
			D3D12_CPU_DESCRIPTOR_HANDLE depth_srv, D3D12_CPU_DESCRIPTOR_HANDLE particle_srv)
		{
			if (emitter_params.reset_emitter)
			{
				InitializeDeadList(cmd_list);
				ResetParticles(cmd_list);
				emitter_params.reset_emitter = false;
			}
			Emit(cmd_list, emitter_params);
			Simulate(cmd_list, depth_srv);
			if (emitter_params.sort)
			{
				Sort(cmd_list);
			}
			Rasterize(cmd_list, emitter_params, depth_srv, particle_srv);
		}

	private:
		GraphicsCoreDX12* gfx;

		Texture2D random_texture;
		StructuredBuffer<U32> dead_list_buffer;
		StructuredBuffer<GPUParticleA> particle_bufferA;
		StructuredBuffer<GPUParticleB> particle_bufferB;
		StructuredBuffer<ViewSpacePositionRadius> view_space_positions_buffer;
		StructuredBuffer<IndexBufferElement> alive_index_buffer;

		D3D12_GPU_VIRTUAL_ADDRESS frame_cbuffer_address;
		D3D12_GPU_VIRTUAL_ADDRESS compute_cbuffer_address;

		DynamicAllocation emitter_allocation;
		DynamicAllocation sort_dispatch_info_allocation;

		Microsoft::WRL::ComPtr<ID3D12CommandSignature>  indirect_render_args_signature;
		Microsoft::WRL::ComPtr<ID3D12Resource> indirect_render_args_buffer;
		D3D12_CPU_DESCRIPTOR_HANDLE indirect_render_args_uav;
		Microsoft::WRL::ComPtr<ID3D12CommandSignature>  indirect_sort_args_signature;
		Microsoft::WRL::ComPtr<ID3D12Resource> indirect_sort_args_buffer;
		D3D12_CPU_DESCRIPTOR_HANDLE indirect_sort_args_uav;
		Microsoft::WRL::ComPtr<ID3D12Resource> counter_reset_buffer;

		std::unique_ptr<DescriptorHeap> particle_heap;
		std::unique_ptr<IndexBuffer> index_buffer;

		std::unordered_map<EShader, ShaderBlob> particle_shader_map;
		std::unordered_map<ERootSignature, Microsoft::WRL::ComPtr<ID3D12RootSignature>> particle_rs_map;
		std::unordered_map<EPipelineStateObject, Microsoft::WRL::ComPtr<ID3D12PipelineState>> particle_pso_map;

	private:

		void LoadShaders()
		{
			ShaderBlob cs_blob;

			ShaderUtility::GetBlobFromCompiledShader(L"Resources/Compiled Shaders/InitDeadListCS.cso", cs_blob);
			particle_shader_map[CS_ParticleInitDeadList] = cs_blob;

			ShaderUtility::GetBlobFromCompiledShader(L"Resources/Compiled Shaders/ParticleResetCS.cso", cs_blob);
			particle_shader_map[CS_ParticleReset] = cs_blob;

			ShaderUtility::GetBlobFromCompiledShader(L"Resources/Compiled Shaders/ParticleEmitCS.cso", cs_blob);
			particle_shader_map[CS_ParticleEmit] = cs_blob;

			ShaderUtility::GetBlobFromCompiledShader(L"Resources/Compiled Shaders/ParticleSimulateCS.cso", cs_blob);
			particle_shader_map[CS_ParticleSimulate] = cs_blob;

			ShaderUtility::GetBlobFromCompiledShader(L"Resources/Compiled Shaders/BitonicSortStepCS.cso", cs_blob);
			particle_shader_map[CS_ParticleBitonicSortStep] = cs_blob;

			ShaderUtility::GetBlobFromCompiledShader(L"Resources/Compiled Shaders/Sort512CS.cso", cs_blob);
			particle_shader_map[CS_ParticleSort512] = cs_blob;

			ShaderUtility::GetBlobFromCompiledShader(L"Resources/Compiled Shaders/SortInner512CS.cso", cs_blob);
			particle_shader_map[CS_ParticleSortInner512] = cs_blob;

			ShaderUtility::GetBlobFromCompiledShader(L"Resources/Compiled Shaders/InitSortDispatchArgsCS.cso", cs_blob);
			particle_shader_map[CS_ParticleInitSortDispatchArgs] = cs_blob;

			ShaderBlob vs_blob, ps_blob;
			ShaderUtility::GetBlobFromCompiledShader(L"Resources/Compiled Shaders/ParticleVS.cso", vs_blob);
			particle_shader_map[VS_Particles] = vs_blob;

			ShaderUtility::GetBlobFromCompiledShader(L"Resources/Compiled Shaders/ParticlePS.cso", ps_blob);
			particle_shader_map[PS_Particles] = ps_blob;
		}
		void CreatePipelineStateObjects()
		{
			ID3D12Device* device = gfx->GetDevice();
			D3D12_FEATURE_DATA_ROOT_SIGNATURE feature_data{};
			feature_data.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;

			if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &feature_data, sizeof(feature_data))))
				feature_data.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;

			//root signatures
			{
				BREAK_IF_FAILED(device->CreateRootSignature(0, particle_shader_map[CS_ParticleInitDeadList].GetPointer(), particle_shader_map[CS_ParticleInitDeadList].GetLength(),
					IID_PPV_ARGS(particle_rs_map[ERootSignature::Particles_InitDeadList].GetAddressOf())));

				BREAK_IF_FAILED(device->CreateRootSignature(0, particle_shader_map[CS_ParticleReset].GetPointer(), particle_shader_map[CS_ParticleReset].GetLength(),
					IID_PPV_ARGS(particle_rs_map[ERootSignature::Particles_Reset].GetAddressOf())));

				BREAK_IF_FAILED(device->CreateRootSignature(0, particle_shader_map[CS_ParticleSimulate].GetPointer(), particle_shader_map[CS_ParticleSimulate].GetLength(),
					IID_PPV_ARGS(particle_rs_map[ERootSignature::Particles_Simulate].GetAddressOf())));

				BREAK_IF_FAILED(device->CreateRootSignature(0, particle_shader_map[CS_ParticleEmit].GetPointer(), particle_shader_map[CS_ParticleEmit].GetLength(),
					IID_PPV_ARGS(particle_rs_map[ERootSignature::Particles_Emit].GetAddressOf())));

				BREAK_IF_FAILED(device->CreateRootSignature(0, particle_shader_map[PS_Particles].GetPointer(), particle_shader_map[PS_Particles].GetLength(),
					IID_PPV_ARGS(particle_rs_map[ERootSignature::Particles_Shading].GetAddressOf())));

				BREAK_IF_FAILED(device->CreateRootSignature(0, particle_shader_map[CS_ParticleInitSortDispatchArgs].GetPointer(), particle_shader_map[CS_ParticleInitSortDispatchArgs].GetLength(),
					IID_PPV_ARGS(particle_rs_map[ERootSignature::Particles_InitSortDispatchArgs].GetAddressOf())));

				BREAK_IF_FAILED(device->CreateRootSignature(0, particle_shader_map[CS_ParticleSort512].GetPointer(), particle_shader_map[CS_ParticleSort512].GetLength(),
					IID_PPV_ARGS(particle_rs_map[ERootSignature::Particles_Sort].GetAddressOf())));
			}

			//PSOs
			{
				D3D12_COMPUTE_PIPELINE_STATE_DESC compute_pso_desc = {};
				compute_pso_desc.pRootSignature = particle_rs_map[ERootSignature::Particles_InitDeadList].Get();
				compute_pso_desc.CS = particle_shader_map[CS_ParticleInitDeadList];
				BREAK_IF_FAILED(device->CreateComputePipelineState(&compute_pso_desc, IID_PPV_ARGS(&particle_pso_map[EPipelineStateObject::Particles_InitDeadList])));

				compute_pso_desc.pRootSignature = particle_rs_map[ERootSignature::Particles_Reset].Get();
				compute_pso_desc.CS = particle_shader_map[CS_ParticleReset];
				BREAK_IF_FAILED(device->CreateComputePipelineState(&compute_pso_desc, IID_PPV_ARGS(&particle_pso_map[EPipelineStateObject::Particles_Reset])));

				compute_pso_desc.pRootSignature = particle_rs_map[ERootSignature::Particles_Simulate].Get();
				compute_pso_desc.CS = particle_shader_map[CS_ParticleSimulate];
				BREAK_IF_FAILED(device->CreateComputePipelineState(&compute_pso_desc, IID_PPV_ARGS(&particle_pso_map[EPipelineStateObject::Particles_Simulate])));

				compute_pso_desc.pRootSignature = particle_rs_map[ERootSignature::Particles_Emit].Get();
				compute_pso_desc.CS = particle_shader_map[CS_ParticleEmit];
				BREAK_IF_FAILED(device->CreateComputePipelineState(&compute_pso_desc, IID_PPV_ARGS(&particle_pso_map[EPipelineStateObject::Particles_Emit])));

				compute_pso_desc.pRootSignature = particle_rs_map[ERootSignature::Particles_InitSortDispatchArgs].Get();
				compute_pso_desc.CS = particle_shader_map[CS_ParticleInitSortDispatchArgs];
				BREAK_IF_FAILED(device->CreateComputePipelineState(&compute_pso_desc, IID_PPV_ARGS(&particle_pso_map[EPipelineStateObject::Particles_InitSortDispatchArgs])));

				compute_pso_desc.pRootSignature = particle_rs_map[ERootSignature::Particles_Sort].Get();
				compute_pso_desc.CS = particle_shader_map[CS_ParticleBitonicSortStep];
				BREAK_IF_FAILED(device->CreateComputePipelineState(&compute_pso_desc, IID_PPV_ARGS(&particle_pso_map[EPipelineStateObject::Particles_BitonicSortStep])));

				compute_pso_desc.CS = particle_shader_map[CS_ParticleSort512];
				BREAK_IF_FAILED(device->CreateComputePipelineState(&compute_pso_desc, IID_PPV_ARGS(&particle_pso_map[EPipelineStateObject::Particles_Sort512])));

				compute_pso_desc.CS = particle_shader_map[CS_ParticleSortInner512];
				BREAK_IF_FAILED(device->CreateComputePipelineState(&compute_pso_desc, IID_PPV_ARGS(&particle_pso_map[EPipelineStateObject::Particles_SortInner512])));

				D3D12_GRAPHICS_PIPELINE_STATE_DESC graphics_pso_desc{};
				graphics_pso_desc.InputLayout = { nullptr, 0u };
				graphics_pso_desc.pRootSignature = particle_rs_map[ERootSignature::Particles_Shading].Get();
				graphics_pso_desc.VS = particle_shader_map[VS_Particles];
				graphics_pso_desc.PS = particle_shader_map[PS_Particles];
				graphics_pso_desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
				graphics_pso_desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
				graphics_pso_desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
				graphics_pso_desc.BlendState.RenderTarget[0].BlendEnable = TRUE;
				graphics_pso_desc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
				graphics_pso_desc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
				graphics_pso_desc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
				graphics_pso_desc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
				graphics_pso_desc.DepthStencilState.DepthEnable = FALSE;
				graphics_pso_desc.DepthStencilState.StencilEnable = FALSE;
				graphics_pso_desc.SampleMask = UINT_MAX;
				graphics_pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
				graphics_pso_desc.NumRenderTargets = 1;
				graphics_pso_desc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
				graphics_pso_desc.SampleDesc.Count = 1;
				graphics_pso_desc.DSVFormat = DXGI_FORMAT_UNKNOWN;

				BREAK_IF_FAILED(device->CreateGraphicsPipelineState(&graphics_pso_desc, IID_PPV_ARGS(&particle_pso_map[EPipelineStateObject::Particles_Shading])));
			}
		}
		void CreateResources()
		{
			ID3D12Device* device = gfx->GetDevice();
			particle_heap = std::make_unique<DescriptorHeap>(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 50);

			//creating command signatures
			{
				D3D12_INDIRECT_ARGUMENT_DESC args[1] = {};
				args[0].Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;

				D3D12_COMMAND_SIGNATURE_DESC command_signature_desc{};
				command_signature_desc.NumArgumentDescs = 1;
				command_signature_desc.pArgumentDescs = args;
				command_signature_desc.ByteStride = sizeof(D3D12_DRAW_INDEXED_ARGUMENTS);
				BREAK_IF_FAILED(device->CreateCommandSignature(&command_signature_desc, nullptr, IID_PPV_ARGS(&indirect_render_args_signature)));

				args[0].Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;
				command_signature_desc.ByteStride = sizeof(D3D12_DISPATCH_ARGUMENTS);
				BREAK_IF_FAILED(device->CreateCommandSignature(&command_signature_desc, nullptr, IID_PPV_ARGS(&indirect_sort_args_signature)));
			}
			//creating indirect args buffers
			{
				auto heap_properties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);

				D3D12_RESOURCE_DESC indirect_render_args_desc{};
				indirect_render_args_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
				indirect_render_args_desc.Alignment = 0;
				indirect_render_args_desc.SampleDesc.Count = 1;
				indirect_render_args_desc.Format = DXGI_FORMAT_UNKNOWN;
				indirect_render_args_desc.Width = 5 * sizeof(U32);
				indirect_render_args_desc.Height = 1;
				indirect_render_args_desc.DepthOrArraySize = 1;
				indirect_render_args_desc.MipLevels = 1;
				indirect_render_args_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
				indirect_render_args_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

				BREAK_IF_FAILED(device->CreateCommittedResource(&heap_properties, D3D12_HEAP_FLAG_NONE, &indirect_render_args_desc,
					D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&indirect_render_args_buffer)));

				D3D12_RESOURCE_DESC indirect_sort_args_desc{};
				indirect_sort_args_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
				indirect_sort_args_desc.Alignment = 0;
				indirect_sort_args_desc.SampleDesc.Count = 1;
				indirect_sort_args_desc.Format = DXGI_FORMAT_UNKNOWN;
				indirect_sort_args_desc.Width = 4 * sizeof(U32);
				indirect_sort_args_desc.Height = 1;
				indirect_sort_args_desc.DepthOrArraySize = 1;
				indirect_sort_args_desc.MipLevels = 1;
				indirect_sort_args_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
				indirect_sort_args_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

				BREAK_IF_FAILED(device->CreateCommittedResource(&heap_properties, D3D12_HEAP_FLAG_NONE, &indirect_sort_args_desc,
					D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&indirect_sort_args_buffer)));
			}
			//creating random texture
			{
				//noise texture
				texture2d_desc_t noise_desc{};
				noise_desc.width = 1024;
				noise_desc.height = 1024;
				noise_desc.format = DXGI_FORMAT_R32G32B32A32_FLOAT;
				noise_desc.start_state = D3D12_RESOURCE_STATE_COPY_DEST;
				noise_desc.clear_value.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
				random_texture = Texture2D(device, noise_desc);
			}
			//creating views
			{
				U32 heap_index = 0;
				random_texture.CreateSRV(particle_heap->GetCpuHandle(heap_index++));

				dead_list_buffer.CreateUAV(particle_heap->GetCpuHandle(heap_index++));
				dead_list_buffer.CreateCounterUAV(particle_heap->GetCpuHandle(heap_index++));

				particle_bufferA.CreateSRV(particle_heap->GetCpuHandle(heap_index++));
				particle_bufferA.CreateUAV(particle_heap->GetCpuHandle(heap_index++));

				particle_bufferB.CreateSRV(particle_heap->GetCpuHandle(heap_index++));
				particle_bufferB.CreateUAV(particle_heap->GetCpuHandle(heap_index++));

				view_space_positions_buffer.CreateSRV(particle_heap->GetCpuHandle(heap_index++));
				view_space_positions_buffer.CreateUAV(particle_heap->GetCpuHandle(heap_index++));

				alive_index_buffer.CreateSRV(particle_heap->GetCpuHandle(heap_index++));
				alive_index_buffer.CreateUAV(particle_heap->GetCpuHandle(heap_index++));
				alive_index_buffer.CreateCounterUAV(particle_heap->GetCpuHandle(heap_index++));

				D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc{};
				uav_desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
				uav_desc.Format = DXGI_FORMAT_R32_UINT; 
				uav_desc.Buffer.FirstElement = 0;
				uav_desc.Buffer.NumElements = 5;
				uav_desc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

				D3D12_CPU_DESCRIPTOR_HANDLE uav_handle = particle_heap->GetCpuHandle(heap_index++);
				device->CreateUnorderedAccessView(indirect_render_args_buffer.Get(), nullptr, &uav_desc, uav_handle);
				indirect_render_args_uav = uav_handle;

				uav_desc.Buffer.NumElements = 4;
				uav_handle = particle_heap->GetCpuHandle(heap_index++);
				device->CreateUnorderedAccessView(indirect_sort_args_buffer.Get(), nullptr, &uav_desc, uav_handle);
				indirect_sort_args_uav = uav_handle;
			}
			//creating reset counter buffer
			{
				CD3DX12_RESOURCE_DESC buffer_desc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(U32));
				CD3DX12_HEAP_PROPERTIES upload_heap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
				BREAK_IF_FAILED(gfx->GetDevice()->CreateCommittedResource(
					&upload_heap,
					D3D12_HEAP_FLAG_NONE,
					&buffer_desc,
					D3D12_RESOURCE_STATE_GENERIC_READ,
					nullptr,
					IID_PPV_ARGS(&counter_reset_buffer)));

				U8* mapped_reset_buffer = nullptr;
				CD3DX12_RANGE read_range(0, 0); 
				BREAK_IF_FAILED(counter_reset_buffer->Map(0, &read_range, reinterpret_cast<void**>(&mapped_reset_buffer)));
				ZeroMemory(mapped_reset_buffer, sizeof(U32));
				counter_reset_buffer->Unmap(0, nullptr);
			}
		}

		void InitializeDeadList(ID3D12GraphicsCommandList* cmd_list)
		{
			ID3D12Device* device = gfx->GetDevice();
			LinearDescriptorAllocator* descriptor_allocator = gfx->GetDescriptorAllocator();

			D3D12_RESOURCE_BARRIER prereset_barrier = CD3DX12_RESOURCE_BARRIER::Transition(dead_list_buffer.CounterBuffer(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST);
			cmd_list->ResourceBarrier(1, &prereset_barrier);
			cmd_list->CopyBufferRegion(dead_list_buffer.CounterBuffer(), 0, counter_reset_buffer.Get(), 0, sizeof(U32));
			D3D12_RESOURCE_BARRIER postreset_barrier = CD3DX12_RESOURCE_BARRIER::Transition(dead_list_buffer.CounterBuffer(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			cmd_list->ResourceBarrier(1, &postreset_barrier);

			cmd_list->SetComputeRootSignature(particle_rs_map[ERootSignature::Particles_InitDeadList].Get());
			cmd_list->SetPipelineState(particle_pso_map[EPipelineStateObject::Particles_InitDeadList].Get());
			OffsetType descriptor_index = descriptor_allocator->Allocate();
			device->CopyDescriptorsSimple(1, descriptor_allocator->GetCpuHandle(descriptor_index), dead_list_buffer.UAV(),
				D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			cmd_list->SetComputeRootDescriptorTable(0, descriptor_allocator->GetGpuHandle(descriptor_index));
			cmd_list->Dispatch((U32)std::ceil(MAX_PARTICLES * 1.0f / 256), 1, 1);
		}
		void ResetParticles(ID3D12GraphicsCommandList* cmd_list)
		{
			ID3D12Device* device = gfx->GetDevice();
			LinearDescriptorAllocator* descriptor_allocator = gfx->GetDescriptorAllocator();

			cmd_list->SetComputeRootSignature(particle_rs_map[ERootSignature::Particles_Reset].Get());
			cmd_list->SetPipelineState(particle_pso_map[EPipelineStateObject::Particles_Reset].Get());
			OffsetType descriptor_index = descriptor_allocator->AllocateRange(2);
			device->CopyDescriptorsSimple(1, descriptor_allocator->GetCpuHandle(descriptor_index), particle_bufferA.UAV(),
				D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			device->CopyDescriptorsSimple(1, descriptor_allocator->GetCpuHandle(descriptor_index + 1), particle_bufferB.UAV(),
				D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			cmd_list->SetComputeRootDescriptorTable(0, descriptor_allocator->GetGpuHandle(descriptor_index));
			cmd_list->Dispatch((U32)std::ceil(MAX_PARTICLES * 1.0f / 256), 1, 1);
		}

		void Emit(ID3D12GraphicsCommandList* cmd_list, Emitter const& emitter_params)
		{
			PIXScopedEvent(cmd_list, PIX_COLOR_DEFAULT, "Particles Emit Pass");
			ID3D12Device* device = gfx->GetDevice();
			LinearDescriptorAllocator* descriptor_allocator = gfx->GetDescriptorAllocator();
			LinearUploadBuffer* upload_buffer = gfx->GetUploadBuffer();

			if (emitter_params.number_to_emit > 0)
			{
				EmitterCBuffer emitter_cbuffer_data{};
				emitter_cbuffer_data.ElapsedTime = emitter_params.elapsed_time;
				emitter_cbuffer_data.EmitterPosition = emitter_params.position;
				emitter_cbuffer_data.EmitterVelocity = emitter_params.velocity;
				emitter_cbuffer_data.StartSize = emitter_params.start_size;
				emitter_cbuffer_data.EndSize = emitter_params.end_size;
				emitter_cbuffer_data.Mass = emitter_params.mass;
				emitter_cbuffer_data.MaxParticlesThisFrame = emitter_params.number_to_emit;
				emitter_cbuffer_data.ParticleLifeSpan = emitter_params.particle_lifespan;
				emitter_cbuffer_data.PositionVariance = emitter_params.position_variance;
				emitter_cbuffer_data.VelocityVariance = emitter_params.velocity_variance;
				emitter_cbuffer_data.Collisions = emitter_params.collisions_enabled;
				emitter_cbuffer_data.CollisionThickness = emitter_params.collision_thickness;

				emitter_allocation = upload_buffer->Allocate(GetCBufferSize<EmitterCBuffer>(), D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
				emitter_allocation.Update(emitter_cbuffer_data);

				cmd_list->SetComputeRootSignature(particle_rs_map[ERootSignature::Particles_Emit].Get());
				cmd_list->SetPipelineState(particle_pso_map[EPipelineStateObject::Particles_Emit].Get());

				OffsetType descriptor_index = descriptor_allocator->AllocateRange(3);
				device->CopyDescriptorsSimple(1, descriptor_allocator->GetCpuHandle(descriptor_index), particle_bufferA.UAV(),
					D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
				device->CopyDescriptorsSimple(1, descriptor_allocator->GetCpuHandle(descriptor_index + 1), particle_bufferB.UAV(),
					D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
				device->CopyDescriptorsSimple(1, descriptor_allocator->GetCpuHandle(descriptor_index + 2), dead_list_buffer.UAV(),
					D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
				cmd_list->SetComputeRootDescriptorTable(0, descriptor_allocator->GetGpuHandle(descriptor_index));

				descriptor_index = descriptor_allocator->Allocate();
				device->CopyDescriptorsSimple(1, descriptor_allocator->GetCpuHandle(descriptor_index), random_texture.SRV(),
					D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
				cmd_list->SetComputeRootDescriptorTable(1, descriptor_allocator->GetGpuHandle(descriptor_index));

				D3D12_RESOURCE_BARRIER barriers[] =
				{
					CD3DX12_RESOURCE_BARRIER::Transition(dead_list_buffer.CounterBuffer(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER)
				};
				cmd_list->ResourceBarrier(ARRAYSIZE(barriers), barriers);
				cmd_list->SetComputeRootConstantBufferView(2, dead_list_buffer.CounterBuffer()->GetGPUVirtualAddress());
				cmd_list->SetComputeRootConstantBufferView(3, emitter_allocation.gpu_address);

				U32 thread_groups_x = (UINT)std::ceil(emitter_params.number_to_emit * 1.0f / 1024);
				cmd_list->Dispatch(thread_groups_x, 1, 1);

				barriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(dead_list_buffer.CounterBuffer(), D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
				cmd_list->ResourceBarrier(ARRAYSIZE(barriers), barriers);
			}
		}
		void Simulate(ID3D12GraphicsCommandList* cmd_list, D3D12_CPU_DESCRIPTOR_HANDLE depth_srv)
		{
			PIXScopedEvent(cmd_list, PIX_COLOR_DEFAULT, "Particles Simulate Pass");

			ID3D12Device* device = gfx->GetDevice();
			LinearDescriptorAllocator* descriptor_allocator = gfx->GetDescriptorAllocator();

			cmd_list->SetComputeRootSignature(particle_rs_map[ERootSignature::Particles_Simulate].Get());
			cmd_list->SetPipelineState(particle_pso_map[EPipelineStateObject::Particles_Simulate].Get());

			//reset index buffer counter
			D3D12_RESOURCE_BARRIER prereset_barrier = CD3DX12_RESOURCE_BARRIER::Transition(alive_index_buffer.CounterBuffer(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST);
			cmd_list->ResourceBarrier(1, &prereset_barrier);
			cmd_list->CopyBufferRegion(alive_index_buffer.CounterBuffer(), 0, counter_reset_buffer.Get(), 0, sizeof(U32));
			D3D12_RESOURCE_BARRIER postreset_barrier = CD3DX12_RESOURCE_BARRIER::Transition(alive_index_buffer.CounterBuffer(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			cmd_list->ResourceBarrier(1, &postreset_barrier);

			//add barriers?
			OffsetType descriptor_index = descriptor_allocator->AllocateRange(6);
			D3D12_CPU_DESCRIPTOR_HANDLE src_ranges[] = { particle_bufferA.UAV(), particle_bufferB.UAV(),
														 dead_list_buffer.UAV(), alive_index_buffer.UAV(),
														 view_space_positions_buffer.UAV(), indirect_render_args_uav };
			D3D12_CPU_DESCRIPTOR_HANDLE dst_ranges[] = { descriptor_allocator->GetCpuHandle(descriptor_index) };
			U32 src_range_sizes[] = { 1, 1, 1, 1, 1, 1 };
			U32 dst_range_sizes[] = { 6 };
			device->CopyDescriptors(ARRAYSIZE(dst_ranges), dst_ranges, dst_range_sizes, ARRAYSIZE(src_ranges), src_ranges, src_range_sizes,
				D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			cmd_list->SetComputeRootDescriptorTable(0, descriptor_allocator->GetGpuHandle(descriptor_index));

			descriptor_index = descriptor_allocator->Allocate();
			device->CopyDescriptorsSimple(1, descriptor_allocator->GetCpuHandle(descriptor_index), depth_srv,
				D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			cmd_list->SetComputeRootDescriptorTable(1, descriptor_allocator->GetGpuHandle(descriptor_index));
			cmd_list->SetComputeRootConstantBufferView(2, frame_cbuffer_address);
			cmd_list->SetComputeRootConstantBufferView(3, compute_cbuffer_address);
			cmd_list->SetComputeRootConstantBufferView(4, emitter_allocation.gpu_address);

			U32 thread_groups_x = (UINT)std::ceil(MAX_PARTICLES * 1.0f / 256);
			cmd_list->Dispatch(thread_groups_x, 1, 1);
		}
		void Rasterize(ID3D12GraphicsCommandList* cmd_list, Emitter const& emitter_params,
			D3D12_CPU_DESCRIPTOR_HANDLE depth_srv, D3D12_CPU_DESCRIPTOR_HANDLE particle_srv)
		{
			PIXScopedEvent(cmd_list, PIX_COLOR_DEFAULT, "Particles Rasterize Pass");

			ID3D12Device* device = gfx->GetDevice();
			LinearDescriptorAllocator* descriptor_allocator = gfx->GetDescriptorAllocator();

			cmd_list->SetGraphicsRootSignature(particle_rs_map[ERootSignature::Particles_Shading].Get());
			cmd_list->SetPipelineState(particle_pso_map[EPipelineStateObject::Particles_Shading].Get());

			//add barriers?
			OffsetType descriptor_index = descriptor_allocator->AllocateRange(3);
			D3D12_CPU_DESCRIPTOR_HANDLE src_ranges1[] = { particle_bufferA.SRV(), view_space_positions_buffer.SRV(), alive_index_buffer.SRV() };
			D3D12_CPU_DESCRIPTOR_HANDLE dst_ranges1[] = { descriptor_allocator->GetCpuHandle(descriptor_index) };
			U32 src_range_sizes1[] = { 1, 1, 1 };
			U32 dst_range_sizes1[] = { 3 };
			device->CopyDescriptors(ARRAYSIZE(dst_ranges1), dst_ranges1, dst_range_sizes1, ARRAYSIZE(src_ranges1), src_ranges1, src_range_sizes1,
				D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			cmd_list->SetGraphicsRootDescriptorTable(0, descriptor_allocator->GetGpuHandle(descriptor_index));

			descriptor_index = descriptor_allocator->AllocateRange(2);
			D3D12_CPU_DESCRIPTOR_HANDLE src_ranges2[] = { particle_srv, depth_srv };
			D3D12_CPU_DESCRIPTOR_HANDLE dst_ranges2[] = { descriptor_allocator->GetCpuHandle(descriptor_index) };
			U32 src_range_sizes2[] = { 1, 1 };
			U32 dst_range_sizes2[] = { 2 };
			device->CopyDescriptors(ARRAYSIZE(dst_ranges2), dst_ranges2, dst_range_sizes2, ARRAYSIZE(src_ranges2), src_ranges2, src_range_sizes2,
				D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			cmd_list->SetGraphicsRootDescriptorTable(1, descriptor_allocator->GetGpuHandle(descriptor_index));

			D3D12_RESOURCE_BARRIER barriers[] =
			{
				CD3DX12_RESOURCE_BARRIER::Transition(alive_index_buffer.CounterBuffer(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER),
				CD3DX12_RESOURCE_BARRIER::Transition(indirect_render_args_buffer.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT)
			};
			cmd_list->ResourceBarrier(ARRAYSIZE(barriers), barriers);

			cmd_list->SetGraphicsRootConstantBufferView(2, alive_index_buffer.CounterBuffer()->GetGPUVirtualAddress());
			cmd_list->SetGraphicsRootConstantBufferView(3, frame_cbuffer_address);

			cmd_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			cmd_list->IASetVertexBuffers(0, 0, nullptr);
			index_buffer->Bind(cmd_list);
			cmd_list->ExecuteIndirect(indirect_render_args_signature.Get(), 1, indirect_render_args_buffer.Get(), 0, nullptr, 0);

			barriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(alive_index_buffer.CounterBuffer(), D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			barriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(indirect_render_args_buffer.Get(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			cmd_list->ResourceBarrier(ARRAYSIZE(barriers), barriers);
		}
		
		void Sort(ID3D12GraphicsCommandList* cmd_list)
		{
			PIXScopedEvent(cmd_list, PIX_COLOR_DEFAULT, "Particles Sort Pass");

			ID3D12Device* device = gfx->GetDevice();
			LinearDescriptorAllocator* descriptor_allocator = gfx->GetDescriptorAllocator();
			LinearUploadBuffer* upload_buffer = gfx->GetUploadBuffer();

			D3D12_RESOURCE_BARRIER barriers[] =
			{
				CD3DX12_RESOURCE_BARRIER::Transition(alive_index_buffer.CounterBuffer(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER)
			};
			cmd_list->ResourceBarrier(ARRAYSIZE(barriers), barriers);

			cmd_list->SetComputeRootSignature(particle_rs_map[ERootSignature::Particles_InitSortDispatchArgs].Get());
			cmd_list->SetPipelineState(particle_pso_map[EPipelineStateObject::Particles_InitSortDispatchArgs].Get());

			OffsetType descriptor_index = descriptor_allocator->Allocate();
			device->CopyDescriptorsSimple(1, descriptor_allocator->GetCpuHandle(descriptor_index), indirect_sort_args_uav,
				D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			cmd_list->SetComputeRootDescriptorTable(0, descriptor_allocator->GetGpuHandle(descriptor_index));
			cmd_list->SetComputeRootConstantBufferView(1, alive_index_buffer.CounterBuffer()->GetGPUVirtualAddress());
			cmd_list->Dispatch(1, 1, 1);

			descriptor_index = descriptor_allocator->Allocate();
			device->CopyDescriptorsSimple(1, descriptor_allocator->GetCpuHandle(descriptor_index), alive_index_buffer.UAV(),
				D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			sort_dispatch_info_allocation = upload_buffer->Allocate(GetCBufferSize<SortDispatchInfo>(), D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
			sort_dispatch_info_allocation.Update(SortDispatchInfo{});
			cmd_list->SetComputeRootSignature(particle_rs_map[ERootSignature::Particles_Sort].Get());
			cmd_list->SetComputeRootDescriptorTable(0, descriptor_allocator->GetGpuHandle(descriptor_index));
			cmd_list->SetComputeRootConstantBufferView(1, alive_index_buffer.CounterBuffer()->GetGPUVirtualAddress());
			cmd_list->SetComputeRootConstantBufferView(2, sort_dispatch_info_allocation.gpu_address);

			bool done = SortInitial(cmd_list);
			U32 presorted = 512;
			while (!done)
			{
				done = SortIncremental(cmd_list, presorted);
				presorted *= 2;
			}

			D3D12_RESOURCE_BARRIER barriers2[] =
			{
				CD3DX12_RESOURCE_BARRIER::Transition(alive_index_buffer.CounterBuffer(), D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER, D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
			};
			cmd_list->ResourceBarrier(ARRAYSIZE(barriers2), barriers2);
		}
		bool SortInitial(ID3D12GraphicsCommandList* cmd_list)
		{
			bool done = true;
			UINT num_thread_groups = ((MAX_PARTICLES - 1) >> 9) + 1;
			if (num_thread_groups > 1) done = false;

			D3D12_RESOURCE_BARRIER barriers[] =
			{
				CD3DX12_RESOURCE_BARRIER::Transition(indirect_sort_args_buffer.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT)
			};
			cmd_list->ResourceBarrier(ARRAYSIZE(barriers), barriers);
			cmd_list->SetPipelineState(particle_pso_map[EPipelineStateObject::Particles_Sort512].Get());
			cmd_list->ExecuteIndirect(indirect_sort_args_signature.Get(), 1, indirect_sort_args_buffer.Get(), 0, nullptr, 0);

			barriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(indirect_sort_args_buffer.Get(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			cmd_list->ResourceBarrier(ARRAYSIZE(barriers), barriers);

			D3D12_RESOURCE_BARRIER uav_barrier = CD3DX12_RESOURCE_BARRIER::UAV(alive_index_buffer.Buffer());
			cmd_list->ResourceBarrier(1, &uav_barrier);

			return done;
		}
		bool SortIncremental(ID3D12GraphicsCommandList* cmd_list, U32 presorted)
		{
			ID3D12Device* device = gfx->GetDevice();
			LinearDescriptorAllocator* descriptor_allocator = gfx->GetDescriptorAllocator();
			LinearUploadBuffer* upload_buffer = gfx->GetUploadBuffer();

			bool done = true;
			cmd_list->SetPipelineState(particle_pso_map[EPipelineStateObject::Particles_BitonicSortStep].Get());
			UINT num_thread_groups = 0;
			if (MAX_PARTICLES > presorted)
			{
				if (MAX_PARTICLES > presorted * 2) done = false;
				UINT pow2 = presorted;
				while (pow2 < MAX_PARTICLES) pow2 *= 2;
				num_thread_groups = pow2 >> 9;
			}
			
			U32 merge_size = presorted * 2;
			for (U32 merge_subsize = merge_size >> 1; merge_subsize > 256; merge_subsize = merge_subsize >> 1)
			{
				SortDispatchInfo sort_dispatch_info{};
				sort_dispatch_info.x = merge_subsize;
				if (merge_subsize == merge_size >> 1)
				{
					sort_dispatch_info.y = (2 * merge_subsize - 1);
					sort_dispatch_info.z = -1;
				}
				else
				{
					sort_dispatch_info.y = merge_subsize;
					sort_dispatch_info.z = 1;
				}
				sort_dispatch_info.w = 0;

				sort_dispatch_info_allocation = upload_buffer->Allocate(GetCBufferSize<SortDispatchInfo>(), D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
				sort_dispatch_info_allocation.Update(sort_dispatch_info);
				cmd_list->SetComputeRootConstantBufferView(2, sort_dispatch_info_allocation.gpu_address);
				cmd_list->Dispatch(num_thread_groups, 1, 1);

				D3D12_RESOURCE_BARRIER uav_barrier = CD3DX12_RESOURCE_BARRIER::UAV(alive_index_buffer.Buffer());
				cmd_list->ResourceBarrier(1, &uav_barrier);
			}
			cmd_list->SetPipelineState(particle_pso_map[EPipelineStateObject::Particles_SortInner512].Get());
			cmd_list->Dispatch(num_thread_groups, 1, 1);
			return done;
		}
	};
}
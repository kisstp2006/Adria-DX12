#include "GPUDrivenRenderer.h"
#include "ShaderStructs.h"
#include "Components.h"
#include "BlackboardData.h"
#include "PSOCache.h"
#include "RenderGraph/RenderGraph.h"
#include "entt/entity/registry.hpp"
#include "Logging/Logger.h"
#include "Core/Defines.h"

#define A_CPU 1
#include "Resources/NewShaders/SPD/ffx_a.h"
#include "Resources/NewShaders/SPD/ffx_spd.h"

namespace adria
{
	static constexpr uint32 MAX_NUM_MESHLETS = 1 << 20u;
	static constexpr uint32 MAX_NUM_INSTANCES = 1 << 14u;
	struct MeshletCandidate
	{
		uint32 instance_id;
		uint32 meshlet_index;
	};

	GPUDrivenRenderer::GPUDrivenRenderer(entt::registry& reg, GfxDevice* gfx, uint32 width, uint32 height) : reg(reg), gfx(gfx), width(width), height(height)
	{
		InitializeHZB();
	}

	void GPUDrivenRenderer::Render(RenderGraph& rg)
	{
		rg.ImportTexture(RG_RES_NAME(HZB), HZB.get());
		AddClearCountersPass(rg);
		Add1stPhasePasses(rg);
		Add2ndPhasePasses(rg);
	}

	void GPUDrivenRenderer::InitializeHZB()
	{
		CalculateHZBParameters();

		GfxTextureDesc hzb_desc{};
		hzb_desc.width = hzb_width;
		hzb_desc.height = hzb_height;
		hzb_desc.mip_levels = hzb_mip_count;
		hzb_desc.format = GfxFormat::R16_FLOAT;
		hzb_desc.initial_state = GfxResourceState::NonPixelShaderResource;
		hzb_desc.bind_flags = GfxBindFlag::ShaderResource | GfxBindFlag::UnorderedAccess;

		HZB = std::make_unique<GfxTexture>(gfx, hzb_desc);
	}

	void GPUDrivenRenderer::AddClearCountersPass(RenderGraph& rg)
	{
		struct ClearCountersPassData
		{
			RGBufferReadWriteId candidate_meshlets_counter;
			RGBufferReadWriteId visible_meshlets_counter;
			RGBufferReadWriteId occluded_instances_counter;
		};

		rg.AddPass<ClearCountersPassData>("Clear Counters Pass",
			[=](ClearCountersPassData& data, RenderGraphBuilder& builder)
			{
				RGBufferDesc counter_desc{};
				counter_desc.size = 3 * sizeof(uint32);
				counter_desc.format = GfxFormat::R32_UINT;
				counter_desc.stride = sizeof(uint32);
				builder.DeclareBuffer(RG_RES_NAME(CandidateMeshletsCounter), counter_desc);
				data.candidate_meshlets_counter = builder.WriteBuffer(RG_RES_NAME(CandidateMeshletsCounter));

				counter_desc.size = 2 * sizeof(uint32);
				builder.DeclareBuffer(RG_RES_NAME(VisibleMeshletsCounter), counter_desc);
				data.visible_meshlets_counter = builder.WriteBuffer(RG_RES_NAME(VisibleMeshletsCounter));

				counter_desc.size = sizeof(uint32);
				builder.DeclareBuffer(RG_RES_NAME(OccludedInstancesCounter), counter_desc);
				data.occluded_instances_counter = builder.WriteBuffer(RG_RES_NAME(OccludedInstancesCounter));
			},
			[=](ClearCountersPassData const& data, RenderGraphContext& ctx, GfxCommandList* cmd_list)
			{
				GfxDevice* gfx = cmd_list->GetDevice();

				GfxDescriptor dst_handle = gfx->AllocateDescriptorsGPU(3);
				GfxDescriptor src_handles[] = { ctx.GetReadWriteBuffer(data.candidate_meshlets_counter),
												ctx.GetReadWriteBuffer(data.visible_meshlets_counter),
												ctx.GetReadWriteBuffer(data.occluded_instances_counter) };
				gfx->CopyDescriptors(dst_handle, src_handles);
				uint32 i = dst_handle.GetIndex();

				struct ClearCountersConstants
				{
					uint32 candidate_meshlets_counter_idx;
					uint32 visible_meshlets_counter_idx;
					uint32 occluded_instances_counter_idx;
				} constants =
				{
					.candidate_meshlets_counter_idx = i,
					.visible_meshlets_counter_idx = i + 1,
					.occluded_instances_counter_idx = i + 2
				};
				cmd_list->SetPipelineState(PSOCache::Get(GfxPipelineStateID::ClearCounters));
				cmd_list->SetRootConstants(1, constants);
				cmd_list->Dispatch(1, 1, 1);
				cmd_list->UavBarrier();
			}, RGPassType::Compute, RGPassFlags::None);
	}

	void GPUDrivenRenderer::Add1stPhasePasses(RenderGraph& rg)
	{
		FrameBlackboardData const& global_data = rg.GetBlackboard().GetChecked<FrameBlackboardData>();

		struct CullInstancesPassData
		{
			RGTextureReadOnlyId hzb;
			RGBufferReadWriteId candidate_meshlets;
			RGBufferReadWriteId candidate_meshlets_counter;
			RGBufferReadWriteId occluded_instances;
			RGBufferReadWriteId occluded_instances_counter;
		};

		rg.AddPass<CullInstancesPassData>("1st Phase Cull Instances Pass",
			[=](CullInstancesPassData& data, RenderGraphBuilder& builder)
			{
				RGBufferDesc candidate_meshlets_buffer_desc{};
				candidate_meshlets_buffer_desc.resource_usage = GfxResourceUsage::Default;
				candidate_meshlets_buffer_desc.misc_flags = GfxBufferMiscFlag::BufferStructured;
				candidate_meshlets_buffer_desc.stride = sizeof(MeshletCandidate);
				candidate_meshlets_buffer_desc.size = sizeof(MeshletCandidate) * MAX_NUM_MESHLETS;
				builder.DeclareBuffer(RG_RES_NAME(CandidateMeshlets), candidate_meshlets_buffer_desc);

				RGBufferDesc occluded_instances_buffer_desc{};
				occluded_instances_buffer_desc.resource_usage = GfxResourceUsage::Default;
				occluded_instances_buffer_desc.misc_flags = GfxBufferMiscFlag::BufferStructured;
				occluded_instances_buffer_desc.stride = sizeof(uint32);
				occluded_instances_buffer_desc.size = sizeof(uint32) * MAX_NUM_INSTANCES;
				builder.DeclareBuffer(RG_RES_NAME(OccludedInstances), occluded_instances_buffer_desc);

				data.hzb = builder.ReadTexture(RG_RES_NAME(HZB));
				data.occluded_instances = builder.WriteBuffer(RG_RES_NAME(OccludedInstances));
				data.occluded_instances_counter = builder.WriteBuffer(RG_RES_NAME(OccludedInstancesCounter));
				data.candidate_meshlets = builder.WriteBuffer(RG_RES_NAME(CandidateMeshlets));
				data.candidate_meshlets_counter = builder.WriteBuffer(RG_RES_NAME(CandidateMeshletsCounter));
			},
			[=](CullInstancesPassData const& data, RenderGraphContext& ctx, GfxCommandList* cmd_list)
			{
				GfxDevice* gfx = cmd_list->GetDevice();

				GfxDescriptor src_handles[] = { ctx.GetReadOnlyTexture(data.hzb),
												ctx.GetReadWriteBuffer(data.occluded_instances),
												ctx.GetReadWriteBuffer(data.occluded_instances_counter),
												ctx.GetReadWriteBuffer(data.candidate_meshlets),
												ctx.GetReadWriteBuffer(data.candidate_meshlets_counter) };
				GfxDescriptor dst_handle = gfx->AllocateDescriptorsGPU(ARRAYSIZE(src_handles));
				gfx->CopyDescriptors(dst_handle, src_handles);
				uint32 i = dst_handle.GetIndex();

				uint32 const num_instances = (uint32)reg.view<Batch>().size();
				struct CullInstances1stPhaseConstants
				{
					uint32 num_instances;
					uint32 hzb_idx;
					uint32 occluded_instances_idx;
					uint32 occluded_instances_counter_idx;
					uint32 candidate_meshlets_idx;
					uint32 candidate_meshlets_counter_idx;
				} constants =
				{
					.num_instances = num_instances,
					.hzb_idx = i,
					.occluded_instances_idx = i + 1,
					.occluded_instances_counter_idx = i + 2,
					.candidate_meshlets_idx = i + 3,
					.candidate_meshlets_counter_idx = i + 4,
				};
				cmd_list->SetPipelineState(PSOCache::Get(GfxPipelineStateID::CullInstances1stPhase));
				cmd_list->SetRootCBV(0, global_data.frame_cbuffer_address);
				cmd_list->SetRootConstants(1, constants);
				cmd_list->Dispatch((uint32)std::ceil(num_instances / 64.0f), 1, 1);

			}, RGPassType::Compute, RGPassFlags::None);

		struct BuildMeshletCullArgsPassData
		{
			RGBufferReadOnlyId  candidate_meshlets_counter;
			RGBufferReadWriteId meshlet_cull_args;
		};

		rg.AddPass<BuildMeshletCullArgsPassData>("1st Phase Build Meshlet Cull Args Pass",
			[=](BuildMeshletCullArgsPassData& data, RenderGraphBuilder& builder)
			{
				RGBufferDesc meshlet_cull_args_desc{};
				meshlet_cull_args_desc.resource_usage = GfxResourceUsage::Default;
				meshlet_cull_args_desc.misc_flags = GfxBufferMiscFlag::IndirectArgs;
				meshlet_cull_args_desc.stride = sizeof(D3D12_DISPATCH_ARGUMENTS);
				meshlet_cull_args_desc.size = sizeof(D3D12_DISPATCH_ARGUMENTS);
				builder.DeclareBuffer(RG_RES_NAME(MeshletCullArgs), meshlet_cull_args_desc);

				data.meshlet_cull_args = builder.WriteBuffer(RG_RES_NAME(MeshletCullArgs));
				data.candidate_meshlets_counter = builder.ReadBuffer(RG_RES_NAME(CandidateMeshletsCounter));
			},
			[=](BuildMeshletCullArgsPassData const& data, RenderGraphContext& ctx, GfxCommandList* cmd_list)
			{
				GfxDevice* gfx = cmd_list->GetDevice();

				GfxDescriptor src_handles[] = { ctx.GetReadOnlyBuffer(data.candidate_meshlets_counter),
												ctx.GetReadWriteBuffer(data.meshlet_cull_args)
				};
				GfxDescriptor dst_handle = gfx->AllocateDescriptorsGPU(ARRAYSIZE(src_handles));
				gfx->CopyDescriptors(dst_handle, src_handles);
				uint32 i = dst_handle.GetIndex();

				struct BuildMeshletCullArgsConstants
				{
					uint32 candidate_meshlets_counter_idx;
					uint32 meshlet_cull_args_idx;
				} constants =
				{
					.candidate_meshlets_counter_idx = i + 0,
					.meshlet_cull_args_idx = i + 1
				};
				cmd_list->SetPipelineState(PSOCache::Get(GfxPipelineStateID::BuildMeshletCullArgs1stPhase));
				cmd_list->SetRootConstants(1, constants);
				cmd_list->Dispatch(1, 1, 1);

			}, RGPassType::Compute, RGPassFlags::None);


		struct CullMeshletsPassData
		{
			RGTextureReadOnlyId hzb;
			RGBufferIndirectArgsId indirect_args;
			RGBufferReadWriteId candidate_meshlets;
			RGBufferReadWriteId candidate_meshlets_counter;
			RGBufferReadWriteId visible_meshlets;
			RGBufferReadWriteId visible_meshlets_counter;
		};

		rg.AddPass<CullMeshletsPassData>("1st Phase Cull Meshlets Pass",
			[=](CullMeshletsPassData& data, RenderGraphBuilder& builder)
			{
				RGBufferDesc visible_meshlets_buffer_desc{};
				visible_meshlets_buffer_desc.resource_usage = GfxResourceUsage::Default;
				visible_meshlets_buffer_desc.misc_flags = GfxBufferMiscFlag::BufferStructured;
				visible_meshlets_buffer_desc.stride = sizeof(MeshletCandidate);
				visible_meshlets_buffer_desc.size = sizeof(MeshletCandidate) * MAX_NUM_MESHLETS;
				builder.DeclareBuffer(RG_RES_NAME(VisibleMeshlets), visible_meshlets_buffer_desc);

				data.hzb = builder.ReadTexture(RG_RES_NAME(HZB));
				data.indirect_args = builder.ReadIndirectArgsBuffer(RG_RES_NAME(MeshletCullArgs));
				data.candidate_meshlets = builder.WriteBuffer(RG_RES_NAME(CandidateMeshlets));
				data.candidate_meshlets_counter = builder.WriteBuffer(RG_RES_NAME(CandidateMeshletsCounter));
				data.visible_meshlets = builder.WriteBuffer(RG_RES_NAME(VisibleMeshlets));
				data.visible_meshlets_counter = builder.WriteBuffer(RG_RES_NAME(VisibleMeshletsCounter));
			},
			[=](CullMeshletsPassData const& data, RenderGraphContext& ctx, GfxCommandList* cmd_list)
			{
				GfxDevice* gfx = cmd_list->GetDevice();

				GfxDescriptor src_handles[] = { ctx.GetReadOnlyTexture(data.hzb),
												ctx.GetReadWriteBuffer(data.candidate_meshlets),
												ctx.GetReadWriteBuffer(data.candidate_meshlets_counter),
												ctx.GetReadWriteBuffer(data.visible_meshlets),
												ctx.GetReadWriteBuffer(data.visible_meshlets_counter)};
				GfxDescriptor dst_handle = gfx->AllocateDescriptorsGPU(ARRAYSIZE(src_handles));
				gfx->CopyDescriptors(dst_handle, src_handles);
				uint32 i = dst_handle.GetIndex();

				struct CullMeshlets1stPhaseConstants
				{
					uint32 hzb_idx;
					uint32 candidate_meshlets_idx;
					uint32 candidate_meshlets_counter_idx;
					uint32 visible_meshlets_idx;
					uint32 visible_meshlets_counter_idx;
				} constants =
				{
					.hzb_idx = i,
					.candidate_meshlets_idx = i + 1,
					.candidate_meshlets_counter_idx = i + 2,
					.visible_meshlets_idx = i + 3,
					.visible_meshlets_counter_idx = i + 4,
				};
				cmd_list->SetPipelineState(PSOCache::Get(GfxPipelineStateID::CullMeshlets1stPhase));
				cmd_list->SetRootCBV(0, global_data.frame_cbuffer_address);
				cmd_list->SetRootConstants(1, constants);

				GfxBuffer const& indirect_args = ctx.GetIndirectArgsBuffer(data.indirect_args);
				cmd_list->DispatchIndirect(indirect_args, 0);
			}, RGPassType::Compute, RGPassFlags::None);

		struct BuildMeshletDrawArgsPassData
		{
			RGBufferReadOnlyId  visible_meshlets_counter;
			RGBufferReadWriteId meshlet_draw_args;
		};

		rg.AddPass<BuildMeshletDrawArgsPassData>("1st Phase Build Meshlet Draw Args Pass",
			[=](BuildMeshletDrawArgsPassData& data, RenderGraphBuilder& builder)
			{
				RGBufferDesc meshlet_cull_draw_desc{};
				meshlet_cull_draw_desc.resource_usage = GfxResourceUsage::Default;
				meshlet_cull_draw_desc.misc_flags = GfxBufferMiscFlag::IndirectArgs;
				meshlet_cull_draw_desc.stride = sizeof(D3D12_DISPATCH_MESH_ARGUMENTS);
				meshlet_cull_draw_desc.size = sizeof(D3D12_DISPATCH_MESH_ARGUMENTS);
				builder.DeclareBuffer(RG_RES_NAME(MeshletDrawArgs), meshlet_cull_draw_desc);

				data.meshlet_draw_args = builder.WriteBuffer(RG_RES_NAME(MeshletDrawArgs));
				data.visible_meshlets_counter = builder.ReadBuffer(RG_RES_NAME(VisibleMeshletsCounter));
			},
			[=](BuildMeshletDrawArgsPassData const& data, RenderGraphContext& ctx, GfxCommandList* cmd_list)
			{
				GfxDevice* gfx = cmd_list->GetDevice();

				GfxDescriptor src_handles[] = { ctx.GetReadOnlyBuffer(data.visible_meshlets_counter),
												ctx.GetReadWriteBuffer(data.meshlet_draw_args)
				};
				GfxDescriptor dst_handle = gfx->AllocateDescriptorsGPU(ARRAYSIZE(src_handles));
				gfx->CopyDescriptors(dst_handle, src_handles);
				uint32 i = dst_handle.GetIndex();

				struct BuildMeshletDrawArgsConstants
				{
					uint32 visible_meshlets_counter_idx;
					uint32 meshlet_draw_args_idx;
				} constants =
				{
					.visible_meshlets_counter_idx = i + 0,
					.meshlet_draw_args_idx = i + 1
				};
				cmd_list->SetPipelineState(PSOCache::Get(GfxPipelineStateID::BuildMeshletDrawArgs1stPhase));
				cmd_list->SetRootConstants(1, constants);
				cmd_list->Dispatch(1, 1, 1);

			}, RGPassType::Compute, RGPassFlags::None);

		struct DrawMeshletsPassData
		{
			RGBufferReadOnlyId visible_meshlets;
			RGBufferIndirectArgsId draw_args;
		};
		rg.AddPass<DrawMeshletsPassData>("Draw Meshlets",
			[=](DrawMeshletsPassData& data, RenderGraphBuilder& builder)
			{
				RGTextureDesc gbuffer_desc{};
				gbuffer_desc.width = width;
				gbuffer_desc.height = height;
				gbuffer_desc.format = GfxFormat::R8G8B8A8_UNORM;
				gbuffer_desc.clear_value = GfxClearValue(0.0f, 0.0f, 0.0f, 0.0f);

				builder.DeclareTexture(RG_RES_NAME(GBufferNormal), gbuffer_desc);
				builder.DeclareTexture(RG_RES_NAME(GBufferAlbedo), gbuffer_desc);
				builder.DeclareTexture(RG_RES_NAME(GBufferEmissive), gbuffer_desc);

				builder.WriteRenderTarget(RG_RES_NAME(GBufferNormal), RGLoadStoreAccessOp::Clear_Preserve);
				builder.WriteRenderTarget(RG_RES_NAME(GBufferAlbedo), RGLoadStoreAccessOp::Clear_Preserve);
				builder.WriteRenderTarget(RG_RES_NAME(GBufferEmissive), RGLoadStoreAccessOp::Clear_Preserve);

				RGTextureDesc depth_desc{};
				depth_desc.width = width;
				depth_desc.height = height;
				depth_desc.format = GfxFormat::R32_TYPELESS;
				depth_desc.clear_value = GfxClearValue(1.0f, 0);
				builder.DeclareTexture(RG_RES_NAME(DepthStencil), depth_desc);
				builder.WriteDepthStencil(RG_RES_NAME(DepthStencil), RGLoadStoreAccessOp::Clear_Preserve);
				builder.SetViewport(width, height);

				data.visible_meshlets = builder.ReadBuffer(RG_RES_NAME(VisibleMeshlets));
				data.draw_args = builder.ReadIndirectArgsBuffer(RG_RES_NAME(MeshletDrawArgs));
			},
			[=](DrawMeshletsPassData const& data, RenderGraphContext& ctx, GfxCommandList* cmd_list)
			{
				GfxDevice* gfx = cmd_list->GetDevice();

				GfxDescriptor src_handles[] =
				{
					ctx.GetReadOnlyBuffer(data.visible_meshlets)
				};
				GfxDescriptor dst_handle = gfx->AllocateDescriptorsGPU(ARRAYSIZE(src_handles));
				gfx->CopyDescriptors(dst_handle, src_handles);
				uint32 i = dst_handle.GetIndex();

				struct DrawMeshlets1stPhaseConstants
				{
					uint32 visible_meshlets_idx;
				} constants =
				{
					.visible_meshlets_idx = i,
				};
				cmd_list->SetPipelineState(PSOCache::Get(GfxPipelineStateID::DrawMeshlets1stPhase));
				cmd_list->SetRootCBV(0, global_data.frame_cbuffer_address);
				cmd_list->SetRootConstants(1, constants);
				GfxBuffer const& draw_args = ctx.GetIndirectArgsBuffer(data.draw_args);
				cmd_list->DispatchMeshIndirect(draw_args, 0);
			}, RGPassType::Graphics, RGPassFlags::None);

		//AddBuildHZBPasses(rg);
	}

	void GPUDrivenRenderer::Add2ndPhasePasses(RenderGraph& rg)
	{
		//todo
	}

	void GPUDrivenRenderer::AddBuildHZBPasses(RenderGraph& rg)
	{
		struct InitializeHZBPassData
		{
			RGTextureReadOnlyId depth;
			RGTextureReadWriteId hzb;
		};
		rg.AddPass<InitializeHZBPassData>("1st Phase Initialize HZB",
			[=](InitializeHZBPassData& data, RenderGraphBuilder& builder)
			{
				data.hzb = builder.WriteTexture(RG_RES_NAME(HZB));
				data.depth = builder.ReadTexture(RG_RES_NAME(DepthStencil));
			},
			[=](InitializeHZBPassData const& data, RenderGraphContext& ctx, GfxCommandList* cmd_list)
			{
				GfxDevice* gfx = cmd_list->GetDevice();

				GfxDescriptor src_handles[] = {
					ctx.GetReadOnlyTexture(data.depth),
					ctx.GetReadWriteTexture(data.hzb)
				};
				GfxDescriptor dst_handle = gfx->AllocateDescriptorsGPU(ARRAYSIZE(src_handles));
				gfx->CopyDescriptors(dst_handle, src_handles);
				uint32 i = dst_handle.GetIndex();

				struct InitializeHZBConstants
				{
					uint32 depth_idx;
					uint32 hzb_idx;
					float inv_hzb_width;
					float inv_hzb_height;
				} constants =
				{
					.depth_idx = i,
					.hzb_idx = i + 1,
					.inv_hzb_width = 1.0f / hzb_width,
					.inv_hzb_height = 1.0f / hzb_height
				};
				cmd_list->SetPipelineState(PSOCache::Get(GfxPipelineStateID::InitializeHZB));
				cmd_list->SetRootConstants(1, constants);
				cmd_list->Dispatch((uint32)std::ceil(hzb_width / 16.0f), (uint32)std::ceil(hzb_width / 16.0f), 1);
			}, RGPassType::Compute, RGPassFlags::ForceNoCull);

		struct HZBMipsPassData
		{
			RGBufferReadWriteId  spd_counter;
			RGTextureReadWriteId hzb_mips[12];
		};
		rg.AddPass<HZBMipsPassData>("1st Phase HZB Mips",
			[=](HZBMipsPassData& data, RenderGraphBuilder& builder)
			{
				RGBufferDesc counter_desc{};
				counter_desc.size = sizeof(uint32);
				counter_desc.format = GfxFormat::R32_UINT;
				counter_desc.stride = sizeof(uint32);
				builder.DeclareBuffer(RG_RES_NAME(SPDCounter), counter_desc);

				ADRIA_ASSERT(hzb_mip_count <= 12);
				for (uint32 i = 0; i < hzb_mip_count; ++i)
				{
					data.hzb_mips[i] = builder.WriteTexture(RG_RES_NAME(HZB), i, 1);
				}
				data.spd_counter = builder.WriteBuffer(RG_RES_NAME(SPDCounter));
			},
			[=](HZBMipsPassData const& data, RenderGraphContext& ctx, GfxCommandList* cmd_list)
			{
				GfxDevice* gfx = cmd_list->GetDevice();

				varAU2(dispatchThreadGroupCountXY);
				varAU2(workGroupOffset);
				varAU2(numWorkGroupsAndMips);
				varAU4(rectInfo) = initAU4(0, 0, hzb_width, hzb_height);
				uint32 mips = hzb_mip_count;

				SpdSetup(
					dispatchThreadGroupCountXY,
					workGroupOffset,
					numWorkGroupsAndMips,
					rectInfo,
					mips - 1);

				std::vector<GfxDescriptor> src_handles(hzb_mip_count + 1);
				src_handles[0] = ctx.GetReadWriteBuffer(data.spd_counter);
				for (uint32 i = 0; i < hzb_mip_count; ++i) src_handles[i + 1] = ctx.GetReadWriteTexture(data.hzb_mips[i]);

				GfxDescriptor dst_handle = gfx->AllocateDescriptorsGPU((uint32)src_handles.size());
				gfx->CopyDescriptors(dst_handle, src_handles);
				uint32 i = dst_handle.GetIndex();

				GfxDescriptor counter_uav_cpu = src_handles[0];
				GfxDescriptor counter_uav_gpu = dst_handle;
				GfxBuffer& spd_counter = ctx.GetBuffer(*data.spd_counter);
				uint32 clear[] = { 0u };
				cmd_list->ClearUAV(spd_counter, counter_uav_gpu, counter_uav_cpu, clear);
				cmd_list->UavBarrier();

				struct HZBMipsConstants
				{
					uint32 num_mips;
					uint32 num_work_groups;
					uint32 work_group_offset_x;
					uint32 work_group_offset_y;
				} constants
				{
					.num_mips = numWorkGroupsAndMips[1],
					.num_work_groups = numWorkGroupsAndMips[0],
					.work_group_offset_x = workGroupOffset[0],
					.work_group_offset_y = workGroupOffset[1]
				};

				struct SPDIndices
				{
					uint32	dstIdx[12];
					uint32	spdGlobalAtomicIdx;
				} indices{ .spdGlobalAtomicIdx = i };
				for (uint32 j = 0; j < hzb_mip_count; ++j) indices.dstIdx[j] = i + 1 + j;

				cmd_list->SetPipelineState(PSOCache::Get(GfxPipelineStateID::HZBMips));
				cmd_list->SetRootConstants(1, constants);
				cmd_list->SetRootCBV(2, indices);
				cmd_list->Dispatch(dispatchThreadGroupCountXY[0], dispatchThreadGroupCountXY[1], 1);
			}, RGPassType::Compute, RGPassFlags::ForceNoCull);
	}

	void GPUDrivenRenderer::CalculateHZBParameters()
	{
		uint32 mips_x = (uint32)std::max(ceilf(log2f((float)width)), 1.0f);
		uint32 mips_y = (uint32)std::max(ceilf(log2f((float)height)), 1.0f);

		hzb_mip_count = std::max(mips_x, mips_y);
		ADRIA_ASSERT(hzb_mip_count <= MAX_HZB_MIP_COUNT);
		hzb_width = 1 << (mips_x - 1);
		hzb_height = 1 << (mips_y - 1);
	}
}

#include "ToneMapPass.h"
#include "BlackboardData.h"
#include "ShaderManager.h" 
#include "TextureManager.h"
#include "Core/Paths.h"
#include "Graphics/GfxDevice.h"
#include "Graphics/GfxPipelineState.h"
#include "Graphics/GfxCommon.h"
#include "Math/Packing.h"
#include "Editor/GUICommand.h"
#include "RenderGraph/RenderGraph.h"

namespace adria
{
	
	ToneMapPass::ToneMapPass(GfxDevice* gfx, uint32 w, uint32 h) : gfx(gfx), width(w), height(h)
	{
		CreatePSO();
	}

	void ToneMapPass::AddPass(RenderGraph& rg, RGResourceName hdr_src)
	{
		FrameBlackboardData const& frame_data = rg.GetBlackboard().Get<FrameBlackboardData>();
		BloomBlackboardData const* bloom_data  = rg.GetBlackboard().TryGet<BloomBlackboardData>();
		
		struct ToneMapPassData
		{
			RGTextureReadOnlyId  hdr_input;
			RGTextureReadOnlyId  exposure;
			RGTextureReadOnlyId  bloom;
			RGTextureReadWriteId output;
		};

		rg.AddPass<ToneMapPassData>("Tonemap Pass",
			[=](ToneMapPassData& data, RenderGraphBuilder& builder)
			{
				data.hdr_input = builder.ReadTexture(hdr_src, ReadAccess_NonPixelShader);

				if (builder.IsTextureDeclared(RG_RES_NAME(Exposure))) data.exposure = builder.ReadTexture(RG_RES_NAME(Exposure), ReadAccess_NonPixelShader);
				else data.exposure.Invalidate();

				if (builder.IsTextureDeclared(RG_RES_NAME(Bloom))) 
					data.bloom = builder.ReadTexture(RG_RES_NAME(Bloom), ReadAccess_NonPixelShader);
				else data.bloom.Invalidate();

				ADRIA_ASSERT(builder.IsTextureDeclared(RG_RES_NAME(FinalTexture)));
				data.output = builder.WriteTexture(RG_RES_NAME(FinalTexture));
			},
			[=](ToneMapPassData const& data, RenderGraphContext& ctx, GfxCommandList* cmd_list)
			{
				GfxDevice* gfx = cmd_list->GetDevice();

				GfxDescriptor src_descriptors[] =
				{
					ctx.GetReadOnlyTexture(data.hdr_input),
					data.exposure.IsValid() ? ctx.GetReadOnlyTexture(data.exposure) : gfxcommon::GetCommonView(GfxCommonViewType::WhiteTexture2D_SRV),
					ctx.GetReadWriteTexture(data.output)
				};
				GfxDescriptor dst_descriptor = gfx->AllocateDescriptorsGPU(ARRAYSIZE(src_descriptors) + 1);
				gfx->CopyDescriptors(dst_descriptor, src_descriptors);
				uint32 const i = dst_descriptor.GetIndex();

				bool const bloom_enabled = data.bloom.IsValid();
				if (bloom_enabled) gfx->CopyDescriptors(1, gfx->GetDescriptorGPU(i + ARRAYSIZE(src_descriptors)), ctx.GetReadOnlyTexture(data.bloom));

				struct TonemapConstants
				{
					float    tonemap_exposure;
					uint32   tonemap_operator_lut_packed;
					uint32   hdr_idx;
					uint32   exposure_idx;
					uint32   output_idx;
					int32    bloom_idx;
					uint32   lens_dirt_idx;
					uint32   bloom_params_packed;
				} constants = 
				{
					.tonemap_exposure = params.tonemap_exposure, .tonemap_operator_lut_packed = PackTwoUint16ToUint32((uint16)params.tone_map_op, (uint16)tony_mc_mapface_lut_handle),
					.hdr_idx = i, .exposure_idx = i + 1, .output_idx = i + 2, .bloom_idx = -1
				};
				if (bloom_enabled)
				{
					ADRIA_ASSERT(bloom_data != nullptr);
					constants.bloom_idx = i + ARRAYSIZE(src_descriptors);
					constants.lens_dirt_idx = (uint32)lens_dirt_handle;
					constants.bloom_params_packed = PackTwoFloatsToUint32(bloom_data->bloom_intensity, bloom_data->bloom_blend_factor);
				}
				
				cmd_list->SetPipelineState(tonemap_pso.get());
				cmd_list->SetRootCBV(0, frame_data.frame_cbuffer_address);
				cmd_list->SetRootConstants(1, constants);
				cmd_list->Dispatch(DivideAndRoundUp(width, 16), DivideAndRoundUp(height, 16), 1);
			}, RGPassType::Compute, RGPassFlags::None);

		GUI();
	}

	void ToneMapPass::AddPass(RenderGraph& rg, RGResourceName hdr_src, RGResourceName output)
	{
		FrameBlackboardData const& frame_data = rg.GetBlackboard().Get<FrameBlackboardData>();
		BloomBlackboardData const* bloom_data  = rg.GetBlackboard().TryGet<BloomBlackboardData>();

		RGPassFlags flags = RGPassFlags::None;

		struct ToneMapPassData
		{
			RGTextureReadOnlyId  hdr_input;
			RGTextureReadOnlyId  exposure;
			RGTextureReadOnlyId  bloom;
			RGTextureReadWriteId output;
		};

		rg.AddPass<ToneMapPassData>("Tonemap Pass",
			[=](ToneMapPassData& data, RenderGraphBuilder& builder)
			{
				RGTextureDesc fxaa_input_desc{};
				fxaa_input_desc.width = width;
				fxaa_input_desc.height = height;
				fxaa_input_desc.format = GfxFormat::R8G8B8A8_UNORM;
				builder.DeclareTexture(output, fxaa_input_desc);

				data.hdr_input = builder.ReadTexture(hdr_src, ReadAccess_NonPixelShader);

				if (builder.IsTextureDeclared(RG_RES_NAME(Exposure))) data.exposure = builder.ReadTexture(RG_RES_NAME(Exposure), ReadAccess_NonPixelShader);
				else data.exposure.Invalidate();

				if (builder.IsTextureDeclared(RG_RES_NAME(Bloom))) 
					data.bloom = builder.ReadTexture(RG_RES_NAME(Bloom), ReadAccess_NonPixelShader);
				else data.bloom.Invalidate();

				data.output = builder.WriteTexture(output);
				builder.SetViewport(width, height);
			},
			[=](ToneMapPassData const& data, RenderGraphContext& ctx, GfxCommandList* cmd_list)
			{
				GfxDevice* gfx = cmd_list->GetDevice();

				GfxDescriptor src_descriptors[] =
				{
					ctx.GetReadOnlyTexture(data.hdr_input),
					data.exposure.IsValid() ? ctx.GetReadOnlyTexture(data.exposure) : gfxcommon::GetCommonView(GfxCommonViewType::WhiteTexture2D_SRV),
					ctx.GetReadWriteTexture(data.output)
				};
				GfxDescriptor dst_descriptor = gfx->AllocateDescriptorsGPU(ARRAYSIZE(src_descriptors) + 1);
				gfx->CopyDescriptors(dst_descriptor, src_descriptors);
				uint32 const i = dst_descriptor.GetIndex();

				bool const bloom_enabled = data.bloom.IsValid();
				if (bloom_enabled) gfx->CopyDescriptors(1, gfx->GetDescriptorGPU(i + ARRAYSIZE(src_descriptors)), ctx.GetReadOnlyTexture(data.bloom));

				struct TonemapConstants
				{
					float    tonemap_exposure;
					uint32   tonemap_operator_lut_packed;
					uint32   hdr_idx;
					uint32   exposure_idx;
					uint32   output_idx;
					int32    bloom_idx;
					uint32   lens_dirt_idx;
					uint32   bloom_params_packed;
				} constants =
				{
					.tonemap_exposure = params.tonemap_exposure, .tonemap_operator_lut_packed = PackTwoUint16ToUint32((uint16)params.tone_map_op, (uint16)tony_mc_mapface_lut_handle),
					.hdr_idx = i, .exposure_idx = i + 1, .output_idx = i + 2, .bloom_idx = -1
				};
				if (bloom_enabled)
				{
					ADRIA_ASSERT(bloom_data != nullptr);
					constants.bloom_idx = i + ARRAYSIZE(src_descriptors);
					constants.lens_dirt_idx = (uint32)lens_dirt_handle;
					constants.bloom_params_packed = PackTwoFloatsToUint32(bloom_data->bloom_intensity, bloom_data->bloom_blend_factor);
				}
				
				cmd_list->SetPipelineState(tonemap_pso.get());
				cmd_list->SetRootCBV(0, frame_data.frame_cbuffer_address);
				cmd_list->SetRootConstants(1, constants);
				cmd_list->Dispatch(DivideAndRoundUp(width, 16), DivideAndRoundUp(height, 16), 1);
			}, RGPassType::Compute, flags);

		GUI();
	}

	void ToneMapPass::OnResize(uint32 w, uint32 h)
	{
		width = w, height = h;
	}

	void ToneMapPass::OnSceneInitialized()
	{
		lens_dirt_handle		   = g_TextureManager.LoadTexture(paths::TexturesDir + "LensDirt.dds");
		tony_mc_mapface_lut_handle = g_TextureManager.LoadTexture(paths::TexturesDir + "tony_mc_mapface.dds");
	}

	void ToneMapPass::CreatePSO()
	{
		GfxComputePipelineStateDesc compute_pso_desc{};
		compute_pso_desc.CS = CS_Tonemap;
		tonemap_pso = gfx->CreateComputePipelineState(compute_pso_desc);
	}

	void ToneMapPass::GUI()
	{
		GUI_Command([&]() 
			{
				if (ImGui::TreeNodeEx("Tone Mapping", 0))
				{
					ImGui::SliderFloat("Exposure", &params.tonemap_exposure, 0.01f, 10.0f);
					static char const* const operators[] = { "REINHARD", "HABLE", "LINEAR", "TONY MCMAPFACE"};
					static int tone_map_operator = static_cast<int>(params.tone_map_op);
					ImGui::ListBox("Tone Map Operator", &tone_map_operator, operators, IM_ARRAYSIZE(operators));
					params.tone_map_op = static_cast<ToneMap>(tone_map_operator);
					ImGui::TreePop();
					ImGui::Separator();
				}
			}, GUICommandGroup_PostProcessor);
	}
}
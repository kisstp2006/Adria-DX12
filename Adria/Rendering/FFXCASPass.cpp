#include "FFXCASPass.h"
#include "FidelityFXUtils.h"
#include "BlackboardData.h"
#include "Graphics/GfxDevice.h"
#include "RenderGraph/RenderGraph.h"
#include "Editor/GUICommand.h"
#include "Logging/Logger.h"
#include "Core/ConsoleVariable.h"


namespace adria
{
	namespace cvars
	{
		static ConsoleVariable cas_sharpness("ffx.cas.sharpness", 0.5f);
	}

	FFXCASPass::FFXCASPass(GfxDevice* gfx, uint32 w, uint32 h) : gfx(gfx), width(w), height(h), ffx_interface(nullptr)
	{
		if (!gfx->GetCapabilities().SupportsShaderModel(SM_6_6)) return;
		sprintf(name_version, "FFX CAS %d.%d.%d", FFX_CAS_VERSION_MAJOR, FFX_CAS_VERSION_MINOR, FFX_CAS_VERSION_PATCH);
		ffx_interface = CreateFfxInterface(gfx, FFX_CAS_CONTEXT_COUNT);
		cas_context_desc.backendInterface = *ffx_interface;
		CreateContext();
		ADRIA_CVAR_CALLBACK(cas_sharpness, (int v) { sharpness = v; });
	}

	FFXCASPass::~FFXCASPass()
	{
		DestroyContext();
		DestroyFfxInterface(ffx_interface);
	}

	RGResourceName FFXCASPass::AddPass(RenderGraph& rg, RGResourceName input)
	{
		struct FFXCASPassData
		{
			RGTextureReadOnlyId input;
			RGTextureReadWriteId output;
		};

		FrameBlackboardData const& frame_data = rg.GetBlackboard().Get<FrameBlackboardData>();

		rg.AddPass<FFXCASPassData>(name_version,
			[=](FFXCASPassData& data, RenderGraphBuilder& builder)
			{
				RGTextureDesc ffx_dof_desc = builder.GetTextureDesc(input);
				builder.DeclareTexture(RG_RES_NAME(FFXCASOutput), ffx_dof_desc);

				data.output = builder.WriteTexture(RG_RES_NAME(FFXCASOutput));
				data.input = builder.ReadTexture(input, ReadAccess_NonPixelShader);
			},
			[=](FFXCASPassData const& data, RenderGraphContext& ctx, GfxCommandList* cmd_list)
			{
				GfxTexture& input_texture = ctx.GetTexture(*data.input);
				GfxTexture& output_texture = ctx.GetTexture(*data.output);

				FfxCasDispatchDescription cas_dispatch_desc{};
				cas_dispatch_desc.commandList = ffxGetCommandListDX12(cmd_list->GetNative());
				cas_dispatch_desc.color = GetFfxResource(input_texture);
				cas_dispatch_desc.output = GetFfxResource(output_texture, FFX_RESOURCE_STATE_UNORDERED_ACCESS);
				cas_dispatch_desc.renderSize = { width, height };
				cas_dispatch_desc.sharpness = sharpness;

				FfxErrorCode errorCode = ffxCasContextDispatch(&cas_context, &cas_dispatch_desc);
				ADRIA_ASSERT(errorCode == FFX_OK);

				cmd_list->ResetState();
			}, RGPassType::Compute);

		GUI_Command([&]()
			{
				if (ImGui::TreeNodeEx(name_version, ImGuiTreeNodeFlags_None))
				{
					ImGui::SliderFloat("Sharpness", &sharpness, 0.0f, 1.0f, "%.2f");
					ImGui::TreePop();
				}
			}, GUICommandGroup_PostProcessor);

		return RG_RES_NAME(FFXCASOutput);
	}

	void FFXCASPass::OnResize(uint32 w, uint32 h)
	{
		width = w, height = h;
		DestroyContext();
		CreateContext();
	}

	void FFXCASPass::CreateContext()
	{
		cas_context_desc.colorSpaceConversion = FFX_CAS_COLOR_SPACE_LINEAR;
		cas_context_desc.flags |= FFX_CAS_SHARPEN_ONLY;
		cas_context_desc.maxRenderSize.width = width;
		cas_context_desc.maxRenderSize.height = height;
		cas_context_desc.displaySize.width = width;
		cas_context_desc.displaySize.height = height;
		FfxErrorCode error_code = ffxCasContextCreate(&cas_context, &cas_context_desc);
		ADRIA_ASSERT(error_code == FFX_OK);
	}

	void FFXCASPass::DestroyContext()
	{
		gfx->WaitForGPU();
		ffxCasContextDestroy(&cas_context);
	}

}



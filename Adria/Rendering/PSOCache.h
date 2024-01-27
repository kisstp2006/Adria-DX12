#pragma once

namespace adria
{
	class GfxDevice;
	class GfxPipelineState;

	enum class GfxPipelineStateID : uint8
	{
		Sky,
		MinimalAtmosphereSky,
		HosekWilkieSky,
		Rain,
		RainSimulation,
		RainSplash,
		Texture,
		Solid_Wireframe,
		Debug_Wireframe,
		Debug_Solid,
		Sun,
		GBuffer,
		GBuffer_NoCull,
		GBuffer_Mask,
		GBuffer_Mask_NoCull,
		Ambient,
		DeferredLighting,
		VolumetricLighting,
		TiledDeferredLighting,
		ClusteredDeferredLighting,
		ClusterBuilding,
		ClusterCulling,
		ToneMap,
		FXAA,
		TAA,
		Copy,
		Copy_AlphaBlend,
		Copy_AdditiveBlend,
		Add,
		Add_AlphaBlend,
		Add_AdditiveBlend,
		Shadow,
		Shadow_Transparent,
		SSAO,
		HBAO,
		SSR,
		GodRays,
		FilmEffects,
		LensFlare,
		LensFlare2,
		DOF,
		Clouds,
		Clouds_Reprojection,
		CloudDetail,
		CloudShape,
		CloudType,
		CloudsCombine,
		Fog,
		MotionBlur,
		Blur_Horizontal,
		Blur_Vertical,
		BloomDownsample,
		BloomDownsample_FirstPass,
		BloomUpsample,
		BokehGenerate,
		Bokeh,
		GenerateMips,
		MotionVectors,
		FFT_Horizontal,
		FFT_Vertical,
		InitialSpectrum,
		OceanNormals,
		Phase,
		Spectrum,
		Ocean,
		Ocean_Wireframe,
		OceanLOD,
		OceanLOD_Wireframe,
		Picking,
		Decals,
		Decals_ModifyNormals,
		BuildHistogram,
		HistogramReduction,
		Exposure,
		ClearCounters,
		CullInstances1stPhase,
		CullInstances1stPhase_NoOcclusionCull,
		CullInstances2ndPhase,
		BuildMeshletCullArgs1stPhase,
		BuildMeshletCullArgs2ndPhase,
		CullMeshlets1stPhase,
		CullMeshlets1stPhase_NoOcclusionCull,
		CullMeshlets2ndPhase,
		BuildMeshletDrawArgs1stPhase,
		BuildMeshletDrawArgs2ndPhase,
		DrawMeshlets,
		BuildInstanceCullArgs,
		InitializeHZB,
		HZBMips,
		RTAOFilter,
		DDGIUpdateIrradiance,
		DDGIUpdateDistance,
		DDGIVisualize,
		Unknown
	};

	namespace PSOCache
	{
		void Initialize(GfxDevice* gfx);
		void Destroy();
		GfxPipelineState* Get(GfxPipelineStateID);
	};
}
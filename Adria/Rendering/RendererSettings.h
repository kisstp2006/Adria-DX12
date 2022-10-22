#pragma once
#include "Enums.h"


namespace adria
{

	struct PostprocessSettings
	{
		bool fog = false;
		EAntiAliasing anti_aliasing = AntiAliasing_None;
		EAmbientOcclusion ambient_occlusion = EAmbientOcclusion::None;
		EReflections reflections = EReflections::SSR;
		bool dof = false;
		bool bokeh = false;
		bool bloom = false;
		bool clouds = false;
		bool motion_blur = false;
		bool automatic_exposure = false;
	};

	struct RendererSettings
	{
		bool gui_visible = false;
		bool use_tiled_deferred = false;
		bool use_clustered_deferred = false;
		PostprocessSettings postprocess{};
	};

}
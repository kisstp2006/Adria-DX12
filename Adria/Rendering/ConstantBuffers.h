#pragma once
#include "Core/CoreTypes.h"
#include <DirectXMath.h>

#ifndef DECLSPEC_ALIGN
#define DECLSPEC_ALIGN(x)   __declspec(align(x))
#endif

namespace adria
{
	DECLSPEC_ALIGN(16) struct FrameCBuffer
	{
		DirectX::XMMATRIX view;
		DirectX::XMMATRIX projection;
		DirectX::XMMATRIX view_projection;
		DirectX::XMMATRIX inverse_view;
		DirectX::XMMATRIX inverse_projection;
		DirectX::XMMATRIX inverse_view_projection;
		DirectX::XMMATRIX prev_view_projection;
		DirectX::XMMATRIX reprojection;
		DirectX::XMVECTOR camera_position;
		DirectX::XMVECTOR camera_forward;
		float  camera_jitter_x;
		float  camera_jitter_y;
		float  camera_near;
		float  camera_far;

		DirectX::XMVECTOR wind_params;
		DirectX::XMVECTOR sun_direction;
		DirectX::XMVECTOR sun_color;
		DirectX::XMVECTOR cascade_splits;

		float  screen_resolution_x;
		float  screen_resolution_y;
		float  mouse_normalized_coords_x;
		float  mouse_normalized_coords_y;

		float  delta_time;
		float  total_time;
		uint32 frame_count;
		int32  lights_idx;

		int32  lights_matrices_idx;
		int32  accel_struct_idx;
		int32  env_map_idx;
	};

	struct LightHLSL
	{
		DirectX::XMVECTOR position;
		DirectX::XMVECTOR direction;
		DirectX::XMVECTOR color;
		int32 active;
		float range;
		int32 type;
		float outer_cosine;

		float inner_cosine;
		int32 volumetric;
		float volumetric_strength;
		int32 use_cascades;

		int32 shadow_texture_index;
		int32 shadow_matrix_index;
		int32 shadow_mask_index;
		int32 padd;
	};
}
#include "RootSignatureCache.h"
#include "ShaderCache.h"
#include "../Graphics/GraphicsDeviceDX12.h"
#include "../Graphics/Shader.h"
#include "../Utilities/HashMap.h"

using namespace Microsoft::WRL;

namespace adria
{
	namespace
	{
		HashMap<ERootSignature, ComPtr<ID3D12RootSignature>> rs_map;
		inline Shader const& GetShader(EShaderId shader)
		{
			return ShaderCache::GetShader(shader);
		}

		void CreateRootSignaturesFromHLSL(ID3D12Device* device)
		{
			BREAK_IF_FAILED(device->CreateRootSignature(0, GetShader(PS_ClusteredLightingPBR).GetPointer(), GetShader(PS_ClusteredLightingPBR).GetLength(),
				IID_PPV_ARGS(rs_map[ERootSignature::ClusteredLightingPBR].GetAddressOf())));

			BREAK_IF_FAILED(device->CreateRootSignature(0, GetShader(PS_Volumetric_Directional).GetPointer(), GetShader(PS_Volumetric_Directional).GetLength(),
				IID_PPV_ARGS(rs_map[ERootSignature::Volumetric].GetAddressOf())));

			BREAK_IF_FAILED(device->CreateRootSignature(0, GetShader(CS_TiledLighting).GetPointer(), GetShader(CS_TiledLighting).GetLength(),
				IID_PPV_ARGS(rs_map[ERootSignature::TiledLighting].GetAddressOf())));

			BREAK_IF_FAILED(device->CreateRootSignature(0, GetShader(CS_ClusterBuilding).GetPointer(), GetShader(CS_ClusterBuilding).GetLength(),
				IID_PPV_ARGS(rs_map[ERootSignature::ClusterBuilding].GetAddressOf())));

			BREAK_IF_FAILED(device->CreateRootSignature(0, GetShader(CS_ClusterCulling).GetPointer(), GetShader(CS_ClusterCulling).GetLength(),
				IID_PPV_ARGS(rs_map[ERootSignature::ClusterCulling].GetAddressOf())));

			/*ID3D12VersionedRootSignatureDeserializer* drs = nullptr;
			D3D12CreateVersionedRootSignatureDeserializer(GetShader(PS_Add).GetPointer(), shader_map[PS_Add].GetLength(), IID_PPV_ARGS(&drs));
			D3D12_VERSIONED_ROOT_SIGNATURE_DESC const* desc = drs->GetUnconvertedRootSignatureDesc();*/
		}
		void CreateRootSignaturesFromCpp(ID3D12Device* device)
		{
			D3D12_FEATURE_DATA_ROOT_SIGNATURE feature_data{};
			feature_data.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
			if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &feature_data, sizeof(feature_data))))
				feature_data.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;

			{
				CD3DX12_ROOT_PARAMETER1 root_parameters[4] = {}; //14 DWORDS = 8 * 1 DWORD for root constants + 3 * 2 DWORDS for CBVs
				root_parameters[0].InitAsConstantBufferView(0);
				root_parameters[1].InitAsConstants(8, 1);
				root_parameters[2].InitAsConstantBufferView(2);
				root_parameters[3].InitAsConstantBufferView(3);

				D3D12_ROOT_SIGNATURE_FLAGS flags =
					D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
					D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED;

				CD3DX12_STATIC_SAMPLER_DESC static_samplers[8] = {};
				static_samplers[0].Init(0, D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_WRAP,   D3D12_TEXTURE_ADDRESS_MODE_WRAP,   D3D12_TEXTURE_ADDRESS_MODE_WRAP);
				static_samplers[1].Init(1, D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
				static_samplers[2].Init(2, D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_BORDER, D3D12_TEXTURE_ADDRESS_MODE_BORDER, D3D12_TEXTURE_ADDRESS_MODE_BORDER);

				static_samplers[3].Init(3, D3D12_FILTER_MIN_MAG_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_WRAP);
				static_samplers[4].Init(4, D3D12_FILTER_MIN_MAG_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
				static_samplers[5].Init(5, D3D12_FILTER_MIN_MAG_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_BORDER, D3D12_TEXTURE_ADDRESS_MODE_BORDER, D3D12_TEXTURE_ADDRESS_MODE_BORDER);

				static_samplers[6].Init(6, D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, 0.0f, 16u, D3D12_COMPARISON_FUNC_LESS_EQUAL);
				static_samplers[7].Init(7, D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_WRAP, 0.0f, 16u, D3D12_COMPARISON_FUNC_LESS_EQUAL);
				
				CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC desc{};
				desc.Init_1_1(ARRAYSIZE(root_parameters), root_parameters, ARRAYSIZE(static_samplers), static_samplers, flags);

				ComPtr<ID3DBlob> signature;
				ComPtr<ID3DBlob> error;
				HRESULT hr = D3DX12SerializeVersionedRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1_1, &signature, &error);
				BREAK_IF_FAILED(hr);
				hr = device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&rs_map[ERootSignature::Common]));
				BREAK_IF_FAILED(hr);
			}
		}

		void CreateAllRootSignatures(ID3D12Device* device)
		{
			CreateRootSignaturesFromHLSL(device);
			CreateRootSignaturesFromCpp(device);
		}
	}

	namespace RootSignatureCache
	{
		void Initialize(GraphicsDevice* gfx)
		{
			CreateAllRootSignatures(gfx->GetDevice());
		}
		void Destroy()
		{
			auto FreeContainer = []<typename T>(T& container)
			{
				container.clear();
				T empty;
				using std::swap;
				swap(container, empty);
			};
			FreeContainer(rs_map);
		}
		ID3D12RootSignature* Get(ERootSignature root_signature_id)
		{
			return rs_map[root_signature_id].Get();
		}
	}
}


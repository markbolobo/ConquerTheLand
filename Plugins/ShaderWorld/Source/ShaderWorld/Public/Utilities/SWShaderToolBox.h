/*
 * ShaderWorld: A procedural framework.
 * Website : https://www.shader.world/
 * Copyright (c) 2021-2023 MONSIEUR MAXIME DUPART
 *
 * This content is provided under the license of :
 * Epic Content License Agreement - https://www.unrealengine.com/en-US/eula/content
 *
 * You may not Distribute Licensed Content in source format to third parties except to employees,
 * affiliates, and contractors who are utilizing the Licensed Content in good faith to develop a Project
 * on your behalf. Those employees, affiliates, and contractors you share Licensed Content
 * with are not permitted to further Distribute the Licensed Content (including as incorporated in a Project)
 * and must delete the Licensed Content once it is no longer needed for developing a Project on your behalf.
 * You are responsible for ensuring that any employees, affiliates, or contractors you share Licensed Content
 * with comply with the terms of this Agreement.
 *
 * General Restrictions - You may not:
 * i. attempt to reverse engineer, decompile, translate, disassemble, or derive source code from Licensed Content;
 * ii. sell, rent, lease, or transfer Licensed Content on a “stand-alone basis”
 * (Projects must reasonably add value beyond the value of the Licensed Content,
 * and the Licensed Content must be merely a component of the Project and not the primary focus of the Project);
 *
 */

 /*
  * Main authors: Maxime Dupart (https://twitter.com/Max_Dupt)
  */

#pragma once

#include "CoreMinimal.h"
#include "GlobalShader.h"
#include "RenderResource.h"
#include "ShaderParameters.h"
#include "Shader.h"
#include "MeshMaterialShader.h"
#include "RenderGraphUtils.h"
#include "MaterialShader.h"
#include "SWStats.h"


class USWorldSubsystem;
class UTextureRenderTarget2D;
class FSWSpawnableRequirements;
struct SWDrawMaterialToRTData;
struct SWCopyData;
struct SWNormalComputeData;
struct SWSampleRequestComputeData;

// Those Computer Shaders work on OpenGL ES 3.1/Vulkan/Metal/DX11/DX12 

namespace ShaderWorldGPUTools
{

	/**
	 * 
	 */
	class SHADERWORLD_API SWShaderToolBox
	{
	public:	
		SWShaderToolBox(){};
		~SWShaderToolBox(){};

		void CopyAtoB(const SWCopyData& Data) const;
		void CopyAtoB_CS_RT(FRHICommandListImmediate& RHICmdList, const SWCopyData& Data) const;

		void ComputeNormalForHeightMap(const SWNormalComputeData& Data) const;
		void ComputeNormalForHeightMap_RT(FRHICommandListImmediate& RHICmdList, const SWNormalComputeData& Data) const;

		void ComputeSpawnables(TSharedPtr<FSWSpawnableRequirements, ESPMode::ThreadSafe>& SpawnConfig) const;
		void ComputeSpawnables_CS_RT(FRHICommandListImmediate& RHICmdList, const TSharedPtr<FSWSpawnableRequirements, ESPMode::ThreadSafe>& SpawnConfig) const;

		void RequestReadBackLoad(const SWSampleRequestComputeData& Data) const;
		void RequestReadBackLoad_RT(FRHICommandListImmediate& RHICmdList, const SWSampleRequestComputeData& Data) const;
		
	};

	static uint32 SW_MobileLowSharedMemory_GroupSizeX = 8;
	static uint32 SW_MobileLowSharedMemory_GroupSizeY = 4;
	


	BEGIN_SHADER_PARAMETER_STRUCT(FSWShaderToolTextureRead, )
	SHADER_PARAMETER_TEXTURE(Texture2D, SourceTex)
	SHADER_PARAMETER_SAMPLER(SamplerState, SourceTexSampler)
	END_SHADER_PARAMETER_STRUCT()

	template <bool bIsMobileRenderer>
	class FShaderCopyTool_CS : public FGlobalShader
	{
	public:

		using FPermutationDomain = TShaderPermutationDomain<>;

		DECLARE_EXPORTED_SHADER_TYPE(FShaderCopyTool_CS, Global, SHADERWORLD_API);
		SHADER_USE_PARAMETER_STRUCT(FShaderCopyTool_CS, FGlobalShader);

		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
		{
			return true;
		}

		static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
		{

			FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
			OutEnvironment.SetDefine(TEXT("SW_COMPUTE"), 1);
			OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZEX"), bIsMobileRenderer ? SW_MobileLowSharedMemory_GroupSizeX : FComputeShaderUtils::kGolden2DGroupSize);
			OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZEY"), bIsMobileRenderer ? SW_MobileLowSharedMemory_GroupSizeY : FComputeShaderUtils::kGolden2DGroupSize);
			OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZEZ"), 1);
			/*....*/
		}

		static int32 GetGroupSizeX()
		{
			return bIsMobileRenderer ? SW_MobileLowSharedMemory_GroupSizeX : FComputeShaderUtils::kGolden2DGroupSize;
		}
		static int32 GetGroupSizeY()
		{
			return bIsMobileRenderer ? SW_MobileLowSharedMemory_GroupSizeY : FComputeShaderUtils::kGolden2DGroupSize;
		}

		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
			SHADER_PARAMETER_STRUCT_INCLUDE(FSWShaderToolTextureRead, Pass)
			SHADER_PARAMETER(uint32, SourceTextDim)
			SHADER_PARAMETER(uint32, DestTextDim)
			SHADER_PARAMETER(uint32, Border)
			SHADER_PARAMETER(uint32, ChannelSelect)
			SHADER_PARAMETER(FVector2f, SourceLoc)
			SHADER_PARAMETER(float, SourceWorldSize)
			SHADER_PARAMETER(FVector2f, DestinationLoc)
			SHADER_PARAMETER(float, DestWorldSize)
			SHADER_PARAMETER_TEXTURE(Texture2D, DestTexDuplicate)
			SHADER_PARAMETER_UAV(RWTexture2D<float4>, DestinationTex)
			END_SHADER_PARAMETER_STRUCT()
	};

	using FSWCopyTool_CS = FShaderCopyTool_CS<false>;
	using FSWCopyTool_MobileCS = FShaderCopyTool_CS<true>;
	/*
	static uint32 SW_Normal_CS_GroupSizeX = 8;
	static uint32 SW_Normal_CS_GroupSizeY = 8;
	static uint32 SW_Normal_CS_GroupSizeZ = 1;
	*/
	template <bool bIsMobileRenderer>
	class FNormalFromHeightmap_CS : public FGlobalShader
	{
	public:

		using FPermutationDomain = TShaderPermutationDomain<>;

		DECLARE_EXPORTED_SHADER_TYPE(FNormalFromHeightmap_CS, Global, SHADERWORLD_API);
		SHADER_USE_PARAMETER_STRUCT(FNormalFromHeightmap_CS, FGlobalShader);

		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
		{
			return true;
		}

		static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
		{

			FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
			OutEnvironment.SetDefine(TEXT("SW_COMPUTE"), 1);
			OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZEX"), bIsMobileRenderer ? SW_MobileLowSharedMemory_GroupSizeX : FComputeShaderUtils::kGolden2DGroupSize);
			OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZEY"), bIsMobileRenderer ? SW_MobileLowSharedMemory_GroupSizeY : FComputeShaderUtils::kGolden2DGroupSize);
			OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZEZ"), 1);
			/*....*/
		}

		static int32 GetGroupSizeX()
		{
			return bIsMobileRenderer ? SW_MobileLowSharedMemory_GroupSizeX : FComputeShaderUtils::kGolden2DGroupSize;
		}
		static int32 GetGroupSizeY()
		{
			return bIsMobileRenderer ? SW_MobileLowSharedMemory_GroupSizeY : FComputeShaderUtils::kGolden2DGroupSize;
		}

		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
			SHADER_PARAMETER_STRUCT_INCLUDE(FSWShaderToolTextureRead, Heightmap)
			SHADER_PARAMETER_UAV(RWTexture2D<float4>, DestinationTex)
			SHADER_PARAMETER(uint32, NormalMapSizeX)
			SHADER_PARAMETER(uint32, N)
			SHADER_PARAMETER(float, LocalGridScaling)
			SHADER_PARAMETER(float, SWHeightmapScale)
			END_SHADER_PARAMETER_STRUCT()
	};

	using FSWNormalFromHeightmap_CS = FNormalFromHeightmap_CS<false>;
	using FSWNormalFromHeightmap_MobileCS = FNormalFromHeightmap_CS<true>;
	/*
	static uint32 ComputeSpawnable_CS_GroupSizeX = 8;
	static uint32 ComputeSpawnable_CS_GroupSizeY = 8;
	static uint32 ComputeSpawnable_CS_GroupSizeZ = 1;
	*/
	template <bool bIsMobileRenderer>
	class FComputeSpawnable_CS : public FGlobalShader
	{
	public:

		using FPermutationDomain = TShaderPermutationDomain<>;

		DECLARE_EXPORTED_SHADER_TYPE(FComputeSpawnable_CS, Global, SHADERWORLD_API);
		SHADER_USE_PARAMETER_STRUCT(FComputeSpawnable_CS, FGlobalShader);		

		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
		{
			return true;
		}

		static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
		{

			FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
			OutEnvironment.SetDefine(TEXT("SW_COMPUTE"), 1);
			OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZEX"), bIsMobileRenderer ? SW_MobileLowSharedMemory_GroupSizeX : FComputeShaderUtils::kGolden2DGroupSize);
			OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZEY"), bIsMobileRenderer ? SW_MobileLowSharedMemory_GroupSizeY : FComputeShaderUtils::kGolden2DGroupSize);
			OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZEZ"), 1);
			/*....*/
		}

		static int32 GetGroupSizeX()
		{
			return bIsMobileRenderer ? SW_MobileLowSharedMemory_GroupSizeX : FComputeShaderUtils::kGolden2DGroupSize;
		}
		static int32 GetGroupSizeY()
		{
			return bIsMobileRenderer ? SW_MobileLowSharedMemory_GroupSizeY : FComputeShaderUtils::kGolden2DGroupSize;
		}

		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
			SHADER_PARAMETER_TEXTURE(Texture2D, Density)
			SHADER_PARAMETER_SAMPLER(SamplerState, DensitySampler)
			SHADER_PARAMETER_TEXTURE(Texture2D, HeightMap)
			SHADER_PARAMETER_SAMPLER(SamplerState, HeightMapSampler)
			SHADER_PARAMETER_TEXTURE(Texture2D, NormalMap)
			SHADER_PARAMETER_SAMPLER(SamplerState, NormalMapSampler)
			SHADER_PARAMETER_TEXTURE(Texture2D, PlacementNoiseMap)
			SHADER_PARAMETER_SAMPLER(SamplerState, PlacementNoiseMapSampler)
			SHADER_PARAMETER_UAV(RWTexture2D<float4>, DestinationTex) //Transforms
			SHADER_PARAMETER(uint32, SamplePerSide)
			SHADER_PARAMETER(uint32, Packed_DX_Status_N_RTDim)
			SHADER_PARAMETER(float, AlignToSlopeOffset)
			SHADER_PARAMETER(float, AlignMaxAngle)
			SHADER_PARAMETER(float, MinSpawnHeight)
			SHADER_PARAMETER(float, MaxSpawnHeight)
			SHADER_PARAMETER(float, MinVerticalOffset)
			SHADER_PARAMETER(float, MaxVerticalOffset)
			SHADER_PARAMETER(float, MinScale)
			SHADER_PARAMETER(float, MaxScale)
			SHADER_PARAMETER(float, MinGroundSlope)
			SHADER_PARAMETER(float, MaxGroundSlope)
			SHADER_PARAMETER(float, MeshScale)
			SHADER_PARAMETER(float, LocalGridScaling)
			SHADER_PARAMETER(float, HasNoise)
			SHADER_PARAMETER(FVector3f, MeshLocation)
			SHADER_PARAMETER(FVector3f, RingLocation)
		END_SHADER_PARAMETER_STRUCT()
	};

	using FSWComputeSpawnable_CS = FComputeSpawnable_CS<false>;
	using FSWComputeSpawnable_MobileCS = FComputeSpawnable_CS<true>;

	BEGIN_SHADER_PARAMETER_STRUCT(FSWShaderToolTopologyUpdateParameters, )
	SHADER_PARAMETER(uint32, IndexLength)
	SHADER_PARAMETER(float, NValue)
	SHADER_PARAMETER_TEXTURE(Texture2D, HeightMap)
	SHADER_PARAMETER_SRV(Buffer<float>, TexCoordBuffer)
	SHADER_PARAMETER_SRV(Buffer<uint>, InputIndexBufferA)
	SHADER_PARAMETER_SRV(Buffer<uint>, InputIndexBufferB)
	SHADER_PARAMETER_UAV(RWBuffer<uint>, OutputIndexBuffer)
	END_SHADER_PARAMETER_STRUCT()

	static uint32 SW_TopoUpdate_GroupSizeX = 32;
	static uint32 SW_TopoUpdate_GroupSizeY = 1;
	static uint32 SW_TopoUpdate_GroupSizeZ = 1;

	class FTopologyUpdate_CS : public FGlobalShader
	{
	public:

		using FPermutationDomain = TShaderPermutationDomain<>;

		DECLARE_EXPORTED_SHADER_TYPE(FTopologyUpdate_CS, Global, SHADERWORLD_API);
		SHADER_USE_PARAMETER_STRUCT(FTopologyUpdate_CS, FGlobalShader);

		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
		{
			return true;
		}

		static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
		{
			FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
			OutEnvironment.SetDefine(TEXT("SW_COMPUTE"), 1);
			OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZEX"), SW_TopoUpdate_GroupSizeX);
			OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZEY"), SW_TopoUpdate_GroupSizeY);
			OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZEZ"), SW_TopoUpdate_GroupSizeZ);
			/*....*/
		}

		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
			SHADER_PARAMETER_STRUCT_INCLUDE(FSWShaderToolTopologyUpdateParameters, Pass)
		END_SHADER_PARAMETER_STRUCT()
	};


	static uint32 SW_LoadReadBackLocations_GroupSizeX = 8;
	static uint32 SW_LoadReadBackLocations_GroupSizeY = 4;
	static uint32 SW_LoadReadBackLocations_GroupSizeZ = 1;

	class FLoadReadBackLocations_CS : public FGlobalShader
	{
	public:

		using FPermutationDomain = TShaderPermutationDomain<>;

		DECLARE_EXPORTED_SHADER_TYPE(FLoadReadBackLocations_CS, Global, SHADERWORLD_API);
		SHADER_USE_PARAMETER_STRUCT(FLoadReadBackLocations_CS, FGlobalShader);

		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
		{
			return true;
		}

		static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
		{
			FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
			OutEnvironment.SetDefine(TEXT("SW_COMPUTE"), 1);
			OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZEX"), SW_LoadReadBackLocations_GroupSizeX);
			OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZEY"), SW_LoadReadBackLocations_GroupSizeY);
			OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZEZ"), SW_LoadReadBackLocations_GroupSizeZ);
			/*....*/
		}

		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
			SHADER_PARAMETER(uint32, SampleDim)
			SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<float>, SourceLocationBuffer)
			SHADER_PARAMETER_UAV(RWTexture2D<float2>, DestLocationsTex)
		END_SHADER_PARAMETER_STRUCT()
	};

}



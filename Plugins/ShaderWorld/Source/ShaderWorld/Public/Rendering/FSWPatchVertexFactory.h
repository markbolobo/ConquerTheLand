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
#include "UniformBuffer.h"
#include "RenderResource.h"
#include "StaticMeshResources.h"
#include "MeshMaterialShader.h"

BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FSWPatchParameters, )
SHADER_PARAMETER_TEXTURE(Texture2D, HeightMap)
SHADER_PARAMETER_TEXTURE(Texture2D, NormalMap)
SHADER_PARAMETER_SAMPLER(SamplerState, NormalMapSampler)
SHADER_PARAMETER(FMatrix44f, LocalToWorldNoScaling)
SHADER_PARAMETER(float, PatchFullSize)
SHADER_PARAMETER(float, SmoothLODRange)
SHADER_PARAMETER(float, SWQuadDistance)
SHADER_PARAMETER(uint32, SWQuadOffset)
END_GLOBAL_SHADER_PARAMETER_STRUCT()

typedef TUniformBufferRef<FSWPatchParameters> FSWPatchVertexFactoryBufferRef;

class FSWPatchVertexFactoryShaderParameters;

struct FSWPatchBatchElementParams
{
	const TUniformBuffer<FSWPatchParameters>* SWPatchUniformParametersResource;
};

class FSWPatchBatchElementParamArray : public FOneFrameResource
{
public:
	TArray<FSWPatchBatchElementParams, SceneRenderingAllocator> ElementParams;
};

struct FSWPatchUserData : public FOneFrameResource
{
	FVector3f PatchLocation;
	FMatrix LocalToWNoScaling;
};

class FSWPatchUniformData : public TSharedFromThis<FSWPatchUniformData>
{
public:

	UTextureRenderTarget2D* HeightMap=nullptr;
	UTextureRenderTarget2D* NormalMap = nullptr;
	FVector PatchLocation=FVector(0);
	float PatchFullSize = 32.f;
	int32 SWHeightScale = 1;
	float SmoothLODRange = 0.2f;
	float MeshScale;
	float N = 64;
	float LocalGridScaling = 0.5f;
	float CacheRes;
	bool UseMorphing = true;

	inline FSWPatchUniformData() = default;
	inline FSWPatchUniformData(UTextureRenderTarget2D* HeightMap_, UTextureRenderTarget2D* NormalMap_,FVector PatchLocation_, float PatchFullSize_, int32 SWHeightScale_
		, float SmoothLODRange_, float MeshScale_, float N_, float LocalGridScaling_, float CacheRes_, bool UseMorphing_)
		: HeightMap(HeightMap_)
		, NormalMap(NormalMap_)
		, PatchLocation(PatchLocation_)
		, PatchFullSize(PatchFullSize_)
		, SWHeightScale(SWHeightScale_)
		, SmoothLODRange(SmoothLODRange_)
		, MeshScale(MeshScale_)
		, N(N_)
		, LocalGridScaling(LocalGridScaling_)
		, CacheRes(CacheRes_)
		, UseMorphing(UseMorphing_)
	{}

	inline ~FSWPatchUniformData() {}
};

/**
 * 
 */
template <bool bWithLODMorphing>
class FSWPatchVertexFactory : public FLocalVertexFactory
{
	DECLARE_VERTEX_FACTORY_TYPE(FSWPatchVertexFactory< bWithLODMorphing >);

	typedef FLocalVertexFactory Super;
public:

	FSWPatchVertexFactory(ERHIFeatureLevel::Type InFeatureLevel, const TSharedPtr < FSWPatchUniformData, ESPMode::ThreadSafe >& InParams);
	virtual ~FSWPatchVertexFactory() override;
	

	virtual void InitRHI() override;
	virtual void ReleaseRHI() override;

	/**
	 * Should we cache the material's shadertype on this platform with this vertex factory?
	 */
	static bool ShouldCompilePermutation(const FVertexFactoryShaderPermutationParameters& Parameters);

	/**
	 * Can be overridden by FVertexFactory subclasses to modify their compile environment just before compilation occurs.
	 */
	static void ModifyCompilationEnvironment(const FVertexFactoryShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment);

private:
	TSharedPtr < FSWPatchUniformData, ESPMode::ThreadSafe > ParamsData;
	FSWPatchParameters Params;
	FSWPatchVertexFactoryBufferRef UniformBuffer;
	friend class FSWPatchVertexFactoryShaderParameters;
};

using FSWPatchVertexFactoryNoMorphing = FSWPatchVertexFactory<false>;
using FSWPatchVertexFactoryMorphing = FSWPatchVertexFactory<true>;


class FSWPatchVertexFactoryShaderParameters : public FVertexFactoryShaderParameters
{
	DECLARE_TYPE_LAYOUT(FSWPatchVertexFactoryShaderParameters, NonVirtual);
public:

	void Bind(const FShaderParameterMap& ParameterMap)
	{

	}

	inline void GetElementShaderBindings(
		const class FSceneInterface* Scene,
		const FSceneView* View,
		const FMeshMaterialShader* Shader,
		const EVertexInputStreamType InputStreamType,
		ERHIFeatureLevel::Type FeatureLevel,
		const FVertexFactory* VertexFactory,
		const FMeshBatchElement& BatchElement,
		FMeshDrawSingleShaderBindings& ShaderBindings,
		FVertexInputStreamArray& VertexStreams
	) const;
	

private:

};


template <bool bWithLODMorphing>
FSWPatchVertexFactory<bWithLODMorphing>::FSWPatchVertexFactory(ERHIFeatureLevel::Type InFeatureLevel, const TSharedPtr < FSWPatchUniformData, ESPMode::ThreadSafe >& InParams)
	: Super(InFeatureLevel, "FSWPatchVertexFactory")
	, ParamsData(InParams)
{

}
template <bool bWithLODMorphing>
FSWPatchVertexFactory<bWithLODMorphing>::~FSWPatchVertexFactory()
{
}
template <bool bWithLODMorphing>
void FSWPatchVertexFactory<bWithLODMorphing>::InitRHI()
{

	FLocalVertexFactory::InitRHI();
}
template <bool bWithLODMorphing>
void FSWPatchVertexFactory<bWithLODMorphing>::ReleaseRHI()
{
	FLocalVertexFactory::ReleaseRHI();
}
template <bool bWithLODMorphing>
bool FSWPatchVertexFactory<bWithLODMorphing>::ShouldCompilePermutation(const FVertexFactoryShaderPermutationParameters& Parameters)
{
	auto FeatureLevel = GetMaxSupportedFeatureLevel(Parameters.Platform);

	return FLocalVertexFactory::ShouldCompilePermutation(Parameters) && (FeatureLevel >= ERHIFeatureLevel::ES3_1)
		&& Parameters.MaterialParameters.bIsUsedWithVirtualHeightfieldMesh
		|| Parameters.MaterialParameters.bIsSpecialEngineMaterial;
}
template <bool bWithLODMorphing>
void FSWPatchVertexFactory<bWithLODMorphing>::ModifyCompilationEnvironment(const FVertexFactoryShaderPermutationParameters& Parameters,
	FShaderCompilerEnvironment& OutEnvironment)
{
	OutEnvironment.SetDefine(TEXT("SW_PATCHFACTORY_MORPHING"), bWithLODMorphing);

	OutEnvironment.SetDefine(TEXT("SW_PATCHFACTORY"), 1);
	Super::ModifyCompilationEnvironment(Parameters, OutEnvironment);
}



void FSWPatchVertexFactoryShaderParameters::GetElementShaderBindings(const FSceneInterface* Scene,
	const FSceneView* View, const FMeshMaterialShader* Shader, const EVertexInputStreamType InputStreamType,
	ERHIFeatureLevel::Type FeatureLevel, const FVertexFactory* VertexFactory, const FMeshBatchElement& BatchElement,
	FMeshDrawSingleShaderBindings& ShaderBindings, FVertexInputStreamArray& VertexStreams) const
{
	const auto* LocalVertexFactory = static_cast<const FLocalVertexFactory*>(VertexFactory);

	FRHIUniformBuffer* VertexFactoryUniformBuffer = static_cast<FRHIUniformBuffer*>(BatchElement.VertexFactoryUserData);
	if (LocalVertexFactory->SupportsManualVertexFetch(FeatureLevel) || UseGPUScene(GMaxRHIShaderPlatform, FeatureLevel))
	{
		if (!VertexFactoryUniformBuffer)
		{
			// No batch element override
			VertexFactoryUniformBuffer = LocalVertexFactory->GetUniformBuffer();
		}

		ShaderBindings.Add(Shader->GetUniformBufferParameter<FLocalVertexFactoryUniformShaderParameters>(), VertexFactoryUniformBuffer);
	}

	const FSWPatchBatchElementParams* BatchElementParams = (const FSWPatchBatchElementParams*)BatchElement.UserData;
	ShaderBindings.Add(Shader->GetUniformBufferParameter<FSWPatchParameters>(), *BatchElementParams->SWPatchUniformParametersResource);
}


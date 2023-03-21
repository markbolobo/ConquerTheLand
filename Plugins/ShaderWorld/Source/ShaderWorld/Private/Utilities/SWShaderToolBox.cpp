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

#include "Utilities/SWShaderToolBox.h"


#include "SceneView.h"
#include "MeshPassProcessor.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "SWStats.h"


#include "Engine/TextureRenderTarget2D.h"
#include "Engine/Texture2D.h"

//#include "Renderer/Private/ScenePrivate.h"
//#include "Renderer/Private/SceneRendering.h"

#include "Data/SWStructs.h"

DECLARE_GPU_STAT_NAMED(ShaderWorldCopy, TEXT("ShaderWorld Copy"));
DECLARE_GPU_STAT_NAMED(ShaderWorldNormalmapCompute, TEXT("ShaderWorld Normalmap Compute"));
DECLARE_GPU_STAT_NAMED(ShaderWorldSpawnableCompute, TEXT("ShaderWorld Spawnable Compute"));
DECLARE_GPU_STAT_NAMED(ShaderWorldReadBack, TEXT("ShaderWorld ReadBack Process"));

namespace ShaderWorldGPUTools
{
	IMPLEMENT_SHADER_TYPE(template<>, FSWCopyTool_CS, TEXT("/ShaderWorld/ShaderWorldUtilities.usf"), TEXT("SimpleCopyCS"), SF_Compute);
	IMPLEMENT_SHADER_TYPE(template<>, FSWNormalFromHeightmap_CS, TEXT("/ShaderWorld/ShaderWorldUtilities.usf"), TEXT("NormalFromHeightMapCS"), SF_Compute);
	IMPLEMENT_SHADER_TYPE(template<>, FSWComputeSpawnable_CS, TEXT("/ShaderWorld/ShaderWorldUtilities.usf"), TEXT("ComputeSpawnableCS"), SF_Compute);

	IMPLEMENT_SHADER_TYPE(template<>, FSWCopyTool_MobileCS, TEXT("/ShaderWorld/ShaderWorldUtilities.usf"), TEXT("SimpleCopyCS"), SF_Compute);
	IMPLEMENT_SHADER_TYPE(template<>, FSWNormalFromHeightmap_MobileCS, TEXT("/ShaderWorld/ShaderWorldUtilities.usf"), TEXT("NormalFromHeightMapCS"), SF_Compute);
	IMPLEMENT_SHADER_TYPE(template<>, FSWComputeSpawnable_MobileCS, TEXT("/ShaderWorld/ShaderWorldUtilities.usf"), TEXT("ComputeSpawnableCS"), SF_Compute);


	IMPLEMENT_SHADER_TYPE(, FTopologyUpdate_CS, TEXT("/ShaderWorld/ShaderWorldUtilities.usf"), TEXT("TopologyUpdateCS"), SF_Compute);
	IMPLEMENT_SHADER_TYPE(, FLoadReadBackLocations_CS, TEXT("/ShaderWorld/ShaderWorldUtilities.usf"), TEXT("SampleLocationLoaderCS"), SF_Compute);




	template<typename T>
	void SW_GroupSize(UTextureRenderTarget2D* RenderTarget, FIntVector& GroupCount)
	{
		GroupCount.X = FMath::DivideAndRoundUp((float)RenderTarget->GetResource()->GetSizeX(), (float)T::GetGroupSizeX());
		GroupCount.Y = FMath::DivideAndRoundUp((float)RenderTarget->GetResource()->GetSizeY(), (float)T::GetGroupSizeY());
	}

	void SWShaderToolBox::CopyAtoB(const SWCopyData& Data) const
	{
		ENQUEUE_RENDER_COMMAND(ShaderTools_copy_rt)
			([this, Data](FRHICommandListImmediate& RHICmdList)
				{
					//SWCopyData rt(Source, Dest, Dest_dup, Border, Channel, SourceWSLocation, DestWSLocation, SourceWorldSize, DestWorldSize);
					CopyAtoB_CS_RT(RHICmdList,Data);
				}
		);
		
	}

	template<typename T>
	void SW_CopyAtoB_Typed(FRDGBuilder& GraphBuilder, const SWCopyData& Data)
	{
		RDG_EVENT_SCOPE(GraphBuilder, "SWRenderTargetCopy");
		RDG_GPU_STAT_SCOPE(GraphBuilder, ShaderWorldCopy);

		const FUnorderedAccessViewRHIRef RT_UAV = RHICreateUnorderedAccessView(Data.B->GetResource()->TextureRHI);

		FIntVector GroupCount;
		GroupCount.X = FMath::DivideAndRoundUp((float)Data.B->GetResource()->GetSizeX(), (float)T::GetGroupSizeX());
		GroupCount.Y = FMath::DivideAndRoundUp((float)Data.B->GetResource()->GetSizeY(), (float)T::GetGroupSizeY());
		GroupCount.Z = 1;

		typename T::FPermutationDomain PermutationVector;
		TShaderMapRef<T> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel), PermutationVector);

		typename T::FParameters* PassParameters = GraphBuilder.AllocParameters<typename T::FParameters>();

		if (Data.ChannelSelect > 0)
		{
			PassParameters->DestTexDuplicate = Data.B_duplicate->GetResource()->TextureRHI;
			PassParameters->SourceLoc = FVector2f(Data.SourceWSLocation);
			PassParameters->SourceWorldSize = Data.SourceWorldSize;

			PassParameters->DestinationLoc = FVector2f(Data.DestWSLocation);
			PassParameters->DestWorldSize = Data.DestWorldSize;
		}
		else
			PassParameters->DestTexDuplicate = Data.A->GetRenderTargetResource()->GetRenderTargetTexture();

		PassParameters->SourceTextDim = Data.A->GetResource()->GetSizeX();
		PassParameters->DestTextDim = Data.B->GetResource()->GetSizeX();
		PassParameters->DestinationTex = RT_UAV;
		PassParameters->Border = Data.Border;
		PassParameters->ChannelSelect = Data.ChannelSelect;
		PassParameters->Pass.SourceTex = Data.A->GetResource()->TextureRHI;
		PassParameters->Pass.SourceTexSampler = Data.A->GetResource()->SamplerStateRHI;
		GraphBuilder.AddPass(
			RDG_EVENT_NAME("SWShaderToolBox::CopyAtoB_CS"),
			PassParameters,
			ERDGPassFlags::Compute |
			ERDGPassFlags::NeverCull,
			[PassParameters, ComputeShader, GroupCount](FRHICommandList& RHICmdList)
			{
				FComputeShaderUtils::Dispatch(RHICmdList, ComputeShader, *PassParameters, GroupCount);
			});
	}

void SWShaderToolBox::CopyAtoB_CS_RT(FRHICommandListImmediate& RHICmdList, const SWCopyData& Data) const
{
	if (!(Data.A->GetResource() && Data.B->GetResource()))
		return;
	if ((Data.ChannelSelect > 0) && (!Data.B_duplicate || Data.B_duplicate && !(Data.B_duplicate->GetResource())))
		return;

	FRDGBuilder GraphBuilder(RHICmdList);

	if (GMaxRHIFeatureLevel > ERHIFeatureLevel::ES3_1)
		SW_CopyAtoB_Typed< FSWCopyTool_CS>(GraphBuilder, Data);
	else
		SW_CopyAtoB_Typed< FSWCopyTool_MobileCS>(GraphBuilder, Data);

#if 0
	const FUnorderedAccessViewRHIRef RT_UAV = RHICreateUnorderedAccessView(Data.B->GetResource()->TextureRHI);

	
	FIntVector GroupCount;
	GroupCount.Z = 1;

	if (GMaxRHIFeatureLevel > ERHIFeatureLevel::ES3_1)
	{
		GroupCount.X = FMath::DivideAndRoundUp((float)Data.B->GetResource()->GetSizeX(), (float)FSWCopyTool_CS::GetGroupSizeX());
		GroupCount.Y = FMath::DivideAndRoundUp((float)Data.B->GetResource()->GetSizeY(), (float)FSWCopyTool_CS::GetGroupSizeY());

		const FSWCopyTool_CS::FPermutationDomain PermutationVector;
		//ComputeShader = Material.GetShader<FSWCopyTool_CS>(&FLocalVertexFactory::StaticType, PermutationVector, false);
		ComputeShader = TShaderMapRef<FSWCopyTool_CS>(GetGlobalShaderMap(GMaxRHIFeatureLevel), PermutationVector);
	}
	else
	{
		GroupCount.X = FMath::DivideAndRoundUp((float)Data.B->GetResource()->GetSizeX(), (float)FSWCopyTool_MobileCS::GetGroupSizeX());
		GroupCount.Y = FMath::DivideAndRoundUp((float)Data.B->GetResource()->GetSizeY(), (float)FSWCopyTool_MobileCS::GetGroupSizeY());

		const FSWCopyTool_MobileCS::FPermutationDomain PermutationVector;
		ComputeShader = Material.GetShader<FSWCopyTool_MobileCS>(&FLocalVertexFactory::StaticType, PermutationVector, false);
	}
	

	

	FShaderCopyTool_CS::FPermutationDomain PermutationVector;
	TShaderMapRef<FShaderCopyTool_CS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel), PermutationVector);
	//SetComputePipelineState(RHICmdList, ComputeShader.GetComputeShader());

	FShaderCopyTool_CS::FParameters* PassParameters = GraphBuilder.AllocParameters<FShaderCopyTool_CS::FParameters>();

	if(Data.ChannelSelect>0)
	{
		PassParameters->DestTexDuplicate = Data.B_duplicate->GetResource()->TextureRHI;
		PassParameters->SourceLoc = FVector2f(Data.SourceWSLocation);
		PassParameters->SourceWorldSize = Data.SourceWorldSize;

		PassParameters->DestinationLoc = FVector2f(Data.DestWSLocation);
		PassParameters->DestWorldSize = Data.DestWorldSize;
	}
	else
		PassParameters->DestTexDuplicate = Data.A->GetRenderTargetResource()->GetRenderTargetTexture();

	PassParameters->SourceTextDim = Data.A->GetResource()->GetSizeX();
	PassParameters->DestTextDim = Data.B->GetResource()->GetSizeX();
	PassParameters->DestinationTex = RT_UAV;
	PassParameters->Border = Data.Border;
	PassParameters->ChannelSelect = Data.ChannelSelect;
	PassParameters->Pass.SourceTex = Data.A->GetResource()->TextureRHI;
	PassParameters->Pass.SourceTexSampler = Data.A->GetResource()->SamplerStateRHI;
	GraphBuilder.AddPass(
		RDG_EVENT_NAME("SWShaderToolBox::CopyAtoB_CS"),
		PassParameters,
		ERDGPassFlags::Compute |
		ERDGPassFlags::NeverCull,
		[PassParameters, ComputeShader, GroupCount](FRHICommandList& RHICmdList)
		{
			FComputeShaderUtils::Dispatch(RHICmdList, ComputeShader, *PassParameters, GroupCount);
		});
#endif
	GraphBuilder.Execute();
}

void SWShaderToolBox::ComputeNormalForHeightMap(const SWNormalComputeData& Data) const
{
	ENQUEUE_RENDER_COMMAND(ShaderTools_copy_rt)
		([this, Data](FRHICommandListImmediate& RHICmdList)
			{
				///if (/*Source && Source->GetResource() && */					Dest && Dest->GetResource())
					ComputeNormalForHeightMap_RT(RHICmdList,Data);
			}
	);
}

template<typename T>
void SW_NormalForHeightMap_Typed(FRDGBuilder& GraphBuilder, const SWNormalComputeData& Data)
{
	RDG_EVENT_SCOPE(GraphBuilder, "SWNormalMapCompute");
	RDG_GPU_STAT_SCOPE(GraphBuilder, ShaderWorldNormalmapCompute);

	FIntVector GroupCount;
	GroupCount.X = FMath::DivideAndRoundUp((float)Data.B->GetResource()->GetSizeX(), (float)T::GetGroupSizeX());
	GroupCount.Y = FMath::DivideAndRoundUp((float)Data.B->GetResource()->GetSizeY(), (float)T::GetGroupSizeY());
	GroupCount.Z = 1;

	const FUnorderedAccessViewRHIRef RT_UAV = RHICreateUnorderedAccessView(Data.B->GetResource()->TextureRHI);

	typename T::FPermutationDomain PermutationVector;
	TShaderMapRef<T> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel), PermutationVector);
	typename T::FParameters* PassParameters = GraphBuilder.AllocParameters<typename T::FParameters>();
	PassParameters->DestinationTex = RT_UAV;

	if (Data.A)
	{
		PassParameters->Heightmap.SourceTex = Data.A->GetResource()->TextureRHI;
		PassParameters->Heightmap.SourceTexSampler = Data.A->GetResource()->SamplerStateRHI;
	}
	PassParameters->NormalMapSizeX = Data.B->GetResource()->GetSizeX();
	PassParameters->SWHeightmapScale = Data.SWHeightmapScale;
	PassParameters->N = Data.N;
	PassParameters->LocalGridScaling = Data.LocalGridScaling;

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("SWShaderToolBox::ComputeNormal_CS"),
		PassParameters,
		ERDGPassFlags::Compute |
		ERDGPassFlags::NeverCull,
		[PassParameters, ComputeShader, GroupCount](FRHICommandList& RHICmdList)
		{
			FComputeShaderUtils::Dispatch(RHICmdList, ComputeShader, *PassParameters, GroupCount);
		});
}

void SWShaderToolBox::ComputeNormalForHeightMap_RT(FRHICommandListImmediate& RHICmdList, const SWNormalComputeData& Data) const
{
	if (!(Data.A->GetResource() && Data.B->GetResource()))
		return;

	FRDGBuilder GraphBuilder(RHICmdList);

	if (GMaxRHIFeatureLevel > ERHIFeatureLevel::ES3_1)
		SW_NormalForHeightMap_Typed< FSWNormalFromHeightmap_CS>(GraphBuilder, Data);
	else
		SW_NormalForHeightMap_Typed< FSWNormalFromHeightmap_MobileCS>(GraphBuilder, Data);
#if 0
	FIntVector GroupCount;
	GroupCount.X = FMath::DivideAndRoundUp((float)Data.B->GetResource()->GetSizeX(), (float)SW_Normal_CS_GroupSizeX);
	GroupCount.Y = FMath::DivideAndRoundUp((float)Data.B->GetResource()->GetSizeY(), (float)SW_Normal_CS_GroupSizeY);
	GroupCount.Z = 1;

	const FUnorderedAccessViewRHIRef RT_UAV = RHICreateUnorderedAccessView(Data.B->GetResource()->TextureRHI);

	FNormalFromHeightmap_CS::FPermutationDomain PermutationVector;
	TShaderMapRef<FNormalFromHeightmap_CS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel), PermutationVector);
	//SetComputePipelineState(RHICmdList, ComputeShader.GetComputeShader());
	FNormalFromHeightmap_CS::FParameters* PassParameters = GraphBuilder.AllocParameters<FNormalFromHeightmap_CS::FParameters>();
	PassParameters->DestinationTex = RT_UAV;

	if(Data.A)
	{
		PassParameters->Heightmap.SourceTex = Data.A->GetResource()->TextureRHI;
		PassParameters->Heightmap.SourceTexSampler = Data.A->GetResource()->SamplerStateRHI;
	}
	PassParameters->NormalMapSizeX = Data.B->GetResource()->GetSizeX();
	PassParameters->SWHeightmapScale = Data.SWHeightmapScale;
	PassParameters->N = Data.N;
	PassParameters->LocalGridScaling = Data.LocalGridScaling;

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("SWShaderToolBox::ComputeNormal_CS"),
		PassParameters,
		ERDGPassFlags::Compute |
		ERDGPassFlags::NeverCull,
		[PassParameters, ComputeShader, GroupCount](FRHICommandList& RHICmdList)
		{
			FComputeShaderUtils::Dispatch(RHICmdList, ComputeShader, *PassParameters, GroupCount);
		});
#endif

	GraphBuilder.Execute();
}


void SWShaderToolBox::ComputeSpawnables(TSharedPtr<FSWSpawnableRequirements, ESPMode::ThreadSafe>& SpawnConfig) const
{
	
	ENQUEUE_RENDER_COMMAND(ShaderTools_copy_rt)
	([this, SpawnConfig](FRHICommandListImmediate& RHICmdList)
		{
		if(SpawnConfig->Transforms && SpawnConfig->Transforms->GetResource() &&
			SpawnConfig->Density && SpawnConfig->Density)
			ComputeSpawnables_CS_RT(RHICmdList,SpawnConfig);
		}
	);
	
}


template<typename T>
void SW_Spawnables_Typed(FRDGBuilder& GraphBuilder, const TSharedPtr<FSWSpawnableRequirements, ESPMode::ThreadSafe>& SpawnConfig)
{
	RDG_EVENT_SCOPE(GraphBuilder, "SWSpawnableCompute");
	RDG_GPU_STAT_SCOPE(GraphBuilder, ShaderWorldSpawnableCompute);

	FIntVector GroupCount;
	GroupCount.X = FMath::DivideAndRoundUp(SpawnConfig->Transforms->GetResource()->GetSizeX() / 2.f, (float)T::GetGroupSizeX());
	GroupCount.Y = FMath::DivideAndRoundUp(SpawnConfig->Transforms->GetResource()->GetSizeY() / 2.f, (float)T::GetGroupSizeY());
	GroupCount.Z = 1;


	const FUnorderedAccessViewRHIRef RT_UAV = RHICreateUnorderedAccessView(SpawnConfig->Transforms->GetResource()->TextureRHI);

	uint32 PackedFlags = 0;
	PackedFlags |= (SpawnConfig->DX_Status ? 1 : 0) << 31;
	PackedFlags |= (SpawnConfig->AlignToSlope ? 1 : 0) << 30;
	PackedFlags |= (SpawnConfig->N & 0x7FFF) << 15;
	PackedFlags |= (SpawnConfig->RT_Dim & 0x7FFF);

	typename T::FPermutationDomain PermutationVector;
	TShaderMapRef<T> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel), PermutationVector);
	//SetComputePipelineState(RHICmdList, ComputeShader.GetComputeShader());

	typename T::FParameters* PassParameters = GraphBuilder.AllocParameters<typename T::FParameters>();
	PassParameters->DestinationTex = RT_UAV;
	PassParameters->Density = SpawnConfig->Density->GetResource()->TextureRHI;
	PassParameters->DensitySampler = SpawnConfig->Density->GetResource()->SamplerStateRHI;
	PassParameters->HeightMap = SpawnConfig->Heightmap->GetResource()->TextureRHI;
	PassParameters->HeightMapSampler = SpawnConfig->Heightmap->GetResource()->SamplerStateRHI;
	PassParameters->NormalMap = SpawnConfig->Normalmap->GetResource()->TextureRHI;
	PassParameters->NormalMapSampler = SpawnConfig->Normalmap->GetResource()->SamplerStateRHI;

	PassParameters->HasNoise = 0.f;

	if (SpawnConfig->NoiseT && SpawnConfig->NoiseT->GetResource())
	{
		PassParameters->HasNoise = 1.f;
		PassParameters->PlacementNoiseMap = SpawnConfig->NoiseT->GetResource()->TextureRHI;
		PassParameters->PlacementNoiseMapSampler = SpawnConfig->NoiseT->GetResource()->SamplerStateRHI;
	}

	PassParameters->SamplePerSide = SpawnConfig->Density->GetResource()->GetSizeX();
	PassParameters->Packed_DX_Status_N_RTDim = PackedFlags;
	PassParameters->AlignToSlopeOffset = SpawnConfig->AlignToSlopeOffset;
	PassParameters->AlignMaxAngle = SpawnConfig->AlignMaxAngle;
	PassParameters->MinSpawnHeight = SpawnConfig->AltitudeRange.Min;
	PassParameters->MaxSpawnHeight = SpawnConfig->AltitudeRange.Max;
	PassParameters->MinVerticalOffset = SpawnConfig->VerticalOffsetRange.Min;
	PassParameters->MaxVerticalOffset = SpawnConfig->VerticalOffsetRange.Max;
	PassParameters->MinScale = SpawnConfig->ScaleRange.Min;
	PassParameters->MaxScale = SpawnConfig->ScaleRange.Max;
	PassParameters->MinGroundSlope = SpawnConfig->GroundSlopeAngle.Min;
	PassParameters->MaxGroundSlope = SpawnConfig->GroundSlopeAngle.Max;
	PassParameters->MeshScale = SpawnConfig->MeshScale;
	PassParameters->LocalGridScaling = SpawnConfig->LocalGridScaling;
	PassParameters->MeshLocation = FVector3f(SpawnConfig->MeshLocation);
	PassParameters->RingLocation = FVector3f(SpawnConfig->RingLocation);

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("SWShaderToolBox::ComputeSpawnables_CS"),
		PassParameters,
		ERDGPassFlags::Compute |
		ERDGPassFlags::NeverCull,
		[PassParameters, ComputeShader, GroupCount](FRHICommandList& RHICmdList)
		{
			FComputeShaderUtils::Dispatch(RHICmdList, ComputeShader, *PassParameters, GroupCount);
		});

}

void SWShaderToolBox::ComputeSpawnables_CS_RT(FRHICommandListImmediate& RHICmdList, const TSharedPtr<FSWSpawnableRequirements, ESPMode::ThreadSafe>& SpawnConfig) const
{
	if (!(SpawnConfig->Transforms->GetResource() && SpawnConfig->Density->GetResource() && SpawnConfig->Heightmap->GetResource() && SpawnConfig->Normalmap->GetResource()))
		return;

	FRDGBuilder GraphBuilder(RHICmdList);

	if (GMaxRHIFeatureLevel > ERHIFeatureLevel::ES3_1)
		SW_Spawnables_Typed< FSWComputeSpawnable_CS>(GraphBuilder, SpawnConfig);
	else
		SW_Spawnables_Typed< FSWComputeSpawnable_MobileCS>(GraphBuilder, SpawnConfig);

#if 0
	FIntVector GroupCount;
	GroupCount.X = FMath::DivideAndRoundUp(SpawnConfig->Transforms->GetResource()->GetSizeX() / 2.f, (float)ComputeSpawnable_CS_GroupSizeX);
	GroupCount.Y = FMath::DivideAndRoundUp(SpawnConfig->Transforms->GetResource()->GetSizeY() / 2.f, (float)ComputeSpawnable_CS_GroupSizeY);
	GroupCount.Z = 1;

	
	const FUnorderedAccessViewRHIRef RT_UAV = RHICreateUnorderedAccessView(SpawnConfig->Transforms->GetResource()->TextureRHI);

	uint32 PackedFlags = 0;
	PackedFlags |= (SpawnConfig->DX_Status ? 1:0) << 31;
	PackedFlags |= (SpawnConfig->N & 0x7FFF) << 15;
	PackedFlags |= (SpawnConfig->RT_Dim & 0x7FFF);

	FComputeSpawnable_CS::FPermutationDomain PermutationVector;
	TShaderMapRef<FComputeSpawnable_CS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel), PermutationVector);
	//SetComputePipelineState(RHICmdList, ComputeShader.GetComputeShader());

	FComputeSpawnable_CS::FParameters* PassParameters = GraphBuilder.AllocParameters<FComputeSpawnable_CS::FParameters>();	
	PassParameters->DestinationTex = RT_UAV;
	PassParameters->Density = SpawnConfig->Density->GetResource()->TextureRHI;
	PassParameters->DensitySampler = SpawnConfig->Density->GetResource()->SamplerStateRHI;
	PassParameters->HeightMap = SpawnConfig->Heightmap->GetResource()->TextureRHI;
	PassParameters->HeightMapSampler = SpawnConfig->Heightmap->GetResource()->SamplerStateRHI;
	PassParameters->NormalMap = SpawnConfig->Normalmap->GetResource()->TextureRHI;
	PassParameters->NormalMapSampler = SpawnConfig->Normalmap->GetResource()->SamplerStateRHI;
	
	PassParameters->HasNoise = 0.f;

	if (SpawnConfig->NoiseT && SpawnConfig->NoiseT->GetResource())
	{
		PassParameters->HasNoise = 1.f;
		PassParameters->PlacementNoiseMap = SpawnConfig->NoiseT->GetResource()->TextureRHI;
		PassParameters->PlacementNoiseMapSampler = SpawnConfig->NoiseT->GetResource()->SamplerStateRHI;
	}
	
	PassParameters->SamplePerSide = SpawnConfig->Density->GetResource()->GetSizeX();
	PassParameters->Packed_DX_Status_N_RTDim = PackedFlags;
	PassParameters->AlignMaxAngle = SpawnConfig->AlignMaxAngle;
	PassParameters->MinSpawnHeight = SpawnConfig->AltitudeRange.Min;
	PassParameters->MaxSpawnHeight = SpawnConfig->AltitudeRange.Max;
	PassParameters->MinVerticalOffset = SpawnConfig->VerticalOffsetRange.Min;
	PassParameters->MaxVerticalOffset = SpawnConfig->VerticalOffsetRange.Max;
	PassParameters->MinScale = SpawnConfig->ScaleRange.Min;
	PassParameters->MaxScale = SpawnConfig->ScaleRange.Max;
	PassParameters->MinGroundSlope = SpawnConfig->GroundSlopeAngle.Min;
	PassParameters->MaxGroundSlope = SpawnConfig->GroundSlopeAngle.Max;
	PassParameters->MeshScale = SpawnConfig->MeshScale;
	PassParameters->LocalGridScaling = SpawnConfig->LocalGridScaling;
	PassParameters->MeshLocation = FVector3f(SpawnConfig->MeshLocation);
	PassParameters->RingLocation = FVector3f(SpawnConfig->RingLocation);

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("SWShaderToolBox::ComputeSpawnables_CS"),
		PassParameters,
		ERDGPassFlags::Compute |
		ERDGPassFlags::NeverCull,
		[PassParameters, ComputeShader, GroupCount](FRHICommandList& RHICmdList)
		{
			FComputeShaderUtils::Dispatch(RHICmdList, ComputeShader, *PassParameters, GroupCount);
		});

#endif

	GraphBuilder.Execute();

}

void SWShaderToolBox::RequestReadBackLoad(const SWSampleRequestComputeData& Data) const
{
	ENQUEUE_RENDER_COMMAND(ShaderTools_copy_rt)
		([this, Data](FRHICommandListImmediate& RHICmdList)
			{
				if (Data.SamplesXY && Data.SamplesXY->GetResource())
				RequestReadBackLoad_RT(RHICmdList,Data);
			}
	);

}

void SWShaderToolBox::RequestReadBackLoad_RT(FRHICommandListImmediate& RHICmdList, const SWSampleRequestComputeData& Data) const
{
	if (!(Data.SamplesXY && Data.SamplesXY->GetResource()))
		return;

	FRDGBuilder GraphBuilder(RHICmdList);

		{
			RDG_EVENT_SCOPE(GraphBuilder, "SWPositionReadBack");
			RDG_GPU_STAT_SCOPE(GraphBuilder, ShaderWorldReadBack);

			FIntVector GroupCount;
			GroupCount.X = FMath::DivideAndRoundUp((float)Data.SamplesXY->GetResource()->GetSizeX(), (float)SW_LoadReadBackLocations_GroupSizeX);
			GroupCount.Y = FMath::DivideAndRoundUp((float)Data.SamplesXY->GetResource()->GetSizeY(), (float)SW_LoadReadBackLocations_GroupSizeY);
			GroupCount.Z = 1;

			const FUnorderedAccessViewRHIRef RT_UAV = RHICreateUnorderedAccessView(Data.SamplesXY->GetResource()->TextureRHI);

			const FRDGBufferRef LocationRequest = CreateUploadBuffer(
				GraphBuilder,
				TEXT("SWLoadSampleLocations"),
				sizeof(float),
				Data.SamplesSource->PositionsXY.Num(),
				Data.SamplesSource->PositionsXY.GetData(),
				Data.SamplesSource->PositionsXY.Num() * Data.SamplesSource->PositionsXY.GetTypeSize()
			);

			const FRDGBufferSRVRef LocationRequestSRV = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(LocationRequest, PF_R32_FLOAT));;

			FLoadReadBackLocations_CS::FPermutationDomain PermutationVector;
			TShaderMapRef<FLoadReadBackLocations_CS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel), PermutationVector);
			//SetComputePipelineState(RHICmdList, ComputeShader.GetComputeShader());

			FLoadReadBackLocations_CS::FParameters* PassParameters = GraphBuilder.AllocParameters<FLoadReadBackLocations_CS::FParameters>();
			PassParameters->SampleDim = Data.SamplesXY->GetResource()->GetSizeX();
			PassParameters->DestLocationsTex = RT_UAV;
			PassParameters->SourceLocationBuffer = LocationRequestSRV;


			GraphBuilder.AddPass(
				RDG_EVENT_NAME("SWShaderToolBox::LoadReadBacklocations_CS"),
				PassParameters,
				ERDGPassFlags::Compute |
				ERDGPassFlags::NeverCull,
				[PassParameters, ComputeShader, GroupCount](FRHICommandList& RHICmdList)
				{
					FComputeShaderUtils::Dispatch(RHICmdList, ComputeShader, *PassParameters, GroupCount);
				});

		}

	
	GraphBuilder.Execute();

}
}

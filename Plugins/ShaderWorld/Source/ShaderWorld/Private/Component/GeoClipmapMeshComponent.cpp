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

  
#include "Component/GeoClipmapMeshComponent.h"
#include "PrimitiveViewRelevance.h"
#include "RenderResource.h"
#include "RenderingThread.h"
#include "PrimitiveSceneProxy.h"
#include "Containers/ResourceArray.h"
#include "EngineGlobals.h"
#include "VertexFactory.h"
#include "MaterialShared.h"
#include "Materials/Material.h"
#include "LocalVertexFactory.h"
#include "Engine/Engine.h"
#include "SceneManagement.h"
#include "PhysicsEngine/BodySetup.h"
//#include "ProceduralMeshComponentPluginPrivate.h"
#include "DynamicMeshBuilder.h"
#include "PhysicsEngine/PhysicsSettings.h"
#include "StaticMeshResources.h"
#include "PrimitiveSceneInfo.h"
#include "Engine/TextureRenderTarget2D.h"
//#include "Renderer/Private/ScenePrivate.h"


#if RHI_RAYTRACING
#include "RayTracingDefinitions.h"
#include "RayTracingInstance.h"
#endif

#include "SWorldSubsystem.h"
#include "SWStats.h"
#include "Utilities/SWShaderToolBox.h"

DECLARE_GPU_STAT_NAMED(ShaderWorldTopologyProcess, TEXT("ShaderWorld Topology Process"));

DECLARE_CYCLE_STAT(TEXT("Create GeoClip ProcMesh Proxy"), STAT_GeoClipProcMesh_CreateSceneProxy, STATGROUP_GeoClipProceduralMesh);
DECLARE_CYCLE_STAT(TEXT("Create GeoClip Mesh Section"), STAT_GeoClipProcMesh_CreateMeshSection, STATGROUP_GeoClipProceduralMesh);
DECLARE_CYCLE_STAT(TEXT("UpdateSection GeoClip GT"), STAT_GeoClipProcMesh_UpdateSectionGT, STATGROUP_GeoClipProceduralMesh);
DECLARE_CYCLE_STAT(TEXT("UpdateSection GeoClip RT"), STAT_GeoClipProcMesh_UpdateSectionRT, STATGROUP_GeoClipProceduralMesh);
DECLARE_CYCLE_STAT(TEXT("Get GeoClip ProcMesh Elements"), STAT_GeoClipProcMesh_GetMeshElements, STATGROUP_GeoClipProceduralMesh);
DECLARE_CYCLE_STAT(TEXT("Update GeoClip Collision"), STAT_GeoClipProcMesh_UpdateCollision, STATGROUP_GeoClipProceduralMesh);

DEFINE_LOG_CATEGORY_STATIC(LogGeoClipProceduralComponent, Log, All);

static TAutoConsoleVariable<int32> CVarRayTracingGeoClipProceduralMesh(
	TEXT("r.RayTracing.Geometry.GeoClipProceduralMeshes"),
	1,
	TEXT("Include GeoClip procedural meshes in ray tracing effects (default = 1 (procedural meshes enabled in ray tracing))"));



/** Class representing a single section of the proc mesh */
class FProcMeshProxySection
{
public:
	/** Material applied to this section */
	UMaterialInterface* Material;	

	/** Vertex factory for this section */
	bool DynamicTopology = false;

	/** Whether this section is currently visible */
	bool bSectionVisible;

#if RHI_RAYTRACING
	FRayTracingGeometry RayTracingGeometry;
#endif

	FProcMeshProxySection()
		: Material(NULL)
		, bSectionVisible(true)
	{}
};



/** 
 *	Struct used to send update to mesh data 
 *	Arrays may be empty, in which case no update is performed.
 */
class FGeoCProcMeshSectionUpdateData
{
public:
	/** Section to update */
	int32 TargetSection;
	/** New vertex information */
	TArray<FGeoCProcMeshVertex> NewVertexBuffer;
};

static void ConvertProcMeshToDynMeshVertex(FDynamicMeshVertex& Vert, const FGeoCProcMeshVertex& ProcVert)
{
	Vert.Position = (FVector3f)ProcVert.Position;
	Vert.Color = ProcVert.Color;
	Vert.TextureCoordinate[0] = ProcVert.UV0;
	Vert.TextureCoordinate[1] = ProcVert.UV1;
	Vert.TextureCoordinate[2] = ProcVert.UV2;
	Vert.TextureCoordinate[3] = ProcVert.UV3;
	Vert.TangentX = ProcVert.Tangent.TangentX;
	Vert.TangentZ = ProcVert.Normal;
	Vert.TangentZ.Vector.W = ProcVert.Tangent.bFlipTangentY ? -127 : 127;
}


/** Single global instance of the SWClipMapBuffersHolder. */
TGlobalResource< FSWClipMapBuffersHolder > GSWClipMapBufferHolder;


static inline void SWInitOrUpdateResource(FRenderResource* Resource)
{
	if (!Resource->IsInitialized())
	{
		Resource->InitResource();
	}
	else
	{
		Resource->UpdateRHI();
	}
}

void FSWClipMapBuffersHolder::DiscardBuffers(const TSharedPtr<FSWShareableID, ESPMode::ThreadSafe>& SWWorldVersion)
{
	FScopeLock ScopeLock(&MapLock);

	if (SectionShareableBuffers.Contains(SWWorldVersion))
	{
		if (IsInRenderingThread())
		{
			for (FDrawInstanceBuffers* Section : (*SectionShareableBuffers.Find(SWWorldVersion)))
			{
				if (Section != nullptr)
				{
					
						Section->VertexBuffers.ColorVertexBuffer.ReleaseResource();
						Section->VertexBuffers.StaticMeshVertexBuffer.ReleaseResource();
						Section->IndexBuffer.ReleaseResource();
						Section->IndexBufferAlt.ReleaseResource();
					
				}

				delete Section;
			}
			SectionShareableBuffers.Remove(SWWorldVersion);
		}
		else
		{
			ENQUEUE_RENDER_COMMAND(InitCommand)(
			[Version = SWWorldVersion](FRHICommandListImmediate& RHICmdList)
			{
				GSWClipMapBufferHolder.DiscardBuffers(Version);
			});
		}
	}
}

void FSWClipMapBuffersHolder::DiscardMeshComponentBuffers(const TSharedPtr<FSWShareableID, ESPMode::ThreadSafe>& MeshComponentVersion)
{
	FScopeLock ScopeLock(&MapLock);

	if ( ProxyShareableBuffers.Contains(MeshComponentVersion))
	{
		if (IsInRenderingThread())
		{

			if(ProxyShareableBuffers.Contains(MeshComponentVersion))
			{
				for (FProxyShareableBuffers* Section : (*ProxyShareableBuffers.Find(MeshComponentVersion)))
				{
					if (Section != nullptr)
					{
						Section->IndexBufferOpti.ReleaseResource();
					}

					delete Section;
				}
				ProxyShareableBuffers.Remove(MeshComponentVersion);
			}			
		}
		else
		{
			ENQUEUE_RENDER_COMMAND(InitCommand)(
				[Version = MeshComponentVersion](FRHICommandListImmediate& RHICmdList)
				{
					GSWClipMapBufferHolder.DiscardMeshComponentBuffers(Version);
				});
		}
	}
}

void FSWClipMapBuffersHolder::DiscardPrimitiveBuffers(	const TSharedPtr<FSWShareableID, ESPMode::ThreadSafe>& MeshComponentVersion)
{
	FScopeLock ScopeLock(&MapLock);

	if (ProxyPrimitiveBuffers.Contains(MeshComponentVersion))
	{
		if (IsInRenderingThread())
		{
			if (ProxyPrimitiveBuffers.Contains(MeshComponentVersion))
			{
				if (FProxyPrimitiveBuffers* PrimitiveBuffers = ProxyPrimitiveBuffers.Find(MeshComponentVersion))
				{
					if (PrimitiveBuffers->SWPatchUniformParameters.IsInitialized())
						PrimitiveBuffers->SWPatchUniformParameters.ReleaseResource();
				}
				ProxyPrimitiveBuffers.Remove(MeshComponentVersion);
			}
		}
		else
		{
			ENQUEUE_RENDER_COMMAND(InitCommand)(
				[Version = MeshComponentVersion](FRHICommandListImmediate& RHICmdList)
				{
					GSWClipMapBufferHolder.DiscardPrimitiveBuffers(Version);
				});
		}
	}
}

bool FSWClipMapBuffersHolder::RegisterPrimitive(const TSharedPtr<FSWShareableID, ESPMode::ThreadSafe>& MeshCompVersion)
{
	FScopeLock ScopeLock(&MapLock);

	if (!ProxyPrimitiveBuffers.Contains(MeshCompVersion))
	{
		ProxyPrimitiveBuffers.Add(MeshCompVersion);
	}
	return true;
}

bool FSWClipMapBuffersHolder::RegisterExtension(ERHIFeatureLevel::Type InFeatureLevel, bool bUseMorphing, const TSharedPtr<FSWShareableID, ESPMode::ThreadSafe>& SWWorldVersion, const TSharedPtr<FSWShareableID, ESPMode::ThreadSafe>& MeshCompVersion, const int32 NumSection, UGeoClipmapMeshComponent* Component, const TSharedPtr < FSWPatchUniformData, ESPMode::ThreadSafe >& UParams)
{
	FScopeLock ScopeLock(&MapLock);

	/*
	 * All components will have the same amount of sections, except the LOD0 which only have one section: a full quad. LOD0 can reuse the previously added buffers of a same SWWorld version
	 */
	if (SectionShareableBuffers.Contains(SWWorldVersion) && ((*SectionShareableBuffers.Find(SWWorldVersion)).Num() < NumSection))
		DiscardBuffers(SWWorldVersion);
	if (ProxyShareableBuffers.Contains(MeshCompVersion) && ((*ProxyShareableBuffers.Find(MeshCompVersion)).Num() < NumSection))
		DiscardMeshComponentBuffers(MeshCompVersion);


	if (!SectionShareableBuffers.Contains(SWWorldVersion))
	{
		SectionShareableBuffers.Add(SWWorldVersion,{});

		(*SectionShareableBuffers.Find(SWWorldVersion)).AddZeroed(NumSection);
		for (int SectionIdx = 0; SectionIdx < NumSection; SectionIdx++)
		{
			FGeoCProcMeshSection& SrcSection = Component->ProcMeshSections[SectionIdx];
			if (SrcSection.IndexBuffer.IsValid() && SrcSection.IndexBuffer->Indices.Num() > 0)
			{
				FDrawInstanceBuffers* NewSection = new FDrawInstanceBuffers(SrcSection.IndexBuffer, SrcSection.IndexBufferAlt);
				{
					{

						// Copy data from vertex buffer
						const int32 NumVerts = SrcSection.ProcVertexBuffer.Num();

						TArray<FDynamicMeshVertex> Vertices;
						Vertices.SetNumUninitialized(NumVerts);
						// Copy verts
						for (int VertIdx = 0; VertIdx < NumVerts; VertIdx++)
						{
							const FGeoCProcMeshVertex& ProcVert = SrcSection.ProcVertexBuffer[VertIdx];
							FDynamicMeshVertex& Vert = Vertices[VertIdx];
							ConvertProcMeshToDynMeshVertex(Vert, ProcVert);
						}
						NewSection->VertexBuffers.StaticMeshVertexBuffer.SetUseFullPrecisionUVs(true);

						uint8 NumTexCoords = 4;

						NewSection->VertexBuffers.StaticMeshVertexBuffer.Init(Vertices.Num(), NumTexCoords);
						NewSection->VertexBuffers.ColorVertexBuffer.Init(Vertices.Num());

						for (int32 i = 0; i < Vertices.Num(); i++)
						{
							const FDynamicMeshVertex& Vertex = Vertices[i];

							NewSection->VertexBuffers.StaticMeshVertexBuffer.SetVertexTangents(i, Vertex.TangentX.ToFVector3f(), Vertex.GetTangentY(), Vertex.TangentZ.ToFVector3f());
							for (uint32 j = 0; j < NumTexCoords; j++)
							{
								NewSection->VertexBuffers.StaticMeshVertexBuffer.SetVertexUV(i, j, Vertex.TextureCoordinate[j]);
							}
							NewSection->VertexBuffers.ColorVertexBuffer.VertexColor(i) = Vertex.Color;
						}

						BeginInitResource(&NewSection->VertexBuffers.StaticMeshVertexBuffer);
						BeginInitResource(&NewSection->VertexBuffers.ColorVertexBuffer);

					}
				}
				(*SectionShareableBuffers.Find(SWWorldVersion))[SectionIdx] = NewSection;
			}
		}
	}

	if (!ProxyShareableBuffers.Contains(MeshCompVersion))
	{
		ProxyShareableBuffers.Add(MeshCompVersion, {});
		(*ProxyShareableBuffers.Find(MeshCompVersion)).AddZeroed(NumSection);

		for (int SectionIdx = 0; SectionIdx < NumSection; SectionIdx++)
		{
			FGeoCProcMeshSection& SrcSection = Component->ProcMeshSections[SectionIdx];

			if (SrcSection.IndexBuffer.IsValid() && SrcSection.IndexBuffer->Indices.Num() > 0)
			{
				FProxyShareableBuffers* NewSection = new FProxyShareableBuffers(InFeatureLevel, UParams, SrcSection.IndexBuffer);

				{
					// Copy data from vertex buffer
					const int32 NumVerts = SrcSection.ProcVertexBuffer.Num();

					// Allocate verts

					TArray<FDynamicMeshVertex> Vertices;
					Vertices.SetNumUninitialized(NumVerts);
					// Copy verts
					for (int VertIdx = 0; VertIdx < NumVerts; VertIdx++)
					{
						const FGeoCProcMeshVertex& ProcVert = SrcSection.ProcVertexBuffer[VertIdx];
						FDynamicMeshVertex& Vert = Vertices[VertIdx];
						ConvertProcMeshToDynMeshVertex(Vert, ProcVert);
					}


					uint32 NumTexCoords = 4;
					NewSection->VertexBuffers.PositionVertexBuffer.Init(Vertices.Num());

					for (int32 i = 0; i < Vertices.Num(); i++)
					{
						const FDynamicMeshVertex& Vertex = Vertices[i];

						NewSection->VertexBuffers.PositionVertexBuffer.VertexPosition(i) = Vertex.Position;
					}
					FStaticMeshVertexBuffers* Self = &NewSection->VertexBuffers;
					FStaticMeshVertexBuffers* Shared = &GSWClipMapBufferHolder.GetBuffersForSection(SWWorldVersion, SectionIdx)->VertexBuffers;

					//if (bUseMorphing)
					{
						ENQUEUE_RENDER_COMMAND(StaticMeshVertexBuffersLegacyInit)(
							[Self, Shared, VF = &NewSection->VertexFactoryMorphing](FRHICommandListImmediate& RHICmdList)
							{
								SWInitOrUpdateResource(&Self->PositionVertexBuffer);

								FSWPatchVertexFactoryMorphing::FDataType Data;
								Self->PositionVertexBuffer.BindPositionVertexBuffer(VF, Data);
								Shared->StaticMeshVertexBuffer.BindTangentVertexBuffer(VF, Data);
								Shared->StaticMeshVertexBuffer.BindPackedTexCoordVertexBuffer(VF, Data);
								Shared->StaticMeshVertexBuffer.BindLightMapVertexBuffer(VF, Data, 0);
								Shared->ColorVertexBuffer.BindColorVertexBuffer(VF, Data);
								VF->SetData(Data);

								SWInitOrUpdateResource(VF);
							});
					}
					//else
					{
						ENQUEUE_RENDER_COMMAND(StaticMeshVertexBuffersLegacyInit)(
							[Self, Shared, VF = &NewSection->VertexFactoryNoMorphing](FRHICommandListImmediate& RHICmdList)
							{
								SWInitOrUpdateResource(&Self->PositionVertexBuffer);

						FSWPatchVertexFactoryNoMorphing::FDataType Data;
						Self->PositionVertexBuffer.BindPositionVertexBuffer(VF, Data);
						Shared->StaticMeshVertexBuffer.BindTangentVertexBuffer(VF, Data);
						Shared->StaticMeshVertexBuffer.BindPackedTexCoordVertexBuffer(VF, Data);
						Shared->StaticMeshVertexBuffer.BindLightMapVertexBuffer(VF, Data, 0);
						Shared->ColorVertexBuffer.BindColorVertexBuffer(VF, Data);
						VF->SetData(Data);

						SWInitOrUpdateResource(VF);
							});
					}
				}

				(*ProxyShareableBuffers.Find(MeshCompVersion))[SectionIdx] = NewSection;				
			}
		}
	}

	return true;
}

/** Procedural mesh scene proxy */
class FGeoClipProceduralMeshSceneProxy final : public FPrimitiveSceneProxy
{
public:
	
	mutable TArray<FConvexVolume> ViewsFrustums;
	mutable FThreadSafeBool ViewFrustumAccessMutex;
	
	TUniformBuffer<FPrimitiveUniformShaderParameters> PrimitiveUniformBuffer;

	TUniformBuffer<FSWPatchParameters> SWPatchUniformParameters;
	TArray<FSWPatchBatchElementParams> StaticBatchParamArray;
	
	SIZE_T GetTypeHash() const override
	{
		static size_t UniquePointer;
		return reinterpret_cast<size_t>(&UniquePointer);
	}

	virtual bool HasSubprimitiveOcclusionQueries() const override{return false;};

	FGeoClipProceduralMeshSceneProxy(UGeoClipmapMeshComponent* Component)
		: FPrimitiveSceneProxy(Component)
		, BodySetup(Component->GetBodySetup())
		, MaterialRelevance(Component->GetMaterialRelevance(GetScene().GetFeatureLevel()))
		, PatchData(Component->GetPatchData())
		, SWPatchLocation((Component->GetPatchData().IsValid() ? Component->GetPatchData()->PatchLocation : FVector(0.f)))
		, bDynamicTopology(Component->GetUseDynamicTopology())
		, MeshCastShadows(Component->CastShadow)
		, SWWorldVersion(Component->GetSWWorldVersion())
		, MeshComponentVersion(Component->GetComponentVersion())		
		, bUseMorphing(Component->GetPatchData().IsValid()? Component->GetPatchData()->UseMorphing: false)
		, bEvaluateWPO(Component->GetUseWPO())
		{
		
		// Copy each section
		const int32 NumSections = Component->ProcMeshSections.Num();

		GSWClipMapBufferHolder.RegisterExtension(GetScene().GetFeatureLevel(), bUseMorphing, SWWorldVersion, MeshComponentVersion, NumSections, Component, PatchData);

		bCastContactShadow = false;

		bAlwaysHasVelocity = false;
		bHasWorldPositionOffsetVelocity = false;
		bHasDeformableMesh = false;
		bEvaluateWorldPositionOffset = bEvaluateWPO;
		bStaticElementsAlwaysUseProxyPrimitiveUniformBuffer = true;

		if(NumSections == 0 || !SWWorldVersion.IsValid() || !MeshComponentVersion.IsValid())
			return;				
		
		Sections.AddZeroed(NumSections);
		for (int SectionIdx = 0; SectionIdx < NumSections; SectionIdx++)
		{
			FGeoCProcMeshSection& SrcSection = Component->ProcMeshSections[SectionIdx];
			if (SrcSection.IndexBuffer.IsValid() && SrcSection.IndexBuffer->Indices.Num() > 0 && SrcSection.ProcVertexBuffer.Num() > 0)
			{
				FProcMeshProxySection* NewSection = new FProcMeshProxySection();

				// Grab material
				NewSection->Material = Component->GetMaterial(SectionIdx);
				if (NewSection->Material == NULL)
				{
					NewSection->Material = UMaterial::GetDefaultMaterial(MD_Surface);
				}

				// Copy visibility info
				NewSection->bSectionVisible = SrcSection.bSectionVisible;

				// Save ref to new section
				Sections[SectionIdx] = NewSection;


			}
		}
	}

	virtual void CreateRenderThreadResources() override
	{
		FPrimitiveSceneProxy::CreateRenderThreadResources();
		// Assign LandscapeUniformShaderParameters
		
		if (MeshComponentVersion.IsValid())
		{
			
			for (int SectionIdx = 0; SectionIdx < Sections.Num(); SectionIdx++)
			{
				FProcMeshProxySection* NewSection = Sections[SectionIdx];
#if RHI_RAYTRACING
				if (IsRayTracingEnabled())
				{
					
					FRayTracingGeometryInitializer Initializer;
					static const FName DebugName("FGeoClipMapSceneProxy");
					Initializer.DebugName = DebugName;
					Initializer.IndexBuffer = nullptr;
					Initializer.TotalPrimitiveCount = 0;
					Initializer.GeometryType = RTGT_Triangles;
					Initializer.bFastBuild = true;
					Initializer.bAllowUpdate = true;

					NewSection->RayTracingGeometry.SetInitializer(Initializer);
					NewSection->RayTracingGeometry.InitResource();

					NewSection->RayTracingGeometry.Initializer.IndexBuffer = GSWClipMapBufferHolder.GetBuffersForSection(SWWorldVersion, SectionIdx)->IndexBuffer.IndexBufferRHI;
					NewSection->RayTracingGeometry.Initializer.TotalPrimitiveCount = GSWClipMapBufferHolder.GetBuffersForSection(SWWorldVersion, SectionIdx)->IndexBuffer.IndicesPtr->Indices.Num() / 3;

					FRayTracingGeometrySegment Segment;
					Segment.VertexBuffer = GSWClipMapBufferHolder.GetSharedProxyBuffers(MeshComponentVersion, SectionIdx)->VertexBuffers.PositionVertexBuffer.VertexBufferRHI;
					Segment.VertexBufferStride = sizeof(FVector3f);
					Segment.VertexBufferElementType = VET_Float3;
					Segment.MaxVertices = GSWClipMapBufferHolder.GetSharedProxyBuffers(MeshComponentVersion, SectionIdx)->VertexBuffers.PositionVertexBuffer.GetNumVertices();
					Segment.NumPrimitives = NewSection->RayTracingGeometry.Initializer.TotalPrimitiveCount;

					NewSection->RayTracingGeometry.Initializer.Segments.Add(Segment);

					//#dxr_todo: add support for segments?

					NewSection->RayTracingGeometry.UpdateRHI();
						
				}
#endif

			}


			FSWPatchParameters Params;
			Params.HeightMap = PatchData->HeightMap->GetRenderTargetResource()->GetRenderTargetTexture();
			Params.NormalMap = PatchData->NormalMap->GetRenderTargetResource()->GetRenderTargetTexture();
			Params.NormalMapSampler = PatchData->NormalMap->GetRenderTargetResource()->SamplerStateRHI;			
			Params.PatchFullSize = (PatchData->N-1.f)* PatchData->LocalGridScaling;
			Params.SmoothLODRange = PatchData->SmoothLODRange;
			Params.SWQuadDistance = PatchData->LocalGridScaling;

			//For Visual Comfort
			FVector2d RelativePatchLocation = FVector2d(SWPatchLocation.X + PatchData->LocalGridScaling, SWPatchLocation.Y + PatchData->LocalGridScaling) / (2.0 * PatchData->LocalGridScaling);
			FIntVector2 AsInt(FMath::RoundToInt(abs(RelativePatchLocation.X)), FMath::RoundToInt(abs(RelativePatchLocation.Y)));

			AsInt.X = AsInt.X % 2;
			AsInt.Y = AsInt.Y % 2;
			
			const uint32 SWOffset = (AsInt.X << 31) | (AsInt.Y << 30) | (static_cast<uint32>(PatchData->SWHeightScale)&0x3FFFFFFF);

			Params.SWQuadOffset = SWOffset;

			FMatrix DynamicLocalToWorld = FMatrix::Identity.ConcatTranslation(PatchData->PatchLocation);
			FMatrix DynamicLocalToWorldNoScaling = DynamicLocalToWorld;
			DynamicLocalToWorldNoScaling.RemoveScaling();

			Params.LocalToWorldNoScaling = FMatrix44f(DynamicLocalToWorldNoScaling);


			SWPatchUniformParameters.SetContents(Params);
			SWPatchUniformParameters.InitResource();
		
			PrimitiveUniformBuffer.SetContents(
				FPrimitiveUniformShaderParametersBuilder{}
				.Defaults()
				.LocalToWorld(DynamicLocalToWorld)
				.PreviousLocalToWorld(DynamicLocalToWorld)
				.ActorWorldPosition(DynamicLocalToWorld.GetOrigin())
				.WorldBounds(GetBounds())
				.LocalBounds(GetLocalBounds())
				.PreSkinnedLocalBounds(FBoxSphereBounds(EForceInit::ForceInit))
				.ReceivesDecals(true)
				.OutputVelocity(false)
				.UseVolumetricLightmap(false)
				.CustomPrimitiveData(nullptr)
				.Build()
			);
			PrimitiveUniformBuffer.InitResource();

			GetScene().UpdateCachedRenderStates(this);

			UpdateDefaultInstanceSceneData();
			
		}		
	}

	virtual void DestroyRenderThreadResources() override
	{
		FPrimitiveSceneProxy::DestroyRenderThreadResources();

		SWPatchUniformParameters.ReleaseResource();
		PrimitiveUniformBuffer.ReleaseResource();

	}

	virtual ~FGeoClipProceduralMeshSceneProxy()
	{
		for (FProcMeshProxySection* Section : Sections)
		{
			if (Section != nullptr)
			{
#if RHI_RAYTRACING
				if (IsRayTracingEnabled())
				{
					Section->RayTracingGeometry.ReleaseResource();
				}
#endif

				delete Section;
			}
		}
	}

	/** Called on render thread to assign new dynamic data */
	void UpdatePatchLocation_RenderThread(FVector NewPatchLocation)
	{
		check(IsInRenderingThread());
		

		SWPatchLocation = NewPatchLocation;

		if(MeshComponentVersion.IsValid() && SWWorldVersion.IsValid())
		{
			bRequireDynamicRendering=true;
			
			FSWPatchParameters Params;
			Params.HeightMap = PatchData->HeightMap->GetRenderTargetResource()->GetRenderTargetTexture();
			Params.NormalMap = PatchData->NormalMap->GetRenderTargetResource()->GetRenderTargetTexture();
			Params.NormalMapSampler = PatchData->NormalMap->GetRenderTargetResource()->SamplerStateRHI;
			Params.PatchFullSize = (PatchData->N - 1.f) * PatchData->LocalGridScaling;
			Params.SmoothLODRange = PatchData->SmoothLODRange;
			Params.SWQuadDistance = PatchData->LocalGridScaling;

			FVector2d RelativePatchLocation= FVector2d(PatchData->PatchLocation.X + PatchData->LocalGridScaling, PatchData->PatchLocation.Y + PatchData->LocalGridScaling) / (2.0 * PatchData->LocalGridScaling);
			FIntVector2 AsInt(FMath::RoundToInt(abs(RelativePatchLocation.X)), FMath::RoundToInt(abs(RelativePatchLocation.Y)));

			AsInt.X = AsInt.X % 2;
			AsInt.Y = AsInt.Y % 2;
			
			const uint32 SWOffset = (AsInt.X << 31) | (AsInt.Y << 30) | (static_cast<uint32>(PatchData->SWHeightScale) & 0x3FFFFFFF);

			Params.SWQuadOffset = SWOffset;

			FMatrix DynamicLocalToWorld = FMatrix::Identity.ConcatTranslation(PatchData->PatchLocation);
			FMatrix DynamicLocalToWorldNoScaling = DynamicLocalToWorld;
			DynamicLocalToWorldNoScaling.RemoveScaling();

			Params.LocalToWorldNoScaling = FMatrix44f(DynamicLocalToWorldNoScaling);

			if (SWPatchUniformParameters.IsInitialized())			
				SWPatchUniformParameters.SetContents(Params);

			
			GetScene().UpdateCachedRenderStates(this);

			UpdateDefaultInstanceSceneData();
			
		}
	}

	/** Called on render thread to assign new dynamic data */
	void UpdateSectionTopology_RenderThread(int32 SectionIndex, int N, float GridScaling, UTextureRenderTarget2D* HeightMap, TEnumAsByte<ERHIFeatureLevel::Type> FeatureLevel_)
	{
		check(IsInRenderingThread());

		if(SectionIndex>=Sections.Num() || !SWWorldVersion.IsValid() || !MeshComponentVersion.IsValid())
		return;

		for(int SectionIdx = 0; SectionIdx < Sections.Num(); SectionIdx++)
		{
			FProcMeshProxySection* Section = Sections[SectionIdx];

			if(!Section->bSectionVisible)
			continue;

			bool bInvalidBuffers = !GSWClipMapBufferHolder.GetBuffersForSection(SWWorldVersion,SectionIdx)->IndexBuffer.IndicesPtr.IsValid();
			bInvalidBuffers |= !GSWClipMapBufferHolder.GetBuffersForSection(SWWorldVersion, SectionIdx)->IndexBufferAlt.IndicesPtr.IsValid();
			bInvalidBuffers |= !GSWClipMapBufferHolder.GetSharedProxyBuffers(MeshComponentVersion, SectionIdx)->IndexBufferOpti.IndicesPtr.IsValid();

			if(bInvalidBuffers/* || !Section->IndexBufferOpti.IndicesPtr.IsValid()*/)
				return;

			if (!(HeightMap->GetRenderTargetResource()))
				return;

			FRHICommandListImmediate& RHICmdList = GRHICommandList.GetImmediateCommandList();
			FRDGBuilder GraphBuilder(RHICmdList);

			{
				RDG_EVENT_SCOPE(GraphBuilder, "SWTopologyUpdate");
				RDG_GPU_STAT_SCOPE(GraphBuilder, ShaderWorldTopologyProcess);
				

				uint32 IndicesCount = GSWClipMapBufferHolder.GetBuffersForSection(SWWorldVersion,SectionIdx)->IndexBuffer.IndicesPtr->Indices.Num() / 3;

				FIntVector GroupCount;
				GroupCount.X = FMath::DivideAndRoundUp((float)IndicesCount, (float)ShaderWorldGPUTools::SW_TopoUpdate_GroupSizeX);
				GroupCount.Y = 1;
				GroupCount.Z = 1;
				/*
				FShaderResourceViewRHIRef Texcoord_SRV = RHICreateShaderResourceView(GSWClipMapBufferHolder.GetBuffersForSection(SWWorldVersion, SectionIdx)->VertexBuffers.StaticMeshVertexBuffer.TexCoordVertexBuffer.VertexBufferRHI, sizeof(float), PF_R32_FLOAT);
				FShaderResourceViewRHIRef IndexA_SRV = RHICreateShaderResourceView(GSWClipMapBufferHolder.GetBuffersForSection(SWWorldVersion,SectionIdx)->IndexBuffer.IndexBufferRHI);
				FShaderResourceViewRHIRef IndexB_SRV = RHICreateShaderResourceView(GSWClipMapBufferHolder.GetBuffersForSection(SWWorldVersion,SectionIdx)->IndexBufferAlt.IndexBufferRHI);

				FUnorderedAccessViewRHIRef Index_UAV = RHICreateUnorderedAccessView(GSWClipMapBufferHolder.GetSharedProxyBuffers(MeshComponentVersion, SectionIdx)->IndexBufferOpti.IndexBufferRHI, PF_R32_UINT);
				*/
				ShaderWorldGPUTools::FTopologyUpdate_CS::FPermutationDomain PermutationVector;
				TShaderMapRef<ShaderWorldGPUTools::FTopologyUpdate_CS> ComputeShader(GetGlobalShaderMap(FeatureLevel_), PermutationVector);
				SetComputePipelineState(RHICmdList, ComputeShader.GetComputeShader());

				ShaderWorldGPUTools::FTopologyUpdate_CS::FParameters* PassParameters = GraphBuilder.AllocParameters<ShaderWorldGPUTools::FTopologyUpdate_CS::FParameters>();
				PassParameters->Pass.IndexLength = IndicesCount;
				PassParameters->Pass.NValue = N;			
				PassParameters->Pass.HeightMap = HeightMap->GetRenderTargetResource()->GetRenderTargetTexture();
				PassParameters->Pass.TexCoordBuffer = RHICreateShaderResourceView(GSWClipMapBufferHolder.GetBuffersForSection(SWWorldVersion, SectionIdx)->VertexBuffers.StaticMeshVertexBuffer.TexCoordVertexBuffer.VertexBufferRHI, sizeof(float), PF_R32_FLOAT);;
				PassParameters->Pass.InputIndexBufferA = RHICreateShaderResourceView(GSWClipMapBufferHolder.GetBuffersForSection(SWWorldVersion, SectionIdx)->IndexBuffer.IndexBufferRHI);;
				PassParameters->Pass.InputIndexBufferB = RHICreateShaderResourceView(GSWClipMapBufferHolder.GetBuffersForSection(SWWorldVersion, SectionIdx)->IndexBufferAlt.IndexBufferRHI);;
				PassParameters->Pass.OutputIndexBuffer = RHICreateUnorderedAccessView(GSWClipMapBufferHolder.GetSharedProxyBuffers(MeshComponentVersion, SectionIdx)->IndexBufferOpti.IndexBufferRHI, PF_R32_UINT);
				;

				GraphBuilder.AddPass(
					RDG_EVENT_NAME("SWShaderToolBox::TopologyUpdate_CS"),
					PassParameters,
					ERDGPassFlags::Compute |
					ERDGPassFlags::NeverCull,
					[PassParameters, ComputeShader, GroupCount](FRHICommandList& RHICmdList)
					{
						FComputeShaderUtils::Dispatch(RHICmdList, ComputeShader, *PassParameters, GroupCount);
					});
			}
			

			GraphBuilder.Execute();


			//RHIUnlockBuffer(GSWClipMapBufferHolder.GetSharedProxyBuffers(MeshComponentVersion, SectionIdx)->IndexBufferOpti.IndexBufferRHI);

			

		}
	}

	/** Called on render thread to assign new dynamic data */
	void UpdateBounds_RenderThread(FBoxSphereBounds NewBounds)
	{
		check(IsInRenderingThread());
		
		OcclusionBounds = NewBounds;
		bHasCustomOcclusionBounds = true;
	}

	void SetSectionVisibility_RenderThread(int32 SectionIndex, bool bNewVisibility)
	{
		check(IsInRenderingThread());

		if(	SectionIndex < Sections.Num() &&
			Sections[SectionIndex] != nullptr)
		{
			bRequireDynamicRendering=true;
			Sections[SectionIndex]->bSectionVisible = bNewVisibility;
			/*
			GetScene().UpdateCachedRenderStates(this);

			UpdateDefaultInstanceSceneData();
			*/
		}
	}

	template<class ArrayType>
	bool GetMeshElementForVirtualTexture(int32 InLodIndex, ERuntimeVirtualTextureMaterialType MaterialType, UMaterialInterface* InMaterialInterface, FMeshBatch& OutMeshBatch, ArrayType& OutStaticBatchParamArray) const
	{
		if (Sections.Num() == 0 || InMaterialInterface == nullptr)
		{
			return false;
		}

		const FProcMeshProxySection* Section = Sections[0];

		OutMeshBatch.VertexFactory = &GSWClipMapBufferHolder.GetSharedProxyBuffers(MeshComponentVersion, 0)->VertexFactoryNoMorphing;//&Section->VertexFactoryNoMorphing;///*Section->UseRuntimeVF ? &Section->RuntimeVertexFactory :*/ &Section->VertexFactory;
		OutMeshBatch.MaterialRenderProxy = InMaterialInterface->GetRenderProxy();
		OutMeshBatch.ReverseCulling = IsLocalToWorldDeterminantNegative();
		OutMeshBatch.CastShadow = false;
		OutMeshBatch.bUseForDepthPass = false;
		OutMeshBatch.bUseAsOccluder = false;
		OutMeshBatch.bUseForMaterial = false;
		OutMeshBatch.Type = PT_TriangleList;
		OutMeshBatch.DepthPriorityGroup = SDPG_World;
		OutMeshBatch.LODIndex = InLodIndex;
		OutMeshBatch.bDitheredLODTransition = false;
		OutMeshBatch.bRenderToVirtualTexture = true;
		OutMeshBatch.RuntimeVirtualTextureMaterialType = (uint32)MaterialType;

		OutMeshBatch.Elements.Empty(1);

		FSWPatchBatchElementParams* BatchElementParams = new(OutStaticBatchParamArray) FSWPatchBatchElementParams;
		BatchElementParams->SWPatchUniformParametersResource = &SWPatchUniformParameters;

		FMeshBatchElement BatchElement;
		BatchElement.UserData = BatchElementParams;

		BatchElement.IndexBuffer = &GSWClipMapBufferHolder.GetBuffersForSection(SWWorldVersion,0)->IndexBuffer;
		BatchElement.NumPrimitives = GSWClipMapBufferHolder.GetBuffersForSection(SWWorldVersion,0)->IndexBuffer.IndicesPtr.IsValid() ? GSWClipMapBufferHolder.GetBuffersForSection(SWWorldVersion,0)->IndexBuffer.IndicesPtr->Indices.Num() / 3 : 0;
		BatchElement.MinVertexIndex = 0;
		BatchElement.MaxVertexIndex = GSWClipMapBufferHolder.GetSharedProxyBuffers(MeshComponentVersion, 0)->VertexBuffers.PositionVertexBuffer.GetNumVertices() - 1;
		BatchElement.FirstIndex = 0;

		OutMeshBatch.Elements.Add(BatchElement);

		return true;

	}

	template<class ArrayType>
	bool GetStaticMeshElement(FProcMeshProxySection* Section,int32 SectionIdx, int32 LODIndex, bool bForToolMesh, bool bForcedLOD, UMaterialInterface* InMaterialInterface, FMeshBatch& Mesh, ArrayType& OutStaticBatchParamArray) const
	{
		UMaterialInterface* MaterialInterface = InMaterialInterface;
		
		if (!MaterialInterface)
		{
			return false;
		}		

		if (bUseMorphing)
			Mesh.VertexFactory = &GSWClipMapBufferHolder.GetSharedProxyBuffers(MeshComponentVersion, SectionIdx)->VertexFactoryMorphing;// &Section->VertexFactoryMorphing;
		else
			Mesh.VertexFactory = &GSWClipMapBufferHolder.GetSharedProxyBuffers(MeshComponentVersion, SectionIdx)->VertexFactoryNoMorphing;//&Section->VertexFactoryNoMorphing;

		Mesh.MaterialRenderProxy = MaterialInterface->GetRenderProxy();

		Mesh.LCI = nullptr;
		Mesh.ReverseCulling = IsLocalToWorldDeterminantNegative();
		Mesh.CastShadow = true;
		Mesh.bUseForDepthPass = true;
		Mesh.bUseAsOccluder = ShouldUseAsOccluder() && GetScene().GetShadingPath() == EShadingPath::Deferred && !IsMovable();
		Mesh.bUseForMaterial = true;
		Mesh.Type = PT_TriangleList;
		Mesh.DepthPriorityGroup = SDPG_World;
		Mesh.LODIndex = 0;
		Mesh.bDitheredLODTransition = false;

		FSWPatchBatchElementParams* BatchElementParams = new(OutStaticBatchParamArray) FSWPatchBatchElementParams;
		BatchElementParams->SWPatchUniformParametersResource = &SWPatchUniformParameters;

		FMeshBatchElement& BatchElement = Mesh.Elements[0];
		BatchElement.UserData = BatchElementParams;

		BatchElement.IndexBuffer = &GSWClipMapBufferHolder.GetSharedProxyBuffers(MeshComponentVersion, SectionIdx)->IndexBufferOpti;
		BatchElement.NumPrimitives = GSWClipMapBufferHolder.GetBuffersForSection(SWWorldVersion, SectionIdx)->IndexBuffer.IndicesPtr.IsValid() ? GSWClipMapBufferHolder.GetBuffersForSection(SWWorldVersion, SectionIdx)->IndexBuffer.IndicesPtr->Indices.Num() / 3 : 0;
		BatchElement.FirstIndex = 0;
		BatchElement.MinVertexIndex = 0;
		BatchElement.MaxVertexIndex = GSWClipMapBufferHolder.GetSharedProxyBuffers(MeshComponentVersion, SectionIdx)->VertexBuffers.PositionVertexBuffer.GetNumVertices() - 1;

		if (bUseMorphing)
			BatchElement.VertexFactoryUserData = GSWClipMapBufferHolder.GetSharedProxyBuffers(MeshComponentVersion, SectionIdx)->VertexFactoryMorphing.GetUniformBuffer();
		else
			BatchElement.VertexFactoryUserData = GSWClipMapBufferHolder.GetSharedProxyBuffers(MeshComponentVersion, SectionIdx)->VertexFactoryNoMorphing.GetUniformBuffer();

		
		BatchElement.MaxScreenSize = 0.0f;
		BatchElement.MinScreenSize = -1.0f;

		return true;
	}



	virtual int32 GetNumMeshBatches() const
	{
		int Batchc = 0;
		for (const FProcMeshProxySection* Section : Sections)
			if (Section != nullptr && Section->bSectionVisible && Section->Material)
				Batchc++;

		return Batchc;
	}

	virtual void DrawStaticElements(FStaticPrimitiveDrawInterface* PDI) override
	{
		//return;
		//checkSlow(IsInParallelRenderingThread());

		if ( !SWWorldVersion.IsValid() || !MeshComponentVersion.IsValid())
			return;

		{
		
			if (Sections.Num() == 0 || !Sections[0]->Material )
			{
				return;
			}

			
			int32 NumBatches = Sections.Num() + 1;
			//NumBatches += 1; //virtual textures


			StaticBatchParamArray.Empty(NumBatches);
			PDI->ReserveMemoryForMeshes(NumBatches);
			
			for (ERuntimeVirtualTextureMaterialType MaterialType : RuntimeVirtualTextureMaterialTypes)
			{

				const int32 MaterialIndex = 0;

				for (int32 LODIndex = 0; LODIndex < 1; ++LODIndex)
				{
					FMeshBatch RuntimeVirtualTextureMeshBatch;
					if (GetMeshElementForVirtualTexture(LODIndex, MaterialType, Sections[0]->Material, RuntimeVirtualTextureMeshBatch, StaticBatchParamArray))
					{
						PDI->DrawMesh(RuntimeVirtualTextureMeshBatch, FLT_MAX);
					}
				}
			}

			// Iterate over sections
			for (int SectionIdx = 0; SectionIdx < Sections.Num(); SectionIdx++)
			{
				const FProcMeshProxySection* Section = Sections[SectionIdx];
	
				if (Section != nullptr && Section->bSectionVisible && Section->Material)
				{

					FMeshBatch MeshBatch;

					if (GetStaticMeshElement(const_cast<FProcMeshProxySection*>(Section), SectionIdx, 0, false, false, Section->Material, MeshBatch, StaticBatchParamArray))
					{
						PDI->DrawMesh(MeshBatch, FLT_MAX);
					}

				}
			}
		}
		
	}


	virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const override
	{
		SCOPE_CYCLE_COUNTER(STAT_GeoClipProcMesh_GetMeshElements);
		
		if(!SWWorldVersion.IsValid() || !MeshComponentVersion.IsValid())
			return;

		//if(!GSWClipMapBufferHolder.GetPrimitiveBuffers(MeshComponentVersion) /*|| !GSWClipMapBufferHolder.GetPrimitiveBuffers(MeshComponentVersion)->SWPatchUniformParameters.IsInitialized()*/)
		//	return;

		// Set up wireframe material (if needed)
		const bool bWireframe = AllowDebugViewmodes() && ViewFamily.EngineShowFlags.Wireframe;

		FColoredMaterialRenderProxy* WireframeMaterialInstance = NULL;
		if (bWireframe)
		{
			const FColor Terrain(50,50,50);
			const FColor Ocean(73, 78, 82);

			WireframeMaterialInstance = new FColoredMaterialRenderProxy(
				GEngine->WireframeMaterial ? GEngine->WireframeMaterial->GetRenderProxy() : NULL,
			(bDynamicTopology?	FLinearColor(Terrain) : FLinearColor(Ocean))
			);

			Collector.RegisterOneFrameMaterialProxy(WireframeMaterialInstance);
		}

		if (!ViewFrustumAccessMutex)		
			ViewsFrustums.Empty(Views.Num());	
			
		

		// Iterate over sections
		for (int SectionIdx = 0; SectionIdx < Sections.Num(); SectionIdx++)
		{
			FProcMeshProxySection* Section = Sections[SectionIdx];

			if (Section != nullptr && Section->bSectionVisible && Section->Material)
			{
				FMaterialRenderProxy* MaterialProxy = bWireframe ? WireframeMaterialInstance : Section->Material->GetRenderProxy();

				bool bUseForDepthPass = true;
				// If there's a valid material, use that to figure out the depth pass status
				if (const FMaterial* BucketMaterial = MaterialProxy->GetMaterialNoFallback(GetScene().GetFeatureLevel()))
				{
					// Preemptively turn off depth rendering for this mesh batch if the material doesn't need it
					bUseForDepthPass = !BucketMaterial->GetShadingModels().HasShadingModel(MSM_SingleLayerWater) && BucketMaterial->GetBlendMode() != EBlendMode::BLEND_Translucent;
				}
				// For each view..
				for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
				{
					if (VisibilityMap & (1 << ViewIndex))
					{
						const FSceneView* View = Views[ViewIndex];

						if (!ViewFrustumAccessMutex && View)													
							ViewsFrustums.Add(View->ViewFrustum);

						
						// Draw the mesh.
						FMeshBatch& Mesh = Collector.AllocateMesh();
						FMeshBatchElement& BatchElement = Mesh.Elements[0];
						//BatchElement.IndexBuffer = &Section->IndexBufferOpti;
						BatchElement.IndexBuffer = &GSWClipMapBufferHolder.GetSharedProxyBuffers(MeshComponentVersion, SectionIdx)->IndexBufferOpti;

						BatchElement.PrimitiveIdMode = PrimID_DynamicPrimitiveShaderData;
						Mesh.bWireframe = bWireframe;

						if (bUseMorphing)
							Mesh.VertexFactory = &GSWClipMapBufferHolder.GetSharedProxyBuffers(MeshComponentVersion, SectionIdx)->VertexFactoryMorphing;// &Section->VertexFactoryMorphing;
						else
							Mesh.VertexFactory = &GSWClipMapBufferHolder.GetSharedProxyBuffers(MeshComponentVersion, SectionIdx)->VertexFactoryNoMorphing;//&Section->VertexFactoryNoMorphing;

						Mesh.MaterialRenderProxy = MaterialProxy;

						FMatrix DynamicLocalToWorld = FMatrix::Identity.ConcatTranslation(PatchData->PatchLocation /*SWPatchLocation*/);
						FMatrix DynamicLocalToWorldNoScaling = DynamicLocalToWorld;
						DynamicLocalToWorldNoScaling.RemoveScaling();

						FSWPatchBatchElementParamArray& ParameterArray = Collector.AllocateOneFrameResource<FSWPatchBatchElementParamArray>();
						
						FSWPatchBatchElementParams* BatchElementParams = new(ParameterArray.ElementParams) FSWPatchBatchElementParams;
						BatchElementParams->SWPatchUniformParametersResource = &SWPatchUniformParameters;
						//BatchElementParams->SWPatchUniformParametersResource = &GSWClipMapBufferHolder.GetPrimitiveBuffers(MeshComponentVersion)->SWPatchUniformParameters;
						BatchElement.UserData = BatchElementParams;				
						
						BatchElement.PrimitiveUniformBufferResource = &PrimitiveUniformBuffer;

						if (bUseMorphing)
							BatchElement.VertexFactoryUserData = GSWClipMapBufferHolder.GetSharedProxyBuffers(MeshComponentVersion, SectionIdx)->VertexFactoryMorphing.GetUniformBuffer();//(Section->VertexFactoryMorphing).GetUniformBuffer();
						else
							BatchElement.VertexFactoryUserData = GSWClipMapBufferHolder.GetSharedProxyBuffers(MeshComponentVersion, SectionIdx)->VertexFactoryNoMorphing.GetUniformBuffer();//(Section->VertexFactoryNoMorphing).GetUniformBuffer();

						BatchElement.FirstIndex = 0;
						BatchElement.NumPrimitives = GSWClipMapBufferHolder.GetBuffersForSection(SWWorldVersion,SectionIdx)->IndexBuffer.IndicesPtr.IsValid() ? GSWClipMapBufferHolder.GetBuffersForSection(SWWorldVersion,SectionIdx)->IndexBuffer.IndicesPtr->Indices.Num() / 3 : 0;
						BatchElement.MinVertexIndex = 0;
						BatchElement.MaxVertexIndex = GSWClipMapBufferHolder.GetSharedProxyBuffers(MeshComponentVersion, SectionIdx)->VertexBuffers.PositionVertexBuffer.GetNumVertices() - 1;// Section->VertexBuffers.PositionVertexBuffer.GetNumVertices() - 1;
						Mesh.ReverseCulling = IsLocalToWorldDeterminantNegative();
						Mesh.Type = PT_TriangleList;
						Mesh.DepthPriorityGroup = SDPG_World;
						Mesh.bUseForMaterial = true;
						Mesh.bCanApplyViewModeOverrides = IsSelected();
						// Preemptively turn off depth rendering for this mesh batch if the material doesn't need it
						Mesh.bUseForDepthPass = bUseForDepthPass;
						Mesh.bUseAsOccluder = bUseForDepthPass;
						Mesh.bUseWireframeSelectionColoring = IsSelected();
						Mesh.CastShadow = MeshCastShadows;
						Collector.AddMesh(ViewIndex, Mesh);
					}
				}
			}
		}
		
		if (!ViewFrustumAccessMutex)
			ViewFrustumAccessMutex = true;		

		// Draw bounds
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
		{
			if (VisibilityMap & (1 << ViewIndex))
			{
				// Render bounds
				RenderBounds(Collector.GetPDI(ViewIndex), ViewFamily.EngineShowFlags, GetBounds(), IsSelected());
			}
		}
#endif
	}

	virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override
	{
		FPrimitiveViewRelevance Result;
		Result.bDrawRelevance = IsShown(View);
		Result.bShadowRelevance = IsShadowCast(View);
		
		if (IsRichView(*View->Family)
		|| View->Family->EngineShowFlags.Wireframe
		|| IsSelected())
		{
			Result.bDynamicRelevance = true;
		}
		else
			Result.bStaticRelevance = true;
			
		Result.bDynamicRelevance = true;
	

		Result.bRenderInMainPass = ShouldRenderInMainPass();
		Result.bUsesLightingChannels = GetLightingChannelMask() != GetDefaultLightingChannelMask();
		Result.bRenderCustomDepth = ShouldRenderCustomDepth();
		Result.bTranslucentSelfShadow = bCastVolumetricTranslucentShadow;
		MaterialRelevance.SetPrimitiveViewRelevance(Result);
		Result.bVelocityRelevance = false;
		Result.bOutputsTranslucentVelocity = false;

		return Result;
	}

	virtual bool CanBeOccluded() const override
	{
		return !MaterialRelevance.bDisableDepthTest;
	}


	virtual uint32 GetMemoryFootprint(void) const
	{
		return(sizeof(*this) + GetAllocatedSize());
	}

	uint32 GetAllocatedSize(void) const
	{
		return(FPrimitiveSceneProxy::GetAllocatedSize());
	}


#if RHI_RAYTRACING
	virtual bool IsRayTracingRelevant() const override { return true; }

	virtual bool HasRayTracingRepresentation() const override { return true; }

	virtual void GetDynamicRayTracingInstances(FRayTracingMaterialGatheringContext& Context, TArray<FRayTracingInstance>& OutRayTracingInstances) override final
	{
		if (!CVarRayTracingGeoClipProceduralMesh.GetValueOnRenderThread())
		{
			return;
		}

		for (int32 SegmentIndex = 0; SegmentIndex < Sections.Num(); ++SegmentIndex)
		{
			const FProcMeshProxySection* Section = Sections[SegmentIndex];
			if (Section != nullptr && Section->bSectionVisible)
			{
				FMaterialRenderProxy* MaterialProxy = Section->Material->GetRenderProxy();
				
				if (Section->RayTracingGeometry.RayTracingGeometryRHI.IsValid())
				{
					const int8 SectionIdx = SegmentIndex;

					check(Section->RayTracingGeometry.Initializer.IndexBuffer.IsValid());

					FMeshBatch MeshBatch;

					MeshBatch.VertexFactory = &GSWClipMapBufferHolder.GetSharedProxyBuffers(MeshComponentVersion, SegmentIndex)->VertexFactoryNoMorphing ;//&Section->VertexFactory;
					MeshBatch.SegmentIndex = 0;
					MeshBatch.MaterialRenderProxy = Section->Material->GetMaterial()->GetRenderProxy();
					MeshBatch.ReverseCulling = IsLocalToWorldDeterminantNegative();
					MeshBatch.Type = PT_TriangleList;
					MeshBatch.DepthPriorityGroup = SDPG_World;
					MeshBatch.bCanApplyViewModeOverrides = false;
					MeshBatch.CastRayTracedShadow = IsShadowCast(Context.ReferenceView);

					FMeshBatchElement& BatchElement = MeshBatch.Elements[0];
					
					FSWPatchBatchElementParamArray& ParameterArray = Context.RayTracingMeshResourceCollector.AllocateOneFrameResource<FSWPatchBatchElementParamArray>();

					FSWPatchBatchElementParams* BatchElementParams = new(ParameterArray.ElementParams) FSWPatchBatchElementParams;
					BatchElementParams->SWPatchUniformParametersResource = &SWPatchUniformParameters;
					BatchElement.UserData = BatchElementParams;

					BatchElement.PrimitiveUniformBufferResource = &PrimitiveUniformBuffer;

					BatchElement.VertexFactoryUserData = GSWClipMapBufferHolder.GetSharedProxyBuffers(MeshComponentVersion, SectionIdx)->VertexFactoryNoMorphing.GetUniformBuffer();//(Section->VertexFactoryNoMorphing).GetUniformBuffer();


					FMatrix DynamicLocalToWorld = FMatrix::Identity.ConcatTranslation(SWPatchLocation);

					BatchElement.IndexBuffer = &GSWClipMapBufferHolder.GetBuffersForSection(SWWorldVersion, SegmentIndex)->IndexBuffer;//&Section->IndexBufferOpti;
					BatchElement.FirstIndex = 0;
					BatchElement.NumPrimitives = GSWClipMapBufferHolder.GetBuffersForSection(SWWorldVersion, SegmentIndex)->IndexBuffer.IndicesPtr.IsValid() ? GSWClipMapBufferHolder.GetBuffersForSection(SWWorldVersion, SegmentIndex)->IndexBuffer.IndicesPtr->Indices.Num() / 3 : 0; //GSWClipMapBufferHolder.GetBuffersForSection(ID, SectionIdx)->IndexBuffer.IndicesPtr.IsValid() ? GSWClipMapBufferHolder.GetBuffersForSection(ID, SectionIdx)->IndexBuffer.IndicesPtr->Indices.Num() / 3 : 0;
					BatchElement.MinVertexIndex = 0;
					BatchElement.MaxVertexIndex = GSWClipMapBufferHolder.GetSharedProxyBuffers(MeshComponentVersion, SectionIdx)->VertexBuffers.PositionVertexBuffer.GetNumVertices() - 1;

					FRayTracingInstance RayTracingInstance;
					RayTracingInstance.Geometry = &Section->RayTracingGeometry;
					RayTracingInstance.InstanceTransforms.Add(DynamicLocalToWorld);
					RayTracingInstance.Materials.Add(MeshBatch);

					RayTracingInstance.BuildInstanceMaskAndFlags(GetScene().GetFeatureLevel());
					OutRayTracingInstances.Add(RayTracingInstance);
				}
			}
		}
	}
	
#endif

private:
	/** Array of sections */
	TArray < FProcMeshProxySection*> Sections;

	UBodySetup* BodySetup;

	FMaterialRelevance MaterialRelevance;

	TSharedPtr < FSWPatchUniformData, ESPMode::ThreadSafe > PatchData;

	FVector SWPatchLocation;

	bool bDynamicTopology = false;
	mutable bool bRequireDynamicRendering = false;
	bool MeshCastShadows = false;
	const TSharedPtr<FSWShareableID, ESPMode::ThreadSafe> SWWorldVersion;
	const TSharedPtr<FSWShareableID, ESPMode::ThreadSafe> MeshComponentVersion;

	bool bUseMorphing = false;
	bool bEvaluateWPO = false;

	bool	bHasCustomOcclusionBounds;

	FBoxSphereBounds OcclusionBounds;
};

//////////////////////////////////////////////////////////////////////////


void FSWDynamicMeshIndexBuffer32::InitRHI()
{
	if(IndicesPtr.IsValid())
	{
		FRHIResourceCreateInfo CreateInfo(TEXT("FDynamicMeshIndexBuffer32"));
		IndexBufferRHI = RHICreateIndexBuffer(sizeof(uint32), IndicesPtr->Indices.Num() * sizeof(uint32), BUF_UnorderedAccess | BUF_ShaderResource, CreateInfo);

		UpdateRHI();
	}
}

void FSWDynamicMeshIndexBuffer32::UpdateRHI()
{
	// Copy the index data into the index buffer.
	void* Buffer = RHILockBuffer(IndexBufferRHI, 0, IndicesPtr->Indices.Num() * sizeof(uint32), RLM_WriteOnly);
	FMemory::Memcpy(Buffer, IndicesPtr->Indices.GetData(), IndicesPtr->Indices.Num() * sizeof(uint32));
	RHIUnlockBuffer(IndexBufferRHI);
}

UGeoClipmapMeshComponent::UGeoClipmapMeshComponent(const FObjectInitializer& ObjectInitializer)
: Super(ObjectInitializer)
{
	//bUseComplexAsSimpleCollision = true;
	bNeverDistanceCull = true;
}

void UGeoClipmapMeshComponent::PostLoad()
{
	Super::PostLoad();

	if (ProcMeshBodySetup && IsTemplate())
	{
		ProcMeshBodySetup->SetFlags(RF_Public);
	}
}

TArray<FConvexVolume> UGeoClipmapMeshComponent::GetViewsFrustums()
{
	if (SceneProxy && !IsRenderStateDirty())
	{
		FGeoClipProceduralMeshSceneProxy* ProcMeshSceneProxy = (FGeoClipProceduralMeshSceneProxy*)SceneProxy;

		if(ProcMeshSceneProxy && ProcMeshSceneProxy->ViewFrustumAccessMutex)
		{
			ViewsFrustums = ProcMeshSceneProxy->ViewsFrustums;	
			ProcMeshSceneProxy->ViewFrustumAccessMutex=false;
		}		
	}

	return ViewsFrustums;
}

void UGeoClipmapMeshComponent::SetPatchData(UTextureRenderTarget2D* HeightMap_, UTextureRenderTarget2D* NormalMap_, FVector PatchLocation_, float PatchFullSize_, float SWHeightScale_, float SmoothLODRange_
	, float MeshScale_, float N_, float LocalGridScaling_, float CacheRes_, bool UseMorphing)
{
	PatchData = MakeShared<FSWPatchUniformData>(HeightMap_, NormalMap_, PatchLocation_, PatchFullSize_, SWHeightScale_, SmoothLODRange_, MeshScale_, N_, LocalGridScaling_, CacheRes_, UseMorphing);
};

void UGeoClipmapMeshComponent::UpdatePatchDataLODSmoothTransition(float NewTransition)
{	
	if (PatchData.IsValid())
	{
		FGeoClipProceduralMeshSceneProxy* ProcMeshSceneProxy = (FGeoClipProceduralMeshSceneProxy*)(SceneProxy && !IsRenderStateDirty()? SceneProxy:nullptr);
		ENQUEUE_RENDER_COMMAND(FSWTransitionUpdate)
			([Data = PatchData, NewTransition, ProcMeshSceneProxy](FRHICommandListImmediate& RHICmdList)
			{
				Data->SmoothLODRange = NewTransition;
				
				if(ProcMeshSceneProxy)
					ProcMeshSceneProxy->UpdatePatchLocation_RenderThread(Data->PatchLocation);
			});
	}	
}

void UGeoClipmapMeshComponent::CreateMeshSection(int32 SectionIndex, const TArray<FVector3f>& Vertices, const TSharedPtr<FSWShareableIndexBuffer>& Triangles, const TSharedPtr<FSWShareableIndexBuffer>& TrianglesAlt, const TArray<FVector>& Normals, const TArray<FVector2f>& UV0, const TArray<FVector2f>& UV1, const TArray<FVector2f>& UV2, const TArray<FVector2f>& UV3, const TArray<FColor>& VertexColors, const TArray<FGeoCProcMeshTangent>& Tangents, bool bCreateCollision)
{
	SCOPE_CYCLE_COUNTER(STAT_GeoClipProcMesh_CreateMeshSection);

	// Ensure sections array is long enough
	if (SectionIndex >= ProcMeshSections.Num())
	{
		ProcMeshSections.SetNum(SectionIndex + 1, false);
	}

	// Reset this section (in case it already existed)
	FGeoCProcMeshSection& NewSection = ProcMeshSections[SectionIndex];
	NewSection.Reset();

	NewSection.IndexBuffer = Triangles;
	NewSection.IndexBufferAlt = TrianglesAlt;

	// Copy data to vertex buffer
	const int32 NumVerts = Vertices.Num();
	NewSection.ProcVertexBuffer.Reset();
	NewSection.ProcVertexBuffer.AddUninitialized(NumVerts);
	for (int32 VertIdx = 0; VertIdx < NumVerts; VertIdx++)
	{
		FGeoCProcMeshVertex& Vertex = NewSection.ProcVertexBuffer[VertIdx];

		Vertex.Position = Vertices[VertIdx];
		Vertex.Normal = (Normals.Num() == NumVerts) ? Normals[VertIdx] : FVector(0.f, 0.f, 1.f);
		Vertex.UV0 = (UV0.Num() == NumVerts) ? UV0[VertIdx] : FVector2f(0.f, 0.f);
		Vertex.UV1 = (UV1.Num() == NumVerts) ? UV1[VertIdx] : FVector2f(0.f, 0.f);
		Vertex.UV2 = (UV2.Num() == NumVerts) ? UV2[VertIdx] : FVector2f(0.f, 0.f);
		Vertex.UV3 = (UV3.Num() == NumVerts) ? UV3[VertIdx] : FVector2f(0.f, 0.f);
		Vertex.Color = (VertexColors.Num() == NumVerts) ? VertexColors[VertIdx] : FColor(255, 255, 255);
		Vertex.Tangent = (Tangents.Num() == NumVerts) ? Tangents[VertIdx] : FGeoCProcMeshTangent();

		// Update bounding box
		NewSection.SectionLocalBox += (FVector3d)Vertex.Position;
	}

	NewSection.bEnableCollision = bCreateCollision;

	UpdateLocalBounds(); // Update overall bounds
	MarkRenderStateDirty(); // New section requires recreating scene proxy
}


void UGeoClipmapMeshComponent::UpdateCustomBounds(FBoxSphereBounds Newbound)
{
	UseCustomBounds=true;
	LocalBoundsGeoC = Newbound;
	// If we have a valid proxy and it is not pending recreation
	//if (SceneProxy && !IsRenderStateDirty())
	{

		// Enqueue command to send to render thread
		FGeoClipProceduralMeshSceneProxy* ProcMeshSceneProxy = (FGeoClipProceduralMeshSceneProxy*)(SceneProxy && !IsRenderStateDirty() ? SceneProxy : nullptr);
		ENQUEUE_RENDER_COMMAND(FGeoCProcMeshSectionUpdate)
			([ProcMeshSceneProxy,Newbound](FRHICommandListImmediate& RHICmdList)
			{
				if (ProcMeshSceneProxy)
					ProcMeshSceneProxy->UpdateBounds_RenderThread(Newbound);
			});
	}

	UpdateLocalBounds();		 // Update overall bounds
	MarkRenderTransformDirty();  // Need to send new bounds to render thread

}

void UGeoClipmapMeshComponent::UpdateSectionTopology(int32 SectionIndex, int N, float GridScaling, UTextureRenderTarget2D* HeightMap)
{	
	if (SectionIndex < ProcMeshSections.Num() && HeightMap)
	{
		//if (SceneProxy)
		{
			// Enqueue command to modify render thread info
			FGeoClipProceduralMeshSceneProxy* ProcMeshSceneProxy = (FGeoClipProceduralMeshSceneProxy*)(SceneProxy && !IsRenderStateDirty() ? SceneProxy : nullptr);

			ENQUEUE_RENDER_COMMAND(FGeoCProcMeshSectionUpdate)
				([ProcMeshSceneProxy, SectionIndex, N, GridScaling, HeightMap, FL = GetWorld()->FeatureLevel](FRHICommandListImmediate& RHICmdList)
				{
					if (ProcMeshSceneProxy)
						ProcMeshSceneProxy->UpdateSectionTopology_RenderThread(SectionIndex, N, GridScaling, HeightMap, FL);
				});

		}
	}
}

void UGeoClipmapMeshComponent::UpdatePatchLocation(const FVector& NewPatchLocation)
{
	//if (SceneProxy)
	{
		// Enqueue command to modify render thread info
		FGeoClipProceduralMeshSceneProxy* ProcMeshSceneProxy = (FGeoClipProceduralMeshSceneProxy*)(SceneProxy && !IsRenderStateDirty() ? SceneProxy : nullptr);

		ENQUEUE_RENDER_COMMAND(FGeoCProcMeshSectionUpdate)
			([Data = PatchData, ProcMeshSceneProxy, NewPatchLocation](FRHICommandListImmediate& RHICmdList)
			{
				if(Data.IsValid())
					Data->PatchLocation = NewPatchLocation;
				if(ProcMeshSceneProxy)
					ProcMeshSceneProxy->UpdatePatchLocation_RenderThread(NewPatchLocation);
			});

	}
}

void UGeoClipmapMeshComponent::ClearMeshSection(int32 SectionIndex)
{
	if (SectionIndex < ProcMeshSections.Num())
	{
		ProcMeshSections[SectionIndex].Reset();
		UpdateLocalBounds();
		MarkRenderStateDirty();
	}
}

void UGeoClipmapMeshComponent::ClearAllMeshSections()
{
	ProcMeshSections.Empty();
	UpdateLocalBounds();
	MarkRenderStateDirty();
}

void UGeoClipmapMeshComponent::SetMeshSectionVisible(int32 SectionIndex, bool bNewVisibility)
{
	if(SectionIndex < ProcMeshSections.Num())
	{
		// Set game thread state
		ProcMeshSections[SectionIndex].bSectionVisible = bNewVisibility;

		
		//if (SceneProxy)
		{
			// Enqueue command to modify render thread info
			/*
			 *
			FGeoClipProceduralMeshSceneProxy* ProcMeshSceneProxy = (FGeoClipProceduralMeshSceneProxy*)(SceneProxy && !IsRenderStateDirty() ? SceneProxy : nullptr);

			ENQUEUE_RENDER_COMMAND(FGeoCProcMeshSectionVisibilityUpdate)(
				[ProcMeshSceneProxy, SectionIndex, bNewVisibility](FRHICommandListImmediate& RHICmdList)
				{
					if(ProcMeshSceneProxy)
						ProcMeshSceneProxy->SetSectionVisibility_RenderThread(SectionIndex, bNewVisibility);
				});
			*/
		}
		
		MarkRenderStateDirty();
		
	}
}

bool UGeoClipmapMeshComponent::IsMeshSectionVisible(int32 SectionIndex) const
{
	return (SectionIndex < ProcMeshSections.Num()) ? ProcMeshSections[SectionIndex].bSectionVisible : false;
}

int32 UGeoClipmapMeshComponent::GetNumSections() const
{
	return ProcMeshSections.Num();
}

void UGeoClipmapMeshComponent::UpdateLocalBounds()
{
	FBox LocalBox(ForceInit);

	for (const FGeoCProcMeshSection& Section : ProcMeshSections)
	{
		LocalBox += Section.SectionLocalBox;
	}

	LocalBounds = UseCustomBounds? LocalBoundsGeoC: (LocalBox.IsValid ? FBoxSphereBounds(LocalBox) : FBoxSphereBounds(FVector(0, 0, 0), FVector(0, 0, 0), 0)); // fallback to reset box sphere bounds

	// Update global bounds
	UpdateBounds();
	// Need to send to render thread
	MarkRenderTransformDirty();
}

FPrimitiveSceneProxy* UGeoClipmapMeshComponent::CreateSceneProxy()
{
	SCOPE_CYCLE_COUNTER(STAT_GeoClipProcMesh_CreateSceneProxy);

	return new FGeoClipProceduralMeshSceneProxy(this);
}

int32 UGeoClipmapMeshComponent::GetNumMaterials() const
{
	return ProcMeshSections.Num();
}


FGeoCProcMeshSection* UGeoClipmapMeshComponent::GetProcMeshSection(int32 SectionIndex)
{
	if (SectionIndex < ProcMeshSections.Num())
	{
		return &ProcMeshSections[SectionIndex];
	}
	else
	{
		return nullptr;
	}
}


void UGeoClipmapMeshComponent::SetProcMeshSection(int32 SectionIndex, const FGeoCProcMeshSection& Section)
{
	// Ensure sections array is long enough
	if (SectionIndex >= ProcMeshSections.Num())
	{
		ProcMeshSections.SetNum(SectionIndex + 1, false);
	}

	ProcMeshSections[SectionIndex] = Section;

	UpdateLocalBounds(); // Update overall bounds
	MarkRenderStateDirty(); // New section requires recreating scene proxy
}

FBoxSphereBounds UGeoClipmapMeshComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	FBoxSphereBounds Ret(LocalBounds.TransformBy(LocalToWorld));

	Ret.BoxExtent *= BoundsScale;
	Ret.SphereRadius *= BoundsScale;

	return Ret;
}

UBodySetup* UGeoClipmapMeshComponent::CreateBodySetupHelper()
{
	// The body setup in a template needs to be public since the property is Tnstanced and thus is the archetype of the instance meaning there is a direct reference
	UBodySetup* NewBodySetup = NewObject<UBodySetup>(this, NAME_None, (IsTemplate() ? RF_Public | RF_ArchetypeObject : RF_NoFlags));
	NewBodySetup->BodySetupGuid = FGuid::NewGuid();

	NewBodySetup->bGenerateMirroredCollision = false;
	NewBodySetup->bDoubleSidedGeometry = true;
	NewBodySetup->CollisionTraceFlag = /*bUseComplexAsSimpleCollision ? CTF_UseComplexAsSimple :*/ CTF_UseDefault;

	return NewBodySetup;
}

void UGeoClipmapMeshComponent::CreateProcMeshBodySetup()
{
	if (ProcMeshBodySetup == nullptr)
	{
		ProcMeshBodySetup = CreateBodySetupHelper();
	}
}

UBodySetup* UGeoClipmapMeshComponent::GetBodySetup()
{
	CreateProcMeshBodySetup();
	return ProcMeshBodySetup;
}

UMaterialInterface* UGeoClipmapMeshComponent::GetMaterialFromCollisionFaceIndex(int32 FaceIndex, int32& SectionIndex) const
{
	UMaterialInterface* Result = nullptr;
	SectionIndex = 0;

	if (FaceIndex >= 0)
	{
		// Look for element that corresponds to the supplied face
		int32 TotalFaceCount = 0;
		for (int32 SectionIdx = 0; SectionIdx < ProcMeshSections.Num(); SectionIdx++)
		{
			const FGeoCProcMeshSection& Section = ProcMeshSections[SectionIdx];
			int32 NumFaces = Section.IndexBuffer->Indices.Num() / 3;
			TotalFaceCount += NumFaces;

			if (FaceIndex < TotalFaceCount)
			{
				// Grab the material
				Result = GetMaterial(SectionIdx);
				SectionIndex = SectionIdx;
				break;
			}
		}
	}

	return Result;
}

void UGeoClipmapMeshComponent::SetMaterial(int32 ElementIndex, UMaterialInterface* Material)
{
	for(int32 Section=0; Section <GetNumSections(); Section++)
	{
		Super::SetMaterial(Section, Material);
	}	
}

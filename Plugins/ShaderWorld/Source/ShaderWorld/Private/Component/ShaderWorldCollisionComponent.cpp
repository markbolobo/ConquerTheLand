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

#include "Component/ShaderWorldCollisionComponent.h"
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

#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"

#include "Async/ParallelFor.h"
#include "AI/Navigation/NavigationTypes.h"
#include "AI/NavigationSystemBase.h"
#include "AI/NavigationSystemHelpers.h"

#include "DynamicMeshBuilder.h"
#include "PhysicsEngine/PhysicsSettings.h"
#include "StaticMeshResources.h"
#include "SWStats.h"
#include "Actor/ShaderWorldActor.h"
#include "Async/Async.h"
#include "Chaos/TriangleMeshImplicitObject.h"

//#include "RayTracingDefinitions.h"
//#include "RayTracingInstance.h"



static TAutoConsoleVariable<int32> CVarRayTracingShaderWColProceduralMesh(
	TEXT("r.RayTracing.Geometry.ShaderWColMeshes"),
	1,
	TEXT("Include ShaderW Collision procedural meshes in ray tracing effects (default = 1 (procedural meshes enabled in ray tracing))"));




/** 
 *	Struct used to send update to mesh data 
 *	Arrays may be empty, in which case no update is performed.
 */
class FShaderWColProcMeshSectionUpdateData
{
public:
	/** Section to update */
	int32 TargetSection;
	/** New vertex information */
	TArray<FGeoCProcMeshVertex> NewVertexBuffer;

	TSharedPtr<FSWShareableVerticePositionBuffer, ESPMode::ThreadSafe> NewPositionBuffer;
};

static void ShaderW_ConvertProcMeshToDynMeshVertex(FDynamicMeshVertex& Vert, const FGeoCProcMeshVertex& ProcVert)
{
	Vert.Position = ProcVert.Position;
	Vert.Color = ProcVert.Color;
	Vert.TextureCoordinate[0] = ProcVert.UV0;
	Vert.TextureCoordinate[1] = ProcVert.UV1;
	Vert.TextureCoordinate[2] = ProcVert.UV2;
	Vert.TextureCoordinate[3] = ProcVert.UV3;
	Vert.TangentX = ProcVert.Tangent.TangentX;
	Vert.TangentZ = ProcVert.Normal;
	Vert.TangentZ.Vector.W = ProcVert.Tangent.bFlipTangentY ? -127 : 127;
}

TGlobalResource< FSWClipMapCollisionBuffersHolder > GSWClipMapBufferCollisionHolder;



void FSWClipMapCollisionBuffersHolder::RegisterExtension(const TSharedPtr<FSWShareableID, ESPMode::ThreadSafe>& ID,
	const int32& NumSection, UShaderWorldCollisionComponent* Component)
{
	FScopeLock ScopeLock(&MapLock);

	if (SectionShareableBuffers.Contains(ID) && (*SectionShareableBuffers.Find(ID)).Num() < NumSection)
		DiscardBuffers(ID);

	if (!SectionShareableBuffers.Contains(ID))
	{
		SectionShareableBuffers.Add(ID, {});

		(*SectionShareableBuffers.Find(ID)).AddZeroed(NumSection);
		for (int SectionIdx = 0; SectionIdx < NumSection; SectionIdx++)
		{
			FGeoCProcMeshSection& SrcSection = Component->ProcMeshSections[SectionIdx];
			if (SrcSection.IndexBuffer.IsValid() && SrcSection.IndexBuffer->Indices.Num() > 0)
			{
				FDrawCollisionBuffers* NewSection = new FDrawCollisionBuffers(SrcSection.IndexBuffer);
				{
				
					//init color and UV buffers
					const int32 NumVerts = SrcSection.ProcVertexBuffer.Num();

					// Allocate verts
					TArray<FDynamicMeshVertex> Vertices;
					Vertices.SetNumUninitialized(NumVerts);
					// Copy verts
					for (int VertIdx = 0; VertIdx < NumVerts; VertIdx++)
					{
						const FGeoCProcMeshVertex& ProcVert = SrcSection.ProcVertexBuffer[VertIdx];
						FDynamicMeshVertex& Vert = Vertices[VertIdx];
						ShaderW_ConvertProcMeshToDynMeshVertex(Vert, ProcVert);
					}

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
				(*SectionShareableBuffers.Find(ID))[SectionIdx] = NewSection;
			}
		}
	}
}

void FSWClipMapCollisionBuffersHolder::DiscardBuffers(const TSharedPtr<FSWShareableID, ESPMode::ThreadSafe>& ID)
{
	FScopeLock ScopeLock(&MapLock);

	if (SectionShareableBuffers.Contains(ID))
	{

		for (FDrawCollisionBuffers* Section : (*SectionShareableBuffers.Find(ID)))
		{
			if (Section != nullptr)
			{
				if (IsInRenderingThread())
				{
					Section->VertexBuffers.StaticMeshVertexBuffer.ReleaseResource();
					Section->VertexBuffers.ColorVertexBuffer.ReleaseResource();
					Section->IndexBuffer.ReleaseResource();
				}
				else
				{
					ENQUEUE_RENDER_COMMAND(InitCommand)(
						[ResourceA = &Section->IndexBuffer, ResourceB = &Section->VertexBuffers.StaticMeshVertexBuffer, ResourceC = &Section->VertexBuffers.ColorVertexBuffer](FRHICommandListImmediate& RHICmdList)
					{
						ResourceA->ReleaseResource();
						ResourceB->ReleaseResource();
						ResourceC->ReleaseResource();
					});
				}
			}

			delete Section;
		}

		SectionShareableBuffers.Remove(ID);
	}
}


/** Procedural mesh scene proxy */
class FShaderWProceduralMeshSceneProxy final : public FPrimitiveSceneProxy
{
public:

	TArray<FConvexVolume> ViewsFrustums;
	FThreadSafeBool ViewFrustumAccessMutex;
	FThreadSafeBool WireframeView;

	SIZE_T GetTypeHash() const override
	{
		static size_t UniquePointer;
		return reinterpret_cast<size_t>(&UniquePointer);
	}

	FShaderWProceduralMeshSceneProxy(UShaderWorldCollisionComponent* Component)
		: FPrimitiveSceneProxy(Component)
		, BodySetup(Component->GetBodySetup())
		, MaterialRelevance(Component->GetMaterialRelevance(GetScene().GetFeatureLevel()))
		, DrawCollisionMesh(Component->DrawCollisionMesh)
		, ID(Component->GetID())
	{
		//if(DrawCollisionMesh)
		{
			// Copy each section
			const int32 NumSections = Component->ProcMeshSections.Num();

			if (NumSections == 0)
				return;

			if(!ID.IsValid())
				return;

			

			GSWClipMapBufferCollisionHolder.RegisterExtension(ID, NumSections, Component);
			Sections.AddZeroed(NumSections);
			for (int SectionIdx = 0; SectionIdx < NumSections; SectionIdx++)
			{
				FGeoCProcMeshSection& SrcSection = Component->ProcMeshSections[SectionIdx];
				if (SrcSection.IndexBuffer.IsValid() && SrcSection.IndexBuffer->Indices.Num() > 0 && SrcSection.ProcVertexBuffer.Num() > 0)
				{
					FShaderWColProxySection* NewSection = new FShaderWColProxySection(GetScene().GetFeatureLevel(), SrcSection.IndexBuffer);

					// Copy Position Buffer
					//NewSection->PositionBuffer = SrcSection.PositionBuffer;
					NewSection->PositionBuffer = MakeShared<FSWShareableVerticePositionBuffer, ESPMode::ThreadSafe>();
					NewSection->PositionBuffer->Positions3f = SrcSection.PositionBuffer->Positions3f;
					NewSection->VertexBuffers.PositionVertexBuffer.Init(SrcSection.PositionBuffer->Positions3f);
					BeginInitResource(&NewSection->VertexBuffers.PositionVertexBuffer);

					FStaticMeshVertexBuffers* Self = &NewSection->VertexBuffers;
					FStaticMeshVertexBuffers* Shared = &GSWClipMapBufferCollisionHolder.GetBuffersForSection(ID, SectionIdx)->VertexBuffers;
					ENQUEUE_RENDER_COMMAND(StaticMeshVertexBuffersLegacyInit)(
						[VF = &NewSection->VertexFactory, Self, Shared](FRHICommandListImmediate& RHICmdList)
						{
						
							FLocalVertexFactory::FDataType Data;
							Self->PositionVertexBuffer.BindPositionVertexBuffer(VF, Data);
							Shared->StaticMeshVertexBuffer.BindTangentVertexBuffer(VF, Data);
							Shared->StaticMeshVertexBuffer.BindPackedTexCoordVertexBuffer(VF, Data);
							Shared->StaticMeshVertexBuffer.BindLightMapVertexBuffer(VF, Data, 0);
							Shared->ColorVertexBuffer.BindColorVertexBuffer(VF, Data);
							VF->SetData(Data);
							
							if (!VF->IsInitialized())
							{
								VF->InitResource();
							}
							else
							{
								VF->UpdateRHI();
							}
						});

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

#if 0 //RHI_RAYTRACING
					if (IsRayTracingEnabled())
					{
						ENQUEUE_RENDER_COMMAND(InitProceduralMeshRayTracingGeometry)(
							[this, DebugName = Component->GetFName(), NewSection](FRHICommandListImmediate& RHICmdList)
						{
							FRayTracingGeometryInitializer Initializer;
							Initializer.DebugName = DebugName;
							Initializer.IndexBuffer = nullptr;
							Initializer.TotalPrimitiveCount = 0;
							Initializer.GeometryType = RTGT_Triangles;
							Initializer.bFastBuild = true;
							Initializer.bAllowUpdate = false;

							NewSection->RayTracingGeometry.SetInitializer(Initializer);
							NewSection->RayTracingGeometry.InitResource();

							NewSection->RayTracingGeometry.Initializer.IndexBuffer = NewSection->IndexBuffer.IndexBufferRHI;
							NewSection->RayTracingGeometry.Initializer.TotalPrimitiveCount = NewSection->IndexBuffer.Indices.Num() / 3;

							FRayTracingGeometrySegment Segment;
							Segment.VertexBuffer = NewSection->VertexBuffers.PositionVertexBuffer.VertexBufferRHI;
							Segment.MaxVertices = NewSection->VertexBuffers.PositionVertexBuffer.GetNumVertices();
							Segment.NumPrimitives = NewSection->RayTracingGeometry.Initializer.TotalPrimitiveCount;
							NewSection->RayTracingGeometry.Initializer.Segments.Add(Segment);

							//#dxr_todo: add support for segments?

							NewSection->RayTracingGeometry.UpdateRHI();
						});
					}
#endif
				}
			}
		}

	}

	virtual ~FShaderWProceduralMeshSceneProxy()
	{
		for (FShaderWColProxySection* Section : Sections)
		{
			if (Section != nullptr)
			{
				Section->PositionBuffer.Reset();
				Section->VertexBuffers.PositionVertexBuffer.ReleaseResource();
				//Section->VertexBuffers.StaticMeshVertexBuffer.ReleaseResource();
				//Section->VertexBuffers.ColorVertexBuffer.ReleaseResource();
				//Section->IndexBuffer.ReleaseResource();
				Section->VertexFactory.ReleaseResource();

#if 0 //RHI_RAYTRACING
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
	void UpdateBounds_RenderThread(FBoxSphereBounds NewBounds)
	{
		check(IsInRenderingThread());
		
		OcclusionBounds = NewBounds;
		bHasCustomOcclusionBounds = true;
	}

	/** Called on render thread to assign new dynamic data */
	void UpdateSection_RenderThread(FShaderWColProcMeshSectionUpdateData* SectionData)
	{
		check(IsInRenderingThread());

		// Check we have data 
		if(	SectionData != nullptr) 			
		{
			// Check it references a valid section
			if (SectionData->TargetSection < Sections.Num() &&
				Sections[SectionData->TargetSection] != nullptr && (SectionData->NewPositionBuffer.IsValid()))
			{
				FShaderWColProxySection* Section = Sections[SectionData->TargetSection];

				/*
				 * We won't update it if it wasn't visible
				 */				
				Section->PositionBuffer.Reset();
				Section->PositionBuffer = SectionData->NewPositionBuffer;

				// Lock vertex buffer
				//const int32 NumVerts = Section->PositionBuffer->Positions3f.Num();

				Section->VertexBuffers.PositionVertexBuffer.Init(Section->PositionBuffer->Positions3f);

				// Iterate through vertex data, copying in new info				

				{
					auto& VertexBuffer = Section->VertexBuffers.PositionVertexBuffer;
					void* VertexBufferData = RHILockBuffer(VertexBuffer.VertexBufferRHI, 0, VertexBuffer.GetNumVertices() * VertexBuffer.GetStride(), RLM_WriteOnly);
					FMemory::Memcpy(VertexBufferData, VertexBuffer.GetVertexData(), VertexBuffer.GetNumVertices() * VertexBuffer.GetStride());
					RHIUnlockBuffer(VertexBuffer.VertexBufferRHI);
				}
			}

			// Free data sent from game thread
			delete SectionData;
		}
	}

	void SetSectionVisibility_RenderThread(int32 SectionIndex, bool bNewVisibility)
	{
		check(IsInRenderingThread());

		if(	SectionIndex < Sections.Num() &&
			Sections[SectionIndex] != nullptr)
		{
			Sections[SectionIndex]->bSectionVisible = bNewVisibility;
		}
	}



	bool GetStaticMeshElement(FShaderWColProxySection* Section,int32 SectionID,int32 LODIndex, bool bForToolMesh, bool bForcedLOD, UMaterialInterface* InMaterialInterface, FMeshBatch& MeshBatch) const
	{
		UMaterialInterface* MaterialInterface = nullptr;


		int32 MaterialIndex = 0;

		// Defaults to the material interface w/ potential tessellation
		MaterialInterface = InMaterialInterface;

		if (!MaterialInterface)
		{
			return false;
		}


		MeshBatch.VertexFactory = &Section->VertexFactory;
		MeshBatch.MaterialRenderProxy = MaterialInterface->GetRenderProxy();

		//MeshBatch.LCI = ComponentLightInfo.Get();
		MeshBatch.ReverseCulling = IsLocalToWorldDeterminantNegative();
		MeshBatch.CastShadow = false;
		#if 0 //RHI_RAYTRACING
		MeshBatch.CastRayTracedShadow = true;
		#endif
		MeshBatch.bUseForDepthPass = true;
		MeshBatch.bUseAsOccluder = ShouldUseAsOccluder() && GetScene().GetShadingPath() == EShadingPath::Deferred;
		MeshBatch.bUseForMaterial = true;
		MeshBatch.Type = PT_TriangleList;

		MeshBatch.DepthPriorityGroup = SDPG_World;
		MeshBatch.LODIndex = 0;
		MeshBatch.bDitheredLODTransition = false;

		// Combined batch element
		FMeshBatchElement& BatchElement = MeshBatch.Elements[0];

		//BatchElement.UserData = BatchElementParams;
		BatchElement.PrimitiveUniformBuffer = nullptr;
		BatchElement.IndexBuffer = &GSWClipMapBufferCollisionHolder.GetBuffersForSection(ID, SectionID)->IndexBuffer;

		
		BatchElement.NumPrimitives = GSWClipMapBufferCollisionHolder.GetBuffersForSection(ID, SectionID)->IndexBuffer.IndicesPtr.IsValid() ? GSWClipMapBufferCollisionHolder.GetBuffersForSection(ID, SectionID)->IndexBuffer.IndicesPtr->Indices.Num() / 3:0;
		BatchElement.FirstIndex = 0;
		BatchElement.MinVertexIndex = 0;
		BatchElement.MaxVertexIndex = Section->VertexBuffers.PositionVertexBuffer.GetNumVertices() - 1;


		return true;
	}



	virtual int32 GetNumMeshBatches() const
	{
		int Batchc = 0;
		for (const FShaderWColProxySection* Section : Sections)
			if (Section != nullptr && Section->bSectionVisible && Section->Material)
				Batchc++;

		return Batchc;
	}

	virtual void DrawStaticElements(FStaticPrimitiveDrawInterface* PDI) override
	{

		checkSlow(IsInParallelRenderingThread());
		if (!HasViewDependentDPG())
		{
		
			if (Sections.Num() == 0 || !Sections[0]->Material )
			{
				return;
			}


			const int32 NumBatches = GetNumMeshBatches();
			PDI->ReserveMemoryForMeshes(NumBatches);


			// Iterate over sections
			for (int SectionIdx = 0; SectionIdx < Sections.Num(); SectionIdx++)
		//	for (const FShaderWColProxySection* Section : Sections)
			{
				const FShaderWColProxySection* Section = Sections[SectionIdx];

				if (Section != nullptr && Section->bSectionVisible && Section->Material)
				{

					FMeshBatch MeshBatch;

					if (GetStaticMeshElement(const_cast<FShaderWColProxySection*>(Section), SectionIdx, 0, false, false, Section->Material, MeshBatch))
					{
						PDI->DrawMesh(MeshBatch, FLT_MAX);
					}

				}
			}
		}
		
	}


	virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const override
	{

		// Set up wireframe material (if needed)
		const bool bWireframe = AllowDebugViewmodes() && ViewFamily.EngineShowFlags.Wireframe;

		{
			FShaderWProceduralMeshSceneProxy* Proxy_local = const_cast<FShaderWProceduralMeshSceneProxy*>(this);
			if(Proxy_local)
				Proxy_local->WireframeView = bWireframe ? true : false;
		}
		
			

		FColoredMaterialRenderProxy* WireframeMaterialInstance = NULL;
		if (bWireframe)
		{
			//const FColor CollisionCOlor(102, 54, 21);
			const FColor CollisionCOlor(50, 50, 65);
			WireframeMaterialInstance = new FColoredMaterialRenderProxy(
				GEngine->WireframeMaterial ? GEngine->WireframeMaterial->GetRenderProxy() : NULL,
				FLinearColor(CollisionCOlor)
			);

			Collector.RegisterOneFrameMaterialProxy(WireframeMaterialInstance);
		}

		// Iterate over sections
		for (int SectionIdx = 0; SectionIdx < Sections.Num(); SectionIdx++)
		//for (const FShaderWColProxySection* Section : Sections)
		{
			const FShaderWColProxySection* Section = Sections[SectionIdx];
			if (Section != nullptr && Section->bSectionVisible)
			{
				FMaterialRenderProxy* MaterialProxy = bWireframe ? WireframeMaterialInstance : Section->Material->GetRenderProxy();

				// For each view..
				for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
				{
					if (VisibilityMap & (1 << ViewIndex))
					{
						const FSceneView* View = Views[ViewIndex];
						// Draw the mesh.
						FMeshBatch& Mesh = Collector.AllocateMesh();
						FMeshBatchElement& BatchElement = Mesh.Elements[0];
						BatchElement.IndexBuffer = &GSWClipMapBufferCollisionHolder.GetBuffersForSection(ID, SectionIdx)->IndexBuffer;
						Mesh.bWireframe = bWireframe;
						Mesh.VertexFactory = &Section->VertexFactory;
						Mesh.MaterialRenderProxy = MaterialProxy;
						Mesh.CastShadow=false;
						
						bool bHasPrecomputedVolumetricLightmap;
						FMatrix PreviousLocalToWorld;
						int32 SingleCaptureIndex;
						bool bOutputVelocity;
						GetScene().GetPrimitiveUniformShaderParameters_RenderThread(GetPrimitiveSceneInfo(), bHasPrecomputedVolumetricLightmap, PreviousLocalToWorld, SingleCaptureIndex, bOutputVelocity);

						FDynamicPrimitiveUniformBuffer& DynamicPrimitiveUniformBuffer = Collector.AllocateOneFrameResource<FDynamicPrimitiveUniformBuffer>();
						DynamicPrimitiveUniformBuffer.Set(GetLocalToWorld(), PreviousLocalToWorld, GetBounds(), GetLocalBounds(), true, bHasPrecomputedVolumetricLightmap, bOutputVelocity);
						BatchElement.PrimitiveUniformBufferResource = &DynamicPrimitiveUniformBuffer.UniformBuffer;						
						
						//BatchElement.PrimitiveUniformBuffer = GetUniformBuffer();
						BatchElement.FirstIndex = 0;
						BatchElement.NumPrimitives = GSWClipMapBufferCollisionHolder.GetBuffersForSection(ID, SectionIdx)->IndexBuffer.IndicesPtr.IsValid() ? GSWClipMapBufferCollisionHolder.GetBuffersForSection(ID, SectionIdx)->IndexBuffer.IndicesPtr->Indices.Num() / 3 : 0;
						BatchElement.MinVertexIndex = 0;
						BatchElement.MaxVertexIndex = Section->VertexBuffers.PositionVertexBuffer.GetNumVertices() - 1;
						Mesh.ReverseCulling = IsLocalToWorldDeterminantNegative();
						Mesh.Type = PT_TriangleList;
						Mesh.DepthPriorityGroup = SDPG_World;
						Mesh.bCanApplyViewModeOverrides = IsSelected();
						Mesh.bUseWireframeSelectionColoring = IsSelected();
						Collector.AddMesh(ViewIndex, Mesh);
					}
				}
			}
		}

		// Draw bounds
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
		{
			if (VisibilityMap & (1 << ViewIndex))
			{
				/*
				// Draw simple collision as wireframe if 'show collision', and collision is enabled, and we are not using the complex as the simple
				if (ViewFamily.EngineShowFlags.Collision && IsCollisionEnabled() && BodySetup->GetCollisionTraceFlag() != ECollisionTraceFlag::CTF_UseComplexAsSimple)
				{
					FTransform GeomTransform(GetLocalToWorld());
					BodySetup->AggGeom.GetAggGeom(GeomTransform, GetSelectionColor(FColor(157, 149, 223, 255), IsSelected(), IsHovered()).ToFColor(true), NULL, false, false, DrawsVelocity(), ViewIndex, Collector);
				}*/

				// Render bounds
				RenderBounds(Collector.GetPDI(ViewIndex), ViewFamily.EngineShowFlags, GetBounds(), IsSelected());
			}
		}
#endif
	}

	virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const
	{
		FPrimitiveViewRelevance Result;
		Result.bDrawRelevance = IsShown(View);
		Result.bShadowRelevance = false;
		Result.bDynamicRelevance = true;
		Result.bRenderInMainPass = ShouldRenderInMainPass();
		Result.bUsesLightingChannels = GetLightingChannelMask() != GetDefaultLightingChannelMask();
		Result.bRenderCustomDepth = false;
		Result.bTranslucentSelfShadow = false;
		MaterialRelevance.SetPrimitiveViewRelevance(Result);
		Result.bVelocityRelevance = false;
		Result.bOutputsTranslucentVelocity = false;
		return Result;
	}

	virtual bool CanBeOccluded() const override
	{
		return !MaterialRelevance.bDisableDepthTest;
	}
#if 0
	/**
	*	Returns whether the proxy utilizes custom occlusion bounds or not
	*
	*	@return	bool		true if custom occlusion bounds are used, false if not;
	*/
	virtual bool HasCustomOcclusionBounds() const override
	{
		return CanBeOccluded() ? bHasCustomOcclusionBounds : FPrimitiveSceneProxy::HasCustomOcclusionBounds();
	}

	/**
	*	Return the custom occlusion bounds for this scene proxy.
	*
	*	@return	FBoxSphereBounds		The custom occlusion bounds.
	*/
	virtual FBoxSphereBounds GetCustomOcclusionBounds() const override
	{
		return CanBeOccluded() ? OcclusionBounds.TransformBy(GetLocalToWorld()) : FPrimitiveSceneProxy::GetCustomOcclusionBounds();
	}
#endif

	virtual uint32 GetMemoryFootprint(void) const
	{
		return(sizeof(*this) + GetAllocatedSize());
	}

	uint32 GetAllocatedSize(void) const
	{
		return(FPrimitiveSceneProxy::GetAllocatedSize());
	}


#if 0 // RHI_RAYTRACING
	virtual bool IsRayTracingRelevant() const override { return true; }

	virtual void GetDynamicRayTracingInstances(FRayTracingMaterialGatheringContext& Context, TArray<FRayTracingInstance>& OutRayTracingInstances) override final
	{
		if (!CVarRayTracingShaderWColProceduralMesh.GetValueOnRenderThread())
		{
			return;
		}

		for (int32 SegmentIndex = 0; SegmentIndex < Sections.Num(); ++SegmentIndex)
		{
			const FShaderWColProxySection* Section = Sections[SegmentIndex];
			if (Section != nullptr && Section->bSectionVisible)
			{
				FMaterialRenderProxy* MaterialProxy = Section->Material->GetRenderProxy();
				
				if (Section->RayTracingGeometry.RayTracingGeometryRHI.IsValid())
				{
					check(Section->RayTracingGeometry.Initializer.IndexBuffer.IsValid());

					FRayTracingInstance RayTracingInstance;
					RayTracingInstance.Geometry = &Section->RayTracingGeometry;
					RayTracingInstance.InstanceTransforms.Add(GetLocalToWorld());

					uint32 SectionIdx = 0;
					FMeshBatch MeshBatch;

					MeshBatch.VertexFactory = &Section->VertexFactory;
					MeshBatch.SegmentIndex = SegmentIndex;
					MeshBatch.MaterialRenderProxy = Section->Material->GetRenderProxy();
					MeshBatch.ReverseCulling = IsLocalToWorldDeterminantNegative();
					MeshBatch.Type = PT_TriangleList;
					MeshBatch.DepthPriorityGroup = SDPG_World;
					MeshBatch.bCanApplyViewModeOverrides = false;
					MeshBatch.CastRayTracedShadow = IsShadowCast(Context.ReferenceView);

					FMeshBatchElement& BatchElement = MeshBatch.Elements[0];
					BatchElement.IndexBuffer = &Section->IndexBuffer;

					bool bHasPrecomputedVolumetricLightmap;
					FMatrix PreviousLocalToWorld;
					int32 SingleCaptureIndex;
					bool bOutputVelocity;
					GetScene().GetPrimitiveUniformShaderParameters_RenderThread(GetPrimitiveSceneInfo(), bHasPrecomputedVolumetricLightmap, PreviousLocalToWorld, SingleCaptureIndex, bOutputVelocity);

					FDynamicPrimitiveUniformBuffer& DynamicPrimitiveUniformBuffer = Context.RayTracingMeshResourceCollector.AllocateOneFrameResource<FDynamicPrimitiveUniformBuffer>();
					DynamicPrimitiveUniformBuffer.Set(GetLocalToWorld(), PreviousLocalToWorld, GetBounds(), GetLocalBounds(), true, bHasPrecomputedVolumetricLightmap, DrawsVelocity(), bOutputVelocity);
					BatchElement.PrimitiveUniformBufferResource = &DynamicPrimitiveUniformBuffer.UniformBuffer;

					BatchElement.FirstIndex = 0;
					BatchElement.NumPrimitives = Section->IndexBuffer.Indices.Num() / 3;
					BatchElement.MinVertexIndex = 0;
					BatchElement.MaxVertexIndex = Section->VertexBuffers.PositionVertexBuffer.GetNumVertices() - 1;

					RayTracingInstance.Materials.Add(MeshBatch);

					RayTracingInstance.BuildInstanceMaskAndFlags();
					OutRayTracingInstances.Add(RayTracingInstance);
				}
			}
		}
	}
	
#endif

private:
	/** Array of sections */
	TArray<FShaderWColProxySection*> Sections;

	UBodySetup* BodySetup;

	FMaterialRelevance MaterialRelevance;

	bool	bHasCustomOcclusionBounds;

	FBoxSphereBounds OcclusionBounds;

	bool DrawCollisionMesh = false;

	const TSharedPtr<FSWShareableID, ESPMode::ThreadSafe> ID;
};


UShaderWorldCollisionComponent::UShaderWorldCollisionComponent(const FObjectInitializer& ObjectInitializer)
: Super(ObjectInitializer)
{
	bUseComplexAsSimpleCollision = true;
	bCanEverAffectNavigation = true;
	bNeverDistanceCull = true;
	bHasCustomNavigableGeometry = EHasCustomNavigableGeometry::EvenIfNotCollidable;
}

//////////////////////////////////////////////////////////////////////////
/// FStaticLightingSystem Friend class of ModelComponent can assign the UModel*
#if WITH_EDITOR
class FStaticLightingSystem
{
public:
	static void SetModel(UModelComponent* Comp, UModel* Model)
	{
		Comp->Model = Model;		
	}
};
#endif


void UShaderWorldCollisionComponent::PostInitProperties()
{
	Super::PostInitProperties();


#if WITH_EDITOR
	const FString ModelString = "Model_" + GetName();
	const FName ModelName = FName(*ModelString);

	const ERenameFlags RenFlags = REN_NonTransactional | REN_DontCreateRedirectors | REN_ForceNoResetLoaders | REN_DoNotDirty;
	UObject* FoundExistingName = StaticFindObject(UModel::StaticClass(), GetOuter(), *ModelString, true);
	FString Rename_str_invalid = ModelString + "_invalid";
	bool NoNameCollision = true;

	if (FoundExistingName)
	{
		UE_LOG(LogTemp, Warning, TEXT("Found colliding UmodelName"));

		NoNameCollision = false;

		const FString ARandNum = FString::FromInt(FMath::RoundToInt(FPlatformTime::Seconds()));
		const FString ARandNumOther = FString::FromInt(FMath::RandRange(0, 999));

		for (int k = 0; k < 100; k++)
		{
			Rename_str_invalid = ModelString + "_invalid" + ARandNum + ARandNumOther + FString::FromInt(k);
			UObject* FoundExistingName_error = StaticFindObject(UModel::StaticClass(), GetOuter(), *Rename_str_invalid, true);
			if (!FoundExistingName_error)
			{
				FoundExistingName->Rename(*Rename_str_invalid, FoundExistingName->GetOuter(), RenFlags);
				NoNameCollision = true;
				break;
			}

			if (k == 99)
			{
				Rename_str_invalid = ModelString + "_invalid" + ARandNum + ARandNumOther + FString::FromInt(FMath::Rand() + k);
				FoundExistingName_error = StaticFindObject(UModel::StaticClass(), GetOuter(), *Rename_str_invalid, true);
				if (!FoundExistingName_error)
				{
					FoundExistingName->Rename(*Rename_str_invalid, FoundExistingName->GetOuter(), RenFlags);
					NoNameCollision = true;
					break;
				}

			}

		}
	}

	if (!NoNameCollision)
	{
		Rename_str_invalid += "_SC" + FString::FromInt(FMath::RoundToInt(FPlatformTime::Seconds()));
		UObject* FoundExistingName_lastResort = StaticFindObject(UModel::StaticClass(), GetOuter(), *ModelString, true);
		if (!FoundExistingName_lastResort)
		{
			FoundExistingName->Rename(*Rename_str_invalid, FoundExistingName->GetOuter(), RenFlags);
			NoNameCollision = true;
		}
	}

	if (NoNameCollision)
	{
		ModelCopy = NewObject<UModel>(this, ModelName, RF_Transient);
		ModelCopy->Initialize(nullptr, 1);		
		ModelCopy->Polys = nullptr;

		ModelCopy->Nodes.Empty();
		ModelCopy->Verts.Empty();
		ModelCopy->Vectors.Empty();
		ModelCopy->Points.Empty();
		ModelCopy->Surfs.Empty();

		ModelCopy->LeafHulls.Empty();
		ModelCopy->Leaves.Empty();

		FStaticLightingSystem::SetModel(this,ModelCopy);
	}		
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("couldnt find viable renaming for UModel, none created"));
	}

#endif
}

void UShaderWorldCollisionComponent::PostLoad()
{
	UPrimitiveComponent::PostLoad();

	if (ProcMeshBodySetup && IsTemplate())
	{
		ProcMeshBodySetup->SetFlags(RF_Public);
	}

	if (SecondaryProcMeshBodySetup && IsTemplate())
	{
		SecondaryProcMeshBodySetup->SetFlags(RF_Public);
	}
}

UMaterialInterface* UShaderWorldCollisionComponent::GetMaterial(int32 ElementIndex) const
{
	if (ElementIndex >= ProcMeshSections.Num())
	return nullptr;

	return ProcMeshSections[ElementIndex].Material;
}

void UShaderWorldCollisionComponent::SetMaterial(int32 ElementIndex, UMaterialInterface* Material)
{
	/*
	if (ElementIndex >= ProcMeshSections.Num())
		return;

	ProcMeshSections[ElementIndex].Material = Material;*/
}

void UShaderWorldCollisionComponent::SetMaterialFromOwner(int32 ElementIndex, UMaterialInterface* Material)
{
	if (ElementIndex >= ProcMeshSections.Num())
		return;

	ProcMeshSections[ElementIndex].Material = Material;
}

void UShaderWorldCollisionComponent::GetUsedMaterials(TArray<UMaterialInterface*>& OutMaterials,
	bool bGetDebugMaterials) const
{
	for (int32 ElementIndex = 0; ElementIndex < GetNumMaterials(); ElementIndex++)
	{
		if (UMaterialInterface* MaterialInterface = GetMaterial(ElementIndex))
		{
			OutMaterials.Add(MaterialInterface);
		}
	}
}


FGeoCProcMeshSection& UShaderWorldCollisionComponent::GetMeshSectionInit(int32 SectionIndex)
{
	// Ensure sections array is long enough
	if (SectionIndex >= ProcMeshSections.Num())
	{
		ProcMeshSections.SetNum(SectionIndex + 1, false);
	}

	FGeoCProcMeshSection& NewSection = ProcMeshSections[SectionIndex];
	NewSection.Reset();
	return ProcMeshSections[SectionIndex];
}

FGeoCProcMeshSection& UShaderWorldCollisionComponent::GetMeshSection(int32 SectionIndex)
{

	return ProcMeshSections[SectionIndex];
}

void UShaderWorldCollisionComponent::SendSections()
{
	UpdateLocalBounds(); // Update overall bounds
	UpdateNavigation();
	UpdateCollision(); // Mark collision as dirty
	MarkRenderStateDirty(); // New section requires recreating scene proxy
}

void UShaderWorldCollisionComponent::UpdateSectionTriMesh(TSharedPtr<FSWShareableVerticePositionBuffer, ESPMode::ThreadSafe>& Positions)
{	
	if (AsyncBodySetupQueue.Num() > 0)
	{
#if SWDEBUG
		SW_LOG("Collision Update received during alreayd occuring computation")
#endif

		UpdatesReceivedDuringCompute.Add(Positions);

		return;
	}
	
	UpdatesReceivedDuringCompute.Empty();
	

	bool EnsureSameBuffers = ProcMeshSections.Num() > 0 && ProcMeshSections[0].ProcVertexBuffer.Num() == Positions->Positions.Num();
	if(!EnsureSameBuffers)
	{
#if SWDEBUG
		UE_LOG(LogTemp,Warning,TEXT("Error UpdateSectionTriMesh : buffers incompatible"));
#endif

	}
	else
	{
		ProcMeshSections[0].PositionBuffer.Reset();
		ProcMeshSections[0].PositionBuffer = Positions;
		ProcMeshSections[0].SectionLocalBox = Positions->Bound;
		ProcMeshSections[0].bEnableCollision = true;
	}
	
	//Materials
	// Pass new positions to trimesh
	UpdateCollision();
	UpdateLocalBounds();		 // Update overall bounds
	UpdateNavigation();

	//SW_LOG("Update trimesh")	

	if (ProcMeshSections.Num() > 0 && (ProcMeshSections[0].bSectionVisible || GetWorld() && !GetWorld()->IsGameWorld()))
	{
		//SW_LOG("Update trimesh ProcMeshSections.Num() > 0")
		// If we have a valid proxy and it is not pending recreation
		if (SceneProxy && !IsRenderStateDirty())
		{
			//SW_LOG("Update trimesh SceneProxy && !IsRenderStateDirty()")
			// Create data to update section
			FShaderWColProcMeshSectionUpdateData* SectionData = new FShaderWColProcMeshSectionUpdateData;
			SectionData->TargetSection = 0;
			//SectionData->NewVertexBuffer = ProcMeshSections[0].ProcVertexBuffer;
			SectionData->NewPositionBuffer = ProcMeshSections[0].PositionBuffer;

			if(AShaderWorldActor* owner = Cast<AShaderWorldActor>(GetOwner()))
			{
				//SW_LOG("Update trimesh owner")
				
				{
					// Enqueue command to send to render thread
					FShaderWProceduralMeshSceneProxy* ProcMeshSceneProxy = (FShaderWProceduralMeshSceneProxy*)(SceneProxy && !IsRenderStateDirty() ? SceneProxy : nullptr);
					ENQUEUE_RENDER_COMMAND(FGeoCProcMeshSectionUpdate)
						([ProcMeshSceneProxy, SectionData](FRHICommandListImmediate& RHICmdList)
						{
							if(ProcMeshSceneProxy)
								ProcMeshSceneProxy->UpdateSection_RenderThread(SectionData);
						});
				}				
			}
			else
			{
				//SW_LOG("Update trimesh !owner")
			}
		}
		else
		{
			//SW_LOG("Update trimesh !(SceneProxy && !IsRenderStateDirty())")
		}

		MarkRenderTransformDirty();
	}
	
}

void UShaderWorldCollisionComponent::CreateMeshSection(int32 SectionIndex, const TSharedPtr<FSWShareableVerticePositionBuffer, ESPMode::ThreadSafe>& Vertices, const TSharedPtr<FSWShareableIndexBuffer, ESPMode::ThreadSafe>& Triangles, bool bCreateCollision)
{

	// Ensure sections array is long enough
	if (SectionIndex >= ProcMeshSections.Num())
	{
		ProcMeshSections.SetNum(SectionIndex + 1, false);
	}

	// Reset this section (in case it already existed)
	FGeoCProcMeshSection& NewSection = ProcMeshSections[SectionIndex];
	NewSection.Reset();

	NewSection.PositionBuffer = Vertices;
	//NewSection.SectionLocalBox = Vertices->Bound;
	NewSection.IndexBuffer = Triangles;

	// Copy data to vertex buffer
	const int32 NumVerts = Vertices->Positions.Num();
	NewSection.ProcVertexBuffer.Reset();
	NewSection.ProcVertexBuffer.AddDefaulted(NumVerts);
	NewSection.MaterialIndices.Empty();

	NewSection.bEnableCollision = bCreateCollision;

	UpdateLocalBounds(); // Update overall bounds
	UpdateNavigation();
	UpdateCollision(); // Mark collision as dirty
	MarkRenderStateDirty(); // New section requires recreating scene proxy
}

void UShaderWorldCollisionComponent::UpdateCustomBounds(FBoxSphereBounds Newbound)
{
	UseCustomBounds=true;
	LocalBoundsGeoC = Newbound;	

	// Enqueue command to send to render thread
	FShaderWProceduralMeshSceneProxy* ProcMeshSceneProxy = (FShaderWProceduralMeshSceneProxy*)(SceneProxy && !IsRenderStateDirty() ? SceneProxy : nullptr);
	ENQUEUE_RENDER_COMMAND(FGeoCProcMeshSectionUpdate)
		([ProcMeshSceneProxy,Newbound](FRHICommandListImmediate& RHICmdList)
		{
			if(ProcMeshSceneProxy)
				ProcMeshSceneProxy->UpdateBounds_RenderThread(Newbound);
		});	

	UpdateLocalBounds();		 // Update overall bounds
	MarkRenderTransformDirty();  // Need to send new bounds to render thread

}

void UShaderWorldCollisionComponent::ClearMeshSection(int32 SectionIndex)
{
	if (SectionIndex < ProcMeshSections.Num())
	{
		ProcMeshSections[SectionIndex].Reset();
		UpdateLocalBounds();
		UpdateCollision();
		MarkRenderStateDirty();
	}
}

void UShaderWorldCollisionComponent::ClearAllMeshSections()
{
	ProcMeshSections.Empty();
	UpdateLocalBounds();
	UpdateCollision();
	MarkRenderStateDirty();
}

void UShaderWorldCollisionComponent::SetMeshSectionVisible(int32 SectionIndex, bool bNewVisibility)
{
	if((SectionIndex>=0) && (SectionIndex < ProcMeshSections.Num()) && (ProcMeshSections.Num()>0))
	{
		// Set game thread state
		ProcMeshSections[SectionIndex].bSectionVisible = bNewVisibility;
		
		// Enqueue command to modify render thread info
		FShaderWProceduralMeshSceneProxy* ProcMeshSceneProxy = (FShaderWProceduralMeshSceneProxy*)(SceneProxy && !IsRenderStateDirty() ? SceneProxy : nullptr);
		ENQUEUE_RENDER_COMMAND(FGeoCProcMeshSectionVisibilityUpdate)(
			[ProcMeshSceneProxy, SectionIndex, bNewVisibility](FRHICommandListImmediate& RHICmdList)
			{
				if(ProcMeshSceneProxy)
					ProcMeshSceneProxy->SetSectionVisibility_RenderThread(SectionIndex, bNewVisibility);
			});

		MarkRenderStateDirty();
	}
}

bool UShaderWorldCollisionComponent::IsMeshSectionVisible(int32 SectionIndex) const
{
	return (SectionIndex < ProcMeshSections.Num()) ? ProcMeshSections[SectionIndex].bSectionVisible : false;
}

int32 UShaderWorldCollisionComponent::GetNumSections() const
{
	return ProcMeshSections.Num();
}

void UShaderWorldCollisionComponent::AddCollisionConvexMesh(TArray<FVector> ConvexVerts)
{
	if(ConvexVerts.Num() >= 4)
	{ 
		// New element
		FKConvexElem NewConvexElem;
		// Copy in vertex info
		NewConvexElem.VertexData = ConvexVerts;
		// Update bounding box
		NewConvexElem.ElemBox = FBox(NewConvexElem.VertexData);
		// Add to array of convex elements
		CollisionConvexElems.Add(NewConvexElem);
		// Refresh collision
		UpdateCollision();
	}
}

void UShaderWorldCollisionComponent::ClearCollisionConvexMeshes()
{
	// Empty simple collision info
	CollisionConvexElems.Empty();
	// Refresh collision
	UpdateCollision();
}

void UShaderWorldCollisionComponent::SetCollisionConvexMeshes(const TArray< TArray<FVector> >& ConvexMeshes)
{
	CollisionConvexElems.Reset();

	// Create element for each convex mesh
	for (int32 ConvexIndex = 0; ConvexIndex < ConvexMeshes.Num(); ConvexIndex++)
	{
		FKConvexElem NewConvexElem;
		NewConvexElem.VertexData = ConvexMeshes[ConvexIndex];
		NewConvexElem.ElemBox = FBox(NewConvexElem.VertexData);

		CollisionConvexElems.Add(NewConvexElem);
	}

	UpdateCollision();
}


void UShaderWorldCollisionComponent::UpdateLocalBounds()
{
	FBox LocalBox(ForceInit);

	for (const FGeoCProcMeshSection& Section : ProcMeshSections)
	{
		//LocalBox += Section.SectionLocalBox;

		if(Section.PositionBuffer.IsValid())
			LocalBox += Section.PositionBuffer->Bound;
	}

	LocalBounds = UseCustomBounds? LocalBoundsGeoC: (LocalBox.IsValid ? FBoxSphereBounds(LocalBox) : FBoxSphereBounds(-100*FVector(1), 100 * FVector(1), 1000)); // fallback to reset box sphere bounds

	// Update global bounds
	UpdateBounds();
	// Need to send to render thread
	MarkRenderTransformDirty();
}

FPrimitiveSceneProxy* UShaderWorldCollisionComponent::CreateSceneProxy()
{
	return new FShaderWProceduralMeshSceneProxy(this);
}

int32 UShaderWorldCollisionComponent::GetNumMaterials() const
{
	return ProcMeshSections.Num()>0?255:0;//ProcMeshSections.Num();
}

FMaterialRelevance UShaderWorldCollisionComponent::GetMaterialRelevance(ERHIFeatureLevel::Type InFeatureLevel) const
{
	// Combine the material relevance for all materials.
	FMaterialRelevance Result;
	for (int32 ElementIndex = 0; ElementIndex < GetNumMaterials(); ElementIndex++)
	{
		UMaterialInterface const* MaterialInterface = GetMaterial(ElementIndex);
		if (!MaterialInterface)
		{
			MaterialInterface = UMaterial::GetDefaultMaterial(MD_Surface);
		}
		Result |= MaterialInterface->GetRelevance_Concurrent(InFeatureLevel);
	}
	return Result;
}


FGeoCProcMeshSection* UShaderWorldCollisionComponent::GetProcMeshSection(int32 SectionIndex)
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


void UShaderWorldCollisionComponent::SetProcMeshSection(int32 SectionIndex, const FGeoCProcMeshSection& Section)
{
	// Ensure sections array is long enough
	if (SectionIndex >= ProcMeshSections.Num())
	{
		ProcMeshSections.SetNum(SectionIndex + 1, false);
	}

	ProcMeshSections[SectionIndex] = Section;

	UpdateLocalBounds(); // Update overall bounds
	UpdateCollision(); // Mark collision as dirty
	MarkRenderStateDirty(); // New section requires recreating scene proxy
}

FBoxSphereBounds UShaderWorldCollisionComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	FBoxSphereBounds Ret(LocalBounds.TransformBy(LocalToWorld));

	Ret.BoxExtent *= BoundsScale;
	Ret.SphereRadius *= BoundsScale;

	return Ret;
}

bool UShaderWorldCollisionComponent::GetPhysicsTriMeshData(struct FTriMeshCollisionData* CollisionData, bool InUseAllTriData)
{

	int32 VertexBase = 0; // Base vertex index for current section

	// See if we should copy UVs
	bool bCopyUVs = UPhysicsSettings::Get()->bSupportUVFromHitResults; 
	if (bCopyUVs)
	{
		CollisionData->UVs.AddZeroed(1); // only one UV channel
	}

	// For each section..
	for (int32 SectionIdx = 0; SectionIdx < ProcMeshSections.Num(); SectionIdx++)
	{
		FGeoCProcMeshSection& Section = ProcMeshSections[SectionIdx];
		if (!Section.PositionBuffer.IsValid() || !Section.IndexBuffer.IsValid())
			continue;

		CollisionData->Vertices = Section.PositionBuffer->Positions3f;
		/*
		 *Massive race condition here
		if(Section.IndexBuffer->Triangles_CollisionOnly.Num() != (Section.IndexBuffer->Indices.Num()/3))
		{
			const int32 NumTriangles = Section.IndexBuffer->Indices.Num() / 3;
			Section.IndexBuffer->Triangles_CollisionOnly.Empty();
			Section.IndexBuffer->Triangles_CollisionOnly.Reserve(NumTriangles);
			for (int32 TriIdx = 0; TriIdx < NumTriangles; TriIdx++)
			{
				FTriIndices& Triangle = Section.IndexBuffer->Triangles_CollisionOnly.AddDefaulted_GetRef();
				Triangle.v0 = Section.IndexBuffer->Indices[(TriIdx * 3) + 0];
				Triangle.v1 = Section.IndexBuffer->Indices[(TriIdx * 3) + 1] ;
				Triangle.v2 = Section.IndexBuffer->Indices[(TriIdx * 3) + 2];
			}
		}
		*/
		CollisionData->Indices = Section.IndexBuffer->Triangles_CollisionOnly;
#if 0
		FGeoCProcMeshSection& Section = ProcMeshSections[SectionIdx];

		const TSharedPtr<FSWShareableVerticePositionBuffer, ESPMode::ThreadSafe> PositionBuffer = Section.PositionBuffer;

		if (!PositionBuffer.IsValid() || !Section.IndexBuffer.IsValid())
		continue;

		
		CollisionData->Vertices = PositionBuffer->Positions3f;

		
		
		const int32 NumTriangles = Section.IndexBuffer->Indices.Num() / 3;

		if(NumTriangles != Section.IndexBuffer->Triangles_CollisionOnly.Num())
		{
			Section.IndexBuffer->Triangles_CollisionOnly.Empty();
			Section.IndexBuffer->Triangles_CollisionOnly.Reserve(NumTriangles);
			for (int32 TriIdx = 0; TriIdx < NumTriangles; TriIdx++)
			{
				FTriIndices& Triangle = Section.IndexBuffer->Triangles_CollisionOnly.AddDefaulted_GetRef();
				Triangle.v0 = Section.IndexBuffer->Indices[(TriIdx * 3) + 0] /* + VertexBase */;
				Triangle.v1 = Section.IndexBuffer->Indices[(TriIdx * 3) + 1] /* + VertexBase */;
				Triangle.v2 = Section.IndexBuffer->Indices[(TriIdx * 3) + 2] /* + VertexBase */;

			}
		}

		CollisionData->Indices = Section.IndexBuffer->Triangles_CollisionOnly;

		/*
		CollisionData->Indices.Empty();
		CollisionData->Indices.Reserve(NumTriangles);

		for (int32 TriIdx = 0; TriIdx < NumTriangles; TriIdx++)
		{
			// Need to add base offset for indices
			FTriIndices& Triangle = CollisionData->Indices.AddDefaulted_GetRef();
			Triangle.v0 = Section.IndexBuffer->Indices[(TriIdx * 3) + 0]  ;
			Triangle.v1 = Section.IndexBuffer->Indices[(TriIdx * 3) + 1]  ;
			Triangle.v2 = Section.IndexBuffer->Indices[(TriIdx * 3) + 2]  ;
			

			
			{
				const int32 MatNum = PositionBuffer->MaterialIndices.Num();
				if ((Triangle.v0 < MatNum) && (Triangle.v1 < MatNum) && (Triangle.v2 < MatNum))
				{
					const uint32 mat1 = PositionBuffer->MaterialIndices[Triangle.v0];
					const uint32 mat2 = PositionBuffer->MaterialIndices[Triangle.v1];
					const uint32 mat3 = PositionBuffer->MaterialIndices[Triangle.v2];

					if (mat1 == mat2 || mat1 == mat3)
						CollisionData->MaterialIndices.Add(mat1);
					else if (mat2 == mat3)
						CollisionData->MaterialIndices.Add(mat2);
					else
						CollisionData->MaterialIndices.Add(mat1);
				}
				else
					CollisionData->MaterialIndices.Add(SectionIdx);
			}
			
			//CollisionData->MaterialIndices.Add(SectionIdx);
		}*/
#endif
	}

	CollisionData->bFlipNormals = true;
	CollisionData->bDeformableMesh = true;
	CollisionData->bFastCook = true;
	
	return true;
}

bool UShaderWorldCollisionComponent::GetTriMeshSizeEstimates(FTriMeshCollisionDataEstimates& OutTriMeshEstimates,
	bool bInUseAllTriData) const
{
	if(ProcMeshSections.Num()>0)
	{
		const FGeoCProcMeshSection& Section = ProcMeshSections[0];

		if (Section.bEnableCollision && Section.PositionBuffer.IsValid() && (Section.PositionBuffer->MaterialIndices.Num() == Section.PositionBuffer->Positions3f.Num()) &&
			Section.IndexBuffer.IsValid() && Section.IndexBuffer->Indices.Num() >= 3)
			OutTriMeshEstimates.VerticeCount = Section.PositionBuffer->Positions.Num();
	}
	
	return true;
}

bool UShaderWorldCollisionComponent::ContainsPhysicsTriMeshData(bool InUseAllTriData) const
{
	for (const FGeoCProcMeshSection& Section : ProcMeshSections)
	{
		if (Section.bEnableCollision && Section.PositionBuffer.IsValid() && (Section.PositionBuffer->MaterialIndices.Num() == Section.PositionBuffer->Positions3f.Num()) &&
		Section.IndexBuffer.IsValid() && Section.IndexBuffer->Indices.Num() >= 3)
		{
			return true;
		}
	}

	return false;
}

void UShaderWorldCollisionComponent::SetUsedPhysicalMaterial(TArray<TObjectPtr<UPhysicalMaterial>>& PhysicalMaterials)
{

}

void UShaderWorldCollisionComponent::OnCreatePhysicsState()
{
	Super::OnCreatePhysicsState();
}

UBodySetup* UShaderWorldCollisionComponent::CreateBodySetupHelper()
{
	// The body setup in a template needs to be public since the property is Tnstanced and thus is the archetype of the instance meaning there is a direct reference
	UBodySetup* NewBodySetup = NewObject<UBodySetup>(this, NAME_None, (IsTemplate() ? RF_Public : RF_NoFlags));
	NewBodySetup->BodySetupGuid = FGuid::NewGuid();

	NewBodySetup->bGenerateMirroredCollision = false;
	NewBodySetup->bDoubleSidedGeometry = true;
	NewBodySetup->CollisionTraceFlag = bUseComplexAsSimpleCollision ? CTF_UseComplexAsSimple : CTF_UseDefault;

	//NewBodySetup->PhysMaterial=
	return NewBodySetup;
}

void UShaderWorldCollisionComponent::CreateProcMeshBodySetup()
{
	if (ProcMeshBodySetup == nullptr)
	{
		ProcMeshBodySetup = CreateBodySetupHelper();
	}
	if (SecondaryProcMeshBodySetup == nullptr)
	{
		SecondaryProcMeshBodySetup = CreateBodySetupHelper();
	}	
}

void UShaderWorldCollisionComponent::UpdateCollision()
{
	if(AsyncBodySetupQueue.Num()>0)
	{
		SW_LOG("No concurrent body update")
		return;
	}
	
	UWorld* World = GetWorld();
	const bool bUseAsyncCook = true;//World && World->IsGameWorld() && bUseAsyncCooking;

	if (bUseAsyncCook)
	{
		// Abort all previous ones still standing
		for (UBodySetup* OldBody : AsyncBodySetupQueue)
		{
			OldBody->AbortPhysicsMeshAsyncCreation();
		}

		//AsyncBodySetupQueue.Add(CreateBodySetupHelper());
		CreateProcMeshBodySetup();
		AsyncBodySetupQueue.Add(!BodySetupAlternator ? SecondaryProcMeshBodySetup : ProcMeshBodySetup);
	}
	else
	{
		AsyncBodySetupQueue.Empty();	//If for some reason we modified the async at runtime, just clear any pending async body setups
		CreateProcMeshBodySetup();
	}

	UBodySetup* UseBodySetup = bUseAsyncCook ? AsyncBodySetupQueue.Last() : ProcMeshBodySetup;
	
	UseBodySetup->InvalidatePhysicsData();
	// Set trace flag
	UseBodySetup->CollisionTraceFlag = bUseComplexAsSimpleCollision ? CTF_UseComplexAsSimple : CTF_UseDefault;

	if(bUseAsyncCook)
	{
		UseBodySetup->CreatePhysicsMeshesAsync(FOnAsyncPhysicsCookFinished::CreateUObject(this, &UShaderWorldCollisionComponent::FinishPhysicsAsyncCook, UseBodySetup));
	}
	else
	{
		// New GUID as collision has changed
		UseBodySetup->BodySetupGuid = FGuid::NewGuid();
		// Also we want cooked data for this
		UseBodySetup->bHasCookedCollisionData = true;
		UseBodySetup->InvalidatePhysicsData();
		UseBodySetup->CreatePhysicsMeshes();
		RecreatePhysicsState();
	}
}

void UShaderWorldCollisionComponent::FinishPhysicsAsyncCook(bool bSuccess, UBodySetup* FinishedBodySetup)
{
	
	TArray<UBodySetup*> NewQueue;
	NewQueue.Reserve(AsyncBodySetupQueue.Num());

	int32 FoundIdx;
	if(AsyncBodySetupQueue.Find(FinishedBodySetup, FoundIdx))
	{
		if (bSuccess)
		{


			if ((DestinationOnNextPhysicCook - GetComponentLocation()).SizeSquared() > 0.5f)
			{
				SetMobility(EComponentMobility::Movable);
				SetWorldLocation(DestinationOnNextPhysicCook, false, nullptr, ETeleportType::TeleportPhysics);
				SetMobility(EComponentMobility::Static);
			}
			
			/*
			 * should be the same as we don't allow concurrent processing
			 */
			//The new body was found in the array meaning it's newer so use it

			BodySetupAlternator = !BodySetupAlternator;
			//ProcMeshBodySetup = FinishedBodySetup;

			RecreatePhysicsState();

			//remove any async body setups that were requested before this one
			for (int32 AsyncIdx = FoundIdx + 1; AsyncIdx < AsyncBodySetupQueue.Num(); ++AsyncIdx)
			{
				NewQueue.Add(AsyncBodySetupQueue[AsyncIdx]);
			}

			AsyncBodySetupQueue = NewQueue;
		}
		else
		{
			AsyncBodySetupQueue.RemoveAt(FoundIdx);
		}
	}
	
	if (UpdatesReceivedDuringCompute.Num() > 0)
	{
		TSharedPtr<FSWShareableVerticePositionBuffer, ESPMode::ThreadSafe> PendingPosition = UpdatesReceivedDuringCompute[UpdatesReceivedDuringCompute.Num() - 1];

		UpdatesReceivedDuringCompute.Empty();
		
		UpdateSectionTriMesh(PendingPosition);
	}
}


void UShaderWorldCollisionComponent::UpdateNavigation()
{	
	if (CanEverAffectNavigation() && IsRegistered() && GetWorld() && GetWorld()->GetNavigationSystem() && FNavigationSystem::WantsComponentChangeNotifies())
	{		
		bNavigationRelevant = IsNavigationRelevant();
		FNavigationSystem::UpdateComponentData(*this);
	}
}

bool UShaderWorldCollisionComponent::DoCustomNavigableGeometryExport(FNavigableGeometryExport& GeomExport) const
{

	for (const FGeoCProcMeshSection& Section : ProcMeshSections)
	{
		//if (CanEverAffectNavigation())
		{
			
			const int NumOfVertex = Section.PositionBuffer->Positions3f.Num();

			const int NumIndices = Section.IndexBuffer.IsValid() ? Section.IndexBuffer->Indices.Num():0;

			GeomExport.ExportCustomMesh(Section.PositionBuffer->Positions.GetData(), NumOfVertex, (int32*)Section.IndexBuffer->Indices.GetData(), NumIndices, GetComponentTransform());
			
		}
	}
	return false;
}

UBodySetup* UShaderWorldCollisionComponent::GetBodySetup()
{
	CreateProcMeshBodySetup();
	return !BodySetupAlternator ? ProcMeshBodySetup: SecondaryProcMeshBodySetup;
}

UMaterialInterface* UShaderWorldCollisionComponent::GetMaterialFromCollisionFaceIndex(int32 FaceIndex, int32& SectionIndex) const
{
	UMaterialInterface* Result = nullptr;
	SectionIndex = 0;

	if(ProcMeshSections.Num()>0)
	{
		/*
		if (ProcMeshBodySetup)
		{
			if (ProcMeshBodySetup->bHasCookedCollisionData)// ChaosTriMeshes.Num()>0)
			{
				if (ProcMeshBodySetup->TriMeshes.Num() > 0)
				{
					SectionIndex = ProcMeshBodySetup->TriMeshes[0]->getTriangleMaterialIndex(FaceIndex);
				}
			}
		}*/
		
		
		if(ProcMeshSections[0].PositionBuffer.IsValid() && ProcMeshSections[0].IndexBuffer.IsValid())
		{
			const int32 NumTri = ProcMeshSections[0].IndexBuffer->Indices.Num()/3;

			if(FaceIndex < NumTri)
			{
				const uint32 v1 = ProcMeshSections[0].IndexBuffer->Indices[FaceIndex * 3];
				const uint32 v2 = ProcMeshSections[0].IndexBuffer->Indices[FaceIndex * 3 + 1];
				const uint32 v3 = ProcMeshSections[0].IndexBuffer->Indices[FaceIndex * 3 + 2];

				const uint32 MatNum = ProcMeshSections[0].PositionBuffer->MaterialIndices.Num();
				if((v1 < MatNum) && (v2 < MatNum) && (v3 < MatNum))
				{
					const uint32 mat1 = ProcMeshSections[0].PositionBuffer->MaterialIndices[v1];
					const uint32 mat2 = ProcMeshSections[0].PositionBuffer->MaterialIndices[v2];
					const uint32 mat3 = ProcMeshSections[0].PositionBuffer->MaterialIndices[v3];

					if (mat1 == mat2 || mat1 == mat3)
						SectionIndex = mat1;
					else if(mat2 == mat3)
						SectionIndex = mat2;
					else
						SectionIndex = mat1;
				}				

				//if(v1 < ProcMeshSections[0].PositionBuffer->MaterialIndices.Num())
				//	SectionIndex = ProcMeshSections[0].PositionBuffer->MaterialIndices[v1];
			}
		}
		
	}

	return Result;
}


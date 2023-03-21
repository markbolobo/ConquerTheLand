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

#include "Component/SWHISMComponent.h"
#include "Component/SWHISMComponentSceneProxy.h"
#include "NaniteSceneProxy.h"
#include "PrimitiveInstanceUpdateCommand.h"
#include "SWStats.h"
#include "AI/NavigationSystemBase.h"
#include "Async/Async.h"
#include "Data/SWStructs.h"

#include "Engine/StaticMesh.h"
#include "Engine/InstancedStaticMesh.h"
#include "Engine/Classes/Components/InstancedStaticMeshComponent.h"

#if WITH_EDITOR
static float GDebugBuildTreeAsyncDelayInSeconds = 0.f;
static FAutoConsoleVariableRef CVarSWDebugBuildTreeAsyncDelayInSeconds(
	TEXT("foliageSW.DebugBuildTreeAsyncDelayInSeconds"),
	GDebugBuildTreeAsyncDelayInSeconds,
	TEXT("Adds a delay (in seconds) to BuildTreeAsync tasks for debugging"));
#endif


static TAutoConsoleVariable<int32> CVarSWMinLOD(
	TEXT("foliageSW.MinLOD"),
	-1,
	TEXT("Used to discard the top LODs for performance evaluation. -1: Disable all effects of this cvar."));

static TAutoConsoleVariable<int32> CVarSWFoliageSplitFactor(
	TEXT("foliageSW.SplitFactor"),
	16,
	TEXT("This controls the branching factor of the foliage tree."));

static TAutoConsoleVariable<int32> CVarSWForceLOD(
	TEXT("foliageSW.ForceLOD"),
	-1,
	TEXT("If greater than or equal to zero, forces the foliage LOD to that level."));

static TAutoConsoleVariable<int32> CVarSWOnlyLOD(
	TEXT("foliageSW.OnlyLOD"),
	-1,
	TEXT("If greater than or equal to zero, only renders the foliage LOD at that level."));

static TAutoConsoleVariable<int32> CVarSWDisableCull(
	TEXT("foliageSW.DisableCull"),
	0,
	TEXT("If greater than zero, no culling occurs based on frustum."));

static TAutoConsoleVariable<int32> CVarSWCullAll(
	TEXT("foliageSW.CullAll"),
	0,
	TEXT("If greater than zero, everything is considered culled."),
	ECVF_Scalability);

static TAutoConsoleVariable<int32> CVarSWDitheredLOD(
	TEXT("foliageSW.DitheredLOD"),
	1,
	TEXT("If greater than zero, dithered LOD is used, otherwise popping LOD is used."));

static TAutoConsoleVariable<int32> CVarSWOverestimateLOD(
	TEXT("foliageSW.OverestimateLOD"),
	0,
	TEXT("If greater than zero and dithered LOD is not used, then we use an overestimate of LOD instead of an underestimate."));

static TAutoConsoleVariable<int32> CVarSWMaxTrianglesToRender(
	TEXT("foliageSW.MaxTrianglesToRender"),
	100000000,
	TEXT("This is an absolute limit on the number of foliage triangles to render in one traversal. This is used to prevent a silly LOD parameter mistake from causing the OS to kill the GPU."));

TAutoConsoleVariable<float> CVarSWFoliageMinimumScreenSize(
	TEXT("foliageSW.MinimumScreenSize"),
	0.000005f,
	TEXT("This controls the screen size at which we cull foliage instances entirely."),
	ECVF_Scalability);

TAutoConsoleVariable<float> CVarSWFoliageLODDistanceScale(
	TEXT("foliageSW.LODDistanceScale"),
	1.0f,
	TEXT("Scale factor for the distance used in computing LOD for foliageSW."));

TAutoConsoleVariable<float> CVarSWRandomLODRange(
	TEXT("foliageSW.RandomLODRange"),
	0.0f,
	TEXT("Random distance added to each instance distance to compute LOD."));

static TAutoConsoleVariable<int32> CVarSWMinVertsToSplitNode(
	TEXT("foliageSW.MinVertsToSplitNode"),
	8192,
	TEXT("Controls the accuracy between culling and LOD accuracy and culling and CPU performance."));

static TAutoConsoleVariable<int32> CVarSWMaxOcclusionQueriesPerComponent(
	TEXT("foliageSW.MaxOcclusionQueriesPerComponent"),
	16,
	TEXT("Controls the granularity of occlusion culling. 16-128 is a reasonable range."));

static TAutoConsoleVariable<int32> CVarSWMinOcclusionQueriesPerComponent(
	TEXT("foliageSW.MinOcclusionQueriesPerComponent"),
	6,
	TEXT("Controls the granularity of occlusion culling. 2 should be the Min."));

static TAutoConsoleVariable<int32> CVarSWMinInstancesPerOcclusionQuery(
	TEXT("foliageSW.MinInstancesPerOcclusionQuery"),
	256,
	TEXT("Controls the granualrity of occlusion culling. 1024 to 65536 is a reasonable range. This is not exact, actual minimum might be off by a factor of two."));

static TAutoConsoleVariable<float> CVarSWFoliageDensityScale(
	TEXT("foliageSW.DensityScale"),
	1.0,
	TEXT("Controls the amount of foliage to render. Foliage must opt-in to density scaling through the foliage type."),
	ECVF_Scalability);

static TAutoConsoleVariable<int32> CVarSWFoliageUseInstanceRuns(
	TEXT("foliageSW.InstanceRuns"),
	0,
	TEXT("Whether to use the InstanceRuns feature of FMeshBatch to compress foliage draw call data sent to the renderer.  Not supported by the Mesh Draw Command pipeline."));

#if RHI_RAYTRACING
static TAutoConsoleVariable<int32> CVarSWRayTracingHISM(
	TEXT("r.RayTracing.Geometry.SWHierarchicalInstancedStaticMesh"),
	1,
	TEXT("Include HISM in ray tracing effects (default = 1)"));
#endif

DECLARE_CYCLE_STAT(TEXT("Traversal Time"), STAT_FoliageTraversalTime, STATGROUP_Foliage);
DECLARE_CYCLE_STAT(TEXT("Build Time"), STAT_FoliageBuildTime, STATGROUP_Foliage);
DECLARE_CYCLE_STAT(TEXT("Batch Time"), STAT_FoliageBatchTime, STATGROUP_Foliage);
DECLARE_CYCLE_STAT(TEXT("Foliage Create Proxy"), STAT_FoliageCreateProxy, STATGROUP_Foliage);
DECLARE_CYCLE_STAT(TEXT("Foliage Post Load"), STAT_FoliagePostLoad, STATGROUP_Foliage);
DECLARE_CYCLE_STAT(TEXT("HISMC_AddInstance"), STAT_HISMCAddInstance, STATGROUP_Foliage);
DECLARE_CYCLE_STAT(TEXT("HISMC_AddInstances"), STAT_HISMCAddInstances, STATGROUP_Foliage);
DECLARE_CYCLE_STAT(TEXT("HISMC_RemoveInstance"), STAT_HISMCRemoveInstance, STATGROUP_Foliage);
DECLARE_CYCLE_STAT(TEXT("HISMC_GetDynamicMeshElement"), STAT_HISMCGetDynamicMeshElement, STATGROUP_Foliage);

DECLARE_DWORD_COUNTER_STAT(TEXT("Runs"), STAT_FoliageRuns, STATGROUP_Foliage);
DECLARE_DWORD_COUNTER_STAT(TEXT("Mesh Batches"), STAT_FoliageMeshBatches, STATGROUP_Foliage);
DECLARE_DWORD_COUNTER_STAT(TEXT("Triangles"), STAT_FoliageTriangles, STATGROUP_Foliage);
DECLARE_DWORD_COUNTER_STAT(TEXT("Instances"), STAT_FoliageInstances, STATGROUP_Foliage);
DECLARE_DWORD_COUNTER_STAT(TEXT("Occlusion Culled Instances"), STAT_OcclusionCulledFoliageInstances, STATGROUP_Foliage);
DECLARE_DWORD_COUNTER_STAT(TEXT("Traversals"), STAT_FoliageTraversals, STATGROUP_Foliage);
DECLARE_MEMORY_STAT(TEXT("Instance Buffers"), STAT_FoliageInstanceBuffers, STATGROUP_Foliage);

SIZE_T SWHISMComponentSceneProxy::GetTypeHash() const
{
	static size_t UniquePointer;
	return reinterpret_cast<size_t>(&UniquePointer);
}

 SWHISMComponentSceneProxy::SWHISMComponentSceneProxy(USWHISMComponent* InComponent, ERHIFeatureLevel::Type InFeatureLevel)
	: FInstancedStaticMeshSceneProxy(InComponent, InFeatureLevel)
	, ClusterTreePtr(InComponent->ClusterTreePtr.ToSharedRef())
	, SWInstanceSceneData(InComponent->SWInstanceSceneData)
	, SWInstanceRandomID(InComponent->SWInstanceRandomID)
	, ClusterTree(*InComponent->ClusterTreePtr)
	, UnbuiltBounds(InComponent->UnbuiltInstanceBoundsList)
	, FirstUnbuiltIndex(InComponent->NumBuiltInstances > 0 ? InComponent->NumBuiltInstances : InComponent->NumBuiltRenderInstances)
	, InstanceCountToRender(InComponent->InstanceCountToRender)
	, ViewRelevance(InComponent->GetViewRelevanceType())
	, bDitheredLODTransitions(InComponent->SupportsDitheredLODTransitions(InFeatureLevel))
	, SceneProxyCreatedFrameNumberRenderThread(UINT32_MAX)
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	, CaptureTag(0)
#endif
{
	SetupOcclusion(InComponent);
	bSupportsMeshCardRepresentation = false;

	bIsHierarchicalInstancedStaticMesh = true;
	bIsLandscapeGrass = (ViewRelevance == EHISMViewRelevanceType::Grass);
}

void SWHISMComponentSceneProxy::SetupOcclusion(UHierarchicalInstancedStaticMeshComponent* InComponent)
{
	FirstOcclusionNode = 0;
	LastOcclusionNode = 0;
	if (ClusterTree.Num() && InComponent->OcclusionLayerNumNodes)
	{
		while (true)
		{
			int32 NextFirstOcclusionNode = ClusterTree[FirstOcclusionNode].FirstChild;
			int32 NextLastOcclusionNode = ClusterTree[LastOcclusionNode].LastChild;

			if (NextFirstOcclusionNode < 0 || NextLastOcclusionNode < 0)
			{
				break;
			}
			int32 NumNodes = 1 + NextLastOcclusionNode - NextFirstOcclusionNode;
			if (NumNodes > InComponent->OcclusionLayerNumNodes)
			{
				break;
			}
			FirstOcclusionNode = NextFirstOcclusionNode;
			LastOcclusionNode = NextLastOcclusionNode;
		}
	}
	int32 NumNodes = 1 + LastOcclusionNode - FirstOcclusionNode;
	if (NumNodes < 2)
	{
		FirstOcclusionNode = -1;
		LastOcclusionNode = -1;
		NumNodes = 0;
		if (ClusterTree.Num())
		{
			//UE_LOG(LogTemp, Display, TEXT("No SubOcclusion %d inst"), 1 + ClusterTree[0].LastInstance - ClusterTree[0].FirstInstance);
		}
	}
	else
	{
		//int32 NumPerNode = (1 + ClusterTree[0].LastInstance - ClusterTree[0].FirstInstance) / NumNodes;
		//UE_LOG(LogTemp, Display, TEXT("Occlusion level %d   %d inst / node"), NumNodes, NumPerNode);
		OcclusionBounds.Reserve(NumNodes);
		FMatrix XForm = InComponent->GetRenderMatrix();
		for (int32 Index = FirstOcclusionNode; Index <= LastOcclusionNode; Index++)
		{
			OcclusionBounds.Add(FBoxSphereBounds(FBox(ClusterTree[Index].BoundMin, ClusterTree[Index].BoundMax).TransformBy(XForm)));
		}
	}	
}

#if WITH_EDITOR
void FStaticMeshInstanceBuffer::FlushGPUUpload()
{
	if (bFlushToGPUPending)
	{
		check(bDeferGPUUpload);

		if (!IsInitialized())
		{
			InitResource();
		}
		else
		{
			UpdateRHI();
		}
		bFlushToGPUPending = false;
	}
}
#endif

void SWHISMComponentSceneProxy::CreateRenderThreadResources()
{
	//double t = FPlatformTime::Seconds();
	//FInstancedStaticMeshSceneProxy::CreateRenderThreadResources();
	{
		
		FStaticMeshSceneProxy::CreateRenderThreadResources();

		
		const bool bCanUseGPUScene = UseGPUScene(GetScene().GetShaderPlatform(), GetScene().GetFeatureLevel());

		// Flush upload of GPU data for ISM/HISM
		if (ensure(InstancedRenderData.PerInstanceRenderData.IsValid()))
		{
			FStaticMeshInstanceBuffer& InstanceBuffer = InstancedRenderData.PerInstanceRenderData->InstanceBuffer;
			if (!bCanUseGPUScene)
			{				
				InstanceBuffer.FlushGPUUpload();
			}
		}
		if (bCanUseGPUScene)
		{
			bSupportsInstanceDataBuffer = true;
			/*
			 * If PerInstanceRenderData is not valid we don't create a proxy - it would mean it got invalidated between the proxy creation and now (RT ressource creation)?
			 * It's not possible has the proxy itself has a ref to the shared pointer.
			 */
			// TODO: can the PerInstanceRenderData ever not be valid here? // SW : Not to my understanding
			if (ensure(InstancedRenderData.PerInstanceRenderData.IsValid()))
			{
				const FStaticMeshInstanceBuffer& InstanceBuffer = InstancedRenderData.PerInstanceRenderData->InstanceBuffer;
				ensureMsgf(InstanceBuffer.RequireCPUAccess, TEXT("GPU-Scene instance culling requires CPU access to instance data for setup."));


				// This happens when this is actually a HISM and the data is not present in the component (which is true for landscape grass
				// which manages its own setup.
				if (InstanceSceneData.Num() == 0)
				{
					InstanceSceneData.SetNum(InstanceBuffer.GetNumInstances());
					InstanceLightShadowUVBias.SetNumZeroed(bHasPerInstanceLMSMUVBias ? InstanceBuffer.GetNumInstances() : 0);

					// Note: since bHasPerInstanceDynamicData is set based on the number (bHasPerInstanceDynamicData = InComponent->PerInstancePrevTransform.Num() == InComponent->GetInstanceCount();)
					//       in the ctor, it gets set to true when both are zero as well, for the landscape grass path (or whatever triggers initialization from the InstanceBuffer only)
					//       there is no component data to get, thus we set the bHasPerInstanceDynamicData to false.
					bHasPerInstanceDynamicData = false;
					InstanceDynamicData.Empty();

					InstanceRandomID.SetNumZeroed(bHasPerInstanceRandom ? InstanceBuffer.GetNumInstances() : 0); // Only allocate if material bound which uses this

#if WITH_EDITOR
					InstanceEditorData.SetNumZeroed(bHasPerInstanceEditorData ? InstanceBuffer.GetNumInstances() : 0);
#endif
				}

				// NOTE: we set up partial data in the construction of ISM proxy (yep, awful but the equally awful way the InstanceBuffer is maintained means complete data is not available)
				if (InstanceSceneData.Num() == InstanceBuffer.GetNumInstances())
				{
					const bool bHasRandomID = bHasPerInstanceRandom && InstanceRandomID.Num() == InstanceSceneData.Num();

					 if(SWInstanceSceneData.IsValid() && ((*SWInstanceSceneData.Get()).Num() == InstanceSceneData.Num()) &&
						(!bHasRandomID || bHasRandomID && SWInstanceRandomID.IsValid() && (SWInstanceRandomID->Num() == InstanceSceneData.Num())))
					 {
						 InstanceSceneData = *SWInstanceSceneData.Get();

					 	if(bHasRandomID)
							InstanceRandomID = *SWInstanceRandomID.Get();

					 }						
					else
					{
						//if(InstanceSceneData.Num()>0)
						{
							
						
#if SWDEBUG
						FString DebugInfo = "SWHISMComponentSceneProxy::CreateRenderThreadResources() |";

						if(!SWInstanceSceneData.IsValid())
							DebugInfo+=" !SWInstanceSceneData.IsValid() ";
						else if((*SWInstanceSceneData.Get()).Num() != InstanceSceneData.Num())
							DebugInfo += " SWInstanceSceneData->Num() "+FString::FromInt((*SWInstanceSceneData.Get()).Num())+" != InstanceSceneData.Num() "+FString::FromInt(InstanceSceneData.Num())+" ";

						if (bHasRandomID && !SWInstanceRandomID.IsValid())
							DebugInfo += " bHasRandomID && !SWInstanceRandomID.IsValid() ";
						else if (bHasRandomID && ((*SWInstanceRandomID.Get()).Num() != InstanceSceneData.Num()))
							DebugInfo += " SWInstanceRandomID->Num() " + FString::FromInt((*SWInstanceRandomID.Get()).Num()) + " != InstanceSceneData.Num() " + FString::FromInt(InstanceSceneData.Num()) + " ";

						 SW_LOG("%s", *DebugInfo)
#endif
						 for (int32 InstanceIndex = 0; InstanceIndex < InstanceSceneData.Num(); ++InstanceIndex)
						 {
							 FPrimitiveInstance& SceneData = InstanceSceneData[InstanceIndex];
							 //InstanceBuffer.GetInstanceTransform(InstanceIndex, SceneData.LocalToPrimitive);
							 InstanceBuffer.InstanceData->GetInstanceTransform(InstanceIndex, SceneData.LocalToPrimitive);

							 if (bHasRandomID)
							 {
								 InstanceBuffer.GetInstanceRandomID(InstanceIndex, InstanceRandomID[InstanceIndex]);
							 }
						 }

						}
					}
				}
				
			}
		}
	}

	SceneProxyCreatedFrameNumberRenderThread = GFrameNumberRenderThread;
	/*
	t = (FPlatformTime::Seconds()- t)*1000.0;
	if(t>0.05)
	{
		SW_LOG("create renderthreadressource %.3f ms | InstanceSceneData.Num() %d",t, InstanceSceneData.Num())
	}*/
}

FPrimitiveViewRelevance SWHISMComponentSceneProxy::GetViewRelevance(const FSceneView* View) const
{
	FPrimitiveViewRelevance Result;
	bool bShowInstancedMesh = true;
	switch (ViewRelevance)
	{
	case EHISMViewRelevanceType::Grass:
		bShowInstancedMesh = View->Family->EngineShowFlags.InstancedGrass;
		break;
	case EHISMViewRelevanceType::Foliage:
		bShowInstancedMesh = View->Family->EngineShowFlags.InstancedFoliage;
		break;
	case EHISMViewRelevanceType::HISM:
		bShowInstancedMesh = View->Family->EngineShowFlags.InstancedStaticMeshes;
		break;
	default:
		break;
	}
	if (bShowInstancedMesh)
	{
		Result = FStaticMeshSceneProxy::GetViewRelevance(View);
		Result.bDynamicRelevance = true;
		Result.bStaticRelevance = false;

		// Remove relevance for primitives marked for runtime virtual texture only.
		if (RuntimeVirtualTextures.Num() > 0 && !ShouldRenderInMainPass())
		{
			Result.bDynamicRelevance = false;
		}
	}
	return Result;
}

void SWHISMComponentSceneProxy::DrawStaticElements(FStaticPrimitiveDrawInterface* PDI)
{
	if (RuntimeVirtualTextures.Num() > 0)
	{
		// Create non-hierarchal static mesh batches for use by the runtime virtual texture rendering.
		//todo[vt]: Build an acceleration structure better suited for VT rendering maybe with batches aligned to VT pages?
		FInstancedStaticMeshSceneProxy::DrawStaticElements(PDI);
	}
}

void SWHISMComponentSceneProxy::ApplyWorldOffset(FVector InOffset)
{
	FInstancedStaticMeshSceneProxy::ApplyWorldOffset(InOffset);

	for (FBoxSphereBounds& Item : OcclusionBounds)
	{
		Item.Origin += InOffset;
	}
}

struct FFoliageRenderInstanceParams : public FOneFrameResource
{
	bool bNeedsSingleLODRuns;
	bool bNeedsMultipleLODRuns;
	bool bOverestimate;
	mutable TArray<uint32, SceneRenderingAllocator> MultipleLODRuns[MAX_STATIC_MESH_LODS];
	mutable TArray<uint32, SceneRenderingAllocator> SingleLODRuns[MAX_STATIC_MESH_LODS];
	mutable int32 TotalSingleLODInstances[MAX_STATIC_MESH_LODS];
	mutable int32 TotalMultipleLODInstances[MAX_STATIC_MESH_LODS];

	FFoliageRenderInstanceParams(bool InbNeedsSingleLODRuns, bool InbNeedsMultipleLODRuns, bool InbOverestimate)
		: bNeedsSingleLODRuns(InbNeedsSingleLODRuns)
		, bNeedsMultipleLODRuns(InbNeedsMultipleLODRuns)
		, bOverestimate(InbOverestimate)
	{
		for (int32 Index = 0; Index < MAX_STATIC_MESH_LODS; Index++)
		{
			TotalSingleLODInstances[Index] = 0;
			TotalMultipleLODInstances[Index] = 0;
		}
	}
	static FORCEINLINE_DEBUGGABLE void AddRun(TArray<uint32, SceneRenderingAllocator>& Array, int32 FirstInstance, int32 LastInstance)
	{
		if (Array.Num() && Array.Last() + 1 == FirstInstance)
		{
			Array.Last() = (uint32)LastInstance;
		}
		else
		{
			Array.Add((uint32)FirstInstance);
			Array.Add((uint32)LastInstance);
		}
	}
	FORCEINLINE_DEBUGGABLE void AddRun(int32 MinLod, int32 MaxLod, int32 FirstInstance, int32 LastInstance) const
	{
		if (bNeedsSingleLODRuns)
		{
			int32 CurrentLOD = bOverestimate ? MaxLod : MinLod;

			if (CurrentLOD < MAX_STATIC_MESH_LODS)
			{
				AddRun(SingleLODRuns[CurrentLOD], FirstInstance, LastInstance);
				TotalSingleLODInstances[CurrentLOD] += 1 + LastInstance - FirstInstance;
			}
		}
		if (bNeedsMultipleLODRuns)
		{
			for (int32 Lod = MinLod; Lod <= MaxLod; Lod++)
			{
				if (Lod < MAX_STATIC_MESH_LODS)
				{
					TotalMultipleLODInstances[Lod] += 1 + LastInstance - FirstInstance;
					AddRun(MultipleLODRuns[Lod], FirstInstance, LastInstance);
				}
			}
		}
	}

	FORCEINLINE_DEBUGGABLE void AddRun(int32 MinLod, int32 MaxLod, const FClusterNode& Node) const
	{
		AddRun(MinLod, MaxLod, Node.FirstInstance, Node.LastInstance);
	}
};

struct FFoliageCullInstanceParams : public FFoliageRenderInstanceParams
{
	FConvexVolume ViewFrustumLocal;
	int32 MinInstancesToSplit[MAX_STATIC_MESH_LODS];
	const TArray<FClusterNode>& Tree;
	const FSceneView* View;
	FVector ViewOriginInLocalZero;
	FVector ViewOriginInLocalOne;
	int32 LODs;
	float LODPlanesMax[MAX_STATIC_MESH_LODS];
	float LODPlanesMin[MAX_STATIC_MESH_LODS];
	int32 FirstOcclusionNode;
	int32 LastOcclusionNode;
	const TArray<bool>* OcclusionResults;
	int32 OcclusionResultsStart;



	FFoliageCullInstanceParams(bool InbNeedsSingleLODRuns, bool InbNeedsMultipleLODRuns, bool InbOverestimate, const TArray<FClusterNode>& InTree)
		: FFoliageRenderInstanceParams(InbNeedsSingleLODRuns, InbNeedsMultipleLODRuns, InbOverestimate)
		, Tree(InTree)
		, FirstOcclusionNode(-1)
		, LastOcclusionNode(-1)
		, OcclusionResults(nullptr)
		, OcclusionResultsStart(0)
	{
	}
};

static bool GUseVectorCull = true;

static void ToggleUseVectorCull(const TArray<FString>& Args)
{
	GUseVectorCull = !GUseVectorCull;
}

static FAutoConsoleCommand ToggleUseVectorCullCmd(
	TEXT("foliageSW.ToggleVectorCull"),
	TEXT("Useful for debugging. Toggles the optimized cull."),
	FConsoleCommandWithArgsDelegate::CreateStatic(&ToggleUseVectorCull)
);

static uint32 GFrameNumberRenderThread_CaptureFoliageRuns = MAX_uint32;

static void LogFoliageFrame(const TArray<FString>& Args)
{
	GFrameNumberRenderThread_CaptureFoliageRuns = GFrameNumberRenderThread + 2;
}

static FAutoConsoleCommand LogFoliageFrameCmd(
	TEXT("foliageSW.LogFoliageFrame"),
	TEXT("Useful for debugging. Logs all foliage rendered in a frame."),
	FConsoleCommandWithArgsDelegate::CreateStatic(&LogFoliageFrame)
);

const VectorRegister		VECTOR_HALF_HALF_HALF_ZERO = DECLARE_VECTOR_REGISTER(0.5f, 0.5f, 0.5f, 0.0f);

template<bool TUseVector>
static FORCEINLINE_DEBUGGABLE bool CullNode(const FFoliageCullInstanceParams& Params, const FClusterNode& Node, bool& bOutFullyContained)
{
	if (TUseVector)
	{
		checkSlow(Params.ViewFrustumLocal.PermutedPlanes.Num() == 4);

		//@todo, once we have more than one mesh per tree, these should be aligned
		VectorRegister BoxMin = VectorLoadFloat3(&Node.BoundMin);
		VectorRegister BoxMax = VectorLoadFloat3(&Node.BoundMax);

		VectorRegister BoxDiff = VectorSubtract(BoxMax, BoxMin);
		VectorRegister BoxSum = VectorAdd(BoxMax, BoxMin);

		// Load the origin & extent
		VectorRegister Orig = VectorMultiply(VECTOR_HALF_HALF_HALF_ZERO, BoxSum);
		VectorRegister Ext = VectorMultiply(VECTOR_HALF_HALF_HALF_ZERO, BoxDiff);
		// Splat origin into 3 vectors
		VectorRegister OrigX = VectorReplicate(Orig, 0);
		VectorRegister OrigY = VectorReplicate(Orig, 1);
		VectorRegister OrigZ = VectorReplicate(Orig, 2);
		// Splat the abs for the pushout calculation
		VectorRegister AbsExtentX = VectorReplicate(Ext, 0);
		VectorRegister AbsExtentY = VectorReplicate(Ext, 1);
		VectorRegister AbsExtentZ = VectorReplicate(Ext, 2);
		// Since we are moving straight through get a pointer to the data
		const FPlane* RESTRICT PermutedPlanePtr = (FPlane*)Params.ViewFrustumLocal.PermutedPlanes.GetData();
		// Process four planes at a time until we have < 4 left
		// Load 4 planes that are already all Xs, Ys, ...
		VectorRegister PlanesX = VectorLoadAligned(&PermutedPlanePtr[0]);
		VectorRegister PlanesY = VectorLoadAligned(&PermutedPlanePtr[1]);
		VectorRegister PlanesZ = VectorLoadAligned(&PermutedPlanePtr[2]);
		VectorRegister PlanesW = VectorLoadAligned(&PermutedPlanePtr[3]);
		// Calculate the distance (x * x) + (y * y) + (z * z) - w
		VectorRegister DistX = VectorMultiply(OrigX, PlanesX);
		VectorRegister DistY = VectorMultiplyAdd(OrigY, PlanesY, DistX);
		VectorRegister DistZ = VectorMultiplyAdd(OrigZ, PlanesZ, DistY);
		VectorRegister Distance = VectorSubtract(DistZ, PlanesW);
		// Now do the push out FMath::Abs(x * x) + FMath::Abs(y * y) + FMath::Abs(z * z)
		VectorRegister PushX = VectorMultiply(AbsExtentX, VectorAbs(PlanesX));
		VectorRegister PushY = VectorMultiplyAdd(AbsExtentY, VectorAbs(PlanesY), PushX);
		VectorRegister PushOut = VectorMultiplyAdd(AbsExtentZ, VectorAbs(PlanesZ), PushY);
		VectorRegister PushOutNegative = VectorNegate(PushOut);

		bOutFullyContained = !VectorAnyGreaterThan(Distance, PushOutNegative);
		// Check for completely outside
		return !!VectorAnyGreaterThan(Distance, PushOut);
	}
	FVector Center = (FVector)(Node.BoundMin + Node.BoundMax) * 0.5f;
	FVector Extent = (FVector)(Node.BoundMax - Node.BoundMin) * 0.5f;
	if (!Params.ViewFrustumLocal.IntersectBox(Center, Extent, bOutFullyContained))
	{
		return true;
	}
	return false;
}

inline void CalcLOD(int32& InOutMinLOD, int32& InOutMaxLOD, const FVector& BoundMin, const FVector& BoundMax, const FVector& ViewOriginInLocalZero, const FVector& ViewOriginInLocalOne, const float(&LODPlanesMin)[MAX_STATIC_MESH_LODS], const float(&LODPlanesMax)[MAX_STATIC_MESH_LODS])
{
	if (InOutMinLOD != InOutMaxLOD)
	{
		const FVector Center = (BoundMax + BoundMin) * 0.5f;
		const float DistCenterZero = FVector::Dist(Center, ViewOriginInLocalZero);
		const float DistCenterOne = FVector::Dist(Center, ViewOriginInLocalOne);
		const float HalfWidth = FVector::Dist(BoundMax, BoundMin) * 0.5f;
		const float NearDot = FMath::Min(DistCenterZero, DistCenterOne) - HalfWidth;
		const float FarDot = FMath::Max(DistCenterZero, DistCenterOne) + HalfWidth;

		while (InOutMaxLOD > InOutMinLOD && NearDot > LODPlanesMax[InOutMinLOD])
		{
			InOutMinLOD++;
		}
		while (InOutMaxLOD > InOutMinLOD && FarDot < LODPlanesMin[InOutMaxLOD - 1])
		{
			InOutMaxLOD--;
		}
	}
}

inline bool CanGroup(const FVector& BoundMin, const FVector& BoundMax, const FVector& ViewOriginInLocalZero, const FVector& ViewOriginInLocalOne, float MaxDrawDist)
{
	const FVector Center = (BoundMax + BoundMin) * 0.5f;
	const float DistCenterZero = FVector::Dist(Center, ViewOriginInLocalZero);
	const float DistCenterOne = FVector::Dist(Center, ViewOriginInLocalOne);
	const float HalfWidth = FVector::Dist(BoundMax, BoundMin) * 0.5f;
	const float FarDot = FMath::Max(DistCenterZero, DistCenterOne) + HalfWidth;

	// We are sure that everything in the bound won't be distance culled
	return FarDot < MaxDrawDist;
}



template<bool TUseVector>
void SWHISMComponentSceneProxy::Traverse(const FFoliageCullInstanceParams& Params, int32 Index, int32 MinLOD, int32 MaxLOD, bool bFullyContained) const
{
	const FClusterNode& Node = Params.Tree[Index];
	if (!bFullyContained)
	{
		if (CullNode<TUseVector>(Params, Node, bFullyContained))
		{
			return;
		}
	}

	if (MinLOD != MaxLOD)
	{
		CalcLOD(MinLOD, MaxLOD, (FVector)Node.BoundMin, (FVector)Node.BoundMax, Params.ViewOriginInLocalZero, Params.ViewOriginInLocalOne, Params.LODPlanesMin, Params.LODPlanesMax);

		if (MinLOD >= Params.LODs)
		{
			return;
		}
	}

	if (Index >= Params.FirstOcclusionNode && Index <= Params.LastOcclusionNode)
	{
		check(Params.OcclusionResults != NULL);
		const TArray<bool>& OcclusionResultsArray = *Params.OcclusionResults;
		if (OcclusionResultsArray[Params.OcclusionResultsStart + Index - Params.FirstOcclusionNode])
		{
			INC_DWORD_STAT_BY(STAT_OcclusionCulledFoliageInstances, 1 + Node.LastInstance - Node.FirstInstance);
			return;
		}
	}

	bool bShouldGroup = Node.FirstChild < 0
		|| ((Node.LastInstance - Node.FirstInstance + 1) < Params.MinInstancesToSplit[MinLOD]
			&& CanGroup((FVector)Node.BoundMin, (FVector)Node.BoundMax, Params.ViewOriginInLocalZero, Params.ViewOriginInLocalOne, Params.LODPlanesMax[Params.LODs - 1]));
	bool bSplit = (!bFullyContained || MinLOD < MaxLOD || Index < Params.FirstOcclusionNode)
		&& !bShouldGroup;

	if (!bSplit)
	{
		MaxLOD = FMath::Min(MaxLOD, Params.LODs - 1);
		Params.AddRun(MinLOD, MaxLOD, Node);
		return;
	}
	for (int32 ChildIndex = Node.FirstChild; ChildIndex <= Node.LastChild; ChildIndex++)
	{
		Traverse<TUseVector>(Params, ChildIndex, MinLOD, MaxLOD, bFullyContained);
	}
}

struct FFoliageElementParams
{
	const FInstancingUserData* PassUserData[2];
	int32 NumSelectionGroups;
	const FSceneView* View;
	int32 ViewIndex;
	bool bSelectionRenderEnabled;
	bool BatchRenderSelection[2];
	bool bIsWireframe;
	bool bUseHoveredMaterial;
	bool bUseInstanceRuns;
	bool bBlendLODs;
	ERHIFeatureLevel::Type FeatureLevel;
	bool ShadowFrustum;
	float FinalCullDistance;
};

void SWHISMComponentSceneProxy::FillDynamicMeshElements(FMeshElementCollector& Collector, const FFoliageElementParams& ElementParams, const FFoliageRenderInstanceParams& Params) const
{
	SCOPE_CYCLE_COUNTER(STAT_FoliageBatchTime);
	int64 TotalTriangles = 0;

	int32 OnlyLOD = FMath::Min<int32>(CVarSWOnlyLOD.GetValueOnRenderThread(), InstancedRenderData.VertexFactories.Num() - 1);
	int32 FirstLOD = FMath::Max((OnlyLOD < 0) ? 0 : OnlyLOD, static_cast<int32>(this->GetCurrentFirstLODIdx_Internal()));
	int32 LastLODPlusOne = (OnlyLOD < 0) ? InstancedRenderData.VertexFactories.Num() : (OnlyLOD + 1);

	const bool bUseGPUScene = UseGPUScene(GetScene().GetShaderPlatform(), GetScene().GetFeatureLevel());

	for (int32 LODIndex = FirstLOD; LODIndex < LastLODPlusOne; LODIndex++)
	{
		const FStaticMeshLODResources& LODModel = RenderData->LODResources[LODIndex];

		for (int32 SelectionGroupIndex = 0; SelectionGroupIndex < ElementParams.NumSelectionGroups; SelectionGroupIndex++)
		{
			for (int32 SectionIndex = 0; SectionIndex < LODModel.Sections.Num(); SectionIndex++)
			{
				const FLODInfo& ProxyLODInfo = LODs[LODIndex];
				UMaterialInterface* Material = ProxyLODInfo.Sections[SectionIndex].Material;
				const bool bDitherLODEnabled = ElementParams.bBlendLODs;

				TArray<uint32, SceneRenderingAllocator>& RunArray = bDitherLODEnabled ? Params.MultipleLODRuns[LODIndex] : Params.SingleLODRuns[LODIndex];

				if (!RunArray.Num())
				{
					continue;
				}

				if (bUseGPUScene)
				{
					FMeshBatch& MeshBatch = Collector.AllocateMesh();
					INC_DWORD_STAT(STAT_FoliageMeshBatches);

					if (!FStaticMeshSceneProxy::GetMeshElement(LODIndex, 0, SectionIndex, GetDepthPriorityGroup(ElementParams.View), ElementParams.BatchRenderSelection[SelectionGroupIndex], true, MeshBatch))
					{
						continue;
					}

					checkSlow(MeshBatch.GetNumPrimitives() > 0);
					MeshBatch.bCanApplyViewModeOverrides = true;
					MeshBatch.bUseSelectionOutline = ElementParams.BatchRenderSelection[SelectionGroupIndex];
					MeshBatch.bUseWireframeSelectionColoring = ElementParams.BatchRenderSelection[SelectionGroupIndex];
					MeshBatch.bUseAsOccluder = ShouldUseAsOccluder();
					MeshBatch.VertexFactory = &InstancedRenderData.VertexFactories[LODIndex];

					FMeshBatchElement& MeshBatchElement = MeshBatch.Elements[0];
					MeshBatchElement.UserData = ElementParams.PassUserData[SelectionGroupIndex];
					MeshBatchElement.bUserDataIsColorVertexBuffer = false;
					MeshBatchElement.MaxScreenSize = 1.0;
					MeshBatchElement.MinScreenSize = 0.0;
					MeshBatchElement.InstancedLODIndex = LODIndex;
					MeshBatchElement.InstancedLODRange = bDitherLODEnabled ? 1 : 0;
					MeshBatchElement.PrimitiveUniformBuffer = GetUniformBuffer();

					int32 TotalInstances = bDitherLODEnabled ? Params.TotalMultipleLODInstances[LODIndex] : Params.TotalSingleLODInstances[LODIndex];
					{
						const int64 Tris = int64(TotalInstances) * int64(MeshBatchElement.NumPrimitives);
						TotalTriangles += Tris;

					}

					//MeshBatchElement.NumInstances = TotalInstances;
					// The index was used as an offset, but the dynamic buffer thing uses a resource view to make this not needed (using PrimitiveInstanceSceneDataOffset as a temp. debug help)
					MeshBatchElement.UserIndex = 0;

					// Note: this call overrides the UserIndex to mean the command index, which is used to fetch the offset to the instance array
					//Collector.AllocateInstancedBatchArguments(ElementParams.ViewIndex, MeshBatch, PrimitiveInstanceSceneDataOffset, PrimitiveInstanceDataCount, RunArray);

					// We use this existing hook to send info about the runs over to the visible mesh batch
					MeshBatchElement.NumInstances = RunArray.Num() / 2;
					MeshBatchElement.InstanceRuns = &RunArray[0];
					MeshBatchElement.bIsInstanceRuns = true;

					if (TotalTriangles < (int64)CVarSWMaxTrianglesToRender.GetValueOnRenderThread())
					{
						Collector.AddMesh(ElementParams.ViewIndex, MeshBatch);

						if (OverlayMaterial != nullptr)
						{
							FMeshBatch& OverlayMeshBatch = Collector.AllocateMesh();
							OverlayMeshBatch = MeshBatch;
							OverlayMeshBatch.bOverlayMaterial = true;
							OverlayMeshBatch.CastShadow = false;
							OverlayMeshBatch.bSelectable = false;
							OverlayMeshBatch.MaterialRenderProxy = OverlayMaterial->GetRenderProxy();
							// make sure overlay is always rendered on top of base mesh
							OverlayMeshBatch.MeshIdInPrimitive += LODModel.Sections.Num();
							Collector.AddMesh(ElementParams.ViewIndex, OverlayMeshBatch);
						}
					}
				}
				else
				{
					int32 NumBatches = 1;
					int32 CurrentRun = 0;
					int32 CurrentInstance = 0;
					int32 RemainingInstances = bDitherLODEnabled ? Params.TotalMultipleLODInstances[LODIndex] : Params.TotalSingleLODInstances[LODIndex];
					int32 RemainingRuns = RunArray.Num() / 2;

					if (!ElementParams.bUseInstanceRuns)
					{
						NumBatches = FMath::DivideAndRoundUp(RemainingRuns, (int32)FInstancedStaticMeshVertexFactory::NumBitsForVisibilityMask());
					}


					bool bDidStats = false;
					for (int32 BatchIndex = 0; BatchIndex < NumBatches; BatchIndex++)
					{
						FMeshBatch& MeshElement = Collector.AllocateMesh();
						INC_DWORD_STAT(STAT_FoliageMeshBatches);

						if (!FStaticMeshSceneProxy::GetMeshElement(LODIndex, 0, SectionIndex, GetDepthPriorityGroup(ElementParams.View), ElementParams.BatchRenderSelection[SelectionGroupIndex], true, MeshElement))
						{
							continue;
						}
						checkSlow(MeshElement.GetNumPrimitives() > 0);

						MeshElement.VertexFactory = &InstancedRenderData.VertexFactories[LODIndex];
						FMeshBatchElement& BatchElement0 = MeshElement.Elements[0];

						BatchElement0.UserData = ElementParams.PassUserData[SelectionGroupIndex];
						BatchElement0.bUserDataIsColorVertexBuffer = false;
						BatchElement0.MaxScreenSize = 1.0;
						BatchElement0.MinScreenSize = 0.0;
						BatchElement0.InstancedLODIndex = LODIndex;
						BatchElement0.InstancedLODRange = bDitherLODEnabled ? 1 : 0;
						BatchElement0.PrimitiveUniformBuffer = GetUniformBuffer();
						MeshElement.bCanApplyViewModeOverrides = true;
						MeshElement.bUseSelectionOutline = ElementParams.BatchRenderSelection[SelectionGroupIndex];
						MeshElement.bUseWireframeSelectionColoring = ElementParams.BatchRenderSelection[SelectionGroupIndex];
						MeshElement.bUseAsOccluder = ShouldUseAsOccluder();

						if (!bDidStats)
						{
							bDidStats = true;
							int64 Tris = int64(RemainingInstances) * int64(BatchElement0.NumPrimitives);
							TotalTriangles += Tris;

						}
						if (ElementParams.bUseInstanceRuns)
						{
							BatchElement0.NumInstances = RunArray.Num() / 2;
							BatchElement0.InstanceRuns = &RunArray[0];
							BatchElement0.bIsInstanceRuns = true;

						}
						else
						{
							const uint32 NumElementsThisBatch = FMath::Min(RemainingRuns, (int32)FInstancedStaticMeshVertexFactory::NumBitsForVisibilityMask());

							MeshElement.Elements.Reserve(NumElementsThisBatch);
							check(NumElementsThisBatch);

							for (uint32 InstanceRun = 0; InstanceRun < NumElementsThisBatch; ++InstanceRun)
							{
								FMeshBatchElement* NewBatchElement;

								if (InstanceRun == 0)
								{
									NewBatchElement = &MeshElement.Elements[0];
								}
								else
								{
									NewBatchElement = &MeshElement.Elements.AddDefaulted_GetRef();
									*NewBatchElement = MeshElement.Elements[0];
								}

								const int32 InstanceOffset = RunArray[CurrentRun];
								NewBatchElement->UserIndex = InstanceOffset;
								NewBatchElement->NumInstances = 1 + RunArray[CurrentRun + 1] - InstanceOffset;

								if (--RemainingRuns)
								{
									CurrentRun += 2;
									check(CurrentRun + 1 < RunArray.Num());
								}
							}
						}

						if (TotalTriangles < (int64)CVarSWMaxTrianglesToRender.GetValueOnRenderThread())
						{
							Collector.AddMesh(ElementParams.ViewIndex, MeshElement);

							if (OverlayMaterial != nullptr)
							{
								FMeshBatch& OverlayMeshBatch = Collector.AllocateMesh();
								OverlayMeshBatch = MeshElement;
								OverlayMeshBatch.bOverlayMaterial = true;
								OverlayMeshBatch.CastShadow = false;
								OverlayMeshBatch.bSelectable = false;
								OverlayMeshBatch.MaterialRenderProxy = OverlayMaterial->GetRenderProxy();
								// make sure overlay is always rendered on top of base mesh
								OverlayMeshBatch.MeshIdInPrimitive += LODModel.Sections.Num();
								Collector.AddMesh(ElementParams.ViewIndex, OverlayMeshBatch);
							}
						}
					}
				}
			}
		}
	}


}

void SWHISMComponentSceneProxy::GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const
{
	if (Views[0]->bRenderFirstInstanceOnly)
	{
		FInstancedStaticMeshSceneProxy::GetDynamicMeshElements(Views, ViewFamily, VisibilityMap, Collector);
		return;
	}

	QUICK_SCOPE_CYCLE_COUNTER(STAT_HierarchicalInstancedStaticMeshSceneProxy_GetMeshElements);
	SCOPE_CYCLE_COUNTER(STAT_HISMCGetDynamicMeshElement);

	bool bMultipleSections = ALLOW_DITHERED_LOD_FOR_INSTANCED_STATIC_MESHES && bDitheredLODTransitions && CVarSWDitheredLOD.GetValueOnRenderThread() > 0;
	bool bSingleSections = !bMultipleSections;
	bool bOverestimate = CVarSWOverestimateLOD.GetValueOnRenderThread() > 0;

	int32 MinVertsToSplitNode = CVarSWMinVertsToSplitNode.GetValueOnRenderThread();

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		if (VisibilityMap & (1 << ViewIndex))
		{
			const FSceneView* View = Views[ViewIndex];

			FFoliageElementParams ElementParams;
			ElementParams.bSelectionRenderEnabled = GIsEditor && ViewFamily.EngineShowFlags.Selection;
			ElementParams.NumSelectionGroups = (ElementParams.bSelectionRenderEnabled && bHasSelectedInstances) ? 2 : 1;
			ElementParams.PassUserData[0] = bHasSelectedInstances && ElementParams.bSelectionRenderEnabled ? &UserData_SelectedInstances : &UserData_AllInstances;
			ElementParams.PassUserData[1] = &UserData_DeselectedInstances;
			ElementParams.BatchRenderSelection[0] = ElementParams.bSelectionRenderEnabled && IsSelected();
			ElementParams.BatchRenderSelection[1] = false;
			ElementParams.bIsWireframe = ViewFamily.EngineShowFlags.Wireframe;
			ElementParams.bUseHoveredMaterial = IsHovered();
			ElementParams.bUseInstanceRuns = (CVarSWFoliageUseInstanceRuns.GetValueOnRenderThread() > 0);
			ElementParams.FeatureLevel = InstancedRenderData.FeatureLevel;
			ElementParams.ViewIndex = ViewIndex;
			ElementParams.View = View;

			// Render built instances
			if (ClusterTree.Num())
			{
				FFoliageCullInstanceParams& InstanceParams = Collector.AllocateOneFrameResource<FFoliageCullInstanceParams>(bSingleSections, bMultipleSections, bOverestimate, ClusterTree);
				InstanceParams.LODs = RenderData->LODResources.Num();

				InstanceParams.View = View;

				FMatrix WorldToLocal = GetLocalToWorld().Inverse();
				bool bUseVectorCull = GUseVectorCull;
				bool bIsOrtho = false;

				bool bDisableCull = !!CVarSWDisableCull.GetValueOnRenderThread();
				ElementParams.ShadowFrustum = !!View->GetDynamicMeshElementsShadowCullFrustum();
				if (View->GetDynamicMeshElementsShadowCullFrustum())
				{
					for (int32 Index = 0; Index < View->GetDynamicMeshElementsShadowCullFrustum()->Planes.Num(); Index++)
					{
						FPlane Src = View->GetDynamicMeshElementsShadowCullFrustum()->Planes[Index];
						FPlane Norm = Src / Src.Size();
						// remove world space preview translation
						Norm.W -= (FVector(Norm) | View->GetPreShadowTranslation());
						FPlane Local = Norm.TransformBy(WorldToLocal);
						FPlane LocalNorm = Local / Local.Size();
						InstanceParams.ViewFrustumLocal.Planes.Add(LocalNorm);
					}
					bUseVectorCull = InstanceParams.ViewFrustumLocal.Planes.Num() == 4;
				}
				else
				{
					// build view frustum with no near plane / no far plane in frustum (far plane culling is done later in the function) : 
					static constexpr bool bViewFrustumUsesNearPlane = false;
					static constexpr bool bViewFrustumUsesFarPlane = false;
					const bool bIsPerspectiveProjection = View->ViewMatrices.IsPerspectiveProjection();

					// Instanced stereo needs to use the right plane from the right eye when constructing the frustum bounds to cull against.
					// Otherwise we'll cull objects visible in the right eye, but not the left.
					if ((View->IsInstancedStereoPass() || View->bIsMobileMultiViewEnabled) && IStereoRendering::IsStereoEyeView(*View) && GEngine->StereoRenderingDevice.IsValid())
					{
						// TODO: Stereo culling frustum needs to use the culling origin instead of the view origin.
						InstanceParams.ViewFrustumLocal = View->CullingFrustum;
						for (FPlane& Plane : InstanceParams.ViewFrustumLocal.Planes)
						{
							Plane = Plane.TransformBy(WorldToLocal);
						}
						InstanceParams.ViewFrustumLocal.Init();

						// Invalid bounds retrieved, so skip render of this frame :
						if (bIsPerspectiveProjection && (InstanceParams.ViewFrustumLocal.Planes.Num() != 4))
						{
							// Report the error as a warning (instead of an ensure or a check) as the problem can come from improper user data (invalid transform or view-proj matrix) : 
							ensureMsgf(false, TEXT("Invalid frustum, skipping render of HISM"));
							continue;
						}
					}
					else
					{
						FMatrix LocalViewProjForCulling = GetLocalToWorld() * View->ViewMatrices.GetViewProjectionMatrix();

						GetViewFrustumBounds(InstanceParams.ViewFrustumLocal, LocalViewProjForCulling, bViewFrustumUsesNearPlane, bViewFrustumUsesFarPlane);

						// Invalid bounds retrieved, so skip render of this frame :
						if (bIsPerspectiveProjection && (InstanceParams.ViewFrustumLocal.Planes.Num() != 4))
						{
							// Report the error as a warning (instead of an ensure or a check) as the problem can come from improper user data (invalid transform or view-proj matrix) : 
							ensureMsgf(false, TEXT("Invalid frustum, skipping render of HISM : culling view projection matrix:%s"), *LocalViewProjForCulling.ToString());
							continue;
						}
					}

					if (bIsPerspectiveProjection)
					{
						check(InstanceParams.ViewFrustumLocal.Planes.Num() == 4);

						FMatrix ThreePlanes;
						ThreePlanes.SetIdentity();
						ThreePlanes.SetAxes(&InstanceParams.ViewFrustumLocal.Planes[0], &InstanceParams.ViewFrustumLocal.Planes[1], &InstanceParams.ViewFrustumLocal.Planes[2]);
						FVector ProjectionOrigin = ThreePlanes.Inverse().GetTransposed().TransformVector(FVector(InstanceParams.ViewFrustumLocal.Planes[0].W, InstanceParams.ViewFrustumLocal.Planes[1].W, InstanceParams.ViewFrustumLocal.Planes[2].W));

						for (int32 Index = 0; Index < InstanceParams.ViewFrustumLocal.Planes.Num(); Index++)
						{
							FPlane Src = InstanceParams.ViewFrustumLocal.Planes[Index];
							FVector Normal = Src.GetSafeNormal();
							InstanceParams.ViewFrustumLocal.Planes[Index] = FPlane(Normal, Normal | ProjectionOrigin);
						}
					}
					else
					{
						bIsOrtho = true;
						bUseVectorCull = false;
					}
				}
				if (!InstanceParams.ViewFrustumLocal.Planes.Num())
				{
					bDisableCull = true;
				}
				else
				{
					InstanceParams.ViewFrustumLocal.Init();
				}

				ElementParams.bBlendLODs = bMultipleSections;

				InstanceParams.ViewOriginInLocalZero = WorldToLocal.TransformPosition(View->GetTemporalLODOrigin(0, bMultipleSections));
				InstanceParams.ViewOriginInLocalOne = WorldToLocal.TransformPosition(View->GetTemporalLODOrigin(1, bMultipleSections));

				float MinSize = bIsOrtho ? 0.0f : CVarSWFoliageMinimumScreenSize.GetValueOnRenderThread();
				float LODScale = CVarSWFoliageLODDistanceScale.GetValueOnRenderThread();
				float LODRandom = CVarSWRandomLODRange.GetValueOnRenderThread();
				float MaxDrawDistanceScale = GetCachedScalabilityCVars().ViewDistanceScale;

				FVector AverageScale(InstanceParams.Tree[0].MinInstanceScale + (InstanceParams.Tree[0].MaxInstanceScale - InstanceParams.Tree[0].MinInstanceScale) / 2.0f);
				FBoxSphereBounds ScaledBounds = RenderData->Bounds.TransformBy(FTransform(FRotator::ZeroRotator, FVector::ZeroVector, AverageScale));
				float SphereRadius = ScaledBounds.SphereRadius;

				float FinalCull = MAX_flt;
				if (MinSize > 0.0)
				{
					FinalCull = ComputeBoundsDrawDistance(MinSize, SphereRadius, View->ViewMatrices.GetProjectionMatrix()) * LODScale;
				}
				if (View->SceneViewInitOptions.OverrideFarClippingPlaneDistance > 0.0f)
				{
					FinalCull = FMath::Min(FinalCull, View->SceneViewInitOptions.OverrideFarClippingPlaneDistance * MaxDrawDistanceScale);
				}
				if (UserData_AllInstances.EndCullDistance > 0.0f)
				{
					FinalCull = FMath::Min(FinalCull, UserData_AllInstances.EndCullDistance * MaxDrawDistanceScale);
				}
				ElementParams.FinalCullDistance = FinalCull;

				for (int32 LODIndex = 1; LODIndex < InstanceParams.LODs; LODIndex++)
				{
					float Distance = ComputeBoundsDrawDistance(RenderData->ScreenSize[LODIndex].GetValue(), SphereRadius, View->ViewMatrices.GetProjectionMatrix()) * LODScale;
					InstanceParams.LODPlanesMin[LODIndex - 1] = FMath::Min(FinalCull - LODRandom, Distance - LODRandom);
					InstanceParams.LODPlanesMax[LODIndex - 1] = FMath::Min(FinalCull, Distance);
				}
				InstanceParams.LODPlanesMin[InstanceParams.LODs - 1] = FinalCull - LODRandom;
				InstanceParams.LODPlanesMax[InstanceParams.LODs - 1] = FinalCull;

				// Added assert guard to track issue UE-53944
				check(InstanceParams.LODs <= 8);
				check(RenderData != nullptr);

				for (int32 LODIndex = 0; LODIndex < InstanceParams.LODs; LODIndex++)
				{
					InstanceParams.MinInstancesToSplit[LODIndex] = 2;

					// Added assert guard to track issue UE-53944
					check(RenderData->LODResources.IsValidIndex(LODIndex));

					int32 NumVerts = RenderData->LODResources[LODIndex].VertexBuffers.StaticMeshVertexBuffer.GetNumVertices();
					if (NumVerts)
					{
						InstanceParams.MinInstancesToSplit[LODIndex] = MinVertsToSplitNode / NumVerts;
					}
				}

				if (FirstOcclusionNode >= 0 && LastOcclusionNode >= 0 && FirstOcclusionNode <= LastOcclusionNode)
				{
					uint32 ViewId = View->GetViewKey();
					const FFoliageOcclusionResults* OldResults = OcclusionResults.Find(ViewId);
					if (OldResults &&
						OldResults->FrameNumberRenderThread == GFrameNumberRenderThread &&
						1 + LastOcclusionNode - FirstOcclusionNode == OldResults->NumResults &&
						// OcclusionResultsArray[Params.OcclusionResultsStart + Index - Params.FirstOcclusionNode]

						OldResults->Results.IsValidIndex(OldResults->ResultsStart) &&
						OldResults->Results.IsValidIndex(OldResults->ResultsStart + LastOcclusionNode - FirstOcclusionNode)
						)
					{
						InstanceParams.FirstOcclusionNode = FirstOcclusionNode;
						InstanceParams.LastOcclusionNode = LastOcclusionNode;
						InstanceParams.OcclusionResults = &OldResults->Results;
						InstanceParams.OcclusionResultsStart = OldResults->ResultsStart;
					}
				}

				INC_DWORD_STAT(STAT_FoliageTraversals);

				{
					SCOPE_CYCLE_COUNTER(STAT_FoliageTraversalTime);

					// validate that the bounding box is layed out correctly in memory
					check((const FVector4f*)&ClusterTree[0].BoundMin + 1 == (const FVector4f*)&ClusterTree[0].BoundMax); //-V594
					//check(UPTRINT(&ClusterTree[0].BoundMin) % 16 == 0);
					//check(UPTRINT(&ClusterTree[0].BoundMax) % 16 == 0);

					int32 UseMinLOD = ClampedMinLOD;

					int32 DebugMin = FMath::Min(CVarSWMinLOD.GetValueOnRenderThread(), InstanceParams.LODs - 1);
					if (DebugMin >= 0)
					{
						UseMinLOD = FMath::Max(UseMinLOD, DebugMin);
					}
					int32 UseMaxLOD = InstanceParams.LODs;

					int32 Force = CVarSWForceLOD.GetValueOnRenderThread();
					if (Force >= 0)
					{
						UseMinLOD = FMath::Clamp(Force, 0, InstanceParams.LODs - 1);
						UseMaxLOD = FMath::Clamp(Force, 0, InstanceParams.LODs - 1);
					}

					if (CVarSWCullAll.GetValueOnRenderThread() < 1)
					{
						if (bUseVectorCull)
						{
							Traverse<true>(InstanceParams, 0, UseMinLOD, UseMaxLOD, bDisableCull);
						}
						else
						{
							Traverse<false>(InstanceParams, 0, UseMinLOD, UseMaxLOD, bDisableCull);
						}
					}

				}

				FillDynamicMeshElements(Collector, ElementParams, InstanceParams);
			}

			int32 UnbuiltInstanceCount = InstanceCountToRender - FirstUnbuiltIndex;

			// Render unbuilt instances
			if (UnbuiltInstanceCount > 0)
			{
				FFoliageRenderInstanceParams& InstanceParams = Collector.AllocateOneFrameResource<FFoliageRenderInstanceParams>(true, false, false);

				// disable LOD blending for unbuilt instances as we haven't calculated the correct LOD.
				ElementParams.bBlendLODs = false;

				if (UnbuiltInstanceCount < 1000 && UnbuiltBounds.Num() >= UnbuiltInstanceCount)
				{
					const int32 NumLODs = RenderData->LODResources.Num();

					int32 Force = CVarSWForceLOD.GetValueOnRenderThread();
					if (Force >= 0)
					{
						Force = FMath::Clamp(Force, 0, NumLODs - 1);
						int32 LastInstanceIndex = FirstUnbuiltIndex + UnbuiltInstanceCount - 1;
						InstanceParams.AddRun(Force, Force, FirstUnbuiltIndex, LastInstanceIndex);
					}
					else
					{
						FMatrix WorldToLocal = GetLocalToWorld().Inverse();
						FVector ViewOriginInLocalZero = WorldToLocal.TransformPosition(View->GetTemporalLODOrigin(0, bMultipleSections));
						FVector ViewOriginInLocalOne = WorldToLocal.TransformPosition(View->GetTemporalLODOrigin(1, bMultipleSections));
						float LODPlanesMax[MAX_STATIC_MESH_LODS];
						float LODPlanesMin[MAX_STATIC_MESH_LODS];

						const bool bIsOrtho = !View->ViewMatrices.IsPerspectiveProjection();
						const float MinSize = bIsOrtho ? 0.0f : CVarSWFoliageMinimumScreenSize.GetValueOnRenderThread();
						const float LODScale = CVarSWFoliageLODDistanceScale.GetValueOnRenderThread();
						const float LODRandom = CVarSWRandomLODRange.GetValueOnRenderThread();
						const float MaxDrawDistanceScale = GetCachedScalabilityCVars().ViewDistanceScale;
						const float SphereRadius = RenderData->Bounds.SphereRadius;

						checkSlow(NumLODs > 0);

						float FinalCull = MAX_flt;
						if (MinSize > 0.0)
						{
							FinalCull = ComputeBoundsDrawDistance(MinSize, SphereRadius, View->ViewMatrices.GetProjectionMatrix()) * LODScale;
						}
						if (View->SceneViewInitOptions.OverrideFarClippingPlaneDistance > 0.0f)
						{
							FinalCull = FMath::Min(FinalCull, View->SceneViewInitOptions.OverrideFarClippingPlaneDistance * MaxDrawDistanceScale);
						}
						if (UserData_AllInstances.EndCullDistance > 0.0f)
						{
							FinalCull = FMath::Min(FinalCull, UserData_AllInstances.EndCullDistance * MaxDrawDistanceScale);
						}
						ElementParams.FinalCullDistance = FinalCull;

						for (int32 LODIndex = 1; LODIndex < NumLODs; LODIndex++)
						{
							float Distance = ComputeBoundsDrawDistance(RenderData->ScreenSize[LODIndex].GetValue(), SphereRadius, View->ViewMatrices.GetProjectionMatrix()) * LODScale;
							LODPlanesMin[LODIndex - 1] = FMath::Min(FinalCull - LODRandom, Distance - LODRandom);
							LODPlanesMax[LODIndex - 1] = FMath::Min(FinalCull, Distance);
						}
						LODPlanesMin[NumLODs - 1] = FinalCull - LODRandom;
						LODPlanesMax[NumLODs - 1] = FinalCull;

						// NOTE: in case of unbuilt we can't really apply the instance scales so the LOD won't be optimal until the build is completed

						// calculate runs
						int32 MinLOD = ClampedMinLOD;
						int32 MaxLOD = NumLODs;
						CalcLOD(MinLOD, MaxLOD, UnbuiltBounds[0].Min, UnbuiltBounds[0].Max, ViewOriginInLocalZero, ViewOriginInLocalOne, LODPlanesMin, LODPlanesMax);
						int32 FirstIndexInRun = 0;
						for (int32 Index = 1; Index < UnbuiltInstanceCount; ++Index)
						{
							int32 TempMinLOD = ClampedMinLOD;
							int32 TempMaxLOD = NumLODs;
							CalcLOD(TempMinLOD, TempMaxLOD, UnbuiltBounds[Index].Min, UnbuiltBounds[Index].Max, ViewOriginInLocalZero, ViewOriginInLocalOne, LODPlanesMin, LODPlanesMax);
							if (TempMinLOD != MinLOD)
							{
								if (MinLOD < NumLODs)
								{
									InstanceParams.AddRun(MinLOD, MinLOD, FirstIndexInRun + FirstUnbuiltIndex, (Index - 1) + FirstUnbuiltIndex - 1);
								}
								MinLOD = TempMinLOD;
								FirstIndexInRun = Index;
							}
						}
						InstanceParams.AddRun(MinLOD, MinLOD, FirstIndexInRun + FirstUnbuiltIndex, FirstUnbuiltIndex + UnbuiltInstanceCount - 1);
					}
				}
				else
				{
					// more than 1000, render them all at lowest LOD (until we have an updated tree)
					const int8 LowestLOD = (RenderData->LODResources.Num() - 1);
					InstanceParams.AddRun(LowestLOD, LowestLOD, FirstUnbuiltIndex, FirstUnbuiltIndex + UnbuiltInstanceCount - 1);
				}
				FillDynamicMeshElements(Collector, ElementParams, InstanceParams);
			}

			if (View->Family->EngineShowFlags.HISMCOcclusionBounds)
			{
				for (auto& OcclusionBound : OcclusionBounds)
				{
					DrawWireBox(Collector.GetPDI(ViewIndex), OcclusionBound.GetBox(), FColor(255, 0, 0), View->Family->EngineShowFlags.Game ? SDPG_World : SDPG_Foreground);
				}
			}

			if (View->Family->EngineShowFlags.HISMCClusterTree)
			{
				FColor StartingColor(100, 0, 0);

				for (const FClusterNode& CulsterNode : ClusterTree)
				{
					DrawWireBox(Collector.GetPDI(ViewIndex), GetLocalToWorld(), FBox(CulsterNode.BoundMin, CulsterNode.BoundMax), StartingColor, View->Family->EngineShowFlags.Game ? SDPG_World : SDPG_Foreground);
					StartingColor.R += 5;
					StartingColor.G += 5;
					StartingColor.B += 5;
				}
			}
		}
	}

}

void SWHISMComponentSceneProxy::AcceptOcclusionResults(const FSceneView* View, TArray<bool>* Results, int32 ResultsStart, int32 NumResults)
{
	// Don't accept subprimitive occlusion results from a previously-created sceneproxy - the tree may have been different
	if (OcclusionBounds.Num() == NumResults && SceneProxyCreatedFrameNumberRenderThread < GFrameNumberRenderThread)
	{
		uint32 ViewId = View->GetViewKey();
		FFoliageOcclusionResults* OldResults = OcclusionResults.Find(ViewId);
		if (OldResults)
		{
			OldResults->FrameNumberRenderThread = GFrameNumberRenderThread;
			OldResults->Results = *Results;
			OldResults->ResultsStart = ResultsStart;
			OldResults->NumResults = NumResults;
		}
		else
		{
			// now is a good time to clean up any stale entries
			for (auto Iter = OcclusionResults.CreateIterator(); Iter; ++Iter)
			{
				if (Iter.Value().FrameNumberRenderThread != GFrameNumberRenderThread)
				{
					Iter.RemoveCurrent();
				}
			}
			OcclusionResults.Add(ViewId, FFoliageOcclusionResults(Results, ResultsStart, NumResults));
		}
	}

}

const TArray<FBoxSphereBounds>* SWHISMComponentSceneProxy::GetOcclusionQueries(const FSceneView* View) const
{
	return &OcclusionBounds;
}

static FBox GetClusterTreeBounds(TArray<FClusterNode> const& InClusterTree, FVector InOffset)
{
	// Return top node of cluster tree. Apply offset on node bounds.
	return (InClusterTree.Num() > 0 ? FBox(InOffset + FVector(InClusterTree[0].BoundMin), InOffset + FVector(InClusterTree[0].BoundMax)) : FBox(ForceInit));
}

FPrimitiveSceneProxy* USWHISMComponent::CreateSceneProxy()
{
	FPrimitiveSceneProxy* OutProxy = nullptr;

	if(bPhysicsStateCreated)
		return nullptr;

	static const auto NaniteProxyRenderModeVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Nanite.ProxyRenderMode"));
	const int32 NaniteProxyRenderMode = (NaniteProxyRenderModeVar != nullptr) ? (NaniteProxyRenderModeVar->GetInt() != 0) : 0;

	if (ProxySize)
	{
		DEC_DWORD_STAT_BY(STAT_FoliageInstanceBuffers, ProxySize);
	}
	ProxySize = 0;

	// Verify that the mesh is valid before using it.
	const bool bMeshIsValid =
		// Make sure we have instances.
		(PerInstanceRenderData.IsValid()) &&
		// Make sure we have an actual static mesh.
		GetStaticMesh() &&
		!GetStaticMesh()->IsCompiling() &&
		GetStaticMesh()->HasValidRenderData(false);

	if (bMeshIsValid)
	{
		check(InstancingRandomSeed != 0);

		// If instance data was modified, update GPU copy.
		// If InstanceBuffer was initialized with RequireCPUAccess (always true in editor).
		
		if (InstanceUpdateCmdBuffer.NumInlineCommands() > 0 && PerInstanceRenderData->InstanceBuffer.RequireCPUAccess)
		{
			PerInstanceRenderData->UpdateFromCommandBuffer(InstanceUpdateCmdBuffer);
		}

		ProxySize = PerInstanceRenderData->ResourceSize;
		INC_DWORD_STAT_BY(STAT_FoliageInstanceBuffers, ProxySize);

		if (ShouldCreateNaniteProxy())
		{
			return ::new Nanite::FSceneProxy(this);
		}
		// If we didn't get a proxy, but Nanite was enabled on the asset when it was built, evaluate proxy creation
		else if (GetStaticMesh()->HasValidNaniteData() && NaniteProxyRenderMode != 0)
		{
			// Do not render Nanite proxy
			return nullptr;
		}
		else
		{
			if(!SWInstanceSceneData.IsValid())
				return nullptr;

			return ::new SWHISMComponentSceneProxy(this, GetWorld()->FeatureLevel);
		}
	}

	return nullptr;
}

void USWHISMComponent::ClearInstances()
{
	/*
	 * We're not removing instances, we're not adding instances during AsyncBuilding,
	 * We need to prevent ClearInstances() from happening during AsyncBuilding as it could cause a race condition on PerInstanceSMData access
	 */
	if(!IsAsyncBuilding())
	{
		Super::ClearInstances();
	}
	else
	{
		PendingClearInstances = true;
	}	
}

bool USWHISMComponent::UpdateInstanceTransform(int32 InstanceIndex, const FTransform& NewInstanceTransform,
                                               bool bWorldSpace, bool bMarkRenderStateDirty, bool bTeleport)
{
	/*
	 *  The only reason we're using updateinstancetransform in shaderworld is for resource harvesting and hidding meshes
	 *	Large instance count position updates are happening by batches and only when !AsyncBuilding()
	 */

	SW_FCT_CYCLE("UpdateInstanceTransform")

	if (!SWPerInstanceSMData.IsValid())
	{
		FScopeLock lock(&PerInstanceDataMutex);
		if (!PerInstanceSMData.IsValidIndex(InstanceIndex))
		{
			return false;
		}
	}
	else
	{
		FScopeLock lock(&SWPerInstanceSMData->DataLock);
		if (!SWPerInstanceSMData->PerInstanceSMData.IsValidIndex(InstanceIndex))
		{
			return false;
		}
	}

	bIsOutOfDate = true;
	// invalidate the results of the current async build we need to modify the tree
	bConcurrentChanges |= IsAsyncBuilding();

	const int32 RenderIndex = GetRenderIndex(InstanceIndex);
	const FTransform NewLocalTransform = bWorldSpace ? NewInstanceTransform.GetRelativeTransform(GetComponentTransform()) : NewInstanceTransform;

	bool Result = false;
	{
		Modify();

		// Render data uses local transform of the instance
		FTransform LocalTransform = bWorldSpace ? NewInstanceTransform.GetRelativeTransform(GetComponentTransform()) : NewInstanceTransform;

		if(!SWPerInstanceSMData.IsValid())
		{
			FScopeLock lock(&PerInstanceDataMutex);

			FInstancedStaticMeshInstanceData& InstanceData = PerInstanceSMData[InstanceIndex];
			InstanceData.Transform = LocalTransform.ToMatrixWithScale();
		}
		else
		{
			FScopeLock lock(&SWPerInstanceSMData->DataLock);

			FInstancedStaticMeshInstanceData& InstanceData = SWPerInstanceSMData->PerInstanceSMData[InstanceIndex];	
			InstanceData.Transform = LocalTransform.ToMatrixWithScale();
		}

		if (bPhysicsStateCreated)
		{
			// Physics uses world transform of the instance
			FTransform WorldTransform = bWorldSpace ? NewInstanceTransform : (LocalTransform * GetComponentTransform());
			//UpdateInstanceBodyTransform(InstanceIndex, WorldTransform, bTeleport);
			{
				FBodyInstance*& InstanceBodyInstance = InstanceBodies[InstanceIndex];

				if (WorldTransform.GetScale3D().IsNearlyZero())
				{
					if (InstanceBodyInstance)
					{
						// delete BodyInstance
						InstanceBodyInstance->TermBody();
						delete InstanceBodyInstance;
						InstanceBodyInstance = nullptr;
					}
				}
				else
				{
					if (InstanceBodyInstance)
					{
						// Update existing BodyInstance
						InstanceBodyInstance->SetBodyTransform(WorldTransform, TeleportFlagToEnum(bTeleport));
						InstanceBodyInstance->UpdateBodyScale(WorldTransform.GetScale3D());
					}
					else
					{
						// create new BodyInstance
						InstanceBodyInstance = new FBodyInstance();
						InitInstanceBody(InstanceIndex, InstanceBodyInstance);
					}
				}
			}
		}

		// Request navigation update
		PartialNavigationUpdate(InstanceIndex);

		// Force recreation of the render data when proxy is created
		//InstanceUpdateCmdBuffer.Edit();
		InstanceUpdateCmdBuffer.NumEdits++;

		if (bMarkRenderStateDirty)
		{
			MarkRenderStateDirty();
		}

		Result = true;
	}

	// The tree will be fully rebuilt once the static mesh compilation is finished, no need for incremental update in that case.
	if (Result && GetStaticMesh() && !GetStaticMesh()->IsCompiling() && GetStaticMesh()->HasValidRenderData())
	{
		const FBox NewInstanceBounds = GetStaticMesh()->GetBounds().GetBox().TransformBy(NewLocalTransform);

		//InstanceUpdateCmdBuffer.UpdateInstance(RenderIndex, NewLocalTransform.ToMatrixWithScale().ConcatTranslation(-TranslatedInstanceSpaceOrigin));

		FInstanceUpdateCmdBuffer::FInstanceUpdateCommand& Cmd = InstanceUpdateCmdBuffer.Cmds.AddDefaulted_GetRef();
		Cmd.InstanceIndex = RenderIndex;
		Cmd.Type = FInstanceUpdateCmdBuffer::Update;
		Cmd.XForm = NewLocalTransform.ToMatrixWithScale().ConcatTranslation(-TranslatedInstanceSpaceOrigin);

		InstanceUpdateCmdBuffer.NumUpdates++;
		//InstanceUpdateCmdBuffer.Edit();
		InstanceUpdateCmdBuffer.NumEdits++;

		UnbuiltInstanceBounds += NewInstanceBounds;
		UnbuiltInstanceBoundsList.Add(NewInstanceBounds);		
	}

	return Result;
}

bool USWHISMComponent::SWBatchUpdateInstancesTransforms(int32 StartInstanceIndex,
                                                        const TArray<FTransform>& NewInstancesTransforms, bool bWorldSpace, bool bMarkRenderStateDirty, bool bTeleport)
{
	bool BatchResult = true;
	int32 InstanceIndex = StartInstanceIndex;
	for (const FTransform& NewInstanceTransform : NewInstancesTransforms)
	{
		bool Result = UpdateInstanceTransform(InstanceIndex, NewInstanceTransform, bWorldSpace, bMarkRenderStateDirty, bTeleport);
		BatchResult = BatchResult && Result;

		InstanceIndex++;
	}
	return BatchResult;
}

bool USWHISMComponent::SWBatchUpdateInstanceData(int32 StartInstanceIndex,	TArray<FInstancedStaticMeshInstanceData>& NewInstancesTransforms, bool bWorldSpace,	bool bMarkRenderStateDirty, bool bTeleport)
{
	bool BatchResult = true;

	SWBatchUpdateInstancesDataInternal(StartInstanceIndex, NewInstancesTransforms.Num(), NewInstancesTransforms.GetData(), bMarkRenderStateDirty, bTeleport);

	return BatchResult;
}

bool USWHISMComponent::SWBatchUpdateCountInstanceData(int32 StartInstanceIndex, int32 NumInstances,	TArray<FInstancedStaticMeshInstanceData>& NewInstancesTransforms, bool bWorldSpace, bool bMarkRenderStateDirty,	bool bTeleport, int32 StartNewInstanceIndex)
{

	if(StartNewInstanceIndex + NumInstances > NewInstancesTransforms.Num())
	{
#if SWDEBUG
		SW_LOG("Out of scope HISM BatchUpdate : StartNewInstanceIndex %d NumInstances %d NewInstancesTransforms.Num() %d", StartNewInstanceIndex, NumInstances, NewInstancesTransforms.Num())
#endif

		return false;
	}

	bool BatchResult = true;

	SWBatchUpdateInstancesDataInternal(StartInstanceIndex, NumInstances, NewInstancesTransforms.GetData(), bMarkRenderStateDirty, bTeleport, StartNewInstanceIndex);

	return BatchResult;
}

void USWHISMComponent::AddInstancesInternal(TArray<int32>& InstanceIndices, const TArray<FInstancedStaticMeshInstanceData>& InstanceTransforms, bool bShouldReturnIndices, bool bWorldSpace)
{
	const int32 Count = InstanceTransforms.Num();

	TArray<int32>& NewInstanceIndices = InstanceIndices;

	if (bShouldReturnIndices)
	{
		NewInstanceIndices.Reserve(Count);
	}

	int32 InstanceIndex = PerInstanceSMData.Num();
	int32 StartingIndex = InstanceIndex;

	PerInstanceSMData.Reserve(PerInstanceSMData.Num() + Count);
	if (bPhysicsStateCreated)
		InstanceBodies.Reserve(InstanceBodies.Num() + Count);

	PerInstanceSMCustomData.AddZeroed(NumCustomDataFloats * Count);

#if WITH_EDITOR
	SelectedInstances.Add(false, Count);
#endif

	TArray<FTransform> WorldTransforms;
	WorldTransforms.AddUninitialized(Count);

	if(Count>0)
	{
		FScopeLock lock(&PerInstanceDataMutex);

		PerInstanceSMData.AddUninitialized(Count);

		if (bPhysicsStateCreated)
			InstanceBodies.AddUninitialized(Count);

		if (bShouldReturnIndices)
		{
			NewInstanceIndices.AddUninitialized(Count);
		}

		const FTransform ComponentT = GetComponentTransform();

		if((Count<500) && !bPhysicsStateCreated)
		{
			for (int32 k = 0; k < Count; k++)
			{
				(&PerInstanceSMData.GetData()[InstanceIndex])[k] = InstanceTransforms.GetData()[k];

				WorldTransforms.GetData()[k] = FTransform(InstanceTransforms.GetData()[k].Transform) * ComponentT;

				if (bShouldReturnIndices)
					NewInstanceIndices.GetData()[k] = k + InstanceIndex;
					
				if (bPhysicsStateCreated)
				{
					if (InstanceTransforms.GetData()[k].Transform.GetScaleVector().IsNearlyZero())
					{
						(&InstanceBodies.GetData()[InstanceIndex])[k] = nullptr;
					}
					else
					{
						FBodyInstance* NewBodyInstance = new FBodyInstance();
						int32 BodyIndex = InstanceIndex + k;

						(&InstanceBodies.GetData()[InstanceIndex])[k] = NewBodyInstance;

						NewBodyInstance->CopyBodyInstancePropertiesFrom(&BodyInstance);
						NewBodyInstance->InstanceBodyIndex = BodyIndex; // Set body index 

						// make sure we never enable bSimulatePhysics for ISMComps
						NewBodyInstance->bSimulatePhysics = false;

						// Create physics body instance.
						NewBodyInstance->bAutoWeld = false;	//We don't support this for instanced meshes.
					}
				}

			}
		}
		else
		{
		
			ParallelFor(Count, [Count, bPhysic = bPhysicsStateCreated, BodyInst = BodyInstance, bShouldReturnIndices, IndicesData = NewInstanceIndices.GetData(), WorldT = WorldTransforms.GetData(), InstanceIndex, T = &PerInstanceSMData.GetData()[InstanceIndex], IBody = &InstanceBodies.GetData()[InstanceIndex], PSM = InstanceTransforms.GetData(), CompT = GetComponentTransform()](int32 k)
				{
					if (k < Count)
					{
						T[k] = PSM[k];

						WorldT[k] = FTransform(PSM[k].Transform) * CompT;

						if(bShouldReturnIndices)
							IndicesData[k] = k + InstanceIndex;
							
						if (bPhysic)
						{
							if (PSM[k].Transform.GetScaleVector().IsNearlyZero())
							{
								IBody[k] = nullptr;
							}
							else
							{
								FBodyInstance* NewBodyInstance = new FBodyInstance();
								const int32 BodyIndex = InstanceIndex + k;

								IBody[k] = NewBodyInstance;

								NewBodyInstance->CopyBodyInstancePropertiesFrom(&BodyInst);
								NewBodyInstance->InstanceBodyIndex = BodyIndex; // Set body index 

								// make sure we never enable bSimulatePhysics for ISMComps
								NewBodyInstance->bSimulatePhysics = false;

								// Create physics body instance.
								NewBodyInstance->bAutoWeld = false;	//We don't support this for instanced meshes.
							}
						}
					}
				});
		}	
		
	}

	if(bPhysicsStateCreated || SupportsPartialNavigationUpdate() || FInstancedStaticMeshDelegates::OnInstanceIndexUpdated.IsBound())
	{
		UBodySetup* BodySetup = GetBodySetup();

		for (const FInstancedStaticMeshInstanceData& InstanceTransform : InstanceTransforms)
		{
			
			if (bPhysicsStateCreated)
			{
				if(FBodyInstance* NewBodyInstance = InstanceBodies[InstanceIndex])
				{
					NewBodyInstance->InitBody(BodySetup, WorldTransforms[InstanceIndex - StartingIndex], this, GetWorld()->GetPhysicsScene(), nullptr);
				}
			}

			if (SupportsPartialNavigationUpdate())
			{
				PartialNavigationUpdate(InstanceIndex);
			}

			if (FInstancedStaticMeshDelegates::OnInstanceIndexUpdated.IsBound())
			{
				FInstancedStaticMeshDelegates::FInstanceIndexUpdateData IndexUpdate{ FInstancedStaticMeshDelegates::EInstanceIndexUpdateType::Added, InstanceIndex };
				FInstancedStaticMeshDelegates::OnInstanceIndexUpdated.Broadcast(this, MakeArrayView(&IndexUpdate, 1));
			}

			++InstanceIndex;
		}
	}

	

	if (!SupportsPartialNavigationUpdate())
	{
		// Index parameter is ignored if partial navigation updates are not supported
		PartialNavigationUpdate(0);
	}

	// Batch update the render state after all instances are finished building
	//InstanceUpdateCmdBuffer.Edit();
	InstanceUpdateCmdBuffer.NumEdits++;
	//MarkRenderStateDirty();

}

bool USWHISMComponent::SWAddInstances(TArray<int32>& OutIndices, const TArray<FInstancedStaticMeshInstanceData>& InstanceTransforms, bool bShouldReturnIndices,
	bool bWorldSpace)
{
	if(IsAsyncBuilding())
	{
#if SWDEBUG
		SW_LOG("Trying to add instances while Tree is ebing built is prevented | %s %d instances",*this->GetFName().ToString(), InstanceTransforms.Num())
#endif
		
	}

	ensure(!IsAsyncBuilding());

	TArray<int32>& InstanceIndices = OutIndices;

	AddInstancesInternal(InstanceIndices,InstanceTransforms, true, bWorldSpace);

	// The tree will be fully rebuilt once the static mesh compilation is finished, no need for incremental update in that case.
	if ( (InstanceIndices.Num() > 0) && GetStaticMesh() && !GetStaticMesh()->IsCompiling() && GetStaticMesh()->HasValidRenderData())
	{
		bIsOutOfDate = true;
		bConcurrentChanges |= IsAsyncBuilding();

		const int32 Count = InstanceIndices.Num();

		InstanceReorderTable.Reserve(InstanceReorderTable.Num() + Count);
		UnbuiltInstanceBoundsList.Reserve(UnbuiltInstanceBoundsList.Num() + Count);

		const int32 InitialBufferOffset = InstanceCountToRender - InstanceReorderTable.Num();

		for (const int32 InstanceIndex : InstanceIndices)
		{
			InstanceReorderTable.Add(InitialBufferOffset + InstanceIndex);

			//InstanceUpdateCmdBuffer.FInstanceUpdateCmdBuffer::AddInstance(PerInstanceSMData[InstanceIndex].Transform.ConcatTranslation(-TranslatedInstanceSpaceOrigin));
			
			FInstanceUpdateCmdBuffer::FInstanceUpdateCommand& Cmd = InstanceUpdateCmdBuffer.Cmds.AddDefaulted_GetRef();
			Cmd.InstanceIndex = INDEX_NONE;
			Cmd.Type = FInstanceUpdateCmdBuffer::Add;
			Cmd.XForm = PerInstanceSMData[InstanceIndex].Transform.ConcatTranslation(-TranslatedInstanceSpaceOrigin);

			InstanceUpdateCmdBuffer.NumAdds++;
			//InstanceUpdateCmdBuffer.Edit();
			InstanceUpdateCmdBuffer.NumEdits++;
			
			const FBox NewInstanceBounds = GetStaticMesh()->GetBounds().GetBox().TransformBy(PerInstanceSMData[InstanceIndex].Transform);
			UnbuiltInstanceBounds += NewInstanceBounds;
			UnbuiltInstanceBoundsList.Add(NewInstanceBounds);
		}

	}
	return true;
	//return bShouldReturnIndices ? MoveTemp(InstanceIndices) : TArray<int32>();
}

bool USWHISMComponent::SWNewAddInstances(int32 PoolIndex, TSharedPtr < FSWInstanceIndexesInHISM, ESPMode::ThreadSafe>& InstancesIndexes, TSharedPtr < FSWSpawnableTransforms, ESPMode::ThreadSafe>& InstancesTransforms, bool bWorldSpace)
{
	if (IsAsyncBuilding())
	{
#if SWDEBUG
		SW_LOG("Trying to add instances while Tree is ebing built is prevented | %s %d instances", *this->GetFName().ToString(), InstancesTransforms->Transforms[PoolIndex].Num())
#endif

	}

	ensure(!IsAsyncBuilding());

	if(!SWPerInstanceSMData.IsValid())
		SWPerInstanceSMData = MakeShared<FSWInstancedStaticMeshInstanceDatas, ESPMode::ThreadSafe>();	

	TArray<int32>& InstanceIndices = InstancesIndexes->InstancesIndexes[PoolIndex].InstancesIndexes;
	TArray<FInstancedStaticMeshInstanceData>& InstanceTransforms = InstancesTransforms->Transforms[PoolIndex];
	bool bShouldReturnIndices = true;

	{
		FScopeLock Lock(&SWPerInstanceSMData->DataLock);

		const int32 Count = InstanceTransforms.Num();

		TArray<int32>& NewInstanceIndices = InstanceIndices;

		if (bShouldReturnIndices)
		{
			NewInstanceIndices.Reserve(Count);
		}

		int32 InstanceIndex = SWPerInstanceSMData->PerInstanceSMData.Num();
		int32 StartingIndex = InstanceIndex;

		SWPerInstanceSMData->PerInstanceSMData.Reserve(SWPerInstanceSMData->PerInstanceSMData.Num() + Count);
		if (bPhysicsStateCreated)
			InstanceBodies.Reserve(InstanceBodies.Num() + Count);

		PerInstanceSMCustomData.AddZeroed(NumCustomDataFloats * Count);

#if WITH_EDITOR
		SelectedInstances.Add(false, Count);
#endif

		TArray<FTransform> WorldTransforms;
		WorldTransforms.AddUninitialized(Count);

		if (Count > 0)
		{
			FScopeLock lock(&SWPerInstanceSMData->DataLock);
			SWPerInstanceSMData->PerInstanceSMData.AddUninitialized(Count);

			if (bPhysicsStateCreated)
				InstanceBodies.AddUninitialized(Count);

			if (bShouldReturnIndices)
			{
				NewInstanceIndices.AddUninitialized(Count);
			}

			const FTransform ComponentT = GetComponentTransform();

			if (((Count <= 500) && !bPhysicsStateCreated) || ((Count <= 50) && bPhysicsStateCreated))
			{
				for (int32 k = 0; k < Count; k++)
				{
					(&SWPerInstanceSMData->PerInstanceSMData.GetData()[InstanceIndex])[k] = InstanceTransforms.GetData()[k];

					WorldTransforms.GetData()[k] = FTransform(InstanceTransforms.GetData()[k].Transform) * ComponentT;

					if (bShouldReturnIndices)
						NewInstanceIndices.GetData()[k] = k + InstanceIndex;

					if (bPhysicsStateCreated)
					{
						if (InstanceTransforms.GetData()[k].Transform.GetScaleVector().IsNearlyZero())
						{
							(&InstanceBodies.GetData()[InstanceIndex])[k] = nullptr;
						}
						else
						{
							FBodyInstance* NewBodyInstance = new FBodyInstance();
							int32 BodyIndex = InstanceIndex + k;

							(&InstanceBodies.GetData()[InstanceIndex])[k] = NewBodyInstance;

							NewBodyInstance->CopyBodyInstancePropertiesFrom(&BodyInstance);
							NewBodyInstance->InstanceBodyIndex = BodyIndex; // Set body index 

							// make sure we never enable bSimulatePhysics for ISMComps
							NewBodyInstance->bSimulatePhysics = false;

							// Create physics body instance.
							NewBodyInstance->bAutoWeld = false;	//We don't support this for instanced meshes.
						}
					}

				}
			}
			else
			{

				ParallelFor(Count, [Count, bPhysic = bPhysicsStateCreated, BodyInst = BodyInstance, bShouldReturnIndices, IndicesData = NewInstanceIndices.GetData(), WorldT = WorldTransforms.GetData(), InstanceIndex, T = &SWPerInstanceSMData->PerInstanceSMData.GetData()[InstanceIndex], IBody = &InstanceBodies.GetData()[InstanceIndex], PSM = InstanceTransforms.GetData(), CompT = GetComponentTransform()](int32 k)
					{
						if (k < Count)
						{
							T[k] = PSM[k];

							WorldT[k] = FTransform(PSM[k].Transform) * CompT;

							if (bShouldReturnIndices)
								IndicesData[k] = k + InstanceIndex;

							if (bPhysic)
							{
								if (PSM[k].Transform.GetScaleVector().IsNearlyZero())
								{
									IBody[k] = nullptr;
								}
								else
								{
									FBodyInstance* NewBodyInstance = new FBodyInstance();
									const int32 BodyIndex = InstanceIndex + k;

									IBody[k] = NewBodyInstance;

									NewBodyInstance->CopyBodyInstancePropertiesFrom(&BodyInst);
									NewBodyInstance->InstanceBodyIndex = BodyIndex; // Set body index 

									// make sure we never enable bSimulatePhysics for ISMComps
									NewBodyInstance->bSimulatePhysics = false;

									// Create physics body instance.
									NewBodyInstance->bAutoWeld = false;	//We don't support this for instanced meshes.
								}
							}
						}
					});
			}

		}

		if (bPhysicsStateCreated || SupportsPartialNavigationUpdate() || FInstancedStaticMeshDelegates::OnInstanceIndexUpdated.IsBound())
		{
			UBodySetup* BodySetup = GetBodySetup();

			for (const FInstancedStaticMeshInstanceData& InstanceTransform : InstanceTransforms)
			{
				if (bPhysicsStateCreated)
				{
					if (FBodyInstance* NewBodyInstance = InstanceBodies[InstanceIndex])
					{
						NewBodyInstance->InitBody(BodySetup, WorldTransforms[InstanceIndex - StartingIndex], this, GetWorld()->GetPhysicsScene(), nullptr);
					}

					if (SupportsPartialNavigationUpdate())
					{
						//PartialNavigationUpdate(InstanceIndex);
						{
							if (InstanceIndex == INDEX_NONE)
							{
								AccumulatedNavigationDirtyArea.Init();
								FNavigationSystem::UpdateComponentData(*this);
							}
							else if (GetStaticMesh())
							{
								// Accumulate dirty areas and send them to navigation system once cluster tree is rebuilt
								if (FNavigationSystem::HasComponentData(*this))
								{
									const FTransform NavInstanceTransform(SWPerInstanceSMData->PerInstanceSMData[InstanceIndex].Transform);
									const FBox InstanceBox = GetStaticMesh()->GetBounds().TransformBy(NavInstanceTransform * GetComponentTransform()).GetBox(); // in world space
									AccumulatedNavigationDirtyArea += InstanceBox;
								}
							}
						}
					}
				}

				if (FInstancedStaticMeshDelegates::OnInstanceIndexUpdated.IsBound())
				{
					FInstancedStaticMeshDelegates::FInstanceIndexUpdateData IndexUpdate{ FInstancedStaticMeshDelegates::EInstanceIndexUpdateType::Added, InstanceIndex };
					FInstancedStaticMeshDelegates::OnInstanceIndexUpdated.Broadcast(this, MakeArrayView(&IndexUpdate, 1));
				}

				++InstanceIndex;
			}
		}



		if (!SupportsPartialNavigationUpdate())
		{
			// Index parameter is ignored if partial navigation updates are not supported
			//PartialNavigationUpdate(0);
		}
		//InstanceUpdateCmdBuffer.NumEdits++;
		
	}
	/*
	// The tree will be fully rebuilt once the static mesh compilation is finished, no need for incremental update in that case.
	if (InstanceIndices.Num() > 0 && GetStaticMesh() && !GetStaticMesh()->IsCompiling() && GetStaticMesh()->HasValidRenderData())
	{
		bIsOutOfDate = true;
		bConcurrentChanges |= IsAsyncBuilding();

		const int32 Count = InstanceIndices.Num();

		InstanceReorderTable.Reserve(InstanceReorderTable.Num() + Count);
		UnbuiltInstanceBoundsList.Reserve(UnbuiltInstanceBoundsList.Num() + Count);

		const int32 InitialBufferOffset = InstanceCountToRender - InstanceReorderTable.Num();

		for (const int32 InstanceIndex : InstanceIndices)
		{
			InstanceReorderTable.Add(InitialBufferOffset + InstanceIndex);

			FInstanceUpdateCmdBuffer::FInstanceUpdateCommand& Cmd = InstanceUpdateCmdBuffer.Cmds.AddDefaulted_GetRef();
			Cmd.InstanceIndex = INDEX_NONE;
			Cmd.Type = FInstanceUpdateCmdBuffer::Add;
			Cmd.XForm = SWPerInstanceSMData->PerInstanceSMData[InstanceIndex].Transform.ConcatTranslation(-TranslatedInstanceSpaceOrigin);

			InstanceUpdateCmdBuffer.NumAdds++;
			InstanceUpdateCmdBuffer.NumEdits++;

			const FBox NewInstanceBounds = GetStaticMesh()->GetBounds().GetBox().TransformBy(SWPerInstanceSMData->PerInstanceSMData[InstanceIndex].Transform);
			UnbuiltInstanceBounds += NewInstanceBounds;
			UnbuiltInstanceBoundsList.Add(NewInstanceBounds);
		}

	}*/

	return true;
}

DECLARE_CYCLE_STAT(TEXT("Build Time"), STAT_SWFoliageBuildTime, STATGROUP_Foliage);

#if WITH_EDITOR
static float GDebugSWBuildTreeAsyncDelayInSeconds = 0.f;
static FAutoConsoleVariableRef CVarSWSWDebugBuildTreeAsyncDelayInSeconds(
	TEXT("SWfoliageSW.DebugBuildTreeAsyncDelayInSeconds"),
	GDebugSWBuildTreeAsyncDelayInSeconds,
	TEXT("Adds a delay (in seconds) to BuildTreeAsync tasks for debugging"));
#endif

static TAutoConsoleVariable<int32> CVarSWSWFoliageSplitFactor(
	TEXT("SWfoliageSW.SWSplitFactor"),
	16,
	TEXT("This controls the branching factor of the foliage tree."));


static TAutoConsoleVariable<int32> CVarSWSWMaxOcclusionQueriesPerComponent(
	TEXT("SWfoliageSW.MaxOcclusionQueriesPerComponent"),
	16,
	TEXT("Controls the granularity of occlusion culling. 16-128 is a reasonable range."));

static TAutoConsoleVariable<int32> CVarSWSWMinInstancesPerOcclusionQuery(
	TEXT("SWfoliageSW.MinInstancesPerOcclusionQuery"),
	256,
	TEXT("Controls the granualrity of occlusion culling. 1024 to 65536 is a reasonable range. This is not exact, actual minimum might be off by a factor of two."));

static TAutoConsoleVariable<int32> CVarSWSWMinOcclusionQueriesPerComponent(
	TEXT("SWfoliageSW.MinOcclusionQueriesPerComponent"),
	6,
	TEXT("Controls the granularity of occlusion culling. 2 should be the Min."));

namespace SW_HISM
{

struct FClusterTree
{
	TArray<FInstancedStaticMeshInstanceData> InstanceData;

	TSharedPtr < TArray<FClusterNode>, ESPMode::ThreadSafe> Nodes;
	TArray<int32> SortedInstances;
	TArray<int32> InstanceReorderTable;
	int32 OutOcclusionLayerNum = 0;

	FClusterTree():Nodes(MakeShared<TArray<FClusterNode>, ESPMode::ThreadSafe>()){}
};

class FClusterBuilder
{
protected:
	TWeakObjectPtr<USWHISMComponent> OwningComponent;
	TSharedPtr < FSWInstancedStaticMeshInstanceDatas, ESPMode::ThreadSafe > TransformsInstanceData;
public:
	TSharedPtr < TArray<FPrimitiveInstance, TInlineAllocator<1>>, ESPMode::ThreadSafe > InstanceSceneData;
	TSharedPtr < TArray<float>, ESPMode::ThreadSafe > InstanceRandomID;
protected:
	int32 OriginalNum;
	int32 Num;
	FBox InstBox;
	int32 BranchingFactor;
	int32 InternalNodeBranchingFactor;
	int32 OcclusionLayerTarget;
	int32 MaxInstancesPerLeaf;
	int32 NumRoots;

	int32 InstancingRandomSeed;
	float DensityScaling;
	bool GenerateInstanceScalingRange;
public:
	bool RequireCPUAccess;
protected:

	TArray<int32> SortIndex;
	TArray<FVector> SortPoints;
	TArray<FInstancedStaticMeshInstanceData> Transforms;
	TArray<float> CustomDataFloats;
	int32 NumCustomDataFloats;

	struct FRunPair
	{
		int32 Start;
		int32 Num;

		FRunPair(int32 InStart, int32 InNum)
			: Start(InStart)
			, Num(InNum)
		{
		}

		bool operator< (const FRunPair& Other) const
		{
			return Start < Other.Start;
		}
	};
	TArray<FRunPair> Clusters;

	struct FSortPair
	{
		float d;
		int32 Index;

		bool operator< (const FSortPair& Other) const
		{
			return d < Other.d;
		}
	};
	TArray<FSortPair> SortPairs;

	void Split(int32 InNum)
	{
		checkSlow(InNum);
		Clusters.Reset();
		Split(0, InNum - 1);
		Clusters.Sort();
		checkSlow(Clusters.Num() > 0);
		int32 At = 0;
		for (auto& Cluster : Clusters)
		{
			checkSlow(At == Cluster.Start);
			At += Cluster.Num;
		}
		checkSlow(At == InNum);
	}

	void Split(int32 Start, int32 End)
	{
		int32 NumRange = 1 + End - Start;
		FBox ClusterBounds(ForceInit);
		for (int32 Index = Start; Index <= End; Index++)
		{
			ClusterBounds += SortPoints[SortIndex[Index]];
		}
		if (NumRange <= BranchingFactor)
		{
			Clusters.Add(FRunPair(Start, NumRange));
			return;
		}
		checkSlow(NumRange >= 2);
		SortPairs.Reset();
		int32 BestAxis = -1;
		float BestAxisValue = -1.0f;
		for (int32 Axis = 0; Axis < 3; Axis++)
		{
			float ThisAxisValue = ClusterBounds.Max[Axis] - ClusterBounds.Min[Axis];
			if (!Axis || ThisAxisValue > BestAxisValue)
			{
				BestAxis = Axis;
				BestAxisValue = ThisAxisValue;
			}
		}
		for (int32 Index = Start; Index <= End; Index++)
		{
			FSortPair Pair;

			Pair.Index = SortIndex[Index];
			Pair.d = SortPoints[Pair.Index][BestAxis];
			SortPairs.Add(Pair);
		}
		SortPairs.Sort();
		for (int32 Index = Start; Index <= End; Index++)
		{
			SortIndex[Index] = SortPairs[Index - Start].Index;
		}

		int32 Half = NumRange / 2;

		int32 EndLeft = Start + Half - 1;
		int32 StartRight = 1 + End - Half;

		if (NumRange & 1)
		{
			if (SortPairs[Half].d - SortPairs[Half - 1].d < SortPairs[Half + 1].d - SortPairs[Half].d)
			{
				EndLeft++;
			}
			else
			{
				StartRight--;
			}
		}
		checkSlow(EndLeft + 1 == StartRight);
		checkSlow(EndLeft >= Start);
		checkSlow(End >= StartRight);

		Split(Start, EndLeft);
		Split(StartRight, End);
	}

	void BuildInstanceBuffer()
	{
		// build new instance buffer
		FRandomStream RandomStream = FRandomStream(InstancingRandomSeed);
		bool bHalfFloat = GVertexElementTypeSupport.IsSupported(VET_Half2);
		BuiltInstanceData = MakeUnique<FStaticMeshInstanceData>(bHalfFloat);

		BuiltInstanceData->SetAllowCPUAccess(RequireCPUAccess);
		
		int32 NumInstances = Result->InstanceReorderTable.Num();
		int32 NumRenderInstances = Result->SortedInstances.Num();

		if (NumRenderInstances > 0)
		{
			
			
			BuiltInstanceData->AllocateInstances(NumRenderInstances, NumCustomDataFloats, GIsEditor ? EResizeBufferFlags::AllowSlackOnGrow | EResizeBufferFlags::AllowSlackOnReduce : EResizeBufferFlags::None, false); // In Editor always permit overallocation, to prevent too much realloc

			FVector2D LightmapUVBias = FVector2D(-1.0f, -1.0f);
			FVector2D ShadowmapUVBias = FVector2D(-1.0f, -1.0f);

			// we loop over all instances to ensure that render instances will get same RandomID regardless of density settings
			for (int32 i = 0; i < NumInstances; ++i)
			{
				int32 RenderIndex = Result->InstanceReorderTable[i];
				float RandomID = RandomStream.GetFraction();
				if (RenderIndex >= 0)
				{
					// LWC_TODO: Precision loss here has been compensated for by use of TranslatedInstanceSpaceOrigin.
					BuiltInstanceData->SetInstance(RenderIndex, FMatrix44f(Transforms[i].Transform), RandomID, LightmapUVBias, ShadowmapUVBias);

					for (int32 DataIndex = 0; DataIndex < NumCustomDataFloats; ++DataIndex)
					{
						BuiltInstanceData->SetInstanceCustomData(RenderIndex, DataIndex, CustomDataFloats[NumCustomDataFloats * i + DataIndex]);
					}
				}
				// correct light/shadow map bias will be setup on game thread side if needed
			}
		}

		/*
		 * Visual only instanced static meshes
		 */
		if (TransformsInstanceData.IsValid())
		{
			TArray<int32>& SortedInstances = Result->SortedInstances;
			TArray<int32>& InstanceReorderTable = Result->InstanceReorderTable;
			//TArray<float>& InstanceCustomDataDummy;

			Result->InstanceData = Transforms;/*
			Result->InstanceData.Reset(NumInstances);
			for (const FInstancedStaticMeshInstanceData& Transform : Transforms)
			{
				Result->InstanceData.Emplace(Transform.Transform);
			}*/

			const bool GetRandomID = InstanceRandomID.IsValid();

			InstanceSceneData->SetNum(NumInstances);

			if(GetRandomID)
				InstanceRandomID->SetNum(NumInstances);

			// in-place sort the instances and generate the sorted instance data
			for (int32 FirstUnfixedIndex = 0; FirstUnfixedIndex < NumInstances; FirstUnfixedIndex++)
			{
				int32 LoadFrom = SortedInstances[FirstUnfixedIndex];

				if (LoadFrom != FirstUnfixedIndex)
				{
					check(LoadFrom > FirstUnfixedIndex);

					//BuiltInstanceData->SwapInstance(FirstUnfixedIndex, LoadFrom);
					Result->InstanceData.Swap(FirstUnfixedIndex, LoadFrom);					

					int32 SwapGoesTo = InstanceReorderTable[FirstUnfixedIndex];
					check(SwapGoesTo > FirstUnfixedIndex);
					check(SortedInstances[SwapGoesTo] == FirstUnfixedIndex);
					SortedInstances[SwapGoesTo] = LoadFrom;
					InstanceReorderTable[LoadFrom] = SwapGoesTo;

					InstanceReorderTable[FirstUnfixedIndex] = FirstUnfixedIndex;
					SortedInstances[FirstUnfixedIndex] = FirstUnfixedIndex;
				}

				FPrimitiveInstance& SceneData = InstanceSceneData->GetData()[FirstUnfixedIndex];
				BuiltInstanceData->GetInstanceTransform(FirstUnfixedIndex, SceneData.LocalToPrimitive);

				if (GetRandomID)
					BuiltInstanceData->GetInstanceRandomID(FirstUnfixedIndex, InstanceRandomID->GetData()[FirstUnfixedIndex]);
			}
		}
		else if(InstanceSceneData.IsValid())
		{
			InstanceSceneData->SetNum(NumInstances);
			for (int32 FirstUnfixedIndex = 0; FirstUnfixedIndex < NumInstances; FirstUnfixedIndex++)
			{
				FPrimitiveInstance& SceneData = InstanceSceneData->GetData()[FirstUnfixedIndex];
				BuiltInstanceData->GetInstanceTransform(FirstUnfixedIndex, SceneData.LocalToPrimitive);
			}
		}
		
	}

	void Init()
	{
		SortIndex.Empty();
		SortPoints.SetNumUninitialized(OriginalNum);

		FRandomStream DensityRand = FRandomStream(InstancingRandomSeed);

		SortIndex.Empty(OriginalNum * DensityScaling);

		for (int32 Index = 0; Index < OriginalNum; Index++)
		{
			SortPoints[Index] = Transforms[Index].Transform.GetOrigin();

			if (DensityScaling < 1.0f && DensityRand.GetFraction() > DensityScaling)
			{
				continue;
			}

			SortIndex.Add(Index);
		}

		Num = SortIndex.Num();

		OcclusionLayerTarget = CVarSWSWMaxOcclusionQueriesPerComponent.GetValueOnAnyThread();
		int32 MinInstancesPerOcclusionQuery = CVarSWSWMinInstancesPerOcclusionQuery.GetValueOnAnyThread();

		if (Num / MinInstancesPerOcclusionQuery < OcclusionLayerTarget)
		{
			OcclusionLayerTarget = Num / MinInstancesPerOcclusionQuery;
			if (OcclusionLayerTarget < CVarSWSWMinOcclusionQueriesPerComponent.GetValueOnAnyThread())
			{
				OcclusionLayerTarget = 0;
			}
		}
		InternalNodeBranchingFactor = CVarSWSWFoliageSplitFactor.GetValueOnAnyThread();

		if (Num / MaxInstancesPerLeaf < InternalNodeBranchingFactor) // if there are less than InternalNodeBranchingFactor leaf nodes
		{
			MaxInstancesPerLeaf = FMath::Clamp<int32>(Num / InternalNodeBranchingFactor, 1, 1024); // then make sure we have at least InternalNodeBranchingFactor leaves
		}
	}

public:
	TUniquePtr<FClusterTree> Result;
	TUniquePtr<FStaticMeshInstanceData> BuiltInstanceData;

	FClusterBuilder(TWeakObjectPtr<USWHISMComponent> OwningComp, TSharedPtr < FSWInstancedStaticMeshInstanceDatas, ESPMode::ThreadSafe > InInstanceData, TSharedPtr < TArray<FPrimitiveInstance, TInlineAllocator<1>>, ESPMode::ThreadSafe > InInstanceSceneData, TSharedPtr < TArray<float>, ESPMode::ThreadSafe > InInstanceRandomID,TArray<FInstancedStaticMeshInstanceData>& InTransforms, TArray<float> InCustomDataFloats, int32 InNumCustomDataFloats, const FBox& InInstBox, int32 InMaxInstancesPerLeaf, float InDensityScaling, int32 InInstancingRandomSeed, bool InGenerateInstanceScalingRange, bool InRequireCPU)
		: OwningComponent(MoveTemp(OwningComp))
		, TransformsInstanceData(InInstanceData)
		, InstanceSceneData(InInstanceSceneData)
		, InstanceRandomID(InInstanceRandomID)
		//, OriginalNum(InTransforms.Num())
		, InstBox(InInstBox)
		, MaxInstancesPerLeaf(InMaxInstancesPerLeaf)
		, InstancingRandomSeed(InInstancingRandomSeed)
		, DensityScaling(InDensityScaling)
		, GenerateInstanceScalingRange(InGenerateInstanceScalingRange)
		, RequireCPUAccess(InRequireCPU)
		//, Transforms(MoveTemp(InTransforms))
		, CustomDataFloats(MoveTemp(InCustomDataFloats))
		, NumCustomDataFloats(InNumCustomDataFloats)
		, Result(nullptr)
	{
	}

	void BuildTreeAndBufferAsync(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
	{
		if(TransformsInstanceData.IsValid())
		{
			FScopeLock lock(&TransformsInstanceData->DataLock);
			Transforms = TransformsInstanceData->PerInstanceSMData;
			OriginalNum = Transforms.Num();
			GenerateInstanceScalingRange = OriginalNum > 0;
		}
		else
		{
			/*
			 *	The component was valid to get a pointer to, but was it actually pending destruction?
			 *	ThreadSafeTest = true description mentions no flags are being checked
			 *	Try to Get() the component again, if it's not available anymore, clear the data we
			 */
			 
			if(USWHISMComponent* Comp = (OwningComponent.IsValid(false, true) ? OwningComponent.Get() : nullptr))
			{
				FScopeLock lock(&Comp->PerInstanceDataMutex);
				Transforms = Comp->PerInstanceSMData;
			}				

			if (USWHISMComponent* Comp = (OwningComponent.IsValid(false, true) ? OwningComponent.Get() : nullptr))
			{
				OriginalNum = Transforms.Num();
				GenerateInstanceScalingRange = OriginalNum > 0;
			}
			else
			{
				Transforms.Empty();
				OriginalNum = Transforms.Num();
				GenerateInstanceScalingRange = OriginalNum > 0;
			}
		}		

#if WITH_EDITOR
		if (!FMath::IsNearlyZero(GDebugSWBuildTreeAsyncDelayInSeconds))
		{
			UE_LOG(LogStaticMesh, Warning, TEXT("BuildTree Debug Delay %5.1f (CVarSW foliageSW.DebugBuildTreeAsyncDelayInSeconds)"), GDebugSWBuildTreeAsyncDelayInSeconds);
			FPlatformProcess::Sleep(GDebugSWBuildTreeAsyncDelayInSeconds);
		}
#endif
		BuildTreeAndBuffer();
	}

	void BuildTreeAndBuffer()
	{
		BuildTree();
		BuildInstanceBuffer();
	}

	void BuildTree()
	{
		Init();

		Result = MakeUnique<FClusterTree>();

		if (Num == 0)
		{
			// Can happen if all instances are excluded due to scalability
			// It doesn't only happen with a scalability factor of 0 - 
			// even with a scalability factor of 0.99, if there's only one instance of this type you can end up with Num == 0 if you're unlucky
			Result->InstanceReorderTable.Init(INDEX_NONE, OriginalNum);
			return;
		}

		bool bIsOcclusionLayer = false;
		BranchingFactor = MaxInstancesPerLeaf;
		if (BranchingFactor > 2 && OcclusionLayerTarget && Num / BranchingFactor <= OcclusionLayerTarget)
		{
			BranchingFactor = FMath::Max<int32>(2, (Num + OcclusionLayerTarget - 1) / OcclusionLayerTarget);
			OcclusionLayerTarget = 0;
			bIsOcclusionLayer = true;
		}
		Split(Num);
		if (bIsOcclusionLayer)
		{
			Result->OutOcclusionLayerNum = Clusters.Num();
			bIsOcclusionLayer = false;
		}

		TArray<int32>& SortedInstances = Result->SortedInstances;
		SortedInstances.Append(SortIndex);

		NumRoots = Clusters.Num();
		Result->Nodes.Get()->Init(FClusterNode(), Clusters.Num());

		for (int32 Index = 0; Index < NumRoots; Index++)
		{
			FClusterNode& Node = (*Result->Nodes.Get())[Index];
			Node.FirstInstance = Clusters[Index].Start;
			Node.LastInstance = Clusters[Index].Start + Clusters[Index].Num - 1;
			FBox NodeBox(ForceInit);
			for (int32 InstanceIndex = Node.FirstInstance; InstanceIndex <= Node.LastInstance; InstanceIndex++)
			{
				const FMatrix& ThisInstTrans = Transforms[SortedInstances[InstanceIndex]].Transform;
				FBox ThisInstBox = InstBox.TransformBy(ThisInstTrans);
				NodeBox += ThisInstBox;

				if (GenerateInstanceScalingRange)
				{
					FVector3f CurrentScale(ThisInstTrans.GetScaleVector());

					Node.MinInstanceScale = Node.MinInstanceScale.ComponentMin(CurrentScale);
					Node.MaxInstanceScale = Node.MaxInstanceScale.ComponentMax(CurrentScale);
				}
			}
			Node.BoundMin = (FVector3f)NodeBox.Min;
			Node.BoundMax = (FVector3f)NodeBox.Max;
		}
		TArray<int32> NodesPerLevel;
		NodesPerLevel.Add(NumRoots);
		int32 LOD = 0;

		TArray<int32> InverseSortIndex;
		TArray<int32> RemapSortIndex;
		TArray<int32> InverseInstanceIndex;
		TArray<int32> OldInstanceIndex;
		TArray<int32> LevelStarts;
		TArray<int32> InverseChildIndex;
		TArray<FClusterNode> OldNodes;

		while (NumRoots > 1)
		{
			SortIndex.Reset();
			SortPoints.Reset();
			SortIndex.AddUninitialized(NumRoots);
			SortPoints.AddUninitialized(NumRoots);
			for (int32 Index = 0; Index < NumRoots; Index++)
			{
				SortIndex[Index] = Index;
				FClusterNode& Node = (*Result->Nodes.Get())[Index];
				SortPoints[Index] = (FVector)(Node.BoundMin + Node.BoundMax) * 0.5f;
			}
			BranchingFactor = InternalNodeBranchingFactor;
			if (BranchingFactor > 2 && OcclusionLayerTarget && NumRoots / BranchingFactor <= OcclusionLayerTarget)
			{
				BranchingFactor = FMath::Max<int32>(2, (NumRoots + OcclusionLayerTarget - 1) / OcclusionLayerTarget);
				OcclusionLayerTarget = 0;
				bIsOcclusionLayer = true;
			}
			Split(NumRoots);
			if (bIsOcclusionLayer)
			{
				Result->OutOcclusionLayerNum = Clusters.Num();
				bIsOcclusionLayer = false;
			}

			InverseSortIndex.Reset();
			InverseSortIndex.AddUninitialized(NumRoots);
			for (int32 Index = 0; Index < NumRoots; Index++)
			{
				InverseSortIndex[SortIndex[Index]] = Index;
			}

			{
				// rearrange the instances to match the new order of the old roots
				RemapSortIndex.Reset();
				RemapSortIndex.AddUninitialized(Num);
				int32 OutIndex = 0;
				for (int32 Index = 0; Index < NumRoots; Index++)
				{
					FClusterNode& Node = (*Result->Nodes.Get())[SortIndex[Index]];
					for (int32 InstanceIndex = Node.FirstInstance; InstanceIndex <= Node.LastInstance; InstanceIndex++)
					{
						RemapSortIndex[OutIndex++] = InstanceIndex;
					}
				}
				InverseInstanceIndex.Reset();
				InverseInstanceIndex.AddUninitialized(Num);
				for (int32 Index = 0; Index < Num; Index++)
				{
					InverseInstanceIndex[RemapSortIndex[Index]] = Index;
				}
				for (int32 Index = 0; Index < (*Result->Nodes.Get()).Num(); Index++)
				{
					FClusterNode& Node = (*Result->Nodes.Get())[Index];
					Node.FirstInstance = InverseInstanceIndex[Node.FirstInstance];
					Node.LastInstance = InverseInstanceIndex[Node.LastInstance];
				}
				OldInstanceIndex.Reset();
				Swap(OldInstanceIndex, SortedInstances);
				SortedInstances.AddUninitialized(Num);
				for (int32 Index = 0; Index < Num; Index++)
				{
					SortedInstances[Index] = OldInstanceIndex[RemapSortIndex[Index]];
				}
			}
			{
				// rearrange the nodes to match the new order of the old roots
				RemapSortIndex.Reset();
				int32 NewNum = (*Result->Nodes.Get()).Num() + Clusters.Num();
				// RemapSortIndex[new index] == old index
				RemapSortIndex.AddUninitialized(NewNum);
				LevelStarts.Reset();
				LevelStarts.Add(Clusters.Num());
				for (int32 Index = 0; Index < NodesPerLevel.Num() - 1; Index++)
				{
					LevelStarts.Add(LevelStarts[Index] + NodesPerLevel[Index]);
				}

				for (int32 Index = 0; Index < NumRoots; Index++)
				{
					FClusterNode& Node = (*Result->Nodes.Get())[SortIndex[Index]];
					RemapSortIndex[LevelStarts[0]++] = SortIndex[Index];

					int32 LeftIndex = Node.FirstChild;
					int32 RightIndex = Node.LastChild;
					int32 LevelIndex = 1;
					while (RightIndex >= 0)
					{
						int32 NextLeftIndex = MAX_int32;
						int32 NextRightIndex = -1;
						for (int32 ChildIndex = LeftIndex; ChildIndex <= RightIndex; ChildIndex++)
						{
							RemapSortIndex[LevelStarts[LevelIndex]++] = ChildIndex;
							int32 LeftChild = (*Result->Nodes.Get())[ChildIndex].FirstChild;
							int32 RightChild = (*Result->Nodes.Get())[ChildIndex].LastChild;
							if (LeftChild >= 0 && LeftChild < NextLeftIndex)
							{
								NextLeftIndex = LeftChild;
							}
							if (RightChild >= 0 && RightChild > NextRightIndex)
							{
								NextRightIndex = RightChild;
							}
						}
						LeftIndex = NextLeftIndex;
						RightIndex = NextRightIndex;
						LevelIndex++;
					}
				}
				checkSlow(LevelStarts[LevelStarts.Num() - 1] == NewNum);
				InverseChildIndex.Reset();
				// InverseChildIndex[old index] == new index
				InverseChildIndex.AddUninitialized(NewNum);
				for (int32 Index = Clusters.Num(); Index < NewNum; Index++)
				{
					InverseChildIndex[RemapSortIndex[Index]] = Index;
				}
				for (int32 Index = 0; Index < (*Result->Nodes.Get()).Num(); Index++)
				{
					FClusterNode& Node = (*Result->Nodes.Get())[Index];
					if (Node.FirstChild >= 0)
					{
						Node.FirstChild = InverseChildIndex[Node.FirstChild];
						Node.LastChild = InverseChildIndex[Node.LastChild];
					}
				}
				{
					Swap(OldNodes, (*Result->Nodes.Get()));
					(*Result->Nodes.Get()).Empty(NewNum);
					for (int32 Index = 0; Index < Clusters.Num(); Index++)
					{
						(*Result->Nodes.Get()).Add(FClusterNode());
					}
					(*Result->Nodes.Get()).AddUninitialized(OldNodes.Num());
					for (int32 Index = 0; Index < OldNodes.Num(); Index++)
					{
						(*Result->Nodes.Get())[InverseChildIndex[Index]] = OldNodes[Index];
					}
				}
				int32 OldIndex = Clusters.Num();
				int32 InstanceTracker = 0;
				for (int32 Index = 0; Index < Clusters.Num(); Index++)
				{
					FClusterNode& Node = (*Result->Nodes.Get())[Index];
					Node.FirstChild = OldIndex;
					OldIndex += Clusters[Index].Num;
					Node.LastChild = OldIndex - 1;
					Node.FirstInstance = (*Result->Nodes.Get())[Node.FirstChild].FirstInstance;
					checkSlow(Node.FirstInstance == InstanceTracker);
					Node.LastInstance = (*Result->Nodes.Get())[Node.LastChild].LastInstance;
					InstanceTracker = Node.LastInstance + 1;
					checkSlow(InstanceTracker <= Num);
					FBox NodeBox(ForceInit);
					for (int32 ChildIndex = Node.FirstChild; ChildIndex <= Node.LastChild; ChildIndex++)
					{
						FClusterNode& ChildNode = (*Result->Nodes.Get())[ChildIndex];
						NodeBox += (FVector)ChildNode.BoundMin;
						NodeBox += (FVector)ChildNode.BoundMax;

						if (GenerateInstanceScalingRange)
						{
							Node.MinInstanceScale = Node.MinInstanceScale.ComponentMin(ChildNode.MinInstanceScale);
							Node.MaxInstanceScale = Node.MaxInstanceScale.ComponentMax(ChildNode.MaxInstanceScale);
						}
					}
					Node.BoundMin = (FVector3f)NodeBox.Min;
					Node.BoundMax = (FVector3f)NodeBox.Max;
				}
				NumRoots = Clusters.Num();
				NodesPerLevel.Insert(NumRoots, 0);
			}
		}

		// Save inverse map
		Result->InstanceReorderTable.Init(INDEX_NONE, OriginalNum);
		for (int32 Index = 0; Index < Num; Index++)
		{
			Result->InstanceReorderTable[SortedInstances[Index]] = Index;
		}

		// Output a general scale of 1 if we dont want the scaling range
		if (!GenerateInstanceScalingRange)
		{
			(*Result->Nodes.Get())[0].MinInstanceScale = FVector3f::OneVector;
			(*Result->Nodes.Get())[0].MaxInstanceScale = FVector3f::OneVector;
		}
	}
};


};
void USWHISMComponent::SWUpdateTree()
{
	if(SWPerInstanceSMData.IsValid())
	{
		SWNewUpdateTree();
		return;
	}
	// The tree will be fully rebuilt once the static mesh compilation is finished.
	if (HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject) || (GetStaticMesh() && GetStaticMesh()->IsCompiling()))
	{
		return;
	}
	if (bIsOutOfDate
		|| InstanceUpdateCmdBuffer.NumTotalCommands() != 0
		|| InstanceReorderTable.Num() != PerInstanceSMData.Num()
		|| NumBuiltInstances != PerInstanceSMData.Num()
		|| (GetStaticMesh() != nullptr && CacheMeshExtendedBounds != GetStaticMesh()->GetBounds())
		|| UnbuiltInstanceBoundsList.Num() > 0
		|| GetLinkerUEVersion() < VER_UE4_REBUILD_HIERARCHICAL_INSTANCE_TREES
		|| GetLinkerCustomVersion(FReleaseObjectVersion::GUID) < FReleaseObjectVersion::HISMCClusterTreeMigration)
	{
	
	if (!GetStaticMesh())
	{
		ApplyEmpty();
	}
	else if(!GetStaticMesh()->HasAnyFlags(RF_NeedLoad))
	{
		
		// Make sure if any of those conditions is true, we mark ourselves out of date so the Async Build completes
		bIsOutOfDate = true;

		GetStaticMesh()->ConditionalPostLoad();

		// Trying to do async processing on the begin play does not work, as this will be dirty but not ready for rendering
		// Shader World use runtime spawned HISM, we're therefore always past beginplay, otherwise we wouldn't be here
		//const bool bForceSync = (NumBuiltInstances == 0 && GetWorld() && !GetWorld()->HasBegunPlay());

		//if (!bForceSync)
		{
			if (IsAsyncBuilding())
			{
				// invalidate the results of the current async build we need to modify the tree
				bConcurrentChanges = true;
			}
			else
			{
				SWBuildTreeAsync();
			}
		}/*
		else
		{
			UHierarchicalInstancedStaticMeshComponent::BuildTreeIfOutdated(true,false);
		}*/
		
	}
	}
	
}

int32 USWHISMComponent::GetNumRenderInstances() const
{
	if (!SWPerInstanceSMData.IsValid())
		return PerInstanceSMData.Num();//Super::GetNumRenderInstances();
	else
		return SWPerInstanceSMData->PerInstanceSMData.Num();
}


void USWHISMComponent::SWNewUpdateTree()
{
	// The tree will be fully rebuilt once the static mesh compilation is finished.
	if (HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject) || (GetStaticMesh() && GetStaticMesh()->IsCompiling()))
	{
		return;
	}

	bool ReorderNumDiffPerInstanceNum = false;

	{
		FScopeLock lock(&SWPerInstanceSMData->DataLock);
		ReorderNumDiffPerInstanceNum |= InstanceReorderTable.Num() != SWPerInstanceSMData->PerInstanceSMData.Num();
		ReorderNumDiffPerInstanceNum |= NumBuiltInstances != SWPerInstanceSMData->PerInstanceSMData.Num();
	}

	if (bIsOutOfDate
		|| InstanceUpdateCmdBuffer.NumTotalCommands() != 0
		|| ReorderNumDiffPerInstanceNum
		|| (GetStaticMesh() != nullptr && CacheMeshExtendedBounds != GetStaticMesh()->GetBounds())
		|| UnbuiltInstanceBoundsList.Num() > 0)
	{

		if (!GetStaticMesh())
		{
			ApplyEmpty();
		}
		else if (!GetStaticMesh()->HasAnyFlags(RF_NeedLoad))
		{

			// Make sure if any of those conditions is true, we mark ourselves out of date so the Async Build completes
			bIsOutOfDate = true;

			GetStaticMesh()->ConditionalPostLoad();

			// Trying to do async processing on the begin play does not work, as this will be dirty but not ready for rendering
			// Shader World HISM are created at runtime, therefore the game has begunplay already, otherwise we wouldn't be here.
			
			if (IsAsyncBuilding())
			{
				// invalidate the results of the current async build we need to modify the tree
				bConcurrentChanges = true;
			}
			else
			{
				SWNewBuildTreeAsync();
			}			
		}
	}
}

void USWHISMComponent::BuildTreeAnyThread(TArray<FMatrix>& InstanceTransforms, TArray<float>& InstanceCustomDataFloats,
	int32 NumCustomDataFloats, const FBox& MeshBox, TArray<FClusterNode>& OutClusterTree,
	TArray<int32>& OutSortedInstances, TArray<int32>& OutInstanceReorderTable, int32& OutOcclusionLayerNum,
	int32 MaxInstancesPerLeaf, bool InGenerateInstanceScalingRange)
{
	/*
	// do grass need this?
	float DensityScaling = 1.0f;
	int32 InstancingRandomSeed = 1;

	FClusterBuilder Builder(InstanceTransforms, InstanceCustomDataFloats, NumCustomDataFloats, MeshBox, MaxInstancesPerLeaf, DensityScaling, InstancingRandomSeed, InGenerateInstanceScalingRange);
	Builder.BuildTree();
	OutOcclusionLayerNum = Builder.Result->OutOcclusionLayerNum;

	OutClusterTree = MoveTemp(*Builder.Result->Nodes);
	OutInstanceReorderTable = MoveTemp(Builder.Result->InstanceReorderTable);
	OutSortedInstances = MoveTemp(Builder.Result->SortedInstances);
	*/
}


bool USWHISMComponent::SWBatchUpdateInstancesDataInternal(int32 StartInstanceIndex, int32 NumInstances, FInstancedStaticMeshInstanceData* StartInstanceData, bool bMarkRenderStateDirty, bool bTeleport, int32 StartInstanceDataIndex)
{
	{
		FScopeLock lock(&PerInstanceDataMutex);

		if (!PerInstanceSMData.IsValidIndex(StartInstanceIndex) || !PerInstanceSMData.IsValidIndex(StartInstanceIndex + NumInstances - 1))
		{
			return false;
		}
	}
	

	
	Modify();
	
	
	FMemory::Memcpy(&PerInstanceSMData[StartInstanceIndex], &StartInstanceData[StartInstanceDataIndex], NumInstances*sizeof(FMatrix));

	if (bPhysicsStateCreated)
	{
		FScopeLock lock(&PerInstanceDataMutex);

		TArray<FTransform> WorldTransforms;
		WorldTransforms.AddUninitialized(NumInstances);

		TArray<FBodyInstance*> BodyInstancedPreAllocated;
		BodyInstancedPreAllocated.AddUninitialized(NumInstances);

		const FTransform CompoTransform = GetComponentTransform();

		for(int32 k = 0; k < NumInstances; k++)
		{
			WorldTransforms.GetData()[k] = FTransform((&PerInstanceSMData.GetData()[StartInstanceIndex])[k].Transform) * CompoTransform;

			if (WorldTransforms.GetData()[k].GetScale3D().IsNearlyZero())
			{

			}
			else
			{
				FBodyInstance*& InstanceBodyInstance = (&InstanceBodies[StartInstanceIndex])[k];
				if (InstanceBodyInstance)
				{

				}
				else
				{
					FBodyInstance** BIP = BodyInstancedPreAllocated.GetData();
					BIP[k] = new FBodyInstance();
					BIP[k]->CopyBodyInstancePropertiesFrom(&BodyInstance);
					BIP[k]->InstanceBodyIndex = StartInstanceIndex + k;
					BIP[k]->bSimulatePhysics = false;
					BIP[k]->bAutoWeld = false;
				}
			}
		}
		
		UBodySetup* BodySetup = GetBodySetup();

		for (int32 i = 0; i < NumInstances; ++i)
		{
			const int32 InstanceIndex = StartInstanceIndex + i;

			
			{
				// Physics uses world transform of the instance
				FTransform& WorldTransform = WorldTransforms[i];//FTransform(InstanceData.Transform) * GetComponentTransform();
				//UpdateInstanceBodyTransform(InstanceIndex, WorldTransform, bTeleport);
				{
					FBodyInstance*& InstanceBodyInstance = InstanceBodies[InstanceIndex];

					if (WorldTransform.GetScale3D().IsNearlyZero())
					{

						if (InstanceBodyInstance)
						{
							// delete BodyInstance
							
							InstanceBodyInstance->TermBody();
							delete InstanceBodyInstance;
							InstanceBodyInstance = nullptr;
						}
					}
					else
					{
						if (InstanceBodyInstance)
						{
							// Update existing BodyInstance
							InstanceBodyInstance->SetBodyTransform(WorldTransform, TeleportFlagToEnum(bTeleport));
							InstanceBodyInstance->UpdateBodyScale(WorldTransform.GetScale3D());

						}
						else
						{

							InstanceBodyInstance = BodyInstancedPreAllocated[i];
							{
								if (!GetStaticMesh())
								{
									UE_LOG(LogStaticMesh, Warning, TEXT("Unabled to create a body instance for %s in Actor %s. No StaticMesh set."), *GetName(), GetOwner() ? *GetOwner()->GetName() : TEXT("?"));
									
								}
								else
								{
									
									check(InstanceIndex < PerInstanceSMData.Num());
									check(InstanceIndex < InstanceBodies.Num());
									check(InstanceBodyInstance);

									
									check(BodySetup);									
									
									InstanceBodyInstance->InitBody(BodySetup, WorldTransforms[i], this, GetWorld()->GetPhysicsScene(), nullptr);
								}
							}
							
						}
					}
				}
			}
		}

		// Request navigation update - Execute on a single index as it updates everything anyway
		PartialNavigationUpdate(StartInstanceIndex);
	}
	



	// Force recreation of the render data when proxy is created
	InstanceUpdateCmdBuffer.NumEdits++;

	if (bMarkRenderStateDirty)
	{
		MarkRenderStateDirty();
	}

	return true;
}

bool USWHISMComponent::SWNewBatchUpdateCountInstanceData(int32 StartInstanceIndex, int32 NumInstances,	TArray<FInstancedStaticMeshInstanceData>& NewInstancesTransforms, bool bWorldSpace, bool bMarkRenderStateDirty,	bool bTeleport, int32 StartInstanceDataIndex)
{
	if(!SWPerInstanceSMData.IsValid())
	{
		return false;
	}

	FInstancedStaticMeshInstanceData* StartInstanceData = NewInstancesTransforms.GetData();

	{
		FScopeLock lock(&SWPerInstanceSMData->DataLock);
		if (!SWPerInstanceSMData->PerInstanceSMData.IsValidIndex(StartInstanceIndex) || !SWPerInstanceSMData->PerInstanceSMData.IsValidIndex(StartInstanceIndex + NumInstances - 1))
		{
			return false;
		}
	}

	/*
	 * Modify relevant only for serialisation? Our spawnables are full transient data in shader world
	 */
	//Modify();

	FMemory::Memcpy(&SWPerInstanceSMData->PerInstanceSMData[StartInstanceIndex], &StartInstanceData[StartInstanceDataIndex], NumInstances * sizeof(FMatrix));

	if (bPhysicsStateCreated)
	{
		FScopeLock lock(&SWPerInstanceSMData->DataLock);
		TArray<FTransform> WorldTransforms;
		WorldTransforms.AddUninitialized(NumInstances);

		TArray<FBodyInstance*> BodyInstancedPreAllocated;
		BodyInstancedPreAllocated.AddUninitialized(NumInstances);

		const FTransform CompoTransform = GetComponentTransform();

		for (int32 k = 0; k < NumInstances; k++)
		{
			WorldTransforms.GetData()[k] = FTransform((&SWPerInstanceSMData->PerInstanceSMData.GetData()[StartInstanceIndex])[k].Transform) * CompoTransform;

			if (WorldTransforms.GetData()[k].GetScale3D().IsNearlyZero())
			{

			}
			else
			{
				FBodyInstance*& InstanceBodyInstance = (&InstanceBodies[StartInstanceIndex])[k];
				if (InstanceBodyInstance)
				{

				}
				else
				{
					FBodyInstance** BIP = BodyInstancedPreAllocated.GetData();
					BIP[k] = new FBodyInstance();
					BIP[k]->CopyBodyInstancePropertiesFrom(&BodyInstance);
					BIP[k]->InstanceBodyIndex = StartInstanceIndex + k;
					BIP[k]->bSimulatePhysics = false;
					BIP[k]->bAutoWeld = false;
				}
			}
		}

		UBodySetup* BodySetup = GetBodySetup();

		for (int32 i = 0; i < NumInstances; ++i)
		{
			const int32 InstanceIndex = StartInstanceIndex + i;


			{
				// Physics uses world transform of the instance
				FTransform& WorldTransform = WorldTransforms[i];//FTransform(InstanceData.Transform) * GetComponentTransform();
				//UpdateInstanceBodyTransform(InstanceIndex, WorldTransform, bTeleport);
				{
					FBodyInstance*& InstanceBodyInstance = InstanceBodies[InstanceIndex];

					if (WorldTransform.GetScale3D().IsNearlyZero())
					{

						if (InstanceBodyInstance)
						{
							// delete BodyInstance

							InstanceBodyInstance->TermBody();
							delete InstanceBodyInstance;
							InstanceBodyInstance = nullptr;
						}
					}
					else
					{
						if (InstanceBodyInstance)
						{
							// Update existing BodyInstance
							InstanceBodyInstance->SetBodyTransform(WorldTransform, TeleportFlagToEnum(bTeleport));
							InstanceBodyInstance->UpdateBodyScale(WorldTransform.GetScale3D());

						}
						else
						{

							InstanceBodyInstance = BodyInstancedPreAllocated[i];
							{
								if (!GetStaticMesh())
								{
									UE_LOG(LogStaticMesh, Warning, TEXT("Unabled to create a body instance for %s in Actor %s. No StaticMesh set."), *GetName(), GetOwner() ? *GetOwner()->GetName() : TEXT("?"));

								}
								else
								{

									check(InstanceIndex < SWPerInstanceSMData->PerInstanceSMData.Num());
									check(InstanceIndex < InstanceBodies.Num());
									check(InstanceBodyInstance);


									check(BodySetup);

									InstanceBodyInstance->InitBody(BodySetup, WorldTransforms[i], this, GetWorld()->GetPhysicsScene(), nullptr);
								}
							}

						}
					}
				}
			}
		}

		// Request navigation update - Execute on a single index as it updates everything anyway
		PartialNavigationUpdate(StartInstanceIndex);
	}



	
	
	/*
	// Force recreation of the render data when proxy is created
	InstanceUpdateCmdBuffer.NumEdits++;
	if (bMarkRenderStateDirty)
	{
		MarkRenderStateDirty();
	}*/

	return true;
}

FVector USWHISMComponent::SWCalcTranslatedInstanceSpaceOrigin() const
{
	// Foliage is often built in world space which can cause problems with large world coordinates because
	// the instance transforms in the renderer are single precision, and the HISM culling is also single precision.
	// We should fix the HISM culling to be double precision.
	// But the instance transforms (relative to the owner primitive) will probably stay single precision to not bloat memory.
	// A fix for that is to have authoring tools set sensible primitive transforms (instead of identity).
	// But until that happens we set a translated instance space here.
	// For simplicity we use the first instance as the origin of the translated space.
	return bUseTranslatedInstanceSpace && PerInstanceSMData.Num() ? PerInstanceSMData[0].Transform.GetOrigin() : FVector::Zero();
}

void USWHISMComponent::SWGetInstanceTransforms(TArray<FMatrix>& InstanceTransforms, FVector const& Offset) const
{

	int32 Num = PerInstanceSMData.Num();

	InstanceTransforms.SetNumUninitialized(Num);

	for (int32 Index = 0; Index < Num; Index++)
	{
		InstanceTransforms[Index] = PerInstanceSMData[Index].Transform.ConcatTranslation(Offset);
	}
	
}

void USWHISMComponent::SWNewApplyBuildTree(SW_HISM::FClusterBuilder& Builder)
{
	bool bForceSync = false;
	int32 NumRenderInstances = Builder.Result->SortedInstances.Num();

	if(Builder.Result->SortedInstances.Num() != SWPerInstanceSMData->PerInstanceSMData.Num())
	{
		SW_LOG("Builder.Result->SortedInstances.Num() != SWPerInstanceSMData->PerInstanceSMData.Num()")
	}
	if ( (NumRenderInstances>0) && (Builder.Result->Nodes->Num() > 0))
	{
		//DrawClusterTree(*Builder.Result->Nodes, 0);

		if (!PerInstanceRenderData.IsValid())
		{
			InitPerInstanceRenderData(true, Builder.BuiltInstanceData.Get(), Builder.RequireCPUAccess/*, Inner.Builder->RequireCPUAccess*/);
		}
		else
		{
			PerInstanceRenderData->UpdateFromPreallocatedData(*Builder.BuiltInstanceData.Get());
		}


		{
			/*
			 * If using nanite enabled proxy, PerinstanceSMData will not be empty
			 */
			PerInstanceSMData.Empty();

			SWInstanceSceneData = Builder.InstanceSceneData;
			SWInstanceRandomID = Builder.InstanceRandomID;

			NumBuiltInstances = Builder.Result->InstanceReorderTable.Num();
			InstanceReorderTable = MoveTemp(Builder.Result->InstanceReorderTable);
			SortedInstances = MoveTemp(Builder.Result->SortedInstances);
			CacheMeshExtendedBounds = GetStaticMesh()->GetBounds();
		}

		AcceptPrebuiltTree(Builder.Result->InstanceData, *Builder.Result->Nodes.Get(), Builder.Result->OutOcclusionLayerNum, NumRenderInstances);

		
		
		if (bForceSync && GetWorld())
		{
			RecreateRenderState_Concurrent();
		}
		
	}
	else
	{
		SW_LOG("Received Builder.Result->SortedInstances.Num() %d Builder.Result->Nodes->Num() %d", Builder.Result->SortedInstances.Num(), Builder.Result->Nodes->Num())
	}

	

}

void USWHISMComponent::SWNewApplyBuildTreeAsync(ENamedThreads::Type CurrentThread,
                                                const FGraphEventRef& MyCompletionGraphEvent, TSharedRef<SW_HISM::FClusterBuilder, ESPMode::ThreadSafe> Builder,
                                                double StartTime)
{

	bIsAsyncBuilding = false;
	BuildTreeAsyncTasks.Empty();
	
	if (PendingClearInstances)
	{
		PendingClearInstances = false;

		bIsOutOfDate = false;
		bConcurrentChanges = false;

		ClearInstances();

		return;
	}

	// We did a sync build while async building. The sync build is newer so we will use that.
	if (!bIsOutOfDate)
	{
		bConcurrentChanges = false;

		return;
	}

	// We did some changes during an async build

	if (bConcurrentChanges)
	{
		bConcurrentChanges = false;

		UE_LOG(LogStaticMesh, Verbose, TEXT("Discarded foliage hierarchy of %d elements build due to concurrent removal (%.1fs)"), Builder->Result->InstanceReorderTable.Num(), (float)(FPlatformTime::Seconds() - StartTime));

		// There were changes while we were building, it's too slow to fix up the result now, so build async again.
		SWNewBuildTreeAsync();

		return;
	}

	// Completed the build
	SWNewApplyBuildTree(Builder.Get());
}

void USWHISMComponent::SWBuildTreeAsync()
{
	// Make sure while Tree is building that our render state is updated (Command Buffer is processed)
	// Done at proxy recreation 
	
	if (InstanceUpdateCmdBuffer.NumInlineCommands() > 0 && PerInstanceRenderData.IsValid() && PerInstanceRenderData->InstanceBuffer.RequireCPUAccess)
	{
		PerInstanceRenderData->UpdateFromCommandBuffer(InstanceUpdateCmdBuffer);		
		//MarkRenderStateDirty();
	}
	

	// Verify that the mesh is valid before using it.
	// The tree will be fully rebuilt once the static mesh compilation is finished, no need to do it now.
	if (PerInstanceSMData.Num() > 0 && GetStaticMesh() && !GetStaticMesh()->IsCompiling() && GetStaticMesh()->HasValidRenderData())
	{
		double StartTime = FPlatformTime::Seconds();

		// Build the tree in translated space to maintain precision.
		//TranslatedInstanceSpaceOrigin = SWCalcTranslatedInstanceSpaceOrigin();

		InitializeInstancingRandomSeed();

		// Oh dear
		//TArray<FMatrix> InstanceTransforms;
		//SWGetInstanceTransforms(InstanceTransforms, -TranslatedInstanceSpaceOrigin);
		//TArray<FInstancedStaticMeshInstanceData> InstanceTransforms(PerInstanceSMData);
		//SWInstanceSceneData = MakeShared<TArray<FPrimitiveInstance, TInlineAllocator<1>>, ESPMode::ThreadSafe>();

		TSharedRef<SW_HISM::FClusterBuilder, ESPMode::ThreadSafe> Builder(new SW_HISM::FClusterBuilder(TWeakObjectPtr<USWHISMComponent>(this), SWPerInstanceSMData, SWInstanceSceneData, SWInstanceRandomID, PerInstanceSMData, PerInstanceSMCustomData, NumCustomDataFloats, GetStaticMesh()->GetBounds().GetBox(), DesiredInstancesPerLeaf(), CurrentDensityScaling, InstancingRandomSeed, PerInstanceSMData.Num() > 0, bKeepInstanceBufferCPUCopy));

		bIsAsyncBuilding = true;		
		
		FGraphEventRef BuildTreeAsyncResult(
			FDelegateGraphTask::CreateAndDispatchWhenReady(FDelegateGraphTask::FDelegate::CreateRaw(&Builder.Get(), &SW_HISM::FClusterBuilder::BuildTreeAndBufferAsync), GET_STATID(STAT_SWFoliageBuildTime), NULL, ENamedThreads::GameThread, ENamedThreads::AnyBackgroundThreadNormalTask));

		BuildTreeAsyncTasks.Add(BuildTreeAsyncResult);

		// add a dependent task to run on the main thread when build is complete
		FGraphEventRef PostBuildTreeAsyncResult(
			FDelegateGraphTask::CreateAndDispatchWhenReady(
				FDelegateGraphTask::FDelegate::CreateUObject(this, &USWHISMComponent::SWApplyBuildTreeAsync, Builder, StartTime), GET_STATID(STAT_SWFoliageBuildTime),
				BuildTreeAsyncResult, ENamedThreads::GameThread, ENamedThreads::GameThread
			)
		);

		BuildTreeAsyncTasks.Add(PostBuildTreeAsyncResult);

	}
	else
	{
		SWApplyEmpty();
	}
}

void USWHISMComponent::SWNewBuildTreeAsync()
{
	// Verify that the mesh is valid before using it.
	// The tree will be fully rebuilt once the static mesh compilation is finished, no need to do it now.
	if (SWPerInstanceSMData->PerInstanceSMData.Num() > 0 && GetStaticMesh() && !GetStaticMesh()->IsCompiling() && GetStaticMesh()->HasValidRenderData())
	{
		double StartTime = FPlatformTime::Seconds();

		// Build the tree in translated space to maintain precision.
		//TranslatedInstanceSpaceOrigin = SWCalcTranslatedInstanceSpaceOrigin();

		InitializeInstancingRandomSeed();

		// Oh dear
		//TArray<FMatrix> InstanceTransforms;
		//SWGetInstanceTransforms(InstanceTransforms, -TranslatedInstanceSpaceOrigin);
		//TArray<FInstancedStaticMeshInstanceData> InstanceTransforms(PerInstanceSMData);

		//New InstanceSceneData
		const TSharedPtr < TArray <FPrimitiveInstance, TInlineAllocator<1>>, ESPMode::ThreadSafe > BuilderInstanceSceneData = MakeShared < TArray <FPrimitiveInstance, TInlineAllocator<1>>, ESPMode::ThreadSafe>();
		const TSharedPtr < TArray<float>, ESPMode::ThreadSafe > BuilderInstanceRandomID = MakeShared < TArray<float>, ESPMode::ThreadSafe>();

		TSharedRef<SW_HISM::FClusterBuilder, ESPMode::ThreadSafe> Builder(new SW_HISM::FClusterBuilder(TWeakObjectPtr<USWHISMComponent>(this), SWPerInstanceSMData, BuilderInstanceSceneData, BuilderInstanceRandomID, SWPerInstanceSMData->PerInstanceSMData, PerInstanceSMCustomData, NumCustomDataFloats, GetStaticMesh()->GetBounds().GetBox(), DesiredInstancesPerLeaf(), CurrentDensityScaling, InstancingRandomSeed, SWPerInstanceSMData->PerInstanceSMData.Num() > 0, bKeepInstanceBufferCPUCopy));

		bIsAsyncBuilding = true;

		FGraphEventRef BuildTreeAsyncResult(
			FDelegateGraphTask::CreateAndDispatchWhenReady(FDelegateGraphTask::FDelegate::CreateRaw(&Builder.Get(), &SW_HISM::FClusterBuilder::BuildTreeAndBufferAsync), GET_STATID(STAT_SWFoliageBuildTime), NULL, ENamedThreads::GameThread, ENamedThreads::AnyBackgroundThreadNormalTask));

		BuildTreeAsyncTasks.Add(BuildTreeAsyncResult);

		// add a dependent task to run on the main thread when build is complete
		FGraphEventRef PostBuildTreeAsyncResult(
			FDelegateGraphTask::CreateAndDispatchWhenReady(
				FDelegateGraphTask::FDelegate::CreateUObject(this, &USWHISMComponent::SWNewApplyBuildTreeAsync, Builder, StartTime), GET_STATID(STAT_SWFoliageBuildTime),
				BuildTreeAsyncResult, ENamedThreads::GameThread, ENamedThreads::GameThread
			)
		);

		BuildTreeAsyncTasks.Add(PostBuildTreeAsyncResult);

	}
}

void USWHISMComponent::SWApplyBuildTreeAsync(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent, TSharedRef<SW_HISM::FClusterBuilder, ESPMode::ThreadSafe> Builder, double StartTime)
{

	bIsAsyncBuilding = false;
	BuildTreeAsyncTasks.Empty();

	if(PendingClearInstances)
	{
		PendingClearInstances = false;

		bIsOutOfDate = false;
		bConcurrentChanges = false;

		ClearInstances();

		return;
	}

	// We did a sync build while async building. The sync build is newer so we will use that.
	if (!bIsOutOfDate)
	{
		bConcurrentChanges = false;

		return;
	}

	// We did some changes during an async build
	
	if (bConcurrentChanges)
	{
		bConcurrentChanges = false;

		UE_LOG(LogStaticMesh, Verbose, TEXT("Discarded foliage hierarchy of %d elements build due to concurrent removal (%.1fs)"), Builder->Result->InstanceReorderTable.Num(), (float)(FPlatformTime::Seconds() - StartTime));

		// There were changes while we were building, it's too slow to fix up the result now, so build async again.
		SWBuildTreeAsync();

		return;
	}

	// Completed the builds
	SWApplyBuildTree(Builder.Get(),/*bWasAsyncBuild*/true, Builder->RequireCPUAccess);
}


void USWHISMComponent::DrawClusterTree(TArray<FClusterNode>& Nodes, int32 NodeIndice)
{
	if(Nodes[NodeIndice].FirstChild<0)
	{
		const FBox NodeBounds = FBox(Nodes[NodeIndice].BoundMin, Nodes[NodeIndice].BoundMax).TransformBy(GetRenderMatrix());
		DrawDebugBox(GetWorld(), NodeBounds.GetCenter(), NodeBounds.GetExtent(),FColor::Cyan,false,0.5f);
	}
	else
	{
		for(int32 Child = Nodes[NodeIndice].FirstChild; Child <= Nodes[NodeIndice].LastChild; Child++)
		{
			DrawClusterTree(Nodes, Child);
		}
	}
}

void USWHISMComponent::SWApplyBuildTree(SW_HISM::FClusterBuilder& Builder, const bool bWasAsyncBuild, const bool bRequireCPU)
{
		bIsOutOfDate = false;

		//InstanceUpdateCmdBuffer.Reset();
		InstanceUpdateCmdBuffer.Cmds.Empty();
		InstanceUpdateCmdBuffer.NumCustomDataFloats = 0;
		InstanceUpdateCmdBuffer.NumAdds = 0;
		InstanceUpdateCmdBuffer.NumUpdates = 0;
		InstanceUpdateCmdBuffer.NumCustomFloatUpdates = 0;
		InstanceUpdateCmdBuffer.NumRemoves = 0;
		InstanceUpdateCmdBuffer.NumEdits = 0;

		check(Builder.Result->InstanceReorderTable.Num() == PerInstanceSMData.Num());

		NumBuiltInstances = Builder.Result->InstanceReorderTable.Num();
		NumBuiltRenderInstances = Builder.Result->SortedInstances.Num();

		ClusterTreePtr = Builder.Result->Nodes;
	
		InstanceReorderTable = MoveTemp(Builder.Result->InstanceReorderTable);
		SortedInstances = MoveTemp(Builder.Result->SortedInstances);
		CacheMeshExtendedBounds = GetStaticMesh()->GetBounds();

		TUniquePtr<FStaticMeshInstanceData> BuiltInstanceData = MoveTemp(Builder.BuiltInstanceData);

		OcclusionLayerNumNodes = Builder.Result->OutOcclusionLayerNum;

		// Get the new bounds taking into account the translated space used when building the tree.
		const TArray<FClusterNode>& ClusterTree = *ClusterTreePtr;
		BuiltInstanceBounds = GetClusterTreeBounds(ClusterTree, TranslatedInstanceSpaceOrigin);


		UnbuiltInstanceBounds.Init();
		UnbuiltInstanceBoundsList.Empty();

		check(BuiltInstanceData.IsValid());
		check(BuiltInstanceData->GetNumInstances() == NumBuiltRenderInstances);

		InstanceCountToRender = NumBuiltInstances;

		check(InstanceReorderTable.Num() == PerInstanceSMData.Num());

		// create per-instance hit-proxies if needed
		TArray<TRefCountPtr<HHitProxy>> HitProxies;
		CreateHitProxyData(HitProxies);
		//SetPerInstanceLightMapAndEditorData(*BuiltInstanceData, HitProxies);

		if (PerInstanceRenderData.IsValid())
		{
			PerInstanceRenderData->UpdateFromPreallocatedData(*BuiltInstanceData);
		}
		else
		{
			InitPerInstanceRenderData(false, BuiltInstanceData.Get(), bRequireCPU);
		}
		PerInstanceRenderData->HitProxies = MoveTemp(HitProxies);
		
		FlushAccumulatedNavigationUpdates();
		PostBuildStats();

		if(!bPhysicsStateCreated)
			MarkRenderStateDirty();

		FHierarchicalInstancedStaticMeshDelegates::OnTreeBuilt.Broadcast(this, bWasAsyncBuild);

}

void USWHISMComponent::SWApplyEmpty()
{
	bIsOutOfDate = false;
	ClusterTreePtr = MakeShareable(new TArray<FClusterNode>);
	NumBuiltInstances = 0;
	NumBuiltRenderInstances = 0;
	InstanceCountToRender = 0;
	InstanceReorderTable.Empty();
	SortedInstances.Empty();
	UnbuiltInstanceBoundsList.Empty();
	BuiltInstanceBounds.Init();
	CacheMeshExtendedBounds = GetStaticMesh() && GetStaticMesh()->HasValidRenderData() ? GetStaticMesh()->GetBounds() : FBoxSphereBounds(ForceInitToZero);

	//InstanceUpdateCmdBuffer.Reset();
	InstanceUpdateCmdBuffer.Cmds.Empty();
	InstanceUpdateCmdBuffer.NumCustomDataFloats = 0;
	InstanceUpdateCmdBuffer.NumAdds = 0;
	InstanceUpdateCmdBuffer.NumUpdates = 0;
	InstanceUpdateCmdBuffer.NumCustomFloatUpdates = 0;
	InstanceUpdateCmdBuffer.NumRemoves = 0;
	InstanceUpdateCmdBuffer.NumEdits = 0;

	if (PerInstanceRenderData.IsValid())
	{
		TUniquePtr<FStaticMeshInstanceData> BuiltInstanceData = MakeUnique<FStaticMeshInstanceData>(GVertexElementTypeSupport.IsSupported(VET_Half2));
		PerInstanceRenderData->UpdateFromPreallocatedData(*BuiltInstanceData);
		PerInstanceRenderData->HitProxies.Empty();
		MarkRenderStateDirty();
	}
}

void USWHISMComponent::AcceptPrebuiltTree(TArray<FInstancedStaticMeshInstanceData>& InInstanceData,
	TArray<FClusterNode>& InClusterTree, int32 InOcclusionLayerNumNodes, int32 InNumBuiltRenderInstances)
{
	checkSlow(IsInGameThread());

	QUICK_SCOPE_CYCLE_COUNTER(STAT_UHierarchicalInstancedStaticMeshComponent_AcceptPrebuiltTree);

	// this is only for prebuild data, already in the correct order
	check(!PerInstanceSMData.Num());

	//NumBuiltInstances = 0;
	TranslatedInstanceSpaceOrigin = FVector::Zero();
	check(PerInstanceRenderData.IsValid());
	NumBuiltRenderInstances = InNumBuiltRenderInstances;
	check(NumBuiltRenderInstances);
	UnbuiltInstanceBounds.Init();
	UnbuiltInstanceBoundsList.Empty();
	ClusterTreePtr = MakeShareable(new TArray<FClusterNode>);
	//InstanceReorderTable.Empty();	
	//SortedInstances.Empty();
	OcclusionLayerNumNodes = InOcclusionLayerNumNodes;
	BuiltInstanceBounds = GetClusterTreeBounds(InClusterTree, FVector::Zero());
	InstanceCountToRender = InNumBuiltRenderInstances;

	// Verify that the mesh is valid before using it.
	const bool bMeshIsValid =
		// make sure we have instances
		NumBuiltRenderInstances > 0 &&
		// make sure we have an actual staticmesh
		GetStaticMesh() &&
		GetStaticMesh()->HasValidRenderData();

	if (bMeshIsValid)
	{
		*ClusterTreePtr = MoveTemp(InClusterTree);

		// We only need to copy off the instances if it is a Nanite mesh, since the Nanite scene proxy uses them instead
		if (ShouldCreateNaniteProxy())
		{
			PerInstanceSMData = MoveTemp(InInstanceData);
		}

		PostBuildStats();

	}
	QUICK_SCOPE_CYCLE_COUNTER(STAT_UHierarchicalInstancedStaticMeshComponent_AcceptPrebuiltTree_Mark);

	MarkRenderStateDirty();
}
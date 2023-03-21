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
#include "Engine/InstancedStaticMesh.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "SWHISMComponent.generated.h"

/*
 *	The difference between Landscape Grass and any other Instanced Mesh is that PerInstanceSMData remains empty for Grass
 *	Because Grass do not care about any book keeping nor incremental update:
 *			Grass is computed once, sent as a whole to proxy, uploaded as a whole to GPU to be rendered.
 *
 *	Except for Nanite Grass which use a different nanite render proxy
 *	For InstancedMeshComponent/HISM : GetInstanceCount() = PerInstanceSMData.Num()
 *
 *	FInstancedStaticMeshSceneProxy::SetupProxy - called in FInstancedStaticMeshSceneProxy constructor - iterate instances for non-grass:
 *	consequence: large hitches on proxy creation for large instance count.
 *
 *	Grass compute the cluster tree once, which output:
 *	BuiltInstanceData (FStaticMeshInstanceData) and InstanceData (TArray<FInstancedStaticMeshInstanceData>)
 *
 *	BuiltInstanceData is used to create PerInstanceRenderData (SharedPtr on the component, copied by render proxy on creation)
 *	(Holds render data that can persist between scene proxy reconstruction)
 *	PerInstanceRenderData hold the InstanceBuffer
 */

class FSWInstanceIndexesInHISM;
class FSWSpawnableTransforms;
class FSWInstancedStaticMeshInstanceDatas;

namespace SW_HISM
{
	class FClusterBuilder;
}

/**
 * 
 */
UCLASS()
class SHADERWORLD_API USWHISMComponent : public UHierarchicalInstancedStaticMeshComponent
{
	GENERATED_BODY()
public:

	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;

	virtual void ClearInstances() override;
	virtual bool UpdateInstanceTransform(int32 InstanceIndex, const FTransform& NewInstanceTransform, bool bWorldSpace, bool bMarkRenderStateDirty = false, bool bTeleport = false) override;
	virtual bool SWBatchUpdateInstancesTransforms(int32 StartInstanceIndex, const TArray<FTransform>& NewInstancesTransforms, bool bWorldSpace = false, bool bMarkRenderStateDirty = false, bool bTeleport = false);
	virtual bool SWBatchUpdateInstanceData(int32 StartInstanceIndex, TArray<FInstancedStaticMeshInstanceData>& NewInstancesTransforms, bool bWorldSpace = false, bool bMarkRenderStateDirty = false, bool bTeleport = false);
	bool SWBatchUpdateCountInstanceData(int32 StartInstanceIndex, int32 NumInstances, TArray<FInstancedStaticMeshInstanceData>& NewInstancesTransforms, bool bWorldSpace = false, bool bMarkRenderStateDirty = false, bool bTeleport = false, int32 StartNewInstanceIndex = 0);
	virtual bool SWAddInstances(TArray<int32>& OutIndices, const TArray<FInstancedStaticMeshInstanceData>& InstanceTransforms, bool bShouldReturnIndices, bool bWorldSpace = false);

	void SWUpdateTree();

	/*
	 * New Path Start
	 */
	virtual int32 GetNumRenderInstances() const override;

	FCriticalSection PerInstanceDataMutex;

	TSharedPtr < FSWInstancedStaticMeshInstanceDatas, ESPMode::ThreadSafe > SWPerInstanceSMData;

	TSharedPtr < TArray<FPrimitiveInstance, TInlineAllocator<1>>, ESPMode::ThreadSafe > SWInstanceSceneData;

	TSharedPtr < TArray<float>, ESPMode::ThreadSafe > SWInstanceRandomID;

	void DrawClusterTree(TArray<FClusterNode>& Nodes, int32 NodeIndice);
	virtual bool SWNewAddInstances(int32 PoolIndex, TSharedPtr < FSWInstanceIndexesInHISM, ESPMode::ThreadSafe>& InstancesIndexes, TSharedPtr < FSWSpawnableTransforms, ESPMode::ThreadSafe>& InstancesTransforms, bool bWorldSpace = false);
	virtual bool SWNewBatchUpdateCountInstanceData(int32 StartInstanceIndex, int32 NumInstances, TArray<FInstancedStaticMeshInstanceData>& NewInstancesTransforms, bool bWorldSpace = false, bool bMarkRenderStateDirty = false, bool bTeleport = false, int32 StartNewInstanceIndex = 0);
	void SWNewUpdateTree();

	static void BuildTreeAnyThread(TArray<FMatrix>& InstanceTransforms, TArray<float>& InstanceCustomDataFloats, int32 NumCustomDataFloats, const FBox& MeshBox, TArray<FClusterNode>& OutClusterTree, TArray<int32>& OutSortedInstances, TArray<int32>& OutInstanceReorderTable, int32& OutOcclusionLayerNum, int32 MaxInstancesPerLeaf, bool InGenerateInstanceScalingRange);

	void AcceptPrebuiltTree(TArray<FInstancedStaticMeshInstanceData>& InInstanceData, TArray<FClusterNode>& InClusterTree, int32 InOcclusionLayerNumNodes, int32 InNumBuiltRenderInstances);
	/*
	 * New Path End
	 */
	void SetKeepInstanceBufferCPUCopy(bool Val){ bKeepInstanceBufferCPUCopy = Val;}

protected:
	bool SWBatchUpdateInstancesDataInternal(int32 StartInstanceIndex, int32 NumInstances, FInstancedStaticMeshInstanceData* StartInstanceData, bool bMarkRenderStateDirty = false, bool bTeleport = false, int32 StartInstanceDataIndex = 0);

	void AddInstancesInternal(TArray<int32>& InstanceIndices, const TArray<FInstancedStaticMeshInstanceData>& InstanceTransforms, bool bShouldReturnIndices, bool bWorldSpace = false);

	void SWBuildTreeAsync();
	void SWNewBuildTreeAsync();
	// Apply the results of the async build
	void SWApplyBuildTreeAsync(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent, TSharedRef<SW_HISM::FClusterBuilder, ESPMode::ThreadSafe> Builder, double StartTime);


	void SWApplyBuildTree(SW_HISM::FClusterBuilder& Builder, const bool bWasAsyncBuild, const bool bRequireCPU);
	void SWApplyEmpty();

	FVector SWCalcTranslatedInstanceSpaceOrigin() const;
	void SWGetInstanceTransforms(TArray<FMatrix>& InstanceTransforms, FVector const& Offset) const;

	void SWNewApplyBuildTree(SW_HISM::FClusterBuilder& Builder);
	void SWNewApplyBuildTreeAsync(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent, TSharedRef<SW_HISM::FClusterBuilder, ESPMode::ThreadSafe> Builder, double StartTime);

	bool PendingClearInstances = false;
	bool bKeepInstanceBufferCPUCopy = false;
};

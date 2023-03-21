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
#include "UObject/ObjectMacros.h"
#include "Interfaces/Interface_CollisionDataProvider.h"
#include "Components/MeshComponent.h"
#include "PhysicsEngine/ConvexElem.h"
#include "Component/GeoClipmapMeshComponent.h"

#include "Components/ModelComponent.h"
#include "ShaderWorldCollisionComponent.generated.h"



class FPrimitiveSceneProxy;
class FSWClipMapCollisionBuffersHolder;
struct FConvexVolume;


extern TGlobalResource< FSWClipMapCollisionBuffersHolder > GSWClipMapBufferCollisionHolder;

class FSWClipMapCollisionBuffersHolder : public FRenderResource
{
	struct FDrawCollisionBuffers
	{
		FStaticMeshVertexBuffers VertexBuffers;
		FSWDynamicMeshIndexBuffer32 IndexBuffer;
		

		FDrawCollisionBuffers(TSharedPtr<FSWShareableIndexBuffer, ESPMode::ThreadSafe>& Indices)
			:IndexBuffer(Indices)
		{
			BeginInitResource(&IndexBuffer);
		}
		~FDrawCollisionBuffers() {};
	};

public:
	FSWClipMapCollisionBuffersHolder()
	{}

	virtual ~FSWClipMapCollisionBuffersHolder() override
	{
		FScopeLock ScopeLock(&MapLock);

		for (auto& Elem : SectionShareableBuffers)
		{
			for (FDrawCollisionBuffers* Section : (Elem.Value))
			{
				if (Section != nullptr)
				{
					Section->VertexBuffers.StaticMeshVertexBuffer.ReleaseResource();
					Section->VertexBuffers.ColorVertexBuffer.ReleaseResource();
					Section->IndexBuffer.ReleaseResource();
				}
				delete Section;
			}
		}
		SectionShareableBuffers.Empty();
	}

	/** Call once to register this extension. */
	void RegisterExtension(const TSharedPtr<FSWShareableID, ESPMode::ThreadSafe>& ID, const int32& NumSection, UShaderWorldCollisionComponent* Component);
	void DiscardBuffers(const TSharedPtr<FSWShareableID, ESPMode::ThreadSafe>& ID);

	FDrawCollisionBuffers* GetBuffersForSection(const TSharedPtr<FSWShareableID, ESPMode::ThreadSafe>& ID, const int32& Index)
	{
		if (!SectionShareableBuffers.Contains(ID))
		{
			UE_LOG(LogTemp,Warning,TEXT("SectionShareableBuffers do not contain ID"));

			return nullptr;
		}
			

		if (Index >= (*SectionShareableBuffers.Find(ID)).Num())
		{
			UE_LOG(LogTemp, Warning, TEXT("Index %d >= (*SectionShareableBuffers.Find(ID)).Num() %d"), Index, (*SectionShareableBuffers.Find(ID)).Num());
			return nullptr;
		}
			

		return (*SectionShareableBuffers.Find(ID))[Index];
	}

protected:

	FCriticalSection MapLock;

	TMap<TSharedPtr<FSWShareableID, ESPMode::ThreadSafe>, TArray < FDrawCollisionBuffers* >> SectionShareableBuffers;

};

/** Class representing a single section of the proc mesh */
class FShaderWColProxySection
{
public:
	/** Material applied to this section */
	UMaterialInterface* Material = nullptr;
	/** Vertex buffer for this section */
	FStaticMeshVertexBuffers VertexBuffers;

	TSharedPtr<FSWShareableVerticePositionBuffer, ESPMode::ThreadSafe> PositionBuffer;

	/** Index buffer for this section */
	//FSWDynamicMeshIndexBuffer32 IndexBuffer;
	/** Vertex factory for this section */
	FLocalVertexFactory VertexFactory;
	/** Whether this section is currently visible */
	bool bSectionVisible;

#if 0 //RHI_RAYTRACING
	FRayTracingGeometry RayTracingGeometry;
#endif

	FShaderWColProxySection(ERHIFeatureLevel::Type InFeatureLevel, TSharedPtr<FSWShareableIndexBuffer, ESPMode::ThreadSafe>& Indices)
		: Material(NULL)		
		//, IndexBuffer(Indices)
		, VertexFactory(InFeatureLevel, "FShaderWColProxySection")
		, bSectionVisible(true)
	{}
};


UCLASS(hidecategories = (Object, LOD), ClassGroup = Rendering)
	class SHADERWORLD_API UShaderWorldCollisionComponent : public UModelComponent
{

	GENERATED_BODY()
public:

	UShaderWorldCollisionComponent(const FObjectInitializer& ObjectInitializer);

	void CreateMeshSection(int32 SectionIndex, const TSharedPtr<FSWShareableVerticePositionBuffer, ESPMode::ThreadSafe>& Vertices, const TSharedPtr<FSWShareableIndexBuffer, ESPMode::ThreadSafe>& Triangles, bool bCreateCollision);

	FGeoCProcMeshSection& GetMeshSectionInit(int32 SectionIndex);

	FGeoCProcMeshSection& GetMeshSection(int32 SectionIndex);
	void SendSections();
	void UpdateSectionTriMesh(TSharedPtr<FSWShareableVerticePositionBuffer, ESPMode::ThreadSafe>& Positions);


	void UpdateCustomBounds(FBoxSphereBounds Newbound);

	/** Clear a section of the procedural mesh. Other sections do not change index. */
	UFUNCTION(BlueprintCallable, Category = "Components|ProceduralMesh")
	void ClearMeshSection(int32 SectionIndex);

	/** Clear all mesh sections and reset to empty state */
	UFUNCTION(BlueprintCallable, Category = "Components|ProceduralMesh")
	void ClearAllMeshSections();

	/** Control visibility of a particular section */
	UFUNCTION(BlueprintCallable, Category = "Components|ProceduralMesh")
	void SetMeshSectionVisible(int32 SectionIndex, bool bNewVisibility);

	/** Returns whether a particular section is currently visible */
	UFUNCTION(BlueprintCallable, Category = "Components|ProceduralMesh")
	bool IsMeshSectionVisible(int32 SectionIndex) const;

	/** Returns number of sections currently created for this component */
	UFUNCTION(BlueprintCallable, Category = "Components|ProceduralMesh")
	int32 GetNumSections() const;

	/** Add simple collision convex to this component */
	UFUNCTION(BlueprintCallable, Category = "Components|ProceduralMesh")
	void AddCollisionConvexMesh(TArray<FVector> ConvexVerts);

	/** Remove collision meshes from this component */
	UFUNCTION(BlueprintCallable, Category = "Components|ProceduralMesh")
	void ClearCollisionConvexMeshes();

	/** Function to replace _all_ simple collision in one go */
	void SetCollisionConvexMeshes(const TArray< TArray<FVector> >& ConvexMeshes);

	//~ Begin Interface_CollisionDataProvider Interface
	virtual bool GetPhysicsTriMeshData(struct FTriMeshCollisionData* CollisionData, bool InUseAllTriData) override;
	virtual bool GetTriMeshSizeEstimates(struct FTriMeshCollisionDataEstimates& OutTriMeshEstimates, bool bInUseAllTriData) const override;
	virtual bool ContainsPhysicsTriMeshData(bool InUseAllTriData) const override;
	virtual bool WantsNegXTriMesh() override{ return false; }
	//~ End Interface_CollisionDataProvider Interface



	/** 
	 *	Controls whether the complex (Per poly) geometry should be treated as 'simple' collision. 
	 *	Should be set to false if this component is going to be given simple collision and simulated.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Procedural Mesh")
	bool bUseComplexAsSimpleCollision = true;

	/**
	*	Controls whether the physics cooking should be done off the game thread. This should be used when collision geometry doesn't have to be immediately up to date (For example streaming in far away objects)
	*/
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Procedural Mesh")
	bool bUseAsyncCooking=true;

	/** Collision data */
	UPROPERTY(Instanced)
		class UBodySetup* ProcMeshBodySetup;

	/** Collision data */
	UPROPERTY(Instanced)
		class UBodySetup* SecondaryProcMeshBodySetup;

	bool BodySetupAlternator = false;

	UPROPERTY(Transient)
	class UModel* ModelCopy = nullptr;

	/** 
	 *	Get pointer to internal data for one section of this procedural mesh component. 
	 *	Note that pointer will becomes invalid if sections are added or removed.
	 */
	FGeoCProcMeshSection* GetProcMeshSection(int32 SectionIndex);

	/** Replace a section with new section geometry */
	void SetProcMeshSection(int32 SectionIndex, const FGeoCProcMeshSection& Section);

	//~ Begin UPrimitiveComponent Interface.
	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
	virtual class UBodySetup* GetBodySetup() override;
	virtual UMaterialInterface* GetMaterialFromCollisionFaceIndex(int32 FaceIndex, int32& SectionIndex) const override;
	virtual int32 GetNumMaterials() const override;
	//~ End UPrimitiveComponent Interface.

	void SetMaterialFromOwner(int32 ElementIndex, UMaterialInterface* Material);

	//~ Begin UMeshComponent Interface.
	/** Accesses the scene relevance information for the materials applied to the mesh. Valid from game thread only. */
	virtual FMaterialRelevance GetMaterialRelevance(ERHIFeatureLevel::Type InFeatureLevel) const;
	//~ End UMeshComponent Interface.

	//~ Begin UObject Interface
	virtual void Serialize(FArchive& Ar) override {UPrimitiveComponent::Serialize(Ar); };
	virtual void PostInitProperties() override;
	virtual void PostLoad() override;
#if WITH_EDITOR
	virtual void PostEditUndo() override { UPrimitiveComponent::PostEditUndo(); };
#endif // WITH_EDITOR
	virtual bool IsNameStableForNetworking() const override { return UPrimitiveComponent::IsNameStableForNetworking(); };
	static void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector) { UPrimitiveComponent::AddReferencedObjects(InThis, Collector); };
	//~ End UObject Interface.




	/////////////////////////////////////////////////////////////////////////
	///Paintable Foliage Start

	//~ Begin UModelComponent
#if WITH_EDITOR
	virtual bool GenerateElements(bool bBuildRenderData) override { return false; };
#endif


	virtual UMaterialInterface* GetMaterial(int32 ElementIndex) const override;
	virtual void SetMaterial(int32 ElementIndex, UMaterialInterface* Material) override;
	virtual void GetUsedMaterials(TArray<UMaterialInterface*>& OutMaterials, bool bGetDebugMaterials = false) const override;

#if 1//WITH_EDITORONLY_DATA

	virtual void CreateRenderState_Concurrent(FRegisterComponentContext* Context) override
	{return UPrimitiveComponent::CreateRenderState_Concurrent(Context);};

	virtual void DestroyRenderState_Concurrent()  override
	{ return UPrimitiveComponent::DestroyRenderState_Concurrent(); };

	virtual bool GetLightMapResolution(int32& Width, int32& Height) const override
	{ return UPrimitiveComponent::GetLightMapResolution(Width,Height); };

	virtual int32 GetStaticLightMapResolution() const override
	{ return UPrimitiveComponent::GetStaticLightMapResolution(); };

	virtual void GetLightAndShadowMapMemoryUsage(int32& LightMapMemoryUsage, int32& ShadowMapMemoryUsage) const override
	{ return UPrimitiveComponent::GetLightAndShadowMapMemoryUsage(LightMapMemoryUsage, ShadowMapMemoryUsage); };

	virtual bool ShouldRecreateProxyOnUpdateTransform() const override
	{ return UPrimitiveComponent::ShouldRecreateProxyOnUpdateTransform(); };

#if WITH_EDITOR
	virtual void GetStaticLightingInfo(FStaticLightingPrimitiveInfo& OutPrimitiveInfo, const TArray<ULightComponent*>& InRelevantLights, const FLightingBuildOptions& Options) override
	{
		return UPrimitiveComponent::GetStaticLightingInfo(OutPrimitiveInfo, InRelevantLights, Options);
	};
	virtual void AddMapBuildDataGUIDs(TSet<FGuid>& InGUIDs) const override
	{
		return UPrimitiveComponent::AddMapBuildDataGUIDs(InGUIDs);
	};
#endif
	virtual ELightMapInteractionType GetStaticLightingType() const override 
	{
		return UPrimitiveComponent::GetStaticLightingType();
	};
	virtual void GetStreamingRenderAssetInfo(FStreamingTextureLevelContext& LevelContext, TArray<FStreamingRenderAssetPrimitiveInfo>& OutStreamingRenderAssets) const override
	{
		return UPrimitiveComponent::GetStreamingRenderAssetInfo(LevelContext, OutStreamingRenderAssets);
	};
	virtual bool IsPrecomputedLightingValid() const override
	{
		return UPrimitiveComponent::IsPrecomputedLightingValid();
	};

	//~ Begin UActorComponent Interface.
	virtual void InvalidateLightingCacheDetailed(bool bInvalidateBuildEnqueuedLighting, bool bTranslationOnly) override
	{
		return UPrimitiveComponent::InvalidateLightingCacheDetailed(bInvalidateBuildEnqueuedLighting, bTranslationOnly);
	};
	virtual void PropagateLightingScenarioChange() override
	{
		return UPrimitiveComponent::PropagateLightingScenarioChange();
	};
	//~ End UActorComponent Interface.
	//~ End UModelComponent.

	/////////////////////////////////////////////////////////////////////////
	///Paintable Foliage End
#endif

	FBoxSphereBounds GetLocalBounds(){return LocalBounds;};

	void SetTargetHeight(float THeight){TargetHeight=THeight;};

	FORCEINLINE void SetLocationOnPhysicCookComplete(const FVector& Destination){ DestinationOnNextPhysicCook = Destination;}
	
	TSharedPtr<FSWShareableID, ESPMode::ThreadSafe> GetID() { return OwnerID; }
	inline void SetSWWorldVersion(TSharedPtr<FSWShareableID, ESPMode::ThreadSafe>& ID_)
	{
		OwnerID = ID_;
		ComponentID = MakeShared<FSWShareableID, ESPMode::ThreadSafe>();
	};

	TSharedPtr<FSWShareableVerticePositionBuffer, ESPMode::ThreadSafe> VerticesTemplate;
	TSharedPtr<FSWShareableIndexBuffer, ESPMode::ThreadSafe> TrianglesTemplate;

private:


	//~ Begin USceneComponent Interface.
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
	//~ Begin USceneComponent Interface.


	/** Update LocalBounds member from the local box of each section */
	void UpdateLocalBounds();
	/** Ensure ProcMeshBodySetup is allocated and configured */
	void CreateProcMeshBodySetup();
	/** Mark collision data as dirty, and re-create on instance if necessary */
	void UpdateCollision();
	/** Once async physics cook is done, create needed state */
	void FinishPhysicsAsyncCook(bool bSuccess, UBodySetup* FinishedBodySetup);

	/** Update Navigation data */
	void UpdateNavigation();

public:
	bool DoCustomNavigableGeometryExport(FNavigableGeometryExport& GeomExport) const final;

	bool HasPendingAsyncCollisionWork() {return AsyncBodySetupQueue.Num()>0;}


	void SetUsedPhysicalMaterial(TArray<TObjectPtr<UPhysicalMaterial>>&PhysicalMaterials);

protected:
		virtual void OnCreatePhysicsState() override;

private:
	/** Helper to create new body setup objects */
	UBodySetup* CreateBodySetupHelper();

	/** Array of sections of mesh */
	UPROPERTY(Transient)
	TArray<FGeoCProcMeshSection> ProcMeshSections;

	/** Convex shapes used for simple collision */
	UPROPERTY(Transient)
	TArray<FKConvexElem> CollisionConvexElems;

	/** Local space bounds of mesh */
	UPROPERTY(Transient)
	FBoxSphereBounds LocalBounds;

	UPROPERTY(Transient)
		bool UseCustomBounds;
	/** Local space bounds of mesh */
	UPROPERTY(Transient)
		FBoxSphereBounds LocalBoundsGeoC;

	UPROPERTY()
		float TargetHeight=0.f;

	/** Local space bounds of mesh */
	UPROPERTY(Transient)
		FVector DestinationOnNextPhysicCook;

	TArray<FConvexVolume> ViewsFrustums;
	
	/** Queue for async body setups that are being cooked */
	UPROPERTY(transient)
	TArray<UBodySetup*> AsyncBodySetupQueue;

	UPROPERTY(transient)
	bool DrawCollisionMesh=false;

	TSharedPtr<FSWShareableID, ESPMode::ThreadSafe> OwnerID;
	TSharedPtr<FSWShareableID, ESPMode::ThreadSafe> ComponentID;

	TArray<TSharedPtr<FSWShareableVerticePositionBuffer, ESPMode::ThreadSafe>> UpdatesReceivedDuringCompute;

	friend class FShaderWProceduralMeshSceneProxy;
	friend class FSWClipMapCollisionBuffersHolder;

};



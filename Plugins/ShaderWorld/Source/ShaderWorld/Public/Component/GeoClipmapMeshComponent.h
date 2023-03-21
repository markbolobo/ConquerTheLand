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
#include "Data/SWStructs.h"
#include "Rendering/FSWPatchVertexFactory.h"

#include "GeoClipmapMeshComponent.generated.h"


class FSWShareableID;
class FPrimitiveSceneProxy;
class FSWClipMapBuffersHolder;



struct FConvexVolume;

DECLARE_STATS_GROUP(TEXT("GeoClipProceduralMesh"), STATGROUP_GeoClipProceduralMesh, STATCAT_Advanced);



extern TGlobalResource< FSWClipMapBuffersHolder > GSWClipMapBufferHolder;
/**
*	Struct used to specify a tangent vector for a vertex
*	The Y tangent is computed from the cross product of the vertex normal (Tangent Z) and the TangentX member.
*/
USTRUCT(BlueprintType)
struct FGeoCProcMeshTangent
{
	GENERATED_BODY()
public:

	/** Direction of X tangent for this vertex */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Tangent)
	FVector TangentX;

	/** Bool that indicates whether we should flip the Y tangent when we compute it using cross product */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Tangent)
	bool bFlipTangentY;

	FGeoCProcMeshTangent()
		: TangentX(1.f, 0.f, 0.f)
		, bFlipTangentY(false)
	{}

	FGeoCProcMeshTangent(float X, float Y, float Z)
		: TangentX(X, Y, Z)
		, bFlipTangentY(false)
	{}

	FGeoCProcMeshTangent(FVector InTangentX, bool bInFlipTangentY)
		: TangentX(InTangentX)
		, bFlipTangentY(bInFlipTangentY)
	{}
};

/** Index Buffer */

class FSWDynamicMeshIndexBuffer32 : public FIndexBuffer
{
public:
	inline FSWDynamicMeshIndexBuffer32(){};
	inline FSWDynamicMeshIndexBuffer32(TSharedPtr<FSWShareableIndexBuffer>& Indices):IndicesPtr(Indices){}

	TSharedPtr<FSWShareableIndexBuffer, ESPMode::ThreadSafe> IndicesPtr;

	virtual void InitRHI() override;
	void UpdateRHI();
};


class FSWClipMapBuffersHolder : public FRenderResource
{
public:

	struct FProxyPrimitiveBuffers
	{
		TUniformBuffer<FSWPatchParameters> SWPatchUniformParameters;
		TUniformBuffer<FPrimitiveUniformShaderParameters> PrimitiveUniformBuffer;		
		
		FProxyPrimitiveBuffers()
		{
		};
		~FProxyPrimitiveBuffers() {};
	};
	/*
	 *	Each proxy section has its own persistent optimized index buffer
	 */
	struct FProxyShareableBuffers
	{
		/** Vertex buffer for this section */
		FStaticMeshVertexBuffers VertexBuffers;

		FSWPatchVertexFactoryMorphing VertexFactoryMorphing;
		FSWPatchVertexFactoryNoMorphing VertexFactoryNoMorphing;

		FSWDynamicMeshIndexBuffer32 IndexBufferOpti;

		FProxyShareableBuffers(ERHIFeatureLevel::Type InFeatureLevel, const TSharedPtr < FSWPatchUniformData, ESPMode::ThreadSafe >& UParams, TSharedPtr<FSWShareableIndexBuffer, ESPMode::ThreadSafe>& Indices)
			: VertexFactoryMorphing(InFeatureLevel, UParams)
			, VertexFactoryNoMorphing(InFeatureLevel, UParams)
			, IndexBufferOpti(Indices)
		{
			BeginInitResource(&IndexBufferOpti);
		}
		~FProxyShareableBuffers() {};
	};

	struct FDrawInstanceBuffers
	{	

		FStaticMeshVertexBuffers VertexBuffers;
		FSWDynamicMeshIndexBuffer32 IndexBuffer;
		FSWDynamicMeshIndexBuffer32 IndexBufferAlt;
		

		FDrawInstanceBuffers(TSharedPtr<FSWShareableIndexBuffer, ESPMode::ThreadSafe>& Indices, TSharedPtr<FSWShareableIndexBuffer, ESPMode::ThreadSafe>& IndicesAlt)
			:IndexBuffer(Indices)
			, IndexBufferAlt(IndicesAlt)
		{
			BeginInitResource(&IndexBuffer);
			BeginInitResource(&IndexBufferAlt);
		}
		~FDrawInstanceBuffers() {};
	};


	FSWClipMapBuffersHolder()
	{}

	void Cleanup()
	{
		FScopeLock ScopeLock(&MapLock);
		/*
		for (auto& Elem : ProxyPrimitiveBuffers)
		{
			if(Elem.Value.SWPatchUniformParameters.IsInitialized())
				Elem.Value.SWPatchUniformParameters.ReleaseResource();
				
		}
		ProxyPrimitiveBuffers.Empty();*/

		for (auto& Elem : ProxyShareableBuffers)
		{
			for (FProxyShareableBuffers* SharedProxy : (Elem.Value))
			{
				if (SharedProxy != nullptr)
				{
					SharedProxy->IndexBufferOpti.ReleaseResource();
					SharedProxy->VertexBuffers.PositionVertexBuffer.ReleaseResource();
					SharedProxy->VertexFactoryMorphing.ReleaseResource();
					SharedProxy->VertexFactoryNoMorphing.ReleaseResource();
					delete SharedProxy;
				}
			}
		}
		ProxyShareableBuffers.Empty();

		for (auto& Elem : SectionShareableBuffers)
		{
			for (FDrawInstanceBuffers* Section : (Elem.Value))
			{
				if (Section != nullptr)
				{
					Section->VertexBuffers.ColorVertexBuffer.ReleaseResource();
					Section->VertexBuffers.StaticMeshVertexBuffer.ReleaseResource();
					Section->IndexBuffer.ReleaseResource();
					Section->IndexBufferAlt.ReleaseResource();
					delete Section;
				}
			}
		}
		SectionShareableBuffers.Empty();
	}

	virtual void ReleaseRHI() override
	{
		FRenderResource::ReleaseRHI();
	}

	virtual void ReleaseDynamicRHI() override
	{		
		FRenderResource::ReleaseDynamicRHI();
	}

	virtual void ReleaseResource() override
	{
		FRenderResource::ReleaseResource();
	};

	virtual ~FSWClipMapBuffersHolder() override
	{
		Cleanup();
	}

	bool RegisterPrimitive(const TSharedPtr<FSWShareableID, ESPMode::ThreadSafe>& MeshCompVersion);

	/** Call once to register this extension. */
	bool RegisterExtension(ERHIFeatureLevel::Type InFeatureLevel, bool bUseMorphing, const TSharedPtr<FSWShareableID, ESPMode::ThreadSafe>& SWWorldVersion, const TSharedPtr<FSWShareableID, ESPMode::ThreadSafe>& MeshCompVersion, const int32 NumSection, UGeoClipmapMeshComponent* Component, const TSharedPtr < FSWPatchUniformData, ESPMode::ThreadSafe >& UParams);

	void DiscardBuffers(const TSharedPtr<FSWShareableID, ESPMode::ThreadSafe>& SWWorldVersion);

	void DiscardMeshComponentBuffers(const TSharedPtr<FSWShareableID, ESPMode::ThreadSafe>& MeshComponentVersion);

	void DiscardPrimitiveBuffers(const TSharedPtr<FSWShareableID, ESPMode::ThreadSafe>& MeshComponentVersion);

	FDrawInstanceBuffers* GetBuffersForSection(const TSharedPtr<FSWShareableID, ESPMode::ThreadSafe>& SWWorldVersion,const int32& Index)
	{
		FScopeLock ScopeLock(&MapLock);

		if(!SectionShareableBuffers.Contains(SWWorldVersion))
			return nullptr;

		if (Index>= (*SectionShareableBuffers.Find(SWWorldVersion)).Num())
			return nullptr;

		return (*SectionShareableBuffers.Find(SWWorldVersion))[Index];
	}

	FProxyShareableBuffers* GetSharedProxyBuffers(const TSharedPtr<FSWShareableID, ESPMode::ThreadSafe>& ComponentID, const int32& Index)
	{
		FScopeLock ScopeLock(&MapLock);

		if (!ProxyShareableBuffers.Contains(ComponentID))
			return nullptr;

		if (Index >= (*ProxyShareableBuffers.Find(ComponentID)).Num())
			return nullptr;

		return (*ProxyShareableBuffers.Find(ComponentID))[Index];
	}

	FProxyPrimitiveBuffers* GetPrimitiveBuffers(const TSharedPtr<FSWShareableID, ESPMode::ThreadSafe>& ComponentID)
	{
		FScopeLock ScopeLock(&MapLock);

		if (!ProxyPrimitiveBuffers.Contains(ComponentID))
		{
			//return &ProxyPrimitiveBuffers.Add(ComponentID);
			return nullptr;
		}
				

		return ProxyPrimitiveBuffers.Find(ComponentID);
	}

protected:

	FCriticalSection MapLock;

	TMap<TSharedPtr<FSWShareableID, ESPMode::ThreadSafe>,TArray < FDrawInstanceBuffers* >> SectionShareableBuffers;
	TMap<TSharedPtr<FSWShareableID, ESPMode::ThreadSafe>, TArray < FProxyShareableBuffers*> > ProxyShareableBuffers;

	TMap<TSharedPtr<FSWShareableID, ESPMode::ThreadSafe>, FProxyPrimitiveBuffers > ProxyPrimitiveBuffers;
};



/** One vertex for the procedural mesh, used for storing data internally */
USTRUCT(BlueprintType)
struct FGeoCProcMeshVertex
{
	GENERATED_BODY()
public:

	/** Vertex position */
	UPROPERTY(Transient)//EditAnywhere, BlueprintReadWrite, Category = Vertex)
	FVector3f Position;

	/** Vertex normal */
	UPROPERTY(Transient)//EditAnywhere, BlueprintReadWrite, Category = Vertex)
	FVector Normal;

	/** Vertex tangent */
	UPROPERTY(Transient)//EditAnywhere, BlueprintReadWrite, Category = Vertex)
	FGeoCProcMeshTangent Tangent;

	/** Vertex color */
	UPROPERTY(Transient)//EditAnywhere, BlueprintReadWrite, Category = Vertex)
	FColor Color;

	/** Vertex texture co-ordinate */
	UPROPERTY(Transient)//EditAnywhere, BlueprintReadWrite, Category = Vertex)
	FVector2f UV0;

	/** Vertex texture co-ordinate */
	UPROPERTY(Transient)//EditAnywhere, BlueprintReadWrite, Category = Vertex)
	FVector2f UV1;

	/** Vertex texture co-ordinate */
	UPROPERTY(Transient)//EditAnywhere, BlueprintReadWrite, Category = Vertex)
	FVector2f UV2;

	/** Vertex texture co-ordinate */
	UPROPERTY(Transient)//EditAnywhere, BlueprintReadWrite, Category = Vertex)
	FVector2f UV3;


	FGeoCProcMeshVertex()
		: Position(0.f, 0.f, 0.f)
		, Normal(0.f, 0.f, 1.f)
		, Tangent(FVector(1.f, 0.f, 0.f), false)
		, Color(255, 255, 255)
		, UV0(0.f, 0.f)
		, UV1(0.f, 0.f)
		, UV2(0.f, 0.f)
		, UV3(0.f, 0.f)
	{}
};

/** One section of the procedural mesh. Each material has its own section. */
USTRUCT()
struct FGeoCProcMeshSection
{
	GENERATED_BODY()
public:

	TSharedPtr<FSWShareableVerticePositionBuffer, ESPMode::ThreadSafe> PositionBuffer;

	/** Vertex buffer for this section */
	UPROPERTY()
	TArray<FGeoCProcMeshVertex> ProcVertexBuffer;

	/** Index buffer for this section */
	TSharedPtr<FSWShareableIndexBuffer, ESPMode::ThreadSafe> IndexBuffer;
	TSharedPtr<FSWShareableIndexBuffer, ESPMode::ThreadSafe> IndexBufferAlt;

	UPROPERTY()
	UMaterialInterface* Material=nullptr;

	/** Physical material indices */
	UPROPERTY()
	TArray<uint16> MaterialIndices;
	/** Local bounding box of section */
	UPROPERTY()
	FBox SectionLocalBox;

	/** Should we build collision data for triangles in this section */
	UPROPERTY()
	bool bEnableCollision;

	/** Should we display this section */
	UPROPERTY()
	bool bSectionVisible;

	FGeoCProcMeshSection()
		: SectionLocalBox(ForceInit)
		, bEnableCollision(false)
		, bSectionVisible(true)
	{}

	/** Reset this section, clear all mesh info. */
	void Reset()
	{
		ProcVertexBuffer.Empty();
		//ProcIndexBuffer.Empty();
		SectionLocalBox.Init();
		bEnableCollision = false;
		bSectionVisible = true;
	}
};

/**
*	Component that allows you to specify custom triangle mesh geometry
*	Beware! This feature is experimental and may be substantially changed in future releases.
*/
UCLASS(hidecategories = (Object, LOD), meta = (BlueprintSpawnableComponent), ClassGroup = Rendering)
class SHADERWORLD_API UGeoClipmapMeshComponent : public UMeshComponent
{
	GENERATED_BODY()
public:

	UGeoClipmapMeshComponent(const FObjectInitializer& ObjectInitializer);

	void CreateMeshSection(int32 SectionIndex, const TArray<FVector3f>& Vertices, const TSharedPtr<FSWShareableIndexBuffer>& Triangles, const TSharedPtr<FSWShareableIndexBuffer>& TrianglesAlt, const TArray<FVector>& Normals, const TArray<FVector2f>& UV0, const TArray<FVector2f>& UV1, const TArray<FVector2f>& UV2, const TArray<FVector2f>& UV3, const TArray<FColor>& VertexColors, const TArray<FGeoCProcMeshTangent>& Tangents, bool bCreateCollision);

	void UpdateCustomBounds(FBoxSphereBounds Newbound);

	void UpdateSectionTopology(int32 SectionIndex, int N, float GridScaling, UTextureRenderTarget2D* HeightMap);

	void UpdatePatchLocation(const FVector& NewPatchLocation);

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

	/** Collision data */
	UPROPERTY(Instanced)
	class UBodySetup* ProcMeshBodySetup;

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
	//~ End UPrimitiveComponent Interface.

	//~ Begin UMeshComponent Interface.
	virtual void SetMaterial(int32 ElementIndex, UMaterialInterface* Material) override;
	virtual int32 GetNumMaterials() const override;
	//~ End UMeshComponent Interface.

	//~ Begin UObject Interface
	virtual void PostLoad() override;
	//~ End UObject Interface.

	FBoxSphereBounds GetLocalBounds(){return LocalBounds;};

	void SetTargetHeight(float THeight){TargetHeight=THeight;};

	TArray<FConvexVolume> GetViewsFrustums();	

	void SetPatchData(UTextureRenderTarget2D* HeightMap_, UTextureRenderTarget2D* NormalMap_,FVector PatchLocation_, float PatchFullSize_, float SWHeightScale_, float SmoothLODRange_
		, float MeshScale_, float N_, float LocalGridScaling_, float CacheRes_, bool UseMorphing);	

	TSharedPtr < FSWPatchUniformData, ESPMode::ThreadSafe > GetPatchData(){ return PatchData; };

	void UpdatePatchDataLODSmoothTransition(float NewTransition);

	inline bool GetUseDynamicTopology() { return DynamicTopology; };

	inline void SetUseDynamicTopology(bool DynamicTopology_) { DynamicTopology = DynamicTopology_; };

	inline bool GetUseWPO() { return EvaluateWorldPositionOffset; };

	inline void SetUseWPO(bool WPO_) { EvaluateWorldPositionOffset = WPO_; };

	TSharedPtr<FSWShareableID, ESPMode::ThreadSafe> GetSWWorldVersion(){return OwnerID;}
	TSharedPtr<FSWShareableID, ESPMode::ThreadSafe> GetComponentVersion() { return ComponentID; }

	inline void SetSWWorldVersion(TSharedPtr<FSWShareableID, ESPMode::ThreadSafe>& ID_)
	{
		OwnerID = ID_;

		if (ComponentID.IsValid())
		{
			ENQUEUE_RENDER_COMMAND(ClearStaticBuffers)(
				[ID = ComponentID](FRHICommandListImmediate& RHICmdList)
				{
					check(IsInRenderingThread());
					GSWClipMapBufferHolder.DiscardMeshComponentBuffers(ID);
				});


			ComponentID.Reset();
		}
		ComponentID = MakeShared<FSWShareableID, ESPMode::ThreadSafe>();
	};

private:


	//~ Begin USceneComponent Interface.
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;	
	//~ Begin USceneComponent Interface.

	UBodySetup* CreateBodySetupHelper();
	void CreateProcMeshBodySetup();


	/** Update LocalBounds member from the local box of each section */
	void UpdateLocalBounds();

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
	
	/** Queue for async body setups that are being cooked */
	UPROPERTY(transient)
	TArray<UBodySetup*> AsyncBodySetupQueue;

	TArray<FConvexVolume> ViewsFrustums;

	bool DynamicTopology = true;
	bool EvaluateWorldPositionOffset = false;

	TSharedPtr < FSWPatchUniformData, ESPMode::ThreadSafe > PatchData;

	TSharedPtr<FSWShareableID, ESPMode::ThreadSafe> OwnerID;
	TSharedPtr<FSWShareableID, ESPMode::ThreadSafe> ComponentID;
	
	friend class FGeoClipProceduralMeshSceneProxy;
	friend class FSWClipMapBuffersHolder;
	};



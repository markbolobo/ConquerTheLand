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

#include "Data/SWEnums.h"
#include "Templates/SharedPointer.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Component/SWHISMComponent.h"
#include "Landscape/Classes/LandscapeGrassType.h"
#include "Interfaces/Interface_CollisionDataProvider.h"
#include "FoliageType_InstancedStaticMesh.h"
#include "Engine/StaticMesh.h"
#include "HAL/ThreadSafeBool.h"

#include "SWCacheManager.h"

#include "StructView.h"
#include "InstancedStruct.h"
#include "SWStructs.generated.h"
/**
 * 
 */

class UGeoClipmapMeshComponent;
class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UTextureRenderTarget2D;
class UShaderWorldCollisionComponent;
class UFoliageType;

struct FSWQuadElement
{
	uint64 QuadID;
};

USTRUCT()
struct FSWQuadDataElement
{
	GENERATED_BODY()
};

USTRUCT()
struct FSWTexture2DCache
{
	GENERATED_BODY()

	UPROPERTY(Transient)
		int32 Dimension = 0;

	UPROPERTY(Transient)
		TEnumAsByte<enum TextureFilter> LayerFiltering = TextureFilter::TF_Nearest;

	UPROPERTY(Transient)
		TEnumAsByte<ETextureRenderTargetFormat> Format = ETextureRenderTargetFormat::RTF_RGBA8;

	UPROPERTY(Transient)
		TArray< UTextureRenderTarget2D* > TextureCache;
};

USTRUCT()
struct FSWTexture2DCacheGroup
{
	GENERATED_BODY()

	UPROPERTY(Transient)
		TMap<FString, FSWTexture2DCache > PerLODCaches;

	UPROPERTY()
		uint32 CacheSizeMeters = 32;

	UPROPERTY()
		uint32 RingCount = 1;

	UPROPERTY(Transient)
		TSet<FIntVector> All_Cameras;

	UPROPERTY(Transient)
		double CamerasUpdateTime = -0.5;

	FSWRingCacheManager CacheManager = {1};

	void UpdateReferencePoints(UWorld* World, double& CurrentTime, TArray<FVector>& ReferenceSources);
	void ReleaseOutOfRange();
	void GenerateWithinRange(UWorld* World);

	void DrawDebugPartition(UWorld* World);

	bool ManagerInitiated = false;

	friend FORCEINLINE uint32 GetTypeHash(const FSWTexture2DCacheGroup& MidiTime)
	{
		const uint32 Hash = (MidiTime.RingCount & 63u)<<25 | (MidiTime.CacheSizeMeters & 33554431u);
		return Hash;
	}

	bool operator ==(const FSWTexture2DCacheGroup& Rhs) const{
		return (CacheSizeMeters == Rhs.CacheSizeMeters) && (RingCount == Rhs.RingCount);
	}
};



USTRUCT()
struct FClipMapPerLODCaches
{
	GENERATED_BODY()

	/*
	 * All the various caches we can have on a single LOD
	 */
	UPROPERTY(Transient)
	TArray< FSWTexture2DCacheGroup > PerLODCacheGroup;


	/*
	UPROPERTY(Transient)
	TMap<FIntVector, int32 > CacheLayout;

	UPROPERTY(Transient)
	TArray<FSWDataCacheElement> CacheElem;

	UPROPERTY(Transient)
		TArray<int> AvailableCacheElem;
	UPROPERTY(Transient)
		TArray<int> UsedCacheElem;

	FSWDataCacheElement& GetACacheElem();
	void ReleaseCacheElem(int ID);
	void CleanUp();

	~FClipMapPerLODCaches();*/
};



 /**
 * A ring Element, support either a procedural grid mesh, or a set of instanced meshes
 *
 */
USTRUCT()
struct FClipMapMeshElement
{
	GENERATED_BODY()

	UPROPERTY(Transient)
		UGeoClipmapMeshComponent* Mesh = nullptr;

	UPROPERTY(Transient)
		FClipMapPerLODCaches TransientCaches;

	/**
	* Each clipmap ring has its own real world distance between vertices represented by GridSpacing value
	*/
	UPROPERTY(Transient)
		int32 GridSpacing = 1;

	/**
	* Level	0 is the largest ring , levels are scaling inversed proportionality to LODs
	*/
	UPROPERTY(Transient)
		int32 Level = 0;

	/**
	* Center of the ring
	*/
	UPROPERTY(Transient)
		FIntVector Location = FIntVector(0.f, 0.f, 0.f);

	/**
	* GeoClipmap L-shape configuration, to compensate for child clipmap offset
	*/
	UPROPERTY(Transient)
		EClipMapInteriorConfig Config = EClipMapInteriorConfig::BotLeft;
	/**
	* Material applied to the ring, keeping a ptr to update its "RingLocation" parameter
	*/
	UPROPERTY(Transient)
		UMaterialInstanceDynamic* MatDyn = nullptr;

	/**
	* If using cache, keeping a ptr to this ring Heightmap
	*/
	UPROPERTY(Transient)
		TObjectPtr < UTextureRenderTarget2D> HeightMap = nullptr;

	/**
	* If using SegmentedUpdates, store temporarily the output of the computation here, keeping a ptr to this ring Heightmap
	*/
	UPROPERTY(Transient)
		UTextureRenderTarget2D* HeightMap_Segmented = nullptr;


	/**
	* If using cache, keeping a ptr to this ring Normalmap
	*/
	UPROPERTY(Transient)
		UTextureRenderTarget2D* NormalMap = nullptr;

	/**
	* If using cache, keeping a ptr to this ring Normalmap
	*/
	UPROPERTY(Transient)
		UTextureRenderTarget2D* NormalMap_Segmented = nullptr;

	/**
	* This material is responsible for generating the cache, updating the Heightmap Render target
	* keeping a ptr to update its "RingLocation" parameter
	*/
	UPROPERTY(Transient)
		TObjectPtr<UMaterialInstanceDynamic> CacheMatDyn = nullptr;

	/**
	* Validated layers names for this ring (layers with no name or no material defined were skipped at initialization)
	*/
	UPROPERTY(Transient)
		TArray<FName> LandLayers_names;
	/**
	* Validated layers Render targets
	*/
	UPROPERTY(Transient)
		TArray<UTextureRenderTarget2D*> LandLayers;

	/**
	* Validated layers Render targets
	*/
	UPROPERTY(Transient)
		TArray<UTextureRenderTarget2D*> LandLayers_Segmented;

	/**
	* Validated layers require Parent layer
	*/
	UPROPERTY(Transient)
		TArray<bool> LandLayers_NeedParent;
	/**
	* Validated layers Material to generate the given layer, keeping a ptr to update its "RingLocation" parameter
	*/
	UPROPERTY(Transient)
		TArray<UMaterialInstanceDynamic*> LayerMatDyn;

	/**
	* When binding two worlds together (land and ocean), keeping a pointer to the last heightmap we read from the Source world
	* if we're updating a ring with new Source data but the heightmap ptr is identical, we're just updating the "Ext_RingLocation" in the ring material (MatDyn)
	* instead of updating the external heightmap, normalmap, gridscaling, etc... necessary for generating UVs from world coordinate for this external Heightmap/Normalmap
	*/
	UPROPERTY(Transient)
		UTextureRenderTarget2D* HeightMapFromLastSourceElement = nullptr;


	/**
	* Instead of directly using 'Issectionvisible' we're using a simple array of visibility to allow the Instanced Mesh Ground workflow as well.
	*/
	UPROPERTY(Transient)
		TArray<bool> SectionVisibility;


	UPROPERTY(Transient)
		TArray<bool> SectionVisibility_SegmentedCache;


	


	double LatestUpdateTime = 0.0;
	double UpdateDelay = 0.0;
	bool DrawingThisFrame = false;
	uint8 NeedComponentLocationUpdate = 0;
	FIntVector LocationLastMove;

	bool IsSectionVisible(int SectionID, bool ToSegmentedCacheIfEnabled = false);
	void SetSectionVisible(int SectionID, bool NewVisibility, bool ToSegmentedCacheIfEnabled = false);
};

USTRUCT(BlueprintType)
struct FShaderWorldHeightReadback
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "")
		FVector Position = FVector(0.f, 0.f, 0.f);
};

USTRUCT(BlueprintType)
struct FClipMapLayer
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "")
		FString LayerName = "LayerName";
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "")
		TObjectPtr < UMaterialInterface> MaterialToGenerateLayer = nullptr;
	/*
	 * Use Case:
	 * Nearest : Store ID - prevent any interpolation of data in between texture element.
	 * Bi/Tri-linear : Store Blend Weights - smoothly blend over texture elements.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "")
		TEnumAsByte<enum TextureFilter> LayerFiltering = TextureFilter::TF_Nearest;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "")
		bool GetParentCache = false;
};

USTRUCT(BlueprintType)
struct FWorldSeeds
{
	GENERATED_BODY()

		UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Seed")
		FName SeedName = "SeedName";
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Seed")
		float Seed = 1337.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Seed")
		bool Randomize = false;
	UPROPERTY(EditAnywhere, Category = "Seed", meta = (EditCondition = Randomize))
		FFloatInterval SeedRange = FFloatInterval(1337.f, 1337.f);
};

USTRUCT(BlueprintType)
struct SHADERWORLD_API FSWSeeds
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "")
	FName SeedName = "SeedName";
};


USTRUCT(BlueprintType)
struct SHADERWORLD_API FScalarSeed : public FSWSeeds
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "")
	float Value = 0.f;
};

USTRUCT(BlueprintType)
struct SHADERWORLD_API FLinearColorSeed : public FSWSeeds
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "")
		FLinearColor Value = FLinearColor::Black;
};

USTRUCT(BlueprintType)
struct SHADERWORLD_API FTextureSeed : public FSWSeeds
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "")
		UTexture* Value = nullptr;
};

USTRUCT(BlueprintType)
struct SHADERWORLD_API FSWBagOfSeeds
{
	GENERATED_BODY()
	
	UPROPERTY(EditAnywhere, Category = Foo, meta = (BaseStruct = "/Script/ShaderWorld.SWSeeds", ExcludeBaseStruct))
		TArray<FInstancedStruct> SeedsArray;
	
};


class FSWShareableID
{
public:
	
};

class FSWShareableSamplePoints
{
public:
	FSWShareableSamplePoints() {};
	~FSWShareableSamplePoints() {};

	TArray<float> PositionsXY;
};

class FSWShareableVerticePositionBuffer
{
public:
	TArray<FVector> Positions;
	//Dirty but prevent further loops of copying over
	TArray<FVector3f> Positions3f;

	//Optional
	TArray<uint16> MaterialIndices;

	FBox Bound;
};

class FSWShareableIndexBuffer
{
public:
	TArray<uint32> Indices;

	TArray < FTriIndices > Triangles_CollisionOnly;
};

class FSWInstancedStaticMeshInstanceDatas
{
public:
	FSWInstancedStaticMeshInstanceDatas() = default;
	~FSWInstancedStaticMeshInstanceDatas() = default;

	FCriticalSection DataLock;

	TArray<FInstancedStaticMeshInstanceData> PerInstanceSMData;
};


class FSWInstanceIndexesInHISM
{
public:
	FSWInstanceIndexesInHISM() = default;
	~FSWInstanceIndexesInHISM() = default;

	FThreadSafeBool Initiated;

	TArray<FInstanceIndexes> InstancesIndexes;

};

class FSWSpawnableTransforms
{
public:
	FSWSpawnableTransforms() = default;
	~FSWSpawnableTransforms() = default;

	TArray<TArray<FInstancedStaticMeshInstanceData>> Transforms;

};

class FSWReadBackCompletion
{
public:
	FSWReadBackCompletion() = default;
	~FSWReadBackCompletion() = default;

	FThreadSafeBool bProcessed;
};

class FSWColorRead
{
	public:
		FSWColorRead(){};
		~FSWColorRead() {};

	TArray<FColor> ReadData;
};

class FSWShareableIndexes
{
public:
	FSWShareableIndexes() {};
	~FSWShareableIndexes() {};

	TArray<int32> Indexes;
};

class FSWRedirectionIndexes
{
public:
	FSWRedirectionIndexes() {};
	~FSWRedirectionIndexes() {};

	TArray<int32> Redirection_Indexes;
	TArray<int32> ColIndexToSpawnableIndex;
	TArray<int32> ColIndexToVarietyIndex;
};

class FSWShareableInstanceIndexes
{
public:
	FSWShareableInstanceIndexes() {};
	~FSWShareableInstanceIndexes() {};

	TArray < TArray<int32> > InstancesIndexes;
};



class FSWShareableIndexesCompletion
{
public:
	FSWShareableIndexesCompletion() {};
	~FSWShareableIndexesCompletion() {};

	FThreadSafeBool bProcessingCompleted;
	//TAtomic<bool> bProcessingCompleted;
	TArray<int32> Indexes;
};

class FSWCollisionMeshElemData;



class FSWCollisionManagementShareableData
{
public:
	inline FSWCollisionManagementShareableData() {}
	inline FSWCollisionManagementShareableData(const float& Res, const int32& Vert)
		:CollisionResolution(Res)
		,VerticePerPatch(Vert)
		,CollisionMeshToCreate(0)
	{
		CollisionMeshData.Empty();
		AvailableCollisionMesh.Empty();
		UsedCollisionMesh.Empty();
		CollisionReadToProcess.Empty();
		GroundCollisionLayout.Empty();
		MultipleCamera.Empty();
		LocRefs.Empty();
		CollisionMeshToUpdate.Empty();
		CollisionMeshToRenameMoveUpdate.Empty();
	};
	inline ~FSWCollisionManagementShareableData() {};

	TArray<FSWCollisionMeshElemData> CollisionMeshData;

	float CollisionResolution;
	int32 VerticePerPatch;

	TArray<int32> AvailableCollisionMesh;
	TArray<int32> UsedCollisionMesh;
	TArray<int32> CollisionReadToProcess;
	TMap<FIntVector, int32> GroundCollisionLayout;

	TMap<FIntVector, FVector> MultipleCamera;
	TMap<FIntVector, FVector> LocRefs;

	void ReleaseCollisionMesh(int32 ID){ AvailableCollisionMesh.Add(ID); }


	//
	TArray <int32> CollisionMeshToUpdate;

	TArray <int32> CollisionMeshToRenameMoveUpdate;

	int32 CollisionMeshToCreate=0;	
};


USTRUCT()
struct FCollisionMeshElement
{
	GENERATED_BODY()

		/**
		* Collisions are handled by a set of traditional procedural meshes, masked to the viewer
		*/
		UPROPERTY(Transient)
		UShaderWorldCollisionComponent* Mesh = nullptr;

	/**
	* The Collision rendertarget holding the computed height data for a given vertice
	*/
	UPROPERTY(Transient)
		UTextureRenderTarget2D* CollisionRT = nullptr;

	UPROPERTY(Transient)
		UTextureRenderTarget2D* CollisionRT_Duplicate = nullptr;

	UPROPERTY(Transient)
		UMaterialInstanceDynamic* DynCollisionCompute = nullptr;

	/**
	* Location of the collision patch
	*/
	UPROPERTY(Transient)
		FIntVector Location = FIntVector(0.f, 0.f, 0.f);
	UPROPERTY(Transient)
		FVector MeshLocation = FVector(0.f, 0.f, 0.f);
	/**
	* IDs are indexes within the collision mesh pool
	*/
	UPROPERTY(Transient)
		int32 ID = -1;
	/**
	* Stores the read back data computed from the AShaderWorldActor::CollisionMat_HeightRead applied to CollisionRT
	*/
	TSharedPtr<FSWColorRead, ESPMode::ThreadSafe> HeightData;

	TSharedPtr < FThreadSafeBool, ESPMode::ThreadSafe> ReadBackCompletion;

	inline bool operator==(const FCollisionMeshElement& Other) const
	{
		return (Other.Mesh == Mesh) && (Other.CollisionRT == CollisionRT);
	}
};

class FSWCollisionMeshElemData : public TSharedFromThis<FSWCollisionMeshElemData>
{
public:
	FSWCollisionMeshElemData() {};
	~FSWCollisionMeshElemData() {};

	FSWCollisionMeshElemData(const FCollisionMeshElement& El) :
		Location(El.Location),
		MeshLocation(El.MeshLocation),
		ID(El.ID),
		HeightData(El.HeightData)
	{};

	/**
	* Location of the collision patch
	*/
	FIntVector Location = FIntVector(0.f, 0.f, 0.f);

	FVector MeshLocation = FVector(0.f, 0.f, 0.f);
	/**
	* IDs are indexes within the collision mesh pool
	*/
	int32 ID = -1;
	/**
	* Stores the read back data computed from the AShaderWorldActor::CollisionMat_HeightRead applied to CollisionRT
	*/
	TSharedPtr<FSWColorRead, ESPMode::ThreadSafe> HeightData;

	inline bool operator==(const FSWCollisionMeshElemData& Other) const
	{
		return (Other.ID == ID)&&(ID>=0);
	}
};

/**
* Just a simple struct to nest indexes within a TArray, each Mesh/Actor variety has its own index
*/
USTRUCT()
struct FInstanceIndexes
{
	GENERATED_BODY()

		UPROPERTY(Transient)
		TArray<int> InstancesIndexes;
};


USTRUCT()
struct FSpawnableMeshSettings
{
	GENERATED_BODY()

	UPROPERTY()
		TArray<FName> ComponentTags;

	UPROPERTY()
		uint8 bHasPerInstanceHitProxies : 1;
	UPROPERTY()
		uint8 CastShadow : 1;
	UPROPERTY()
		uint8 CastDynamicShadow : 1;
	UPROPERTY()
		uint8 CastStaticShadow : 1;
	UPROPERTY()
		int32 TranslucencySortPriority = 0;
	UPROPERTY()
		uint8 AffectDynamicIndirectLighting;
	UPROPERTY()
		uint8 AffectDistanceFieldLighting;
	UPROPERTY()
		uint8 CastShadowAsTwoSided;
	UPROPERTY()
		uint8 ReceivesDecals;
	UPROPERTY()
		uint8 UseAsOccluder;
	UPROPERTY()
		uint8 EnableDensityScaling;
	UPROPERTY()
		float CurrentDensityScaling;
	UPROPERTY()
		uint8 CollisionEnabled;

	UPROPERTY()
		float AlignMaxAngle = 0.f;

	UPROPERTY()
		FFloatInterval AltitudeRange;
	UPROPERTY()
		FFloatInterval VerticalOffsetRange;
	UPROPERTY()
		FFloatInterval GroundSlopeAngle;

	UPROPERTY()
		FInt32Interval CullDistance;

	UPROPERTY()
		int32 FoliageRandomSeed = 1337;
	UPROPERTY()
		TEnumAsByte<EComponentMobility::Type> Mobility;
	UPROPERTY()
		TEnumAsByte<ECollisionEnabled::Type> CollisionType;


	bool bCanAffectNavigation = false;
	bool bUseTranslatedSpace = false;
	bool bAlwaysCreatePhysicsState = false;

	UPROPERTY()
		TObjectPtr<UStaticMesh> Mesh;

	void Clear()
	{
		
	}
};


USTRUCT()
struct FSpawnableMeshProximityCollisionElement
{
	GENERATED_BODY()

	/**
	* We're computing assets transforms within a grid around the view point, that could be interpreted has the
	*/
	UPROPERTY(Transient)
	FIntVector Location = FIntVector(0.f, 0.f, 0.f);
	/**
	* IDs are indexes within the mesh Collision element pool
	*/
	UPROPERTY(Transient)
		int32 ID = -1;

	//UPROPERTY(Transient)
	//	TArray<FInstanceIndexes> InstancesIndexes;

	TSharedPtr < FSWInstanceIndexesInHISM, ESPMode::ThreadSafe> InstancesIndexes = nullptr;

	/**
	* Index Offset for each Mesh/actor variety computed on this element within the owning FSpawnableMesh HIM_Mesh/Spawned_Actors
	*/
	UPROPERTY(Transient)
		TArray<int> InstanceOffset;

	UPROPERTY(Transient)
		TArray<int32> OffsetOfSegmentedUpdate;
};


class FSWSpawnableRequirements
{
public:
	FSWSpawnableRequirements(){};
	~FSWSpawnableRequirements() {};

	UTextureRenderTarget2D* Heightmap = nullptr;
	UTextureRenderTarget2D* Normalmap = nullptr;

	UTexture2D* NoiseT = nullptr;

	uint32 N = 511;
	uint8 AlignToSlope = 0;
	float AlignToSlopeOffset = 0.f;
	float DX_Status = 1;
	float LocalGridScaling = 1.f;
	float AlignMaxAngle = 60.f;
	FFloatInterval AltitudeRange;
	FFloatInterval VerticalOffsetRange;
	FFloatInterval ScaleRange;
	FFloatInterval GroundSlopeAngle;
	float MeshScale = 1.f;
	uint32 RT_Dim = 20;
	FVector RingLocation = FVector(0);
	FVector MeshLocation = FVector(0);

	UTextureRenderTarget2D* Density = nullptr;
	UTextureRenderTarget2D* Transforms = nullptr;

	FSWSpawnableRequirements(UTextureRenderTarget2D* InHeightmap, UTextureRenderTarget2D* InNormalmap, UTexture2D* InNoiseT, uint32 InN	,uint8 In_AlignToSlope , float In_AlignToSlopeOffset, float InDX_Status	, float InLocalGridScaling	, float InAlignMaxAngle	, FFloatInterval& InAltitudeRange	, FFloatInterval& InVerticalOffsetRange	, FFloatInterval& InScaleRange
		, FFloatInterval& InGroundSlopeAngle		, float InMeshScale	, uint32 InRT_Dim	, FVector InRingLocation	, FVector InMeshLocation	, UTextureRenderTarget2D* InDensity	, UTextureRenderTarget2D* InTransforms)
		: Heightmap(InHeightmap)
		, Normalmap(InNormalmap)
		, NoiseT(InNoiseT)
		, N(MoveTemp(InN))
		, AlignToSlope(In_AlignToSlope)
		, AlignToSlopeOffset(MoveTemp(In_AlignToSlopeOffset))
		, DX_Status(MoveTemp(InDX_Status))
		, LocalGridScaling(MoveTemp(InLocalGridScaling))
		, AlignMaxAngle(MoveTemp(InAlignMaxAngle))
		, AltitudeRange(InAltitudeRange)
		, VerticalOffsetRange(InVerticalOffsetRange)
		, ScaleRange(InScaleRange)
		, GroundSlopeAngle(InGroundSlopeAngle)
		, MeshScale(MoveTemp(InMeshScale))
		, RT_Dim(MoveTemp(InRT_Dim))
		, RingLocation(MoveTemp(InRingLocation))
		, MeshLocation(MoveTemp(InMeshLocation))
		, Density(InDensity)
		, Transforms(InTransforms)
	{};
};

/**
* This is a computation grid element for Spawnables, managed by FSpawnableMesh
*/
USTRUCT()
struct FSpawnableMeshElement
{
	GENERATED_BODY()

	/*
	 * Moving HISM to MeshElement START
	 */
	
	UPROPERTY(Transient)
	double AllTreesRebuildTime = 0.0;

	UPROPERTY(Transient)
		USWHISMComponent* HIM_Mesh=nullptr;
	UPROPERTY(Transient)
		double HIM_Mesh_TreeRebuild_Time = 0.0;
	

	/*
	 * Moving HISM to MeshElement END
	 */


		UPROPERTY(Transient)
		bool ComputeLaunched = false;

	UPROPERTY(Transient)
		UTextureRenderTarget2D* SpawnDensity = nullptr;
	UPROPERTY(Transient)
		UTextureRenderTarget2D* SpawnTransforms = nullptr;


	/**
	* Dynamic material used to computed the assets transforms
	*/
	UPROPERTY(Transient)
		UMaterialInstanceDynamic* ComputeSpawnTransformDyn = nullptr;

	float LocalGridScaling_LatestC = 1.f;
	UTextureRenderTarget2D* HeightMap_LatestC = nullptr;
	UTextureRenderTarget2D* NormalMap_LatestC = nullptr;

	FVector RingLocation_LatestC;
	FVector MeshLocation_latestC;

	/**
	* We're computing assets transforms within a grid around the view point, that could be interpreted has the
	*/
	UPROPERTY(Transient)
		FIntVector Location = FIntVector(0, 0, 0);
	/**
	* IDs are indexes within the mesh element pool
	*/
	UPROPERTY(Transient)
		int32 ID = -1;

	UPROPERTY(Transient)
		int32 LOD_usedLastUpdate = -1;


	TSharedPtr < FSWColorRead, ESPMode::ThreadSafe> SpawnData;

	TSharedPtr < FThreadSafeBool, ESPMode::ThreadSafe> ReadBackCompletion;

	TSharedPtr < FSWSpawnableTransforms, ESPMode::ThreadSafe> InstancesT;

	TSharedPtr < FSWInstanceIndexesInHISM, ESPMode::ThreadSafe> InstancesIndexes;

	//UPROPERTY(Transient)
	//	TArray<FInstanceIndexes> InstancesIndexes;

	/**
	* Index Offset for each Mesh/actor variety computed on this element within the owning FSpawnableMesh HIM_Mesh/Spawned_Actors
	*/
	UPROPERTY(Transient)
		TArray<int> InstanceOffset;

	UPROPERTY(Transient)
		TArray<int32> OffsetOfSegmentedUpdate;


	UPROPERTY(Transient)
		int32 Collision_Mesh_ID = -1;

	UPROPERTY(Transient)
		bool NextUpdateIsAPositionAdjustement = false;
};

class AShaderWorldActor;

struct FSWBiom;
/**
* Hold the ptr to each spawned actors, elements can be nullptr if a spawning wasn't necessary
*/
USTRUCT()
struct FSpawnedActorList
{
	GENERATED_BODY()

		UPROPERTY(Transient)
		TArray<AActor*> SpawnedActors;
};

/**
* The spawnable system is tied to the cache system, if cache is disabled, no asset will be spawned
*/
USTRUCT(BlueprintType)
struct FSpawnableMesh
{
	GENERATED_BODY()


	/**
	* Spawn instanced meshes or actors
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MeshToSpawn")
		ESpawnableType SpawnType = ESpawnableType::Undefined;

	UPROPERTY()
		ESpawnableType SpawnType_LastUpdate = ESpawnableType::Undefined;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MeshToSpawn", meta = (EditCondition = "SpawnType==ESpawnableType::Grass"))
		ULandscapeGrassType* GrassType = nullptr;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MeshToSpawn", meta = (EditCondition = "SpawnType==ESpawnableType::Foliage"))
		TArray<UFoliageType_InstancedStaticMesh*> Foliages;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MeshToSpawn", meta = (EditCondition = "SpawnType==ESpawnableType::Mesh"))
		TArray<UStaticMesh*> Mesh;

	/**
	* Class of actors to spawn
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MeshToSpawn", meta = (EditCondition = "SpawnType==ESpawnableType::Actor"))
		TArray<TSubclassOf<AActor>> Actors;

	/**
	* Could spawn actor on hit, and put scale to zero. But don't remove/add item manually otherwise it'll corrupt our local book keeping
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MeshToSpawn")
		TSubclassOf<USWHISMComponent> FoliageComponent = USWHISMComponent::StaticClass();

	/**
	* Only relevant for Instanced mesh NOT defined with a Foliage
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MeshToSpawn")
		bool CollisionEnabled = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MeshToSpawn", meta = (EditCondition = "CollisionEnabled"))
		TEnumAsByte<ECollisionChannel> CollisionChannel = ECollisionChannel::ECC_WorldDynamic;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MeshToSpawn", meta = (EditCondition = "CollisionEnabled"))
		FCollisionResponseContainer CollisionProfile;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MeshToSpawn", meta = (EditCondition = "CollisionEnabled"))
		bool bResourceCollectable = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MeshToSpawn", meta = (EditCondition = "bResourceCollectable"))
		FString ResourceName = "";
	/**
	* Relevant only if CollisionEnabled and Region Per Quadrant Side >1
	*/
	UPROPERTY(/*EditAnywhere, BlueprintReadWrite, Category = "MeshToSpawn", meta = (EditCondition = "SpawnType!=ESpawnableType::Undefined && CollisionEnabled && NumberGridRings>0") */ )
		bool CollisionOnlyAtProximity = true;

	/** Array of tags that can be used for grouping and categorizing. Can also be accessed from scripting. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MeshToSpawn")
		TArray<FName> ComponentTags;

	UPROPERTY(Transient)
		FSpawnableMeshSettings SpawnableMeshSetting;


	/**
	* Only relevant for Instanced mesh NOT defined with a Foliage
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MeshToSpawn")
		bool CastShadows = true;

	/**
	* Only relevant for Instanced mesh NOT defined with a Foliage
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MeshToSpawn")
		bool AlignToTerrainSlope = false;
	/**
	* Only relevant for Instanced mesh NOT defined with a Foliage
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MeshToSpawn", meta = (UIMin = -180.0f, ClampMin = -180.0f, UIMax = 180.f, ClampMax = 180.f, EditCondition = "AlignToTerrainSlope"))
		float YawOffsetAlignToTerrainSlope = 0.f;
	/**
	* Only relevant for Instanced mesh NOT defined with a Foliage
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MeshToSpawn")
		float AlignMaxAngle = 90.f;

	/**
	* Only relevant for Instanced mesh NOT defined with a Foliage
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MeshToSpawn")
		FFloatInterval AltitudeRange = FFloatInterval(-10000000.f, 10000000.f);
	/**
	* Only relevant for Instanced mesh NOT defined with a Foliage
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MeshToSpawn")
		FFloatInterval VerticalOffsetRange = FFloatInterval(0.f, 0.f);
	/**
	* Only relevant for Instanced mesh NOT defined with a Foliage
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MeshToSpawn", meta = (UIMin = 0.0f, ClampMin = 0.0f, UIMax = 20.f, ClampMax = 20.f))
		FFloatInterval ScaleRange = FFloatInterval(.75f, 1.25f);
	/**
	* Only relevant for Instanced mesh NOT defined with a Foliage
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MeshToSpawn")
		FFloatInterval GroundSlopeAngle = FFloatInterval(0.f, 45.f);
	/**
	 * In Unreal Unit (cm). The distance where instances will begin to fade out if using a PerInstanceFadeAmount material node. 0 disables.
	 * When the entire cluster is beyond this distance, the cluster is completely culled and not rendered at all.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MeshToSpawn", meta = (UIMin = 0))
		FInt32Interval CullDistance = FInt32Interval(0, 0);

	/** Only relevant for Instanced mesh NOT defined with a Foliage,Controls whether the foliage should inject light into the Light Propagation Volume.  This flag is only used if CastShadow is true. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MeshToSpawn", meta = (EditCondition = "CastShadows"))
		bool bAffectDynamicIndirectLighting = false;

	/** Only relevant for Instanced mesh NOT defined with a Foliage,Controls whether the primitive should affect dynamic distance field lighting methods.  This flag is only used if CastShadow is true. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MeshToSpawn", meta = (EditCondition = "CastShadows"))
		bool bAffectDistanceFieldLighting = false;

	/** Only relevant for Instanced mesh NOT defined with a Foliage,Whether this foliage should cast dynamic shadows as if it were a two sided material. */
	UPROPERTY(EditAnywhere, AdvancedDisplay, BlueprintReadWrite, Category = "MeshToSpawn", meta = (EditCondition = "CastShadows"))
		bool bCastShadowAsTwoSided = false;

	/** Whether the foliage receives decals. */
	UPROPERTY(EditAnywhere, AdvancedDisplay, BlueprintReadWrite, Category = "MeshToSpawn")
		bool bReceivesDecals = false;

	UPROPERTY(EditAnywhere, AdvancedDisplay, BlueprintReadWrite, Category = "MeshToSpawn")
		bool bKeepInstanceBufferCPUCopy = false;

	




	UPROPERTY(Transient)
		int32 FoliageIndexSeed = 0;

	UPROPERTY(Transient)
		double AllTreesRebuildTime=0.0;

	UPROPERTY(Transient)
		TArray<USWHISMComponent*> HIM_Mesh;
	UPROPERTY(Transient)
		TArray<double> HIM_Mesh_TreeRebuild_Time;

	/** If we want collision we'll spawn asset here */
	UPROPERTY(Transient)
		TArray<USWHISMComponent*> HIM_Mesh_Collision_enabled;
	UPROPERTY(Transient)
		TArray<double> HIM_Mesh_Collision_TreeRebuild_Time;


	UPROPERTY(Transient)
		TArray<FSpawnedActorList> Spawned_Actors;

	/**
	* Filtered list of valid asset classes to spawn
	*/
	UPROPERTY(Transient)
		TArray<TSubclassOf<AActor>> Actors_Validated;

	TSharedPtr <FSWShareableIndexes, ESPMode::ThreadSafe> InstanceIndexToHIMIndex;
	TSharedPtr <FSWShareableIndexes, ESPMode::ThreadSafe> NumInstancePerHIM;
	TSharedPtr <FSWShareableIndexes, ESPMode::ThreadSafe> InstanceIndexToIndexForHIM;

	UPROPERTY(Transient)
		AShaderWorldActor* Owner = nullptr;

	/**
	* Foliage have spawning information, we want to import this information once and then allow finetuning from the detail panel
	*/
	UPROPERTY(Transient)
		int32 MashNumLastCheck = 0;
	/**
	* Foliage have spawning information, we want to import this information once and then allow finetuning from the detail panel
	*/
	
	bool UpdateSpawnSettings=false;
	

	/** Instances will be placed at this density, specified in instances per 1000x1000 unit area */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MeshToSpawn", meta = (DisplayName = "Density / 1Kuu", UIMin = 0, ClampMin = 0, UIMax = 10000, ClampMax = 10000))
		float Density = 1.5f;
	/**
	* How many instances are we computing per computed grid ?
	*/
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MeshToSpawn")
		int32 InstanceCountPerSector = 65;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MeshToSpawn")
		int32 TotalComputedInstanceCount = 65;
		
	/**
	* What are the world dimension of a grid side size? In Unreal Engine unity/cm.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MeshToSpawn", meta = (UIMin = 25, ClampMin = 25, UIMax = 500, ClampMax = 500))
		int32 GridSizeMeters = 150;
	/**
	* 1...30
	*/
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MeshToSpawn"/*,meta=(UIMin = 1, UIMax = 30, ClampMin = 1, ClampMax = 30)*/)
		int32 NumberGridRings = 3;

	/**
	* Given specified information this is the dimension of the rendertarget that will be used to store the computed assets.
	*/
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MeshToSpawn")
		int32 RT_Dim = 30;
	/**
	* The lower, the more precise we can refine the position, the lower it is, the more expensive it is.
	*/
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MeshToSpawn")
		int32 PositionCanBeAdjustedWithLOD = 30;

	UPROPERTY(Transient)
		FVector ExtentOfMeshElement = FVector(0.f, 0.f, 0.f);

	UPROPERTY(Transient)
		TSet<FIntVector> All_Cams;
	UPROPERTY(Transient)
		TSet<FIntVector> Cam_Proximity;

	UPROPERTY(Transient)
	double CamerasUpdateTime = 0.0;

	//////////////////////////////////////////////////////////////////////////////////
	// GENERAL PURPOSE COMPUTE GRID
	/**
	* Pool of Computed grid
	*/
	UPROPERTY(Transient)
		TArray<FSpawnableMeshElement> SpawnablesElem;

	UPROPERTY(Transient)
		TArray<int> AvailableSpawnablesElem;
	UPROPERTY(Transient)
		TArray<int> UsedSpawnablesElem;
	UPROPERTY(Transient)
		TArray<int> SpawnablesElemReadToProcess;

	TSharedPtr <FSWShareableIndexesCompletion, ESPMode::ThreadSafe> ProcessedRead;

	struct FSpawnableProcessingWork
	{
		bool bCollisionProcessingOverflow = false;

		bool SentToHISM=false;

		int32 ElemID = -1;
		bool bUsePrecomputedTransform = false;
		int32 RTDim = -1;
		FVector CompLocation;
		FVector MeshLocCompute;
		FFloatInterval AltitudeRange;
		TSharedPtr<FSWColorRead, ESPMode::ThreadSafe> Read;
		TSharedPtr<FSWSpawnableTransforms, ESPMode::ThreadSafe> InstancesT;

		TSharedPtr <FSWShareableIndexes, ESPMode::ThreadSafe> InstanceIndexToHIMIndex;
		TSharedPtr <FSWShareableIndexes, ESPMode::ThreadSafe> NumInstancePerHIM;
		TSharedPtr <FSWShareableIndexes, ESPMode::ThreadSafe> InstanceIndexToIndexForHIM;

		TSharedPtr<FSWShareableVerticePositionBuffer, ESPMode::ThreadSafe> DestB;
		inline FSpawnableProcessingWork() {}
	};

	TArray<FSpawnableProcessingWork> SpawnableWorkQueue;

	/** Allows skipping processing, if another one is already in progress */
	//TAtomic<bool> bProcessingSpawnablesData;


	UPROPERTY(Transient)
		TArray<int> SpawnablesElemNeedCollisionUpdate;
	/**
	* A map holding the compute grid elements location and IDs
	*/
	UPROPERTY(Transient)
		TMap<FIntVector, int32 > SpawnablesLayout;


	UPROPERTY(Transient)
		TArray<int> SegmentedOnly_ElementToUpdateData;

	//////////////////////////////////////////////////////////////////////////////////
	// PROXIMITY COLLISION ONLY
	/**
	* Pool of Proximity collision
	*/
	UPROPERTY(Transient)
		TArray<FSpawnableMeshProximityCollisionElement> SpawnablesCollisionElem;
	UPROPERTY(Transient)
		TArray<int> AvailableSpawnablesCollisionElem;
	UPROPERTY(Transient)
		TArray<int> UsedSpawnablesCollisionElem;
	/**
	* A map holding the Proximity Collision Layout
	*/
	UPROPERTY(Transient)
		TMap<FIntVector, int32 > SpawnablesCollisionLayout;
	//////////////////////////////////////////////////////////////////////////////////

	/**
	* This spawnable element is tied to a specific clipmap ring defined by the surface around the player we're computing asset for.
	* Asset computed near the player will be tied to lower LODs/(higher level) clipmap ring.
	* If those rings are not visible, the assets won't be neither/ not computed.
	*/
	UPROPERTY(Transient)
		int32 IndexOfClipMapForCompute = -1;

	/**
	* Used only if this spawnable wants to use custom density logic to be spawned, it can use the landscape heightmap, normalmap and all its layers
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawnables")
		TObjectPtr < UMaterialInterface> CustomSpawnablesMat = nullptr;

	FSpawnableMeshElement& GetASpawnableElem();
	FSpawnableMeshProximityCollisionElement& GetASpawnableCollisionElem();

	void ReleaseSpawnableElem(int ID);

	void UpdateSpawnableData(FSWBiom& Biom, FSpawnableMeshElement& MeshElem);

	void UpdateStaticMeshHierachyComponentSettings(UHierarchicalInstancedStaticMeshComponent* Component, FInt32Interval CullDist);
	void UpdateComponentSettings(UHierarchicalInstancedStaticMeshComponent* Component, const UFoliageType_InstancedStaticMesh* InSettings);
	void Initiate(AShaderWorldActor* Owner_, int32 Index, int32 BiomIndex);


	void InitiateNew(AShaderWorldActor* Owner_, int32 Index, int32 BiomIndex);


	void SpawnCollisionEnabled_HISM(TArray<TArray<FTransform>>& Transforms);

	void CleanUp();

	bool bHasValidSpawningData();

	void Initialize(const FSpawnableMesh& Source)
	{
		SpawnType = Source.SpawnType;
		GrassType = Source.GrassType;
		Foliages = Source.Foliages;
		Mesh = Source.Mesh;
		Actors = Source.Actors;
		FoliageComponent = Source.FoliageComponent;
		CollisionEnabled = Source.CollisionEnabled;
		CollisionChannel = Source.CollisionChannel;
		bResourceCollectable = Source.bResourceCollectable;
		ResourceName = Source.ResourceName;
		CollisionOnlyAtProximity = Source.CollisionOnlyAtProximity;
		CastShadows = Source.CastShadows;
		AlignMaxAngle = Source.AlignMaxAngle;
		AltitudeRange = Source.AltitudeRange;
		VerticalOffsetRange = Source.VerticalOffsetRange;
		ScaleRange = Source.ScaleRange;
		GroundSlopeAngle = Source.GroundSlopeAngle;
		CullDistance = Source.CullDistance;
		bAffectDynamicIndirectLighting = Source.bAffectDynamicIndirectLighting;
		bAffectDistanceFieldLighting = Source.bAffectDistanceFieldLighting;
		bCastShadowAsTwoSided = Source.bCastShadowAsTwoSided;
		bReceivesDecals = Source.bReceivesDecals;
		Density = Source.Density;
		GridSizeMeters = Source.GridSizeMeters;
		ExtentOfMeshElement = Source.ExtentOfMeshElement;
		CustomSpawnablesMat = Source.CustomSpawnablesMat;
	};

	const FString GetSpawnableName()
	{
		switch(SpawnType)
		{
		case(ESpawnableType::Undefined):
			return "Undefined Spawnable Type";
		case(ESpawnableType::Grass):
			if(GrassType)
				return GrassType->GetName();
			return "Empty Grass Type";
		case(ESpawnableType::Mesh):
			if(Mesh.Num()>0 && Mesh[0])
				return Mesh[0]->GetName();
			return "Empty Mesh Type";
		case(ESpawnableType::Foliage):
			if (Foliages.Num() > 0 && Foliages[0])
				return Foliages[0]->GetName();
			return "Empty Foliage Type";
		case(ESpawnableType::Actor):
			if (Actors.Num() > 0 && Actors[0])
				return Actors[0]->GetName();
			return "Empty Actor Type";
		default:
			return "";
		}
	}

	~FSpawnableMesh();
};


USTRUCT(BlueprintType)
struct FSWBiom
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "")
	FString BiomName = "";
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "")
		TObjectPtr < UMaterialInterface> BiomDensitySpawner = nullptr;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "")
	TArray<FSpawnableMesh> Spawnables;

	UPROPERTY(Transient)
		TArray<int> SortedSpawnables;

	UPROPERTY(Transient)
		TArray<int> SortedSpawnables_Collision;
			
};
class SHADERWORLD_API SWStructs
{
public:
	SWStructs();
	~SWStructs();
};

struct SWDrawMaterialToRTData
{
	FScene* Scene = nullptr;
	/*
	 * Can be used by ocean generator
	 */
	float Time = 0.f;
	bool UseBorder = false;
	float PatchFullSize = 10.f;
	FVector PatchLocation = FVector(0.f);
	bool UseSpecifiedLocation = false;
	UTextureRenderTarget2D* SpecificLocations = nullptr;

	UMaterialInterface* Material = nullptr;
	UTextureRenderTarget2D* RenderTarget = nullptr;
};

struct SWCopyData
{
	UTextureRenderTarget2D* A = nullptr;
	UTextureRenderTarget2D* B = nullptr;
	int32 Border = 0;
	uint32 ChannelSelect = 0;

	UTextureRenderTarget2D* B_duplicate = nullptr;
	FVector2D SourceWSLocation;
	float SourceWorldSize;
	FVector2D DestWSLocation;
	float DestWorldSize;

	
	SWCopyData(UTextureRenderTarget2D* A_, UTextureRenderTarget2D* B_, UTextureRenderTarget2D* BDup, int32 Border_, uint32 Channel_, FVector2D SL, FVector2D DL,float SDim,float DDim )
	: A(A_)
	, B(B_)
	, Border(Border_)
	, ChannelSelect(Channel_)
	, B_duplicate(BDup)
	, SourceWSLocation(SL)
	, SourceWorldSize(SDim)
	, DestWSLocation(DL)
	, DestWorldSize(DDim)
	{};

	SWCopyData() {};
	~SWCopyData() {};
};

struct SWNormalComputeData
{
	UTextureRenderTarget2D* A = nullptr;
	UTextureRenderTarget2D* B = nullptr;
	uint32 N = 15;
	float LocalGridScaling = 1.f;
	float SWHeightmapScale = 1.f;

	SWNormalComputeData(UTextureRenderTarget2D* A_, UTextureRenderTarget2D* B_, uint32 N_,float LocalGridScaling_, float SWHeightmapScale_) :A(A_), B(B_), N(N_), LocalGridScaling(LocalGridScaling_), SWHeightmapScale(SWHeightmapScale_){};

	SWNormalComputeData() {};
	~SWNormalComputeData() {};
};

struct SWSampleRequestComputeData
{
	UTextureRenderTarget2D* SamplesXY = nullptr;
	TSharedPtr<FSWShareableSamplePoints> SamplesSource;

	SWSampleRequestComputeData(UTextureRenderTarget2D* InSamplesXY, TSharedPtr<FSWShareableSamplePoints>& InSamplesSource)
		:SamplesXY(InSamplesXY)
		, SamplesSource(InSamplesSource)
	{};

	SWSampleRequestComputeData() {};
	~SWSampleRequestComputeData() {};
};
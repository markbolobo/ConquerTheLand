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
#include "Data/SWStructs.h"
#include "GameFramework/Actor.h"
#include "ConvexVolume.h"
#include "Component/SWorldRoot.h"
#include "Data/SW_PointerQuadtree.h"

#include "Engine/CollisionProfile.h"
#include "Kismet/KismetRenderingLibrary.h"


#include "ShaderWorldActor.generated.h"

class USWorldSubsystem;
class UShaderWorldCollisionComponent;
class USWCollectableInstancedSMeshComponent;
class USW_CollisionComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UTextureRenderTarget2D;
class UStaticMesh;
class UGeoClipmapMeshComponent;
class UInstancedStaticMeshComponent;
class UMaterialParameterCollection;
class UTextureRenderTarget2DArray;
class AShaderWorldBrushManager;
class UFoliageType;
class UFoliageType_InstancedStaticMesh;
class ULandscapeGrassType;
class FSWShareableIndexBuffer;
class FSWShareableVerticePositionBuffer;
class FSWSimpleReadbackManager;
class USWSeedGenerator;

struct FGeoCProcMeshVertex;

static int ShaderWorldDebug = 0;

#define SW_COMPUTE_GENERATION 0

static double CollisionReadBackTime = 0;
static double CollisionReadBackTimeMax = 0;
static double CollisionReadBackTimeMin = 100.0;
static int32 Collisioncount = 0;

extern TGlobalResource< FSWSimpleReadbackManager > GSWSimpleReadbackManager;

class FSWSimpleReadbackManager : public FRenderResource
{
	struct FReadBackTask
	{
		int32 BlockSize = 0;
		int32 ExtentX = 0;
		int32 ExtentY = 0;
		TSharedPtr<FRHIGPUTextureReadback> ReadStage;
		TSharedPtr<FSWColorRead, ESPMode::ThreadSafe> Destination;
		TSharedPtr < FThreadSafeBool, ESPMode::ThreadSafe> ProcessedStatus;		

		

		FReadBackTask(int32 InBlockSize, int32 InExtentX, int32 InExtentY, TSharedPtr<FRHIGPUTextureReadback>& InReadStage, TSharedPtr<FSWColorRead, ESPMode::ThreadSafe>& InDest, TSharedPtr < FThreadSafeBool, ESPMode::ThreadSafe>& InProc)
			: BlockSize(MoveTemp(InBlockSize))
			, ExtentX(MoveTemp(InExtentX))
			, ExtentY(MoveTemp(InExtentY))
			, ReadStage(MoveTemp(InReadStage))
			, Destination(MoveTemp(InDest))
			, ProcessedStatus(MoveTemp(InProc))
		{
		}

		FReadBackTask() {};
		~FReadBackTask() {};
	};

public:
	FSWSimpleReadbackManager()
	{}

	virtual ~FSWSimpleReadbackManager() override
	{
		FScopeLock ScopeLock(&MapLock);

	}
	void TickReadBack();
	void AddPendingReadBack(int32 InBlockSize, int32 InExtentX, int32 InExtentY, TSharedPtr<FRHIGPUTextureReadback>& InReadStage, TSharedPtr<FSWColorRead, ESPMode::ThreadSafe>& InDest, TSharedPtr < FThreadSafeBool, ESPMode::ThreadSafe>& InProc);

protected:

	FCriticalSection MapLock;
	TArray<FReadBackTask> PendingReads;
};


DECLARE_DYNAMIC_DELEGATE_OneParam(FSWHeightRetrievalDelegate, const TArray<FVector3f>&, Locations);

UCLASS(hideCategories(Rendering, Input, Game, LOD, Replication, Networking, Cooking, HLOD,Collision), meta = (DisplayName = "Shader World Actor"), AutoCollapseCategories = ("Advanced"))
class SHADERWORLD_API AShaderWorldActor : public AActor
{
	GENERATED_UCLASS_BODY()
	
public:	
	// Sets default values for this actor's properties
	AShaderWorldActor();

protected:
	// Called when the game starts or when spawned
	virtual void PostInitializeComponents() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void ApplyWorldOffset(const FVector& InOffset, bool bWorldShift) override;
	static void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);

	/*
	 * When editing properties from detail panels, all our components get unregistered and registered back
	 * It is unecessary in our case, in which construction script for instanced, isn't relevant.
	 * The consequences of editing Shader World properties will be handled at runtime. 
	 */
	bool PreventReRegistration = false;
	bool RuntimePropertyEditing = false;
	virtual void UnregisterAllComponents(bool bForReregister = false) override;
	virtual void PostUnregisterAllComponents() override;
	virtual void ReregisterAllComponents() override;

public:


#if WITH_EDITOR

	bool ShouldTickIfViewportsOnly() const override;
	virtual void PreEditChange(FProperty* PropertyThatWillChange) override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;

#endif

	// Called every frame
	virtual void Tick(float DeltaTime) override;
	
	UPROPERTY(Transient)
		EGeoRenderingAPI RendererAPI = EGeoRenderingAPI::DX11;

	UPROPERTY(Transient)
		FString RHIString="";

	/**
	* Hard rebuild of the world
	*/
	UPROPERTY(Transient, EditAnywhere, BlueprintReadWrite, Category = "World Settings")
		bool rebuild = false;

	FThreadSafeBool EditRebuild = false;
	/**
	* You can generated an ocean not requiring collisions
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World Settings")
		bool GenerateCollision = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config", meta = (EditCondition = "GenerateCollision"))
		TEnumAsByte<ECollisionChannel> CollisionChannel = ECollisionChannel::ECC_WorldStatic;

	/**
	* Update Patch Location after checking Camera Players location
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config",meta=(UIMin = 1.0, UIMax = 120.0, ClampMin = 1.f, ClampMax = 120.0f))
		float UpdateRateCameras = 30.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config", meta = (UIMin = 0.4, UIMax = 1.0, ClampMin = 0.4f, ClampMax = 1.0f))
		float AltitudeToLODTransition = 0.7f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config")
		bool SmoothLODTransition = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config", meta = (UIMin = 0.001, UIMax = 0.5, ClampMin = 0.001f, ClampMax = 0.5f, EditCondition = "SmoothLODTransition"))
		float TransitionWidth = 0.2f;

	/**
	* Update Height Data Over Time
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config")
		bool UpdateHeightDataOverTime = false;

	FThreadSafeBool UpdateHOverTime = false;

	/**
	* Update Height Data Over Time
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config", meta = (EditCondition = "UpdateHeightDataOverTime"))
		float UpdateRateHeight = 30.0f;
	/**
	* 1.0 : Far LOD update as often as close LOD
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config", meta = (EditCondition = "UpdateHeightDataOverTime", UIMin = 1.0, UIMax = 1.6, ClampMin = 1.f, ClampMax = 1.6f))
		float UpdateRateDegradationFactor = 1.35f;

	UPROPERTY(/*EditAnywhere, BlueprintReadWrite, Category = "Config" */ )
		bool WorldHasBounds = false;
	/**
	* 1.0 : WorldBounds in Meters
	*/
	UPROPERTY(/*EditAnywhere, BlueprintReadWrite, Category = "Config", meta = (EditCondition = "WorldHasBounds") */ )
		FVector2D WorldBounds = FVector2D(8000,8000);
	UPROPERTY(/*EditAnywhere, BlueprintReadWrite, Category = "Config", meta = (EditCondition = "WorldHasBounds") */ )
		int32 TileSizeGeneration = 64;

	UPROPERTY(Transient)
		float TimeAcu = 0.0f;	

	UPROPERTY(Transient)
		float FoliageStart = 0.0f;


	inline void BrushManagerRequestRedraw(const TArray<FBox2D>& RedrawScope){BrushManagerRedrawScopes.Append(RedrawScope); BrushManagerRedrawScopesSpawnables.Append(RedrawScope); BrushManagerRedrawScopes_collision.Append(RedrawScope);};

	UPROPERTY(Transient)
		TArray<FBox2D> BrushManagerRedrawScopes;
	UPROPERTY(Transient)
		TArray<FBox2D> BrushManagerRedrawScopesSpawnables;
	UPROPERTY(Transient)
		TArray<FBox2D> BrushManagerRedrawScopes_collision;

	bool BrushRequestVegetationRedraw = false;
	FBox2D CurrentRedrawScopeVegetation;

	UPROPERTY(Transient)
		float UncompleteBrushSpawnableUpdate = false;

	UPROPERTY(Transient)
		float CollisionUpdateTimeAcu=0.f;

	/**
	* Currently supporting 511/255/127/63/31/15
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config")
		ENValue VerticePerPatch = ENValue::N511;
	/**
	* How Far are we Computing data, in meters
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config")
		int32 DrawDistance = 6000;
	/**
	*  Distance between two points at highest quality LOD
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config", meta = (UIMin = 1, UIMax = 500, ClampMin = 1, ClampMax = 500))
		int32 TerrainResolution = 50;


	/**
	* Define the cache resolution
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config",meta = (UIMin = 1, UIMax = 10, ClampMin = 1, ClampMax = 10))
		int ClipMapCacheIntraVerticesTexel = 2;
	/**
	* World Position Offset prevent caching of Virtual Shadow Maps, Prevent WPO above a specific LOD
	* Requires " r.OptimizedWPO = true "
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config", meta = (UIMin = 0, UIMax = 15, ClampMin = 0, ClampMax = 15))
		int EnableWorldPositionOffsetUnderLOD = 2;
	/**
	* 0 for Mobile | LOD below adjust their topology to better represent the terrain.
	* Depending on your rendering API, Unreal Engine might not have the required features to update the topology
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config", meta = ( UIMin = 0, UIMax = 15, ClampMin = 0, ClampMax = 15))
		int TopologyFixUnderLOD = 5;

	UPROPERTY(/*EditAnywhere, BlueprintReadWrite, Category = "World Settings", meta = (EditCondition = "EnableCaching", UIMin = 1, UIMax = 15, ClampMin = 1, ClampMax = 15) */ )
		int LOD_above_doubleCacheResolution = 15;
	/**
	* Indicated the total memory budget allocated by the rendertargets, including the spawnables and collisions
	*/
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Config")
		float RendertargetMemoryBudgetMB = 0;
	
	/**
	* Hard rebuild of the vegetation
	*/
	UPROPERTY(Transient, EditAnywhere, BlueprintReadWrite, Category = "Spawnables")
		bool rebuildVegetationOnly = false;

	FThreadSafeBool EditRebuildVegetation;
	
	FThreadSafeBool UpdatePatchData = false;

	/**
	* Level 0 / Highest LOD World dimensions per side in meters
	*/
	UPROPERTY(Transient)
		int32 WorldDimensionMeters = 12700;
	
	
	/**
	* Translate WorldDimensionMeters to intra vertices dimension for Level 0 / Highest LOD
	*/
	UPROPERTY(Transient)
		int32 GridSpacing = 5000;
	

	UPROPERTY(Transient)
		int32 N = 511;
	
	/**
	* Number of LODs to compute
	*/
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Config",meta = (UIMin = 1, UIMax = 16, ClampMin = 1, ClampMax = 16))
		int32 LOD_Num = 8;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Config")
		TArray<int32> LODs_DimensionsMeters;


	////////////////////////////////////////////////////////
	// Segmented Updates Start
	///////////////////////////////////////////////////////

	UPROPERTY(Transient)
		TArray<bool> NeedSegmentedUpdate;
	UPROPERTY(Transient)
		TArray<bool> ClipMapToUpdateAndMove;
	UPROPERTY(Transient)
		TArray<bool> ClipMapToUpdate;

		FRenderCommandFence SegmentedFence;
	////////////////////////////////////////////////////////
	// Segmented Updates End
	///////////////////////////////////////////////////////

	/**
	* Material applied when Cache is enabled: Ocean/Land world is Height/Normals are read from the cache
	*/
	UPROPERTY(EditAnywhere, Category = "World Settings")
		TObjectPtr < UMaterialInterface> Material = nullptr;

	UPROPERTY(Transient)
		TObjectPtr < UMaterialInstanceDynamic> MatDyn = nullptr;

	UPROPERTY(EditAnywhere, Category = "World Settings")
		TObjectPtr < UMaterialInterface> Generator = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World Settings")
		TArray<FClipMapLayer> LandDataLayers;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Instanced, Category = "World Settings")
		USWSeedGenerator* SeedGenerator;

	UPROPERTY(Transient)
		FSWBagOfSeeds CurrentSeedsArray;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World Settings",meta=(UIMin = 0, UIMax = 2000000, ClampMin = 0, ClampMax = 2000000))
		int32 ShadowCastingRange=300000;		

	UPROPERTY(Transient)
		TObjectPtr<UTextureRenderTarget2D> CollisionSampleLocation = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World Collision")
		bool CollisionVisible = false;

	FThreadSafeBool CollisionVisibleChanged = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World Collision")
		int CollisionGridRingNumber = 2;
	/**
	*  Distance between two adjacent points on collision mesh
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World Collision", meta = (UIMin = 1, UIMax = 500, ClampMin = 1, ClampMax = 500))
		int32 CollisionResolution = 50;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World Collision", meta = (UIMin = 15, UIMax = 511, ClampMin = 15, ClampMax = 511))
		int32 CollisionVerticesPerPatch = 130;
	/**
	*  DrawCall count we are allowed to send per frame to generate collision patch
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World Collision")
		int32 CollisionMaxDrawCallPerFrame = 3;

	
	
	UPROPERTY(EditAnywhere, Category = "World Collision")
		TObjectPtr < UMaterialInterface> CollisionMat = nullptr;


	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World Collision")
		bool bExportPhysicalMaterialID = false;

	bool bExportPhysicalMaterialID_cached = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World Collision", meta = (EditCondition = bExportPhysicalMaterialID))
		FName LayerStoringMaterialID = "";

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World Collision", meta = (EditCondition = bExportPhysicalMaterialID))
		EDataMapChannel LayerChannelStoringID = EDataMapChannel::Red;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "")
		TObjectPtr < USWorldRoot > RootComp;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawnables")
		TArray <FSWBiom> Bioms;

		TMap<USWHISMComponent*, FSpawnableMesh*> CollisionToSpawnable;

	UPROPERTY(EditAnywhere, Category = "Spawnables")
		UMaterialInterface* SpawnablesMat = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawnables")
		int DrawCallBudget_Spawnables = 8;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawnables", meta = (DisplayName = "No Popping Range Meters",UIMin = 0, UIMax = 2500, ClampMin = 0, ClampMax = 2500))
		int NoPoppingRange = 750;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawnables")
		int32 MaximumAmountofInstanceToUpdateonGameThread = 1000;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawnables")
		float DelayBetweenTwoVisualUpdates = 0.45f;

	int32 InstanceCountUpdate = 1000;

	UPROPERTY(Transient)
		bool DrawCallBudget_Spawnables_Start_set = false;
	UPROPERTY(Transient)
		int DrawCallBudget_Spawnables_Start = 0;


			////////////////////////////////////////////// VT START
	/**
	 * Array of runtime virtual textures into which we render this landscape.
	 * The material also needs to be set up to output to a virtual texture.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = VirtualTexture, meta = (DisplayName = "Render to Virtual Textures"))
		TArray<URuntimeVirtualTexture*> RuntimeVirtualTextures;
	/**
	* Number of lower mips in the runtime virtual texture to skip for rendering this primitive.
	* Larger values reduce the effective draw distance in the runtime virtual texture.
	* This culling method doesn't take into account primitive size or virtual texture size.
	*/
	UPROPERTY(EditAnywhere, AdvancedDisplay, BlueprintReadOnly, Category = VirtualTexture, meta = (DisplayName = "Virtual Texture Skip Mips", UIMin = "0", UIMax = "7"))
		int32 VirtualTextureCullMips = 0;

	/** Desired cull distance in the main pass if we are rendering to both the virtual texture AND the main pass. A value of 0 has no effect. */
	UPROPERTY(EditAnywhere, AdvancedDisplay, BlueprintReadOnly, Category = VirtualTexture, meta = (DisplayName = "Max Draw Distance in Main Pass"))
		float VirtualTextureMainPassMaxDrawDistance = 0.f;
	/**
	 * Number of mesh levels to use when rendering landscape into runtime virtual texture.
	 * Set this only if the material used to render the virtual texture requires interpolated vertex data such as height.
	 * Higher values use more tessellated meshes and are expensive when rendering the runtime virtual texture.
	 */
	UPROPERTY(EditAnywhere, AdvancedDisplay, BlueprintReadOnly, Category = VirtualTexture, meta = (DisplayName = "Virtual Texture Num LODs", UIMin = "0", UIMax = "7"))
		int32 VirtualTextureNumLods = 0;

	/** Bias to the LOD selected for rendering to runtime virtual textures. */
	UPROPERTY(EditAnywhere, AdvancedDisplay, BlueprintReadOnly, Category = VirtualTexture, meta = (DisplayName = "Virtual Texture LOD Bias", UIMin = "0", UIMax = "7"))
		int32 VirtualTextureLodBias = 0;

	/** Render to the main pass based on the virtual texture settings. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = VirtualTexture, meta = (DisplayName = "Virtual Texture Pass Type"))
		ERuntimeVirtualTextureMainPassType VirtualTextureRenderPassType = ERuntimeVirtualTextureMainPassType::Always;

	////////////////////////////////////////////// VT END

	/*
	 * When computing height for an ocean, the altitude range used are much lower than for a terrain.
	 * While producing heightmap we round output height every 1cm, for oceans it might not be precise enough and steps are visible after normalmap computation 
	 * To reach precision below 1cm we multiply the height by HeightScale at the end of the ocean generator, and whenever we use the heightmap we divide the read value by HeightScale
	 * It's manageable because we don't use brushes when rendering the ocean, otherwise we would have to add and update this HeightScale factor in every material used, which is not planned for now.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Advanced|Height Scaling (Optional - Ocean)", meta = (UIMin = 1.0, UIMax = 10000.0, ClampMin = 1.f, ClampMax = 10000.0f))
		float HeightScale = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Advanced|Brush Manager (Optional)")
	AShaderWorldBrushManager* BrushManager = nullptr;

	UPROPERTY(EditAnywhere, Category = "Advanced|NoiseTexture")
		TObjectPtr<class UTexture2D> TileableNoiseTexture;

	/**
	 * To counter CPU culling we're scaterring the actual ring clipmaps vertices vertically
	 * Ocean should work with this parameter close to 0.f
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Advanced|ClipMap Hack Culling", meta = (UIMin = 0.25, UIMax = 60000.0, ClampMin = 0.25f, ClampMax = 60000.0f))
		float VerticalRangeMeters = 2000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Advanced|World Shape")
		EWorldShape WorldShape = EWorldShape::Flat;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Advanced|World Shape", meta=(EditCondition = "WorldShape == EWorldShape::Spherical ",UIMin = 10, UIMax = 6371, ClampMin = 10, ClampMax = 6371))
		int PlanetRadiusKm = 800;		


	//////////////////////////////////////////
// Data sharing

	UFUNCTION(BlueprintCallable, Category = "Collision")
		bool UpdateCollisionInRegion(const FBox Area);

	TQueue<FBox2D> CollisionUpdateRequest;

	/*
	 * Evaluate the generator function anywhere : 25 Samples per frame maximum
	 * Doesn't take into account brushes for now, if you want to use it extensively you would need
	 * A seperate actor or subsystem which would gather requestd from everyone, compute the answers and send them back when ready mostly through TOptional<FVector>
	 */
	UFUNCTION(BlueprintCallable, Category = "HeightRetrieve")
		bool RetrieveHeightAt(const TArray<FVector>& Locations, const FSWHeightRetrievalDelegate& Callback);

	FSWHeightRetrievalDelegate HeightRetrieveDelegate;
	FRenderCommandFence HeighRetrievalFence;
	TSharedPtr < FThreadSafeBool, ESPMode::ThreadSafe> bProcessingHeightRetrieval;
	TSharedPtr < FThreadSafeBool, ESPMode::ThreadSafe> bProcessingHeightRetrievalRT;

/*Allow other actors to access the landscape data, i.e ocean getting landscape heightmap*/
	UFUNCTION(BlueprintCallable, Category = "WorldData")
		float Get_TerrainPatchSize(int LOD);

	UFUNCTION(BlueprintCallable, Category = "WorldData")
		FIntVector Get_LOD_RingLocation(int LOD);

	UFUNCTION(BlueprintCallable, Category = "WorldData")
		void AssignHeightMapToDynamicMaterial(const int LOD, UMaterialInstanceDynamic* MatDyn_,FName ParameterName);

	UFUNCTION(BlueprintCallable, Category = "WorldData")
		UTextureRenderTarget2D* Get_LOD_HeightMap(int LOD);

	UFUNCTION(BlueprintCallable, Category = "WorldData")
		UTextureRenderTarget2D* Get_LOD_HeightMap_Segmented(int LOD);

	UFUNCTION(BlueprintCallable, Category = "WorldData")
		UTextureRenderTarget2D* Get_LOD_NormalMap(int LOD);

	UFUNCTION(BlueprintCallable, Category = "WorldData")
		UMaterialInstanceDynamic* Get_LOD_DynamicGeneratorMaterial(int LOD);

	UFUNCTION(BlueprintCallable, Category = "WorldData")
		float Get_LOD_Dimension(int LOD);

	UFUNCTION(BlueprintCallable, Category = "WorldData")
		void RequestNewOrigin(FIntVector NewWorldOrigin);
	

	/*Send our data updates TO those other world*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Advanced|Dependencies")
		AShaderWorldActor* DataReceiver = nullptr;

	/*Receive data updates FROM those other world*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Advanced|Dependencies")
		AShaderWorldActor* DataSource = nullptr;

	/*A ring of LOD 5 in receiver will use the LOD 5 + LOD_Offset_FromReceiverToSource of source*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Advanced|Dependencies")
		int LOD_Offset_FromReceiverToSource = 0;

	/*Update Data that will not change regularly: ring dimensions, vertices number,...*/
	void UpdateStaticDataFor(AShaderWorldActor* Source_, FVector& CamLocationSource);
	/*Update Location */
	void ReceiveExternalDataUpdate(AShaderWorldActor* Source, int LOD_, FVector NewLocation);


	//////////////////////////////////////////
	int GetMeshNum(){return Meshes.Num();};
	FClipMapMeshElement& GetMesh(int i){return Meshes[i];};

	FBox2D GetHighestLOD_FootPrint();
	bool HighestLOD_Visible();

	FVector GetCameraLocation(){return CamLocation;};
	bool UseSegmented();

	//FORCEINLINE void TrackComponent(USW_CollisionComponent* ActorToTrack){if(ActorToTrack)External_Actors_Tracked.Add(ActorToTrack);};

protected:

	UPROPERTY(Transient)
		TArray<FClipMapMeshElement> Meshes;

	UPROPERTY(Transient)
		TArray<FClipMapPerLODCaches> LODCaches;

	TSharedPtr < FThreadSafeBool, ESPMode::ThreadSafe> bPreprocessingCollisionUpdate;

	UPROPERTY(Transient)
		TArray<FCollisionMeshElement> CollisionMesh;

	TSharedPtr<FSWCollisionManagementShareableData, ESPMode::ThreadSafe> CollisionShareable;

	UPROPERTY(Transient)
		TArray<int> UsedCollisionMesh;

	UPROPERTY(Transient)
		TArray<int> CollisionReadToProcess;


	UPROPERTY(Transient)
		TObjectPtr < UTextureRenderTarget2D> ReadRequestLocation = nullptr;
	UPROPERTY(Transient)
		TObjectPtr < UTextureRenderTarget2D> ReadRequestLocationHeightmap = nullptr;
	UPROPERTY(Transient)
		TObjectPtr < UMaterialInstanceDynamic> GeneratorDynamicForReadBack = nullptr;

	TArray<FVector3f> PositionsOfReadBacks;

	TSharedPtr<FSWShareableSamplePoints, ESPMode::ThreadSafe> PointsPendingReadBacks;
	TSharedPtr<FSWColorRead, ESPMode::ThreadSafe> ReadBackHeightData;

	FRenderCommandFence HeightReadBackFence;


	FCollisionMeshElement& GetACollisionMesh();
	void ReleaseCollisionMesh(int ID);

	void ProcessSeeds();
	void RebuildCleanup();
	bool Setup();

	bool ProcessSegmentedComputation();
	void UpdateRenderAPI();

	void InitiateWorld();
	void Merge_SortList(FSWBiom& Biom,TArray<int>& SourceList);
	void SortSpawnabledBySurface(FSWBiom& Biom);
	void SetupCameraForSpawnable(FSpawnableMesh& Spawn, TSet<FIntVector>& Cameras_proximity, TSet<FIntVector>& All_cameras);
	void ReleaseCollisionBeyondRange(FSpawnableMesh& Spawn, TSet<FIntVector>& Cam_Proximity);
	public:
	int32 FindBestCandidate_SpawnableElem(int32& GridSizeMeters,int& IndexOfClipMapForCompute,FSpawnableMeshElement& El,bool& Segmented);
	protected:
	void ReleaseSpawnElemBeyondRange(FSWBiom& Biom, FSpawnableMesh& Spawn, TSet<FIntVector>& Cam_Proximity, TSet<FIntVector>& All_cams, bool& Segmented, int& MaxRing);
	void AssignSpawnableMeshElement(int& i,int& j,bool& Segmented, FSWBiom& Biom, FSpawnableMesh& Spawn, FIntVector& LocMeshInt, TSet<FIntVector>& Cam_Proximity);
	bool UpdateSpawnable(FSWBiom& Biom, int indice, int Biomindice, bool MustBeInFrustum, int MaxRing = -1);

	void SetN();
	void IncrementSpawnableDrawCounter();

	void CreateGridMeshWelded(int offset, int32 NumX, int32 NumY, TSharedPtr<FSWShareableIndexBuffer>& Triangles, TSharedPtr<FSWShareableIndexBuffer>& TrianglesAlt, TArray<FVector3f>& Vertices, TArray<FVector2f>& UVs, TArray<FVector2f>& UV1s, TArray<FVector2f>& UV2s, int32& GridSpacing, FVector& Offset, uint8 StitchProfil);
	void CreateGridMeshWelded(int32 NumX, int32 NumY, TSharedPtr<FSWShareableIndexBuffer>& Triangles, TSharedPtr<FSWShareableVerticePositionBuffer>& Vertices, TArray<FVector2f>& UVs, int32 GridSpac);
	void UpdateViewFrustum();
	void UpdateCameraLocation();
	float HeightToClosestCollisionMesh();
	void HideChildrenLODs(int Level_Parent);

protected:

	void MoveClipMapComponentToLocation(int index);
	void MoveClipMapMaterialLocation(int index, bool Segmented = false);
	void ComputeHeight_Segmented_MapForClipMap(int index);
	void ComputeHeightMapForClipMap(int index);
	void TopologyUpdate(int index);
	void CopyHeightmapSegmentedToHeightMap(int index);
	void CopyNormalMapSegmentedToNormalMap(int index);
	void CopyLayersSegmentedToLayers(int index);

	void ComputeNormalForClipMap(int index, bool bFromSegmented = false);
	void ComputeSegmentedNormalForClipMap(int index);
	void ComputeDataLayersForClipMap(int index);
	void ComputeDataLayersSegmentedForClipMap(int index);
	void UpdateClipMap();
	void FixNamingCollision(const UWorld* W, const FCollisionMeshElement& Mesh, const FString& WantedName);


	void UpdateSpawnables();
	bool CanUpdateSpawnables();

	void FinalizeAsyncWork();

	bool UpdateSpawnableNew(FSWBiom& Biom, int indice, int Biomindice, bool MustBeInFrustum, int MaxRing = -1);
	void UpdateSpawnablesNew();


	void SpawnablesManagement(float& DeltaT);
	void TerrainAndSpawnablesManagement(float& DeltaT);
	void BoundedWorldUpdate(float& DeltaT);
	void ProcessBoundedWorldVisit(const uint64 TreeID_, const TArray<TStaticArray<TSharedPtr < FSW_PointerTree<FSWQuadElement>::FPointerNode>, 4 >>& ToCreate, const TArray<TStaticArray<TSharedPtr < FSW_PointerTree<FSWQuadElement>::FPointerNode>, 4 >>& ToCleanUp);

	void CollisionManagement(float& DeltaT);
	bool SetupCollisions();
	bool CollisionFinalizeWork();
	bool CollisionPreprocessGPU();
	void CollisionCPU();
	void CollisionGPU();
	FRenderCommandFence CollisionProcess;
	bool RedbuildCollisionContext = false;

	void ReadbacksManagement();

	int32 SphericalProjection(FIntVector Destination);
	inline static double GetHeightFromGPURead(uint8* ReadLoc,uint16& MaterialIndice);
	static double GetHeightFromGPUReadOld(FColor& ReadLoc, uint16& MaterialIndice);

	double ComputeWorldHeightAt(FVector WorldLocation);
	void UpdateCollisionMeshData(FCollisionMeshElement& Mesh );

	static void GetLocalTransformOfSpawnable(FInstancedStaticMeshInstanceData& OutTransform, const FVector& CompLoc, FColor& LocX, FColor& LocY, FColor& LocZ, FColor& Rot,/*FColor& Scale, */ FInstancedStaticMeshInstanceData& FromT, const bool& IsAdjustement, const FFloatInterval& AtitudeRange, const FVector& MeshLoc);
	bool UpdateSpawnableCollisions(FSpawnableMesh& Spawn, double& UpdateStartTime);
	bool ProcessSpawnablePending();

	EClipMapInteriorConfig RelativeLocationToParentInnerMeshConfig(FVector RelativeLocation);

	void UpdateParentInnerMesh(int ChildLevel, EClipMapInteriorConfig NewConfig, bool Segmented = false);

	UPROPERTY(Transient)
		FVector CamLocation = FVector(0.f);
	UPROPERTY(Transient)
		TArray<FVector> CameraLocations;
	UPROPERTY(Transient)
		TArray<USW_CollisionComponent*> External_Actors_Tracked;

	bool NewOriginRequestPending = false;
	FIntVector NewOriginRequested;
	
	uint8 OriginChange_Count=0;

public:
	UPROPERTY(Transient)
		USWorldSubsystem* SWorldSubsystem = nullptr;
protected:

	FRenderCommandFence RTUpdate;
	FConvexVolume ViewFrustum;

	FRenderCommandFence RT_UpdateRateEstimate;

	UPROPERTY(Transient)
		float RenderThreadUpdateRateDeviation = 0.0f;
	UPROPERTY(Transient)
		float RenderThreadUpdateRatePast = 0.0f;
	UPROPERTY(Transient)
		float RenderThreadUpdateRate = 0.0f;
	UPROPERTY(Transient)
		double TimeSecondsLastUpdate = 0.0f;
	UPROPERTY(Transient)
		bool Below30FPS_GPU = false;

	
	UPROPERTY(Transient)
		UCanvas* CollisionCanvas;
	UPROPERTY()
		FVector2D DrawMatSize;
	UPROPERTY()
		FDrawToRenderTargetContext Context;

	UPROPERTY(Transient)
		UCanvas* HeightmapCanvas;

	UPROPERTY(Transient)
		UCanvas* NormalCanvas;
	
	public:

		float HeightOnStart = 0.f;

		TSharedPtr<FSWShareableID, ESPMode::ThreadSafe> Shareable_ID;

	protected:

	struct FCollisionProcessingWork
	{
		int32 MeshID=-1;
		TSharedPtr<FSWColorRead, ESPMode::ThreadSafe> Read;
		TSharedPtr<FSWShareableVerticePositionBuffer, ESPMode::ThreadSafe> SourceB;
		TSharedPtr<FSWShareableVerticePositionBuffer, ESPMode::ThreadSafe> DestB;

		inline FCollisionProcessingWork(){}
		inline FCollisionProcessingWork(const int32 ID, const TSharedPtr<FSWColorRead>& R, const TSharedPtr<FSWShareableVerticePositionBuffer>& S, const TSharedPtr<FSWShareableVerticePositionBuffer>& D)
			:MeshID(ID),
			Read(R),
			SourceB(S),
			DestB(D)
		{}
	};

	TArray<FCollisionProcessingWork> CollisionWorkQueue;

	/** Allows skipping processing, if another one is already in progress */
	TSharedPtr < FThreadSafeBool, ESPMode::ThreadSafe> bProcessingGroundCollision;

	FThreadSafeBool BoundedWorldVisitorProcessing = false;

	UPROPERTY(Transient)
		float Debug_Timer_Acu = 0.0f;

	float MaxDrawDistanceScale_past=1.f;
	bool Segmented_Initialized=false;

	bool GenerateCollision_last = false;
	float VerticalRangeMeters_last = 0.f;

	int DrawCall_Spawnables_count = 0;
	int Spawnable_Stopped_indice=-1;
	
	bool SegmentedUpdateProcessed = true;
	bool SegmentedUpdateProcessedButSpawnablesLeft = false;

	UPROPERTY(Transient)
		TArray<int> SegmentedOnly_CollisionElementToUpdate;

	bool SegmentedUpdateProcessedButCollisionsLeft = false;


	friend USWCollectableInstancedSMeshComponent;


public:

	TSharedPtr<FSW_PointerTree<FSWQuadElement>> BoundedWorldTree = nullptr;
	uint64 TreeID = 0;

private:

	bool bHadGeneratorAtRebuildTime = false;

	bool CameraSet = false;
	double LatestRebuildTime = 0.0;

	double SpawnablesUpdateTime = 0.0;

	TArray<double> UpdateDelayForLODRuntime;

	FVector WorldLocationLastBuild = FVector(0.f);
	FVector WorldLocationLastMove = FVector(0.f);

	bool bDelayNextUpdate = false;
	double TimeAtDelayStart = 0.0;

	uint64 WorldCycle = 0;

	bool EditorInitialRenderThreadFence=false;
	double timerUntilVegetation=0.0;

};


/*
 * ShaderWorld: A procedural framework.
 * Website : https://www.shader.world/
 * Copyright (c) 2021-2023 MONSIEUR MAXIME DUPART
 *
 * This content is provided under the license of :
 * Epic Content License Agreement - https://www.unrealengine.com/en-US/eula/content
 *
 * You may not Distribute Licensed Content in source format to third parties except to employees,NormalMap
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

#include "Actor/ShaderWorldActor.h"

#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInstanceDynamic.h"
//#include "Materials/MaterialParameterCollectionInstance.h"
#include "Materials/MaterialParameterCollection.h"

#include "Engine/TextureRenderTarget2D.h"
#include "Async/ParallelFor.h"
#include "Async/Async.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Engine/AutoDestroySubsystem.h"
#include "AI/NavigationSystemBase.h"
#include "EngineModule.h"
#include "CanvasRender.h"
#include "Engine/Canvas.h"
#include "MeshPassProcessor.h"
#include "RawIndexBuffer.h"
#include "Engine/NetDriver.h"
#include "Engine/PackageMapClient.h"

#include "UnrealEngine.h" //cvar viewdistancescale

#include "Kismet/GameplayStatics.h"

#include "Component/SWFoliageInstancedSMeshComponent.h"
#include "Component/SW_CollisionComponent.h"

#include "FoliageType.h"
#include "FoliageType_InstancedStaticMesh.h"
#include "FoliageInstancedStaticMeshComponent.h"
#include "LandscapeGrassType.h"

#include "Engine/StaticMesh.h"
#include "Component/GeoClipmapMeshComponent.h"
#include "Component/ShaderWorldCollisionComponent.h"
#include "ConvexVolume.h"
#include "Actor/ShaderWorldBrushManager.h"


#include "HardwareInfo.h"
#include "ShaderCompiler.h"
#include "SWorldSubsystem.h"
#include "SWStats.h"
#include "DrawDebugHelpers.h"
#include "Component/SWSeedGenerator.h"
#include "Data/SWCacheManager.h"

#if INTEL_ISPC
#include "ShaderWorldActorISPC.ispc.generated.h"
#endif

#define LOCTEXT_NAMESPACE "ShaderWorld"


static TAutoConsoleVariable<int32> CVarSWBatchUpdateCount(
	TEXT("sw.BatchUpdateCount"),
	300,
	TEXT("Number of history frames to use for stats."));

static TAutoConsoleVariable<int32> CVarSWCollBatchUpdateCount(
	TEXT("sw.BatchCollUpdateCount"),
	75,
	TEXT("Number of history frames to use for stats."));


/** Single global instance of the SWClipMapBuffersHolder. */
TGlobalResource< FSWSimpleReadbackManager > GSWSimpleReadbackManager;


DEFINE_LOG_CATEGORY(LogShaderWorld);

/*
 * Below are some gamethread time budget available for specific components of a given Shader World
 */

/*
 * Computing the heightmaps and data layers
 */
static TAutoConsoleVariable<float> SWGameThreadBudgetSegmentedprocess_ms(
	TEXT("sw.GTStartSpawnablesBudget"),
	1.25,
	TEXT("Gamethread Time in ms allowed to spend generating Density map requests for Spawnables."));
/*
 * Updating the ground collision meshes
 */
static TAutoConsoleVariable<float> SWGameThreadBudgetCollision_ms(
	TEXT("sw.GTCollisionBudget"),
	1.0,
	TEXT("Gamethread Time in ms allowed to spend updating Ground collision meshes."));
/*
 * Updating spawnables (costly part is updating spawnables with collisions)
 */
static TAutoConsoleVariable<float> SWGameThreadBudgetFinalize_ms(
	TEXT("sw.GTEndSpawnablesBudget"),
	1.5,
	TEXT("Gamethread Time in ms allowed to spend updating Hierachical Instanced Static Meshes of spawnables, most expensive being collisions enabled ones."));

static int32 GSWMaxInstancesPerComponent = 16000;//36864;
static FAutoConsoleVariableRef CVarMaxInstancesPerComponent(
	TEXT("sw.Spawn.MaxInstancesPerComponent"),
	GSWMaxInstancesPerComponent,
	TEXT("Used to control the number of grass components created. More can be more efficient, but can be hitchy as new components come into range"));

static int32 GSWMaxCollisionInstancesPerComponent = 1500;//4096;
static FAutoConsoleVariableRef CVarMaxCollisionInstancesPerComponent(
	TEXT("sw.Spawn.MaxCollisionInstancesPerComponent"),
	GSWMaxCollisionInstancesPerComponent,
	TEXT("Used to control the number of grass components created. More can be more efficient, but can be hitchy as new components come into range"));




// Sets default values
AShaderWorldActor::AShaderWorldActor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

	RootComp = CreateDefaultSubobject<USWorldRoot>(TEXT("Root"));
	RootComponent = RootComp;
	RootComponent->SetMobility((EComponentMobility::Static));

#if WITH_EDITOR
	SetIsSpatiallyLoaded(false);
#endif

	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		ConstructorHelpers::FObjectFinder<UMaterial> DefaultGroundMat;
		ConstructorHelpers::FObjectFinder<UMaterial> DefaultGeneratorMat;
		ConstructorHelpers::FObjectFinder<UMaterial> DefaultSpawnablesMat;
		ConstructorHelpers::FObjectFinder<UMaterialInstanceConstant> DefaultCollisionMeshMat;
		ConstructorHelpers::FObjectFinder<UTexture2D> DefaultTileableNoiseTexture;
		FConstructorStatics()
			: DefaultGroundMat(TEXT("/ShaderWorld/Demo_Blank/Material/M_Blank_Material.M_Blank_Material"))
			, DefaultGeneratorMat(TEXT("/ShaderWorld/Demo_Blank/Material/M_Blank_Generator.M_Blank_Generator"))
			, DefaultSpawnablesMat(TEXT("/ShaderWorld/BP/ToolBox/Spawnables/M_SpawnDensity_Default.M_SpawnDensity_Default"))
			, DefaultCollisionMeshMat(TEXT("/ShaderWorld/BP/ToolBox/Collision/MT_CollisionHide_Inst.MT_CollisionHide_Inst"))
			, DefaultTileableNoiseTexture(TEXT("/ShaderWorld/BP/WorldGeneration/ImprovedPerlin/T_permuTexture2d.T_permuTexture2d"))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	Material = ConstructorStatics.DefaultGroundMat.Object;
	Generator = ConstructorStatics.DefaultGeneratorMat.Object;
	SpawnablesMat = ConstructorStatics.DefaultSpawnablesMat.Object;
	CollisionMat = ConstructorStatics.DefaultCollisionMeshMat.Object;
	TileableNoiseTexture = ConstructorStatics.DefaultTileableNoiseTexture.Object;
#if WITH_EDITORONLY_DATA
	bRunConstructionScriptOnDrag=false;
#endif
}

void FSWSimpleReadbackManager::TickReadBack()
{
	for (int32 RB_Index = PendingReads.Num() - 1; RB_Index >= 0; RB_Index--)
	{
		const FReadBackTask& RB = PendingReads[RB_Index];

		if (!RB.ReadStage.IsValid() || !RB.Destination.IsValid() || !RB.ProcessedStatus.IsValid())
		{
#if SWDEBUG
			SW_LOG("Error : Invalidated readback")
#endif
			PendingReads.RemoveAt(RB_Index);
			continue;
		}

		if(RB.ReadStage->IsReady())
		{
			void* Dest = RB.Destination->ReadData.GetData();
			int32 OutRowPitchInPixels;
			int32 BlockSize = RB.BlockSize;
			void* ResultsBuffer = RB.ReadStage->Lock(OutRowPitchInPixels);
			if (RB.ExtentX == OutRowPitchInPixels)
			{
				FPlatformMemory::Memcpy(Dest, ResultsBuffer, (RB.ExtentX * RB.ExtentY * BlockSize));
			}
			else
			{
				char* DestPtr = static_cast<char*>(Dest);
				const char* SrcPtr = static_cast<const char*>(ResultsBuffer);
				size_t Row = RB.ExtentY;
				while (Row--)
				{
					FMemory::Memcpy(DestPtr + RB.ExtentX * BlockSize * Row, SrcPtr + OutRowPitchInPixels * BlockSize * Row, RB.ExtentX * BlockSize);
				}
			}
			RB.ReadStage->Unlock();

			RB.ProcessedStatus.Get()->AtomicSet(true);

			PendingReads.RemoveAt(RB_Index);
		}
	}
}

void FSWSimpleReadbackManager::AddPendingReadBack(int32 InBlockSize, int32 InExtentX, int32 InExtentY, TSharedPtr<FRHIGPUTextureReadback>& InReadStage,
	TSharedPtr<FSWColorRead, ESPMode::ThreadSafe>& InDest,
	TSharedPtr<FThreadSafeBool, ESPMode::ThreadSafe>& InProc)
{
	FReadBackTask& Task = PendingReads.AddDefaulted_GetRef();
	Task = 
	{	InBlockSize,
		InExtentX,
		InExtentY,
		InReadStage,
		InDest,
		InProc
	};
}

void AShaderWorldActor::PostInitializeComponents()
{
	Super::PostInitializeComponents();

}

// Called when the game starts or when spawned
void AShaderWorldActor::BeginPlay()
{
	Super::BeginPlay();

	RHIString = "";
	CameraSet = false;

	if (UWorld* World = GetWorld())
	{
		SetN();
		//UpdateRenderAPI();

		CamLocation = GetActorLocation();
		CameraLocations.Empty();		
		CameraLocations.Add(CamLocation);
		CameraSet = true;

		RebuildCleanup();

		InitiateWorld();

		RTUpdate.BeginFence();
	}
	else
	{
#if SWDEBUG
		UE_LOG(LogTemp, Warning, TEXT("Beginplay: No World available"))
#endif
	}	
}

void AShaderWorldActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	FlushRenderingCommands();

	Super::EndPlay(EndPlayReason);
}

void AShaderWorldActor::ApplyWorldOffset(const FVector& InOffset, bool bWorldShift)
{	

	if (bWorldShift)
	{	

		// Shift UActorComponent derived components, but not USceneComponents
		for (UActorComponent* ActorComponent : GetComponents())
		{
			if (!ActorComponent)
			{
				continue;
			}
			if (IsValid(ActorComponent) && ActorComponent != GetRootComponent())
			{
				ActorComponent->ApplyWorldOffset(InOffset, bWorldShift);
			}
		}

		// Navigation receives update during component registration. World shift needs a separate path to shift all navigation data
		// So this normally should happen only in the editor when user moves visible sub-levels
		if (!bWorldShift && !InOffset.IsZero())
		{
			if (RootComponent != nullptr && RootComponent->IsRegistered())
			{
				FNavigationSystem::OnActorBoundsChanged(*this);
				FNavigationSystem::UpdateActorAndComponentData(*this);
			}
		}

		for (FClipMapMeshElement& Elem : Meshes)
		{
			if(Elem.MatDyn)
			{
				Elem.MatDyn->SetVectorParameterValue("WorldLocation", FVector(GetWorld()->RequestedOriginLocation));
			}
				

			if (Elem.CacheMatDyn)
			{
				Elem.CacheMatDyn->SetVectorParameterValue("WorldLocation", FVector(GetWorld()->RequestedOriginLocation));
			}				

			for(UMaterialInstanceDynamic* DynlayerMat : Elem.LayerMatDyn)
			{
				if(DynlayerMat)
					DynlayerMat->SetVectorParameterValue("WorldLocation", FVector(GetWorld()->RequestedOriginLocation));
			}
		}
		
		for(FCollisionMeshElement& CollisionElement : CollisionMesh)
		{
		
			if(CollisionElement.DynCollisionCompute)
				CollisionElement.DynCollisionCompute->SetVectorParameterValue("WorldLocation", FVector(GetWorld()->RequestedOriginLocation));
		}

	}
	else
	{
		Super::ApplyWorldOffset(InOffset, bWorldShift);	
	}
}


void AShaderWorldActor::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
	Super::AddReferencedObjects(InThis, Collector);

	const AShaderWorldActor* This = CastChecked<AShaderWorldActor>(InThis);

	if(!This)
	return;

	/*
 * For each LOD
 */
	for (const FClipMapPerLODCaches& PerLODCaches : This->LODCaches)
	{
		/*
		 * For each CacheGroup within a LOD
		 */
		for (const FSWTexture2DCacheGroup& CacheGroup : PerLODCaches.PerLODCacheGroup)
		{
			/*
			 * For each cache within a CacheGroup
			 */
			for (const auto& Caches : CacheGroup.PerLODCaches)
			{
				/*
				 * For each texture within a cache
				 */
				for (UTextureRenderTarget2D* Texture : Caches.Value.TextureCache)
				{
					SW_TOCOLLECTOR(Texture);
				}
			}
		}
	}


	SW_TOCOLLECTOR(This->Material)
	SW_TOCOLLECTOR(This->Generator)
	SW_TOCOLLECTOR(This->SpawnablesMat)
	SW_TOCOLLECTOR(This->CollisionMat)

	SW_TOCOLLECTOR(This->ReadRequestLocation)
	SW_TOCOLLECTOR(This->ReadRequestLocationHeightmap)
	SW_TOCOLLECTOR(This->GeneratorDynamicForReadBack)

	SW_TOCOLLECTOR(This->CollisionSampleLocation)

	for (const FCollisionMeshElement& el : This->CollisionMesh)
	{
		SW_TOCOLLECTOR(el.CollisionRT)
		SW_TOCOLLECTOR(el.CollisionRT_Duplicate)
		SW_TOCOLLECTOR(el.DynCollisionCompute)
		SW_TOCOLLECTOR(el.Mesh)
	}

	for (const FClipMapMeshElement& el : This->Meshes)
	{
		SW_TOCOLLECTOR(el.HeightMap)
		SW_TOCOLLECTOR(el.HeightMap_Segmented)
		SW_TOCOLLECTOR(el.NormalMap)
		SW_TOCOLLECTOR(el.NormalMap_Segmented)
		SW_TOCOLLECTOR(el.Mesh)
		SW_TOCOLLECTOR(el.MatDyn)
		SW_TOCOLLECTOR(el.CacheMatDyn)

		for (UTextureRenderTarget2D* el_rt : el.LandLayers)
		{
			SW_TOCOLLECTOR(el_rt)
		}
		for (UTextureRenderTarget2D* el_rt : el.LandLayers_Segmented)
		{
			SW_TOCOLLECTOR(el_rt)
		}
		for (UMaterialInstanceDynamic* el_DM : el.LayerMatDyn)
		{
			SW_TOCOLLECTOR(el_DM)
		}

		/*
		 * For each CacheGroup within a LOD
		 */
		for (const FSWTexture2DCacheGroup& CacheGroup : el.TransientCaches.PerLODCacheGroup)
		{
			/*
			 * For each cache within a CacheGroup
			 */
			for (const auto& Caches : CacheGroup.PerLODCaches)
			{
				/*
				 * For each texture within a cache
				 */
				for (UTextureRenderTarget2D* Texture : Caches.Value.TextureCache)
				{
					SW_TOCOLLECTOR(Texture);
				}
			}
		}
	}



	for (const FSWBiom& elB : This->Bioms)
	{
		for (const FSpawnableMesh& el : elB.Spawnables)
		{
			{
				for (const FSpawnableMeshElement& El : el.SpawnablesElem)
				{
					SW_TOCOLLECTOR(El.SpawnDensity)
					SW_TOCOLLECTOR(El.SpawnTransforms)
					SW_TOCOLLECTOR(El.ComputeSpawnTransformDyn)
				}
				for (USWHISMComponent* HISM : el.HIM_Mesh)
				{
					SW_TOCOLLECTOR(HISM)
				}
				for (USWHISMComponent* HISM : el.HIM_Mesh_Collision_enabled)
				{
					SW_TOCOLLECTOR(HISM)					
				}
			}
		}
	}
}

#if WITH_EDITOR
bool AShaderWorldActor::ShouldTickIfViewportsOnly() const
{
	return true;
}
#endif

const TCHAR* EnumToString(EClipMapInteriorConfig InCurrentState)
{
	switch (InCurrentState)
	{
	case EClipMapInteriorConfig::BotLeft:
		return TEXT("BotLeft");
	case EClipMapInteriorConfig::BotRight:
		return TEXT("BotRight");
	case EClipMapInteriorConfig::TopLeft:
		return TEXT("TopLeft");
	case EClipMapInteriorConfig::TopRight:
		return TEXT("TopRight");
	case EClipMapInteriorConfig::NotVisible:
		return TEXT("NotVisible");
	}
	ensure(false);
	return TEXT("Unknown");
}

void AShaderWorldActor::UpdateRenderAPI()
{
	if(!IsValid(SWorldSubsystem))
	{
		RHIString = "D3D11";
		RendererAPI = EGeoRenderingAPI::DX11;
#if SWDEBUG
		const FString APIDebugMessage = "AShaderWorldActor::UpdateRenderAPI() : Invalid Subsystem - default DX11 API config";
		if (GEngine)
			GEngine->AddOnScreenDebugMessage(-1, 15.0f, FColor::Yellow, APIDebugMessage);
#endif
	}

	if ((RHIString == ""))
	{
		RHIString = SWorldSubsystem->RHIString;
		RendererAPI = SWorldSubsystem->RendererAPI;

		if(SWorldSubsystem->TopologyFixUnderLOD==0)
			TopologyFixUnderLOD=0;
	}
}

void AShaderWorldActor::RebuildCleanup()
{
	bHadGeneratorAtRebuildTime = false;
	SegmentedUpdateProcessed = true;

	CollisionShareable = nullptr;

	ReadRequestLocation	=	nullptr;
	ReadRequestLocationHeightmap = nullptr;
	GeneratorDynamicForReadBack = nullptr;
	
	CollisionSampleLocation = nullptr;

	for (int i = Meshes.Num() - 1; i >= 0; i--)
	{
		FClipMapMeshElement& Elem = Meshes[i];

		if (Elem.Mesh)
		{
			Elem.Mesh->ClearAllMeshSections();
			Elem.Mesh->UnregisterComponent();
			Elem.Mesh->DestroyComponent();
			Elem.Mesh = nullptr;
		}

		Elem.HeightMap = nullptr;
		Elem.HeightMap_Segmented = nullptr;
		Elem.NormalMap = nullptr;
		Elem.NormalMap_Segmented = nullptr;		

		Elem.LandLayers.Empty();
		Elem.LandLayers_Segmented.Empty();
		Elem.LayerMatDyn.Empty();
		Elem.LandLayers_names.Empty();
		Elem.LandLayers_NeedParent.Empty();
	}

	Meshes.Empty();


	for (int i = CollisionMesh.Num() - 1; i >= 0; i--)
	{
		FCollisionMeshElement& Elem = CollisionMesh[i];
		if (Elem.Mesh)
		{
			Elem.Mesh->ClearAllMeshSections();

			if (GetNetMode() != ENetMode::NM_Standalone)
			{
				FString CompNape = "CollisionsToDestroy";
				FName UniqueN = MakeUniqueObjectName(Elem.Mesh->GetOuter(), Elem.Mesh->GetClass(), FName(CompNape));

				FString Rename_str = UniqueN.ToString();
				const ERenameFlags RenFlags = REN_NonTransactional | REN_DontCreateRedirectors | REN_ForceNoResetLoaders | REN_DoNotDirty;

				UObject* FoundExistingName = StaticFindObject(UShaderWorldCollisionComponent::StaticClass(), Elem.Mesh->GetOuter(), *Rename_str, true);
				FString Rename_str_invalid = Rename_str + "_invalid";
				if (FoundExistingName)
				{
					FoundExistingName->Rename(*Rename_str_invalid, FoundExistingName->GetOuter(), RenFlags);
				}
				Elem.Mesh->Rename(*Rename_str, Elem.Mesh->GetOuter(), RenFlags);
			}

			if (Elem.Mesh->IsRegistered())
				Elem.Mesh->UnregisterComponent();

			Elem.Mesh->DestroyComponent();
			Elem.Mesh = nullptr;
			Elem.CollisionRT = nullptr;
			Elem.CollisionRT_Duplicate = nullptr;
			Elem.DynCollisionCompute = nullptr;

		}

	}

	CollisionShareable = nullptr;
	CollisionMesh.Empty();
	UsedCollisionMesh.Empty();

	if (Shareable_ID.IsValid())
	{
		ENQUEUE_RENDER_COMMAND(ClearStaticBuffers)(
			[ID = Shareable_ID](FRHICommandListImmediate& RHICmdList)
			{
				check(IsInRenderingThread());
				GSWClipMapBufferHolder.DiscardBuffers(ID);
				GSWClipMapBufferCollisionHolder.DiscardBuffers(ID);
			});

		Shareable_ID.Reset();
	}

	InstanceCountUpdate = MaximumAmountofInstanceToUpdateonGameThread;

	for (FSWBiom& elB : Bioms)
	{
		for (FSpawnableMesh& el : elB.Spawnables)
		{
			el.CleanUp();
		}
		/*
		//No point computing further than we can see
		for (auto& El : elB.Spawnables)
		{
			if (El.CullDistance.Max > 0.f)
			{
				if (El.CullDistance.Max < 50000.f)
				{
					const float MminD = FMath::Clamp(El.CullDistance.Max * (1.0 + 4.0 / 100.0), El.CullDistance.Max, 50000.f);
					const float MmaxD = FMath::Clamp(El.CullDistance.Max * (1.0 + 15.0 / 100.0), El.CullDistance.Max, 50000.f);
					El.GridSizeMeters = FMath::Clamp(El.GridSizeMeters, MminD / 100.0, MmaxD / 100.0);
				}
				else
				{
					bool found = false;
					for (uint32 Subdi = 1; Subdi <= 30; Subdi++)
					{
						if (El.CullDistance.Max * (1.0 + 4.0 / 100.0) / Subdi <= 50000.f)
						{
							El.GridSizeMeters = El.CullDistance.Max / 100.0 * (1.0 + 4.0 / 100.0) / Subdi;
							found = true;
							break;
						}
					}
					if (!found)
						El.GridSizeMeters = 500.0f;
				}
			}

		}*/

		elB.SortedSpawnables.Empty();
		elB.SortedSpawnables_Collision.Empty();
	}

	rebuildVegetationOnly = false;

	CollisionToSpawnable.Empty();
	if (BrushManager)
		BrushManager->ResetB();

	if (BoundedWorldTree)
	{
		BoundedWorldTree = nullptr;
	}
	BoundedWorldVisitorProcessing = false;

	rebuild = false;
	GenerateCollision_last = GenerateCollision;
	VerticalRangeMeters_last = VerticalRangeMeters;
}

bool AShaderWorldActor::Setup()
{
	if(!GetWorld() || GetWorld() && !GetWorld()->bIsWorldInitialized)
	{
#if SWDEBUG
		UE_LOG(LogTemp, Warning, TEXT("Setup Fail: !GetWorld()"))
#endif
		return false;
	}

	if (GShaderCompilingManager && GShaderCompilingManager->IsCompiling())
	{
		#if SWDEBUG
		FString CompilingMessage = "Shaders Compiling, Shader World update Frozen";
		if(GEngine)
			GEngine->AddOnScreenDebugMessage(INDEX_NONE, 0.f, FColor::Blue, CompilingMessage,false);
		#endif
		
		return false;
	}

	if (USWorldSubsystem* ShaderWorldSubsystem = SWorldSubsystem)
	{
		if (!ShaderWorldSubsystem->IsReadyToHandleGPURequest())
		{
#if SWDEBUG
			UE_LOG(LogTemp, Warning, TEXT("Setup Fail: !ShaderWorldSubsystem->IsReadyToHandleGPURequest()"))
#endif
			return false;
		}			

		ShaderWorldSubsystem->SetCameraUpdateRate(UpdateRateCameras);
	}
	else
	{
		return false;
	}

	if(!RootComponent)
		return false;

#if WITH_EDITOR
	if (GetIsSpatiallyLoaded())
		SetIsSpatiallyLoaded(false);
#endif

	SetN();
	UpdateRenderAPI();

	if(EditRebuildVegetation)
	{
		EditRebuildVegetation.AtomicSet(false);
		rebuildVegetationOnly = true;
	}

	

	if(MaxDrawDistanceScale_past!=GetCachedScalabilityCVars().ViewDistanceScale)
	{
		MaxDrawDistanceScale_past=GetCachedScalabilityCVars().ViewDistanceScale;
		rebuildVegetationOnly=true;
	}

	if(GetWorld()->OriginLocation.Size()==0)
		HeightOnStart=GetActorLocation().Z;

	TimeAcu = 0.f;
	DrawCall_Spawnables_count = 0;

	UpdateViewFrustum();

	UpdateCameraLocation();

	if(UpdatePatchData)
	{
		for(auto& Mesh: Meshes)
		{
			if(Mesh.Mesh)
			{
				Mesh.Mesh->UpdatePatchDataLODSmoothTransition(TransitionWidth);
			}
		}
		UpdatePatchData.AtomicSet(false);
	}

	if ((Meshes.Num() > 0) && (GenerateCollision_last != GenerateCollision || VerticalRangeMeters_last != VerticalRangeMeters))
	{
		rebuild = true;
	}
		


	if((Meshes.Num()>0) && ((GetActorLocation() - WorldLocationLastBuild).Length() > 0.f))
	{		
		rebuild = true;
		bDelayNextUpdate = true;
		TimeAtDelayStart = 0.0;		
		
		FVector DeltaLoc = FVector(0.f,0.f,(GetActorLocation() - WorldLocationLastBuild).Z);
		WorldLocationLastMove = GetActorLocation();

		for(auto& el: Meshes)
		{
			if(el.Mesh)
				el.Mesh->AddWorldOffset(DeltaLoc,false,nullptr,ETeleportType::TeleportPhysics);

			el.Location = el.Location + FIntVector(0,0,DeltaLoc.Z);
		}
	}

	if (rebuild)
	{
		RebuildCleanup();		
	}

	if(rebuildVegetationOnly)
	{		
		for (FSWBiom& elB : Bioms)
		{
			for (FSpawnableMesh& el : elB.Spawnables)
			{
				el.CleanUp();
			}

			//No point computing further than we can see
			/*
			for (auto& El : elB.Spawnables)GridSizeMeters
			{
				if (El.CullDistance.Max > 0.f)
				{
					if (El.CullDistance.Max < 50000.f)
					{
						const float MminD = FMath::Clamp(El.CullDistance.Max * (1.0 + 4.0 / 100.0), El.CullDistance.Max, 50000.f);
						const float MmaxD = FMath::Clamp(El.CullDistance.Max * (1.0 + 15.0 / 100.0), El.CullDistance.Max, 50000.f);
						El.GridSizeMeters = FMath::Clamp(El.GridSizeMeters, MminD / 100.0, MmaxD / 100.0);
					}
					else
					{
						bool found=false;
						for (uint32 Subdi = 1; Subdi <= 30; Subdi++)
						{
							if (El.CullDistance.Max * (1.0 + 4.0 / 100.0) / Subdi <= 50000.f)
							{
								El.GridSizeMeters = El.CullDistance.Max / 100.0 * (1.0 + 4.0 / 100.0) / Subdi;
								found=true;
								break;
							}
						}
						if(!found)
							El.GridSizeMeters = 500.0f;
					}
				}

			}*/

			elB.SortedSpawnables.Empty();
			elB.SortedSpawnables_Collision.Empty();
		}

		rebuildVegetationOnly=false;
	}


	if (RHIString == "")
		return false;

	return true;
}


bool AShaderWorldActor::ProcessSegmentedComputation()
{	
	SW_FCT_CYCLE()

	if(!UseSegmented() || rebuild)
	{
		NeedSegmentedUpdate.Empty();
		ClipMapToUpdateAndMove.Empty();
		ClipMapToUpdate.Empty();
		SegmentedUpdateProcessed = true;
		SegmentedUpdateProcessedButSpawnablesLeft=false;		

		return true;
	}

	if(!SegmentedFence.IsFenceComplete())
		return false;

	const double SegmentedComputationStart = FPlatformTime::Seconds();

	if(SegmentedUpdateProcessed && SegmentedFence.IsFenceComplete())
	{
		const double GameThreadBudget_ms = SWGameThreadBudgetSegmentedprocess_ms.GetValueOnGameThread();

		if(SegmentedUpdateProcessedButSpawnablesLeft)
		{
			SegmentedUpdateProcessedButSpawnablesLeft=false;
			bool launched = false;
			for (FSWBiom& elB : Bioms)
			{
				for (FSpawnableMesh& Spawn : elB.Spawnables)
				{
					if(launched && (((FPlatformTime::Seconds() - SegmentedComputationStart) * 1000.0) > GameThreadBudget_ms))
					{
						SegmentedUpdateProcessedButSpawnablesLeft=true;
						RTUpdate.BeginFence();
						return false;
					}
					while(Spawn.SegmentedOnly_ElementToUpdateData.Num()>0)
					{
						int32 ID = Spawn.SegmentedOnly_ElementToUpdateData.Pop();

						if (ID >= Spawn.SpawnablesElem.Num())
						{
							SW_LOG("ERROR ShaderWorld, ProcessSegmentedComputation SegmentedUpdateProcessedButSpawnablesLeft  : ID >= Spawn.SpawnablesElem.Num()")
							rebuild = true;
							return true;
						}

						Spawn.SegmentedOnly_ElementToUpdateData.Remove(ID);

						FSpawnableMeshElement& El = Spawn.SpawnablesElem[ID];

						Spawn.UpdateSpawnableData(elB, El);
						launched = true;						
					}
					/*
					for (int& ID : Spawn.SegmentedOnly_ElementToUpdateData)
					{
						if (ID >= Spawn.SpawnablesElem.Num())
						{
							SW_LOG("ERROR ShaderWorld, ProcessSegmentedComputation SegmentedUpdateProcessedButSpawnablesLeft  : ID >= Spawn.SpawnablesElem.Num()")
							rebuild = true;
							return true;
						}

						FSpawnableMeshElement& El = Spawn.SpawnablesElem[ID];
						
						Spawn.UpdateSpawnableData(elB,El);
						launched = true;
					}*/
					Spawn.SegmentedOnly_ElementToUpdateData.Empty();					
				}
			}

			if (launched)			
				RTUpdate.BeginFence();		

		}
		
		return true;
	}
	

	{
		for (int i = 0; i < Meshes.Num(); i++)
		{
			ensure(i < NeedSegmentedUpdate.Num());

			if(NeedSegmentedUpdate[i])
			{	
				
				ComputeHeight_Segmented_MapForClipMap(i);
				ComputeSegmentedNormalForClipMap(i);
				ComputeDataLayersSegmentedForClipMap(i);

				NeedSegmentedUpdate[i]=false;

				SegmentedFence.BeginFence();

				return false;
			}
		}		
		

		for (int i = Meshes.Num()-1; i >=0; i--)
		{
			FClipMapMeshElement& Elem = Meshes[i];

			bool AtLeastOneSectionVisible = false;

			for (int j = 0; j < 6; j++)
			{
				if (Elem.IsSectionVisible(j) != Elem.IsSectionVisible(j, true))
					Elem.SetSectionVisible(j, Elem.IsSectionVisible(j, true));

				AtLeastOneSectionVisible |= Elem.IsSectionVisible(j);
			}			

			ensure(i < ClipMapToUpdateAndMove.Num());

			if (!AtLeastOneSectionVisible)
				continue;

			if (ClipMapToUpdateAndMove[i])
			{							
				MoveClipMapMaterialLocation(i);

				if (bHadGeneratorAtRebuildTime)
				{
					if (Elem.CacheMatDyn)
					{
						CopyHeightmapSegmentedToHeightMap(i);
						CopyNormalMapSegmentedToNormalMap(i);
						CopyLayersSegmentedToLayers(i);
						TopologyUpdate(i);					
					}
					else
					{
						UE_LOG(LogTemp, Warning, TEXT("ERROR Recreating the clipmap cache computation materials - should not be happening"));
					}
				}

				
				ClipMapToUpdateAndMove[i] = false;

				ClipMapToUpdate[i] = false;
			}

			if (ClipMapToUpdate[i])
			{
				if (bHadGeneratorAtRebuildTime)
				{
					CopyHeightmapSegmentedToHeightMap(i);
					CopyNormalMapSegmentedToNormalMap(i);
					CopyLayersSegmentedToLayers(i);
					TopologyUpdate(i);
				}			

				ClipMapToUpdate[i] = false;
			}

		}

		if (DataReceiver)
			DataReceiver->UpdateStaticDataFor(this, CamLocation);

		/*
		 * All Heightmap Normal and Layers are computed past this point
		 */
		for (FSWBiom& elB : Bioms)
		{
			for (FSpawnableMesh& Spawn : elB.Spawnables)
			{
				while (Spawn.SegmentedOnly_ElementToUpdateData.Num() > 0)
				{
					int32 ID = Spawn.SegmentedOnly_ElementToUpdateData.Pop();

					ensure(ID < Spawn.SpawnablesElem.Num());

					Spawn.SegmentedOnly_ElementToUpdateData.Remove(ID);

					FSpawnableMeshElement& El = Spawn.SpawnablesElem[ID];

					Spawn.UpdateSpawnableData(elB, El);					
				}

				Spawn.SegmentedOnly_ElementToUpdateData.Empty();
			}
		}

		
		SegmentedUpdateProcessedButSpawnablesLeft=false;
		RTUpdate.BeginFence();

		SegmentedUpdateProcessed = true;

		SegmentedFence.BeginFence();
	}

	return false;
}

// Called every frame
void AShaderWorldActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	SW_FCT_CYCLE("Geo World Tick")

	/*
	 * Can we use "DrawMaterialToRenderTarget" ?
	 */
	if (!FApp::CanEverRender())	{return;}

	/*
	 * Can we use Compute Shaders ?
	 */
	if(!SWorldSubsystem)
		SWorldSubsystem = GetWorld()->GetSubsystem<USWorldSubsystem>();

	if (SWorldSubsystem)
	{
		if (!SWorldSubsystem->IsReadyToHandleGPURequest())
			return;
	}
	else
		return;

	/*
	 * Prevent rebuilding the Shader World every frame when we move it around.
	 */
	if(bDelayNextUpdate)
	{
		TimeAtDelayStart += DeltaTime;

		if(TimeAtDelayStart<1.0)
		{
			if(abs((GetActorLocation() - WorldLocationLastMove).Z)>0.f)
			{
				FVector DeltaLoc = FVector(0.f, 0.f, (GetActorLocation() - WorldLocationLastMove).Z);
				WorldLocationLastMove = GetActorLocation();

				for (auto& el : Meshes)
				{
					if (el.Mesh)
						el.Mesh->AddWorldOffset(DeltaLoc, false, nullptr, ETeleportType::TeleportPhysics);

					el.Location = el.Location + FIntVector(0, 0, DeltaLoc.Z);
				}
			}			
			return;
		}			
		bDelayNextUpdate=false;
		TimeAtDelayStart=0.0;
	}

	ReadbacksManagement();

	CollisionManagement(DeltaTime);

	SpawnablesManagement(DeltaTime);

	TerrainAndSpawnablesManagement(DeltaTime);
}

bool AShaderWorldActor::UpdateCollisionInRegion(const FBox Area)
{
	CollisionUpdateRequest.Enqueue(FBox2D(FVector2D(Area.Min), FVector2D(Area.Max)));

	return true;
}


void AShaderWorldActor::ReadbacksManagement()
{
	if(!bProcessingHeightRetrieval.IsValid())
		bProcessingHeightRetrieval = MakeShared<FThreadSafeBool, ESPMode::ThreadSafe>();
	if (!bProcessingHeightRetrievalRT.IsValid())
		bProcessingHeightRetrievalRT = MakeShared<FThreadSafeBool, ESPMode::ThreadSafe>();

	if((*bProcessingHeightRetrieval.Get()) && (*bProcessingHeightRetrievalRT.Get()) && HeightReadBackFence.IsFenceComplete() && PointsPendingReadBacks.IsValid())
	{
		
		if (!ReadBackHeightData.IsValid())
			return;

		//For now we do Compute samples on a rendertarget 5x5, therefore 25 positions evaluated per request.
		const int NumOfVertex = 25;

		PositionsOfReadBacks.Empty();
		PositionsOfReadBacks.AddUninitialized(25);

		uint8* ReadData8 = (uint8*)ReadBackHeightData->ReadData.GetData();

		uint16 MaterialIndice = 0;

		for (int32 k = 0; k < NumOfVertex; k++)
		{			
			FVector3f& PositionSample = PositionsOfReadBacks[k];

			if (RendererAPI == EGeoRenderingAPI::OpenGL)
			{
				int X = k % 5;
				int Y = k / 5;

				int index = X + Y * 5;

				Y = (5 - 1) - Y;

				index = X + Y * 5;

				PositionSample = FVector3f(PointsPendingReadBacks->PositionsXY[2 * k], PointsPendingReadBacks->PositionsXY[2 * k + 1], GetHeightFromGPURead(&ReadData8[index*4], MaterialIndice) / HeightScale);
			}
			else
				PositionSample = FVector3f(PointsPendingReadBacks->PositionsXY[2 * k], PointsPendingReadBacks->PositionsXY[2 * k + 1], GetHeightFromGPURead(&ReadData8[k*4], MaterialIndice) / HeightScale);

		}

		HeightRetrieveDelegate.ExecuteIfBound(PositionsOfReadBacks);

		bProcessingHeightRetrieval->AtomicSet(false);
		bProcessingHeightRetrievalRT->AtomicSet(false);
	}


}

bool AShaderWorldActor::RetrieveHeightAt(const TArray<FVector>& Origin, const FSWHeightRetrievalDelegate& Callback)
{
	if(!GeneratorDynamicForReadBack || !SWorldSubsystem)
		return false;

	if (!bProcessingHeightRetrieval.IsValid())
	{
		bProcessingHeightRetrieval = MakeShared<FThreadSafeBool, ESPMode::ThreadSafe>();
		bProcessingHeightRetrieval->AtomicSet(false);
	}
	if (!bProcessingHeightRetrievalRT.IsValid())
	{
		bProcessingHeightRetrievalRT = MakeShared<FThreadSafeBool, ESPMode::ThreadSafe>();
		bProcessingHeightRetrievalRT->AtomicSet(false);
	}
			

	if(!(*bProcessingHeightRetrieval.Get()) && ReadRequestLocation && ReadRequestLocationHeightmap && GeneratorDynamicForReadBack)
	{
		bProcessingHeightRetrieval->AtomicSet(true);
		bProcessingHeightRetrievalRT->AtomicSet(false);
		HeightRetrieveDelegate = Callback;

		PointsPendingReadBacks = MakeShared<FSWShareableSamplePoints, ESPMode::ThreadSafe>();
		TSharedPtr<FSWShareableSamplePoints>& Samples = PointsPendingReadBacks;

		FBox BoundingBoxRead(Origin);		

		Samples->PositionsXY.SetNum(25*2);
		for(int i=0; i<25;i++)
		{
			if(i< Origin.Num())
			{
				Samples->PositionsXY[i*2] = Origin[i].X;
				Samples->PositionsXY[i*2+1] = Origin[i].Y;
			}
			else
			{
				Samples->PositionsXY[i*2] = 0.f;
				Samples->PositionsXY[i*2+1] = 0.f;
			}
		}

		if (USWorldSubsystem* ShaderWorldSubsystem = SWorldSubsystem)
		{
			ShaderWorldSubsystem->LoadSampleLocationsInRT(ReadRequestLocation, Samples);

#if SW_COMPUTE_GENERATION
			
#else
			UKismetRenderingLibrary::DrawMaterialToRenderTarget(this, ReadRequestLocationHeightmap, GeneratorDynamicForReadBack);
#endif

			int32 Size_RT_Readback = ReadRequestLocationHeightmap.Get()->SizeX;

			FVector Barycentre = BoundingBoxRead.GetCenter();
			FVector Extent = BoundingBoxRead.GetExtent();
			float gridspacing = Extent.X*2.0/ (Size_RT_Readback-1);

			if (BrushManager)
				BrushManager->ApplyBrushStackToHeightMap(this, 0, ReadRequestLocationHeightmap.Get(), Barycentre, gridspacing, Size_RT_Readback, false,true, ReadRequestLocation.Get());


			ReadBackHeightData = MakeShared<FSWColorRead, ESPMode::ThreadSafe>();
			ReadBackHeightData->ReadData.SetNum(25);

			ENQUEUE_RENDER_COMMAND(ReadGeoClipMapRTCmd)(
				[InRT = ReadRequestLocationHeightmap, HeightData = ReadBackHeightData, Completion = bProcessingHeightRetrievalRT](FRHICommandListImmediate& RHICmdList)
				{
					check(IsInRenderingThread());

					if (HeightData.IsValid() && InRT->GetResource())
					{						
						FRDGBuilder GraphBuilder(RHICmdList);

						TSharedPtr<FRHIGPUTextureReadback> ReadBackStaging = MakeShared<FRHIGPUTextureReadback>(TEXT("SWGPUTextureReadback"));

						FRDGTextureRef RDGSourceTexture = RegisterExternalTexture(GraphBuilder, InRT->GetResource()->TextureRHI, TEXT("SWSourceTextureToReadbackTexture"));

						AddEnqueueCopyPass(GraphBuilder, ReadBackStaging.Get(), RDGSourceTexture);

						GraphBuilder.Execute();

						GSWSimpleReadbackManager.AddPendingReadBack(GPixelFormats[RDGSourceTexture->Desc.Format].BlockBytes, RDGSourceTexture->Desc.Extent.X, RDGSourceTexture->Desc.Extent.Y,ReadBackStaging, const_cast<TSharedPtr<FSWColorRead, ESPMode::ThreadSafe>&>(HeightData), const_cast<TSharedPtr < FThreadSafeBool, ESPMode::ThreadSafe>&>(Completion));

					}

				});

			HeightReadBackFence.BeginFence();
		}

		return true;
	}

	return false;	
}

float AShaderWorldActor::Get_TerrainPatchSize(int LOD)
{
	if (Meshes.Num() > 0 && LOD >= 0 && LOD < Meshes.Num())
	{
		return Meshes[Meshes.Num() - 1 - LOD].GridSpacing*(N-1.f);
	}
	return 1.f;
}

FIntVector AShaderWorldActor::Get_LOD_RingLocation(int LOD)
{
	if (Meshes.Num() > 0 && LOD >= 0 && LOD < Meshes.Num())
	{		
		return Meshes[Meshes.Num() - 1 - LOD].Location;
	}
	return FIntVector();
}

void AShaderWorldActor::AssignHeightMapToDynamicMaterial(const int LOD, UMaterialInstanceDynamic* MatDyn_,
	 FName ParameterName)
{
	if (MatDyn_ && Meshes.Num() > 0 && LOD >= 0 && LOD < Meshes.Num())
	{
		MatDyn_->SetTextureParameterValue(ParameterName, Meshes[Meshes.Num() - 1 - LOD].HeightMap);		
	}
}

UTextureRenderTarget2D* AShaderWorldActor::Get_LOD_HeightMap(int LOD)
{
	if (Meshes.Num() > 0 && LOD >= 0 && LOD < Meshes.Num())
	{
		return Meshes[Meshes.Num() - 1 - LOD].HeightMap;
	}
	return nullptr;
}

UTextureRenderTarget2D* AShaderWorldActor::Get_LOD_HeightMap_Segmented(int LOD)
{
	if (Meshes.Num() > 0 && LOD >= 0 && LOD < Meshes.Num())
	{
		return Meshes[Meshes.Num() - 1 - LOD].HeightMap_Segmented;
	}
	return nullptr;
}

UTextureRenderTarget2D* AShaderWorldActor::Get_LOD_NormalMap(int LOD)
{
	if (Meshes.Num() > 0 && LOD >= 0 && LOD < Meshes.Num())
	{
		return Meshes[Meshes.Num() - 1 - LOD].NormalMap;
	}
	return nullptr;
}

UMaterialInstanceDynamic* AShaderWorldActor::Get_LOD_DynamicGeneratorMaterial(int LOD)
{
	if (Meshes.Num() > 0 && LOD >= 0 && LOD < Meshes.Num())
	{
		return Meshes[Meshes.Num() - 1 - LOD].CacheMatDyn;
	}
	return nullptr;
}

float AShaderWorldActor::Get_LOD_Dimension(int LOD)
{
	if (Meshes.Num() > 0 && LOD >= 0 && LOD < Meshes.Num())
	{
		return Meshes[Meshes.Num() - 1 - LOD].GridSpacing*(N-1);
	}
	return -1.f;
}

void AShaderWorldActor::RequestNewOrigin(FIntVector NewWorldOrigin)
{
	if(!NewOriginRequestPending)
	{
		NewOriginRequestPending=true;
		NewOriginRequested=NewWorldOrigin;
	}
}

void AShaderWorldActor::UpdateStaticDataFor(AShaderWorldActor* Source_, FVector& CamLocationSource)
{
	SW_FCT_CYCLE()

	if(!Source_ || Source_!=DataSource || Source_ && Source_->GetMeshNum()==0)
		return;

	const int SourceMaxLOD = Source_->LOD_Num-1;

	CamLocation = CamLocationSource;
	CameraSet = true;

	for(FClipMapMeshElement& el : Meshes)
	{
		const int el_LOD = LOD_Num-1-el.Level;

		if (el.MatDyn)
		{
			const int SourceLOD_tolookFor = el_LOD + LOD_Offset_FromReceiverToSource > SourceMaxLOD ? SourceMaxLOD : el_LOD+LOD_Offset_FromReceiverToSource;

			for(int i=SourceLOD_tolookFor; i < Source_->LOD_Num;i++)
			{
			
				if(Source_->LOD_Num-1 - i < Source_->GetMeshNum())
				{
					const FClipMapMeshElement& ClipMMesh = Source_->GetMesh(Source_->LOD_Num-1 - i);

					if(ClipMMesh.Mesh->IsMeshSectionVisible(0)||ClipMMesh.Mesh->IsMeshSectionVisible(1))
					{
						const int CacheResExt = ClipMMesh.NormalMap->SizeX;

						if(el.HeightMapFromLastSourceElement && el.HeightMapFromLastSourceElement==ClipMMesh.HeightMap)
						{
							el.CacheMatDyn->SetVectorParameterValue("Ext_RingLocation", FVector(ClipMMesh.Location));							

							el.MatDyn->SetVectorParameterValue("Ext_RingLocation", FVector(ClipMMesh.Location));
						}
						else
						{
							
							el.CacheMatDyn->SetVectorParameterValue("Ext_RingLocation", FVector(ClipMMesh.Location));
							el.CacheMatDyn->SetScalarParameterValue("Ext_MeshScale", (Source_->N - 1) * ClipMMesh.GridSpacing * CacheResExt / (CacheResExt - 1));
							el.CacheMatDyn->SetScalarParameterValue("Ext_N", Source_->N);
							el.CacheMatDyn->SetScalarParameterValue("Ext_LocalGridScaling", ClipMMesh.GridSpacing);
							el.CacheMatDyn->SetScalarParameterValue("Ext_CacheRes", CacheResExt);

							el.CacheMatDyn->SetTextureParameterValue("Ext_HeightMap", ClipMMesh.HeightMap);
							el.CacheMatDyn->SetTextureParameterValue("Ext_NormalMap", ClipMMesh.NormalMap);

							el.MatDyn->SetVectorParameterValue("Ext_RingLocation", FVector(ClipMMesh.Location));
							el.MatDyn->SetScalarParameterValue("Ext_MeshScale", (Source_->N - 1) * ClipMMesh.GridSpacing * CacheResExt / (CacheResExt - 1));
							el.MatDyn->SetScalarParameterValue("Ext_N", Source_->N);
							el.MatDyn->SetScalarParameterValue("Ext_LocalGridScaling", ClipMMesh.GridSpacing);
							el.MatDyn->SetScalarParameterValue("Ext_CacheRes", CacheResExt);

							el.MatDyn->SetTextureParameterValue("Ext_HeightMap", ClipMMesh.HeightMap);
							el.MatDyn->SetTextureParameterValue("Ext_NormalMap", ClipMMesh.NormalMap);
						}						

						break;
					}

					
				}

			}
		
		}
	}
}

void AShaderWorldActor::ReceiveExternalDataUpdate(AShaderWorldActor* Source, int LOD_, FVector NewLocation)
{
	if(Source && DataSource && Source==DataSource && Meshes.Num()>0)
	{
		const int SourceMaxLOD = Source->LOD_Num-1;

		for (FClipMapMeshElement& el : Meshes)
		{
			const int el_LOD = LOD_Num - 1 - el.Level;

			if (el.MatDyn)
			{
				const int SourceLOD_tolookFor = el_LOD + LOD_Offset_FromReceiverToSource > SourceMaxLOD ? SourceMaxLOD : el_LOD + LOD_Offset_FromReceiverToSource;

				if(SourceLOD_tolookFor == LOD_)
				{
					el.MatDyn->SetVectorParameterValue("Ext_RingLocation", NewLocation);
				}

			}
		}
	}
}

FBox2D AShaderWorldActor::GetHighestLOD_FootPrint()
{
	if(GetMeshNum()>0)
	{	
		const FVector2D Location(Meshes[0].Location.X, Meshes[0].Location.Y);
		const float Size = Meshes[0].GridSpacing * (N - 1) / 2.0;

		const FBox2D FootPrintRT(Location - Size * FVector2D(1.f, 1.f), Location + Size * FVector2D(1.f, 1.f));
		return FootPrintRT;
	}

	return FBox2D(ForceInit);
}

bool AShaderWorldActor::HighestLOD_Visible()
{
	if (GetMeshNum() > 0)
	{
		return Meshes[0].IsSectionVisible(0)||Meshes[0].IsSectionVisible(1);
	}

	return false;
}


void AShaderWorldActor::UnregisterAllComponents(bool bForReregister)
{
	if (!PreventReRegistration)
	{
		Super::UnregisterAllComponents(bForReregister);
	}	
}

void AShaderWorldActor::PostUnregisterAllComponents()
{
	if (!PreventReRegistration)
	{
		Super::PostUnregisterAllComponents();
	}
}

void AShaderWorldActor::ReregisterAllComponents()
{
	if (!PreventReRegistration)
	{
		Super::ReregisterAllComponents();
	}
}

#if WITH_EDITOR

void AShaderWorldActor::PreEditChange(FProperty* PropertyThatWillChange)
{
	PreventReRegistration = true;

	if (PropertyThatWillChange)
	{
		const FString PropName = PropertyThatWillChange->GetName();		

		if (PropName == TEXT("TransitionWidth") ||
			PropName == TEXT("UpdateRateCameras"))
		{
			RuntimePropertyEditing = true;
		}		

		if (PropName == TEXT("Spawnables") ||
			PropName == TEXT("Bioms") ||
			PropName == TEXT("GridSizeMeters") ||
			PropName == TEXT("Density") ||
			PropName == TEXT("SpawnType") ||
			PropName == TEXT("Mesh") ||
			PropName == TEXT("GrassType") ||
			PropName == TEXT("Foliages") ||
			PropName == TEXT("Actors") ||
			PropName == TEXT("NoPoppingRange") ||
			PropName == TEXT("MaximumAmountofInstanceToUpdateonGameThread") ||

			PropName == TEXT("bResourceCollectable") ||
			PropName == TEXT("ResourceName") ||
			PropName == TEXT("CollisionEnabled") ||
			PropName == TEXT("CollisionChannel") ||
			PropName == TEXT("CollisionProfile") ||
			PropName == TEXT("CastShadows") ||

			PropName == TEXT("AlignMaxAngle") ||
			PropName == TEXT("AltitudeRange") ||
			PropName == TEXT("AlignToTerrainSlope") ||
			PropName == TEXT("VerticalOffsetRange") ||
			PropName == TEXT("ScaleRange") ||
			PropName == TEXT("GroundSlopeAngle") ||
			PropName == TEXT("Max") ||
			PropName == TEXT("Min") ||

			PropName == TEXT("bAffectDynamicIndirectLighting") ||
			PropName == TEXT("bAffectDistanceFieldLighting") ||
			PropName == TEXT("bCastShadowAsTwoSided") ||
			PropName == TEXT("bReceivesDecals") ||

			PropName == TEXT("NumberGridRings") ||
			PropName == TEXT("FoliageComponent") ||
			PropName == TEXT("CollisionOnlyAtProximity")
			)
		{
			PreventReRegistration = true;
		}
		else if (PropName == TEXT("VerticePerPatch") ||
			PropName == TEXT("LOD_Num") ||
			PropName == TEXT("Material") ||
			PropName == TEXT("Generator") ||
			PropName == TEXT("ClipMapCacheIntraVerticesTexel") ||
			PropName == TEXT("WorldDimensionMeters") ||
			PropName == TEXT("LandDataLayers") ||
			PropName == TEXT("HeightScale") ||

			PropName == TEXT("LOD_above_doubleCacheResolution") ||

			PropName == TEXT("WorldShape") ||
			PropName == TEXT("PlanetRadiusKm") ||
			PropName == TEXT("RootComp") ||

			PropName == TEXT("WorldHasBounds") ||

			PropName == TEXT("ShadowCastingRange") ||
			PropName == TEXT("DrawDistance") ||
			PropName == TEXT("TerrainResolution") ||

			PropName == TEXT("GenerateCollision") ||
			PropName == TEXT("CollisionChannel") ||
			PropName == TEXT("TopologyFixUnderLOD") ||
			PropName == TEXT("EnableWorldPositionOffsetUnderLOD") ||
			

			PropName == TEXT("BrushManager") ||

			PropName == TEXT("CollisionGridRingNumber") ||

			PropName == TEXT("UpdateHeightDataOverTime") ||
			PropName == TEXT("UpdateRateHeight") ||
			PropName == TEXT("UpdateRateDegradationFactor") ||


			PropName == TEXT("CollisionResolution") ||
			PropName == TEXT("CollisionVerticesPerPatch") ||

			PropName == TEXT("ExportMaterial") ||
			PropName == TEXT("DataLayerWithMaterialID") ||
			PropName == TEXT("ChannelOfMaterialID") ||
			PropName == TEXT("CollisionVisible") ||

			PropName == TEXT("WorldSeeds") ||
			PropName == TEXT("SeedName") ||
			PropName == TEXT("Seed") ||
			PropName == TEXT("SmoothLODTransition") ||
			PropName == TEXT("TransitionWidth") ||
			PropName == TEXT("AltitudeToLODTransition") ||
			PropName == TEXT("SeedGenerator") ||
			PropName == TEXT("SeedRange")
			)
		{
			RuntimePropertyEditing = true;
			PreventReRegistration = true;
		}
	}

	if(!RuntimePropertyEditing)
		Super::PreEditChange(PropertyThatWillChange);			
}

void AShaderWorldActor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{	
	FProperty* Property = PropertyChangedEvent.MemberProperty;


	if (Property && PropertyChangedEvent.ChangeType != EPropertyChangeType::Interactive)
	{

		FString PropName = PropertyChangedEvent.Property->GetName();

		//rebuildVegetationOnly = true;
		

		if(	PropName == TEXT("Spawnables") || 
			PropName == TEXT("Bioms") ||
			PropName == TEXT("GridSizeMeters")|| 
			PropName == TEXT("Density")|| 			
			PropName == TEXT("SpawnType")||
			PropName == TEXT("Mesh")||
			PropName == TEXT("GrassType")||	
			PropName == TEXT("Foliages")||			
			PropName == TEXT("Actors")|| 
			PropName == TEXT("NoPoppingRange")||
			PropName == TEXT("MaximumAmountofInstanceToUpdateonGameThread") ||

			

			PropName == TEXT("bResourceCollectable") ||
			PropName == TEXT("ResourceName") ||
			PropName == TEXT("CollisionEnabled") ||
			PropName == TEXT("CollisionChannel") ||
			PropName == TEXT("CollisionProfile") ||			
			PropName == TEXT("CastShadows")|| 

			
			PropName == TEXT("AlignMaxAngle")|| 
			PropName == TEXT("AltitudeRange")||
			PropName == TEXT("AlignToTerrainSlope") ||
			PropName == TEXT("VerticalOffsetRange")|| 
			PropName == TEXT("ScaleRange")|| 
			PropName == TEXT("GroundSlopeAngle")||
			PropName == TEXT("Max") ||
			PropName == TEXT("Min") ||

			PropName == TEXT("bAffectDynamicIndirectLighting")|| 
			PropName == TEXT("bAffectDistanceFieldLighting")|| 
			PropName == TEXT("bCastShadowAsTwoSided")|| 
			PropName == TEXT("bReceivesDecals")|| 

			PropName == TEXT("NumberGridRings")||
			PropName == TEXT("FoliageComponent") ||
			PropName == TEXT("CollisionOnlyAtProximity")
			)
		{	
			EditRebuildVegetation.AtomicSet(true);

			//rebuildVegetationOnly=true;				
			
			for (FSWBiom& elB : Bioms)
			{
				for (FSpawnableMesh& Sp : elB.Spawnables)
				{

					if (Sp.SpawnType !=  Sp.SpawnType_LastUpdate)
					{
						Sp.UpdateSpawnSettings = true;
					}
					
				}
			}
		}
		else if(PropName == TEXT("VerticePerPatch") || 
			PropName == TEXT("LOD_Num")||
			PropName == TEXT("Material") ||
			PropName == TEXT("Generator") ||
			PropName == TEXT("ClipMapCacheIntraVerticesTexel")|| 
			PropName == TEXT("WorldDimensionMeters")|| 
			PropName == TEXT("LandDataLayers") ||
				
			
			PropName == TEXT("LOD_above_doubleCacheResolution") ||

			PropName == TEXT("WorldShape") ||
			PropName == TEXT("PlanetRadiusKm") ||
			PropName == TEXT("RootComp") ||

			PropName == TEXT("WorldHasBounds") ||			

			PropName == TEXT("ShadowCastingRange") ||
			PropName == TEXT("DrawDistance") ||
			PropName == TEXT("TerrainResolution") ||

			PropName == TEXT("GenerateCollision") ||
			PropName == TEXT("CollisionChannel") ||
			PropName == TEXT("TopologyFixUnderLOD") ||
			PropName == TEXT("EnableWorldPositionOffsetUnderLOD") ||

			PropName == TEXT("BrushManager") ||

			PropName == TEXT("CollisionGridRingNumber") ||
			
			PropName == TEXT("UpdateHeightDataOverTime") ||
			PropName == TEXT("UpdateRateHeight") ||
			PropName == TEXT("UpdateRateDegradationFactor") ||
			

			PropName == TEXT("CollisionResolution") ||
			PropName == TEXT("CollisionVerticesPerPatch") ||

			PropName == TEXT("ExportMaterial") ||
			PropName == TEXT("DataLayerWithMaterialID") ||
			PropName == TEXT("ChannelOfMaterialID") ||
			

			PropName == TEXT("WorldSeeds") ||
			PropName == TEXT("SeedName") ||
			PropName == TEXT("Seed") ||
			PropName == TEXT("SmoothLODTransition") ||
			PropName == TEXT("SeedGenerator") ||
			PropName == TEXT("SeedRange")
			)
		{
			HeightScale = FMath::RoundToInt(FMath::Clamp(HeightScale,1.f,10000.f));

			if(UpdateHeightDataOverTime)
			{
				UpdateHOverTime.AtomicSet(true);
				UpdateRateCameras = FMath::Clamp(UpdateRateCameras, UpdateRateHeight+5.f, UpdateRateHeight+120.f);
			}
			
			EditRebuild.AtomicSet(true);			

		}

		if (PropName == TEXT("TransitionWidth"))
		{
			UpdatePatchData.AtomicSet(true);
		}

		if (PropName == TEXT("CollisionVisible"))
		{
			CollisionVisibleChanged.AtomicSet(true);
		}
	}

	if(RuntimePropertyEditing)
	{
		RuntimePropertyEditing=false;
		return;
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
	
	PreventReRegistration = false;
}
#endif

void AShaderWorldActor::SetN()
{
	const int N_values[8] = {2047,1023,511,255,127,63,31,15};

	N = N_values[(uint8)VerticePerPatch];

}

void AShaderWorldActor::IncrementSpawnableDrawCounter()
{
	DrawCall_Spawnables_count++;
}

bool AShaderWorldActor::CanUpdateSpawnables()
{
	if(DrawCall_Spawnables_count<DrawCallBudget_Spawnables)
	{
		DrawCall_Spawnables_count++;
		return true;
	}
	return false;
}

void AShaderWorldActor::FinalizeAsyncWork()
{
	SW_FCT_CYCLE()

	if(rebuild)
	{
		return;
	}
		

	{
		double VegetationStart = FPlatformTime::Seconds();

		const double GameThreadBudget_ms = SWGameThreadBudgetFinalize_ms.GetValueOnGameThread();


		const uint32 HISMStaggerUpdateBy = InstanceCountUpdate;
		uint32 LocalHISMWorkCounter = 0;

	for (FSWBiom& elB : Bioms)
	{
		
	for (FSpawnableMesh& Spawn : elB.Spawnables)
	{		

		if (Spawn.SpawnType == ESpawnableType::Undefined)
		{
			continue;
		}

		if (LocalHISMWorkCounter > HISMStaggerUpdateBy)
			continue;

		if ((FPlatformTime::Seconds() - VegetationStart)*1000.0 > GameThreadBudget_ms)
		{
			return;
		}

		if(!Spawn.SpawnablesElemReadToProcess.IsEmpty())
			continue;

		if (Spawn.SegmentedOnly_ElementToUpdateData.Num() > 0)
		{
			SegmentedUpdateProcessedButSpawnablesLeft = true;
			continue;
		}
		


		bool AllSentToHISM = true;


		if(Spawn.ProcessedRead.IsValid() && Spawn.ProcessedRead->bProcessingCompleted && Spawn.SpawnableWorkQueue.Num()>0)
		{

			if(Spawn.SegmentedOnly_ElementToUpdateData.Num()>0 || Spawn.SpawnablesElemReadToProcess.Num()>0)
			{
				UE_LOG(LogTemp,Warning,TEXT("ElementToUpdateData %d ElemReadToProcess %d"), Spawn.SegmentedOnly_ElementToUpdateData.Num(), Spawn.SpawnablesElemReadToProcess.Num())
			}
#if 1
			int32 BudgetElementPerUpdate = CVarSWBatchUpdateCount.GetValueOnGameThread();

			TArray<int32> Iters;			
			TArray<int32> Reminders;
			for (auto& W : Spawn.SpawnableWorkQueue)
			{				

				if (!W.bCollisionProcessingOverflow && (Spawn.SpawnType != ESpawnableType::Actor))
				{
					if (Spawn.HIM_Mesh.Num() != W.InstancesT->Transforms.Num())
					{
						Spawn.SpawnableWorkQueue.Empty();
						UE_LOG(LogTemp, Warning, TEXT("ShaderWorld : (Spawn.HIM_Mesh.Num() != W.InstancesT->Transforms.Num())"))
						rebuildVegetationOnly=true;
						
						return;
					}

					Iters.SetNum(Spawn.HIM_Mesh.Num());
					Reminders.SetNum(Spawn.HIM_Mesh.Num());					

					for (int i = 0; i < Spawn.HIM_Mesh.Num(); i++)
					{
						Iters[i] = W.InstancesT->Transforms[i].Num() / BudgetElementPerUpdate;
						Reminders[i] = W.InstancesT->Transforms[i].Num() - Iters[i] * BudgetElementPerUpdate;
					}
					break;
				}
			}

			for(auto& W : Spawn.SpawnableWorkQueue)
			{
				if(W.bCollisionProcessingOverflow)
					continue;

				if(W.ElemID >= Spawn.SpawnablesElem.Num())
				{
					Spawn.SpawnableWorkQueue.Empty();

					break;
				}

				if(W.SentToHISM || !W.InstancesT.IsValid())
					continue;

				if ((FPlatformTime::Seconds() - VegetationStart) * 1000.0 > GameThreadBudget_ms)
				{
					AllSentToHISM = false;
					return;
				}

				bool bCanBeUpdated = true;

				for (int i = 0; i < Spawn.HIM_Mesh.Num(); i++)
				{
					bCanBeUpdated &= !Spawn.HIM_Mesh[i]->IsAsyncBuilding();
					if(!bCanBeUpdated)
					{
						AllSentToHISM = false;
						break;
					}
					
				}

				if(bCanBeUpdated)
				{
					for (int i = 0; i < Spawn.HIM_Mesh_Collision_enabled.Num(); i++)
					{
						bCanBeUpdated &= !Spawn.HIM_Mesh_Collision_enabled[i]->IsAsyncBuilding();
						if (!bCanBeUpdated)
						{
							AllSentToHISM = false;
							break;
						}

					}
				}				

				if(!bCanBeUpdated)
				{
					AllSentToHISM = false;
					continue;
				}
					

				if(LocalHISMWorkCounter > HISMStaggerUpdateBy)
				{
					AllSentToHISM=false;
					break;
				}

				FSpawnableMeshElement& Mesh = Spawn.SpawnablesElem[W.ElemID];

				if (Mesh.InstancesIndexes.IsValid() && Mesh.InstancesIndexes->Initiated && (Mesh.InstancesIndexes->InstancesIndexes.Num() > 0) /*Mesh.InstancesIndexes.Num() > 0*/)
				{
					
					int32 loop = 0;

					if (Spawn.SpawnType != ESpawnableType::Actor)
					{
						if (Mesh.OffsetOfSegmentedUpdate.Num() <= 0)
						{
							Mesh.OffsetOfSegmentedUpdate.AddDefaulted(Spawn.HIM_Mesh.Num());
						}

						

						

						
						

						while (true)
						{
							bool AllComplete = true;

							for (int i = 0; i < Spawn.HIM_Mesh.Num(); i++)
							{
								loop++;

								if (!Spawn.HIM_Mesh[i])
								{
									UE_LOG(LogTemp, Warning, TEXT("ERROR Hierachical instanced mesh nullptr: Garbage Collected in editor ?"));
									rebuild = true;
									return;
								}

								if ((FPlatformTime::Seconds() - VegetationStart) * 1000.0 > GameThreadBudget_ms)
								{
									return;
								}

								if ((Reminders[i] == 0) && (Mesh.OffsetOfSegmentedUpdate[i] == Iters[i]) || (Reminders[i] > 0 && (Mesh.OffsetOfSegmentedUpdate[i] > Iters[i])))
								{

								}
								else
								{
									Spawn.HIM_Mesh[i]->bAutoRebuildTreeOnInstanceChanges = false;

									int32 Iteration = Mesh.OffsetOfSegmentedUpdate[i];

									if (Iteration < Iters[i])
									{
										if(LocalHISMWorkCounter < (uint32)Spawn.HIM_Mesh[i]->GetNumRenderInstances())
										{
											LocalHISMWorkCounter = Spawn.HIM_Mesh[i]->GetNumRenderInstances();
										}
										Spawn.HIM_Mesh[i]->SWNewBatchUpdateCountInstanceData(Mesh.InstanceOffset[i] + Iteration * BudgetElementPerUpdate, BudgetElementPerUpdate, W.InstancesT->Transforms[i], false, false, true, Iteration * BudgetElementPerUpdate);
										Mesh.OffsetOfSegmentedUpdate[i]++;

										if ((Reminders[i] > 0) || ((Reminders[i] == 0) && (Mesh.OffsetOfSegmentedUpdate[i] < Iters[i])))
											AllComplete = false;
									}
									else if (Reminders[i] > 0)
									{
										Spawn.HIM_Mesh[i]->SWNewBatchUpdateCountInstanceData(Mesh.InstanceOffset[i] + Iters[i] * BudgetElementPerUpdate, Reminders[i], W.InstancesT->Transforms[i], false, false, true, Iters[i] * BudgetElementPerUpdate);
										Mesh.OffsetOfSegmentedUpdate[i]++;
									}
								}
							}

							if (AllComplete)
							{
								break;
							}
						}


						Mesh.OffsetOfSegmentedUpdate.Empty();
					}						
					else
					{
						for (int i = 0; i < Spawn.Spawned_Actors.Num(); i++)
						{
							FSpawnedActorList& SAL = Spawn.Spawned_Actors[i];
							int FirstIndice = Mesh.InstanceOffset[i];

							for (int j = 0; j < W.InstancesT->Transforms[i].Num(); j++)
							{
								FVector Location = W.InstancesT->Transforms[i][j].Transform.GetOrigin();
								FVector Scale3D = W.InstancesT->Transforms[i][j].Transform.GetScaleVector();
								FMatrix NoScale = W.InstancesT->Transforms[i][j].Transform;
								NoScale.RemoveScaling();
								NoScale = NoScale.RemoveTranslation();
								FQuat Rotation(NoScale);

								FTransform T = FTransform(Rotation,Location, Scale3D);
								
								if (T.GetScale3D().X > 0.0001f)
								{

									if (AActor* Local_Actor = SAL.SpawnedActors[FirstIndice + j])
									{
										Local_Actor->SetActorTransform(T, false, nullptr, ETeleportType::TeleportPhysics);
									}
									else
									{
										FActorSpawnParameters ActorSpawnParameters;
										ActorSpawnParameters.ObjectFlags = RF_Transient;

										AActor* SpawnedActor = GetWorld()->SpawnActor(Spawn.Actors_Validated[i], &T, ActorSpawnParameters);
										SAL.SpawnedActors[FirstIndice + j] = SpawnedActor;
									}
								}

							}
						}
					}
					
				}
				else
				{
					
					if (Mesh.InstancesIndexes.IsValid() && !Mesh.InstancesIndexes->Initiated)
					{
						SW_LOG("Mesh.InstancesIndexes.IsValid() && !Mesh.InstancesIndexes->Initiated")
						AllSentToHISM = false;
						continue;
					}/*
					else if(Mesh.InstancesIndexes.IsValid() && Mesh.InstancesIndexes->Initiated)
					{
						SW_LOG("Initiated with zero instances ?!")
					}*/
					else
					{
						
						Mesh.InstancesIndexes = MakeShared<FSWInstanceIndexesInHISM, ESPMode::ThreadSafe>();						

						Mesh.InstancesIndexes->InstancesIndexes.SetNum(Spawn.NumInstancePerHIM->Indexes.Num());
						Mesh.InstanceOffset.SetNum(Spawn.NumInstancePerHIM->Indexes.Num());

						if (Spawn.SpawnType != ESpawnableType::Actor)
						{
							for (int i = 0; i < Spawn.HIM_Mesh.Num(); i++)
							{
								USWHISMComponent* SWHISMComp = Spawn.HIM_Mesh[i];

								if (SWHISMComp && IsValid(SWHISMComp))
								{
									Mesh.InstanceOffset[i] = SWHISMComp->GetNumRenderInstances();
									SWHISMComp->bAutoRebuildTreeOnInstanceChanges = false;

									//Mesh.InstancesIndexes[i].InstancesIndexes = SWHISMComp->SWAddInstances(Mesh.InstancesIndexes[i].InstancesIndexes,W.InstancesT->Transforms[i], true, false);
									//SWHISMComp->SWAddInstances(Mesh.InstancesIndexes->InstancesIndexes[i].InstancesIndexes,W.InstancesT->Transforms[i], true, false);
									SWHISMComp->SWNewAddInstances(i,Mesh.InstancesIndexes, W.InstancesT);

									LocalHISMWorkCounter += W.InstancesT->Transforms[i].Num();

								}
							}

							/*
							 * The index buffer was just filled on gamethread.. for now!
							 */
							Mesh.InstancesIndexes->Initiated = true;
						}							
						else
						{
							for (int i = 0; i < Spawn.Spawned_Actors.Num(); i++)
							{
								FSpawnedActorList& SAL = Spawn.Spawned_Actors[i];
								Mesh.InstanceOffset[i] = SAL.SpawnedActors.Num();

								for (int j = 0; j < W.InstancesT->Transforms[i].Num(); j++)
								{
									Mesh.InstancesIndexes->InstancesIndexes[i].InstancesIndexes.Add(SAL.SpawnedActors.Num());

									FVector Location = W.InstancesT->Transforms[i][j].Transform.GetOrigin();
									FVector Scale3D = W.InstancesT->Transforms[i][j].Transform.GetScaleVector();
									FMatrix NoScale = W.InstancesT->Transforms[i][j].Transform;
									NoScale.RemoveScaling();
									NoScale = NoScale.RemoveTranslation();
									FQuat Rotation(NoScale);

									FTransform T = FTransform(Rotation, Location, Scale3D);

									//FTransform& T = W.InstancesT->Transforms[i][j];
									if (T.GetScale3D().X > 0.0001f)
									{
										FActorSpawnParameters ActorSpawnParameters;
										ActorSpawnParameters.ObjectFlags = RF_Transient;

										AActor* SpawnedActor = GetWorld()->SpawnActor(Spawn.Actors_Validated[i], &T, ActorSpawnParameters);
										SAL.SpawnedActors.Add(SpawnedActor);

									}
									else
										SAL.SpawnedActors.Add(nullptr);
								}
							}

							/*
							 * The index buffer was just filled on gamethread
							 */
							Mesh.InstancesIndexes->Initiated = true;
						}
					}
				}				

				W.SentToHISM=true;

			}

			if(AllSentToHISM)
			{
				SW_FCT_CYCLE("UpdateSpawnableCollisions")

				Spawn.SpawnableWorkQueue.Empty();

				if(!UpdateSpawnableCollisions(Spawn, VegetationStart))
				{
					/*
					 * Processing the collision update is taking too much time, will be processed over multiple frames
					 */

					FSpawnableMesh::FSpawnableProcessingWork& LocalSpawn = Spawn.SpawnableWorkQueue.AddDefaulted_GetRef();
					LocalSpawn.bCollisionProcessingOverflow = true;					
				}
			}
#else
		Spawn.SpawnableWorkQueue.Empty();
#endif
			
		}

		if (Spawn.SpawnType != ESpawnableType::Actor && (Spawn.SpawnableWorkQueue.Num()==0) && AllSentToHISM)
		{
			double MinSecondsDelayBetweenTreeRebuild = DelayBetweenTwoVisualUpdates;
			const double CurrentTime = FPlatformTime::Seconds();

			if((CurrentTime - Spawn.AllTreesRebuildTime) > MinSecondsDelayBetweenTreeRebuild)
			{
				bool LaunchedBuild = false;

				bool bCanBeUpdated = true;

				for (int i = 0; i < Spawn.HIM_Mesh.Num(); i++)
				{
					ensure(Spawn.HIM_Mesh[i]);

					bCanBeUpdated &= !Spawn.HIM_Mesh[i]->IsAsyncBuilding();
					if (!bCanBeUpdated)
						break;
				}
				for (int i = 0; i < Spawn.HIM_Mesh_Collision_enabled.Num(); i++)
				{
					ensure(Spawn.HIM_Mesh_Collision_enabled[i]);

					bCanBeUpdated &= !Spawn.HIM_Mesh_Collision_enabled[i]->IsAsyncBuilding();
					if (!bCanBeUpdated)
						break;
				}

				if (!bCanBeUpdated)
					continue;
				

				for (int i = 0; i < Spawn.HIM_Mesh.Num(); i++)
				{
					if (Spawn.HIM_Mesh[i] && IsValid(Spawn.HIM_Mesh[i]) && Spawn.HIM_Mesh[i]->GetStaticMesh() && !Spawn.HIM_Mesh[i]->bAutoRebuildTreeOnInstanceChanges && !Spawn.HIM_Mesh[i]->IsAsyncBuilding() && !Spawn.HIM_Mesh[i]->GetStaticMesh()->IsCompiling()
						&& ((CurrentTime - Spawn.HIM_Mesh_TreeRebuild_Time[i]) > MinSecondsDelayBetweenTreeRebuild))
					{
						LaunchedBuild=true;
						Spawn.HIM_Mesh_TreeRebuild_Time[i] = CurrentTime;
						Spawn.HIM_Mesh[i]->bAutoRebuildTreeOnInstanceChanges = true;
						Spawn.HIM_Mesh[i]->SWUpdateTree();

					}
				}
				for (int i = 0; i < Spawn.HIM_Mesh_Collision_enabled.Num(); i++)
				{
					if (Spawn.HIM_Mesh_Collision_enabled[i] && IsValid(Spawn.HIM_Mesh_Collision_enabled[i]) && Spawn.HIM_Mesh_Collision_enabled[i]->GetStaticMesh() && !Spawn.HIM_Mesh_Collision_enabled[i]->bAutoRebuildTreeOnInstanceChanges && !Spawn.HIM_Mesh_Collision_enabled[i]->IsAsyncBuilding() && !Spawn.HIM_Mesh_Collision_enabled[i]->GetStaticMesh()->IsCompiling()
						&& ((CurrentTime - Spawn.HIM_Mesh_Collision_TreeRebuild_Time[i]) > MinSecondsDelayBetweenTreeRebuild))
					{
						LaunchedBuild = true;
						Spawn.HIM_Mesh_Collision_TreeRebuild_Time[i] = CurrentTime;
						Spawn.HIM_Mesh_Collision_enabled[i]->bAutoRebuildTreeOnInstanceChanges = true;
						Spawn.HIM_Mesh_Collision_enabled[i]->SWUpdateTree();
					}
				}

				if(LaunchedBuild)
					Spawn.AllTreesRebuildTime = CurrentTime;
			}			
		}
	}
	}

	}

	
}


void AShaderWorldActor::TerrainAndSpawnablesManagement(float& DeltaT)
{

	TimeAcu += DeltaT;	

	if (!ProcessSegmentedComputation())
		return;

	if(!RTUpdate.IsFenceComplete())
		return;

	if (!(TimeAcu > 1.0 / (FMath::Clamp( UpdateHOverTime?(FMath::Max(UpdateRateCameras, UpdateRateHeight)):UpdateRateCameras, 1.f, 200.f))))
		return;

	if (!Setup())
		return;

	if (Meshes.Num() <= 0)
	{
		InitiateWorld();

		RTUpdate.BeginFence();
		return;
	}

	/*
	 * Using main Camera information, and collisions meshes if available (for distance to ground), update the Clip-maps
	 * If Updating over time (Ocean), launch the GPU task directly, otherwise tag the Clip-map that need to be updated.
	 */
	UpdateClipMap();

	ensure(Meshes.Num() > 0 && Meshes[0].HeightMap);
		
	/*
	 *	Using the Clip-maps information available once the next Clip-map update will be fully processed.
	 *	Per Bioms, per Spawnable, with priority to spawnables with collisions and in view frustum, per view distance:
	 *		Initiate a spawnable if it is not (i.e Allocation HISM)
	 *		Gather relevant Spawnables tiles
	 *		If an old tile is not relevant anymore, release it.
	 *		If a new location needs to be computed: allocate a tile and add its relevant computation work to the GPU work queue
	 */
	UpdateSpawnables();

	/*
	 * Currently just having BrushManagerRedrawScopes will force redraw of every LOD visible, no point keeping those scopes
	 * Scope are mostly interesting for costly spawnables and collisions recomputations
	 */
	BrushManagerRedrawScopes.Empty();
	RTUpdate.BeginFence();
}

void AShaderWorldActor::BoundedWorldUpdate(float& DeltaT)
{
	SW_FCT_CYCLE()

	if(!BoundedWorldTree.IsValid())
	{		
		return;
	}

}

void AShaderWorldActor::ProcessBoundedWorldVisit(const uint64 TreeID_, const TArray<TStaticArray<TSharedPtr < FSW_PointerTree<FSWQuadElement>::FPointerNode>, 4 >>& ToCreate, const TArray<TStaticArray<TSharedPtr < FSW_PointerTree<FSWQuadElement>::FPointerNode>, 4 >>& ToCleanUp)
{
	if(TreeID_!= TreeID)
	return;
	

	if(!BoundedWorldTree.IsValid())
	{		
		return;
	}

	BoundedWorldVisitorProcessing = false;


}

void AShaderWorldActor::CollisionManagement(float& DeltaT)
{
	SW_FCT_CYCLE()

	/*
	 * Can we execute compute shader?
	 * Do we need to rebuild collision?
	 * Did ShaderWorld toggled collision generation On/Off?
	 * Are we pending a full rebuild of the Shader World?
	 */
	if(!SetupCollisions())
		return;

	/*
	 * Using collision updates, update Collision meshes
	 */
	if(!CollisionFinalizeWork())
		return;

	if(CollisionProcess.IsFenceComplete())
	{
		/*
		 * Convert Compute shader results to actionable collision updates
		 */
		if(!CollisionPreprocessGPU())
		{
			CollisionProcess.BeginFence();
			return;
		}			
	}		

	/*
	 * Process GPU work queue by launching GPU tasks to evaluate the collision of new tiles
	 */
	CollisionGPU();

	// Timer
	{
		CollisionUpdateTimeAcu += DeltaT;

		if (CollisionUpdateTimeAcu <= 1.f / 10.f || CollisionWorkQueue.Num() > 0 || !CollisionProcess.IsFenceComplete())
			return;

		CollisionUpdateTimeAcu = 0.f;
	}

	/*
	 * Gather relevant collision tiles
	 * If an old tile is not relevant anymore, release it.
	 * If a new location needs to be computed: allocate a tile and add its relevant computation work to the GPU work queue
	 */
	CollisionCPU();
}

void AShaderWorldActor::SpawnablesManagement(float& DeltaT)
{
	/*
	 * Update HISM
	 */
	FinalizeAsyncWork();

	if (RTUpdate.IsFenceComplete())
	{
		/*
		 * Convert results from Compute Shaders to actionable HISM update data.
		 */
		if (!ProcessSpawnablePending())
		{
			RTUpdate.BeginFence();
		}
	}
}


bool AShaderWorldActor::SetupCollisions()
{
	SW_FCT_CYCLE()

	if(!bHadGeneratorAtRebuildTime)
		return false;


	if(!Shareable_ID.IsValid() || Meshes.Num() <= 0)
		return false;

	if(!bProcessingGroundCollision.IsValid())
		bProcessingGroundCollision = MakeShared<FThreadSafeBool, ESPMode::ThreadSafe>();
	if(!bPreprocessingCollisionUpdate.IsValid())
		bPreprocessingCollisionUpdate = MakeShared<FThreadSafeBool, ESPMode::ThreadSafe>();

	if(EditRebuild)
	{
		EditRebuild.AtomicSet(false);
		rebuild=true;
	}

	if (rebuild)
		RedbuildCollisionContext = true;

	if (RedbuildCollisionContext)
	{
		if (!(*bProcessingGroundCollision.Get()) && !(*bPreprocessingCollisionUpdate.Get())
			&& CollisionProcess.IsFenceComplete()
			&& (CollisionMesh.Num() <= 0)
			&& (UsedCollisionMesh.Num() <= 0))
		{

			CollisionShareable = nullptr;

			CollisionWorkQueue.Empty();
			CollisionReadToProcess.Empty();
			
			RedbuildCollisionContext = false;

		}
	}

	if (RedbuildCollisionContext)
		return false;

	if(!GenerateCollision)
		return false;

	if (!CollisionShareable.IsValid())
		CollisionShareable = MakeShared<FSWCollisionManagementShareableData, ESPMode::ThreadSafe>(CollisionResolution, CollisionVerticesPerPatch);

	if ((*bProcessingGroundCollision.Get()) || (*bPreprocessingCollisionUpdate.Get()) || !CameraSet || (Meshes.Num()==0))
		return false;

	//Let the data layer be computed before generating collisions and trying to extract material IDs
	if(WorldCycle<2)
		return false;


	if(CollisionVisibleChanged)
	{
		if(GetWorld())
		{
			for (auto& ColM : CollisionMesh)
			{
				if (auto& Mesh = ColM.Mesh)
				{					
					if(Mesh->bHiddenInGame != !CollisionVisible)
					{
						Mesh->bHiddenInGame = !CollisionVisible;
						Mesh->MarkRenderStateDirty();
						Mesh->SetMeshSectionVisible(0, CollisionVisible);

						if(UMaterialInstanceDynamic* DynColMat = Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(0)))
							DynColMat->SetScalarParameterValue("MakeCollisionVisible", CollisionVisible ? 1.f : 0.f);
					}
				}
			}
		}		
		CollisionVisibleChanged.AtomicSet(false);
	}
		
	return true;
}

bool AShaderWorldActor::CollisionFinalizeWork()
{
	SW_FCT_CYCLE()

	if(!CollisionReadToProcess.IsEmpty())
		return true;

	const double GameThreadBudget_ms = SWGameThreadBudgetCollision_ms.GetValueOnGameThread();

	double TimeStart = FPlatformTime::Seconds();

	for (int i = CollisionWorkQueue.Num()-1; i>=0; i--)
	{
		if ((FPlatformTime::Seconds() - TimeStart) * 1000.0 > GameThreadBudget_ms)
			return false;

		FCollisionProcessingWork& Work = CollisionWorkQueue[i];

		ensure(Work.MeshID < CollisionMesh.Num());

		FCollisionMeshElement& Mesh = CollisionMesh[Work.MeshID];

		Mesh.Mesh->UpdateSectionTriMesh(Work.DestB);

		CollisionWorkQueue.RemoveAt(i);
	}

	CollisionWorkQueue.Empty();
	return true;

}

bool AShaderWorldActor::CollisionPreprocessGPU()
{
	SW_FCT_CYCLE()

	for (int32 CollID = CollisionReadToProcess.Num() - 1; CollID >= 0; CollID--)
	{
		const int32& ElID = CollisionReadToProcess[CollID];
	

		if (ElID >= CollisionMesh.Num())
		{
			CollisionWorkQueue.Empty();
			return false;
		}

		FCollisionMeshElement& Mesh = CollisionMesh[ElID];

		if ((*Mesh.ReadBackCompletion.Get()))
		{
			ensure(Mesh.Mesh);
		
			FGeoCProcMeshSection* Section = Mesh.Mesh->GetProcMeshSection(0);

			const int NumOfVertex = Section->ProcVertexBuffer.Num();

			TSharedPtr<FSWColorRead, ESPMode::ThreadSafe>& SourceRead = Mesh.HeightData;

			TSharedPtr<FSWShareableVerticePositionBuffer, ESPMode::ThreadSafe>& SourceVertices = CollisionMesh[0].Mesh->VerticesTemplate;

			TSharedPtr<FSWShareableVerticePositionBuffer, ESPMode::ThreadSafe> Vertices = MakeShared<FSWShareableVerticePositionBuffer, ESPMode::ThreadSafe>();
			Vertices->Positions.SetNum(NumOfVertex);
			Vertices->Positions3f.SetNum(NumOfVertex);
			Vertices->MaterialIndices.SetNum(NumOfVertex);
			Vertices->Bound = FBox(EForceInit::ForceInit);

			FCollisionProcessingWork CollisionElementWork(ElID, SourceRead, SourceVertices, Vertices);

			CollisionWorkQueue.Add(CollisionElementWork);

			CollisionReadToProcess.RemoveAt(CollID);
		}
	}

	if(!CollisionReadToProcess.IsEmpty())
	{
		return false;
	}
		

	CollisionReadToProcess.Empty();

	if (CollisionWorkQueue.Num() > 0)
	{
		(*bProcessingGroundCollision.Get()) = true;

		AShaderWorldActor* SWContext = this;

		Async(EAsyncExecution::TaskGraph, [Completion = bProcessingGroundCollision,RenderAPI= RendererAPI,VerticesPerPatch = CollisionVerticesPerPatch,Work = CollisionWorkQueue]
			{

				const int NumOfPatch = Work.Num();

				ParallelFor(NumOfPatch, [&](int32 j)
					{
						if (j < NumOfPatch)
						{
							const FCollisionProcessingWork& WorkEl = Work[j];


							if (!WorkEl.Read.IsValid() || !WorkEl.SourceB.IsValid() || !WorkEl.DestB.IsValid())
								return;

							const int NumOfVertex = WorkEl.SourceB->Positions.Num();

							FVector LocationfVertice_WS(0);
							uint16 MaterialIndice = 0;

							uint8* ReadData8 = (uint8*)WorkEl.Read->ReadData.GetData();

							for (int32 k = 0; k < NumOfVertex; k++)
							{								

								if (RenderAPI == EGeoRenderingAPI::OpenGL)
								{
									const int index = k % VerticesPerPatch + (VerticesPerPatch - 1 - (k / VerticesPerPatch)) * VerticesPerPatch;

									LocationfVertice_WS = FVector(WorkEl.SourceB->Positions[k].X, WorkEl.SourceB->Positions[k].Y,  GetHeightFromGPURead(&ReadData8[4*index], MaterialIndice));
								}
								else									
									LocationfVertice_WS = FVector(WorkEl.SourceB->Positions[k].X, WorkEl.SourceB->Positions[k].Y, GetHeightFromGPURead(&ReadData8[4*k], MaterialIndice));

								WorkEl.DestB->Positions[k] = LocationfVertice_WS;
								WorkEl.DestB->Positions3f[k] = FVector3f(LocationfVertice_WS);
								WorkEl.DestB->MaterialIndices[k] = MaterialIndice;

							}
							WorkEl.DestB->Bound = FBox(WorkEl.DestB->Positions);


						}
					}
					);
				
				if(Completion.IsValid())
					Completion->AtomicSet(false);				
				
			});
	}

	return true;
}

void AShaderWorldActor::CollisionCPU()
{
	SW_FCT_CYCLE()

	if ((*bPreprocessingCollisionUpdate.Get()) ||
		CollisionShareable->CollisionMeshToUpdate.Num() > 0 ||
		CollisionShareable->CollisionMeshToRenameMoveUpdate.Num() > 0 ||
		CollisionShareable->CollisionMeshToCreate > 0)
		return;


	UWorld* World = GetWorld();

	bool bBrushManagerAskedRedraw = BrushManager && (BrushManagerRedrawScopes_collision.Num()>0);
	FVector LocalActorLocation = GetActorLocation();
	FIntVector LocalOriginLocation = GetWorld()->OriginLocation;

	TArray<FBox2D> BrushRedrawScope;

	if(bBrushManagerAskedRedraw)
	{
		BrushRedrawScope = MoveTemp(BrushManagerRedrawScopes_collision);
		BrushManagerRedrawScopes_collision.Empty();
	}
		


	bool bExternalCollisionRebuildRequest = !CollisionUpdateRequest.IsEmpty();
	FBox2D BPCollisionRebuildRequest(ForceInit);

	if(bExternalCollisionRebuildRequest)
		 CollisionUpdateRequest.Dequeue(BPCollisionRebuildRequest);

	TArray<FVector> VisitorLocations;

	// Use latest camera we know if current array is empty
	if (CameraLocations.Num() <= 0)
		VisitorLocations.Add(CamLocation);

	VisitorLocations.Append(CameraLocations);
	for (int32 It = External_Actors_Tracked.Num() - 1; It >= 0; It--)
	{
		if (!External_Actors_Tracked[It] || !IsValid(External_Actors_Tracked[It]->GetOwner()))
			External_Actors_Tracked.RemoveAt(It);
	}
	for (auto& It : External_Actors_Tracked)
	{
		VisitorLocations.Add(It->GetComponentLocation());
	}

	(*bPreprocessingCollisionUpdate.Get()) = true;

	Async(EAsyncExecution::TaskGraph, [Completion = bPreprocessingCollisionUpdate, CollData = CollisionShareable, VisitorLocations, bBrushManagerAskedRedraw, BrushScope = MoveTemp(BrushRedrawScope), bExternalCollisionRebuildRequest, BPRecomputeScope = BPCollisionRebuildRequest, ColRingCount = CollisionGridRingNumber, LocalActorLocation, LocalOriginLocation, Height0 = HeightOnStart, Cameras = CameraLocations, External_Actors = External_Actors_Tracked]
		{
			if (!CollData.IsValid())
			{
				if(Completion.IsValid())
					(*Completion.Get()) = false;
				return;
			}

			TMap<FIntVector,FVector> BrushRedraws;

			for(const FBox2d& B : BrushScope)
			{
				const int32 MinBoxX_local = FMath::FloorToInt((B.Min.X + LocalOriginLocation.X) / (CollData->CollisionResolution * (CollData->VerticePerPatch - 1)) + 0.45);
				const int32 MinBoxY_local = FMath::FloorToInt((B.Min.Y + LocalOriginLocation.Y) / (CollData->CollisionResolution * (CollData->VerticePerPatch - 1)) + 0.45);

				const int32 MaxBoxX_local = FMath::FloorToInt((B.Max.X + LocalOriginLocation.X) / (CollData->CollisionResolution * (CollData->VerticePerPatch - 1)) + 0.55);
				const int32 MaxBoxY_local = FMath::FloorToInt((B.Max.Y + LocalOriginLocation.Y) / (CollData->CollisionResolution * (CollData->VerticePerPatch - 1)) + 0.55);

				for(int32 X_iter = MinBoxX_local; X_iter <= MaxBoxX_local ; X_iter++)
				{
					for (int32 Y_iter = MinBoxY_local; Y_iter <= MaxBoxY_local; Y_iter++)
					{
						BrushRedraws.Add(FIntVector(X_iter, Y_iter, 0));
					}
				}
			}

			CollData->MultipleCamera.Empty();
			CollData->LocRefs.Empty();

			for (const FVector& SingleCamLoc : VisitorLocations)
			{

				const double Cam_X_local = SingleCamLoc.X + LocalOriginLocation.X;
				const double Cam_Y_local = SingleCamLoc.Y + LocalOriginLocation.Y;
				//const double Cam_Z_local = SingleCamLoc.Z + LocalOriginLocation.Z;

				const int CamX_local = FMath::RoundToInt(Cam_X_local / (CollData->CollisionResolution * (CollData->VerticePerPatch - 1)));
				const int CamY_local = FMath::RoundToInt(Cam_Y_local / (CollData->CollisionResolution * (CollData->VerticePerPatch - 1)));

				FIntVector LocRef_local = FIntVector(CamX_local, CamY_local, 0.f) * CollData->CollisionResolution * (CollData->VerticePerPatch - 1) + FIntVector(0.f, 0.f, 1) * Height0 - LocalOriginLocation;

				CollData->LocRefs.Add(LocRef_local);
				CollData->MultipleCamera.Add(FIntVector(CamX_local, CamY_local, 0));
			}


			for (int i = CollData->UsedCollisionMesh.Num() - 1; i >= 0; i--)
			{
				FSWCollisionMeshElemData& El = CollData->CollisionMeshData[CollData->UsedCollisionMesh[i]];

				bool BeyondCriteria_local = true;


				for (auto& Elem : CollData->LocRefs)
				{
					FIntVector& SingleLocRef = Elem.Key;

					const FVector ToCompLocal = FVector(FIntVector(El.MeshLocation) - SingleLocRef) / (CollData->CollisionResolution * (CollData->VerticePerPatch - 1));
					BeyondCriteria_local = BeyondCriteria_local && (FMath::Abs(ToCompLocal.X) > ColRingCount + .1f || FMath::Abs(ToCompLocal.Y) > ColRingCount + .1f);
					if (!BeyondCriteria_local)
						break;
				}


				if (BeyondCriteria_local)
				{

					CollData->AvailableCollisionMesh.Add(El.ID);
					CollData->UsedCollisionMesh.RemoveAt(i);

					for (auto It = CollData->GroundCollisionLayout.CreateConstIterator(); It; ++It)
					{

						if (It->Value < CollData->CollisionMeshData.Num())
						{
							if (CollData->CollisionMeshData[It->Value] == El)
							{
								CollData->GroundCollisionLayout.Remove(It->Key);
								break;
							}
						}

					}

				}
				else
				{

					if (bBrushManagerAskedRedraw || bExternalCollisionRebuildRequest)
					{						

						bool UpdateRequested = false;
						if(bBrushManagerAskedRedraw)
						{
							if(BrushRedraws.Contains(El.Location))
							{
								CollData->CollisionMeshToUpdate.Add(El.ID);
								UpdateRequested=true;
							}/*
							for(auto& Brush : BrushScope)
							{
								if (Brush.Intersect(LocalCollisionMeshBox))
								{
									CollData->CollisionMeshToUpdate.Add(El.ID);
								}
							}*/
						}
						
						if (!UpdateRequested && bExternalCollisionRebuildRequest)
						{
							const FVector Location_LocalOrigin = FVector(El.Location * CollData->CollisionResolution * (CollData->VerticePerPatch - 1) + FIntVector(0, 0, 1) * LocalActorLocation.Z - LocalOriginLocation);

							FVector2D Location_Mesh(Location_LocalOrigin.X, Location_LocalOrigin.Y);
							FVector2D Extent = CollData->CollisionResolution * (CollData->VerticePerPatch - 1) / 2.f * FVector2D(1.f, 1.f);
							FBox2D LocalCollisionMeshBox(Location_Mesh - Extent, Location_Mesh + Extent);

							if (BPRecomputeScope.Intersect(LocalCollisionMeshBox))
							{
								CollData->CollisionMeshToUpdate.Add(El.ID);
							}
						}
					}

				}

			}

			BrushRedraws.Empty();

			for (auto& Elem : CollData->MultipleCamera)
			{
				FIntVector& SingleCam = Elem.Key;


				for (int r = ColRingCount; r >= 0; r--)
				{
					for (int i = -r; i <= r; i++)
					{
						for (int j = -r; j <= r; j++)
						{
							if (abs(j) != r && abs(i) != r)
								continue;

							FIntVector LocMeshInt = FIntVector(SingleCam.X + i, SingleCam.Y + j, 0);
							FIntVector MeshLoc = LocMeshInt * CollData->CollisionResolution * (CollData->VerticePerPatch - 1) + FIntVector(0.f, 0.f, 1) * Height0 - LocalOriginLocation;


							bool ContainCriteria = !CollData->GroundCollisionLayout.Contains(LocMeshInt);

							if (ContainCriteria)
							{
								if (CollData->AvailableCollisionMesh.Num() > 0)
								{
									FSWCollisionMeshElemData& ElemData = CollData->CollisionMeshData[CollData->AvailableCollisionMesh[CollData->AvailableCollisionMesh.Num() - 1]];

									CollData->UsedCollisionMesh.Add(ElemData.ID);
									CollData->AvailableCollisionMesh.RemoveAt(CollData->AvailableCollisionMesh.Num() - 1);

									ElemData.Location = LocMeshInt;
									ElemData.MeshLocation = FVector(MeshLoc);

									CollData->CollisionMeshToRenameMoveUpdate.Add(ElemData.ID);

									CollData->GroundCollisionLayout.Add(LocMeshInt, ElemData.ID);
								}
								else
								{
									CollData->CollisionMeshToCreate++;
									FCollisionMeshElement NewElem;
									NewElem.ID = CollData->CollisionMeshData.Num();

									NewElem.Location = LocMeshInt;
									NewElem.MeshLocation = FVector(MeshLoc);

									CollData->UsedCollisionMesh.Add(NewElem.ID);
									CollData->CollisionMeshData.Add(NewElem);

									CollData->CollisionMeshToRenameMoveUpdate.Add(NewElem.ID);

									CollData->GroundCollisionLayout.Add(LocMeshInt, NewElem.ID);
								}
							}
						}
					}
				}
			}

			

			if (Completion.IsValid())
				(*Completion.Get()) = false;
		});
}

void AShaderWorldActor::CollisionGPU()
{
	SW_FCT_CYCLE()

	if (!(!(*bPreprocessingCollisionUpdate.Get()) && (
		CollisionShareable->CollisionMeshToUpdate.Num() > 0 ||
		CollisionShareable->CollisionMeshToRenameMoveUpdate.Num() > 0 ||
		CollisionShareable->CollisionMeshToCreate > 0)))
		return;
		

	UWorld* World = GetWorld();

	bool RequireRenderFence = false;
	int32 CollisionDrawCallCount = 0;


	UsedCollisionMesh = CollisionShareable->UsedCollisionMesh;

	for (int32 Index = 0; Index < CollisionShareable->CollisionMeshToCreate; Index++)
	{
		FCollisionMeshElement& Mesh = GetACollisionMesh();
		const FSWCollisionMeshElemData& Mesh_Shareable = CollisionShareable->CollisionMeshData[Mesh.ID];

		Mesh.Location = Mesh_Shareable.Location;
		Mesh.MeshLocation = Mesh_Shareable.MeshLocation;
	}

	CollisionShareable->CollisionMeshToCreate = 0;

	
	for(int i = CollisionShareable->CollisionMeshToUpdate.Num()-1;i>=0;i--)
	{
		FCollisionMeshElement& El = CollisionMesh[CollisionShareable->CollisionMeshToUpdate[i]];
		UpdateCollisionMeshData(El);

		CollisionShareable->CollisionMeshToUpdate.RemoveAt(i);

		RequireRenderFence = true;
		CollisionDrawCallCount++;

		if(CollisionDrawCallCount >= CollisionMaxDrawCallPerFrame)
			break;
	}

	for (int i = CollisionShareable->CollisionMeshToRenameMoveUpdate.Num()-1;i>=0;i--)
	{
		
		FCollisionMeshElement& El = CollisionMesh[CollisionShareable->CollisionMeshToRenameMoveUpdate[i]];
		FSWCollisionMeshElemData& El_Shareable = CollisionShareable->CollisionMeshData[CollisionShareable->CollisionMeshToRenameMoveUpdate[i]];
		El.Location = El_Shareable.Location;
		El.MeshLocation = El_Shareable.MeshLocation;

		FString Rename_str = "SW_Collision_X_" + FString::FromInt(El.Location.X) + "_Y_" + FString::FromInt(El.Location.Y) + "_Z_" + FString::FromInt(El.Location.Z);

		FixNamingCollision(World, El, Rename_str);

		El.Mesh->SetLocationOnPhysicCookComplete(El.MeshLocation);

		UpdateCollisionMeshData(El);

		CollisionShareable->CollisionMeshToRenameMoveUpdate.RemoveAt(i);

		RequireRenderFence = true;
		CollisionDrawCallCount++;

		if (CollisionDrawCallCount >= CollisionMaxDrawCallPerFrame)
			break;
	}

	if(RequireRenderFence)
		CollisionProcess.BeginFence();
}

void AShaderWorldActor::CreateGridMeshWelded(int32 NumX, int32 NumY, TSharedPtr<FSWShareableIndexBuffer>& Triangles, TSharedPtr<FSWShareableVerticePositionBuffer>& Vertices, TArray<FVector2f>& UVs, int32 GridSpac)
{
	Triangles->Indices.Empty();
	Vertices->Positions.Empty();
	Vertices->Positions3f.Empty();
	Vertices->MaterialIndices.Empty();
	Vertices->Bound=FBox(EForceInit::ForceInit);
	UVs.Empty();

	if (NumX >= 2 && NumY >= 2)
	{
		FVector2D Extent = FVector2D((NumX - 1) * GridSpac, (NumY - 1) * GridSpac) / 2;

		for (int i = 0; i < NumY; i++)
		{
			for (int j = 0; j < NumX; j++)
			{
				FVector Pos = FVector((float)j * GridSpac - Extent.X, (float)i * GridSpac - Extent.Y, 0);
				Vertices->Positions.Add(Pos);
				Vertices->Positions3f.Add(FVector3f(Pos));
				Vertices->Bound+= Pos;

				Vertices->MaterialIndices.Add(0);
				
				UVs.Add(FVector2f((float)j / ((float)NumX - 1), (float)i / ((float)NumY - 1)));
			}
		}

		for (int i = 0; i < NumY - 1; i++)
		{
			for (int j = 0; j < NumX - 1; j++)
			{
				int idx = j + (i * NumX);
				Triangles->Indices.Add(idx);
				Triangles->Indices.Add(idx + NumX);
				Triangles->Indices.Add(idx + 1);

				Triangles->Indices.Add(idx + 1);
				Triangles->Indices.Add(idx + NumX);
				Triangles->Indices.Add(idx + NumX + 1);
			}
		}
	}
}

void AddIndexToTrianglesList(int Index, TSharedPtr<FSWShareableIndexBuffer>& Triangles, TSharedPtr<FSWShareableIndexBuffer>& TrianglesAlt)
{
	Triangles->Indices.Add(Index);
	TrianglesAlt->Indices.Add(Index);
}

void AddQuadToTrianglesList(int idx, int NumX, TSharedPtr<FSWShareableIndexBuffer>& Triangles, TSharedPtr<FSWShareableIndexBuffer>& TrianglesAlt, bool Flip = false, bool OverrideTopology = false)
{
	if(Flip)
	{				
		Triangles->Indices.Add(idx);
		Triangles->Indices.Add(idx + NumX);
		Triangles->Indices.Add(idx + NumX + 1);

		Triangles->Indices.Add(idx);
		Triangles->Indices.Add(idx + NumX + 1);
		Triangles->Indices.Add(idx + 1);

		//
		if(!OverrideTopology)
		{
			TrianglesAlt->Indices.Add(idx);
			TrianglesAlt->Indices.Add(idx + NumX);
			TrianglesAlt->Indices.Add(idx + 1);

			TrianglesAlt->Indices.Add(idx + 1);
			TrianglesAlt->Indices.Add(idx + NumX);
			TrianglesAlt->Indices.Add(idx + NumX + 1);
		}
		else
		{
			TrianglesAlt->Indices.Add(idx);
			TrianglesAlt->Indices.Add(idx + NumX);
			TrianglesAlt->Indices.Add(idx + NumX + 1);

			TrianglesAlt->Indices.Add(idx);
			TrianglesAlt->Indices.Add(idx + NumX + 1);
			TrianglesAlt->Indices.Add(idx + 1);
		}
	}
	else
	{
		Triangles->Indices.Add(idx);
		Triangles->Indices.Add(idx + NumX);
		Triangles->Indices.Add(idx + 1);

		Triangles->Indices.Add(idx + 1);
		Triangles->Indices.Add(idx + NumX);
		Triangles->Indices.Add(idx + NumX + 1);

		//
		if (!OverrideTopology)
		{
			TrianglesAlt->Indices.Add(idx);
			TrianglesAlt->Indices.Add(idx + NumX);
			TrianglesAlt->Indices.Add(idx + NumX + 1);

			TrianglesAlt->Indices.Add(idx);
			TrianglesAlt->Indices.Add(idx + NumX + 1);
			TrianglesAlt->Indices.Add(idx + 1);
		}
		else
		{
			TrianglesAlt->Indices.Add(idx);
			TrianglesAlt->Indices.Add(idx + NumX);
			TrianglesAlt->Indices.Add(idx + 1);

			TrianglesAlt->Indices.Add(idx + 1);
			TrianglesAlt->Indices.Add(idx + NumX);
			TrianglesAlt->Indices.Add(idx + NumX + 1);
		}
	}

}

void AShaderWorldActor::CreateGridMeshWelded(int Indexoffset, int32 NumX, int32 NumY, TSharedPtr<FSWShareableIndexBuffer>& Triangles, TSharedPtr<FSWShareableIndexBuffer>& TrianglesAlt, TArray<FVector3f>& Vertices, TArray<FVector2f>& UVs,TArray<FVector2f>& UV1s,TArray<FVector2f>& UV2s, int32& GridSpacing_,FVector& Offset, uint8 StitchProfil)
{


	bool StitchX0 = (((StitchProfil>>3) & 1) > 0);
	bool StitchXN = (((StitchProfil>>2) & 1) > 0);

	bool StitchY0 = (((StitchProfil >> 1) & 1) > 0);
	bool StitchYN = ((StitchProfil & 1) > 0);
	

	int IDOffset = Vertices.Num();

	FVector Loc = GetActorLocation();

	if (NumX >= 2 && NumY >= 2)
	{
		FVector2f Extent = FVector2f(Offset.X,Offset.Y);

		for (int i = 0; i < NumY; i++)
		{
			for (int j = 0; j < NumX; j++)
			{
				FVector3f PosVertex = FVector3f((float)j * GridSpacing_ + Extent.X, (float)i * GridSpacing_ + Extent.Y, 0);

				Vertices.Add(PosVertex + FVector3f(0.f,0.f,1.f)*VerticalRangeMeters*100.f * ((i+j)%2==0?1.f:-1.f));
				UVs.Add(FVector2f(PosVertex.X/GridSpacing_, PosVertex.Y/GridSpacing_));
				UV1s.Add(FVector2f(FMath::Frac(PosVertex.X/400000.f), FMath::Frac(PosVertex.Y/400000.f)));
				UV2s.Add(FVector2f((i>0 && i<NumY-1)&&(j>0 && j<NumX-1)?1.f:0.f,0.f));	
			}
		}

	
		for (int i = 0; i < NumY - 1; i++)
		{
			for (int j = 0; j < NumX - 1; j++)
			{
				int idx = j + (i * NumX) + IDOffset;

				if (i > 0 && i < NumY - 2 && j>0 && j < NumX - 2 || NumX == 2 || NumY == 2 || !StitchX0 && !StitchXN && !StitchY0 && !StitchYN)
				{				
					AddQuadToTrianglesList(idx, NumX, Triangles, TrianglesAlt,(UpdateHOverTime) && ((i+j + Indexoffset)%2>0));
				}
				else
				{
					if (i == 0)
					{
						if (StitchY0)
						{
							if (j % 2 == 0 && j < NumX - 2)
							{
								if (j > 0)
								{
									AddIndexToTrianglesList(idx, Triangles, TrianglesAlt);
									AddIndexToTrianglesList(idx + NumX, Triangles, TrianglesAlt);
									AddIndexToTrianglesList(idx + 1 + NumX, Triangles, TrianglesAlt);
								}

								AddIndexToTrianglesList(idx, Triangles, TrianglesAlt);
								AddIndexToTrianglesList(idx + 1 + NumX, Triangles, TrianglesAlt);
								AddIndexToTrianglesList(idx + 2, Triangles, TrianglesAlt);

								if (j + 2 < NumX - 2)
								{
									AddIndexToTrianglesList(idx + 2, Triangles, TrianglesAlt);
									AddIndexToTrianglesList(idx + 1 + NumX, Triangles, TrianglesAlt);
									AddIndexToTrianglesList(idx + 2 + NumX, Triangles, TrianglesAlt);
								}
							}
						}
						else
						{

							if (j == 0 && StitchX0)
							{
								AddIndexToTrianglesList(idx, Triangles, TrianglesAlt);
								AddIndexToTrianglesList(idx + 1 + NumX, Triangles, TrianglesAlt);
								AddIndexToTrianglesList(idx + 1, Triangles, TrianglesAlt);
							}
							else if (j == NumX - 2)
							{
								AddIndexToTrianglesList(idx, Triangles, TrianglesAlt);
								AddIndexToTrianglesList(idx + NumX, Triangles, TrianglesAlt);
								AddIndexToTrianglesList(idx + 1, Triangles, TrianglesAlt);
							}
							else
							{
								AddQuadToTrianglesList(idx, NumX, Triangles, TrianglesAlt, (UpdateHOverTime) && ((i + j + Indexoffset) % 2 > 0));
							}
						}
					}
					if (i == NumY - 2)
					{
						if (StitchYN)
						{
							if (j % 2 == 0 && j < NumX - 2)
							{
								if (j > 0)
								{
									AddIndexToTrianglesList(idx, Triangles, TrianglesAlt);
									AddIndexToTrianglesList(idx + NumX, Triangles, TrianglesAlt);
									AddIndexToTrianglesList(idx + 1, Triangles, TrianglesAlt);
								}

								AddIndexToTrianglesList(idx + NumX, Triangles, TrianglesAlt);
								AddIndexToTrianglesList(idx + 2 + NumX, Triangles, TrianglesAlt);
								AddIndexToTrianglesList(idx + 1, Triangles, TrianglesAlt);

								if (j + 2 < NumX - 2)
								{
									AddIndexToTrianglesList(idx + 1, Triangles, TrianglesAlt);
									AddIndexToTrianglesList(idx + 2 + NumX, Triangles, TrianglesAlt);
									AddIndexToTrianglesList(idx + 2, Triangles, TrianglesAlt);
								}
							}
						}
						else
						{

							if (j == 0)
							{
								AddIndexToTrianglesList(idx + 1, Triangles, TrianglesAlt);
								AddIndexToTrianglesList(idx + NumX, Triangles, TrianglesAlt);
								AddIndexToTrianglesList(idx + NumX + 1, Triangles, TrianglesAlt);
							}
							else if (j == NumX - 2 && StitchXN)
							{
								AddIndexToTrianglesList(idx, Triangles, TrianglesAlt);
								AddIndexToTrianglesList(idx + NumX, Triangles, TrianglesAlt);
								AddIndexToTrianglesList(idx + 1 + NumX, Triangles, TrianglesAlt);
							}
							else
							{
								AddQuadToTrianglesList(idx, NumX, Triangles, TrianglesAlt, (UpdateHOverTime) && ((i + j + Indexoffset) % 2 > 0));
							}
						}
					}
					if (j == 0)
					{
						if (StitchX0)
						{
							if (i % 2 == 0 && i < NumY - 2)
							{

								AddIndexToTrianglesList(idx, Triangles, TrianglesAlt);
								AddIndexToTrianglesList(idx + 2 * NumX, Triangles, TrianglesAlt);
								AddIndexToTrianglesList(idx + NumX + 1, Triangles, TrianglesAlt);

								if (i > 0)
								{
									AddIndexToTrianglesList(idx + 1, Triangles, TrianglesAlt);
									AddIndexToTrianglesList(idx, Triangles, TrianglesAlt);
									AddIndexToTrianglesList(idx + NumX + 1, Triangles, TrianglesAlt);
								}

								if (i + 2 < NumY - 2)
								{
									AddIndexToTrianglesList(idx + 1 + NumX, Triangles, TrianglesAlt);
									AddIndexToTrianglesList(idx + 2 * NumX, Triangles, TrianglesAlt);
									AddIndexToTrianglesList(idx + 2 * NumX + 1, Triangles, TrianglesAlt);
								}
							}
						}
						else
						{

							if (i > 0 && i < NumY - 2)
							{
								AddQuadToTrianglesList(idx, NumX, Triangles, TrianglesAlt, (UpdateHOverTime) && ((i + j + Indexoffset) % 2 > 0));
							}
							else
							{
								if (i == 0 && StitchY0)
								{
									AddIndexToTrianglesList(idx, Triangles, TrianglesAlt);
									AddIndexToTrianglesList(idx + NumX, Triangles, TrianglesAlt);
									AddIndexToTrianglesList(idx + NumX + 1, Triangles, TrianglesAlt);
								}
								else
								{
									AddQuadToTrianglesList(idx, NumX, Triangles, TrianglesAlt, (UpdateHOverTime) && ((i + j + Indexoffset) % 2 > 0));
								}
							}
						}
					}
					if (j == NumX - 2)
					{

						if (StitchXN)
						{
							if (i % 2 == 0 && i < NumY - 2)
							{

								AddIndexToTrianglesList(idx + 1, Triangles, TrianglesAlt);
								AddIndexToTrianglesList(idx + NumX, Triangles, TrianglesAlt);
								AddIndexToTrianglesList(idx + 1 + 2 * NumX, Triangles, TrianglesAlt);

								if (i > 0)
								{
									
									AddIndexToTrianglesList(idx, Triangles, TrianglesAlt);
									AddIndexToTrianglesList(idx + NumX, Triangles, TrianglesAlt);
									AddIndexToTrianglesList(idx + 1, Triangles, TrianglesAlt);
								}

								if (i + 2 < NumY - 2)
								{
									AddIndexToTrianglesList(idx + NumX, Triangles, TrianglesAlt);
									AddIndexToTrianglesList(idx + 2 * NumX, Triangles, TrianglesAlt);
									AddIndexToTrianglesList(idx + 2 * NumX + 1, Triangles, TrianglesAlt);
								}
							}
						}
						else
						{

							if (i > 0 && i < NumY - 2)
							{
								AddQuadToTrianglesList(idx, NumX, Triangles, TrianglesAlt, (UpdateHOverTime) && ((i + j + Indexoffset) % 2 > 0));
							}
							else if (i < NumY - 2)
							{
								AddIndexToTrianglesList(idx + 1, Triangles, TrianglesAlt);
								AddIndexToTrianglesList(idx + NumX, Triangles, TrianglesAlt);
								AddIndexToTrianglesList(idx + NumX + 1, Triangles, TrianglesAlt);
							}
							else
							{


							}
						}
					}
				}
			}
		}
	}

}

void AShaderWorldActor::UpdateViewFrustum()
{
	if(Meshes.Num()>0 && Meshes[0].Mesh)
	{
		TArray<FConvexVolume> Frustums = Meshes[0].Mesh->GetViewsFrustums();		

		for(int i=0; i<Frustums.Num();i++)
		{
			if(Frustums[i].Planes.Num()>0)
			{
				ViewFrustum = Frustums[i];
			}			
		}
	}
}
FCollisionQueryParams ConfigureCollisionParamsLocal(FName TraceTag, bool bTraceComplex, const TArray<AActor*>& ActorsToIgnore, bool bIgnoreSelf, const UObject* WorldContextObject)
{
	FCollisionQueryParams Params(TraceTag, SCENE_QUERY_STAT_ONLY(PoolTraceUtils), bTraceComplex);
	Params.bReturnPhysicalMaterial = true;
	Params.bReturnFaceIndex = true;//!UPhysicsSettings::Get()->bSuppressFaceRemapTable; // Ask for face index, as long as we didn't disable globally
	Params.AddIgnoredActors(ActorsToIgnore);
	if (bIgnoreSelf)
	{
		const AActor* IgnoreActor = Cast<AActor>(WorldContextObject);
		if (IgnoreActor)
		{
			Params.AddIgnoredActor(IgnoreActor);
		}
		else
		{
			// find owner
			const UObject* CurrentObject = WorldContextObject;
			while (CurrentObject)
			{
				CurrentObject = CurrentObject->GetOuter();
				IgnoreActor = Cast<AActor>(CurrentObject);
				if (IgnoreActor)
				{
					Params.AddIgnoredActor(IgnoreActor);
					break;
				}
			}
		}
	}

	return Params;
}
FCollisionObjectQueryParams ConfigureCollisionObjectParamsLocal(const TArray<TEnumAsByte<EObjectTypeQuery> >& ObjectTypes)
{
	TArray<TEnumAsByte<ECollisionChannel>> CollisionObjectTraces;
	CollisionObjectTraces.AddUninitialized(ObjectTypes.Num());

	for (auto Iter = ObjectTypes.CreateConstIterator(); Iter; ++Iter)
	{
		CollisionObjectTraces[Iter.GetIndex()] = UEngineTypes::ConvertToCollisionChannel(*Iter);
	}

	FCollisionObjectQueryParams ObjectParams;
	for (auto Iter = CollisionObjectTraces.CreateConstIterator(); Iter; ++Iter)
	{
		const ECollisionChannel& Channel = (*Iter);
		if (FCollisionObjectQueryParams::IsValidObjectQuery(Channel))
		{
			ObjectParams.AddObjectTypesToQuery(Channel);
		}
		else
		{
			UE_LOG(LogBlueprintUserMessages, Warning, TEXT("%d isn't valid object type"), (int32)Channel);
		}
	}

	return ObjectParams;
}

void AShaderWorldActor::UpdateCameraLocation()
{

	UWorld* World = GetWorld();
	if(!World)
		return;

	
	{
		
		CameraLocations.Empty();

		SWorldSubsystem->GetVisitors(CameraLocations);
		if (!DataSource)
		{
			if(CameraLocations.Num()>0)
			{
				CamLocation = CameraLocations[0];
				CameraSet=true;
			}
			if (World->ViewLocationsRenderedLastFrame.Num()<=0)
				CameraSet=false;
		}
	}

}

float AShaderWorldActor::HeightToClosestCollisionMesh()
{
	UShaderWorldCollisionComponent* ClosestMesh = nullptr;
	float ClosestDistance = -1.f;

	for(int& i : UsedCollisionMesh)
	{
		FVector CompToCam = CollisionMesh[i].Mesh->GetComponentLocation()-CamLocation;
		CompToCam.Z=0.f;

		if(ClosestDistance<0.f || CompToCam.Size()<ClosestDistance)
		{
			ClosestDistance=CompToCam.Size();
			ClosestMesh = CollisionMesh[i].Mesh;
		}

	}

	if(ClosestMesh)
	{
		if (const FGeoCProcMeshSection* Section = ClosestMesh->GetProcMeshSection(0))
			return (Section->SectionLocalBox.GetCenter()+ClosestMesh->GetComponentLocation().Z*FVector(0.f,0.f,1.f) - CamLocation).Z;
	}

	return -1.f;
}

void AShaderWorldActor::HideChildrenLODs(int Level_Parent)
{
	FClipMapMeshElement& Elem_Parent = Meshes[Level_Parent];

	if (!Elem_Parent.IsSectionVisible(0))
	{
		Elem_Parent.SetSectionVisible(0, true);
	}		

	Elem_Parent.Config = EClipMapInteriorConfig::NotVisible;

	for (int i = Level_Parent+1; i < Meshes.Num(); i++)
	{
		FClipMapMeshElement& Elem = Meshes[i];

		if (!Elem.Mesh)
			continue;

		if (Elem.IsSectionVisible(0))
			Elem.SetSectionVisible(0, false);
		if (Elem.IsSectionVisible(1))
			Elem.SetSectionVisible(1, false);
		if (Elem.IsSectionVisible(2))
			Elem.SetSectionVisible(2, false);
		if (Elem.IsSectionVisible(3))
			Elem.SetSectionVisible(3, false);
		if (Elem.IsSectionVisible(4))
			Elem.SetSectionVisible(4, false);
		if (Elem.IsSectionVisible(5))
			Elem.SetSectionVisible(5, false);

		Elem.Config = EClipMapInteriorConfig::NotVisible;

	}
}


bool AShaderWorldActor::UseSegmented()
{
	return (!UpdateHOverTime) && bHadGeneratorAtRebuildTime;
}

void AShaderWorldActor::MoveClipMapComponentToLocation(int index)
{
	if(index>= Meshes.Num())
		return;

	FClipMapMeshElement& Elem = Meshes[index];

	if (Elem.Mesh)
	{
		Elem.LocationLastMove = Elem.Location;
		Elem.Mesh->Mobility = EComponentMobility::Movable;
		Elem.Mesh->SetWorldLocation(FVector(Elem.Location - GetWorld()->OriginLocation), false, nullptr, ETeleportType::TeleportPhysics);
		Elem.Mesh->Mobility = EComponentMobility::Static;
	}
}

void AShaderWorldActor::MoveClipMapMaterialLocation(int index, bool Segmented)
{
	if (index >= Meshes.Num())
		return;

	FClipMapMeshElement& Elem = Meshes[index];

	FIntVector RemoveAnySphereAdjust = FIntVector(Elem.Location.X,Elem.Location.Y,HeightOnStart);

	//Elem.NeedComponentLocationUpdate++;
	MoveClipMapComponentToLocation(index);

	if(Elem.Mesh)
		Elem.Mesh->UpdatePatchLocation(FVector(RemoveAnySphereAdjust));

	if (Elem.MatDyn)
	{
		Elem.MatDyn->SetVectorParameterValue("PatchLocation", FVector(RemoveAnySphereAdjust));
	}
	else
	{
		//UE_LOG(LogTemp, Warning, TEXT("ERROR !Elem.MatDyn - should not be happening"));

		UMaterialInstanceDynamic* MatDyn_ = nullptr;
		if (Elem.Mesh)
			MatDyn_ = Cast<UMaterialInstanceDynamic>(Elem.Mesh->GetMaterial(0));

		if (MatDyn_)
			MatDyn_->SetVectorParameterValue("PatchLocation", FVector(RemoveAnySphereAdjust));
	}


	if (Elem.Level > 0)
		UpdateParentInnerMesh(Elem.Level, RelativeLocationToParentInnerMeshConfig(FVector(Elem.Location - Meshes[Elem.Level - 1].Location)),Segmented);
}

void AShaderWorldActor::ComputeHeight_Segmented_MapForClipMap(int index)
{
	SW_FCT_CYCLE()

	if (index >= Meshes.Num())
		return;

	FClipMapMeshElement& Elem = Meshes[index];
	
	if(!Elem.HeightMap_Segmented)
		return;

	FIntVector RemoveAnySphereAdjust = FIntVector(Elem.Location.X,Elem.Location.Y,HeightOnStart);

	Elem.CacheMatDyn->SetVectorParameterValue("PatchLocation", FVector(RemoveAnySphereAdjust));


	float Extent = Elem.GridSpacing*(N-1)/2;
	FBox2D ScopeToCompute(FVector2d(Elem.Location.X, Elem.Location.Y), FVector2d(Elem.Location.X, Elem.Location.Y));


	// 1) Intersect clipmap with grid quad
	// 2) Gather non computed quads
	// 3) Allocated Compute element to missing Quad
	// 4) Update the indirection data to the new elements
	// 5) Update the Clipmap Heightmap with the grid data 

#if SW_COMPUTE_GENERATION
	
#else
	UKismetRenderingLibrary::DrawMaterialToRenderTarget(this, Elem.HeightMap_Segmented, Elem.CacheMatDyn);
#endif
	if (BrushManager)
		BrushManager->ApplyBrushStackToHeightMap(this, Elem.Level, Elem.HeightMap_Segmented, FVector(RemoveAnySphereAdjust), Elem.GridSpacing, N, false);

	/*
	 * Let's apply our local cache on top
	 */
	 /*
	if(index == (Meshes.Num()-1))
	{
		
		double TimeAtStart = FPlatformTime::Seconds();

		for(auto& Group : Elem.TransientCaches.PerLODCacheGroup)
		{
			TArray<FVector> Cams = { FVector(Elem.Location) - 0.5*FVector(Group.CacheSizeMeters*100.0) };

			if(Group.PerLODCaches.Find("Heightmap"))
			{
				Group.UpdateReferencePoints(GetWorld(), TimeAtStart, Cams);

				Group.ReleaseOutOfRange();
				Group.GenerateWithinRange(GetWorld());

				//Group.DrawDebugPartition(GetWorld());
				break;
			}			
		}
	}*/

}

void AShaderWorldActor::ComputeHeightMapForClipMap(int index)
{
	if (index >= Meshes.Num())
		return;

	FClipMapMeshElement& Elem = Meshes[index];

	Elem.DrawingThisFrame=true;

	FIntVector RemoveAnySphereAdjust = FIntVector(Elem.Location.X,Elem.Location.Y,HeightOnStart);

	Elem.CacheMatDyn->SetVectorParameterValue("PatchLocation", FVector(RemoveAnySphereAdjust));
	
	// 1) Intersect clipmap with grid quad
	// 2) Gather non computed quads
	// 3) Allocated Compute element to missing Quad
	// 4) Update the indirection data to the new elements
	// 5) Update the Clipmap Heightmap with the grid data 
	
	//UKismetRenderingLibrary::ClearRenderTarget2D(this, Elem.HeightMap_Segmented, FLinearColor::Black);
	/*
	UKismetRenderingLibrary::DrawMaterialToRenderTarget(this, Elem.HeightMap_Segmented, Elem.CacheMatDyn);

	if (BrushManager)
		BrushManager->ApplyBrushStackToHeightMap(this, Elem.Level, Elem.HeightMap_Segmented, FVector(RemoveAnySphereAdjust), Elem.GridSpacing, N, false);

	if (Subsystem)
		Subsystem->CopyAtoB(Elem.HeightMap_Segmented, Elem.HeightMap,nullptr, index > 0 ? (Meshes[index - 1].DrawingThisFrame ? 0 : 2) : 0);
	*/

#if SW_COMPUTE_GENERATION
	
#else
	UKismetRenderingLibrary::DrawMaterialToRenderTarget(this, Elem.HeightMap, Elem.CacheMatDyn);
#endif
	/*
	if(Subsystem && (index > 0))
	{
		if (Meshes[index - 1].DrawingThisFrame)
			Subsystem->CopyAtoB(Elem.HeightMap, Elem.HeightMap_Segmented, nullptr, 0);
		else
			Subsystem->CopyAtoB(Elem.HeightMap, Elem.HeightMap_Segmented, nullptr, 2);
	}	*/

	if (BrushManager)
		BrushManager->ApplyBrushStackToHeightMap(this, Elem.Level, Elem.HeightMap, FVector(RemoveAnySphereAdjust), Elem.GridSpacing, N, false);

	//if (Subsystem)
	//	Subsystem->CopyAtoB(Elem.HeightMap_Segmented, Elem.HeightMap, nullptr, index > 0 ? (Meshes[index - 1].DrawingThisFrame ? 0 : 2) : 0);

}

void AShaderWorldActor::TopologyUpdate(int index)
{
	if (index >= Meshes.Num())
		return;

	
	if(((Meshes.Num() - 1) - index) >= TopologyFixUnderLOD)
		return;

	const FClipMapMeshElement& Elem = Meshes[index];

	if (!Elem.HeightMap_Segmented)
		return;

	if (SWorldSubsystem)
	{
		if (Elem.Mesh && (Elem.Mesh->IsMeshSectionVisible(0) || Elem.Mesh->IsMeshSectionVisible(1)))
			Elem.Mesh->UpdateSectionTopology(Elem.Mesh->IsMeshSectionVisible(0) ? 0 : 1, N, Elem.GridSpacing, Elem.HeightMap);
	}
}

void AShaderWorldActor::CopyHeightmapSegmentedToHeightMap(int index)
{
	if (index >= Meshes.Num())
		return;

	FClipMapMeshElement& Elem = Meshes[index];

	if (!Elem.HeightMap_Segmented)
		return;

	if (SWorldSubsystem)
		SWorldSubsystem->CopyAtoB(Elem.HeightMap_Segmented, Elem.HeightMap, 0);

}

void AShaderWorldActor::CopyNormalMapSegmentedToNormalMap(int index)
{
	if (index >= Meshes.Num())
		return;

	FClipMapMeshElement& Elem = Meshes[index];

	if (!Elem.NormalMap_Segmented)
		return;

	if (SWorldSubsystem)
		SWorldSubsystem->CopyAtoB(Elem.NormalMap_Segmented, Elem.NormalMap, 0);
}

void AShaderWorldActor::CopyLayersSegmentedToLayers(int index)
{
	if (index >= Meshes.Num())
		return;

	FClipMapMeshElement& Elem = Meshes[index];

	for(int32 LayerIndex = 0; LayerIndex< Elem.LandLayers_Segmented.Num(); LayerIndex++)
	{
		ensure(LayerIndex < Elem.LandLayers.Num());

		if (SWorldSubsystem)
			SWorldSubsystem->CopyAtoB(Elem.LandLayers_Segmented[LayerIndex], Elem.LandLayers[LayerIndex], 0);
	}

	
}

void AShaderWorldActor::ComputeNormalForClipMap(int index, bool bFromSegmented)
{
	if (index >= Meshes.Num())
		return;

	FClipMapMeshElement& Elem = Meshes[index];

	if (SWorldSubsystem)
		SWorldSubsystem->ComputeNormalForHeightmap(bFromSegmented? Elem.HeightMap_Segmented :Elem.HeightMap.Get(), Elem.NormalMap,N, Elem.GridSpacing,HeightScale);

}


void AShaderWorldActor::ComputeSegmentedNormalForClipMap(int index)
{
	if (index >= Meshes.Num())
		return;

	FClipMapMeshElement& Elem = Meshes[index];

	if (SWorldSubsystem)
		SWorldSubsystem->ComputeNormalForHeightmap(Elem.HeightMap_Segmented , Elem.NormalMap_Segmented, N, Elem.GridSpacing, HeightScale);

}

void AShaderWorldActor::ComputeDataLayersSegmentedForClipMap(int index)
{
	SW_FCT_CYCLE()

		if (index >= Meshes.Num())
			return;

	FClipMapMeshElement& Elem = Meshes[index];

	for (int k = 0; k < Elem.LandLayers_Segmented.Num(); k++)
	{
		if (!Elem.LayerMatDyn[k] || !Elem.LandLayers_Segmented[k])
		{
			UE_LOG(LogTemp, Warning, TEXT("ERROR drawing layers: !Elem.LayerMatDyn[%d] || !Elem.LandLayers_Segmented[%d]"), k, k);
			continue;
		}

		Elem.LayerMatDyn[k]->SetVectorParameterValue("PatchLocation", FVector(Elem.Location));

		if (Elem.LandLayers_NeedParent[k])
		{
			if (Elem.Level > 0)
			{
				Elem.LayerMatDyn[k]->SetVectorParameterValue("RingLocation_Parent", FVector(Meshes[Elem.Level - 1].Location));
			}
			else
			{
				Elem.LayerMatDyn[k]->SetVectorParameterValue("RingLocation_Parent", FVector(Elem.Location));
			}
		}

		//UKismetRenderingLibrary::ClearRenderTarget2D(this, Elem.LandLayers_Segmented[k], FLinearColor::Black);
		
#if 0 //SW_COMPUTE_GENERATION
		
#else
		UKismetRenderingLibrary::DrawMaterialToRenderTarget(this, Elem.LandLayers_Segmented[k], Elem.LayerMatDyn[k]);
#endif
		FString LocalLayerName = Elem.LandLayers_names[k].ToString();
		if (BrushManager)
			BrushManager->ApplyBrushStackToLayer(this, Elem.Level, Elem.LandLayers_Segmented[k], FVector(Elem.Location), Elem.GridSpacing, N, LocalLayerName);
	}
}

void AShaderWorldActor::ComputeDataLayersForClipMap(int index)
{
	SW_FCT_CYCLE()

	if (index >= Meshes.Num())
		return;

	FClipMapMeshElement& Elem = Meshes[index];

	for (int k = 0; k < Elem.LandLayers.Num(); k++)
	{
		if (!Elem.LayerMatDyn[k] || !Elem.LandLayers[k])
		{
			UE_LOG(LogTemp, Warning, TEXT("ERROR drawing layers: !Elem.LayerMatDyn[%d] || !Elem.LandLayers[%d]"), k, k);
			continue;
		}

		Elem.LayerMatDyn[k]->SetVectorParameterValue("PatchLocation", FVector(Elem.Location));

		if(Elem.LandLayers_NeedParent[k])
		{
			if(Elem.Level>0)
			{
				Elem.LayerMatDyn[k]->SetVectorParameterValue("RingLocation_Parent", FVector(Meshes[Elem.Level-1].Location));
			}
			else
			{
				Elem.LayerMatDyn[k]->SetVectorParameterValue("RingLocation_Parent", FVector(Elem.Location));
			}
		}
		
		UKismetRenderingLibrary::ClearRenderTarget2D(this, Elem.LandLayers[k], FLinearColor::Black);
		
#if 0 //SW_COMPUTE_GENERATION
		
#else
		UKismetRenderingLibrary::DrawMaterialToRenderTarget(this, Elem.LandLayers[k], Elem.LayerMatDyn[k]);
#endif
		FString LocalLayerName = Elem.LandLayers_names[k].ToString();
		if (BrushManager)
			BrushManager->ApplyBrushStackToLayer(this, Elem.Level, Elem.LandLayers[k], FVector(Elem.Location), Elem.GridSpacing, N, LocalLayerName);
	}
}

int32 AShaderWorldActor::SphericalProjection(FIntVector Destination)
{
	FIntVector PlanetRef = Destination/100000 - FIntVector(0,0,-PlanetRadiusKm);

	int32 Z_square = PlanetRadiusKm*PlanetRadiusKm - (PlanetRef.X*PlanetRef.X + PlanetRef.Y*PlanetRef.Y);

	float blendOutside = FMath::Min(FMath::Max(0.0,1-Z_square*100000.0),1.0);

	Z_square = (int32)FMath::Sqrt((float)FMath::Max(0.f,(float)Z_square));

	return (-PlanetRadiusKm + Z_square-Destination.Z)*100000;

}

void AShaderWorldActor::UpdateClipMap()
{
	SW_FCT_CYCLE()

	if(!CameraSet)
		return;

	WorldCycle++;

	float Height = FMath::Abs((CamLocation - GetActorLocation()).Z);


	if (GenerateCollision )
	{
		float RelevantHeight = HeightToClosestCollisionMesh();

		Height=FMath::Abs(RelevantHeight);
	}

	bool Segmented = UseSegmented();

	const double CurrentTime = FPlatformTime::Seconds();

	if(Segmented)
		Segmented_Initialized=true;


	if(UpdateHOverTime && bHadGeneratorAtRebuildTime && (UpdateDelayForLODRuntime.Num() == Meshes.Num()))
	{

		const int32 LodNum = Meshes.Num();
		int32 NotVisibleLOD = LodNum;
		bool bParentDrawing=false;


		for (int i = 0; i < Meshes.Num(); i++)
		{
			FClipMapMeshElement& Elem = Meshes[i];			
			
			if (Height > Elem.GridSpacing * AltitudeToLODTransition * N && Elem.Level > 0)
			{				
				NotVisibleLOD = i;
				break;
			}
			
		}

		for (int i = 0; i < Meshes.Num(); i++)
		{
			FClipMapMeshElement& Elem = Meshes[i];

			if(i >= NotVisibleLOD)
			break;

			int32 IndexInIncreasingDelayOrder = (NotVisibleLOD-1)-i;

			Elem.UpdateDelay = UpdateDelayForLODRuntime[LodNum -1 - IndexInIncreasingDelayOrder];
		}

	}

	USWorldSubsystem* ShaderWorldSubsystem = SWorldSubsystem;

	for(int i=0; i< Meshes.Num(); i++)
	{
		FClipMapMeshElement& Elem = Meshes[i];
		
		if(!Elem.Mesh)
			continue;

		Elem.DrawingThisFrame = false;

		if(Height>Elem.GridSpacing * AltitudeToLODTransition * N && Elem.Level>0)
		{
			if(Elem.IsSectionVisible(0,Segmented))
				Elem.SetSectionVisible(0,false,Segmented);
			if(Elem.IsSectionVisible(1,Segmented))
				Elem.SetSectionVisible(1,false,Segmented);
			if(Elem.IsSectionVisible(2,Segmented))
				Elem.SetSectionVisible(2,false,Segmented);
			if(Elem.IsSectionVisible(3,Segmented))
				Elem.SetSectionVisible(3,false,Segmented);
			if(Elem.IsSectionVisible(4,Segmented))
				Elem.SetSectionVisible(4,false,Segmented);
			if(Elem.IsSectionVisible(5,Segmented))
				Elem.SetSectionVisible(5,false,Segmented);

			Elem.Config = EClipMapInteriorConfig::NotVisible;
			
		}
		else
		{
			if(Height>Elem.GridSpacing * AltitudeToLODTransition * N/2.f || Elem.Level==LOD_Num-1)
			{
				
				if(!Elem.IsSectionVisible(0,Segmented))
					Elem.SetSectionVisible(0,true,Segmented);
				if(Elem.IsSectionVisible(1,Segmented))
					Elem.SetSectionVisible(1, false,Segmented);
				if(Elem.IsSectionVisible(2,Segmented))
					Elem.SetSectionVisible(2, false,Segmented);
				if(Elem.IsSectionVisible(3,Segmented))
					Elem.SetSectionVisible(3, false,Segmented);
				if(Elem.IsSectionVisible(4,Segmented))
					Elem.SetSectionVisible(4, false,Segmented);
				if(Elem.IsSectionVisible(5,Segmented))
					Elem.SetSectionVisible(5, false,Segmented);

				Elem.Config = EClipMapInteriorConfig::NotVisible;
				
			}
			else
			{
				if(Elem.IsSectionVisible(0,Segmented))
					Elem.SetSectionVisible(0, false,Segmented);
				if(!Elem.IsSectionVisible(1,Segmented))
					Elem.SetSectionVisible(1, true,Segmented);

			}

			const FVector CompToCam = CamLocation - FVector(Elem.Location-GetWorld()->OriginLocation);
			const float MaxPlanarOffset = FMath::Max(FMath::Abs(CompToCam.X),FMath::Abs(CompToCam.Y));
			
				bool bForceUpdate = false;

				// This is purely a stability safeguard for a case that should never happen
				//	Precision issues on android on visible map beyond 5km side. (Fortnite is 3.5km?)	
				// If we're beyond 20km we'll have precision issue unless relocation origin anyway - skip test
				if (!(MaxPlanarOffset>Elem.GridSpacing) && (Elem.NeedComponentLocationUpdate==0) && (Elem.Level > 0) && (Elem.IsSectionVisible(0,Segmented) || Elem.IsSectionVisible(1,Segmented)) && FVector(Elem.Location-GetWorld()->OriginLocation).Size()<2000000.f/*20km*/)
				{
					FVector LocationToParent = FVector(Elem.Location - Meshes[Elem.Level - 1].Location);
					if(Elem.Mesh)
						LocationToParent = FVector(Elem.Mesh->GetComponentLocation() - Meshes[Elem.Level - 1].Mesh->GetComponentLocation());

					float MarginOfError = 0.8f;
					if (abs(abs(LocationToParent.X) - Elem.GridSpacing) > MarginOfError || abs(abs(LocationToParent.Y) - Elem.GridSpacing) > MarginOfError)
					{
						bForceUpdate=true;
						UE_LOG(LogTemp,Warning,TEXT("ERROR Ring Location has offset %f %f Forced grid relocate |local gridspacing: %f , ParentToChild X %f Y %f"),abs(abs(LocationToParent.X) - Elem.GridSpacing),abs(abs(LocationToParent.Y) - Elem.GridSpacing), Elem.GridSpacing,LocationToParent.X,LocationToParent.Y);
					
						//rebuild=true;
					}
				}

				if(UpdateHOverTime && i>0)
				{
					if(Meshes[i-1].DrawingThisFrame)
						Elem.DrawingThisFrame=true;
				}

				const bool bRedrawingBecauseOfCamera = MaxPlanarOffset > Elem.GridSpacing || bForceUpdate;
				const bool bRedrawingBecauseOfTime = (Elem.UpdateDelay > 0.0) && ((CurrentTime - Elem.LatestUpdateTime) >= Elem.UpdateDelay) || UpdateHOverTime && Elem.DrawingThisFrame;

				if(bRedrawingBecauseOfCamera || bRedrawingBecauseOfTime)
				{
					Elem.LatestUpdateTime = CurrentTime;

					double X_cam = CamLocation.X + GetWorld()->OriginLocation.X;
					double Y_cam = CamLocation.Y + GetWorld()->OriginLocation.Y;

					double Spacing = Elem.GridSpacing;

					double X_LocRef = 2.0*Spacing*floor(X_cam/(2.0*Spacing));
					double Y_LocRef = 2.0*Spacing*floor(Y_cam/(2.0*Spacing));

					double X_Diff = X_cam-X_LocRef;
					double Y_Diff = Y_cam-Y_LocRef;

					double ToLoc_X = -Spacing;
					double ToLoc_Y = -Spacing;

					if (X_Diff > 0.f && Y_Diff > 0.f)
					{
						ToLoc_X += 2.0 * Spacing;
						ToLoc_Y += 2.0 * Spacing;						
					}
					else if (X_Diff > 0.f && Y_Diff <= 0.f)
					{
						ToLoc_X += 2.0 * Spacing;							
					}
					else if (X_Diff <= 0.f && Y_Diff > 0.f)
					{
						ToLoc_Y += 2.0 * Spacing;	
					}
					
					Elem.Location = FIntVector(X_LocRef+ToLoc_X,Y_LocRef+ToLoc_Y,HeightOnStart);

					//Attempt to mitigate GBuffer velocity contributions in UE5 which didn't seem to be a problem on UE4
					//if (Elem.Mesh && Elem.NeedComponentLocationUpdate > 0 && FVector(Elem.Location - Elem.LocationLastMove).Length() > Elem.GridSpacing * (N - 1) / 2.05f)
					if(false)
					{
						Elem.NeedComponentLocationUpdate = 0;
						MoveClipMapComponentToLocation(i);
					}

					if(WorldShape != EWorldShape::Flat)
					{
						int VerticalOffset = SphericalProjection(Elem.Location);

						Elem.Location+=FIntVector(0,0,VerticalOffset);
					}


					if(Segmented)
					{						
						SegmentedUpdateProcessed = false;
						ClipMapToUpdateAndMove[i] = true;
						NeedSegmentedUpdate[i] = true;				
					}
					else
					{
						if(bRedrawingBecauseOfCamera)
							MoveClipMapMaterialLocation(i);

						if(bHadGeneratorAtRebuildTime)
						{
							
							if((i>0) && !Meshes[i-1].DrawingThisFrame)
							{
								/*
								 * This LOD is moving, but our parent do not, when we compute our heightmap, our borders won't match the heightmap of our parent at inner border anymore
								 * Force our parent to recompute his Heightmap while preserving its own outer border with its own parent
								 */
								if(ShaderWorldSubsystem)
								{
									ShaderWorldSubsystem->CopyAtoB(Meshes[i - 1].HeightMap, Meshes[i - 1].HeightMap_Segmented, nullptr, 0);
								}

								ComputeHeightMapForClipMap(i - 1);

								if (ShaderWorldSubsystem)
								{
									ShaderWorldSubsystem->CopyAtoB(Meshes[i - 1].HeightMap, Meshes[i - 1].HeightMap_Segmented, nullptr, 2);
								}

								ComputeNormalForClipMap(i - 1, false);
								ComputeDataLayersForClipMap(i - 1);

								if (ShaderWorldSubsystem)
								{
									ShaderWorldSubsystem->CopyAtoB(Meshes[i - 1].HeightMap_Segmented, Meshes[i - 1].HeightMap, nullptr, 0);
								}

								//if (ShaderWorldSubsystem && ((i-1) > 0) && !Meshes[(i - 1) - 1].DrawingThisFrame)
								//	ShaderWorldSubsystem->CopyAtoB(Meshes[i - 1].HeightMap_Segmented, Meshes[i - 1].HeightMap, nullptr, 2);
							}

							

							if (Elem.CacheMatDyn)
							{
								/*
								 * If we reached here, we forced out parent to be recomputed
								 */
								ComputeHeightMapForClipMap(i);
								ComputeNormalForClipMap(i, false);
								ComputeDataLayersForClipMap(i);					
							}
							else
							{
								UE_LOG(LogTemp, Warning, TEXT("ERROR Recreating the clipmap cache computation materials - should not be happening"));
							}							
						}
					}
					
				}
				else
				{
					if(BrushManager && BrushManagerRedrawScopes.Num()>0)
					{
						

						if (bHadGeneratorAtRebuildTime)
						{
							if (Elem.CacheMatDyn)
							{
								
								if (Segmented)
								{
									SegmentedUpdateProcessed = false;
									ClipMapToUpdate[i] = true;
									NeedSegmentedUpdate[i] = true;
								}
								else
								{
									if ((i > 0) && !Meshes[i - 1].DrawingThisFrame)
									{										
										ComputeHeightMapForClipMap(i - 1);
										ComputeNormalForClipMap(i - 1, false);
										ComputeDataLayersForClipMap(i - 1);
									}

									ComputeHeightMapForClipMap(i);
									ComputeNormalForClipMap(i,false);
									ComputeDataLayersForClipMap(i);
								}
								
							}
							else
							{
								UE_LOG(LogTemp, Warning, TEXT("ERROR Recreating the clipmap cache computation materials - should not be happening"));
							}
						}
					}
					else
					{						
						if (Elem.Mesh && Elem.NeedComponentLocationUpdate>0)
						{
							Elem.NeedComponentLocationUpdate = 0;
							MoveClipMapComponentToLocation(i);
						}
		
					}
					
				}				

				
				if (Elem.Level > 0 && Meshes[Elem.Level - 1].Config!=RelativeLocationToParentInnerMeshConfig(FVector(Elem.Location - Meshes[Elem.Level - 1].Location)))
				{
					UpdateParentInnerMesh(Elem.Level, RelativeLocationToParentInnerMeshConfig(FVector(Elem.Location - Meshes[Elem.Level - 1].Location)), Segmented);
				}
		}
	}

	
	if (DataReceiver && !Segmented)
		DataReceiver->UpdateStaticDataFor(this, CamLocation);
}

void AShaderWorldActor::FixNamingCollision(const UWorld* World, const FCollisionMeshElement& Mesh,const FString& Rename_str)
{
	const ERenameFlags RenFlags = REN_NonTransactional | REN_DontCreateRedirectors | REN_ForceNoResetLoaders | REN_DoNotDirty;

	UObject* FoundExistingName = StaticFindObject(UShaderWorldCollisionComponent::StaticClass(), Mesh.Mesh->GetOuter(), *Rename_str, true);
	FString Rename_str_invalid = Rename_str + "_invalid";
	bool NoNameCollision = true;
	if (FoundExistingName)
	{
		NoNameCollision=false;
		const FString ARandNum = FString::FromInt(FMath::RoundToInt(FPlatformTime::Seconds()));
		const FString ARandNumOther = FString::FromInt(FMath::RandRange(0,999));

		for (int k = 0; k < 100; k++)
		{
			Rename_str_invalid = Rename_str + "_invalid" + ARandNum + ARandNumOther + FString::FromInt(k);
			UObject* FoundExistingName_error = StaticFindObject(UShaderWorldCollisionComponent::StaticClass(), Mesh.Mesh->GetOuter(), *Rename_str_invalid, true);
			if (!FoundExistingName_error)
			{
				FoundExistingName->Rename(*Rename_str_invalid, FoundExistingName->GetOuter(), RenFlags);
				NoNameCollision = true;
				break;
			}

			if (k == 99)
			{
				Rename_str_invalid = Rename_str + "_invalid" + ARandNum + ARandNumOther + FString::FromInt(FMath::Rand()+k);
				FoundExistingName_error = StaticFindObject(UShaderWorldCollisionComponent::StaticClass(), Mesh.Mesh->GetOuter(), *Rename_str_invalid, true);
				if (!FoundExistingName_error)
				{
					FoundExistingName->Rename(*Rename_str_invalid, FoundExistingName->GetOuter(), RenFlags);
					NoNameCollision = true;
					break;
				}
				
			}

		}
	}

	if (GetLocalRole() != ROLE_Authority)
	{
		FName CurrentName = Mesh.Mesh->GetFName();

		FWorldContext* WorldContext = GEngine->GetWorldContextFromWorld(World);
		for (FNamedNetDriver& Driver : WorldContext->ActiveNetDrivers)
		{
			if (Driver.NetDriver && Driver.NetDriver->GuidCache.IsValid())
			{
				for (TPair<FNetworkGUID, FNetGuidCacheObject>& GuidPair : Driver.NetDriver->GuidCache->ObjectLookup)
				{
					const bool bIsPackage = GuidPair.Key.IsStatic() && GuidPair.Value.OuterGUID.IsValid() && (GuidPair.Value.PathName == CurrentName);
					if (bIsPackage)
					{
						GuidPair.Value.PathName = FName(Rename_str);
					}

				}
			}
		}
	}

	if(NoNameCollision)
		Mesh.Mesh->Rename(*Rename_str, Mesh.Mesh->GetOuter(), RenFlags);
	else
	{
		const FString Corrected = Rename_str + "_FailedRenaming_"+FString::FromInt(FMath::Rand());
		if(!StaticFindObject(UShaderWorldCollisionComponent::StaticClass(), Mesh.Mesh->GetOuter(), *Corrected, true))
		{
			Mesh.Mesh->Rename(*Corrected, Mesh.Mesh->GetOuter(), RenFlags);
		}		
		UE_LOG(LogTemp, Warning, TEXT("ERROR enable to find proper renaming for pre existing collision mesh"))
	}
}

inline double AShaderWorldActor::GetHeightFromGPURead(uint8* ReadLoc,uint16& MaterialIndice)
{
	MaterialIndice = ReadLoc[3];

	const bool Positive = !((ReadLoc[2] & 0x80) > 0);

	return (int32)((Positive ? 0x0 : 0xFF) << 24 | (ReadLoc[2] | (Positive ? 0x0 : 0x80)) << 16 | ReadLoc[1] << 8 | ReadLoc[0]);	
}

double AShaderWorldActor::GetHeightFromGPUReadOld(FColor& ReadLoc, uint16& MaterialIndice)
{

	int Height = 0;
	uint8* HeightAs8 = reinterpret_cast<uint8*>(&Height);
	uint8 R_ = ReadLoc.R & 0x7F;
	const bool Positive = !((ReadLoc.R & 0x80) > 0);

	HeightAs8[0] = ReadLoc.B;
	HeightAs8[1] = ReadLoc.G;
	HeightAs8[2] = ReadLoc.R | (Positive ? 0x0 : 0x80);
	HeightAs8[3] = Positive ? 0x0 : 0xFF;

	MaterialIndice = ReadLoc.A;

	return Height;
}


double AShaderWorldActor::ComputeWorldHeightAt(FVector WorldLocation)
{
	//Implement your noise here // same one as the one in shader



	return 0.f;


}

void ReadPixelsFromRT(UTextureRenderTarget2D* InRT, FCollisionMeshElement& Mesh)
{

	ENQUEUE_RENDER_COMMAND(ReadGeoClipMapRTCmd)(
		[InRT, HeightData = Mesh.HeightData, Completion = Mesh.ReadBackCompletion](FRHICommandListImmediate& RHICmdList)
	{		
		check(IsInRenderingThread());

		if (HeightData.IsValid() && InRT->GetResource())
		{			
			FRDGBuilder GraphBuilder(RHICmdList);

			TSharedPtr<FRHIGPUTextureReadback> ReadBackStaging = MakeShared<FRHIGPUTextureReadback>(TEXT("SWGPUTextureReadback"));

			FRDGTextureRef RDGSourceTexture = RegisterExternalTexture(GraphBuilder, InRT->GetResource()->TextureRHI, TEXT("SWSourceTextureToReadbackTexture"));

			AddEnqueueCopyPass(GraphBuilder, ReadBackStaging.Get(), RDGSourceTexture);

			GraphBuilder.Execute();

			GSWSimpleReadbackManager.AddPendingReadBack(GPixelFormats[RDGSourceTexture->Desc.Format].BlockBytes, RDGSourceTexture->Desc.Extent.X, RDGSourceTexture->Desc.Extent.Y, ReadBackStaging, const_cast<TSharedPtr<FSWColorRead, ESPMode::ThreadSafe>&>(HeightData), const_cast<TSharedPtr < FThreadSafeBool, ESPMode::ThreadSafe>&>(Completion));
		}
		
	});
	

}

void AShaderWorldActor::UpdateCollisionMeshData(FCollisionMeshElement& Mesh)
{
	// Couple options i see here, either make a readback from a render target applying the same noise than the geoclipmap mesh
	// or implement the same noise in C++ and compute it in parallel/on another thread

	//FVector MesgLoc = Mesh.Mesh->GetComponentLocation()+FVector(GetWorld()->OriginLocation);

	FVector MesgLoc = FVector((Mesh.Location* CollisionResolution*(CollisionVerticesPerPatch-1) + FIntVector(0.f, 0.f, 1) * HeightOnStart));

	if (!Generator)
	{
#if SWDEBUG
	SW_LOG("Generator Material not available for Shader World %s",*GetName())
#endif
	}
	else
	{
		UWorld* World = GetWorld();

		//OPTION A : Compute collision form GPU readback
		UMaterialInstanceDynamic* DynCollisionMat = Mesh.DynCollisionCompute;
		
		if(!DynCollisionMat)
		{
			
			DynCollisionMat = UMaterialInstanceDynamic::Create(Generator.Get(), this);

			DynCollisionMat->SetScalarParameterValue("NoMargin", 1.f);
			DynCollisionMat->SetScalarParameterValue("TexelPerSide", CollisionVerticesPerPatch);			
			DynCollisionMat->SetScalarParameterValue("PatchFullSize", CollisionResolution * (CollisionVerticesPerPatch - 1));
			DynCollisionMat->SetScalarParameterValue("MeshScale", CollisionResolution *(CollisionVerticesPerPatch <=1? 1 : CollisionVerticesPerPatch));


			TSet<FName> UsedNames;
			for (const FInstancedStruct& Seed : CurrentSeedsArray.SeedsArray)
			{
				if (Seed.IsValid())
				{
					const UScriptStruct* Type = Seed.GetScriptStruct();
					CA_ASSUME(Type);
					if (Type->IsChildOf(FTextureSeed::StaticStruct()))
					{
						FTextureSeed& TypedSeed = Seed.GetMutable<FTextureSeed>();
						if (!UsedNames.Contains(TypedSeed.SeedName))
						{
							UsedNames.Add(TypedSeed.SeedName);
							DynCollisionMat->SetTextureParameterValue(TypedSeed.SeedName, TypedSeed.Value);
						}
					}
					else if (Type->IsChildOf(FLinearColorSeed::StaticStruct()))
					{
						FLinearColorSeed& TypedSeed = Seed.GetMutable<FLinearColorSeed>();
						if (!UsedNames.Contains(TypedSeed.SeedName))
						{
							UsedNames.Add(TypedSeed.SeedName);
							DynCollisionMat->SetVectorParameterValue(TypedSeed.SeedName, TypedSeed.Value);
						}
					}
					else if (Type->IsChildOf(FScalarSeed::StaticStruct()))
					{
						FScalarSeed& TypedSeed = Seed.GetMutable<FScalarSeed>();
						if (!UsedNames.Contains(TypedSeed.SeedName))
						{
							UsedNames.Add(TypedSeed.SeedName);
							DynCollisionMat->SetScalarParameterValue(TypedSeed.SeedName, TypedSeed.Value);
						}
					}
					else
					{
#if SWDEBUG
						SW_LOG("Invalid Seed type found: '%s'", *GetPathNameSafe(Type));
#endif

					}
				}
			}
			if (WorldShape == EWorldShape::Spherical)
			{

				DynCollisionMat->SetScalarParameterValue("PlanetRadiusKm", PlanetRadiusKm);
			}
			
			Mesh.DynCollisionCompute = DynCollisionMat;
		}
		
		DynCollisionMat->SetVectorParameterValue("PatchLocation",MesgLoc);	

		
		//UKismetRenderingLibrary::ClearRenderTarget2D(this, Mesh.CollisionRT, FLinearColor::Black);

#if SW_COMPUTE_GENERATION
		
#else
		UKismetRenderingLibrary::DrawMaterialToRenderTarget(this, Mesh.CollisionRT, DynCollisionMat);
#endif

		if (BrushManager)
			BrushManager->ApplyBrushStackToHeightMap(this,0,Mesh.CollisionRT, MesgLoc, CollisionResolution, CollisionVerticesPerPatch, true);

		if(bExportPhysicalMaterialID_cached && (LayerStoringMaterialID != "") && GetMeshNum()>0 && CollisionMesh.Num()>0 && CollisionMesh[0].CollisionRT_Duplicate)
		{


			if (USWorldSubsystem* ShaderWorldSubsystem = SWorldSubsystem)
			{
				
				bool LayerIsRelevant = false;
				FString LayerSource = LayerStoringMaterialID.ToString();

				for(auto& La : LandDataLayers)
				{				 
					if(La.LayerName== LayerSource)
					{
						LayerIsRelevant = true;
						break;
					}
				}

				if(LayerIsRelevant)
				{
					FVector2D Location_Mesh(MesgLoc);
					FVector2D Extent = CollisionResolution * (CollisionVerticesPerPatch - 1) / 2.f * FVector2D(1.f, 1.f);//Margin
					FBox2D LocalMeshBox(Location_Mesh - Extent, Location_Mesh + Extent);

					int LOD_Candidate = -1;
					///////////////////////////////

					for (int k = 0; k < GetMeshNum(); k++)
					{
						FClipMapMeshElement& Elem_Local = GetMesh(k);
						FIntVector ClipMapLocation = Elem_Local.Location - GetWorld()->OriginLocation;

						FVector2D Location_Elem_Local(ClipMapLocation.X, ClipMapLocation.Y);
						FVector2D Extent_Elem_Local = (N - 1) * Elem_Local.GridSpacing / 2.f * FVector2D(1.f, 1.f);
						FBox2D Elem_Local_Footprint(Location_Elem_Local - Extent_Elem_Local, Location_Elem_Local + Extent_Elem_Local);

						if (Elem_Local_Footprint.IsInside(LocalMeshBox) && (Elem_Local.IsSectionVisible(0) || Elem_Local.IsSectionVisible(1)))
						{
							LOD_Candidate = k;
						}
						else
						{
							break;
						}
					}

					if (LOD_Candidate>=0 /* && (LOD_Candidate >= (GetMeshNum() - 4)) */)
					{
						FClipMapMeshElement& Elem_Local = GetMesh(LOD_Candidate);
						float PatchSize = (N-1) * Elem_Local.GridSpacing;

						for (int k = 0; k < Elem_Local.LandLayers.Num(); k++)
						{
							if(Elem_Local.LandLayers_names[k] == LayerStoringMaterialID)
							{
								ShaderWorldSubsystem->CopyAtoB(Mesh.CollisionRT, CollisionMesh[0].CollisionRT_Duplicate);

								uint8 channel = (static_cast<uint8>(LayerChannelStoringID))+1;

								ShaderWorldSubsystem->CopyAtoB(Elem_Local.LandLayers[k], Mesh.CollisionRT, CollisionMesh[0].CollisionRT_Duplicate, 0, channel, FVector2D(FVector(Elem_Local.Location)), FVector2D(MesgLoc), PatchSize, CollisionResolution * (CollisionVerticesPerPatch - 1));

								break;
							}
						}
						
					}
					else
					{
						UE_LOG(LogTemp, Warning, TEXT("Non Admissible LOD_Candidate %d"), LOD_Candidate);
					}
				}
				else
				{
					UE_LOG(LogTemp, Warning, TEXT("No relevant data layer"));
				}
			}
			
		}

		if(!Mesh.HeightData.IsValid())
		{
			Mesh.HeightData = MakeShared<FSWColorRead, ESPMode::ThreadSafe>();
			Mesh.HeightData->ReadData.SetNum(CollisionVerticesPerPatch * CollisionVerticesPerPatch);
		}

		if (!Mesh.ReadBackCompletion.IsValid())
		{
			Mesh.ReadBackCompletion = MakeShared<FThreadSafeBool, ESPMode::ThreadSafe>();
		}

		Mesh.ReadBackCompletion->AtomicSet(false);	


		ReadPixelsFromRT(Mesh.CollisionRT,Mesh);

		CollisionReadToProcess.Add(Mesh.ID);
		
		return;
	}
	
	return;

	//OPTION B : Implement in c++ the same noise as the one in Shader graph and evaluate the noise here to generate the collision mesh
	
	/*
	FGeoCProcMeshSection* Section = Mesh.Mesh->GetProcMeshSection(0);

	int NumOfVertex = Section->ProcVertexBuffer.Num();

	TArray<FVector> Vertices;
	Vertices.SetNum(NumOfVertex);
	TArray<FVector> Normals;
	Normals.SetNum(NumOfVertex);
	TArray<FVector2D> UV;
	UV.SetNum(NumOfVertex);
	TArray<FColor> Colors;
	Colors.SetNum(NumOfVertex);

	
	ParallelFor(NumOfVertex, [&](int32 k)
	{
		FVector LocationfVertice_WS = Section->ProcVertexBuffer[k].Position*FVector(1.f,1.f,0.f) + MesgLoc;

		// ComputeWorldHeightAt is empty / not Implemented

		LocationfVertice_WS.Z = ComputeWorldHeightAt(LocationfVertice_WS);

		Vertices[k] = LocationfVertice_WS - Mesh.Mesh->GetComponentLocation();
		Normals[k] = FVector(0.f,0.f,1.f);
		UV[k] = FVector2D(0.f,0.f);
		Colors[k] = FColor::Blue;
	});

	TArray<FGeoCProcMeshTangent> Tangents;
	Tangents.Init(FGeoCProcMeshTangent(FVector(0.f, 0.f, 1.f), false), Vertices.Num());


	Mesh.Mesh->UpdateMeshSection(0,Vertices,Normals,UV,Colors,Tangents);
	*/

}

void AShaderWorldActor::GetLocalTransformOfSpawnable(FInstancedStaticMeshInstanceData& OutTransform, const FVector& CompLoc, FColor& LocX,FColor& LocY,FColor& LocZ,FColor& Rot,/*FColor& Scale, */ FInstancedStaticMeshInstanceData& FromT, const bool& IsAdjustement, const FFloatInterval& AtitudeRange,const FVector& MeshLoc)
{
	int X = 0;
	uint8* XAs8 = reinterpret_cast<uint8*>(&X);
	
	LocX.A = (LocX.R&0x80)*255/128;

	uint8 HighBit = ((LocX.R) & 0x80) | ((((LocX.A) & 0xFE) >> 1) & 0x7F);
	uint8 LowerBit = ((LocX.A) & 1) << 7 | ((LocX.R) & 0x7F);

	XAs8[0] = LocX.B;
	XAs8[1] = LocX.G;
	XAs8[2] = LowerBit;
	XAs8[3] = HighBit;

	X+=MeshLoc.X;

	int Y = 0;
	uint8* YAs8 = reinterpret_cast<uint8*>(&Y);

	LocY.A = (LocY.R&0x80)*255/128;
	HighBit = ((LocY.R) & 0x80) | ((((LocY.A) & 0xFE) >> 1) & 0x7F);
	LowerBit = ((LocY.A) & 1) << 7 | ((LocY.R) & 0x7F);

	YAs8[0] = LocY.B;
	YAs8[1] = LocY.G;
	YAs8[2] = LowerBit;
	YAs8[3] = HighBit;

	Y += MeshLoc.Y;

	int Z = 0;
	uint8* ZAs8 = reinterpret_cast<uint8*>(&Z);

	LocZ.A = (LocZ.R&0x80)*255/128;
	HighBit = ((LocZ.R) & 0x80) | ((((LocZ.A) & 0xFE) >> 1) & 0x7F);
	LowerBit = ((LocZ.A) & 1) << 7 | ((LocZ.R) & 0x7F);

	ZAs8[0] = LocZ.B;
	ZAs8[1] = LocZ.G;
	ZAs8[2] = LowerBit;
	ZAs8[3] = HighBit;

	Z += MeshLoc.Z;

	// Rot.R : Yaw
	// Rot.G : Pitch
	// Rot.B : Roll
	// Rot.A : Scale

	float Yaw = ((float)Rot.R)/255.f*360.f;
	float Pitch = ((float)Rot.G)/255.f*360.f;
	float Roll = ((float)Rot.B)/255.f*360.f;

	float Scale_out = ((float)Rot.A) / 255.f * 20.f;

	FRotator OutRot = FRotator(Pitch,Yaw,Roll);
	FVector OutLoc = FVector(X,Y,Z)-CompLoc;
	FVector OutScale = FVector(Scale_out,Scale_out,Scale_out);


	if(IsAdjustement)
	{
		FMatrix ReferenceTransform = FromT.Transform;
		FVector RefLocation = ReferenceTransform.GetOrigin();
		FVector RefScale = ReferenceTransform.GetScaleVector();
		ReferenceTransform.RemoveScaling();
		ReferenceTransform = ReferenceTransform.RemoveTranslation();
		FQuat RefRotation(ReferenceTransform);

		FTransform Value(RefRotation, RefLocation, RefScale);

		//FTransform Value = FromT;
		OutLoc.X = Value.GetTranslation().X;
		OutLoc.Y = Value.GetTranslation().Y;

		OutRot= Value.Rotator();
		OutScale= Value.GetScale3D();

		if (Value.GetScale3D().X > 0.f && Scale_out == 0.f && (Z <= AtitudeRange.Min || Z >= AtitudeRange.Max))
		{
			Value.SetScale3D(FVector(0.f, 0.f, 0.f));
			OutScale= Value.GetScale3D();
		}

	}

	OutTransform.Transform = FTransform(OutRot.Quaternion(), OutLoc, OutScale).ToMatrixWithScale();

}

bool AShaderWorldActor::UpdateSpawnableCollisions(FSpawnableMesh& Spawn, double& UpdateStartTime)
{
	

	const double GameThreadBudget_ms = SWGameThreadBudgetFinalize_ms.GetValueOnGameThread();

	int32 BudgetElementPerCollisionUpdate = CVarSWCollBatchUpdateCount.GetValueOnGameThread();

	if (Spawn.SpawnablesElemNeedCollisionUpdate.Num() > 0)
	{
		/*
		 * We assume the amount of variations isn't extreme to the point of it being costly to iterate
		 */
		bool bCanUpdateInstanceData = true;
		for (int i = 0; i < Spawn.HIM_Mesh_Collision_enabled.Num(); i++)
		{
			bCanUpdateInstanceData &= !Spawn.HIM_Mesh_Collision_enabled[i]->IsAsyncBuilding();
			if(!bCanUpdateInstanceData)
			{
#if SWDEBUG
				SW_LOG("!bCanUpdateInstanceData")
#endif
				break;
			}
			
		}

		if(!bCanUpdateInstanceData)
			return false;
#if 0
		while(Spawn.SpawnablesElemNeedCollisionUpdate.Num()>0)
		{
			int32 ElID = Spawn.SpawnablesElemNeedCollisionUpdate.Pop();

			if (ElID >= Spawn.SpawnablesElem.Num())
				continue;

			if ((FPlatformTime::Seconds() - UpdateStartTime) * 1000.0 > GameThreadBudget_ms)
			{
				return false;
			}

			FSpawnableMeshElement& Mesh = Spawn.SpawnablesElem[ElID];

			if (Spawn.SpawnType != ESpawnableType::Actor && Mesh.Collision_Mesh_ID >= 0)
			{
				FSpawnableMeshProximityCollisionElement& Collision_Mesh = Spawn.SpawnablesCollisionElem[Mesh.Collision_Mesh_ID];
#if 1
				if (Collision_Mesh.InstancesIndexes.Num() > 0)
				{

					/*
					 * Dans le cas spécifique d'une batch update, on peut considérer segmenter l'update physique des asset sous jacent sur plusieurs frames
					 *
					 */



					if (Collision_Mesh.OffsetOfSegmentedUpdate.Num() <= 0)
					{
						Collision_Mesh.OffsetOfSegmentedUpdate.AddDefaulted(Spawn.HIM_Mesh_Collision_enabled.Num());

						//Collision_Mesh.OffsetOfSegmentedUpdate.SetNum(Spawn.HIM_Mesh_Collision_enabled.Num());
						//for(int32&Val : Collision_Mesh.OffsetOfSegmentedUpdate)
						//	Val=0;
					}

					TArray<int32> Iters;
					Iters.SetNum(Spawn.HIM_Mesh_Collision_enabled.Num());
					TArray<int32> Reminders;
					Reminders.SetNum(Spawn.HIM_Mesh_Collision_enabled.Num());

					for (int i = 0; i < Spawn.HIM_Mesh_Collision_enabled.Num(); i++)
					{
						Iters[i] = Mesh.InstancesT->Transforms[i].Num() / BudgetElementPerCollisionUpdate;
						Reminders[i] = Mesh.InstancesT->Transforms[i].Num() - Iters[i] * BudgetElementPerCollisionUpdate;
					}


					while (true)
					{
						bool AllComplete = true;

						for (int i = 0; i < Spawn.HIM_Mesh_Collision_enabled.Num(); i++)
						{

							if (!Spawn.HIM_Mesh_Collision_enabled[i])
							{
								UE_LOG(LogTemp, Warning, TEXT("ERROR Hierachical instanced mesh nullptr: Garbage Collected in editor ?"));
								rebuild = true;
								return false;
							}

							if ((FPlatformTime::Seconds() - UpdateStartTime) * 1000.0 > GameThreadBudget_ms)
							{
								return false;
							}

							if ((Reminders[i] == 0) && (Collision_Mesh.OffsetOfSegmentedUpdate[i] == Iters[i]) || (Reminders[i] > 0 && (Collision_Mesh.OffsetOfSegmentedUpdate[i] > Iters[i])))
							{

							}
							else
							{
								Spawn.HIM_Mesh_Collision_enabled[i]->bAutoRebuildTreeOnInstanceChanges = false;

								int32 Iteration = Collision_Mesh.OffsetOfSegmentedUpdate[i];

								if (Iteration < Iters[i])
								{
									Spawn.HIM_Mesh_Collision_enabled[i]->SWBatchUpdateCountInstanceData(Collision_Mesh.InstanceOffset[i] + Iteration * BudgetElementPerCollisionUpdate, BudgetElementPerCollisionUpdate, Mesh.InstancesT->Transforms[i], false, false, true, Iteration * BudgetElementPerCollisionUpdate);
									Collision_Mesh.OffsetOfSegmentedUpdate[i]++;

									if ((Reminders[i] > 0) || ((Reminders[i] == 0) && (Collision_Mesh.OffsetOfSegmentedUpdate[i] < Iters[i])))
										AllComplete = false;
								}
								else if (Reminders[i] > 0)
								{
									Spawn.HIM_Mesh_Collision_enabled[i]->SWBatchUpdateCountInstanceData(Collision_Mesh.InstanceOffset[i] + Iters[i] * BudgetElementPerCollisionUpdate, Reminders[i], Mesh.InstancesT->Transforms[i], false, false, true, Iters[i] * BudgetElementPerCollisionUpdate);
									Collision_Mesh.OffsetOfSegmentedUpdate[i]++;
								}
							}
						}

						if (AllComplete)
						{
							break;
						}
					}


					Collision_Mesh.OffsetOfSegmentedUpdate.Empty();

					for (int i = 0; i < Spawn.HIM_Mesh_Collision_enabled.Num(); i++)
					{
						if (USWCollectableInstancedSMeshComponent* HSM_Col = Cast<USWCollectableInstancedSMeshComponent>(Spawn.HIM_Mesh_Collision_enabled[i]))
						{
							const int numIndexes = Mesh.InstancesIndexes[i].InstancesIndexes.Num();

#if INTEL_ISPC								

							ispc::ShaderWorld_SetRedirectionData(numIndexes
								, Collision_Mesh.InstanceOffset[i]
								, Mesh.InstancesIndexes[i].InstancesIndexes.GetData()
								, ElID
								, i
								, HSM_Col->Redirection->Redirection_Indexes.GetData()
								, HSM_Col->Redirection->ColIndexToSpawnableIndex.GetData()
								, HSM_Col->Redirection->ColIndexToVarietyIndex.GetData());

#else

							ParallelFor(numIndexes, [&](int32 k)
								{

									if ((k < numIndexes) && (HSM_Col->Redirection->Redirection_Indexes.Num() > 0))
									{
										HSM_Col->Redirection->Redirection_Indexes[Collision_Mesh.InstanceOffset[i] + k] = Mesh.InstancesIndexes[i].InstancesIndexes[k];

										HSM_Col->Redirection->ColIndexToSpawnableIndex[Collision_Mesh.InstanceOffset[i] + k] = ElID;
										HSM_Col->Redirection->ColIndexToVarietyIndex[Collision_Mesh.InstanceOffset[i] + k] = i;
									}

								});
#endif

						}

					}
				}
				else
				{
					Collision_Mesh.InstancesIndexes.SetNum(Spawn.NumInstancePerHIM->Indexes.Num());
					Collision_Mesh.InstanceOffset.SetNum(Spawn.NumInstancePerHIM->Indexes.Num());

					const uint32 InstanceNum = Spawn.NumInstancePerHIM->Indexes.Num();

					bool bValid = InstanceNum == Mesh.InstancesT->Transforms.Num();
					bValid &= InstanceNum == Mesh.InstancesIndexes.Num();
					bValid &= InstanceNum == Spawn.HIM_Mesh_Collision_enabled.Num();


					if (bValid)
					{
						for (int i = 0; i < Spawn.HIM_Mesh_Collision_enabled.Num(); i++)
						{
							if (Spawn.HIM_Mesh_Collision_enabled.Num() == Mesh.InstancesIndexes.Num())


								Collision_Mesh.InstanceOffset[i] = Spawn.HIM_Mesh_Collision_enabled[i]->GetNumRenderInstances();
							Spawn.HIM_Mesh_Collision_enabled[i]->bAutoRebuildTreeOnInstanceChanges = false;
							Collision_Mesh.InstancesIndexes[i].InstancesIndexes = Spawn.HIM_Mesh_Collision_enabled[i]->SWAddInstances(Mesh.InstancesT->Transforms[i], true);

							//Redirection
							if (USWCollectableInstancedSMeshComponent* HSM_Col = Cast<USWCollectableInstancedSMeshComponent>(Spawn.HIM_Mesh_Collision_enabled[i]))
							{
								HSM_Col->Redirection->Redirection_Indexes.Append(Mesh.InstancesIndexes[i].InstancesIndexes);

								TArray<int32> SpawnElemIndex;
								SpawnElemIndex.Init(ElID, Mesh.InstancesIndexes[i].InstancesIndexes.Num());

								TArray<int32> VarietyID;
								VarietyID.Init(i, Mesh.InstancesIndexes[i].InstancesIndexes.Num());

								HSM_Col->Redirection->ColIndexToSpawnableIndex.Append(SpawnElemIndex);
								HSM_Col->Redirection->ColIndexToVarietyIndex.Append(VarietyID);
							}


						}
					}
					else
					{
						//assign but not drawn yet
						UE_LOG(LogTemp, Warning, TEXT("Error Mesh.InstancesT.Num() %d !=Spawn.HIM_Mesh_Collision_enabled.Num() %d"), Mesh.InstancesT->Transforms.Num(), Spawn.HIM_Mesh_Collision_enabled.Num());
					}
				}
#endif
			}

			Spawn.SpawnablesElemNeedCollisionUpdate.Remove(ElID);
		}
#endif
#if 1
		for (int SpawnQuadIDIndex = Spawn.SpawnablesElemNeedCollisionUpdate.Num()-1; SpawnQuadIDIndex>=0; SpawnQuadIDIndex--)
		{
			int& ElID = Spawn.SpawnablesElemNeedCollisionUpdate[SpawnQuadIDIndex];

			if (ElID >= Spawn.SpawnablesElem.Num())
				continue;

			if ((FPlatformTime::Seconds() - UpdateStartTime) * 1000.0 > GameThreadBudget_ms)
			{
				return false;
			}

			FSpawnableMeshElement& Mesh = Spawn.SpawnablesElem[ElID];

			if (Spawn.SpawnType != ESpawnableType::Actor && Mesh.Collision_Mesh_ID >= 0)
			{
				FSpawnableMeshProximityCollisionElement& Collision_Mesh = Spawn.SpawnablesCollisionElem[Mesh.Collision_Mesh_ID];
#if 1
				if (Collision_Mesh.InstancesIndexes.IsValid() && Collision_Mesh.InstancesIndexes->Initiated && (Collision_Mesh.InstancesIndexes->InstancesIndexes.Num() > 0) /*Collision_Mesh.InstancesIndexes.Num() > 0*/)
				{

					/*
					 * Dans le cas spécifique d'une batch update, on peut considérer segmenter l'update physique des asset sous jacent sur plusieurs frames
					 *
					 */

					

					if(Collision_Mesh.OffsetOfSegmentedUpdate.Num()<=0)
					{
						Collision_Mesh.OffsetOfSegmentedUpdate.AddDefaulted(Spawn.HIM_Mesh_Collision_enabled.Num());

						//Collision_Mesh.OffsetOfSegmentedUpdate.SetNum(Spawn.HIM_Mesh_Collision_enabled.Num());
						//for(int32&Val : Collision_Mesh.OffsetOfSegmentedUpdate)
						//	Val=0;
					}

					TArray<int32> Iters;
					Iters.SetNum(Spawn.HIM_Mesh_Collision_enabled.Num());
					TArray<int32> Reminders;					
					Reminders.SetNum(Spawn.HIM_Mesh_Collision_enabled.Num());

					for (int i = 0; i < Spawn.HIM_Mesh_Collision_enabled.Num(); i++)
					{
						Iters[i] = Mesh.InstancesT->Transforms[i].Num() / BudgetElementPerCollisionUpdate;
						Reminders[i] = Mesh.InstancesT->Transforms[i].Num() - Iters[i] * BudgetElementPerCollisionUpdate;
					}
					

					while(true)
					{
						bool AllComplete = true;

						for (int i = 0; i < Spawn.HIM_Mesh_Collision_enabled.Num(); i++)
						{							

							if (!Spawn.HIM_Mesh_Collision_enabled[i])
							{
								UE_LOG(LogTemp, Warning, TEXT("ERROR Hierachical instanced mesh nullptr: Garbage Collected in editor ?"));
								rebuild = true;
								return false;
							}

							if ((FPlatformTime::Seconds() - UpdateStartTime) * 1000.0 > GameThreadBudget_ms)
							{
								return false;
							}

							if((Reminders[i] == 0) && (Collision_Mesh.OffsetOfSegmentedUpdate[i] == Iters[i]) || (Reminders[i]>0 && (Collision_Mesh.OffsetOfSegmentedUpdate[i] > Iters[i])))
							{
								
							}
							else
							{
								Spawn.HIM_Mesh_Collision_enabled[i]->bAutoRebuildTreeOnInstanceChanges = false;
								
								int32 Iteration = Collision_Mesh.OffsetOfSegmentedUpdate[i];
								
								if(Iteration < Iters[i])
								{								
									Spawn.HIM_Mesh_Collision_enabled[i]->SWBatchUpdateCountInstanceData(Collision_Mesh.InstanceOffset[i] + Iteration * BudgetElementPerCollisionUpdate, BudgetElementPerCollisionUpdate, Mesh.InstancesT->Transforms[i], false, false, true, Iteration * BudgetElementPerCollisionUpdate);
									Collision_Mesh.OffsetOfSegmentedUpdate[i]++;

									if((Reminders[i] > 0) || ((Reminders[i] == 0) && (Collision_Mesh.OffsetOfSegmentedUpdate[i] < Iters[i])))
										AllComplete=false;
								}
								else if(Reminders[i] > 0)
								{
									Spawn.HIM_Mesh_Collision_enabled[i]->SWBatchUpdateCountInstanceData(Collision_Mesh.InstanceOffset[i] + Iters[i] * BudgetElementPerCollisionUpdate, Reminders[i], Mesh.InstancesT->Transforms[i], false, false, true, Iters[i] * BudgetElementPerCollisionUpdate);
									Collision_Mesh.OffsetOfSegmentedUpdate[i]++;
								}
							}
						}

						if(AllComplete)
						{
							break;
						}							
					}
					

					Collision_Mesh.OffsetOfSegmentedUpdate.Empty();
					
					for (int i = 0; i < Spawn.HIM_Mesh_Collision_enabled.Num(); i++)
					{
						if (USWCollectableInstancedSMeshComponent* HSM_Col = Cast<USWCollectableInstancedSMeshComponent>(Spawn.HIM_Mesh_Collision_enabled[i]))
						{
							const int numIndexes = Mesh.InstancesIndexes->InstancesIndexes[i].InstancesIndexes.Num();

#if INTEL_ISPC								

							ispc::ShaderWorld_SetRedirectionData(numIndexes
								, Collision_Mesh.InstanceOffset[i]
								, Mesh.InstancesIndexes->InstancesIndexes[i].InstancesIndexes.GetData()
								, ElID
								, i
								, HSM_Col->Redirection->Redirection_Indexes.GetData()
								, HSM_Col->Redirection->ColIndexToSpawnableIndex.GetData()
								, HSM_Col->Redirection->ColIndexToVarietyIndex.GetData());

#else

							ParallelFor(numIndexes, [&](int32 k)
								{

									if ((k < numIndexes) && (HSM_Col->Redirection->Redirection_Indexes.Num() > 0))
									{
										HSM_Col->Redirection->Redirection_Indexes[Collision_Mesh.InstanceOffset[i] + k] = Mesh.InstancesIndexes->InstancesIndexes[i].InstancesIndexes[k];

										HSM_Col->Redirection->ColIndexToSpawnableIndex[Collision_Mesh.InstanceOffset[i] + k] = ElID;
										HSM_Col->Redirection->ColIndexToVarietyIndex[Collision_Mesh.InstanceOffset[i] + k] = i;
						}

					});
#endif

						}
						
					}
				}
				else
				{
					if (Collision_Mesh.InstancesIndexes.IsValid() && !Collision_Mesh.InstancesIndexes->Initiated)
					{
						SW_LOG("Collision_Mesh.InstancesIndexes.IsValid() && !Collision_Mesh.InstancesIndexes->Initiated")
						
						continue;
					}
					else
					{
						Collision_Mesh.InstancesIndexes = MakeShared<FSWInstanceIndexesInHISM, ESPMode::ThreadSafe>();
					Collision_Mesh.InstancesIndexes->InstancesIndexes.SetNum(Spawn.NumInstancePerHIM->Indexes.Num());
					//Collision_Mesh.InstancesIndexes.SetNum(Spawn.NumInstancePerHIM->Indexes.Num());
					Collision_Mesh.InstanceOffset.SetNum(Spawn.NumInstancePerHIM->Indexes.Num());

					const uint32 InstanceNum = Spawn.NumInstancePerHIM->Indexes.Num();

					bool bValid = InstanceNum == Mesh.InstancesT->Transforms.Num();
					bValid &= (Mesh.InstancesIndexes.IsValid()/* && Mesh.InstancesIndexes->Initiated*/) && (InstanceNum == Mesh.InstancesIndexes->InstancesIndexes.Num());
					bValid &= InstanceNum == Spawn.HIM_Mesh_Collision_enabled.Num();

					ensure(Mesh.InstancesIndexes.IsValid());

					if (bValid)
					{
						for (int i = 0; i < Spawn.HIM_Mesh_Collision_enabled.Num(); i++)
						{
							if (Spawn.HIM_Mesh_Collision_enabled.Num() == Mesh.InstancesIndexes->InstancesIndexes.Num())
								Collision_Mesh.InstanceOffset[i] = Spawn.HIM_Mesh_Collision_enabled[i]->GetNumRenderInstances();

							Spawn.HIM_Mesh_Collision_enabled[i]->bAutoRebuildTreeOnInstanceChanges = false;
							//Collision_Mesh.InstancesIndexes[i].InstancesIndexes = Spawn.HIM_Mesh_Collision_enabled[i]->SWAddInstances(Collision_Mesh.InstancesIndexes[i].InstancesIndexes,Mesh.InstancesT->Transforms[i], true);
							Spawn.HIM_Mesh_Collision_enabled[i]->SWAddInstances(Collision_Mesh.InstancesIndexes->InstancesIndexes[i].InstancesIndexes,Mesh.InstancesT->Transforms[i], true);
							//Spawn.HIM_Mesh_Collision_enabled[i]->SWNewAddInstances(i,Collision_Mesh.InstancesIndexes, Mesh.InstancesT);

							//Redirection
							if (USWCollectableInstancedSMeshComponent* HSM_Col = Cast<USWCollectableInstancedSMeshComponent>(Spawn.HIM_Mesh_Collision_enabled[i]))
							{
								HSM_Col->Redirection->Redirection_Indexes.Append(Mesh.InstancesIndexes->InstancesIndexes[i].InstancesIndexes);

								TArray<int32> SpawnElemIndex;
								SpawnElemIndex.Init(ElID, Mesh.InstancesIndexes->InstancesIndexes[i].InstancesIndexes.Num());

								TArray<int32> VarietyID;
								VarietyID.Init(i, Mesh.InstancesIndexes->InstancesIndexes[i].InstancesIndexes.Num());

								HSM_Col->Redirection->ColIndexToSpawnableIndex.Append(SpawnElemIndex);
								HSM_Col->Redirection->ColIndexToVarietyIndex.Append(VarietyID);

								if(HSM_Col->Redirection->Redirection_Indexes.Num()==0)
								{
									SW_LOG("HSM_Col->Redirection->Redirection_Indexes == 0 Mesh.InstancesIndexes->InstancesIndexes[i].InstancesIndexes %d", Mesh.InstancesIndexes->InstancesIndexes[i].InstancesIndexes.Num())
								}
							}


						}

						/*
						 * The index buffer was just filled on gamethread.. for now!
						 */
						Collision_Mesh.InstancesIndexes->Initiated = true;
					}
					else
					{
						//assign but not drawn yet
						UE_LOG(LogTemp, Warning, TEXT("Error Mesh.InstancesT.Num() %d !=Spawn.HIM_Mesh_Collision_enabled.Num() %d"), Mesh.InstancesT->Transforms.Num(), Spawn.HIM_Mesh_Collision_enabled.Num());						
					}
					}
					
				}
#endif
			}


			Spawn.SpawnablesElemNeedCollisionUpdate.RemoveAt(SpawnQuadIDIndex);

			
		}
#endif
	}

	Spawn.SpawnablesElemNeedCollisionUpdate.Empty();

	return true;
}

bool AShaderWorldActor::ProcessSpawnablePending()
{	
	SW_FCT_CYCLE()

	if(rebuild)
		return true;

	bool AllSpawnableReadBack = true; 

	for (FSWBiom& elB : Bioms)
	{
	for (FSpawnableMesh& Spawn : elB.Spawnables)
	{
		if (Spawn.SpawnType == ESpawnableType::Undefined)
		{
			continue;
		}

		// The idea is that if we use instanced meshes, their transform are computed in local space, while actors require world space			
		const FVector CompLocation = Spawn.SpawnType != ESpawnableType::Actor ? GetActorLocation() + FVector(GetWorld()->OriginLocation) : FVector(GetWorld()->OriginLocation);

		for(int32 SpawnIndex = Spawn.SpawnablesElemReadToProcess.Num()-1; SpawnIndex>=0; SpawnIndex--)
		{
			const int32& ElID = Spawn.SpawnablesElemReadToProcess[SpawnIndex];
			
			FSpawnableMeshElement& Mesh = Spawn.SpawnablesElem[ElID];

			if((*Mesh.ReadBackCompletion.Get()))
			{
				bool IsAnAdjustement = Mesh.NextUpdateIsAPositionAdjustement;
				Mesh.NextUpdateIsAPositionAdjustement = false;

				if (Spawn.SpawnType != ESpawnableType::Actor)
					Mesh.InstancesT->Transforms.SetNum(Spawn.HIM_Mesh.Num());
				else
					Mesh.InstancesT->Transforms.SetNum(Spawn.Spawned_Actors.Num());


				bool UsePrecomputedT = false;
				if (Mesh.InstancesT->Transforms.Num() == Spawn.HIM_Mesh.Num() && IsAnAdjustement && Mesh.InstancesT->Transforms.Num() > 0)
					UsePrecomputedT = true;

				FSpawnableMesh::FSpawnableProcessingWork& SpawnElemWork = Spawn.SpawnableWorkQueue.AddDefaulted_GetRef();
				SpawnElemWork.ElemID = ElID;
				SpawnElemWork.RTDim = Spawn.RT_Dim;
				SpawnElemWork.bUsePrecomputedTransform = UsePrecomputedT;
				SpawnElemWork.Read = Mesh.SpawnData;
				SpawnElemWork.InstancesT = Mesh.InstancesT;
				SpawnElemWork.CompLocation = CompLocation;
				SpawnElemWork.MeshLocCompute = Mesh.MeshLocation_latestC;
				SpawnElemWork.AltitudeRange = Spawn.AltitudeRange;
				SpawnElemWork.InstanceIndexToHIMIndex = Spawn.InstanceIndexToHIMIndex;
				SpawnElemWork.NumInstancePerHIM = Spawn.NumInstancePerHIM;
				SpawnElemWork.InstanceIndexToIndexForHIM = Spawn.InstanceIndexToIndexForHIM;

				Spawn.SpawnablesElemReadToProcess.RemoveAt(SpawnIndex);
			}
		}

		if(!Spawn.SpawnablesElemReadToProcess.IsEmpty())
		{
			AllSpawnableReadBack=false;
			continue;
		}
		

		Spawn.SpawnablesElemReadToProcess.Empty();	
			
		

		if(Spawn.SpawnableWorkQueue.Num()>0 && Spawn.ProcessedRead.IsValid() && Spawn.ProcessedRead->bProcessingCompleted)
		{
			if(Spawn.SpawnableWorkQueue.Num()==1 && Spawn.SpawnableWorkQueue[0].bCollisionProcessingOverflow)
				continue;

			Spawn.ProcessedRead->bProcessingCompleted = false;

			Async(EAsyncExecution::TaskGraph, [RAPI = RendererAPI, Work = Spawn.SpawnableWorkQueue, CompletionAtomic = Spawn.ProcessedRead]
			{

				FInstancedStaticMeshInstanceData DefaultT;
				for(auto& W : Work)
				{
					if(W.bCollisionProcessingOverflow)
						continue;

					for (int i = 0; i < W.InstancesT->Transforms.Num(); i++)
					{
						TArray<FInstancedStaticMeshInstanceData>& T = W.InstancesT->Transforms[i];
						T.SetNum(W.NumInstancePerHIM->Indexes[i]);
					}

					const int NumOfVertex = W.RTDim * W.RTDim;

					for(int32 k=0; k< NumOfVertex;k++)
					{						
						FInstancedStaticMeshInstanceData& Tr = W.bUsePrecomputedTransform ? W.InstancesT->Transforms[W.InstanceIndexToHIMIndex->Indexes[k]][W.InstanceIndexToIndexForHIM->Indexes[k]] : DefaultT;

						uint32 x = k % (W.RTDim);
						uint32 y = k / (W.RTDim);

						if (RAPI == EGeoRenderingAPI::OpenGL)
							y = W.RTDim - 1 - y;

						uint32 x2 = x * 2;
						uint32 y2 = y * 2;

						uint32 X_index = y2 * (W.RTDim * 2) + x2;
						uint32 Y_index = y2 * (W.RTDim * 2) + x2 + 1;
						uint32 Z_index = (y2 + 1) * (W.RTDim * 2) + x2;
						uint32 RotScale_index = (y2 + 1) * (W.RTDim * 2) + x2 + 1;

						
						if (RAPI == EGeoRenderingAPI::OpenGL)
						{
							X_index = (y2 + 1) * (W.RTDim * 2) + x2;
							Y_index = (y2 + 1) * (W.RTDim * 2) + x2 + 1;
							Z_index = (y2 ) * (W.RTDim * 2) + x2;
							RotScale_index = (y2 ) * (W.RTDim * 2) + x2 + 1;
						}						
				
						GetLocalTransformOfSpawnable((W.InstancesT->Transforms[W.InstanceIndexToHIMIndex->Indexes[k]])[W.InstanceIndexToIndexForHIM->Indexes[k]] , W.CompLocation, W.Read->ReadData[X_index], W.Read->ReadData[Y_index], W.Read->ReadData[Z_index], W.Read->ReadData[RotScale_index], Tr, W.bUsePrecomputedTransform, W.AltitudeRange, W.MeshLocCompute);
												
					}
					
				}
				if(CompletionAtomic.IsValid())
					CompletionAtomic->bProcessingCompleted = true;

			});
		}


	}
	}

	return AllSpawnableReadBack;
}

EClipMapInteriorConfig AShaderWorldActor::RelativeLocationToParentInnerMeshConfig(FVector RelativeLocation)
{

	if(RelativeLocation.X>0 && RelativeLocation.Y>0)
		return EClipMapInteriorConfig::BotLeft;
	else if (RelativeLocation.X > 0 && RelativeLocation.Y <= 0)
		return EClipMapInteriorConfig::BotRight ;
	else if (RelativeLocation.X <= 0 && RelativeLocation.Y > 0)
		return EClipMapInteriorConfig::TopLeft;
	else
		return EClipMapInteriorConfig::TopRight;

}

void AShaderWorldActor::UpdateParentInnerMesh(int ChildLevel,EClipMapInteriorConfig NewConfig, bool Segmented)
{
	
	int ParentIndice = ChildLevel-1;
	if(ParentIndice>=0)
	{		
		bool VisibilityFlag[4]={NewConfig==EClipMapInteriorConfig::BotLeft,NewConfig==EClipMapInteriorConfig::TopLeft,NewConfig==EClipMapInteriorConfig::BotRight,NewConfig==EClipMapInteriorConfig::TopRight};
		for(int i=0;i<4;i++)
		{
			Meshes[ParentIndice].SetSectionVisible(2+i,VisibilityFlag[i], Segmented);
		}

		Meshes[ParentIndice].Config = NewConfig;	
	}
}



FCollisionMeshElement& AShaderWorldActor::GetACollisionMesh()
{
	if (CollisionShareable->AvailableCollisionMesh.Num() > 0)
	{
		FCollisionMeshElement& Elem = CollisionMesh[CollisionShareable->AvailableCollisionMesh[CollisionShareable->AvailableCollisionMesh.Num() - 1]];
		UsedCollisionMesh.Add(Elem.ID);

		CollisionShareable->UsedCollisionMesh.Add(Elem.ID);
		CollisionShareable->AvailableCollisionMesh.RemoveAt(CollisionShareable->AvailableCollisionMesh.Num() - 1);
		return Elem;
	}


	FCollisionMeshElement NewElem;
	NewElem.ID=CollisionMesh.Num();

	UWorld* World = GetWorld();

	uint32 SizeT = (uint32)CollisionVerticesPerPatch;

	RendertargetMemoryBudgetMB+=(SizeT*SizeT*4)/1000000.0f;

	SW_RT(NewElem.CollisionRT, World, SizeT,TF_Nearest, RTF_RGBA8)

	if(NewElem.ID==0)
	{
		RendertargetMemoryBudgetMB += (SizeT * SizeT * 4) / 1000000.0f;

		SW_RT(NewElem.CollisionRT_Duplicate, World, SizeT, TF_Nearest, RTF_RGBA8)
	}

	NewElem.HeightData = MakeShared<FSWColorRead, ESPMode::ThreadSafe>();
	NewElem.HeightData->ReadData.SetNum(CollisionVerticesPerPatch * CollisionVerticesPerPatch);

	NewElem.Mesh = NewObject<UShaderWorldCollisionComponent>(this, NAME_None, RF_Transient);

	NewElem.Mesh->SetCastShadow(false);
	NewElem.Mesh->bUseAsyncCooking=true;
	NewElem.Mesh->SetCanEverAffectNavigation(true);

	NewElem.Mesh->SetSWWorldVersion(Shareable_ID);

	if(RootComp)
	{
		NewElem.Mesh->ComponentTags = RootComp->ComponentTags;
		NewElem.Mesh->BodyInstance.CopyBodyInstancePropertiesFrom(&RootComp->BodyInstance);
		
		NewElem.Mesh->ComponentTags = RootComp->ComponentTags;

	}
	else
	{
		UE_LOG(LogTemp,Warning,TEXT("invalid RootComp"))
	}

	NewElem.Mesh->SetCollisionObjectType(CollisionChannel);
	NewElem.Mesh->SetupAttachment(RootComponent);
	
	//NewElem.Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	
	NewElem.Mesh->SetRelativeLocation(FVector(0.f,0.f, 0.f));
	NewElem.Mesh->bUseComplexAsSimpleCollision = true;
	NewElem.Mesh->SetNetAddressable();

	NewElem.Mesh->SetUsingAbsoluteLocation(true);
	NewElem.Mesh->SetUsingAbsoluteRotation(true);

	NewElem.MeshLocation = NewElem.Mesh->GetComponentLocation();

#if WITH_EDITORONLY_DATA
	NewElem.Mesh->bConsiderForActorPlacementWhenHidden = true;
#endif

	
	NewElem.Mesh->bHiddenInGame = !CollisionVisible;

	NewElem.Mesh->RegisterComponent();
	
	

	TSharedPtr<FSWShareableVerticePositionBuffer, ESPMode::ThreadSafe> Vertices = MakeShared<FSWShareableVerticePositionBuffer, ESPMode::ThreadSafe>();
	TSharedPtr<FSWShareableIndexBuffer, ESPMode::ThreadSafe> Triangles = MakeShared<FSWShareableIndexBuffer, ESPMode::ThreadSafe>();

	float Spacing = CollisionResolution;

	if (NewElem.ID == 0)
	{
		TArray<FVector2f> UV;
		CreateGridMeshWelded(CollisionVerticesPerPatch, CollisionVerticesPerPatch, Triangles, Vertices, UV, Spacing);

		//if (Triangles->Triangles_CollisionOnly.Num() != (Triangles->Indices.Num() / 3))
		{
			const int32 NumTriangles = Triangles->Indices.Num() / 3;
			Triangles->Triangles_CollisionOnly.Empty();
			Triangles->Triangles_CollisionOnly.Reserve(NumTriangles);
			for (int32 TriIdx = 0; TriIdx < NumTriangles; TriIdx++)
			{
				FTriIndices& Triangle = Triangles->Triangles_CollisionOnly.AddDefaulted_GetRef();
				Triangle.v0 = Triangles->Indices[(TriIdx * 3) + 0] /* + VertexBase */;
				Triangle.v1 = Triangles->Indices[(TriIdx * 3) + 1] /* + VertexBase */;
				Triangle.v2 = Triangles->Indices[(TriIdx * 3) + 2] /* + VertexBase */;
			}
		}
	}
	else
	{
		Vertices->Positions = CollisionMesh[0].Mesh->VerticesTemplate->Positions;
		Vertices->Positions3f = CollisionMesh[0].Mesh->VerticesTemplate->Positions3f;
		Vertices->MaterialIndices = CollisionMesh[0].Mesh->VerticesTemplate->MaterialIndices;

		Triangles = CollisionMesh[0].Mesh->TrianglesTemplate;
		//Triangles->Indices = CollisionMesh[0].Mesh->TrianglesTemplate->Indices;
		//Triangles->Triangles_CollisionOnly = CollisionMesh[0].Mesh->TrianglesTemplate->Triangles_CollisionOnly;
	}

	NewElem.Mesh->CreateMeshSection(0,Vertices,Triangles,false);

	/*
	 * If not in editor mode, only make collision blue mesh visible if we asked for collisions to be visible
	 * The renderthread will otherwise completely skip the collision meshes
	 */
	if(World/* && World->IsGameWorld()*/)
		NewElem.Mesh->SetMeshSectionVisible(0, CollisionVisible);
	
	
	if(NewElem.ID==0)
	{
		NewElem.Mesh->VerticesTemplate = MakeShared<FSWShareableVerticePositionBuffer, ESPMode::ThreadSafe>();
		NewElem.Mesh->VerticesTemplate->Positions = Vertices->Positions;
		NewElem.Mesh->VerticesTemplate->Positions3f = Vertices->Positions3f;
		NewElem.Mesh->VerticesTemplate->MaterialIndices = Vertices->MaterialIndices;

		NewElem.Mesh->TrianglesTemplate = Triangles;
		/*
		NewElem.Mesh->TrianglesTemplate = MakeShared<FSWShareableIndexBuffer, ESPMode::ThreadSafe>();
		NewElem.Mesh->TrianglesTemplate->Indices = Triangles->Indices;
		NewElem.Mesh->TrianglesTemplate->Triangles_CollisionOnly = Triangles->Triangles_CollisionOnly;
		*/
	}

	if(!CollisionMat)
	{
#if SWDEBUG
		SW_LOG("Material for Collision mesh not available for Shader World %s",*GetName())
#endif
	}
	else
	{		
		UMaterialInstanceDynamic* DynColMat = UMaterialInstanceDynamic::Create(CollisionMat.Get(), this);
		DynColMat->SetScalarParameterValue("MakeCollisionVisible", CollisionVisible?1.f:0.f);
		NewElem.Mesh->SetMaterialFromOwner(0, DynColMat);		
	}

	
	CollisionMesh.Add(NewElem);

	if(CollisionShareable->CollisionMeshData.Num() < CollisionMesh.Num())
	{
		UsedCollisionMesh.Add(NewElem.ID);

		CollisionShareable->UsedCollisionMesh.Add(NewElem.ID);

		CollisionShareable->CollisionMeshData.Add(NewElem);
	}
	

	return CollisionMesh[CollisionMesh.Num()-1];
}

void AShaderWorldActor::ReleaseCollisionMesh(int ID)
{
	if(CollisionShareable.IsValid())
		CollisionShareable->ReleaseCollisionMesh(ID);

}

void AShaderWorldActor::ProcessSeeds()
{
	if(SeedGenerator)
	{
		SeedGenerator->GenerateSeed(CurrentSeedsArray);
	}
}

void AShaderWorldActor::InitiateWorld()
{
	if(Meshes.Num()>0)
	{
		return;
	}

	WorldCycle = 0;

	UWorld* World = GetWorld();

	USWorldSubsystem* ShaderWorldSubsystem = SWorldSubsystem? SWorldSubsystem:GetWorld()->GetSubsystem<USWorldSubsystem>();

	WorldLocationLastBuild = GetActorLocation();

	GenerateCollision_last = GenerateCollision;
	VerticalRangeMeters_last = VerticalRangeMeters;

	if (Shareable_ID.IsValid())
	{
		ENQUEUE_RENDER_COMMAND(ClearStaticBuffers)(
			[ID = Shareable_ID](FRHICommandListImmediate& RHICmdList)
			{
				check(IsInRenderingThread());
				GSWClipMapBufferHolder.DiscardBuffers(ID);
				GSWClipMapBufferCollisionHolder.DiscardBuffers(ID);
			});


		Shareable_ID.Reset();
	}

	Shareable_ID = MakeShared<FSWShareableID, ESPMode::ThreadSafe>();

	if(!Material)
	{		
#if SWDEBUG
		SW_LOG("No Material for ShaderWorld %s",*GetName())
#endif
	}
	else
	{
		if (UMaterial* BaseM = Material.Get()->GetMaterial())
		{
			if (!BaseM->GetUsageByFlag(MATUSAGE_VirtualHeightfieldMesh))//BaseM->bUsedWithVirtualHeightfieldMesh)
			{
				UE_LOG(LogTemp,Warning,TEXT("ShaderWorld : This Material is not Compatible with ShaderWorld Terrain/Ocean, you need to check 'Used With Virtual Heightfield Mesh' within the material Details/Usage tab"));
				Material = nullptr;
			}
		}
	}

	bHadGeneratorAtRebuildTime = Generator != nullptr;

	RendertargetMemoryBudgetMB=0;

	for(uint8 LODCount=0; LODCount<16; LODCount++)
	{		
		if(((float)(1 << LODCount) * TerrainResolution * (N - 1)) < (2.0 * DrawDistance * 100.f))
		{
			LOD_Num = LODCount+1;
			WorldDimensionMeters = 2.0 * DrawDistance;
		}
		else
		{
			break;
		}
	}

	GridSpacing = TerrainResolution * pow(2.0, LOD_Num - 1);

	UpdateHOverTime = UpdateHeightDataOverTime;

	UpdateDelayForLODRuntime.Empty();

	if(UpdateHOverTime)
	{
		for (int i = 0; i < LOD_Num; i++)
		{
			UpdateDelayForLODRuntime.Add(pow(UpdateRateDegradationFactor, ((LOD_Num - 1) - i)) * 1.0 / UpdateRateHeight);
		}
	}


	LODs_DimensionsMeters.Empty();
	LODs_DimensionsMeters.SetNum(LOD_Num);

	ClipMapToUpdateAndMove.Empty();
	ClipMapToUpdateAndMove.SetNum(LOD_Num);

	ClipMapToUpdate.Empty();
	ClipMapToUpdate.SetNum(LOD_Num);	

	NeedSegmentedUpdate.Empty();
	NeedSegmentedUpdate.SetNum(LOD_Num);	

	ProcessSeeds();

	Segmented_Initialized=false;

	{
		if(ReadRequestLocation)
		{
			ReadRequestLocation = nullptr;
		}
		if (ReadRequestLocationHeightmap)
		{
			ReadRequestLocationHeightmap = nullptr;
		}
		if (GeneratorDynamicForReadBack)
		{
			GeneratorDynamicForReadBack = nullptr;
		}
		
		RendertargetMemoryBudgetMB += 4 * 2 * (5 * 5) / 1000000.0f;
		SW_RT(ReadRequestLocation, World, 5, TF_Nearest, RTF_RG32f)

		RendertargetMemoryBudgetMB += 4 * (5 * 5) / 1000000.0f;
		SW_RT(ReadRequestLocationHeightmap, World, 5, TF_Nearest, RTF_RGBA8)

		if(!Generator)
		{
#if SWDEBUG
			SW_LOG("Generator Material not available for Shader World %s", *GetName())
#endif
		}
		else
		{
			
			GeneratorDynamicForReadBack = UMaterialInstanceDynamic::Create(Generator.Get(), this);

			GeneratorDynamicForReadBack->SetScalarParameterValue("HeightReadBack", 1.f);
			GeneratorDynamicForReadBack->SetTextureParameterValue("SpecificLocationsRT", ReadRequestLocation);

			GeneratorDynamicForReadBack->SetScalarParameterValue("NoMargin", 0.f);
			GeneratorDynamicForReadBack->SetScalarParameterValue("N", N);
			GeneratorDynamicForReadBack->SetScalarParameterValue("LOD0GridSpacing", pow(2.0, -(LOD_Num - 1))* GridSpacing);

			GeneratorDynamicForReadBack->SetScalarParameterValue("NormalMapSelect", 0.f);
			GeneratorDynamicForReadBack->SetScalarParameterValue("HeightMapToggle", 1.f);

			TSet<FName> UsedNames;
			for (const FInstancedStruct& Seed : CurrentSeedsArray.SeedsArray)
			{
				if (Seed.IsValid())
				{
					const UScriptStruct* Type = Seed.GetScriptStruct();
					CA_ASSUME(Type);
					if (Type->IsChildOf(FTextureSeed::StaticStruct()))
					{
						FTextureSeed& TypedSeed = Seed.GetMutable<FTextureSeed>();
						if (!UsedNames.Contains(TypedSeed.SeedName))
						{
							UsedNames.Add(TypedSeed.SeedName);
							GeneratorDynamicForReadBack->SetTextureParameterValue(TypedSeed.SeedName, TypedSeed.Value);
						}
					}
					else if (Type->IsChildOf(FLinearColorSeed::StaticStruct()))
					{
						FLinearColorSeed& TypedSeed = Seed.GetMutable<FLinearColorSeed>();
						if (!UsedNames.Contains(TypedSeed.SeedName))
						{
							UsedNames.Add(TypedSeed.SeedName);
							GeneratorDynamicForReadBack->SetVectorParameterValue(TypedSeed.SeedName, TypedSeed.Value);
						}
					}
					else if (Type->IsChildOf(FScalarSeed::StaticStruct()))
					{
						FScalarSeed& TypedSeed = Seed.GetMutable<FScalarSeed>();
						if (!UsedNames.Contains(TypedSeed.SeedName))
						{
							UsedNames.Add(TypedSeed.SeedName);
							GeneratorDynamicForReadBack->SetScalarParameterValue(TypedSeed.SeedName, TypedSeed.Value);
						}
					}
					else
					{
#if SWDEBUG
						SW_LOG("Invalid Seed type found: '%s'", *GetPathNameSafe(Type));
#endif
						
					}
				}
			}
			
		}

		//CollisionSampleLocation = UKismetRenderingLibrary::CreateRenderTarget2D(GetWorld(), CollisionVerticesPerPatch, CollisionVerticesPerPatch, RTF_RGBA16f, FLinearColor(0, 0, 0, 1), false);
		//FVector CollisionPatchExtent = CollisionResolution * (CollisionVerticesPerPatch - 1) / 2.f * FVector(1.f, 1.f, 0.f);
	}
	
	bExportPhysicalMaterialID_cached = bExportPhysicalMaterialID;

	for(int i=0; i<LOD_Num;i++)
	{

		int CacheRes = ClipMapCacheIntraVerticesTexel * (N - 1) + 1;

		if (i == (LOD_Num - 1) && (ClipMapCacheIntraVerticesTexel > 1))
		{
			CacheRes = 1 * (N - 1) + 1;
		}
		else if (i == (LOD_Num - 2) && (ClipMapCacheIntraVerticesTexel > 1))
		{
			CacheRes = 2 * (N - 1) + 1;
		}
		else if (i == (LOD_Num - 3) && (ClipMapCacheIntraVerticesTexel > 1))
		{
			CacheRes = 2 * (N - 1) + 1;
		}
		else if (i == (LOD_Num - 4) && (ClipMapCacheIntraVerticesTexel > 1))
		{
			CacheRes = 2 * (N - 1) + 1;
		}

		int LOD = LOD_Num-1-i;
		FClipMapMeshElement NewElem;	
		NewElem.LandLayers_names.Empty();
		NewElem.LandLayers.Empty();
		NewElem.LandLayers_Segmented.Empty();
		NewElem.LandLayers_NeedParent.Empty();
		NewElem.LayerMatDyn.Empty();
		NewElem.SectionVisibility.Empty();
		NewElem.SectionVisibility_SegmentedCache.Empty();
		NewElem.Level=i;
		NewElem.GridSpacing=pow(2.0, LOD_Num - 1 - i) * TerrainResolution;

		if(UpdateHOverTime)
		{			
			NewElem.UpdateDelay = pow(1.35, ((LOD_Num - 1) - i)) * 1.0 / UpdateRateHeight;
		}
		else
			NewElem.UpdateDelay = 0.0;

		int LocalM = (N+1)/4;

		LODs_DimensionsMeters[LOD_Num-1-i] = (int32)((N-1)*NewElem.GridSpacing/100.f);


		if (LOD == 0)
		{
			FSWTexture2DCacheGroup& CacheGroup = NewElem.TransientCaches.PerLODCacheGroup.AddDefaulted_GetRef();

			/*
			 * Shared aspect within a group: Size of a tile in worldspace + How far we see those tiles
			 */
			{
				CacheGroup.CacheSizeMeters = 32; //32 meters square cache

				//RingCount - We need to have enough rings to cover the LOD0 heightmap width
				CacheGroup.RingCount = 1;

				int32 Optimal = FMath::DivideAndRoundUp(LODs_DimensionsMeters[LOD_Num - 1 - i] * 1.005 / 2.0, ((double)CacheGroup.CacheSizeMeters));

				if (Optimal > 1)
				{
					CacheGroup.RingCount = Optimal;
					CacheGroup.CacheManager = { Optimal };
					CacheGroup.ManagerInitiated = true;
				}
			}

			/*
			 * Let's add a virtual cache for the heightmap at LOD 0 (highest quality, around the player)
			 */
			{
				double ResolutionOfCache = TerrainResolution/100.0; //50cm precision

				int32 VirtualCacheTileResolution = CacheGroup.CacheSizeMeters / ResolutionOfCache;

				CacheGroup.PerLODCaches.Add("Heightmap", { VirtualCacheTileResolution , TextureFilter::TF_Nearest, ETextureRenderTargetFormat::RTF_RGBA8, {} });
			}
			
		}




		// Heightmap + Normal memory cost
		RendertargetMemoryBudgetMB+=4*((CacheRes*CacheRes)+(CacheRes+2)*(CacheRes+2))/1000000.0f;
		//1 texel border so we can compute the normal from the heightmap and not re-evaluate the generator/layers

		SW_RT(NewElem.HeightMap, World, CacheRes + 2, TF_Nearest, RTF_RGBA8)

		// We'll actually use it to prevent gaps between LOD of water
		//if (UseSegmented())
		{
			//For segmented computation
			RendertargetMemoryBudgetMB += 4 * (CacheRes + 2) * (CacheRes + 2) / 1000000.0f;

			SW_RT(NewElem.HeightMap_Segmented, World, CacheRes + 2, TF_Nearest, RTF_RGBA8)
		}

		SW_RT(NewElem.NormalMap, World, CacheRes, TF_Default, RTF_RGBA8)

		if(UseSegmented())
		{
			SW_RT(NewElem.NormalMap_Segmented, World, CacheRes, TF_Default, RTF_RGBA8)
		}			

		for(FClipMapLayer& layer : LandDataLayers)
		{
			if(layer.LayerName!="" && layer.MaterialToGenerateLayer)
			{
				RendertargetMemoryBudgetMB+=(CacheRes*CacheRes*4)/1000000.0f;

				UTextureRenderTarget2D* LayerRT = nullptr;
				SW_RT(LayerRT, World, CacheRes, layer.LayerFiltering, RTF_RGBA8)

				NewElem.LandLayers.Add(LayerRT);
				NewElem.LandLayers_names.Add(FName(*layer.LayerName));
				NewElem.LandLayers_NeedParent.Add(layer.GetParentCache);

				if (UseSegmented())
				{
					RendertargetMemoryBudgetMB += (CacheRes * CacheRes * 4) / 1000000.0f;

					UTextureRenderTarget2D* LayerRT_segmented = nullptr;
					SW_RT(LayerRT_segmented, World, CacheRes, layer.LayerFiltering, RTF_RGBA8)

					NewElem.LandLayers_Segmented.Add(LayerRT_segmented);
				}
			}
		}


		NewElem.Location = FIntVector(0,0,RootComponent->GetComponentLocation().Z) + FIntVector(-NewElem.GridSpacing, -NewElem.GridSpacing, 0);

		{
			NewElem.Mesh = NewObject<UGeoClipmapMeshComponent>(this, NAME_None, RF_Transient);

			NewElem.Mesh->SetupAttachment(RootComponent);
			NewElem.Mesh->RegisterComponent();

			NewElem.Mesh->SetSWWorldVersion(Shareable_ID);

			NewElem.Mesh->SetUseDynamicTopology(!UpdateHOverTime);
			
			NewElem.Mesh->SetUseWPO(LOD < EnableWorldPositionOffsetUnderLOD);

			HeightScale = FMath::RoundToInt(FMath::Clamp(HeightScale, 1, 10000));
			TransitionWidth = FMath::Clamp(TransitionWidth, 0.001, 0.5f);
			NewElem.Mesh->SetPatchData(NewElem.HeightMap, NewElem.NormalMap, FVector(NewElem.Location), (N - 1)* NewElem.GridSpacing, HeightScale, TransitionWidth, (N - 1)* NewElem.GridSpacing* (CacheRes <= 1 ? 1 : CacheRes / (CacheRes - 1)), N, NewElem.GridSpacing, CacheRes, SmoothLODTransition);


			NewElem.Mesh->BoundsScale=10.f;

			NewElem.Mesh->SetUsingAbsoluteLocation(true);
			NewElem.Mesh->SetUsingAbsoluteRotation(true);

			NewElem.Mesh->bNeverDistanceCull = true;

			NewElem.Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			
			if (!UpdateHOverTime && (((N-1)*NewElem.GridSpacing)/2.f<ShadowCastingRange))
			{
				NewElem.Mesh->CastShadow = true;
				NewElem.Mesh->bCastFarShadow = true;
			}
			else
			{
				NewElem.Mesh->CastShadow = false;
				NewElem.Mesh->bCastFarShadow = false;
			}

			NewElem.Mesh->Mobility = EComponentMobility::Movable;
			NewElem.Mesh->SetWorldLocation(FVector(-NewElem.GridSpacing, -NewElem.GridSpacing, RootComponent->GetComponentLocation().Z));
			NewElem.Mesh->Mobility = EComponentMobility::Static;

			if (RuntimeVirtualTextures.Num() > 0)
			{
				NewElem.Mesh->RuntimeVirtualTextures = RuntimeVirtualTextures;
			}
			if (NewElem.Mesh->VirtualTextureLodBias != VirtualTextureLodBias)
			{
				NewElem.Mesh->VirtualTextureLodBias = VirtualTextureLodBias;
			}

			if (NewElem.Mesh->VirtualTextureCullMips != VirtualTextureCullMips)
			{
				NewElem.Mesh->VirtualTextureCullMips = VirtualTextureCullMips;
			}

			if (NewElem.Mesh->VirtualTextureRenderPassType != VirtualTextureRenderPassType)
			{
				NewElem.Mesh->VirtualTextureRenderPassType = VirtualTextureRenderPassType;
			}
		}

		TArray<FVector3f> Vertices;
		TSharedPtr<FSWShareableIndexBuffer, ESPMode::ThreadSafe> Triangles = MakeShared<FSWShareableIndexBuffer>();
		TSharedPtr<FSWShareableIndexBuffer, ESPMode::ThreadSafe> TrianglesAlt = MakeShared<FSWShareableIndexBuffer>();
		TArray<FVector2f> UV;
		TArray<FVector2f> UV1;
		TArray<FVector2f> UV2;
		TArray<FVector2f> UV_dummy;
		TArray<FColor> Color_dummy;


		float LocalExtent = ((N - 1) * NewElem.GridSpacing) / 2.f;
		FVector LocalOffset = FVector(-LocalExtent, -LocalExtent, 0.f);	

		// StichingProfile
		// 1<<3 X=0
		// 1<<2 X=N-1
		// 1<<1 Y=0
		// 1 Y=N-1
		uint8 StichingProfile = 1<<3|1<<2|1<<1|1;

		
		CreateGridMeshWelded(0,N,N,Triangles, TrianglesAlt,Vertices,UV,UV1,UV2,NewElem.GridSpacing,LocalOffset,StichingProfile);

			
		TArray<FVector> Normals;
		Normals.Init(FVector(0.f, 0.f, 1.f), Vertices.Num());
		TArray<FGeoCProcMeshTangent> Tangents;
		Tangents.Init(FGeoCProcMeshTangent(FVector(0.f, 0.f, 1.f), false), Vertices.Num());

		if(NewElem.Mesh)
		NewElem.Mesh->CreateMeshSection(0,Vertices,Triangles, TrianglesAlt, Normals,UV,UV1,UV2,UV_dummy,Color_dummy,Tangents,false);
		
		if(i<LOD_Num-1)
		{
		Vertices.Empty();
		//Triangles.Empty();
		Triangles = MakeShared<FSWShareableIndexBuffer>();
		TrianglesAlt = MakeShared<FSWShareableIndexBuffer>();
		UV.Empty();
		UV1.Empty();
		UV2.Empty();
		
		StichingProfile = 1<<3|1<<2|1<<1;

		CreateGridMeshWelded(0,N,3,Triangles, TrianglesAlt, Vertices,UV,UV1,UV2,NewElem.GridSpacing,LocalOffset,StichingProfile);

		
		StichingProfile = 0;
		LocalOffset = FVector(-LocalExtent,-LocalExtent,0.f) + 2.f*NewElem.GridSpacing*FVector(0.f,1.f,0.f)  + (LocalM-1)*NewElem.GridSpacing*FVector(1.f,0.f,0.f);

		CreateGridMeshWelded(1,(LocalM-1)*2+3,(LocalM-1)-2+1, Triangles, TrianglesAlt, Vertices, UV, UV1,UV2, NewElem.GridSpacing, LocalOffset, StichingProfile);
		LocalOffset = FVector(-LocalExtent,-LocalExtent,0.f) + ((LocalM-1)*3 + 2)*NewElem.GridSpacing*FVector(0.f,1.f,0.f)  + (LocalM-1)*NewElem.GridSpacing*FVector(1.f,0.f,0.f);

		CreateGridMeshWelded(0,(LocalM-1)*2+3, (LocalM-1)-2+1, Triangles, TrianglesAlt, Vertices, UV, UV1,UV2, NewElem.GridSpacing, LocalOffset, StichingProfile);
		
		
		StichingProfile = 1<<3|1<<2|1;
		LocalOffset = FVector(-LocalExtent, -LocalExtent, 0.f)+ ((N-1) - 2) *NewElem.GridSpacing*FVector(0.f,1.f,0.f);

		CreateGridMeshWelded(0,N,3,Triangles, TrianglesAlt, Vertices,UV,UV1,UV2,NewElem.GridSpacing,LocalOffset,StichingProfile);
		
		StichingProfile = 1<<3;
		LocalOffset = FVector(-LocalExtent,-LocalExtent,0.f) + (2) * NewElem.GridSpacing * FVector(0.f, 1.f, 0.f);

		CreateGridMeshWelded(0,LocalM, N-4 /*LocalM*2+1+2*/, Triangles, TrianglesAlt, Vertices, UV,UV1,UV2, NewElem.GridSpacing, LocalOffset,StichingProfile);

		StichingProfile = 1<<2;
		LocalOffset = FVector(-LocalExtent, -LocalExtent, 0.f) + (2) * NewElem.GridSpacing * FVector(0.f, 1.f, 0.f) + ((LocalM - 1)*3+2) * NewElem.GridSpacing * FVector(1.f, 0.f, 0.f) ;

		CreateGridMeshWelded(1,LocalM, N-4/*LocalM*2+1 +2*/, Triangles, TrianglesAlt, Vertices, UV,UV1,UV2, NewElem.GridSpacing, LocalOffset,StichingProfile);

		Normals.Empty();
		Normals.Init(FVector(0.f, 0.f, 1.f), Vertices.Num());
		Tangents.Empty();
		Tangents.Init(FGeoCProcMeshTangent(FVector(0.f, 0.f, 1.f), false), Vertices.Num());

		if(NewElem.Mesh)
		NewElem.Mesh->CreateMeshSection(1, Vertices, Triangles, TrianglesAlt, Normals, UV, UV1, UV2, UV_dummy, Color_dummy, Tangents, false);


		Vertices.Empty();
		//Triangles.Empty();
		Triangles = MakeShared<FSWShareableIndexBuffer>();
		TrianglesAlt = MakeShared<FSWShareableIndexBuffer>();
		UV.Empty();
		UV1.Empty();
		UV2.Empty();
		//inner L Shape have no stiching
		StichingProfile = 0;

		LocalOffset = FVector(-LocalExtent, -LocalExtent, 0.f) + ((LocalM - 1)) * NewElem.GridSpacing * FVector(1.f, 1.f, 0.f);

		CreateGridMeshWelded(0,LocalM*2+1, 2, Triangles, TrianglesAlt, Vertices, UV,UV1,UV2, NewElem.GridSpacing, LocalOffset,StichingProfile);

		LocalOffset = FVector(-LocalExtent, -LocalExtent, 0.f) + ((LocalM - 1)) * NewElem.GridSpacing * FVector(1.f, 1.f, 0.f) + (1.0) * NewElem.GridSpacing * FVector(0.f, 1.f, 0.f);

		CreateGridMeshWelded(1,2, LocalM * 2, Triangles, TrianglesAlt, Vertices, UV,UV1,UV2, NewElem.GridSpacing, LocalOffset,StichingProfile);

		Normals.Empty();
		Normals.Init(FVector(0.f, 0.f, 1.f), Vertices.Num());
		Tangents.Empty();
		Tangents.Init(FGeoCProcMeshTangent(FVector(0.f, 0.f, 1.f), false), Vertices.Num());
		
		// botleft
		if(NewElem.Mesh)
		NewElem.Mesh->CreateMeshSection(2, Vertices, Triangles, TrianglesAlt, Normals, UV, UV1, UV2, UV_dummy, Color_dummy, Tangents, false);


		Vertices.Empty();
		//Triangles.Empty();
		Triangles = MakeShared<FSWShareableIndexBuffer>();
		TrianglesAlt = MakeShared<FSWShareableIndexBuffer>();
		UV.Empty();
		UV1.Empty();
		UV2.Empty();

		LocalOffset = FVector(-LocalExtent, -LocalExtent, 0.f) + ((LocalM - 1)) * NewElem.GridSpacing * FVector(1.f, 1.f, 0.f);

		CreateGridMeshWelded(0,LocalM * 2 + 1, 2, Triangles, TrianglesAlt, Vertices, UV,UV1,UV2, NewElem.GridSpacing, LocalOffset,StichingProfile);

		LocalOffset = FVector(-LocalExtent, -LocalExtent, 0.f) + ((LocalM - 1)) * NewElem.GridSpacing * FVector(1.f, 1.f, 0.f) + (1.0) * NewElem.GridSpacing * FVector(0.f, 1.f, 0.f)+ (LocalM *2 -1) * NewElem.GridSpacing * FVector(1.f, 0.f, 0.f);

		CreateGridMeshWelded(0,2, LocalM * 2, Triangles, TrianglesAlt, Vertices, UV,UV1,UV2, NewElem.GridSpacing, LocalOffset,StichingProfile);

		Normals.Empty();
		Normals.Init(FVector(0.f, 0.f, 1.f), Vertices.Num());
		Tangents.Empty();
		Tangents.Init(FGeoCProcMeshTangent(FVector(0.f, 0.f, 1.f), false), Vertices.Num());
		
		// topleft
		if(NewElem.Mesh)
		NewElem.Mesh->CreateMeshSection(3, Vertices, Triangles, TrianglesAlt, Normals, UV, UV1, UV2, UV_dummy, Color_dummy, Tangents, false);


		Vertices.Empty();
		//Triangles.Empty();
		Triangles = MakeShared<FSWShareableIndexBuffer>();
		TrianglesAlt = MakeShared<FSWShareableIndexBuffer>();
		UV.Empty();
		UV1.Empty();
		UV2.Empty();

		LocalOffset = FVector(-LocalExtent, -LocalExtent, 0.f) + ((LocalM - 1)) * NewElem.GridSpacing * FVector(1.f, 1.f, 0.f);

		CreateGridMeshWelded(0,2, LocalM * 2 + 1, Triangles, TrianglesAlt, Vertices, UV,UV1,UV2, NewElem.GridSpacing, LocalOffset,StichingProfile);

		LocalOffset = FVector(-LocalExtent, -LocalExtent, 0.f) + ((LocalM - 1)) * NewElem.GridSpacing * FVector(1.f, 1.f, 0.f) + (LocalM*2.0-1.0) * NewElem.GridSpacing * FVector(0.f, 1.f, 0.f)+ (1.0) * NewElem.GridSpacing * FVector(1.f, 0.f, 0.f);

		CreateGridMeshWelded(0,LocalM * 2, 2, Triangles, TrianglesAlt, Vertices, UV,UV1,UV2, NewElem.GridSpacing, LocalOffset,StichingProfile);

		Normals.Empty();
		Normals.Init(FVector(0.f, 0.f, 1.f), Vertices.Num());
		Tangents.Empty();
		Tangents.Init(FGeoCProcMeshTangent(FVector(0.f, 0.f, 1.f), false), Vertices.Num());
		
		// botright
		if(NewElem.Mesh)
		NewElem.Mesh->CreateMeshSection(4, Vertices, Triangles, TrianglesAlt, Normals, UV, UV1, UV2, UV_dummy, Color_dummy, Tangents, false);


		Vertices.Empty();
		//Triangles.Empty();
		Triangles = MakeShared<FSWShareableIndexBuffer>();
		TrianglesAlt = MakeShared<FSWShareableIndexBuffer>();
		UV.Empty();
		UV1.Empty();
		UV2.Empty();

		LocalOffset = FVector(-LocalExtent, -LocalExtent, 0.f) + ((LocalM - 1)) * NewElem.GridSpacing * FVector(1.f, 1.f, 0.f)+ (LocalM * 2.0 - 1.0) * NewElem.GridSpacing * FVector(1.f, 0.f, 0.f);

		CreateGridMeshWelded(1,2, LocalM * 2 + 1, Triangles, TrianglesAlt, Vertices, UV,UV1,UV2, NewElem.GridSpacing, LocalOffset,StichingProfile);

		LocalOffset = FVector(-LocalExtent, -LocalExtent, 0.f) + ((LocalM - 1)) * NewElem.GridSpacing * FVector(1.f, 1.f, 0.f) + (LocalM * 2.0 - 1.0) * NewElem.GridSpacing * FVector(0.f, 1.f, 0.f);

		CreateGridMeshWelded(1,LocalM * 2, 2, Triangles, TrianglesAlt, Vertices, UV,UV1,UV2, NewElem.GridSpacing, LocalOffset,StichingProfile);

		Normals.Empty();
		Normals.Init(FVector(0.f, 0.f, 1.f), Vertices.Num());
		Tangents.Empty();
		Tangents.Init(FGeoCProcMeshTangent(FVector(0.f, 0.f, 1.f), false), Vertices.Num());
		
		// topright
		if(NewElem.Mesh)
		NewElem.Mesh->CreateMeshSection(5, Vertices, Triangles, TrianglesAlt, Normals, UV, UV1, UV2, UV_dummy, Color_dummy, Tangents, false);

		}

		LocalOffset = FVector(-LocalExtent,-LocalExtent,0.f);

		if(NewElem.SectionVisibility.Num()!=6)
		{
			NewElem.SectionVisibility.SetNum(6);
			for(bool& el : NewElem.SectionVisibility)
				el = true;
		}

		if (NewElem.SectionVisibility_SegmentedCache.Num() != 6)
		{
			NewElem.SectionVisibility_SegmentedCache.SetNum(6);
			for (bool& el : NewElem.SectionVisibility_SegmentedCache)
				el = true;
		}

		NewElem.Config= EClipMapInteriorConfig::BotLeft;

		if(!Generator)
		{
#if SWDEBUG
			SW_LOG("Generator Material not available for Shader World %s", *GetName())
#endif
			UKismetRenderingLibrary::ClearRenderTarget2D(this, NewElem.HeightMap, FLinearColor::Black);
		}
		else
		{
			NewElem.CacheMatDyn = UMaterialInstanceDynamic::Create(Generator.Get(), this);

			NewElem.CacheMatDyn->SetVectorParameterValue("PatchLocation", FVector(NewElem.Location));
			NewElem.CacheMatDyn->SetScalarParameterValue("PatchFullSize", (N - 1) * NewElem.GridSpacing);
			NewElem.CacheMatDyn->SetScalarParameterValue("TexelPerSide", CacheRes+2);
			NewElem.CacheMatDyn->SetScalarParameterValue("NoMargin", 0.f);

			NewElem.CacheMatDyn->SetScalarParameterValue("MeshScale", (N - 1) * NewElem.GridSpacing * (CacheRes<=1?1: CacheRes / (CacheRes - 1)));
			NewElem.CacheMatDyn->SetScalarParameterValue("N", N);
			NewElem.CacheMatDyn->SetScalarParameterValue("CacheRes", CacheRes);
			NewElem.CacheMatDyn->SetScalarParameterValue("LocalGridScaling", NewElem.GridSpacing);
			NewElem.CacheMatDyn->SetScalarParameterValue("LOD0GridSpacing", pow(2.0,-(LOD_Num-1))*GridSpacing);
			
			NewElem.CacheMatDyn->SetScalarParameterValue("NormalMapSelect", 0.f);
			NewElem.CacheMatDyn->SetScalarParameterValue("HeightMapToggle", 1.f);


			TSet<FName> UsedNames;
			for (const FInstancedStruct& Seed : CurrentSeedsArray.SeedsArray)
			{
				if (Seed.IsValid())
				{
					const UScriptStruct* Type = Seed.GetScriptStruct();
					CA_ASSUME(Type);
					if (Type->IsChildOf(FTextureSeed::StaticStruct()))
					{
						FTextureSeed& TypedSeed = Seed.GetMutable<FTextureSeed>();
						if (!UsedNames.Contains(TypedSeed.SeedName))
						{
							UsedNames.Add(TypedSeed.SeedName);
							NewElem.CacheMatDyn->SetTextureParameterValue(TypedSeed.SeedName, TypedSeed.Value);
						}
					}
					else if (Type->IsChildOf(FLinearColorSeed::StaticStruct()))
					{
						FLinearColorSeed& TypedSeed = Seed.GetMutable<FLinearColorSeed>();
						if (!UsedNames.Contains(TypedSeed.SeedName))
						{
							UsedNames.Add(TypedSeed.SeedName);
							NewElem.CacheMatDyn->SetVectorParameterValue(TypedSeed.SeedName, TypedSeed.Value);
						}
					}
					else if (Type->IsChildOf(FScalarSeed::StaticStruct()))
					{
						FScalarSeed& TypedSeed = Seed.GetMutable<FScalarSeed>();
						if (!UsedNames.Contains(TypedSeed.SeedName))
						{
							UsedNames.Add(TypedSeed.SeedName);
							NewElem.CacheMatDyn->SetScalarParameterValue(TypedSeed.SeedName, TypedSeed.Value);
						}
					}
					else
					{
#if SWDEBUG
						SW_LOG("Invalid Seed type found: '%s'", *GetPathNameSafe(Type));
#endif

					}
				}
			}
			if (WorldShape == EWorldShape::Spherical)
			{

				NewElem.CacheMatDyn->SetScalarParameterValue("PlanetRadiusKm", PlanetRadiusKm);
			}

			if(false && HasActorBegunPlay())
			{				
			//UKismetRenderingLibrary::ClearRenderTarget2D(this, NewElem.HeightMap, FLinearColor::Black);			
#if SW_COMPUTE_GENERATION
		
#else
			UKismetRenderingLibrary::DrawMaterialToRenderTarget(this, NewElem.HeightMap, NewElem.CacheMatDyn);
#endif

			if (BrushManager)
				BrushManager->ApplyBrushStackToHeightMap(this,NewElem.Level, NewElem.HeightMap, FVector(NewElem.Location), NewElem.GridSpacing, N, false);

			}
			

			int layerIndice = 0;
			for(FClipMapLayer& Layer : LandDataLayers)
			{
				if(Layer.LayerName!="" && Layer.MaterialToGenerateLayer)
				{
					UMaterialInstanceDynamic* LayerDynMat = UMaterialInstanceDynamic::Create(Layer.MaterialToGenerateLayer, this);

					// required for Position to UV coord
					LayerDynMat->SetVectorParameterValue("PatchLocation", FVector(NewElem.Location));
					LayerDynMat->SetScalarParameterValue("PatchFullSize", (N - 1) * NewElem.GridSpacing);
					LayerDynMat->SetScalarParameterValue("TexelPerSide", CacheRes);
					LayerDynMat->SetScalarParameterValue("NoMargin", 1.f);

					LayerDynMat->SetScalarParameterValue("MeshScale", (N - 1) * NewElem.GridSpacing * (CacheRes<=1?1: CacheRes / (CacheRes - 1)));
					LayerDynMat->SetScalarParameterValue("N", N);
					LayerDynMat->SetScalarParameterValue("CacheRes", CacheRes);
					LayerDynMat->SetScalarParameterValue("LocalGridScaling", NewElem.GridSpacing);
					LayerDynMat->SetScalarParameterValue("LOD0GridSpacing", pow(2.0,-(LOD_Num-1))*GridSpacing);

					LayerDynMat->SetScalarParameterValue("NormalMapSelect", 1.f);

					if (UseSegmented())
					{
						LayerDynMat->SetTextureParameterValue("HeightMap", NewElem.HeightMap_Segmented);
						LayerDynMat->SetTextureParameterValue("NormalMap", NewElem.NormalMap_Segmented);
						for (int u = 0; u < layerIndice; u++)
						{
							LayerDynMat->SetTextureParameterValue(NewElem.LandLayers_names[u], NewElem.LandLayers_Segmented[u]);
						}						
					}
					else
					{
						LayerDynMat->SetTextureParameterValue("HeightMap", NewElem.HeightMap);
						LayerDynMat->SetTextureParameterValue("NormalMap", NewElem.NormalMap);
						for (int u = 0; u < layerIndice; u++)
						{
							LayerDynMat->SetTextureParameterValue(NewElem.LandLayers_names[u], NewElem.LandLayers[u]);
						}
					}

					if(Layer.GetParentCache)
					{
						if (i==0)
						{
							LayerDynMat->SetVectorParameterValue("RingLocation_Parent", FVector(NewElem.Location));

							if (UseSegmented())
							{
								LayerDynMat->SetTextureParameterValue("HeightMap_Parent", NewElem.HeightMap_Segmented);
								LayerDynMat->SetTextureParameterValue("NormalMap_Parent", NewElem.NormalMap_Segmented);
							}
							else
							{
								LayerDynMat->SetTextureParameterValue("HeightMap_Parent", NewElem.HeightMap);
								LayerDynMat->SetTextureParameterValue("NormalMap_Parent", NewElem.NormalMap);
							}

							LayerDynMat->SetScalarParameterValue("LocalGridScaling_Parent", NewElem.GridSpacing);

							for (int u = 0; u < layerIndice; u++)
							{
								FString ParentLayerString = NewElem.LandLayers_names[u].ToString()+"_Parent";
								FName ParentLayerName = FName(*ParentLayerString);
								
								if (UseSegmented())
									LayerDynMat->SetTextureParameterValue(ParentLayerName, NewElem.LandLayers_Segmented[u]);
								else
									LayerDynMat->SetTextureParameterValue(ParentLayerName, NewElem.LandLayers[u]);
							}
						}
						else
						{
							LayerDynMat->SetVectorParameterValue("RingLocation_Parent", FVector(Meshes[i-1].Location));

							if (UseSegmented())
							{
								LayerDynMat->SetTextureParameterValue("HeightMap_Parent", Meshes[i - 1].HeightMap_Segmented);
								LayerDynMat->SetTextureParameterValue("NormalMap_Parent", Meshes[i - 1].NormalMap_Segmented);
							}
							else
							{
								LayerDynMat->SetTextureParameterValue("HeightMap_Parent", Meshes[i - 1].HeightMap);
								LayerDynMat->SetTextureParameterValue("NormalMap_Parent", Meshes[i - 1].NormalMap);
							}

							LayerDynMat->SetScalarParameterValue("LocalGridScaling_Parent", Meshes[i-1].GridSpacing);

							if (UseSegmented())
							{
								for (int parent_layer_id = 0; parent_layer_id < Meshes[i - 1].LandLayers_Segmented.Num(); parent_layer_id++)
								{
									FString ParentLayerString = Meshes[i - 1].LandLayers_names[parent_layer_id].ToString() + "_Parent";
									FName ParentLayerName = FName(*ParentLayerString);
									LayerDynMat->SetTextureParameterValue(ParentLayerName, Meshes[i - 1].LandLayers_Segmented[parent_layer_id]);
								}
							}
							else
							{
								for (int parent_layer_id = 0; parent_layer_id < Meshes[i - 1].LandLayers.Num(); parent_layer_id++)
								{
									FString ParentLayerString = Meshes[i - 1].LandLayers_names[parent_layer_id].ToString() + "_Parent";
									FName ParentLayerName = FName(*ParentLayerString);
									LayerDynMat->SetTextureParameterValue(ParentLayerName, Meshes[i - 1].LandLayers[parent_layer_id]);
								}
							}
						}					
					}
					
					

					
					if (false && HasActorBegunPlay())
					{
					if (UseSegmented())
					{
						UKismetRenderingLibrary::ClearRenderTarget2D(this, NewElem.LandLayers_Segmented[layerIndice], FLinearColor::Black);

#if 0//SW_COMPUTE_GENERATION
						
#else
						UKismetRenderingLibrary::DrawMaterialToRenderTarget(this, NewElem.LandLayers_Segmented[layerIndice], LayerDynMat);
#endif
					}						
					else
					{
						UKismetRenderingLibrary::ClearRenderTarget2D(this, NewElem.LandLayers[layerIndice], FLinearColor::Black);
#if 0//SW_COMPUTE_GENERATION
						
#else
						UKismetRenderingLibrary::DrawMaterialToRenderTarget(this, NewElem.LandLayers[layerIndice], LayerDynMat);
#endif

					}
					}
						

					
					FString LocalLayerName = NewElem.LandLayers_names[layerIndice].ToString();

					//if (BrushManager)
					//	BrushManager->ApplyBrushStackToLayer(this, NewElem.Level, NewElem.LandLayers[layerIndice], FVector(NewElem.Location), NewElem.GridSpacing, N, LocalLayerName);
					if (false && HasActorBegunPlay())
					{
					if (BrushManager)
					{
						if(UseSegmented())
							BrushManager->ApplyBrushStackToLayer(this, NewElem.Level, NewElem.LandLayers_Segmented[layerIndice], FVector(NewElem.Location), NewElem.GridSpacing, N, LocalLayerName);
						else
							BrushManager->ApplyBrushStackToLayer(this, NewElem.Level, NewElem.LandLayers[layerIndice], FVector(NewElem.Location), NewElem.GridSpacing, N, LocalLayerName);

					}
						
					if (UseSegmented() && ShaderWorldSubsystem)
						ShaderWorldSubsystem->CopyAtoB(NewElem.LandLayers_Segmented[layerIndice], NewElem.LandLayers[layerIndice], 0);

					}
					NewElem.LayerMatDyn.Add(LayerDynMat);					

					layerIndice++;
				}
			}
			
		}

		
		{
			if(NewElem.Mesh)
			{
				if(!Material)
				{
#if SWDEBUG
					SW_LOG("No Material for Shader World %s", *GetName())
#endif
				}
				else
				{
					NewElem.MatDyn = UMaterialInstanceDynamic::Create(Material.Get(), this);

					if (WorldShape == EWorldShape::Spherical)
					{
						NewElem.MatDyn->SetScalarParameterValue("PlanetRadiusKm", PlanetRadiusKm);
					}
				}
			}

		}

		if(NewElem.MatDyn)
		{
			if(NewElem.Mesh)
			{
				NewElem.Mesh->SetMaterial(0, NewElem.MatDyn);
			}
			
			NewElem.MatDyn->SetScalarParameterValue("PatchLOD", LOD_Num-1-i);

			NewElem.MatDyn->SetVectorParameterValue("PatchLocation", FVector(NewElem.Location));
			NewElem.MatDyn->SetScalarParameterValue("PatchFullSize", (N - 1) * NewElem.GridSpacing);

			NewElem.MatDyn->SetScalarParameterValue("MeshScale", (N - 1) * NewElem.GridSpacing * (CacheRes<=1?1: CacheRes / (CacheRes - 1)));
			NewElem.MatDyn->SetScalarParameterValue("N", N);
			NewElem.MatDyn->SetScalarParameterValue("LocalGridScaling", NewElem.GridSpacing);
			NewElem.MatDyn->SetScalarParameterValue("CacheRes", CacheRes);

			NewElem.MatDyn->SetTextureParameterValue("HeightMap", NewElem.HeightMap);
			NewElem.MatDyn->SetTextureParameterValue("NormalMap", NewElem.NormalMap);

			for (int u = 0; u < NewElem.LandLayers.Num(); u++)
			{
				NewElem.MatDyn->SetTextureParameterValue(NewElem.LandLayers_names[u], NewElem.LandLayers[u]);
			}
		}

		if (i == LOD_Num - 1)
			NewElem.SetSectionVisible(1, false);
		else
			NewElem.SetSectionVisible(0, false);

		// botleft
		NewElem.SetSectionVisible(2, true);
		// topleft
		NewElem.SetSectionVisible(3, false);
		// botright
		NewElem.SetSectionVisible(4, false);
		// topright
		NewElem.SetSectionVisible(5, false);
		
		

		Meshes.Add(NewElem);

		if (NewElem.Mesh && NewElem.CacheMatDyn )
		{			
			if(UseSegmented())
			{
				SegmentedUpdateProcessed = false;
				ClipMapToUpdateAndMove[i] = true;
				NeedSegmentedUpdate[i] = true;
			}
			else
			{
				if (false && HasActorBegunPlay())
				{
					ComputeHeightMapForClipMap(i);
					ComputeNormalForClipMap(i, false);
					ComputeDataLayersForClipMap(i);
				}
			}
			
		}

	}
	
	if (DataReceiver)
		DataReceiver->UpdateStaticDataFor(this, CamLocation);
}

void AShaderWorldActor::Merge_SortList(FSWBiom& Biom,TArray<int>& SourceList)
{
	if (SourceList.Num() <= 0 || SourceList.Num() == 1)
		return;


	int MidIndex = SourceList.Num()/2;

	TArray<int> UnSortedList_Left;

	TArray<int> UnSortedList_Right;

	for (int i = 0; i < SourceList.Num(); i++)
	{
		if (i < MidIndex)
			UnSortedList_Left.Add(SourceList[i]);
		else
			UnSortedList_Right.Add(SourceList[i]);
	}

	Merge_SortList(Biom,UnSortedList_Left);
	Merge_SortList(Biom,UnSortedList_Right);

	TArray<int> Sorted_List;
	Sorted_List.Empty();

	while(UnSortedList_Left.Num()>0 && UnSortedList_Right.Num()>0)
	{
		//if(Biom.Spawnables[UnSortedList_Left[0]].GridSizeMeters*100.f* Biom.Spawnables[UnSortedList_Left[0]].NumberGridRings < Biom.Spawnables[UnSortedList_Right[0]].GridSizeMeters*100.f* Biom.Spawnables[UnSortedList_Right[0]].NumberGridRings)
		if (Biom.Spawnables[UnSortedList_Left[0]].CullDistance.Max < Biom.Spawnables[UnSortedList_Right[0]].CullDistance.Max)
		{
			Sorted_List.Add(UnSortedList_Right[0]);
			UnSortedList_Right.RemoveAt(0);			
		}
		else
		{
			Sorted_List.Add(UnSortedList_Left[0]);
			UnSortedList_Left.RemoveAt(0);
		}
	}

	if(UnSortedList_Left.Num()>0)
		Sorted_List.Append(UnSortedList_Left);

	UnSortedList_Left.Empty();

	if (UnSortedList_Right.Num() > 0)
		Sorted_List.Append(UnSortedList_Right);

	UnSortedList_Right.Empty();

	for (int i = 0; i < SourceList.Num(); i++)
	{
		SourceList[i] = Sorted_List[i];
	}

}


void AShaderWorldActor::SortSpawnabledBySurface(FSWBiom& Biom)
{
	//No point computing further than we can see
	/*
	for (auto& El : Biom.Spawnables)
	{
		if (El.CullDistance.Max > 0.f)
		{
			if (El.CullDistance.Max < 50000.f)
			{
				const float MminD = FMath::Clamp(El.CullDistance.Max * (1.0 + 4.0 / 100.0), El.CullDistance.Max, 50000.f);
				const float MmaxD = FMath::Clamp(El.CullDistance.Max * (1.0 + 15.0 / 100.0), El.CullDistance.Max, 50000.f);
				El.GridSizeMeters = FMath::Clamp(El.GridSizeMeters, MminD / 100.0, MmaxD / 100.0);
			}
			else
			{
				bool found = false;
				for (uint32 Subdi = 1; Subdi <= 30; Subdi++)
				{
					if (El.CullDistance.Max * (1.0 + 4.0 / 100.0) / Subdi <= 50000.f)
					{
						El.GridSizeMeters = El.CullDistance.Max / 100.0 * (1.0 + 4.0 / 100.0) / Subdi;
						found = true;
						break;
					}
				}
				if (!found)
					El.GridSizeMeters = 500.0f;
			}
		}

	}*/

	Biom.SortedSpawnables.Empty();
	Biom.SortedSpawnables_Collision.Empty();

	if (Biom.Spawnables.Num() == 1)
	{
		FSpawnableMesh& Spawn = Biom.Spawnables[0];
		if(Spawn.CollisionEnabled)
			Biom.SortedSpawnables_Collision.Add(0);

		Biom.SortedSpawnables.Add(0);
		return;
	}

	for (int i = 0; i < Biom.Spawnables.Num(); i++)
	{		
		Biom.SortedSpawnables.Add(i);
	}
	
	Merge_SortList(Biom, Biom.SortedSpawnables);
	
	for (int i = Biom.SortedSpawnables.Num() - 1; i >= 0; i--)
	{
		int& indice = Biom.SortedSpawnables[i];
		if(indice< Biom.Spawnables.Num())
		{
			FSpawnableMesh& Spawn = Biom.Spawnables[indice];
			if (Spawn.CollisionEnabled)
				Biom.SortedSpawnables_Collision.Add(indice);
		}	
		else
		{
			//UE_LOG(LogTemp,Warning,TEXT("Sorted indice outside of valid range"));
			UE_LOG(LogTemp, Warning, TEXT("ShaderWorld : Sorted indice outside of valid range (Spawn.HIM_Mesh.Num() != W.InstancesT->Transforms.Num())"))
			rebuildVegetationOnly=true;
		}
	}

	
}

void AShaderWorldActor::SetupCameraForSpawnable(FSpawnableMesh& Spawn, TSet<FIntVector>& Cameras_Proximity, TSet<FIntVector>& All_Cameras)
{

	Spawn.CamerasUpdateTime = SpawnablesUpdateTime;
	All_Cameras.Empty();
	Cameras_Proximity.Empty();

	bool IsServer = false;
	if((GetNetMode() != NM_Standalone) && (GetNetMode() != NM_Client))
	{
		IsServer=true;
	}

	if(!IsServer)
	{

		double Cam_X = (CamLocation.X + GetWorld()->OriginLocation.X) / (Spawn.GridSizeMeters * 100.0);
		double Cam_Y = (CamLocation.Y + GetWorld()->OriginLocation.Y) / (Spawn.GridSizeMeters * 100.0);
		//double Cam_Z = (CamLocation.Z + GetWorld()->OriginLocation.Z) / (Spawn.GridSizeMeters * 100.0);

		int CamX = FMath::RoundToInt(Cam_X);
		int CamY = FMath::RoundToInt(Cam_Y);

		All_Cameras.Add(FIntVector(CamX, CamY, 0));
		Cameras_Proximity.Add(FIntVector(CamX, CamY, 0));
		for (int i = -1; i <= 1; i++)
		{
			for (int j = -1; j <= 1; j++)
			{
				if (i == 0 && j == 0)
					continue;

				double Cam_X_local = (CamLocation.X + GetWorld()->OriginLocation.X + i * Spawn.GridSizeMeters * 100.0 / 5.f) / (Spawn.GridSizeMeters * 100.0);
				double Cam_Y_local = (CamLocation.Y + GetWorld()->OriginLocation.Y + j * Spawn.GridSizeMeters * 100.0 / 5.f) / (Spawn.GridSizeMeters * 100.0);

				Cameras_Proximity.Add(FIntVector(FMath::RoundToInt(Cam_X_local), FMath::RoundToInt(Cam_Y_local), 0));
			}
		}
	}
	else
	{
		for(auto& CamLoc : CameraLocations)
		{
			double Cam_X = (CamLoc.X + GetWorld()->OriginLocation.X) / (Spawn.GridSizeMeters * 100.0);
			double Cam_Y = (CamLoc.Y + GetWorld()->OriginLocation.Y) / (Spawn.GridSizeMeters * 100.0);
			//double Cam_Z = (CamLoc.Z + GetWorld()->OriginLocation.Z) / (Spawn.GridSizeMeters * 100.0);

			int CamX = FMath::RoundToInt(Cam_X);
			int CamY = FMath::RoundToInt(Cam_Y);

			All_Cameras.Add(FIntVector(CamX, CamY, 0));
			Cameras_Proximity.Add(FIntVector(CamX, CamY, 0));
			for (int i = -1; i <= 1; i++)
			{
				for (int j = -1; j <= 1; j++)
				{
					if (i == 0 && j == 0)
						continue;

					double Cam_X_local = (CamLoc.X + GetWorld()->OriginLocation.X + i * Spawn.GridSizeMeters * 100.0 / 5.0f) / (Spawn.GridSizeMeters * 100.0);
					double Cam_Y_local = (CamLoc.Y + GetWorld()->OriginLocation.Y + j * Spawn.GridSizeMeters * 100.0 / 5.0f) / (Spawn.GridSizeMeters * 100.0);

					Cameras_Proximity.Add(FIntVector(FMath::RoundToInt(Cam_X_local), FMath::RoundToInt(Cam_Y_local), 0));
				}
			}
		}
	}

}

void AShaderWorldActor::ReleaseCollisionBeyondRange(FSpawnableMesh& Spawn, TSet<FIntVector>& Cam_Proximity)
{
	for (int i = Spawn.UsedSpawnablesCollisionElem.Num() - 1; i >= 0; i--)
	{
		FSpawnableMeshProximityCollisionElement& El = Spawn.SpawnablesCollisionElem[Spawn.UsedSpawnablesCollisionElem[i]];

		bool Beyond = true;

		if(Cam_Proximity.Contains(FIntVector(El.Location.X, El.Location.Y,0)))
			Beyond = false;

		if (Beyond)
		{			
			Spawn.AvailableSpawnablesCollisionElem.Add(El.ID);
			Spawn.UsedSpawnablesCollisionElem.RemoveAt(i);

			for (auto It = Spawn.SpawnablesCollisionLayout.CreateConstIterator(); It; ++It)
			{
				if (It->Value == El.ID)
				{
					Spawn.SpawnablesCollisionLayout.Remove(It->Key);
					break;
				}
			}
		}
	}
}

int32 AShaderWorldActor::FindBestCandidate_SpawnableElem(int32& GridSizeMeters,int& IndexOfClipMapForCompute, FSpawnableMeshElement& El,bool& Segmented)
{
	FVector Location_Mesh_LocalOrigin = FVector(El.Location * GridSizeMeters * 100.0 + FIntVector(0.f, 0.f, 1) * HeightOnStart - GetWorld()->OriginLocation);

	FVector2D Location_Mesh(Location_Mesh_LocalOrigin.X, Location_Mesh_LocalOrigin.Y);
	FVector2D Extent = GridSizeMeters * 100.f / 2.f * FVector2D(1.f, 1.f) * 1.25f;//Margin
	FBox2D LocalMeshBox(Location_Mesh - Extent, Location_Mesh + Extent);

	int LOD_Candidate = -1;
	///////////////////////////////

	for (int k = 1; IndexOfClipMapForCompute + k < GetMeshNum(); k++)
	{
		int index_local = IndexOfClipMapForCompute + k;
		FClipMapMeshElement& Elem_Local = GetMesh(index_local);
		FIntVector ClipMapLocation = Elem_Local.Location - GetWorld()->OriginLocation;

		FVector2D Location_Elem_Local(ClipMapLocation.X, ClipMapLocation.Y);
		FVector2D Extent_Elem_Local = (N - 1 - 1) * Elem_Local.GridSpacing / 2.f * FVector2D(1.f, 1.f);
		FBox2D Elem_Local_Footprint(Location_Elem_Local - Extent_Elem_Local, Location_Elem_Local + Extent_Elem_Local);

		if (Elem_Local_Footprint.IsInside(LocalMeshBox.Max) && Elem_Local_Footprint.IsInside(LocalMeshBox.Min) && (Elem_Local.IsSectionVisible(0, Segmented) || Elem_Local.IsSectionVisible(1, Segmented)))
		{
			LOD_Candidate = index_local;
		}
		else
		{
			break;
		}

	}

	return LOD_Candidate;
}

void AShaderWorldActor::ReleaseSpawnElemBeyondRange(FSWBiom& Biom, FSpawnableMesh& Spawn, TSet<FIntVector>& Cam_Proximity, TSet<FIntVector>& All_cams, bool& Segmented,int& MaxRing)
{
	/*
	 *	All_cams.Num()>0 is assured
	*/

	FIntVector LocMeshInt(0);
	for (auto& C: All_cams)
	{
		LocMeshInt=C;
		break;
	}
	

	for (int i = Spawn.UsedSpawnablesElem.Num() - 1; i >= 0; i--)
	{

		FSpawnableMeshElement& El = Spawn.SpawnablesElem[Spawn.UsedSpawnablesElem[i]];

		bool Beyond = true;

		if(Cam_Proximity.Contains(FIntVector(El.Location.X, El.Location.Y,0)))
			Beyond = false;		

		FVector ToComp = FVector(El.Location - LocMeshInt);

		bool NeedToUpdateCollisionMeshElement = false;
		bool CollisionProxyHasToBeUpdated = false;

		if (Spawn.SpawnType != ESpawnableType::Actor)
		{
			if (Spawn.CollisionEnabled && Spawn.CollisionOnlyAtProximity && Spawn.NumberGridRings > 0)
			{
				if (Beyond)
				{
					El.Collision_Mesh_ID = -1;
				}					
				else
				{
					CollisionProxyHasToBeUpdated=true;

					if (El.Collision_Mesh_ID < 0)
					{
						//if (Spawn.CollisionEnabled && Spawn.CollisionOnlyAtProximity && Spawn.NumberGridRings > 0)
						{
							NeedToUpdateCollisionMeshElement = true;

							FSpawnableMeshProximityCollisionElement& CollisionMeshElem = Spawn.GetASpawnableCollisionElem();
							El.Collision_Mesh_ID = CollisionMeshElem.ID;
							CollisionMeshElem.Location = El.Location;
							Spawn.SpawnablesCollisionLayout.Add(LocMeshInt, CollisionMeshElem.ID);

							Spawn.SpawnablesElemNeedCollisionUpdate.Add(Spawn.UsedSpawnablesElem[i]);
							//UE_LOG(LogTemp,Warning,TEXT("Assign collision to new element"));
						}
					}

				}
			}
		}


		if (FMath::Abs(ToComp.X) > Spawn.NumberGridRings + 0.1f || FMath::Abs(ToComp.Y) > Spawn.NumberGridRings + 0.1f)
		{
			
			El.ComputeLaunched = true;
			El.InstancesT->Transforms.Empty();

			Spawn.ReleaseSpawnableElem(El.ID);
			/*
			Spawn.AvailableSpawnablesElem.Add(El.ID);
			Spawn.UsedSpawnablesElem.RemoveAt(i);
			Spawn.SpawnablesLayout.Remove(El.Location);
			*/
			/*		

			for (auto It = Spawn.SpawnablesLayout.CreateConstIterator(); It; ++It)
			{
				if (It->Value == El.ID)
				{
					Spawn.SpawnablesLayout.Remove(It->Key);					
					break;
				}
			}*/

		}
		else
		{

			if (Spawn.IndexOfClipMapForCompute >= 0 && Spawn.IndexOfClipMapForCompute < GetMeshNum())
			{
				FClipMapMeshElement& Elem = GetMesh(Spawn.IndexOfClipMapForCompute);

				if (Elem.IsSectionVisible(0, Segmented) || Elem.IsSectionVisible(1, Segmented))
				{

					FVector Location_Mesh_LocalOrigin = FVector(El.Location * Spawn.GridSizeMeters * 100.0 + FIntVector(0.f, 0.f, 1) * HeightOnStart - GetWorld()->OriginLocation);

					
					{
						//if in view frustum
						if (/*MaxRing>=0 ||*/ ViewFrustum.Planes.Num() <= 0 || ViewFrustum.IntersectBox(FVector(Location_Mesh_LocalOrigin), Spawn.ExtentOfMeshElement))
						{
							//if closer lod is available than the one used initially to place assets
							FVector2D Location_Mesh(Location_Mesh_LocalOrigin.X, Location_Mesh_LocalOrigin.Y);
							FVector2D Extent = Spawn.GridSizeMeters * 100.f / 2.f * FVector2D(1.f, 1.f) * 1.25f;//Margin
							FBox2D LocalMeshBox(Location_Mesh - Extent, Location_Mesh + Extent);

							int LOD_Candidate = -1;
							///////////////////////////////

							LOD_Candidate = FindBestCandidate_SpawnableElem(Spawn.GridSizeMeters,Spawn.IndexOfClipMapForCompute,El,Segmented);

							if (LOD_Candidate >= 0 && LOD_Candidate > El.LOD_usedLastUpdate)
							{
								
								//if i have draw calls left, update asset position
								if (CanUpdateSpawnables())
								{
									
									if(LOD_Candidate > (Meshes.Num()-1-Spawn.PositionCanBeAdjustedWithLOD))
										El.NextUpdateIsAPositionAdjustement=true;

									if (Segmented)
									{
										SegmentedUpdateProcessedButSpawnablesLeft = true;
										Spawn.SegmentedOnly_ElementToUpdateData.Add(Spawn.UsedSpawnablesElem[i]);
									}
									else
										Spawn.UpdateSpawnableData(Biom,El);


									if(CollisionProxyHasToBeUpdated && !NeedToUpdateCollisionMeshElement)
										Spawn.SpawnablesElemNeedCollisionUpdate.Add(Spawn.UsedSpawnablesElem[i]);
								}
								else{}
							}
							else{}
						}
						else		{}
					}
				}
			}
			else
			{
				//UE_LOG(LogTemp,Warning,TEXT("IndexOfClipMapForCompute %d !(Spawn.IndexOfClipMapForCompute > 0 && Spawn.IndexOfClipMapForCompute < GetMeshNum())"),Spawn.IndexOfClipMapForCompute);
			}
		}
	}
}

void AShaderWorldActor::AssignSpawnableMeshElement(int& i,int& j,bool& Segmented, FSWBiom& Biom, FSpawnableMesh& Spawn,FIntVector& LocMeshInt, TSet<FIntVector>& Cam_Proximity)
{
	FSpawnableMeshElement& Mesh = Spawn.GetASpawnableElem();

	bool Beyond = false;

	for (FIntVector& Cam : Cam_Proximity)
	{
		if (LocMeshInt.X == Cam.X && LocMeshInt.Y == Cam.Y)
		{
			Beyond = true;
			break;
		}
	}

	

	if ((abs(i) <= 1 * 0 && abs(j) <= 1 * 0 || Beyond) && Spawn.CollisionEnabled && Spawn.CollisionOnlyAtProximity && Spawn.NumberGridRings > 0)
	{
		FSpawnableMeshProximityCollisionElement& CollisionMeshElem = Spawn.GetASpawnableCollisionElem();
		CollisionMeshElem.OffsetOfSegmentedUpdate.Empty();
		Mesh.Collision_Mesh_ID = CollisionMeshElem.ID;
		CollisionMeshElem.Location = LocMeshInt;//MeshLoc;
		Spawn.SpawnablesCollisionLayout.Add(LocMeshInt, CollisionMeshElem.ID);
		Spawn.SpawnablesElemNeedCollisionUpdate.Add(Mesh.ID);
	}

	//set location before UpdateSpawnableData as we'll use it
	Mesh.Location = LocMeshInt;	

	if (Segmented)
	{
		SegmentedUpdateProcessedButSpawnablesLeft = true;
		Spawn.SegmentedOnly_ElementToUpdateData.Add(Mesh.ID);
	}
	else
		Spawn.UpdateSpawnableData(Biom,Mesh);

	Spawn.SpawnablesLayout.Add(LocMeshInt, Mesh.ID);
}



bool AShaderWorldActor::UpdateSpawnable(FSWBiom& Biom, int indice, int Biomindice, bool MustBeInFrustum, int MaxRing)
{
	FSpawnableMesh& Spawn = Biom.Spawnables[indice];

	if (!Spawn.bHasValidSpawningData())
		return true;
			
	if (!Spawn.Owner)
		Spawn.Initiate(this, indice, Biomindice);


	if (!Spawn.ProcessedRead.IsValid() || !Spawn.ProcessedRead->bProcessingCompleted || Spawn.SpawnableWorkQueue.Num() > 0)
		return true;

	bool Segmented = UseSegmented();

	TSet<FIntVector>& All_Cams = Spawn.All_Cams;
	TSet<FIntVector>& Cam_Proximity = Spawn.Cam_Proximity;
	if(((SpawnablesUpdateTime - Spawn.CamerasUpdateTime)>0.1) || (Spawn.CamerasUpdateTime > SpawnablesUpdateTime))
		SetupCameraForSpawnable(Spawn,Cam_Proximity, All_Cams);

	if(Cam_Proximity.Num()<=0 || All_Cams.Num()<=0)
	{
		return true;
	}	

	//Only necessary to do this once during the initial InFrustum pass
	if (MustBeInFrustum && (MaxRing<0) && !Spawn.CollisionEnabled || MaxRing>=0)
	{		
		ReleaseCollisionBeyondRange(Spawn,Cam_Proximity);	
		ReleaseSpawnElemBeyondRange(Biom,Spawn,Cam_Proximity, All_Cams,Segmented,MaxRing);
	}

	bool InterruptUpdate = false;

	if (Spawn.IndexOfClipMapForCompute >= 0 && Spawn.IndexOfClipMapForCompute < GetMeshNum())
	{
		FClipMapMeshElement& Elem = GetMesh(Spawn.IndexOfClipMapForCompute);

		if (!Elem.IsSectionVisible(0,Segmented) && !Elem.IsSectionVisible(1,Segmented))
			return true;
	}

	int CurrentRingScope = MaxRing>=0?FMath::Min(MaxRing,Spawn.NumberGridRings):Spawn.NumberGridRings;

	FIntVector LocalCam(0);
	for(auto& C: All_Cams)
	{
		LocalCam=C;
		break;
	}
			
	for (int r = 0; r <= CurrentRingScope; r++)
	{
	
		for (int i = -r; i <= r; i++)
		{
			if (InterruptUpdate)
				break;

			for (int j = -r; j <= r; j++)
			{
				if(abs(j)!=r && abs(i)!=r)
				continue;

				FIntVector LocMeshInt = LocalCam + FIntVector(i, j, 0);

				FIntVector MeshLoc = LocMeshInt * Spawn.GridSizeMeters * 100.0 + FIntVector(0.f, 0.f, 1) * HeightOnStart - GetWorld()->OriginLocation;

				if (!Spawn.SpawnablesLayout.Contains(LocMeshInt))
				{
					if (!MustBeInFrustum || MustBeInFrustum && (ViewFrustum.Planes.Num() <= 0 || ViewFrustum.IntersectBox(FVector(MeshLoc), Spawn.ExtentOfMeshElement)))
					{
						if (CanUpdateSpawnables())
						{
							AssignSpawnableMeshElement(i,j,Segmented, Biom,Spawn,LocMeshInt,Cam_Proximity);
						}
						else
						{
							InterruptUpdate = true;
							break;
						}
					}
				}
			}
		}
	}

	if (InterruptUpdate)
	{		
		return false;
	}
	return true;
	
}

void FSpawnableMesh::InitiateNew(AShaderWorldActor* Owner_, int32 Index, int32 BiomIndex)
{
	CleanUp();

	if (Owner_ && bHasValidSpawningData())
	{
		Owner = Owner_;

		UWorld* World = Owner->GetWorld();

		FoliageIndexSeed = Index;

		ProcessedRead = MakeShared<FSWShareableIndexesCompletion, ESPMode::ThreadSafe>();
		ProcessedRead->bProcessingCompleted = true;

		/// Computation Optimization
		/// instead of evaluating the noise and generating the precise normal, we can use the already computed heightmap/NormalMap
		/// it adds a dependency to those maps/this ring but makes for far better performances
		/// 
		int32 MaxCullDistance = 0;


		float MaxDrawDistanceScale = GetCachedScalabilityCVars().ViewDistanceScale;

		if (MaxDrawDistanceScale <= 0.f)
			MaxDrawDistanceScale = 1.f;

		// Find number of rings START
		if (UpdateSpawnSettings)
		{
			/*
			 *	Update Cull distance and density from privded asset
			 */
			if (Foliages.Num() > 0)
			{
				for (UFoliageType_InstancedStaticMesh* FoliageType : Foliages)
				{
					if (FoliageType && FoliageType->Mesh)
					{
						if (FoliageType->CullDistance.Max > MaxCullDistance)
						{
							MaxCullDistance = FoliageType->CullDistance.Max;

							CullDistance.Min = FoliageType->CullDistance.Min;
							CullDistance.Max = FoliageType->CullDistance.Max;
						}
					}
				}
			}
			else if (GrassType)
			{
				for (FGrassVariety& Grass : GrassType->GrassVarieties)
				{
					if (Grass.GrassMesh)
					{
						if (Grass.EndCullDistance.Default > MaxCullDistance)
						{
							MaxCullDistance = Grass.EndCullDistance.Default;

							CullDistance.Min = Grass.StartCullDistance.Default;
							CullDistance.Max = Grass.EndCullDistance.Default;
						}
					}
				}
			}
			else
			{
				MaxCullDistance = CullDistance.Max;
			}

			if (GrassType)
				for (FGrassVariety& Grass : GrassType->GrassVarieties)
				{
					if (Grass.GrassDensity.Default > Density)
						Density = Grass.GrassDensity.Default;
				}

			for (UFoliageType_InstancedStaticMesh* FoliageType : Foliages)
			{
				if (FoliageType && FoliageType->Density > Density)
					Density = FoliageType->Density;

			}
		}
		else
		{
			MaxCullDistance = CullDistance.Max;
		}


		{
			if (MaxCullDistance <= 0.f)
				MaxCullDistance = 500; // 5 meters

			float MaxDistanceMeters = MaxCullDistance * FMath::Min(1.f, MaxDrawDistanceScale) / 100.f;

			/*
			 * We have a draw distance and a density
			 * InstanceCountPerSector = Density * (GridSizeMeters*GridSizeMeters)/(10.f*10.f);
			 *
			 * If collision, max instance per patch 1000, if no collision max instance per patch 10000
			 * Figure out Grid size meter
			 */

			int32 iterGridCount = 1;


			/*
			 * GridSizeMeters must be independant from MaxDrawDistanceScale which can vary depending on scalability settings
			 * We therefore need to compute it regardless
			 */
			/*
			 * As a comparison, for Unreal Engine landscape, GridSizeMeters equals the width in meter of a single LandscapeComponent
			 * Here we adjust GridSizeMeters given the amount of entities that a component will hold. Entities with physic notably need to have less
			 */

			while (iterGridCount < 30)
			{
				float GridSizeMeterCandidate = FMath::CeilToInt((MaxCullDistance / 100.f * 1.075f / ((float)iterGridCount)));

				int32 InstanceCountCandidate = Density * (GridSizeMeterCandidate * GridSizeMeterCandidate) / (10.f * 10.f);

				if (CollisionEnabled)
				{
					if (InstanceCountCandidate < GSWMaxCollisionInstancesPerComponent)
					{
						GridSizeMeters = GridSizeMeterCandidate;
						break;
					}
				}
				else
				{
					if (InstanceCountCandidate < GSWMaxInstancesPerComponent)
					{
						GridSizeMeters = GridSizeMeterCandidate;
						break;
					}
				}

				iterGridCount++;
			}

			if (GridSizeMeters > 0.1)
				iterGridCount = FMath::CeilToInt(MaxDistanceMeters * 1.075f / GridSizeMeters);
			else
				iterGridCount = 1;

			if (iterGridCount > 30)
				iterGridCount = 30;

			int NumberOfRings = iterGridCount;//FMath::Max(1, FMath::RoundToInt(MaxDistanceMeters / GridSizeMeters));

			// As we re rounding there case where we wont compute far enough - we tolerate it if within 25% of GridSizeMeters away
			if ((MaxDistanceMeters - NumberOfRings * GridSizeMeters) > 0.25f * GridSizeMeters)
				NumberOfRings++;

			//if(NumberGridRings>=30)
			NumberGridRings = FMath::Max(1, FMath::Min(30, NumberOfRings));
		}
		// Find number of rings END

		//float NoPoppingRange = NoPoppingRange;//meters

		FVector2D Extent = GridSizeMeters * 100.f * (NumberGridRings + 1) * FVector2D(1.f, 1.f) * 1.1f;//Margin
		FVector2D Extent_single = Owner_->NoPoppingRange * 100.f * FVector2D(1.f, 1.f) + GridSizeMeters * 100.f * FVector2D(1.f, 1.f) * 1.1f;//Margin
		FBox2D LocalMeshBox(-Extent, Extent);


		IndexOfClipMapForCompute = 0;
		PositionCanBeAdjustedWithLOD = 0;

		for (int i = Owner->GetMeshNum() - 1; i >= 0; i--)
		{
			FClipMapMeshElement& Elem = Owner->GetMesh(i);

			FVector2D Extent_Elem_Local = (Owner->N - 1 - 1) * Elem.GridSpacing / 2.f * FVector2D(1.f, 1.f);

			FBox2D Elem_Local_Footprint(-Extent_Elem_Local, Extent_Elem_Local);

			if (Elem_Local_Footprint.IsInside(LocalMeshBox.Max))
			{
				IndexOfClipMapForCompute = i;

			}
			if (Elem_Local_Footprint.IsInside(Extent_single) && PositionCanBeAdjustedWithLOD == 0)
			{
				PositionCanBeAdjustedWithLOD =/*Owner->GetMeshNum()-1-*/i;

			}
			if (IndexOfClipMapForCompute > 0)
				break;
		}

		//PositionCanBeAdjustedWithLOD Include the no popping condition - it might have a larger footprint than all the rings combined
		if (PositionCanBeAdjustedWithLOD < IndexOfClipMapForCompute)
			PositionCanBeAdjustedWithLOD = IndexOfClipMapForCompute;

		PositionCanBeAdjustedWithLOD = Owner->GetMeshNum() - 1 - PositionCanBeAdjustedWithLOD;

		//VERIF OK

		InstanceCountPerSector = Density * (GridSizeMeters * GridSizeMeters) / (10.f * 10.f);


		int PoolItemCount = 0;
		NumInstancePerHIM->Indexes.Empty();

		if (SpawnType != ESpawnableType::Actor)
		{
			if (GrassType)
			{
				for (FGrassVariety& Grass : GrassType->GrassVarieties)
				{
					if (Grass.GrassMesh)
					{
						PoolItemCount++;
						NumInstancePerHIM->Indexes.Add(0);
					}
				}
			}

			for (UFoliageType_InstancedStaticMesh* Sm : Foliages)
			{
				if (Sm && Sm->Mesh)
				{
					PoolItemCount++;
					NumInstancePerHIM->Indexes.Add(0);
				}
			}
			for (UStaticMesh* Sm : Mesh)
			{
				if (Sm)
				{
					PoolItemCount++;
					NumInstancePerHIM->Indexes.Add(0);
				}
			}
		}
		else
			for (TSubclassOf<AActor>& A : Actors)
			{
				if ((*A) != nullptr)
				{
					PoolItemCount++;
					NumInstancePerHIM->Indexes.Add(0);
				}
			}

		/*
		 * Si je calcule moins d'instances qu'il n'y a de varieté | PLUS VALIDE
		 */
		//if (InstanceCountPerSector < PoolItemCount)
		//	InstanceCountPerSector = PoolItemCount;

		RT_Dim = FMath::Floor(FMath::Sqrt((float)InstanceCountPerSector)) + (FMath::Frac(FMath::Sqrt((float)InstanceCountPerSector)) > 0.f ? 1 : 0);

		// Compute grid real world size is : RegionWorldDimension * RT_Dim / (RT_Dim - 1)
		// To reach top right corner from center of grid we using half this distance in X Y. 
		// Adding a large Z value to counter our lack of information about the height assets will spawn at

		ExtentOfMeshElement = FVector(1.f, 1.f, 0.f) * (GridSizeMeters * 100.f * (RT_Dim <= 1 ? 1 : RT_Dim / (RT_Dim - 1))) / 2.f + FVector(0.f, 0.f, 1.f) * (Owner->VerticalRangeMeters * 100.f);

		const int NumOfVertex = RT_Dim * RT_Dim;

#if 0
		/*
		 * The RT computing for all varieties, distribute texel to each variety | WRONG NOW Each variety will fully use the RT to behave similar to landscape grass
		 * WRONG NOW Each SpawnableMeshElement will have its set of HISM
		 *
		 * In short, I used to have a large buffer computed and the result was distributed among several HISM
		 * Now each HISM will compute its own buffer
		 */
		for (int i = 0; i < NumOfVertex; i++)
		{
			InstanceIndexToHIMIndex->Indexes.Add(i % PoolItemCount);
			InstanceIndexToIndexForHIM->Indexes.Add(NumInstancePerHIM->Indexes[i % PoolItemCount]);
			NumInstancePerHIM->Indexes[i % PoolItemCount] = NumInstancePerHIM->Indexes[i % PoolItemCount] + 1;
		}
#endif

		if (SpawnType != ESpawnableType::Actor)
		{

#if 1
			int32 FoliageVar = 0;
			for (UFoliageType_InstancedStaticMesh* FoliageType : Foliages)
			{

				if (FoliageType && FoliageType->GetStaticMesh())
				{

					USWHISMComponent* NHISM = nullptr;
					bool FoliageComp = false;

					UClass* ComponentClass = *FoliageType->ComponentClass;
					if (ComponentClass == nullptr || !ComponentClass->IsChildOf(USWHISMComponent::StaticClass()))
					{
						ComponentClass = USWHISMComponent::StaticClass();
					}

					NHISM = NewObject<USWHISMComponent>(Owner, ComponentClass, NAME_None, RF_Transient);					

					NHISM->ComponentTags = ComponentTags;

					NHISM->bHasPerInstanceHitProxies = false;
					NHISM->SetCanEverAffectNavigation(false);
					NHISM->bUseTranslatedInstanceSpace = false;
					NHISM->bAlwaysCreatePhysicsState = false;

					if (UFoliageInstancedStaticMeshComponent* FComp = Cast<UFoliageInstancedStaticMeshComponent>(NHISM))
						FoliageComp = true;

					NHISM->SetupAttachment(Owner->GetRootComponent());
					NHISM->RegisterComponent();
					NHISM->SetStaticMesh(FoliageType->GetStaticMesh());
					NHISM->SetRelativeLocation(FVector(0.f, 0.f, 0.f));

					NHISM->SetUsingAbsoluteLocation(true);
					NHISM->SetUsingAbsoluteRotation(true);

					NHISM->SetKeepInstanceBufferCPUCopy(bKeepInstanceBufferCPUCopy);

					if (UpdateSpawnSettings)
					{
						UpdateStaticMeshHierachyComponentSettings(NHISM, CullDistance);

						CastShadows = FoliageType->CastShadow;
						bAffectDynamicIndirectLighting = FoliageType->bAffectDynamicIndirectLighting;
						bAffectDistanceFieldLighting = FoliageType->bAffectDistanceFieldLighting;
						bCastShadowAsTwoSided = FoliageType->bCastShadowAsTwoSided;
						bReceivesDecals = FoliageType->bReceivesDecals;

						CollisionEnabled = FoliageType->BodyInstance.GetCollisionEnabled() != ECollisionEnabled::NoCollision;

						AlignMaxAngle = FoliageType->AlignToNormal ? FoliageType->AlignMaxAngle : 0.f;

						AltitudeRange.Min = FoliageType->Height.Min;
						AltitudeRange.Max = FoliageType->Height.Max;

						VerticalOffsetRange.Min = FoliageType->ZOffset.Min;
						VerticalOffsetRange.Max = FoliageType->ZOffset.Max;

						GroundSlopeAngle.Min = FoliageType->GroundSlopeAngle.Min;
						GroundSlopeAngle.Max = FoliageType->GroundSlopeAngle.Max;
					}


					NHISM->CastShadow = CastShadows;
					NHISM->bCastDynamicShadow = CastShadows;
					NHISM->bCastStaticShadow = false;

					NHISM->InstanceStartCullDistance = CullDistance.Min;
					NHISM->InstanceEndCullDistance = CullDistance.Max;

					NHISM->bAffectDynamicIndirectLighting = bAffectDynamicIndirectLighting;
					NHISM->bAffectDistanceFieldLighting = bAffectDistanceFieldLighting;
					NHISM->bCastShadowAsTwoSided = bCastShadowAsTwoSided;
					NHISM->bReceivesDecals = bReceivesDecals;

					{
						// To guarantee consistency across platforms, we force the string to be lowercase and always treat it as an ANSI string.
						int32 FolSeed = FCrc::StrCrc32(StringCast<ANSICHAR>(*FString::Printf(TEXT("%s%s%d"), *FoliageType->GetName().ToLower(), *NHISM->GetName().ToLower(), FoliageVar)).Get());
						if (FolSeed == 0)
						{
							FolSeed++;
						}

						NHISM->InstancingRandomSeed = FolSeed;
						NHISM->Mobility = EComponentMobility::Static;
						NHISM->PrecachePSOs();
					}



					if (NumberGridRings == 0 || !CollisionOnlyAtProximity && (NumberGridRings > 0))
						NHISM->SetCollisionEnabled(CollisionEnabled ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
					else
						NHISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);

					HIM_Mesh.Add(NHISM);
					HIM_Mesh_TreeRebuild_Time.Add(0);

					if (CollisionEnabled && CollisionOnlyAtProximity && NumberGridRings > 0)
					{
						USWHISMComponent* NHISM_w_Collision = nullptr;

						//NHISM_w_Collision = NewObject<UHierarchicalInstancedStaticMeshComponent>(Owner, ComponentClass, NAME_None, RF_Transient);
						NHISM_w_Collision = NewObject<USWHISMComponent>(Owner, USWCollectableInstancedSMeshComponent::StaticClass(), NAME_None, RF_Transient);

						NHISM_w_Collision->bUseTranslatedInstanceSpace = false;
						NHISM_w_Collision->SetCanEverAffectNavigation(false);
						NHISM_w_Collision->ComponentTags = ComponentTags;

						Owner->CollisionToSpawnable.Add(NHISM_w_Collision, this);
						if (USWCollectableInstancedSMeshComponent* HSM_Col = Cast<USWCollectableInstancedSMeshComponent>(NHISM_w_Collision))
						{
							HSM_Col->Visual_Mesh_Owner = NHISM;
							HSM_Col->SpawnableID = Index;
							HSM_Col->BiomID = BiomIndex;

							if (bResourceCollectable)
							{
								HSM_Col->ResourceName = ResourceName;
								HSM_Col->bCollectable = true;
							}

							HSM_Col->Redirection = MakeShared<FSWRedirectionIndexes>();

						}

						NHISM_w_Collision->bHiddenInGame = true;

						NHISM_w_Collision->SetupAttachment(Owner->GetRootComponent());
						NHISM_w_Collision->RegisterComponent();
						NHISM_w_Collision->SetStaticMesh(FoliageType->GetStaticMesh());
						NHISM_w_Collision->SetRelativeLocation(FVector(0.f, 0.f, 0.f));

						NHISM->SetUsingAbsoluteLocation(true);
						NHISM->SetUsingAbsoluteRotation(true);

						NHISM_w_Collision->SetCastShadow(false /*CastShadows */);

						NHISM_w_Collision->InstanceStartCullDistance = 0;
						NHISM_w_Collision->InstanceEndCullDistance = 0;

						NHISM_w_Collision->bAffectDynamicIndirectLighting = false;//bAffectDynamicIndirectLighting;
						NHISM_w_Collision->bAffectDistanceFieldLighting = false;//bAffectDistanceFieldLighting;
						NHISM_w_Collision->bCastShadowAsTwoSided = false;//bCastShadowAsTwoSided;
						NHISM_w_Collision->bReceivesDecals = false;//bReceivesDecals;

						NHISM_w_Collision->SetCollisionObjectType(CollisionChannel);
						NHISM_w_Collision->SetCollisionResponseToChannels(CollisionProfile);
						NHISM_w_Collision->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);

						{
							NHISM_w_Collision->Mobility = EComponentMobility::Static;
						}

						HIM_Mesh_Collision_enabled.Add(NHISM_w_Collision);
						HIM_Mesh_Collision_TreeRebuild_Time.Add(0.f);

					}

				}
				FoliageVar++;
			}

#endif

			if (GrassType)
			{
				int32 GrassVar = 0;
				for (FGrassVariety& Grass : GrassType->GrassVarieties)
				{

					if (Grass.GrassMesh)
					{

						USWHISMComponent* NHISM = nullptr;
						bool FoliageComp = false;

						UClass* ComponentClass = USWHISMComponent::StaticClass();

						NHISM = NewObject<USWHISMComponent>(Owner, ComponentClass, NAME_None, RF_Transient);

						NHISM->bUseTranslatedInstanceSpace = false;
						NHISM->bAlwaysCreatePhysicsState = false;
						NHISM->ComponentTags = ComponentTags;

						NHISM->SetupAttachment(Owner->GetRootComponent());

						NHISM->RegisterComponent();
						NHISM->SetStaticMesh(Grass.GrassMesh);
						NHISM->SetRelativeLocation(FVector(0.f, 0.f, 0.f));

						NHISM->SetUsingAbsoluteLocation(true);
						NHISM->SetUsingAbsoluteRotation(true);

						NHISM->SetKeepInstanceBufferCPUCopy(bKeepInstanceBufferCPUCopy);

						if (UpdateSpawnSettings)
						{
							CastShadows = Grass.bCastDynamicShadow;

							bAffectDynamicIndirectLighting = false;
							bAffectDistanceFieldLighting = false;
							bCastShadowAsTwoSided = false;
							bReceivesDecals = Grass.bReceivesDecals;
							CollisionEnabled = false;

							AlignMaxAngle = Grass.AlignToSurface ? 90.f : 0.f;

						}

						CollisionEnabled = false;

						NHISM->SetCastShadow(CastShadows);

						NHISM->MinLOD = Grass.MinLOD;
						NHISM->bSelectable = false;
						NHISM->bHasPerInstanceHitProxies = false;
						NHISM->bDisableCollision = true;
						NHISM->SetCanEverAffectNavigation(false);

						NHISM->LightingChannels = Grass.LightingChannels;
						NHISM->bCastStaticShadow = false;
						NHISM->CastShadow = CastShadows;
						NHISM->bCastDynamicShadow = CastShadows;
						NHISM->OverrideMaterials = Grass.OverrideMaterials;

						NHISM->bEnableDensityScaling = GrassType->bEnableDensityScaling;

						NHISM->InstanceStartCullDistance = CullDistance.Min;
						NHISM->InstanceEndCullDistance = CullDistance.Max;


						NHISM->bAffectDynamicIndirectLighting = bAffectDynamicIndirectLighting;
						NHISM->bAffectDistanceFieldLighting = false;//bAffectDistanceFieldLighting;
						NHISM->bCastShadowAsTwoSided = bCastShadowAsTwoSided;
						NHISM->bReceivesDecals = bReceivesDecals;

						{
							// To guarantee consistency across platforms, we force the string to be lowercase and always treat it as an ANSI string.
							int32 FolSeed = FCrc::StrCrc32(StringCast<ANSICHAR>(*FString::Printf(TEXT("%s%s%d"), *GrassType->GetName().ToLower(), *NHISM->GetName().ToLower(), GrassVar)).Get());
							if (FolSeed == 0)
							{
								FolSeed++;
							}

							NHISM->InstancingRandomSeed = FolSeed;
							NHISM->Mobility = EComponentMobility::Static;
							NHISM->bEvaluateWorldPositionOffset = true;
							NHISM->WorldPositionOffsetDisableDistance = Grass.InstanceWorldPositionOffsetDisableDistance;

							NHISM->PrecachePSOs();
						}



						NHISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);

						HIM_Mesh.Add(NHISM);
						HIM_Mesh_TreeRebuild_Time.Add(0);

					}

					GrassVar++;
				}


			}

			int32 MeshVar = 0;
			for (UStaticMesh* Sm : Mesh)
			{

				if (Sm)
				{

					USWHISMComponent* NHISM = nullptr;
					bool FoliageComp = false;

					UClass* ComponentClass = *FoliageComponent;
					if (ComponentClass == nullptr || !ComponentClass->IsChildOf(USWHISMComponent::StaticClass()))
					{
						ComponentClass = USWHISMComponent::StaticClass();
					}

					NHISM = NewObject<USWHISMComponent>(Owner, ComponentClass, NAME_None, RF_Transient);

					NHISM->bHasPerInstanceHitProxies = false;
					NHISM->SetCanEverAffectNavigation(false);
					NHISM->bUseTranslatedInstanceSpace = false;
					NHISM->bAlwaysCreatePhysicsState = false;

					NHISM->ComponentTags = ComponentTags;

					if (UFoliageInstancedStaticMeshComponent* FComp = Cast<UFoliageInstancedStaticMeshComponent>(NHISM))
						FoliageComp = true;

					NHISM->SetupAttachment(Owner->GetRootComponent());
					NHISM->RegisterComponent();
					NHISM->SetStaticMesh(Sm);
					NHISM->SetRelativeLocation(FVector(0.f, 0.f, 0.f));

					NHISM->SetUsingAbsoluteLocation(true);
					NHISM->SetUsingAbsoluteRotation(true);

					NHISM->SetKeepInstanceBufferCPUCopy(bKeepInstanceBufferCPUCopy);

					UpdateStaticMeshHierachyComponentSettings(NHISM, CullDistance);


					NHISM->CastShadow = CastShadows;
					NHISM->bCastDynamicShadow = CastShadows;
					NHISM->bCastStaticShadow = false;

					NHISM->InstanceStartCullDistance = CullDistance.Min;
					NHISM->InstanceEndCullDistance = CullDistance.Max;

					NHISM->bAffectDynamicIndirectLighting = bAffectDynamicIndirectLighting;
					NHISM->bAffectDistanceFieldLighting = bAffectDistanceFieldLighting;
					NHISM->bCastShadowAsTwoSided = bCastShadowAsTwoSided;
					NHISM->bReceivesDecals = bReceivesDecals;

					{
						// To guarantee consistency across platforms, we force the string to be lowercase and always treat it as an ANSI string.
						int32 FolSeed = FCrc::StrCrc32(StringCast<ANSICHAR>(*FString::Printf(TEXT("%s%s%d"), *Sm->GetName().ToLower(), *NHISM->GetName().ToLower(), MeshVar)).Get());
						if (FolSeed == 0)
						{
							FolSeed++;
						}
						NHISM->InstancingRandomSeed = FolSeed;
						NHISM->Mobility = EComponentMobility::Static;
						NHISM->PrecachePSOs();
					}


					if (NumberGridRings == 0 || !CollisionOnlyAtProximity && (NumberGridRings > 0))
						NHISM->SetCollisionEnabled(CollisionEnabled ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
					else
						NHISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);

					HIM_Mesh.Add(NHISM);
					HIM_Mesh_TreeRebuild_Time.Add(0);

					if (CollisionEnabled && CollisionOnlyAtProximity && NumberGridRings > 0)
					{
						USWHISMComponent* NHISM_w_Collision = nullptr;

						NHISM_w_Collision = NewObject<USWHISMComponent>(Owner, USWCollectableInstancedSMeshComponent::StaticClass(), NAME_None, RF_Transient);

						NHISM_w_Collision->SetCanEverAffectNavigation(true);
						NHISM_w_Collision->bUseTranslatedInstanceSpace = false;
						NHISM_w_Collision->ComponentTags = ComponentTags;

						Owner->CollisionToSpawnable.Add(NHISM_w_Collision, this);
						if (USWCollectableInstancedSMeshComponent* HSM_Col = Cast<USWCollectableInstancedSMeshComponent>(NHISM_w_Collision))
						{
							HSM_Col->Visual_Mesh_Owner = NHISM;
							HSM_Col->SpawnableID = Index;
							HSM_Col->BiomID = BiomIndex;

							if (bResourceCollectable)
							{
								HSM_Col->ResourceName = ResourceName;
								HSM_Col->bCollectable = true;
							}

							HSM_Col->Redirection = MakeShared<FSWRedirectionIndexes>();

						}

						NHISM_w_Collision->bHiddenInGame = true;

						NHISM_w_Collision->SetupAttachment(Owner->GetRootComponent());
						NHISM_w_Collision->RegisterComponent();
						NHISM_w_Collision->SetStaticMesh(Sm);
						NHISM_w_Collision->SetRelativeLocation(FVector(0.f, 0.f, 0.f));

						NHISM->SetUsingAbsoluteLocation(true);
						NHISM->SetUsingAbsoluteRotation(true);

						NHISM_w_Collision->SetCastShadow(false /*CastShadows */);

						NHISM_w_Collision->InstanceStartCullDistance = 0;
						NHISM_w_Collision->InstanceEndCullDistance = 0;

						NHISM_w_Collision->bAffectDynamicIndirectLighting = false;//bAffectDynamicIndirectLighting;
						NHISM_w_Collision->bAffectDistanceFieldLighting = false;//bAffectDistanceFieldLighting;
						NHISM_w_Collision->bCastShadowAsTwoSided = false;//bCastShadowAsTwoSided;
						NHISM_w_Collision->bReceivesDecals = false;//bReceivesDecals;

						NHISM_w_Collision->SetCollisionObjectType(CollisionChannel);
						NHISM_w_Collision->SetCollisionResponseToChannels(CollisionProfile);
						NHISM_w_Collision->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);

						{
							NHISM_w_Collision->Mobility = EComponentMobility::Static;
						}
						HIM_Mesh_Collision_enabled.Add(NHISM_w_Collision);
						HIM_Mesh_Collision_TreeRebuild_Time.Add(0.f);
					}
				}
				MeshVar++;
			}


		}
		else
			for (TSubclassOf<AActor>& A : Actors)
			{
				if ((*A) != nullptr)
				{
					Spawned_Actors.Add(FSpawnedActorList());
					Actors_Validated.Add(A);
				}

			}
		UpdateSpawnSettings = false;

	}
}


bool AShaderWorldActor::UpdateSpawnableNew(FSWBiom& Biom, int indice, int Biomindice, bool MustBeInFrustum, int MaxRing)
{
	FSpawnableMesh& Spawn = Biom.Spawnables[indice];

	if (!Spawn.bHasValidSpawningData())
		return true;

	if (!Spawn.Owner)
		Spawn.InitiateNew(this, indice, Biomindice);

	if (!Spawn.ProcessedRead.IsValid() || !Spawn.ProcessedRead->bProcessingCompleted || Spawn.SpawnableWorkQueue.Num() > 0)
		return true;

	bool Segmented = UseSegmented();

	TSet<FIntVector>& All_Cams = Spawn.All_Cams;
	TSet<FIntVector>& Cam_Proximity = Spawn.Cam_Proximity;
	if (((SpawnablesUpdateTime - Spawn.CamerasUpdateTime) > 0.1) || (Spawn.CamerasUpdateTime > SpawnablesUpdateTime))
		SetupCameraForSpawnable(Spawn, Cam_Proximity, All_Cams);

	if (Cam_Proximity.Num() <= 0 || All_Cams.Num() <= 0)
	{
		return true;
	}

	//Only necessary to do this once during the initial InFrustum pass
	if (MustBeInFrustum && (MaxRing < 0) && !Spawn.CollisionEnabled || MaxRing >= 0)
	{
		ReleaseCollisionBeyondRange(Spawn, Cam_Proximity);		

		ReleaseSpawnElemBeyondRange(Biom, Spawn, Cam_Proximity, All_Cams, Segmented, MaxRing);
	}

	bool InterruptUpdate = false;

	if (Spawn.IndexOfClipMapForCompute >= 0 && Spawn.IndexOfClipMapForCompute < GetMeshNum())
	{
		FClipMapMeshElement& Elem = GetMesh(Spawn.IndexOfClipMapForCompute);

		if (!Elem.IsSectionVisible(0, Segmented) && !Elem.IsSectionVisible(1, Segmented))
			return true;
	}

	int CurrentRingScope = MaxRing >= 0 ? FMath::Min(MaxRing, Spawn.NumberGridRings) : Spawn.NumberGridRings;

	FIntVector LocalCam(0);
	for (auto& C : All_Cams)
	{
		LocalCam = C;
		break;
	}

	for (int r = 0; r <= CurrentRingScope; r++)
	{

		for (int i = -r; i <= r; i++)
		{
			if (InterruptUpdate)
				break;

			for (int j = -r; j <= r; j++)
			{
				if (abs(j) != r && abs(i) != r)
					continue;

				FIntVector LocMeshInt = LocalCam + FIntVector(i, j, 0);

				FIntVector MeshLoc = LocMeshInt * Spawn.GridSizeMeters * 100.0 + FIntVector(0.f, 0.f, 1) * HeightOnStart - GetWorld()->OriginLocation;

				if (!Spawn.SpawnablesLayout.Contains(LocMeshInt))
				{
					if (!MustBeInFrustum || MustBeInFrustum && (ViewFrustum.Planes.Num() <= 0 || ViewFrustum.IntersectBox(FVector(MeshLoc), Spawn.ExtentOfMeshElement)))
					{
						if (CanUpdateSpawnables())
						{
							AssignSpawnableMeshElement(i, j, Segmented, Biom, Spawn, LocMeshInt, Cam_Proximity);
						}
						else
						{
							InterruptUpdate = true;
							break;
						}
					}
				}
			}
		}
	}

	if (InterruptUpdate)
	{
		return false;
	}
	return true;

}

void AShaderWorldActor::UpdateSpawnablesNew()
{
	SpawnablesUpdateTime = FPlatformTime::Seconds();

	
	for(int32 BiomLocalID = 0; BiomLocalID < Bioms.Num(); BiomLocalID++)
	{
		FSWBiom& elB = Bioms[BiomLocalID];

		if (elB.SortedSpawnables.Num() == 0 && elB.Spawnables.Num() > 0 || elB.SortedSpawnables.Num() != elB.Spawnables.Num())
			SortSpawnabledBySurface(elB);

		bool Interrupted = false;

		//Update Spawnables with collisions around players camera (important for fast traversal - we don't want players to fly through a tree with no collisions)

		for (int i = elB.SortedSpawnables_Collision.Num() - 1; i >= 0; i--)
		{
			int& indice = elB.SortedSpawnables_Collision[i];
			if (!UpdateSpawnableNew(elB, indice, BiomLocalID, false, 1))
			{
				Interrupted = true;
				break;
			}
		}

		//Update All spawnables in Frustum - Sorted by view distance (trees far away will be computed last)

		for (int i = elB.SortedSpawnables.Num() - 1; i >= 0; i--)
		{
			int& indice = elB.SortedSpawnables[i];
			if (!UpdateSpawnableNew(elB, indice, BiomLocalID, true))
			{
				Interrupted = true;
				break;
			}
		}

		// If we have remaining draw call and time budget, update spawnables out of frustum	
		if (!Interrupted)
		{
			for (int i = elB.SortedSpawnables.Num() - 1; i >= 0; i--)
			{
				int& indice = elB.SortedSpawnables[i];
				if (!UpdateSpawnableNew(elB, indice, BiomLocalID, false))
					break;
			}
		}
	}
}

void AShaderWorldActor::UpdateSpawnables()
{
	SW_FCT_CYCLE()
	/*
	if (BrushManagerRedrawScopesSpawnables.Num() > 0)
		BrushManagerRedrawScopesSpawnables.Empty();

	UpdateSpawnablesNew();
	return;
	*/

	
	/*
	 * Brush impact on vegetation disabled for now - please use rebuild vegetation only
	 */
	if(BrushManagerRedrawScopesSpawnables.Num()>0)
	{
		bool Segmented = UseSegmented();

		for (FSWBiom& elB : Bioms)
		{
			for (FSpawnableMesh& Spawn : elB.Spawnables)
			{				

				if (Spawn.IndexOfClipMapForCompute >= 0 && Spawn.IndexOfClipMapForCompute < GetMeshNum())
				{
					FClipMapMeshElement& Elem = GetMesh(Spawn.IndexOfClipMapForCompute);

					if (Elem.IsSectionVisible(0, Segmented) || Elem.IsSectionVisible(1, Segmented))
					{

						TMap<FIntVector, FVector> BrushRedraws;
						for (const FBox2d& B : BrushManagerRedrawScopesSpawnables)
						{
							const int32 MinBoxX_local = FMath::FloorToInt((B.Min.X + GetWorld()->OriginLocation.X) / (Spawn.GridSizeMeters * 100.0) + 0.45 * 0.0);
							const int32 MinBoxY_local = FMath::FloorToInt((B.Min.Y + GetWorld()->OriginLocation.Y) / (Spawn.GridSizeMeters * 100.0) + 0.45 * 0.0);

							const int32 MaxBoxX_local = FMath::FloorToInt((B.Max.X + GetWorld()->OriginLocation.X) / (Spawn.GridSizeMeters * 100.0) + 0.55 * 2.0);
							const int32 MaxBoxY_local = FMath::FloorToInt((B.Max.Y + GetWorld()->OriginLocation.Y) / (Spawn.GridSizeMeters * 100.0) + 0.55 * 2.0);

							for (int32 X_iter = MinBoxX_local; X_iter <= MaxBoxX_local; X_iter++)
							{
								for (int32 Y_iter = MinBoxY_local; Y_iter <= MaxBoxY_local; Y_iter++)
								{
									if (Spawn.SpawnablesLayout.Contains(FIntVector(X_iter, Y_iter, 0)))
									{
										BrushRedraws.Add(FIntVector(X_iter, Y_iter, 0));
									}									
								}
							}
						}

						for(auto& Element : BrushRedraws)
						{
							//if (Spawn.SpawnablesLayout.Contains(Element.Key))
							{
								int32 IndiceOfSpawnableElement = *(Spawn.SpawnablesLayout.Find(Element.Key));
								FSpawnableMeshElement& MeshEl = Spawn.SpawnablesElem[IndiceOfSpawnableElement];

								if (Segmented)
								{
									SegmentedUpdateProcessedButSpawnablesLeft = true;
									/*
									if(Spawn.SegmentedOnly_ElementToUpdateData.Find(IndiceOfSpawnableElement))
									{
										SW_LOG("Spawnable alreayd pending compute %d", IndiceOfSpawnableElement)
									}
									else*/
									{
										
										Spawn.SegmentedOnly_ElementToUpdateData.AddUnique(IndiceOfSpawnableElement);
									}
								
								}
								else
									Spawn.UpdateSpawnableData(elB, MeshEl);

								if (Spawn.SpawnType != ESpawnableType::Actor)
								{
									if (/*Spawn.CollisionEnabled && Spawn.CollisionOnlyAtProximity && (Spawn.NumberGridRings > 0) &&*/ (MeshEl.Collision_Mesh_ID >= 0))
									{
										Spawn.SpawnablesElemNeedCollisionUpdate.AddUnique(IndiceOfSpawnableElement);
									}
										
								}
							}							
						}
					}
				}
			}
		}

		BrushManagerRedrawScopesSpawnables.Empty();
		return;
	}
		

	SpawnablesUpdateTime = FPlatformTime::Seconds();

	int32 BiomLocalID = 0;

	for (FSWBiom& elB : Bioms)
	{

		if(elB.SortedSpawnables.Num()==0 && elB.Spawnables.Num()>0 || elB.SortedSpawnables.Num() != elB.Spawnables.Num())
			SortSpawnabledBySurface(elB);

		bool Interrupted = false;		
		
		for (int i = elB.SortedSpawnables_Collision.Num()-1; i >= 0; i--)
		{
			int& indice = elB.SortedSpawnables_Collision[i];
			if (!UpdateSpawnable(elB,indice, BiomLocalID, false,1))
			{
				Interrupted = true;
				break;
			}
		}

		for (int i = elB.SortedSpawnables.Num()-1; i>=0;i--)
		{
			int& indice = elB.SortedSpawnables[i];
			if(!UpdateSpawnable(elB,indice, BiomLocalID,true))
			{
				Interrupted=true;
				break;
			}
		}

		// If we have remaining draw call within spawnable budget, update spawnables out of frustum	
		if(!Interrupted)
		{	
			for (int i = elB.SortedSpawnables.Num()-1; i>=0;i--)
			{
				int& indice = elB.SortedSpawnables[i];
				if (!UpdateSpawnable(elB,indice, BiomLocalID, false))
					break;			
			}	
		}

		BiomLocalID++;
	}

}
#if 1
FSpawnableMeshProximityCollisionElement& FSpawnableMesh::GetASpawnableCollisionElem()
{
	if (AvailableSpawnablesCollisionElem.Num() > 0)
	{
		FSpawnableMeshProximityCollisionElement& Elem = SpawnablesCollisionElem[AvailableSpawnablesCollisionElem[AvailableSpawnablesCollisionElem.Num() - 1]];
		UsedSpawnablesCollisionElem.Add(Elem.ID);
		AvailableSpawnablesCollisionElem.RemoveAt(AvailableSpawnablesCollisionElem.Num() - 1);
		return Elem;
	}

	FSpawnableMeshProximityCollisionElement NewElem;
	NewElem.InstancesIndexes = nullptr;
	//NewElem.InstancesIndexes.Empty();
	NewElem.InstanceOffset.Empty();
	NewElem.ID = SpawnablesCollisionElem.Num();
	NewElem.OffsetOfSegmentedUpdate.Empty();

	UsedSpawnablesCollisionElem.Add(NewElem.ID);
	SpawnablesCollisionElem.Add(NewElem);

	return SpawnablesCollisionElem[SpawnablesCollisionElem.Num() - 1];
}

FSpawnableMeshElement& FSpawnableMesh::GetASpawnableElem()
{
	if (AvailableSpawnablesElem.Num() > 0)
	{
		FSpawnableMeshElement& Elem = SpawnablesElem[AvailableSpawnablesElem[AvailableSpawnablesElem.Num() - 1]];
		UsedSpawnablesElem.Add(Elem.ID);
		AvailableSpawnablesElem.RemoveAt(AvailableSpawnablesElem.Num() - 1);
		Elem.OffsetOfSegmentedUpdate.Empty();
		return Elem;
	}

	//if the spawnable is not defined yet, early out
	if( !bHasValidSpawningData())
	{
		FSpawnableMeshElement NewElem;
		NewElem.InstancesIndexes = nullptr;
		//NewElem.InstancesIndexes.Empty();
		NewElem.InstanceOffset.Empty();
		NewElem.OffsetOfSegmentedUpdate.Empty();
		SpawnablesElem.Add(NewElem);
		return SpawnablesElem[SpawnablesElem.Num() - 1];
	}
		

	if (!Owner)
	{
		UE_LOG(LogTemp,Warning,TEXT("ERROR : FSpawnableMesh has no owner"));
		FSpawnableMeshElement NewElem;
		NewElem.InstancesIndexes = nullptr;
		//NewElem.InstancesIndexes.Empty();
		NewElem.InstanceOffset.Empty();
		NewElem.OffsetOfSegmentedUpdate.Empty();
		SpawnablesElem.Add(NewElem);
		return SpawnablesElem[SpawnablesElem.Num() - 1];	
	}


	FSpawnableMeshElement NewElem;
	NewElem.InstancesIndexes = nullptr;
	//NewElem.InstancesIndexes.Empty();
	NewElem.InstanceOffset.Empty();
	NewElem.OffsetOfSegmentedUpdate.Empty();
	NewElem.ID = SpawnablesElem.Num();
	NewElem.LOD_usedLastUpdate = -1;
	NewElem.InstancesT = MakeShared<FSWSpawnableTransforms, ESPMode::ThreadSafe>();
	NewElem.InstancesT->Transforms.Empty();
	
	UWorld* World = Owner->GetWorld();

	uint32 SizeT = (uint32)RT_Dim;


	Owner->RendertargetMemoryBudgetMB += 4 * (SizeT * SizeT) / 1000000.0f;
	
	SW_RT(NewElem.SpawnDensity, World, SizeT, TF_Nearest, RTF_RGBA8)

	Owner->RendertargetMemoryBudgetMB += 4 * (SizeT * 2 * SizeT * 2) / 1000000.0f;

	SW_RT(NewElem.SpawnTransforms, World, SizeT * 2, TF_Nearest, RTF_RGBA8)

	UsedSpawnablesElem.Add(NewElem.ID);
	SpawnablesElem.Add(NewElem);

	return SpawnablesElem[SpawnablesElem.Num() - 1];	
}

void FSpawnableMesh::ReleaseSpawnableElem(int ID)
{	
	//AvailableSpawnablesElem.Add(ID);

	check(ID < SpawnablesElem.Num())

	AvailableSpawnablesElem.Add(ID);
	UsedSpawnablesElem.Remove(ID);
	SpawnablesLayout.Remove(SpawnablesElem[ID].Location);

}

void ReadPixelsFromRT_Spawn(FSpawnableMeshElement& Mesh)
{

	UTextureRenderTarget2D* T_rt = Mesh.SpawnTransforms;

	ENQUEUE_RENDER_COMMAND(ReadGeoClipMapRTCmd)(
		[T_rt, TransformData = Mesh.SpawnData, Completion = Mesh.ReadBackCompletion](FRHICommandListImmediate& RHICmdList)
	{
		check(IsInRenderingThread());

		if(TransformData.IsValid() && T_rt->GetResource())
		{
			FRDGBuilder GraphBuilder(RHICmdList);

			TSharedPtr<FRHIGPUTextureReadback> ReadBackStaging = MakeShared<FRHIGPUTextureReadback>(TEXT("SWGPUTextureReadback"));

			FRDGTextureRef RDGSourceTexture = RegisterExternalTexture(GraphBuilder, T_rt->GetResource()->TextureRHI, TEXT("SWSourceTextureToReadbackTexture"));

			AddEnqueueCopyPass(GraphBuilder, ReadBackStaging.Get(), RDGSourceTexture);

			GraphBuilder.Execute();

			GSWSimpleReadbackManager.AddPendingReadBack(GPixelFormats[RDGSourceTexture->Desc.Format].BlockBytes, RDGSourceTexture->Desc.Extent.X, RDGSourceTexture->Desc.Extent.Y, ReadBackStaging, const_cast<TSharedPtr<FSWColorRead, ESPMode::ThreadSafe>&>(TransformData), const_cast<TSharedPtr < FThreadSafeBool, ESPMode::ThreadSafe>&>(Completion));
		}
		
	});
}

void FSpawnableMesh::UpdateSpawnableData(FSWBiom& Biom,FSpawnableMeshElement& MeshElem)
{
	SW_FCT_CYCLE()

	/*
	if(!Owner->SpawnablesMat && !Biom.BiomDensitySpawner && !CustomSpawnablesMat)
	{
#if SWDEBUG
		SW_LOG("This Spawnable has no Spawn-Probability/Density Generator Material: Shader World: %s Spawnable %s", *Owner->GetName(), *GetSpawnableName())
#endif
	}*/

	if(!Owner->SpawnablesMat)
	{
#if SWDEBUG
		SW_LOG("This ShaderWorld has no default Spawnable Probability/Density Generator Material: Shader World: %s", *Owner->GetName())
#endif
	}
	else if (Owner && Owner->GetMeshNum()>0 && Owner->GetMesh(0).HeightMap)
	{
		UWorld* World = Owner->GetWorld();

		TSharedPtr <FSWSpawnableRequirements, ESPMode::ThreadSafe> SpawnConfig = MakeShared<FSWSpawnableRequirements, ESPMode::ThreadSafe>
		(
			nullptr,
			nullptr,
			Owner->TileableNoiseTexture,
			Owner->N,
			AlignToTerrainSlope?1:0,
			YawOffsetAlignToTerrainSlope,
			(Owner->RendererAPI == EGeoRenderingAPI::OpenGL) ? 0.f : 1.f,
			1.f,
			AlignMaxAngle,
			AltitudeRange,
			VerticalOffsetRange,
			ScaleRange,
			GroundSlopeAngle,
			GridSizeMeters * 100.f * (RT_Dim <= 1 ? 1 : RT_Dim / (RT_Dim - 1)),
			RT_Dim,
			FVector(0),
			FVector(0),
			MeshElem.SpawnDensity,
			MeshElem.SpawnTransforms
		);

		//OPTION A : Compute collision from GPU readback

		UMaterialInstanceDynamic* DynSpawnMat = MeshElem.ComputeSpawnTransformDyn ? MeshElem.ComputeSpawnTransformDyn : (UMaterialInstanceDynamic::Create(CustomSpawnablesMat.Get() ? CustomSpawnablesMat.Get() : (Biom.BiomDensitySpawner.Get() ? Biom.BiomDensitySpawner.Get() : Owner->SpawnablesMat), Owner));
		
		bool Segmented = Owner->UseSegmented();

		 int LOD_Candidate = -1;

		 FIntVector MeshLoc = MeshElem.Location*GridSizeMeters*100.f + FIntVector(0.f, 0.f, 1) * Owner->HeightOnStart - Owner->GetWorld()->OriginLocation;
		
		
		//Prevent recompute by reading HeightMap and NormalMap
		//would need a different material to switch
		if(IndexOfClipMapForCompute>=0 && IndexOfClipMapForCompute<Owner->GetMeshNum())
		{		
			LOD_Candidate = Owner->FindBestCandidate_SpawnableElem(GridSizeMeters,IndexOfClipMapForCompute,MeshElem,Segmented);		

			FClipMapMeshElement& Elem = Owner->GetMesh(IndexOfClipMapForCompute);

			bool JustCreated=false;
			if (!MeshElem.ComputeSpawnTransformDyn)
			{
				MeshElem.ComputeSpawnTransformDyn = DynSpawnMat;
				MeshElem.LOD_usedLastUpdate=-1;

				if(Owner->RendererAPI==EGeoRenderingAPI::OpenGL)
				{
					DynSpawnMat->SetScalarParameterValue("DX_Status", 0.f);					
				}
					

				DynSpawnMat->SetScalarParameterValue("N", Owner->N);

				if (LOD_Candidate >= 0)
				{				
					//UE_LOG(LogTemp,Warning,TEXT("LOD_Candidate %d"),LOD_Candidate);
					FClipMapMeshElement& Elem_Local = Owner->GetMesh(LOD_Candidate);
					
					MeshElem.LOD_usedLastUpdate = LOD_Candidate;

					MeshElem.LocalGridScaling_LatestC = Elem_Local.GridSpacing;
					MeshElem.HeightMap_LatestC = Elem_Local.HeightMap;
					MeshElem.NormalMap_LatestC = Elem_Local.NormalMap;

					DynSpawnMat->SetScalarParameterValue("PatchFullSize", (SpawnConfig->N - 1) * Elem_Local.GridSpacing);

					DynSpawnMat->SetScalarParameterValue("LocalGridScaling", Elem_Local.GridSpacing);
					DynSpawnMat->SetTextureParameterValue("HeightMap", Elem_Local.HeightMap);
					DynSpawnMat->SetTextureParameterValue("NormalMap", Elem_Local.NormalMap);

					for (int i = 0; i < Elem_Local.LandLayers.Num(); i++)
					{
						DynSpawnMat->SetTextureParameterValue(Elem_Local.LandLayers_names[i], Elem_Local.LandLayers[i]);
					}
					
				}
				else
				{					

					MeshElem.LocalGridScaling_LatestC = Elem.GridSpacing;
					MeshElem.HeightMap_LatestC = Elem.HeightMap;
					MeshElem.NormalMap_LatestC = Elem.NormalMap;

					DynSpawnMat->SetScalarParameterValue("PatchFullSize", (SpawnConfig->N - 1) * Elem.GridSpacing);

					DynSpawnMat->SetScalarParameterValue("LocalGridScaling", Elem.GridSpacing);
					DynSpawnMat->SetTextureParameterValue("HeightMap", Elem.HeightMap);
					DynSpawnMat->SetTextureParameterValue("NormalMap", Elem.NormalMap);
					for (int i = 0; i < Elem.LandLayers.Num(); i++)
					{
						DynSpawnMat->SetTextureParameterValue(Elem.LandLayers_names[i], Elem.LandLayers[i]);
					}
				}

				/*
				DynSpawnMat->SetScalarParameterValue("AlignMaxAngle", AlignMaxAngle);
				DynSpawnMat->SetScalarParameterValue("MinSpawnHeight", AltitudeRange.Min);
				DynSpawnMat->SetScalarParameterValue("MaxSpawnHeight", AltitudeRange.Max);
				DynSpawnMat->SetScalarParameterValue("MinVerticalOffset", VerticalOffsetRange.Min);
				DynSpawnMat->SetScalarParameterValue("MaxVerticalOffset", VerticalOffsetRange.Max);				
				DynSpawnMat->SetScalarParameterValue("MinScale", ScaleRange.Min);
				DynSpawnMat->SetScalarParameterValue("MaxScale", ScaleRange.Max);
				DynSpawnMat->SetScalarParameterValue("MinGroundSlope", GroundSlopeAngle.Min);
				DynSpawnMat->SetScalarParameterValue("MaxGroundSlope", GroundSlopeAngle.Max);*/
				DynSpawnMat->SetScalarParameterValue("MeshScale", GridSizeMeters*100.f * (RT_Dim <= 1 ? 1 : RT_Dim / (RT_Dim - 1)));

				DynSpawnMat->SetScalarParameterValue("FoliageSeed", FoliageIndexSeed);

				DynSpawnMat->SetScalarParameterValue("RT_Dim", RT_Dim);

				JustCreated=true;
			}


			// Should always be the case as it was filtered before reaching this point
			if(Elem.IsSectionVisible(0)||Elem.IsSectionVisible(1))
			{
				
				
				if(LOD_Candidate>=0)
				{					

					FClipMapMeshElement& Elem_Local = Owner->GetMesh(LOD_Candidate);

					if (LOD_Candidate != MeshElem.LOD_usedLastUpdate)
					{
						
						// I need to update but my LOD_usedLastUpdate cant be used anymore
						MeshElem.LOD_usedLastUpdate = LOD_Candidate;

						MeshElem.LocalGridScaling_LatestC = Elem_Local.GridSpacing;
						MeshElem.HeightMap_LatestC = Elem_Local.HeightMap;
						MeshElem.NormalMap_LatestC = Elem_Local.NormalMap;

						DynSpawnMat->SetScalarParameterValue("PatchFullSize", (SpawnConfig->N - 1) * Elem_Local.GridSpacing);

						DynSpawnMat->SetScalarParameterValue("LocalGridScaling", Elem_Local.GridSpacing);

						DynSpawnMat->SetTextureParameterValue("HeightMap", Elem_Local.HeightMap);
						DynSpawnMat->SetTextureParameterValue("NormalMap", Elem_Local.NormalMap);					

						for (int i = 0; i < Elem_Local.LandLayers.Num(); i++)
						{
							DynSpawnMat->SetTextureParameterValue(Elem_Local.LandLayers_names[i], Elem_Local.LandLayers[i]);
						}

						
					}	
					else
					{					
					}

					MeshElem.RingLocation_LatestC = FVector(Elem_Local.Location);
					DynSpawnMat->SetVectorParameterValue("PatchLocation", FVector(Elem_Local.Location));
	
			}
			else
				{
					if(MeshElem.LOD_usedLastUpdate>0)
					{

						MeshElem.LOD_usedLastUpdate=-1;

						MeshElem.LocalGridScaling_LatestC = Elem.GridSpacing;
						MeshElem.HeightMap_LatestC = Elem.HeightMap;
						MeshElem.NormalMap_LatestC = Elem.NormalMap;

						DynSpawnMat->SetScalarParameterValue("PatchFullSize", (SpawnConfig->N - 1) * Elem.GridSpacing);

						DynSpawnMat->SetScalarParameterValue("LocalGridScaling", Elem.GridSpacing);
						DynSpawnMat->SetTextureParameterValue("HeightMap", Elem.HeightMap);
						DynSpawnMat->SetTextureParameterValue("NormalMap", Elem.NormalMap);						

						for (int i = 0; i < Elem.LandLayers.Num(); i++)
						{
							DynSpawnMat->SetTextureParameterValue(Elem.LandLayers_names[i], Elem.LandLayers[i]);
						}
					}
					else
					{
					}

					MeshElem.RingLocation_LatestC = FVector(Elem.Location);
					DynSpawnMat->SetVectorParameterValue("PatchLocation", FVector(Elem.Location));
				}
				///////////////////////////////
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("Elem.IsSectionVisible(0)||Elem.IsSectionVisible(1)"));
				return;

			}
		}
		else
		{

		}

		MeshElem.MeshLocation_latestC = FVector(MeshLoc + Owner->GetWorld()->OriginLocation);

		SpawnConfig->Heightmap = MeshElem.HeightMap_LatestC;
		SpawnConfig->Normalmap = MeshElem.NormalMap_LatestC;

		SpawnConfig->LocalGridScaling = MeshElem.LocalGridScaling_LatestC;
		SpawnConfig->MeshLocation = MeshElem.MeshLocation_latestC;
		SpawnConfig->RingLocation = MeshElem.RingLocation_LatestC;

		
		DynSpawnMat->SetVectorParameterValue("MeshLocation", MeshElem.MeshLocation_latestC);
		/*
		DynSpawnMat->SetScalarParameterValue("OutputRotationScale", 0.f);		
		DynSpawnMat->SetScalarParameterValue("OutputScale", 0.f);
		DynSpawnMat->SetScalarParameterValue("OutputDensity", 0.f);
		*/
		//
		//UKismetRenderingLibrary::ClearRenderTarget2D(Owner, MeshElem.SpawnDensity, FLinearColor::Black);
		//UKismetRenderingLibrary::ClearRenderTarget2D(Owner, MeshElem.SpawnTransforms, FLinearColor::Black);
		//

		{
			//DynSpawnMat->SetScalarParameterValue("OutputDensity", 1.f);
			//New Workflow : draw density
		
			UKismetRenderingLibrary::DrawMaterialToRenderTarget(Owner, MeshElem.SpawnDensity, DynSpawnMat);
			//if (Owner->SWorldSubsystem)
			//	Owner->SWorldSubsystem->DrawMaterialToRT(Owner, DynSpawnMat, MeshElem.SpawnDensity);


			MeshElem.SpawnData = MakeShared<FSWColorRead, ESPMode::ThreadSafe>();
			MeshElem.SpawnData->ReadData.Empty();
			MeshElem.SpawnData->ReadData.SetNum(RT_Dim * RT_Dim * 4);

			MeshElem.ReadBackCompletion = MakeShared<FThreadSafeBool, ESPMode::ThreadSafe>();
			MeshElem.ReadBackCompletion->AtomicSet(false);

			if (USWorldSubsystem* ShaderWorldSubsystem = Owner->SWorldSubsystem? Owner->SWorldSubsystem: Owner->GetWorld()->GetSubsystem<USWorldSubsystem>())
			{
				ShaderWorldSubsystem->ComputeSpawnables(SpawnConfig);				
			}				
			else
			{
#if SWDEBUG
				if (GEngine)
					GEngine->AddOnScreenDebugMessage(-1, 15.0f, FColor::Yellow, "Failed to access ShaderWorldSubsystem");
#endif
			}
		}
		

		ReadPixelsFromRT_Spawn(MeshElem);
		SpawnablesElemReadToProcess.Add(MeshElem.ID);


		return;
	}

	
}

void FSpawnableMesh::UpdateStaticMeshHierachyComponentSettings(UHierarchicalInstancedStaticMeshComponent* Component,FInt32Interval CullDist)
{
	if (Component)
	{
		bool bNeedsMarkRenderStateDirty = false;
		bool bNeedsInvalidateLightingCache = false;

		
		if (Component->CastShadow != true)
		{
			Component->CastShadow = true;
			bNeedsMarkRenderStateDirty = true;
			bNeedsInvalidateLightingCache = true;
		}
		if (Component->bCastDynamicShadow != true)
		{
			Component->bCastDynamicShadow = true;
			bNeedsMarkRenderStateDirty = true;
			bNeedsInvalidateLightingCache = true;
		}
		if (Component->bCastStaticShadow != false)
		{
			Component->bCastStaticShadow = false;
			bNeedsMarkRenderStateDirty = true;
			bNeedsInvalidateLightingCache = true;
		}
		if (Component->TranslucencySortPriority != 0)
		{
			Component->TranslucencySortPriority = 0;
			bNeedsMarkRenderStateDirty = true;
		}
		if (Component->bAffectDynamicIndirectLighting != false)
		{
			Component->bAffectDynamicIndirectLighting = false;
			bNeedsMarkRenderStateDirty = true;
			bNeedsInvalidateLightingCache = true;
		}
		if (Component->bAffectDistanceFieldLighting != false)
		{
			Component->bAffectDistanceFieldLighting = false;
			bNeedsMarkRenderStateDirty = true;
			bNeedsInvalidateLightingCache = true;
		}
		if (Component->bCastShadowAsTwoSided != false)
		{
			Component->bCastShadowAsTwoSided = false;
			bNeedsMarkRenderStateDirty = true;
			bNeedsInvalidateLightingCache = true;
		}
		if (Component->bReceivesDecals != false)
		{
			Component->bReceivesDecals = false;
			bNeedsMarkRenderStateDirty = true;
			bNeedsInvalidateLightingCache = true;
		}
		if (Component->bUseAsOccluder != false)
		{
			Component->bUseAsOccluder = false;
			bNeedsMarkRenderStateDirty = true;
		}

		if (Component->bEnableDensityScaling != false)
		{
			Component->bEnableDensityScaling = false;

			Component->UpdateDensityScaling();

			bNeedsMarkRenderStateDirty = true;
		}

	}
}
void FSpawnableMesh::UpdateComponentSettings(UHierarchicalInstancedStaticMeshComponent* Component, const UFoliageType_InstancedStaticMesh* InSettings)
{
	if (Component)
	{
		bool bNeedsMarkRenderStateDirty = false;
		//bool bNeedsInvalidateLightingCache = false;

		const UFoliageType_InstancedStaticMesh* FoliageType = InSettings;
#if WITH_EDITORONLY_DATA
		if (InSettings->GetClass()->ClassGeneratedBy)
		{
			// If we're updating settings for a BP foliage type, use the CDO
			FoliageType = InSettings->GetClass()->GetDefaultObject<UFoliageType_InstancedStaticMesh>();
		}
#endif


		if (Component->CastShadow != FoliageType->CastShadow)
		{
			Component->CastShadow = FoliageType->CastShadow;
			bNeedsMarkRenderStateDirty = true;
			//bNeedsInvalidateLightingCache = true;
		}
		if (Component->bCastDynamicShadow != FoliageType->bCastDynamicShadow)
		{
			Component->bCastDynamicShadow = FoliageType->bCastDynamicShadow;
			bNeedsMarkRenderStateDirty = true;
			//bNeedsInvalidateLightingCache = true;
		}

		if (Component->RuntimeVirtualTextures != FoliageType->RuntimeVirtualTextures)
		{
			Component->RuntimeVirtualTextures = FoliageType->RuntimeVirtualTextures;
			bNeedsMarkRenderStateDirty = true;
		}
		if (Component->VirtualTextureRenderPassType != FoliageType->VirtualTextureRenderPassType)
		{
			Component->VirtualTextureRenderPassType = FoliageType->VirtualTextureRenderPassType;
			bNeedsMarkRenderStateDirty = true;
		}
		if (Component->VirtualTextureCullMips != FoliageType->VirtualTextureCullMips)
		{
			Component->VirtualTextureCullMips = FoliageType->VirtualTextureCullMips;
			bNeedsMarkRenderStateDirty = true;
		}
		if (Component->TranslucencySortPriority != FoliageType->TranslucencySortPriority)
		{
			Component->TranslucencySortPriority = FoliageType->TranslucencySortPriority;
			bNeedsMarkRenderStateDirty = true;
		}
		if (Component->bAffectDynamicIndirectLighting != FoliageType->bAffectDynamicIndirectLighting)
		{
			Component->bAffectDynamicIndirectLighting = FoliageType->bAffectDynamicIndirectLighting;
			bNeedsMarkRenderStateDirty = true;
			//bNeedsInvalidateLightingCache = true;
		}
		if (Component->bAffectDistanceFieldLighting != FoliageType->bAffectDistanceFieldLighting)
		{
			Component->bAffectDistanceFieldLighting = FoliageType->bAffectDistanceFieldLighting;
			bNeedsMarkRenderStateDirty = true;
			//bNeedsInvalidateLightingCache = true;
		}
		if (Component->bCastShadowAsTwoSided != FoliageType->bCastShadowAsTwoSided)
		{
			Component->bCastShadowAsTwoSided = FoliageType->bCastShadowAsTwoSided;
			bNeedsMarkRenderStateDirty = true;
			//bNeedsInvalidateLightingCache = true;
		}
		if (Component->bReceivesDecals != FoliageType->bReceivesDecals)
		{
			Component->bReceivesDecals = FoliageType->bReceivesDecals;
			bNeedsMarkRenderStateDirty = true;
			//bNeedsInvalidateLightingCache = true;
		}

		if (Component->bUseAsOccluder != FoliageType->bUseAsOccluder)
		{
			Component->bUseAsOccluder = FoliageType->bUseAsOccluder;
			bNeedsMarkRenderStateDirty = true;
		}

		if (Component->bEnableDensityScaling != FoliageType->bEnableDensityScaling)
		{
			Component->bEnableDensityScaling = FoliageType->bEnableDensityScaling;

			Component->UpdateDensityScaling();

			bNeedsMarkRenderStateDirty = true;
		}

		if (GetLightingChannelMaskForStruct(Component->LightingChannels) != GetLightingChannelMaskForStruct(FoliageType->LightingChannels))
		{
			Component->LightingChannels = FoliageType->LightingChannels;
			bNeedsMarkRenderStateDirty = true;
		}

		UFoliageInstancedStaticMeshComponent* FoliageComponent_ = Cast<UFoliageInstancedStaticMeshComponent>(Component);

		if (Component->bRenderCustomDepth != FoliageType->bRenderCustomDepth)
		{
			Component->bRenderCustomDepth = FoliageType->bRenderCustomDepth;
			bNeedsMarkRenderStateDirty = true;
		}

		if (Component->CustomDepthStencilWriteMask != FoliageType->CustomDepthStencilWriteMask)
		{
			Component->CustomDepthStencilWriteMask = FoliageType->CustomDepthStencilWriteMask;
			bNeedsMarkRenderStateDirty = true;
		}

		if (Component->CustomDepthStencilValue != FoliageType->CustomDepthStencilValue)
		{
			Component->CustomDepthStencilValue = FoliageType->CustomDepthStencilValue;
			bNeedsMarkRenderStateDirty = true;
		}

		const UFoliageType_InstancedStaticMesh* FoliageType_ISM = Cast<UFoliageType_InstancedStaticMesh>(FoliageType);
		if (FoliageType_ISM)
		{
			// Check override materials
			if (Component->OverrideMaterials.Num() != FoliageType_ISM->OverrideMaterials.Num())
			{
				Component->OverrideMaterials = FoliageType_ISM->OverrideMaterials;
				bNeedsMarkRenderStateDirty = true;
				//bNeedsInvalidateLightingCache = true;
			}
			else
			{
				for (int32 Index = 0; Index < FoliageType_ISM->OverrideMaterials.Num(); Index++)
				{
					if (Component->OverrideMaterials[Index] != FoliageType_ISM->OverrideMaterials[Index])
					{
						Component->OverrideMaterials = FoliageType_ISM->OverrideMaterials;
						bNeedsMarkRenderStateDirty = true;
						//bNeedsInvalidateLightingCache = true;
						break;
					}
				}
			}
		}


		if (bNeedsMarkRenderStateDirty)
		{
			Component->MarkRenderStateDirty();
		}
	}
}
void FSpawnableMesh::Initiate(AShaderWorldActor* Owner_, int32 Index, int32 BiomIndex)
{
	CleanUp();	
	
	if(Owner_ && bHasValidSpawningData())
	{
		Owner=Owner_;

		UWorld* World = Owner->GetWorld();

		FoliageIndexSeed = Index;

		ProcessedRead = MakeShared<FSWShareableIndexesCompletion, ESPMode::ThreadSafe>();
		ProcessedRead->bProcessingCompleted = true;

		/// Computation Optimization
		/// instead of evaluating the noise and generating the precise normal, we can use the already computed heightmap/NormalMap
		/// it adds a dependency to those maps/this ring but makes for far better performances
		/// 
		int32 MaxCullDistance = 0;


		float MaxDrawDistanceScale = GetCachedScalabilityCVars().ViewDistanceScale;

		if(MaxDrawDistanceScale<=0.f)
			MaxDrawDistanceScale=1.f;

		// Find number of rings START
		if (UpdateSpawnSettings)
		{
			/*
			 *	Update Cull distance and density from privded asset
			 */
			if(Foliages.Num()>0)
			{
				for (UFoliageType_InstancedStaticMesh* FoliageType : Foliages)
				{
					if (FoliageType && FoliageType->Mesh)
					{
						if (FoliageType->CullDistance.Max > MaxCullDistance)
						{
							MaxCullDistance = FoliageType->CullDistance.Max;

							CullDistance.Min = FoliageType->CullDistance.Min;
							CullDistance.Max = FoliageType->CullDistance.Max;
						}
					}
				}
			}
			else if (GrassType)
			{
				for (FGrassVariety& Grass : GrassType->GrassVarieties)
				{
					if (Grass.GrassMesh)
					{
						if (Grass.EndCullDistance.Default > MaxCullDistance)
						{
							MaxCullDistance = Grass.EndCullDistance.Default;

							CullDistance.Min = Grass.StartCullDistance.Default;
							CullDistance.Max = Grass.EndCullDistance.Default;
						}
					}
				}
			}
			else
			{
				MaxCullDistance = CullDistance.Max;				
			}
			
			if (GrassType)
				for (FGrassVariety& Grass : GrassType->GrassVarieties)
				{
					if (Grass.GrassDensity.Default > Density)
						Density = Grass.GrassDensity.Default;
				}
			
			for (UFoliageType_InstancedStaticMesh* FoliageType : Foliages)
			{
				if (FoliageType && FoliageType->Density > Density)
					Density = FoliageType->Density;

			}			
		}
		else
		{
			MaxCullDistance = CullDistance.Max;			
		}


		{
			if(MaxCullDistance <= 0.f)
				MaxCullDistance = 50;

			float MaxDistanceMeters = MaxCullDistance * FMath::Min(1.f, MaxDrawDistanceScale) / 100.f;

			//int32 InstanceCountPerCompute = Density / (10.f * 10.f) * (GridSizeMeters * GridSizeMeters);

			

			/*
			 * We have a draw distance and a density
			 * InstanceCountPerSector = Density * (GridSizeMeters*GridSizeMeters)/(10.f*10.f);
			 *
			 * If collision, max instance per patch 1000, if no collision max instance per patch 10000
			 * Figure out Grid size meter
			 */

			int32 iterGridCount = 1;


			/*
			 * GridSizeMeters must be independant of MaxDrawDistanceScale
			 * We therefore need to compute it regardless of MaxDrawDistanceScale
			 */

			while (iterGridCount < 30)
			{
				float GridSizeMeterCandidate = FMath::CeilToInt((MaxCullDistance / 100.f * 1.075f / ((float)iterGridCount)));

				int32 InstanceCountCandidate = Density * (GridSizeMeterCandidate * GridSizeMeterCandidate) / (10.f * 10.f);

				if (CollisionEnabled)
				{
					if (InstanceCountCandidate < GSWMaxCollisionInstancesPerComponent)
					{
						GridSizeMeters = GridSizeMeterCandidate;
						break;
					}
				}
				else
				{
					if (InstanceCountCandidate < GSWMaxInstancesPerComponent)
					{
						GridSizeMeters = GridSizeMeterCandidate;
						break;
					}
				}

				iterGridCount++;
			}

			if(GridSizeMeters>0.1)
				iterGridCount = FMath::CeilToInt(MaxDistanceMeters * 1.075f/ GridSizeMeters);
			else
				iterGridCount = 1;

			if(iterGridCount>30)
				iterGridCount = 30;

			int NumberOfRings = iterGridCount;//FMath::Max(1, FMath::RoundToInt(MaxDistanceMeters / GridSizeMeters));

			// As we re rounding there case where we wont compute far enough - we tolerate it if within 25% of GridSizeMeters away
			if ((MaxDistanceMeters - NumberOfRings * GridSizeMeters) > 0.25f * GridSizeMeters)
				NumberOfRings++;

			//if(NumberGridRings>=30)
			NumberGridRings = FMath::Max(1, FMath::Min(30, NumberOfRings));
		}
		// Find number of rings END
		
		//float NoPoppingRange = NoPoppingRange;//meters

		FVector2D Extent = GridSizeMeters*100.f *(NumberGridRings+1) * FVector2D(1.f, 1.f) * 1.1f;//Margin
		FVector2D Extent_single = Owner_->NoPoppingRange*100.f* FVector2D(1.f, 1.f) + GridSizeMeters*100.f * FVector2D(1.f, 1.f) * 1.1f;//Margin
		FBox2D LocalMeshBox(- Extent, Extent);

		

		IndexOfClipMapForCompute = 0;
		PositionCanBeAdjustedWithLOD = 0;

		for (int i = Owner->GetMeshNum()-1; i >=0 ; i--)
		{
			FClipMapMeshElement& Elem = Owner->GetMesh(i);			
			
			FVector2D Extent_Elem_Local = (Owner->N - 1 - 1) * Elem.GridSpacing / 2.f * FVector2D(1.f, 1.f);

			FBox2D Elem_Local_Footprint(- Extent_Elem_Local, Extent_Elem_Local);

			if(Elem_Local_Footprint.IsInside(LocalMeshBox.Max))
			{
				IndexOfClipMapForCompute = i;
				
			}
			if (Elem_Local_Footprint.IsInside(Extent_single) && PositionCanBeAdjustedWithLOD==0)
			{
				PositionCanBeAdjustedWithLOD =/*Owner->GetMeshNum()-1-*/i;

			}
			if(IndexOfClipMapForCompute>0)
				break;
			
		}
		
		//PositionCanBeAdjustedWithLOD Include the no popping condition - it might have a larger footprint than all the rings combined
		if(PositionCanBeAdjustedWithLOD<IndexOfClipMapForCompute)
			PositionCanBeAdjustedWithLOD=IndexOfClipMapForCompute;

		PositionCanBeAdjustedWithLOD = Owner->GetMeshNum()-1-PositionCanBeAdjustedWithLOD;

		InstanceCountPerSector = Density * (GridSizeMeters*GridSizeMeters)/(10.f*10.f);

		TotalComputedInstanceCount = (NumberGridRings*2 + 1) * (NumberGridRings * 2 + 1) * InstanceCountPerSector;

		int PoolItemCount=0;
		NumInstancePerHIM->Indexes.Empty();
		if(SpawnType!= ESpawnableType::Actor)
		{
			if(GrassType)
			{
				for (FGrassVariety& Grass : GrassType->GrassVarieties)
				{
					if(Grass.GrassMesh)
					{
						PoolItemCount++;
						NumInstancePerHIM->Indexes.Add(0);
					}
				}			
			}

			for (UFoliageType_InstancedStaticMesh* Sm : Foliages)
			{
				if (Sm && Sm->Mesh)
				{
					PoolItemCount++;
					NumInstancePerHIM->Indexes.Add(0);
				}

			}
			for (UStaticMesh* Sm : Mesh)
			{
				if (Sm)
				{
					PoolItemCount++;
					NumInstancePerHIM->Indexes.Add(0);
				}

			}
		}
		else
			for (TSubclassOf<AActor>& A : Actors)
			{
				if ((*A)!= nullptr)
				{
					PoolItemCount++;
					NumInstancePerHIM->Indexes.Add(0);
				}

			}


		if (InstanceCountPerSector < PoolItemCount)
			InstanceCountPerSector = PoolItemCount;
		
		RT_Dim = FMath::Floor(FMath::Sqrt((float)InstanceCountPerSector)) + (FMath::Frac(FMath::Sqrt((float)InstanceCountPerSector))>0.f?1:0);

		// Compute grid real world size is : RegionWorldDimension * RT_Dim / (RT_Dim - 1)
		// To reach top right corner from center of grid we using half this distance in X Y. 
		// Adding a large Z value to counter our lack of information about the height assets will spawn at
		
		ExtentOfMeshElement = FVector(1.f,1.f,0.f)*(GridSizeMeters*100.f * (RT_Dim<=1? 1 : RT_Dim / (RT_Dim - 1)))/2.f + FVector(0.f,0.f,1.f)*(Owner->VerticalRangeMeters*100.f);


		const int NumOfVertex = RT_Dim*RT_Dim;

		for(int i=0; i<NumOfVertex;i++)
		{
			InstanceIndexToHIMIndex->Indexes.Add(i%PoolItemCount);
			InstanceIndexToIndexForHIM->Indexes.Add(NumInstancePerHIM->Indexes[i%PoolItemCount]);
			NumInstancePerHIM->Indexes[i%PoolItemCount] = NumInstancePerHIM->Indexes[i%PoolItemCount] + 1;
			
		}

		

		
		if(SpawnType!= ESpawnableType::Actor)
		{

#if 1
			int32 FoliageVar = 0;
			for (UFoliageType_InstancedStaticMesh* FoliageType : Foliages)
			{
				
				if (FoliageType && FoliageType->GetStaticMesh())
				{

					USWHISMComponent* NHISM = nullptr;
					bool FoliageComp = false;
					
					UClass* ComponentClass = *FoliageType->ComponentClass;
					if (ComponentClass == nullptr || !ComponentClass->IsChildOf(USWHISMComponent::StaticClass()))
					{
						ComponentClass = USWHISMComponent::StaticClass();
					}

					NHISM = NewObject<USWHISMComponent>(Owner, ComponentClass, NAME_None, RF_Transient);
					
					NHISM->ComponentTags = ComponentTags;

					NHISM->bHasPerInstanceHitProxies = false;
					NHISM->SetCanEverAffectNavigation(false);
					NHISM->bUseTranslatedInstanceSpace=false;
					NHISM->bAlwaysCreatePhysicsState=false;
					

					if (UFoliageInstancedStaticMeshComponent* FComp = Cast<UFoliageInstancedStaticMeshComponent>(NHISM))
						FoliageComp = true;

					NHISM->SetupAttachment(Owner->GetRootComponent());
					NHISM->RegisterComponent();
					NHISM->SetStaticMesh(FoliageType->GetStaticMesh());
					NHISM->SetRelativeLocation(FVector(0.f, 0.f, 0.f));

					NHISM->SetUsingAbsoluteLocation(true);
					NHISM->SetUsingAbsoluteRotation(true);

					

					if (UpdateSpawnSettings)
					{
						UpdateStaticMeshHierachyComponentSettings(NHISM, CullDistance);

						CastShadows = FoliageType->CastShadow;
						bAffectDynamicIndirectLighting = FoliageType->bAffectDynamicIndirectLighting;
						bAffectDistanceFieldLighting = FoliageType->bAffectDistanceFieldLighting;
						bCastShadowAsTwoSided = FoliageType->bCastShadowAsTwoSided;
						bReceivesDecals = FoliageType->bReceivesDecals;

						CollisionEnabled = FoliageType->BodyInstance.GetCollisionEnabled() != ECollisionEnabled::NoCollision;

						AlignMaxAngle = FoliageType->AlignToNormal ? FoliageType->AlignMaxAngle : 0.f;

						AltitudeRange.Min = FoliageType->Height.Min;
						AltitudeRange.Max = FoliageType->Height.Max;

						VerticalOffsetRange.Min = FoliageType->ZOffset.Min;
						VerticalOffsetRange.Max = FoliageType->ZOffset.Max;

						GroundSlopeAngle.Min = FoliageType->GroundSlopeAngle.Min;
						GroundSlopeAngle.Max = FoliageType->GroundSlopeAngle.Max;
					}


					NHISM->CastShadow = CastShadows;
					NHISM->bCastDynamicShadow = CastShadows;
					NHISM->bCastStaticShadow = false;

					NHISM->InstanceStartCullDistance = CullDistance.Min;
					NHISM->InstanceEndCullDistance = CullDistance.Max;

					NHISM->bAffectDynamicIndirectLighting = bAffectDynamicIndirectLighting;
					NHISM->bAffectDistanceFieldLighting = bAffectDistanceFieldLighting;
					NHISM->bCastShadowAsTwoSided = bCastShadowAsTwoSided;
					NHISM->bReceivesDecals = bReceivesDecals;

					{
						// To guarantee consistency across platforms, we force the string to be lowercase and always treat it as an ANSI string.
						int32 FolSeed = FCrc::StrCrc32(StringCast<ANSICHAR>(*FString::Printf(TEXT("%s%s%d"), *FoliageType->GetName().ToLower(), *NHISM->GetName().ToLower(), FoliageVar)).Get());
						if (FolSeed == 0)
						{
							FolSeed++;
						}

						NHISM->InstancingRandomSeed = FolSeed;
						NHISM->Mobility = EComponentMobility::Static;
						NHISM->PrecachePSOs();
					}
					


					if (NumberGridRings == 0 || !CollisionOnlyAtProximity && (NumberGridRings > 0))
						NHISM->SetCollisionEnabled(CollisionEnabled ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
					else
						NHISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);

					HIM_Mesh.Add(NHISM);
					HIM_Mesh_TreeRebuild_Time.Add(0);

					if (CollisionEnabled && CollisionOnlyAtProximity && NumberGridRings > 0)
					{
						USWHISMComponent* NHISM_w_Collision = nullptr;

						//NHISM_w_Collision = NewObject<UHierarchicalInstancedStaticMeshComponent>(Owner, ComponentClass, NAME_None, RF_Transient);
						NHISM_w_Collision = NewObject<USWHISMComponent>(Owner, USWCollectableInstancedSMeshComponent::StaticClass(), NAME_None, RF_Transient);

						NHISM_w_Collision->bUseTranslatedInstanceSpace = false;
						NHISM_w_Collision->SetCanEverAffectNavigation(false);
						NHISM_w_Collision->ComponentTags = ComponentTags;
						
						Owner->CollisionToSpawnable.Add(NHISM_w_Collision,this);
						if (USWCollectableInstancedSMeshComponent* HSM_Col = Cast<USWCollectableInstancedSMeshComponent>(NHISM_w_Collision))
						{
							HSM_Col->Visual_Mesh_Owner = NHISM;
							HSM_Col->SpawnableID = Index;
							HSM_Col->BiomID= BiomIndex;

							if(bResourceCollectable)
							{
								HSM_Col->ResourceName = ResourceName;
								HSM_Col->bCollectable = true;
							}

							HSM_Col->Redirection = MakeShared<FSWRedirectionIndexes>();
							
						}
	
						NHISM_w_Collision->bHiddenInGame = true;

						NHISM_w_Collision->SetupAttachment(Owner->GetRootComponent());
						NHISM_w_Collision->RegisterComponent();
						NHISM_w_Collision->SetStaticMesh(FoliageType->GetStaticMesh());
						NHISM_w_Collision->SetRelativeLocation(FVector(0.f, 0.f, 0.f));

						NHISM->SetUsingAbsoluteLocation(true);
						NHISM->SetUsingAbsoluteRotation(true);

						NHISM_w_Collision->SetCastShadow(false /*CastShadows */ );

						NHISM_w_Collision->InstanceStartCullDistance = 0;
						NHISM_w_Collision->InstanceEndCullDistance = 0;

						NHISM_w_Collision->bAffectDynamicIndirectLighting = false;//bAffectDynamicIndirectLighting;
						NHISM_w_Collision->bAffectDistanceFieldLighting = false;//bAffectDistanceFieldLighting;
						NHISM_w_Collision->bCastShadowAsTwoSided = false;//bCastShadowAsTwoSided;
						NHISM_w_Collision->bReceivesDecals = false;//bReceivesDecals;

						NHISM_w_Collision->SetCollisionObjectType(CollisionChannel);
						NHISM_w_Collision->SetCollisionResponseToChannels(CollisionProfile);
						NHISM_w_Collision->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);

						{
							NHISM_w_Collision->Mobility = EComponentMobility::Static;
						}

						HIM_Mesh_Collision_enabled.Add(NHISM_w_Collision);
						HIM_Mesh_Collision_TreeRebuild_Time.Add(0.f);

					}

				}
				FoliageVar++;
			}
			
#endif
			
			if (GrassType)
			{
				int32 GrassVar = 0;
				for (FGrassVariety& Grass : GrassType->GrassVarieties)
				{
					
					if (Grass.GrassMesh)
					{							
					
						USWHISMComponent* NHISM = nullptr;
						bool FoliageComp = false;

						UClass* ComponentClass = USWHISMComponent::StaticClass();

						NHISM = NewObject<USWHISMComponent>(Owner, ComponentClass, NAME_None, RF_Transient);
						
						NHISM->bUseTranslatedInstanceSpace = false;
						NHISM->bAlwaysCreatePhysicsState = false;
						NHISM->ComponentTags = ComponentTags;

						NHISM->SetupAttachment(Owner->GetRootComponent());						

						NHISM->RegisterComponent();
						NHISM->SetStaticMesh(Grass.GrassMesh);
						NHISM->SetRelativeLocation(FVector(0.f, 0.f, 0.f));

						NHISM->SetUsingAbsoluteLocation(true);
						NHISM->SetUsingAbsoluteRotation(true);

						if (UpdateSpawnSettings)
						{							
							CastShadows = Grass.bCastDynamicShadow;							

							bAffectDynamicIndirectLighting = false;
							bAffectDistanceFieldLighting = false;
							bCastShadowAsTwoSided = false;
							bReceivesDecals = Grass.bReceivesDecals;
							CollisionEnabled = false;

							AlignMaxAngle = Grass.AlignToSurface ? 90.f : 0.f;

						}

						CollisionEnabled = false;

						NHISM->SetCastShadow(CastShadows);

						NHISM->MinLOD = Grass.MinLOD;
						NHISM->bSelectable = false;
						NHISM->bHasPerInstanceHitProxies = false;
						NHISM->bDisableCollision = true;
						NHISM->SetCanEverAffectNavigation(false);
						
						NHISM->LightingChannels = Grass.LightingChannels;
						NHISM->bCastStaticShadow = false;
						NHISM->CastShadow = CastShadows;
						NHISM->bCastDynamicShadow = CastShadows;
						NHISM->OverrideMaterials = Grass.OverrideMaterials;

						NHISM->bEnableDensityScaling = GrassType->bEnableDensityScaling;
						
						NHISM->InstanceStartCullDistance = CullDistance.Min;
						NHISM->InstanceEndCullDistance = CullDistance.Max;	
						
						
						NHISM->bAffectDynamicIndirectLighting = bAffectDynamicIndirectLighting;
						NHISM->bAffectDistanceFieldLighting = false;//bAffectDistanceFieldLighting;
						NHISM->bCastShadowAsTwoSided = bCastShadowAsTwoSided;
						NHISM->bReceivesDecals = bReceivesDecals;

						{
							// To guarantee consistency across platforms, we force the string to be lowercase and always treat it as an ANSI string.
							int32 FolSeed = FCrc::StrCrc32(StringCast<ANSICHAR>(*FString::Printf(TEXT("%s%s%d"), *GrassType->GetName().ToLower(), *NHISM->GetName().ToLower(), GrassVar)).Get());
							if (FolSeed == 0)
							{
								FolSeed++;
							}

							NHISM->InstancingRandomSeed = FolSeed;
							NHISM->Mobility = EComponentMobility::Static;
							NHISM->bEvaluateWorldPositionOffset = true;
							NHISM->WorldPositionOffsetDisableDistance = Grass.InstanceWorldPositionOffsetDisableDistance;

							NHISM->PrecachePSOs();
						}
						

					
						NHISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);

						HIM_Mesh.Add(NHISM);
						HIM_Mesh_TreeRebuild_Time.Add(0);

					}

					GrassVar++;
				}
				
				
			}

			int32 MeshVar = 0;
			for(UStaticMesh* Sm : Mesh)
			{
				
				if(Sm)
				{
					
					USWHISMComponent* NHISM = nullptr;
					bool FoliageComp = false;

					UClass* ComponentClass = *FoliageComponent;
					if (ComponentClass == nullptr || !ComponentClass->IsChildOf(USWHISMComponent::StaticClass()))
					{
						ComponentClass = USWHISMComponent::StaticClass();
					}
				
					NHISM = NewObject<USWHISMComponent>(Owner, ComponentClass,NAME_None, RF_Transient);

					NHISM->bHasPerInstanceHitProxies = false;
					NHISM->SetCanEverAffectNavigation(false);
					NHISM->bUseTranslatedInstanceSpace = false;
					NHISM->bAlwaysCreatePhysicsState = false;

					NHISM->ComponentTags = ComponentTags;
				
					if (UFoliageInstancedStaticMeshComponent* FComp = Cast<UFoliageInstancedStaticMeshComponent>(NHISM))
						FoliageComp = true;

					NHISM->SetupAttachment(Owner->GetRootComponent());
					NHISM->RegisterComponent();
					NHISM->SetStaticMesh(Sm);
					NHISM->SetRelativeLocation(FVector(0.f, 0.f, 0.f));

					NHISM->SetUsingAbsoluteLocation(true);
					NHISM->SetUsingAbsoluteRotation(true);

					UpdateStaticMeshHierachyComponentSettings(NHISM,CullDistance);

					
					NHISM->CastShadow = CastShadows;
					NHISM->bCastDynamicShadow = CastShadows;
					NHISM->bCastStaticShadow =false;

					NHISM->InstanceStartCullDistance = CullDistance.Min;
					NHISM->InstanceEndCullDistance = CullDistance.Max;

					NHISM->bAffectDynamicIndirectLighting = bAffectDynamicIndirectLighting;
					NHISM->bAffectDistanceFieldLighting = bAffectDistanceFieldLighting;
					NHISM->bCastShadowAsTwoSided = bCastShadowAsTwoSided;
					NHISM->bReceivesDecals = bReceivesDecals;

					{
						// To guarantee consistency across platforms, we force the string to be lowercase and always treat it as an ANSI string.
						int32 FolSeed = FCrc::StrCrc32(StringCast<ANSICHAR>(*FString::Printf(TEXT("%s%s%d"), *Sm->GetName().ToLower(), *NHISM->GetName().ToLower(), MeshVar)).Get());
						if (FolSeed == 0)
						{
							FolSeed++;
						}
						NHISM->InstancingRandomSeed = FolSeed;
						NHISM->Mobility = EComponentMobility::Static;
						NHISM->PrecachePSOs();
					}


					if(NumberGridRings==0 || !CollisionOnlyAtProximity && (NumberGridRings>0))
						NHISM->SetCollisionEnabled(CollisionEnabled ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
					else
						NHISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);

					HIM_Mesh.Add(NHISM);
					HIM_Mesh_TreeRebuild_Time.Add(0);

					if(CollisionEnabled && CollisionOnlyAtProximity && NumberGridRings>0)
					{
						USWHISMComponent* NHISM_w_Collision = nullptr;
						
						NHISM_w_Collision = NewObject<USWHISMComponent>(Owner, USWCollectableInstancedSMeshComponent::StaticClass(), NAME_None, RF_Transient);

						NHISM_w_Collision->SetCanEverAffectNavigation(true);
						NHISM_w_Collision->bUseTranslatedInstanceSpace = false;
						NHISM_w_Collision->ComponentTags = ComponentTags;

						Owner->CollisionToSpawnable.Add(NHISM_w_Collision, this);
						if (USWCollectableInstancedSMeshComponent* HSM_Col = Cast<USWCollectableInstancedSMeshComponent>(NHISM_w_Collision))
						{
							HSM_Col->Visual_Mesh_Owner = NHISM;
							HSM_Col->SpawnableID = Index;
							HSM_Col->BiomID = BiomIndex;

							if (bResourceCollectable)
							{
								HSM_Col->ResourceName = ResourceName;
								HSM_Col->bCollectable = true;
							}

							HSM_Col->Redirection = MakeShared<FSWRedirectionIndexes>();
							
						}						
					
						NHISM_w_Collision->bHiddenInGame=true;

						NHISM_w_Collision->SetupAttachment(Owner->GetRootComponent());
						NHISM_w_Collision->RegisterComponent();
						NHISM_w_Collision->SetStaticMesh(Sm);
						NHISM_w_Collision->SetRelativeLocation(FVector(0.f, 0.f, 0.f));

						NHISM->SetUsingAbsoluteLocation(true);
						NHISM->SetUsingAbsoluteRotation(true);

						NHISM_w_Collision->SetCastShadow(false /*CastShadows */ );

						NHISM_w_Collision->InstanceStartCullDistance = 0;
						NHISM_w_Collision->InstanceEndCullDistance = 0;

						NHISM_w_Collision->bAffectDynamicIndirectLighting = false;//bAffectDynamicIndirectLighting;
						NHISM_w_Collision->bAffectDistanceFieldLighting = false;//bAffectDistanceFieldLighting;
						NHISM_w_Collision->bCastShadowAsTwoSided = false;//bCastShadowAsTwoSided;
						NHISM_w_Collision->bReceivesDecals = false;//bReceivesDecals;

						NHISM_w_Collision->SetCollisionObjectType(CollisionChannel);
						NHISM_w_Collision->SetCollisionResponseToChannels(CollisionProfile);						
						NHISM_w_Collision->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);					

						{							
							NHISM_w_Collision->Mobility = EComponentMobility::Static;
						}
						HIM_Mesh_Collision_enabled.Add(NHISM_w_Collision);
						HIM_Mesh_Collision_TreeRebuild_Time.Add(0.f);
					}
				}
				MeshVar++;
			}
			
			
		}
		else
			for (TSubclassOf<AActor>& A : Actors)
			{
				if ((*A) != nullptr)
				{
					Spawned_Actors.Add(FSpawnedActorList());
					Actors_Validated.Add(A);
				}

			}
		UpdateSpawnSettings=false;

	}
}

void FSpawnableMesh::SpawnCollisionEnabled_HISM(TArray<TArray<FTransform>>& Transforms)
{

}

void FSpawnableMesh::CleanUp()
{
	
	for(FSpawnableMeshElement& El:SpawnablesElem)
	{
		if(IsValid(Owner) && El.SpawnDensity)
			Owner->RendertargetMemoryBudgetMB-=4*(El.SpawnDensity->SizeX *El.SpawnDensity->SizeX*4)/1000000.0f;

		El.SpawnDensity = nullptr;
		El.SpawnTransforms = nullptr;

		El.ComputeSpawnTransformDyn=nullptr;
		El.LOD_usedLastUpdate=-1;

		
		if (USWHISMComponent* HISM = El.HIM_Mesh)
		{
			if (IsValid(Owner) && IsValid(HISM) && Owner->GetWorld())
			{
				HISM->ClearInstances();
				if (HISM->IsRegistered())
					HISM->UnregisterComponent();
				
				HISM->DestroyComponent();
				
			}

			HISM = nullptr;
		}		
	}

	SegmentedOnly_ElementToUpdateData.Empty();

	SpawnablesElem.Empty();
	AvailableSpawnablesElem.Empty();
	UsedSpawnablesElem.Empty();	
	SpawnablesElemReadToProcess.Empty();
	SpawnablesLayout.Empty();

	SpawnableWorkQueue.Empty();

	SpawnablesCollisionElem.Empty();
	AvailableSpawnablesCollisionElem.Empty();
	UsedSpawnablesCollisionElem.Empty();
	SpawnablesCollisionLayout.Empty();

	SpawnablesElemNeedCollisionUpdate.Empty();


	for (USWHISMComponent* HISM : HIM_Mesh)
	{
		if(IsValid(Owner) && IsValid(HISM) && Owner->GetWorld())
		{
			HISM->ClearInstances();
			if(HISM->IsRegistered())
				HISM->UnregisterComponent();
			
			HISM->DestroyComponent();
			HISM = nullptr;
		}		
	}
	HIM_Mesh.Empty();
	HIM_Mesh_TreeRebuild_Time.Empty();

	for (USWHISMComponent* HISM : HIM_Mesh_Collision_enabled)
	{
		if (IsValid(Owner) && IsValid(HISM) && Owner->GetWorld())
		{
			HISM->ClearInstances();
			if(HISM->IsRegistered())
				HISM->UnregisterComponent();
			
			HISM->DestroyComponent();
			HISM = nullptr;
			if (IsValid(Owner))
			{
				Owner->CollisionToSpawnable.Remove(HISM);
			}
		}
	}
	HIM_Mesh_Collision_enabled.Empty();
	HIM_Mesh_Collision_TreeRebuild_Time.Empty();

	UWorld* World = nullptr;
	if(IsValid(Owner))
		World = Owner->GetWorld();

	for (FSpawnedActorList& SL: Spawned_Actors)
	{
		for (AActor* Act: SL.SpawnedActors)
		{
			if(World && IsValid(Act))
				World->DestroyActor(Act); 
		}
		SL.SpawnedActors.Empty();
	}
	Spawned_Actors.Empty();
	Actors_Validated.Empty();

	if (SpawnType == ESpawnableType::Grass)
	{
		Foliages.Empty();
		Mesh.Empty();
		Actors.Empty();
	}
	else if(SpawnType == ESpawnableType::Foliage)
	{
		GrassType=nullptr;
		Mesh.Empty();
		Actors.Empty();
	}
	else if (SpawnType == ESpawnableType::Mesh)
	{
		GrassType = nullptr;
		Foliages.Empty();
		Actors.Empty();
	}
	else if (SpawnType == ESpawnableType::Actor)
	{
		GrassType = nullptr;
		Foliages.Empty();
		Mesh.Empty();
	}
	SpawnType_LastUpdate=SpawnType;


	InstanceIndexToHIMIndex = MakeShared<FSWShareableIndexes, ESPMode::ThreadSafe>();
	NumInstancePerHIM = MakeShared<FSWShareableIndexes, ESPMode::ThreadSafe>();
	InstanceIndexToIndexForHIM = MakeShared<FSWShareableIndexes, ESPMode::ThreadSafe>();

	ProcessedRead.Reset();
	ProcessedRead = nullptr;

	InstanceIndexToHIMIndex->Indexes.Empty();
	NumInstancePerHIM->Indexes.Empty();
	InstanceIndexToIndexForHIM->Indexes.Empty();	

	IndexOfClipMapForCompute=-1;

	Owner = nullptr;
	
}

bool FSpawnableMesh::bHasValidSpawningData()
{
	if(SpawnType == ESpawnableType::Undefined)
	{
		return false;
	}
		
	
	if(SpawnType == ESpawnableType::Mesh)
	{
		for (UStaticMesh* sm : Mesh)
		{
			if (sm)
				return true;
		}
	}
	else if(SpawnType == ESpawnableType::Grass)
	{
		if(GrassType)
		{
			for (FGrassVariety& Grass : GrassType->GrassVarieties)
			{
				if (Grass.GrassMesh)
				{
					return true;
				}
			}
		}
	}
	else if (SpawnType == ESpawnableType::Foliage)
	{

		for (UFoliageType_InstancedStaticMesh* fol : Foliages)
		{
			if (fol && fol->Mesh)
				return true;
		}
	}
	else
	{
		for (TSubclassOf<AActor>& Act : Actors)
		{
			if (*Act)
				return true;
		}
	}

	return false;

}

FSpawnableMesh::~FSpawnableMesh()
{
	
	CleanUp();
	for (FSpawnableMeshElement& El : SpawnablesElem)
	{
		if (El.SpawnDensity && Owner && IsValid(Owner))
			Owner->RendertargetMemoryBudgetMB -= 4 * (El.SpawnDensity->SizeX * El.SpawnDensity->SizeX * 4) / 1000000.0f;

		El.SpawnDensity = nullptr;
		El.SpawnTransforms = nullptr;

		El.ComputeSpawnTransformDyn = nullptr;
		El.LOD_usedLastUpdate = -1;


		if (USWHISMComponent* HISM = El.HIM_Mesh)
		{
			if (IsValid(HISM) && Owner && IsValid(Owner) && Owner->GetWorld())
			{
				HISM->ClearInstances();
				if (HISM->IsRegistered())
					HISM->UnregisterComponent();

				HISM->DestroyComponent();

			}

			HISM = nullptr;
		}
	}

	SegmentedOnly_ElementToUpdateData.Empty();

	SpawnablesElem.Empty();
	AvailableSpawnablesElem.Empty();
	UsedSpawnablesElem.Empty();
	SpawnablesElemReadToProcess.Empty();
	SpawnablesLayout.Empty();

	SpawnableWorkQueue.Empty();

	SpawnablesCollisionElem.Empty();
	AvailableSpawnablesCollisionElem.Empty();
	UsedSpawnablesCollisionElem.Empty();
	SpawnablesCollisionLayout.Empty();

	SpawnablesElemNeedCollisionUpdate.Empty();


	for (USWHISMComponent* HISM : HIM_Mesh)
	{
		if (IsValid(HISM) && Owner && IsValid(Owner) && Owner->GetWorld())
		{
			HISM->ClearInstances();
			if (HISM->IsRegistered())
				HISM->UnregisterComponent();

			HISM->DestroyComponent();
			HISM = nullptr;
		}
	}
	HIM_Mesh.Empty();
	HIM_Mesh_TreeRebuild_Time.Empty();

	for (USWHISMComponent* HISM : HIM_Mesh_Collision_enabled)
	{
		if (IsValid(HISM) && Owner && IsValid(Owner) && Owner->GetWorld())
		{
			HISM->ClearInstances();
			if (HISM->IsRegistered())
				HISM->UnregisterComponent();

			HISM->DestroyComponent();
			HISM = nullptr;
			if (Owner && IsValid(Owner))
			{
				Owner->CollisionToSpawnable.Remove(HISM);
			}
		}
	}
	HIM_Mesh_Collision_enabled.Empty();
	HIM_Mesh_Collision_TreeRebuild_Time.Empty();

	UWorld* World = nullptr;
	if (Owner && IsValid(Owner))
		World = Owner->GetWorld();

	for (FSpawnedActorList& SL : Spawned_Actors)
	{
		if (World)
		{
			for (AActor* Act : SL.SpawnedActors)
			{
				if (IsValid(Act))
					World->DestroyActor(Act);
			}
		}		
		SL.SpawnedActors.Empty();
	}
	Spawned_Actors.Empty();
	Actors_Validated.Empty();

	if (SpawnType == ESpawnableType::Grass)
	{
		Foliages.Empty();
		Mesh.Empty();
		Actors.Empty();
	}
	else if (SpawnType == ESpawnableType::Foliage)
	{
		GrassType = nullptr;
		Mesh.Empty();
		Actors.Empty();
	}
	else if (SpawnType == ESpawnableType::Mesh)
	{
		GrassType = nullptr;
		Foliages.Empty();
		Actors.Empty();
	}
	else if (SpawnType == ESpawnableType::Actor)
	{
		GrassType = nullptr;
		Foliages.Empty();
		Mesh.Empty();
	}
	SpawnType_LastUpdate = SpawnType;


	InstanceIndexToHIMIndex = MakeShared<FSWShareableIndexes, ESPMode::ThreadSafe>();
	NumInstancePerHIM = MakeShared<FSWShareableIndexes, ESPMode::ThreadSafe>();
	InstanceIndexToIndexForHIM = MakeShared<FSWShareableIndexes, ESPMode::ThreadSafe>();

	ProcessedRead.Reset();
	ProcessedRead = nullptr;

	InstanceIndexToHIMIndex->Indexes.Empty();
	NumInstancePerHIM->Indexes.Empty();
	InstanceIndexToIndexForHIM->Indexes.Empty();

	IndexOfClipMapForCompute = -1;

	Owner = nullptr;

}

bool FClipMapMeshElement::IsSectionVisible(int SectionID, bool ToSegmentedCacheIfEnabled)
{
	if (ToSegmentedCacheIfEnabled && SectionID >= 0 && SectionID < SectionVisibility_SegmentedCache.Num())
	{
		return SectionVisibility_SegmentedCache[SectionID];
	}

	if(SectionID>=0 && SectionID<SectionVisibility.Num())
	{
		return SectionVisibility[SectionID];
	}
	return false;
}

void FClipMapMeshElement::SetSectionVisible(int SectionID,bool NewVisibility, bool ToSegmentedCacheIfEnabled)
{

	if (SectionID >= 0 && SectionID < SectionVisibility.Num())
	{
		if(ToSegmentedCacheIfEnabled)
		{
			if(SectionID < SectionVisibility_SegmentedCache.Num())
				SectionVisibility_SegmentedCache[SectionID]=NewVisibility;
		
			return;
		}


		SectionVisibility[SectionID]=NewVisibility;


		if(SectionID < SectionVisibility_SegmentedCache.Num())
			SectionVisibility_SegmentedCache[SectionID]=NewVisibility;

		if(Mesh)
		{
			Mesh->SetMeshSectionVisible(SectionID,NewVisibility);

			if(SectionID==0 && Mesh->GetNumSections()>6)
				Mesh->SetMeshSectionVisible(6,NewVisibility);
		}
	}
	
}
#endif


#undef LOCTEXT_NAMESPACE 
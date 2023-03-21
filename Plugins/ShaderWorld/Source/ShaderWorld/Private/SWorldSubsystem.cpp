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

#include "SWorldSubsystem.h"
#include "Data/SWorldConfig.h"
#include "Engine/World.h"
#include "PhysicsEngine/PhysicsSettings.h"
#include "ContentStreaming.h"
#include "HardwareInfo.h"
#include "ShaderWorld.h"
#include "SWStats.h"
#include "Actor/ShaderWorldActor.h"
#include "Component/ShaderWorldCollisionComponent.h"
#include "Component/SW_CollisionComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Character.h"
#include "Data/SWStructs.h"
#include "GameFramework/PawnMovementComponent.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"


ETickableTickType USWorldSubsystem::GetTickableTickType() const
{
	return !GetWorld() ? ETickableTickType::Never : ETickableTickType::Always;
}

TStatId USWorldSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(USWorldSubsystem, STATGROUP_Tickables);
}


USWorldSubsystem::~USWorldSubsystem()
{
}

void USWorldSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	SW_Contexts.Empty();
}
void USWorldSubsystem::Deinitialize()
{
	SW_Contexts.Empty();
	Super::Deinitialize();
}

bool USWorldSubsystem::IsContextRegistered(USWContextBase* SW_Context) const
{
	return SW_Contexts.Find(SW_Context) != INDEX_NONE;
}

void USWorldSubsystem::RegisterContext(USWContextBase* SW_Context)
{
	SW_Contexts.AddUnique(SW_Context);
}

void USWorldSubsystem::UnregisterContext(USWContextBase* SW_Context)
{
	SW_Contexts.Remove(SW_Context);
}


void USWorldSubsystem::Setup()
{
	if(TimeAcum<0.3f)
		return;

	if (!RenderThreadPoked)
	{
		RT_Ready.BeginFence();
		RenderThreadPoked = true;
	}
	else
	{
		if (RT_Ready.IsFenceComplete())
		{
			RenderThreadResponded = true;
		}
	}
}

void USWorldSubsystem::UpdateRenderAPI()
{

	if (RHIString == "")
	{
		RHIString = FHardwareInfo::GetHardwareInfo(NAME_RHI);

		if (RHIString != "")
		{
			UE_LOG(LogTemp, Warning, TEXT("RHI = %s"), *RHIString);

			if (RHIString == TEXT("D3D11"))
			{
				RendererAPI = EGeoRenderingAPI::DX11;
			}
			else if (RHIString == TEXT("D3D12"))
			{
				RendererAPI = EGeoRenderingAPI::DX12;
			}
			else if (RHIString == TEXT("OpenGL"))
			{
				RendererAPI = EGeoRenderingAPI::OpenGL;
			}
			else if (RHIString == TEXT("Vulkan"))
			{
				RendererAPI = EGeoRenderingAPI::Vulkan;
			}
			else if (RHIString == TEXT("Metal"))
			{
				RendererAPI = EGeoRenderingAPI::Metal;
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("No case setup for this RHI, default to DX11"));
				RHIString = "D3D11";
				RendererAPI = EGeoRenderingAPI::DX11;
			}
			
			if (!((RendererAPI == EGeoRenderingAPI::DX11) || (RendererAPI == EGeoRenderingAPI::DX12)))
				TopologyFixUnderLOD = 0;
				

#if SWDEBUG
			if (GEngine)
				GEngine->AddOnScreenDebugMessage(-1, 15.0f, FColor::Yellow, RHIString);
#endif

		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("Couldnt parse RHI, default to DX11"));
			RHIString = "D3D11";
			RendererAPI = EGeoRenderingAPI::DX11;
		}
	}
}

void USWorldSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	SW_FCT_CYCLE()

	TimeAcum += DeltaTime;

	UpdateRenderAPI();

	if (UWorld* World = GetWorld())
	{
		Setup();
		
		CameUpdateAcu += DeltaTime;

		if(CameUpdateAcu>1.0/(FMath::Min(UpdateRateCameras,120.0f)))
		{
			CameUpdateAcu = 0.0;
			UpdateVisitors(World);

			for (auto& Context : SW_Contexts)
			{
				Context->UpdateContext(DeltaTime, World, Visitors);
			}
		}
	}


	if(AsyncReads.IsFenceComplete())
	{
		ENQUEUE_RENDER_COMMAND(SWReadsBack)(
			[](FRHICommandListImmediate& RHICmdList)
			{
				check(IsInRenderingThread());

				GSWSimpleReadbackManager.TickReadBack();
			});

		AsyncReads.BeginFence();
	}
}

namespace SWSubsystem{
FCollisionQueryParams ConfigureCollisionParamsLocal(FName TraceTag, bool bTraceComplex, const TArray<AActor*>& ActorsToIgnore, bool bIgnoreSelf, const UObject* WorldContextObject)
{
	FCollisionQueryParams Params(TraceTag, SCENE_QUERY_STAT_ONLY(PoolTraceUtils), bTraceComplex);
	Params.bReturnPhysicalMaterial = true;
	Params.bReturnFaceIndex = !UPhysicsSettings::Get()->bSuppressFaceRemapTable; // Ask for face index, as long as we didn't disable globally
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
}

bool USWorldSubsystem::IsReadyToHandleGPURequest()
{
	//return true;
	return RenderThreadResponded;
}

void USWorldSubsystem::GetVisitors(TArray<FVector>& OutVisitor)
{
	OutVisitor = Visitors;
}

void USWorldSubsystem::SetCameraUpdateRate(float& NewRate)
{
	if(UpdateRateCameras < NewRate && (NewRate>0.0))
		UpdateRateCameras = NewRate;
}

void USWorldSubsystem::TrackComponent(USW_CollisionComponent* Comp)
{
	Tracked_Components.AddUnique(Comp);
}

void USWorldSubsystem::UnTrackComponent(USW_CollisionComponent* Comp)
{
	Tracked_Components.Remove(Comp);
}

void USWorldSubsystem::UpdateVisitors(UWorld* World)
{
	
	static TArray<FVector> OldCameras;
	TArray<FVector>* Cameras = nullptr;

	TSet<FIntVector> SegmentedWorldLocations;

	for (FConstPlayerControllerIterator Iterator = World->GetPlayerControllerIterator(); Iterator; ++Iterator)
	{
		if (APlayerController* PC = Iterator->Get())
		{
			FVector CamLoc;
			FRotator CamRot;
			PC->GetPlayerViewPoint(CamLoc, CamRot);

			if (APawn* Pawn = PC->GetPawn())
			{
				CamLoc = Pawn->GetActorLocation();

				bool Falling = false;

				if(ACharacter* Char = Cast<ACharacter>(Pawn))
				{
					CamLoc = (Char->GetCapsuleComponent()->GetComponentLocation() - Char->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() * FVector(0.f, 0.f, 1.f));
					Falling = Char->GetMovementComponent()->IsFalling();					
				}	

				static const FName LineTraceSingleName(TEXT("UnderGroundTrace"));
				FHitResult ResultHit;
				TArray<TEnumAsByte<EObjectTypeQuery>> Objectype;
				Objectype.Add(UEngineTypes::ConvertToObjectType(ECC_WorldStatic));
				Objectype.Add(UEngineTypes::ConvertToObjectType(ECC_WorldDynamic));
				FCollisionObjectQueryParams ObjectParams = SWSubsystem::ConfigureCollisionObjectParamsLocal(Objectype);

				TArray<AActor*> ignoredActor;
				ignoredActor.Add(Pawn);
				FCollisionQueryParams Params = SWSubsystem::ConfigureCollisionParamsLocal(LineTraceSingleName, false/*bTraceComplex*/, ignoredActor/*ActorsToIgnore*/, false/*bIgnoreSelf*/, this/*WorldContextObject*/);

				FVector Start = CamLoc + 0.0*(Pawn->GetVelocity().Size()>0.5f? 5.f:0.f) * FVector(0.f, 0.f, 1.f) + 10.f* Pawn->GetActorUpVector();
				FVector End = Start + 50000.f * FVector(0.f, 0.f, 1.f);

				TArray<FHitResult>Results;

				bool bHitWorld = false;//World->LineTraceSingleByObjectType(ResultHit, Start, End, ObjectParams, Params);

				UKismetSystemLibrary::SphereTraceMultiForObjects(World,Start,End,10.f, Objectype,false,ignoredActor,EDrawDebugTrace::Type::None, Results,true);

				UShaderWorldCollisionComponent* CollisionComp = nullptr;
				FVector HitLocation;
				float ImpactDistance=0.f;
				for(auto& r : Results)
				{
					if(r.Component.Get())
					{
						if(r.Component.Get()->GetClass()->GetName() == "ShaderWorldCollisionComponent")
						{
							bHitWorld = true;
							//CollisionComp = CollComp;
							HitLocation = r.ImpactPoint;
							ImpactDistance = r.Distance;

							ResultHit.Distance = ImpactDistance;
							ResultHit.ImpactPoint = HitLocation;
							break;
						}
					}
				}

				if (bHitWorld && (ResultHit.Distance > 250.f || (Falling && ResultHit.Distance >= 0.0f)))
				{
					#if SWDEBUG
					UE_LOG(LogTemp, Warning, TEXT("Underground")); 
					#endif

					FVector Destination = ResultHit.ImpactPoint + FVector(0.f, 0.f, 50.f);

					if (ACharacter* Char = Cast<ACharacter>(Pawn))
						Destination = ResultHit.ImpactPoint + Char->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() * FVector(0.f, 0.f, 1.f)*2.f;

					
					Pawn->SetActorLocation(Destination, false, nullptr, ETeleportType::TeleportPhysics);					

					CamLoc = ResultHit.ImpactPoint + FVector(0.f, 0.f, 50.f);
				}
			}

			SegmentedWorldLocations.Add(FIntVector(CamLoc/200.f));
		}
	}

	for (USW_CollisionComponent*Comp : Tracked_Components)
	{
		if(IsValid(Comp))
		{
			FVector CamLoc = Comp->GetComponentLocation();
			AActor* CompOwner = Comp->GetOwner();

			static const FName LineTraceSingleName(TEXT("UnderGroundTrace"));
			FHitResult ResultHit;
			TArray<TEnumAsByte<EObjectTypeQuery>> Objectype;
			Objectype.Add(UEngineTypes::ConvertToObjectType(ECC_WorldStatic));
			Objectype.Add(UEngineTypes::ConvertToObjectType(ECC_WorldDynamic));
			FCollisionObjectQueryParams ObjectParams = SWSubsystem::ConfigureCollisionObjectParamsLocal(Objectype);

			TArray<AActor*> ignoredActor;
			ignoredActor.Add(CompOwner);
			FCollisionQueryParams Params = SWSubsystem::ConfigureCollisionParamsLocal(LineTraceSingleName, false/*bTraceComplex*/, ignoredActor/*ActorsToIgnore*/, false/*bIgnoreSelf*/, this/*WorldContextObject*/);

			FVector Start = CompOwner->GetActorLocation() + 5.f * FVector(0.f, 0.f, 1.f);
			FVector End = Start + 50000.f * FVector(0.f, 0.f, 1.f);
			bool const bHitWorld = World->LineTraceSingleByObjectType(ResultHit, CompOwner->GetActorLocation(), End, ObjectParams, Params);

			UShaderWorldCollisionComponent* CollisionComp = Cast<UShaderWorldCollisionComponent>(ResultHit.Component.Get());

			if (bHitWorld && CollisionComp && (ResultHit.Distance > 250.f))
			{
#if SWDEBUG
				UE_LOG(LogTemp, Warning, TEXT("Underground"));
#endif

				CompOwner->SetActorLocation(ResultHit.ImpactPoint + FVector(0.f, 0.f, 50.f), false, nullptr, ETeleportType::TeleportPhysics);

				CamLoc = ResultHit.ImpactPoint + FVector(0.f, 0.f, 50.f);
			}

			SegmentedWorldLocations.Add(FIntVector(CamLoc / 200.f));
		}
	}



	bool VisitorCleared = false;


	if(SegmentedWorldLocations.Num()>0)
	{
		Visitors.Empty();
		Visitors.Reserve(SegmentedWorldLocations.Num());
		for(auto& el : SegmentedWorldLocations)
		{
			Visitors.Add(FVector(el*200.f) + 100.f*FVector(1.f));
		}
		VisitorCleared = true;
	}

	// there is a bug here, which often leaves us with no cameras in the editor
	if (World->ViewLocationsRenderedLastFrame.Num())
	{		
		Cameras = &World->ViewLocationsRenderedLastFrame;
		OldCameras = *Cameras;
	}
	else
	{		
		if (int32 Num = IStreamingManager::Get().GetNumViews())
		{
			OldCameras.Reset(Num);
			for (int32 Index = 0; Index < Num; Index++)
			{
				auto& ViewInfo = IStreamingManager::Get().GetViewInformation(Index);
				OldCameras.Add(ViewInfo.ViewOrigin);
			}
		}		
	}

	if(OldCameras.Num()>0)
	{
		if(!VisitorCleared)
			Visitors.Empty();

		Visitors.Append(OldCameras);
	}

}

bool USWorldSubsystem::CopyAtoB(UTextureRenderTarget2D* A, UTextureRenderTarget2D* B, UTextureRenderTarget2D* B_dup, int32 Border, uint32 Channel, FVector2D SL, FVector2D DL, float SDim, float DDim)
{	
	if(!RenderThreadResponded)
		return false;
	
	const SWCopyData Copy(A, B, B_dup, Border, Channel, SL,DL,SDim,DDim);
	SWToolBox->CopyAtoB(Copy);
	return true;
}

bool USWorldSubsystem::ComputeSpawnables(TSharedPtr<FSWSpawnableRequirements>& SpawnConfig)
{
	if (!RenderThreadResponded)
	{
#if SWDEBUG
		SW_LOG("!RenderThreadResponded Can't launch ComputeSpawnables")
#endif
		return false;
	}
		

	SWToolBox->ComputeSpawnables(SpawnConfig);
	return true;
}

bool USWorldSubsystem::ComputeNormalForHeightmap(UTextureRenderTarget2D* HeightM, UTextureRenderTarget2D* NormalM, int32& N, int32& LGC, float& HeightScale)
{
	if (!RenderThreadResponded)
		return false;

	const SWNormalComputeData NCData(HeightM, NormalM, N, LGC, HeightScale);
	SWToolBox->ComputeNormalForHeightMap(NCData);
	return true;
}

bool USWorldSubsystem::LoadSampleLocationsInRT(UTextureRenderTarget2D* LocationsRequestedRT,
	TSharedPtr<FSWShareableSamplePoints>& Samples)
{
	if (!RenderThreadResponded)
		return false;

	const SWSampleRequestComputeData ReadBackData(LocationsRequestedRT, Samples);
	SWToolBox->RequestReadBackLoad(ReadBackData);
	return true;
}

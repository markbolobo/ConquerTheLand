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
#include "Subsystems/WorldSubsystem.h"
#include "Utilities/SWShaderToolBox.h"
#include "Templates/SharedPointer.h"
#include "RenderCommandFence.h"
#include "Data/SWEnums.h"
#include "Engine/TextureRenderTarget2D.h"
#include "SWorldSubsystem.generated.h"

class USWContextBase;
class UTextureRenderTarget2D;
class FSWShareableSamplePoints;
class USW_CollisionComponent;
class FSWSpawnableRequirements;
/**
 * 
 */
UCLASS()
class SHADERWORLD_API USWorldSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual ~USWorldSubsystem();

	// USubsystem implementation Begin
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;	
	// USubsystem implementation End

	bool IsContextRegistered(USWContextBase* SW_Context) const;
	void RegisterContext(USWContextBase* SW_Context);
	void UnregisterContext(USWContextBase* SW_Context);

	// Begin FTickableGameObject overrides
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickableInEditor() const override { return true; }
	virtual ETickableTickType GetTickableTickType() const override;
	virtual TStatId GetStatId() const override;
	// End FTickableGameObject overrides

	void Setup();
	void UpdateRenderAPI();
	bool IsReadyToHandleGPURequest();
	void UpdateVisitors(UWorld* World);
	void GetVisitors(TArray<FVector>& OutVisitor);
	void SetCameraUpdateRate(float& NewRate);
	void TrackComponent(USW_CollisionComponent* Comp);
	void UnTrackComponent(USW_CollisionComponent* Comp);

	//Shader helper
	bool CopyAtoB(UTextureRenderTarget2D* A, UTextureRenderTarget2D* B, UTextureRenderTarget2D* B_dup=nullptr, int32 Border=0, uint32 Channel=0, FVector2D SL = FVector2D(), FVector2D DL= FVector2D(), float SDim = 0.f, float DDim = 0.f);
	bool ComputeSpawnables(TSharedPtr<FSWSpawnableRequirements, ESPMode::ThreadSafe>& SpawnConfig);
	bool ComputeNormalForHeightmap(UTextureRenderTarget2D* HeightM, UTextureRenderTarget2D* NormalM, int32& N, int32& LGC, float& HeightScale);

	bool LoadSampleLocationsInRT(UTextureRenderTarget2D* LocationsRequestedRT, TSharedPtr<FSWShareableSamplePoints>& Samples);

private:
	TArray<USWContextBase*> SW_Contexts;
	TArray<FVector> Visitors;
	TArray<USW_CollisionComponent*> Tracked_Components;
	TSharedPtr<ShaderWorldGPUTools::SWShaderToolBox> STools;


	//Fix Crash on opening, assure renderthread is responsive
	double TimeAcum = 0.0;

	double CameUpdateAcu=0.0;
	float UpdateRateCameras = 20.0f;
	FRenderCommandFence RT_Ready;
	bool RenderThreadPoked = false;
	bool RenderThreadResponded = false;

	FRenderCommandFence AsyncReads;

public:
	UPROPERTY(Transient)
		EGeoRenderingAPI RendererAPI = EGeoRenderingAPI::DX11;

	UPROPERTY(Transient)
		FString RHIString = "";

	int32 TopologyFixUnderLOD = 5;
};



#if !WITH_EDITOR

#define SW_RT(Destination,GWorld,Res,Filt,Format)\
Destination = NewObject<UTextureRenderTarget2D>(GWorld);\
Destination->RenderTargetFormat = Format;\
Destination->ClearColor = FLinearColor(0, 0, 0, 1);\
Destination->bAutoGenerateMips = false;\
Destination->bCanCreateUAV = true;\
Destination->InitAutoFormat(Res, Res);\
Destination->bForceLinearGamma=true;\
Destination->ClearColor = FLinearColor(0.0f, 0.0f, 0.0f, 1.0f);\
Destination->SRGB = false;\
Destination->Filter = Filt;\
Destination->AddressX = TA_Clamp;\
Destination->AddressY = TA_Clamp;\
Destination->UpdateResourceImmediate(false);\

#else

#define SW_RT(Destination,GWorld,Res,Filt,Format)\
Destination = NewObject<UTextureRenderTarget2D>(GWorld);\
Destination->RenderTargetFormat = Format;\
Destination->ClearColor = FLinearColor(0, 0, 0, 1);\
Destination->bAutoGenerateMips = false;\
Destination->bCanCreateUAV = true;\
Destination->InitAutoFormat(Res, Res);\
Destination->bForceLinearGamma=true;\
Destination->ClearColor = FLinearColor(0.0f, 0.0f, 0.0f, 1.0f);\
Destination->SRGB = false;\
Destination->Filter = Filt;\
Destination->AddressX = TA_Clamp;\
Destination->AddressY = TA_Clamp;\
Destination->UpdateResourceImmediate(false);\

#endif



#define SW_UNROOT(A)\
	if(A && A->IsRooted())\
		A->RemoveFromRoot();\

#define SW_TOCOLLECTOR(A)\
		if(A){\
		if(UObject* Obj = A) Collector.AddReferencedObject(Obj);\
		}\



#if !WITH_EDITOR

#define SW_ROOT(A,W)\
	if(A && W)\
	{\
		A->AddToRoot();\
		if (W->WorldType == EWorldType::PIE)\
			A->AddToRoot();\
	}\

#else

#define SW_ROOT(A,W)\
	if(A && W)\
	{\
		if (W->WorldType == EWorldType::PIE)\
			A->AddToRoot();\
	}\

#endif

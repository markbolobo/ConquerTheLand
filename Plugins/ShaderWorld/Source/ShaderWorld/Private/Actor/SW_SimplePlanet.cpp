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

#include "Actor/SW_SimplePlanet.h"

#include "ShaderCompiler.h"
#include "SWorldSubsystem.h"

// Sets default values
ASW_SimplePlanet::ASW_SimplePlanet()
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
	RootComp = CreateDefaultSubobject<USWorldRoot>(TEXT("RootPrimComp"));
	RootComponent = RootComp;
}

// Called when the game starts or when spawned
void ASW_SimplePlanet::BeginPlay()
{
	Super::BeginPlay();
	
}

// Called every frame
void ASW_SimplePlanet::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);


	WorldManagement(DeltaTime);
	SpawnablesManagement(DeltaTime);
	CollisionManagement(DeltaTime);

}

void ASW_SimplePlanet::SpawnablesManagement(float& DeltaT)
{
}

void ASW_SimplePlanet::CollisionManagement(float& DeltaT)
{
}

bool ASW_SimplePlanet::Setup()
{
	if (GShaderCompilingManager && GShaderCompilingManager->IsCompiling())
	{
#if WITH_EDITOR
		FString CompilingMessage = "Shaders Compiling, Shader World update Frozen";
		if (GEngine)
			GEngine->AddOnScreenDebugMessage(INDEX_NONE, 0.f, FColor::Blue, CompilingMessage, false);
#endif

		return false;
	}

	if (USWorldSubsystem* ShaderWorldSubsystem = GetWorld()->GetSubsystem<USWorldSubsystem>())
	{
		if (!ShaderWorldSubsystem->IsReadyToHandleGPURequest())
			return false;
	}

	UpdateCameraLocation();

	if (RadiusKm != Radius_Creation || GroundResolution != GroundResolution_Creation || PatchSize != PatchSize_creation)
	{
		Rebuild = true;
	}

	if (Rebuild && ((FPlatformTime::Seconds() - Rebuild_last_time) > 0.35f))
	{
		Rebuild = false;

		Rebuild_last_time = FPlatformTime::Seconds();
		Radius_Creation = RadiusKm;
		GroundResolution_Creation = GroundResolution;
		PatchSize_creation = PatchSize;


		for (uint8 LODCount = 0; LODCount < 30; LODCount++)
		{
			if (((double)(1 << LODCount) * GroundResolution_Creation * (PatchSize_creation - 1)) < (2.0 * Radius_Creation * 1000.0 * 100.0))
			{
				LOD_Num = LODCount + 1;
				AdjusedRadiusMeter = Radius_Creation * 1000.0;
			}
			else
			{
				break;
			}
		}

	
	}

	return true;
}

void ASW_SimplePlanet::UpdateCameraLocation()
{

	UWorld* World = GetWorld();

	if (!World)
		return;

	if (USWorldSubsystem* ShaderWorldSubsystem = GetWorld()->GetSubsystem<USWorldSubsystem>())
	{
	

	}
}

void ASW_SimplePlanet::WorldManagement(float& DeltaT)
{
	if(!Setup())
		return;


}

#if WITH_EDITOR


bool ASW_SimplePlanet::ShouldTickIfViewportsOnly() const
{
	return true;
}

void ASW_SimplePlanet::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

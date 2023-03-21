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

#include "Component/SW_CollisionComponent.h"
#include "SWorldSubsystem.h"
#include "Actor/ShaderWorldActor.h"
#include "Kismet/GameplayStatics.h"

// Sets default values for this component's properties
USW_CollisionComponent::USW_CollisionComponent()
{
	// Set this component to be initialized when the game starts, and to be ticked every frame.  You can turn these features
	// off to improve performance if you don't need them.
	PrimaryComponentTick.bCanEverTick = false;

	// ...
}


// Called when the game starts
void USW_CollisionComponent::BeginPlay()
{
	Super::BeginPlay();

	// ...

	if(UWorld* World = GetWorld())
	{
		if (USWorldSubsystem* ShaderWorldSubsystem = GetWorld()->GetSubsystem<USWorldSubsystem>())
		{
			ShaderWorldSubsystem->TrackComponent(this);
			RegisteredToShaderWorld = true;
		}
	}
}

void USW_CollisionComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		if (USWorldSubsystem* ShaderWorldSubsystem = GetWorld()->GetSubsystem<USWorldSubsystem>())
		{
			ShaderWorldSubsystem->UnTrackComponent(this);
			RegisteredToShaderWorld=false;
		}
	}

	Super::EndPlay(EndPlayReason);
}


// Called every frame
void USW_CollisionComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// ...
	if(GetWorld() && GetWorld()->IsGameWorld())
	{
		if(!RegisteredToShaderWorld)
		{
			if (USWorldSubsystem* ShaderWorldSubsystem = GetWorld()->GetSubsystem<USWorldSubsystem>())
			{
				ShaderWorldSubsystem->TrackComponent(this);
				RegisteredToShaderWorld = true;
			}
		}
	}
#if WITH_EDITOR
	else if(GetWorld())
	{
		if (!RegisteredToShaderWorld)
		{
			if (USWorldSubsystem* ShaderWorldSubsystem = GetWorld()->GetSubsystem<USWorldSubsystem>())
			{
				ShaderWorldSubsystem->TrackComponent(this);
				RegisteredToShaderWorld = true;
			}
		}
	}
#endif
}


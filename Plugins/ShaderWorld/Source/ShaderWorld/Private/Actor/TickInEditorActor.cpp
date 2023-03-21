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

#include "Actor/TickInEditorActor.h"

// Sets default values
ATickInEditorActor::ATickInEditorActor()
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

}

#if WITH_EDITOR
bool ATickInEditorActor::ShouldTickIfViewportsOnly() const
{
	return true;
}
#endif


// Called when the game starts or when spawned
void ATickInEditorActor::BeginPlay()
{
	Super::BeginPlay();

	ResetBP();

	TimeAcu=0.f;
}

void ATickInEditorActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	ResetBP();

	TimeAcu = 0.f;

	if (EndPlayReason != EEndPlayReason::Type::EndPlayInEditor)
	{
		CleanAllDependencies();
		DestructionStarted = true;
	}
		

}

// Called every frame
void ATickInEditorActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if(!GetWorld() || DestructionStarted)
		return;

	if(Trigger)
	{
		Compute();
		Trigger=false;
	}

	TimeAcu += DeltaTime;

	if(TimeAcu > 1.f/25.f)
	{
		EditorCustomTicking(TimeAcu);
		TimeAcu=0.f;
	}

}


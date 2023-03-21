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
#include "Component/SWorldRoot.h"
#include "Component/SW_CollisionComponent.h"
#include "GameFramework/Actor.h"
#include "Data/SW_PointerQuadtree.h"
#include "SW_SimplePlanet.generated.h"



UCLASS(BlueprintType, Blueprintable, hideCategories(Rendering, Input, Actor, Game, LOD, Replication, Cooking, Collision, HLOD))
class SHADERWORLD_API ASW_SimplePlanet : public AActor
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	ASW_SimplePlanet();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	void SpawnablesManagement(float& DeltaT);
	void CollisionManagement(float& DeltaT);
	bool Setup();
	void UpdateCameraLocation();
	void WorldManagement(float& DeltaT);

public:


#if WITH_EDITOR

	bool ShouldTickIfViewportsOnly() const override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;

#endif


public:

	UPROPERTY(EditDefaultsOnly, Category = "")
		USWorldRoot* RootComp = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World Parameters")
		float RadiusKm = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World Parameters")
		float GroundResolution = 50.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World Parameters")
		int32 PatchSize = 65;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "World Settings", meta = (UIMin = 1, UIMax = 30, ClampMin = 1, ClampMax = 30))
		int32 LOD_Num = 8;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "World Settings")
		int32 AdjusedRadiusMeter = 12700;



private:

	bool Rebuild = false;
	double Rebuild_last_time = 0.0;

	float Radius_Creation = 0.f;
	float GroundResolution_Creation = 0.f;
	int32 PatchSize_creation = 0;

	double Adjusted_Radius = 0.0;

};

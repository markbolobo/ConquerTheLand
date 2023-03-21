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
#include "GameFramework/Actor.h"
#include "Data/SWorldConfig.h"
#include "Storage/SWStorageBase.h"
#include "SWorld.generated.h"

class USWorldRoot;

UCLASS(BlueprintType, Blueprintable, hideCategories(Rendering, Input, Actor, Game, LOD, Replication, Cooking, Collision, HLOD))
class SHADERWORLD_API ASWorld : public AActor
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	ASWorld();

	UPROPERTY(EditDefaultsOnly, Category = "")
		USWorldRoot* RootComp = nullptr;

	UPROPERTY(EditAnywhere, Instanced, Category = "")
		USWContextBase* Context;

	UPROPERTY(EditAnywhere, Instanced, Category = "")
		USWStorageBase* Storage;

#if WITH_EDITORONLY_DATA
	UPROPERTY(EditDefaultsOnly, Category = "")
		UHierarchicalInstancedStaticMeshComponent* SimpleTile = nullptr;
#endif

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;
	virtual void PostRegisterAllComponents() override;
	virtual void UnregisterAllComponents(bool bForReregister = false) override;

};

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
#include "HeightMapImporter.generated.h"

UCLASS(hideCategories(Transform,Collision,Rendering, Input,Actor, Game, LOD, Replication, Cooking))
class SHADERWORLD_API AHeightMapImporter : public AActor
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	AHeightMapImporter();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

#if WITH_EDITOR

	bool ShouldTickIfViewportsOnly() const override;
	
	
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "1) Locate")
		void LocateHeightmapFile();
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "3) Import")
		void Import();

		void ImportHeightmap();
#endif

	UPROPERTY(Transient)
		bool bTriggerHeightmapImport=false;
	UPROPERTY(Transient)
		bool bDisableDuringGame = false;

	UPROPERTY(Transient,BlueprintReadWrite, EditAnywhere, Category = "2) Import Settings")
		FString HeightMapToImport;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "2) Import Settings")
		FIntPoint HeightMapDimensionIfRaw;

	UPROPERTY(EditAnywhere, Category = "2) Import Settings")
		FString HeightmapFolder;
	UPROPERTY(EditAnywhere, Category = "2) Import Settings")
		FString HeightmapName;

	TMap<FIntPoint, uint16> SelectedData;

	

	void Import(int32 VertsX, int32 VertsY, uint16* HeightData);

	void SampleData(int32 SizeX, int32 SizeY);

	static uint16 convertFrom8To16(uint8 dataFirst, uint8 dataSecond);
	static uint8* convertFrom16To8(uint16 dataAll);


};

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
#include "Actor/ShaderWorldBrush.h"
#include "ShaderWorldPaintableBrush.generated.h"


UCLASS(hideCategories(Collision, Input,Actor, Game, LOD, Replication, Cooking))
class SHADERWORLD_API AShaderWorldPaintableBrush : public AShaderWorldBrush
{
	GENERATED_UCLASS_BODY()
	
public:	
	// Sets default values for this actor's properties
	AShaderWorldPaintableBrush();

#if WITH_EDITOR

	bool ShouldTickIfViewportsOnly() const override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;

#endif

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HeightPainter")
		int RenderTargetDimension = 1024;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HeightPainter")
		float WorldDimensionMeters = 50.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HeightPainter")
		float CentimetersPerTexel = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HeightPainter")
		UMaterialInterface* HeightPainterMaterial = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HeightPainter")
		UMaterialInterface* ForceSplatMaterial = nullptr;

protected:

	UPROPERTY(Transient)
		UTextureRenderTarget2D* RT_A = nullptr;
	UPROPERTY(Transient)
		UTextureRenderTarget2D* RT_B = nullptr;

	UPROPERTY(Transient)
		bool Recreate_RenderTarget = false;

	// Called when the game starts or when spawned
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	static void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	void ApplyBrushAt(UTextureRenderTarget2D* Destination_RT,UTextureRenderTarget2D* Source_RT,float LayerInfluence,float BrushInfluence, FVector RingLocation, int32 GridScaling, int N,bool CollisionMesh, bool IsLayer, bool IsReadback = false, UTextureRenderTarget2D* Location_RT = nullptr) override;
	void ResetB(bool LayerEnabled,bool BrushEnabled, float LayerInfluence,float BrushInfluence) override;
};

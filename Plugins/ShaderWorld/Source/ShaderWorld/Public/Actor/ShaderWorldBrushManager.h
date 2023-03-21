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
#include "ShaderWorldBrushManager.generated.h"

class AShaderWorldActor;
class AShaderWorldBrushManager;

USTRUCT(BlueprintType)
struct FBrushElement
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Default")
		bool Enabled=true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Default", meta = (UIMin = 0.f, UIMax = 1.f, ClampMin = 0.f, ClampMax = 1.f))
		float Influence = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Default")
		AShaderWorldBrush* Brush = nullptr;

		bool IsValid();

	UPROPERTY(Transient)
		AShaderWorldBrushManager* BrushManagerOwner = nullptr;

		FBrushElement()=default;

		FBrushElement(AShaderWorldBrush* Brush_, AShaderWorldBrushManager* Manager)
		:Enabled(true)
		, Influence(1.f)
		, Brush(Brush_)
		, BrushManagerOwner(Manager)
		{}

		bool operator==(const FBrushElement& Rhs) const {return (Brush == Rhs.Brush); };
		
		~FBrushElement();
};

USTRUCT(BlueprintType)
struct FBrushLayer
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Default")
		bool Enabled=true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Default", meta = (UIMin = 0.f, UIMax = 1.f, ClampMin = 0.f, ClampMax = 1.f))
		float Influence = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Default")
		TArray<FBrushElement> Brushes;

	UPROPERTY(Transient)
		AShaderWorldBrushManager* BrushManagerOwner=nullptr;

	~FBrushLayer();

};

USTRUCT()
struct FHeightMapSourceElement
{
	GENERATED_BODY()

	public:
	UPROPERTY(Transient)
		bool CollisionRT=false;

	UPROPERTY(Transient)
		UMaterialInstanceDynamic* HeightmapToLocalRT= nullptr;

};

UCLASS(hideCategories(Collision, Input,Actor, Game, LOD, Replication, Cooking))
class SHADERWORLD_API AShaderWorldBrushManager : public AActor
{
	GENERATED_BODY()

public:
	// Sets default values for this actor's properties
	AShaderWorldBrushManager();

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Serialize(FArchive& Ar) override;
	static void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);

	/*
	* Do NOT add runtime spawned Brushes into those layers, use RuntimeDynamicBrushLayers instead
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LayerStack")
		TArray<FBrushLayer> BrushLayers;
	/*
	* Add Runtime spawned Brushes here
	*/
	UPROPERTY(Transient,EditAnywhere, BlueprintReadWrite, Category = "RuntimeDynamic")
		TArray<FBrushLayer> RuntimeDynamicBrushLayers;

#if WITH_EDITOR

	bool ShouldTickIfViewportsOnly() const override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;

#endif

	UPROPERTY(Transient)
		TMap<int32,int32> DimensionToID;
	UPROPERTY(Transient)
		TArray<UTextureRenderTarget2D*> RenderTargetPool;

	UPROPERTY(Transient)
		UTextureRenderTarget2D* WorkRT = nullptr;
	UPROPERTY(Transient)
		UTextureRenderTarget2D* CollisionWorkRT = nullptr;

	UPROPERTY(Transient)
		UTextureRenderTarget2D* ReadBackRT = nullptr;
	
	UPROPERTY(Transient)
		UTextureRenderTarget2D* LayerRT = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Required", meta = (UIMin = 1.f, UIMax = 60.f, ClampMin = 1.f, ClampMax = 60.f))
		float RedrawCheckPerSecond = 35.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Required", meta = (UIMin = 1, UIMax = 12, ClampMin = 1, ClampMax = 12))
		uint8 IncludeBlueprintUpdateEvery = 3;
	UPROPERTY(Transient)
		float TimeAcu = 0.f;
	UPROPERTY(Transient)
		float TimeAcuReorder = 0.f;

	UPROPERTY(Transient)
		float TimeAcuRemoveUnused = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Required")
		AShaderWorldActor* ShaderWorldOwner = nullptr;

	void ResetB();

	void ApplyBrushStackToHeightMap(AShaderWorldActor* SourceWorld,int level, UTextureRenderTarget2D* Heightmap_RT, FVector RingLocation, int32 GridScaling, int& N, bool CollisionMesh, bool ReadBack=false, UTextureRenderTarget2D* Location_RT = nullptr);
	void ApplyBrushStackToLayer(AShaderWorldActor* SourceWorld,int level, UTextureRenderTarget2D* Layer_RT, FVector RingLocation, int32& GridScaling, int& N, FString& LayerName);

	void ApplyStackForFootprint(FBox2D FootPrint, UTextureRenderTarget2D* Heightmap_RT, FVector RingLocation, int32 GridScaling, int N, bool CollisionMesh, bool IsLayer, FString& LayerName, bool ReadBack = false, UTextureRenderTarget2D* Location_RT = nullptr);

	void AddExogeneReDrawBox(FBox2D Box){ExogeneReDrawBox.Add(Box);};

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

private:
	bool ForceRedraw=false;
	TArray<FBox2D> ExogeneReDrawBox;
	uint8 BPUpdateCounter = 0;

public:
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	bool EndPlayCalled=false;
};

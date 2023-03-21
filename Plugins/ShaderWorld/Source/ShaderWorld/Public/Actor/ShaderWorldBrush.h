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
#include "Engine/VolumeTexture.h"
#include "ShaderWorldBrush.generated.h"

class UBoxComponent;

UENUM(BlueprintType)
enum class EBrushParameterType : uint8
{
	Float UMETA(DisplayName = "Float"),
	Vector UMETA(DisplayName = "Vector"),
	Texture2D UMETA(DisplayName = "Texture2D"),
	Texture3D UMETA(DisplayName = "Texture3D"),
};

USTRUCT(BlueprintType)
struct FScalarBrushParameter
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Default")
		float float_value = 0.f;

	UPROPERTY(Transient)
		float float_value_past = 0.f;

	UPROPERTY(Transient)
		bool SentToMaterial=false;

	UPROPERTY(Transient)
		FString Name_past = "";

		bool IsValidParameter(){return true;};
};

USTRUCT(BlueprintType)
struct FVectorBrushParameter
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Default")
		FVector4 Vector_value = FVector4(0.f, 0.f, 0.f, 0.f);

	UPROPERTY(Transient)
		FVector4 Vector_value_past = FVector4(0.f, 0.f, 0.f, 0.f);

	UPROPERTY(Transient)
		bool SentToMaterial = false;

	UPROPERTY(Transient)
		FString Name_past = "";

	bool IsValidParameter() { return true; };
};

USTRUCT(BlueprintType)
struct FTextureBrushParameter
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Default")
		TObjectPtr < UTexture2D> Texture2D_value = nullptr;
	
	UPROPERTY(Transient)
		TObjectPtr < UTexture2D> Texture2D_value_past = nullptr;

	UPROPERTY(Transient)
		bool SentToMaterial = false;

	UPROPERTY(Transient)
		FString Name_past = "";

	bool IsValidParameter() { return Texture2D_value?true:false; };
};

USTRUCT(BlueprintType)
struct FVolumeTextureBrushParameter
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Default")
		UVolumeTexture* Texture3D_value = nullptr;

	UPROPERTY(Transient)
		UVolumeTexture* Texture3D_value_past = nullptr;

	UPROPERTY(Transient)
		bool SentToMaterial = false;

	UPROPERTY(Transient)
		FString Name_past = "";

	bool IsValidParameter() { return Texture3D_value ? true : false; };
};
UCLASS(hideCategories(Rendering, HLOD, NetWorking, Physics, Collision, Input, Game, LOD, Replication, Cooking))
class SHADERWORLD_API AShaderWorldBrush : public AActor
{
	GENERATED_UCLASS_BODY()
	
public:	
	// Sets default values for this actor's properties
	AShaderWorldBrush();

	UPROPERTY(EditAnywhere, Category = "Default")
	UBoxComponent* BoxBound;

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	static void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);

	UPROPERTY(Transient,EditAnywhere, BlueprintReadWrite, Category = "")
		bool RedrawNeed = true;
	/*
	 * Higher Priority are drawn on top of others
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "")
		uint8 Priority = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "")
		TMap<FString, FScalarBrushParameter> BrushScalarParameters;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "")
		TMap<FString, FVectorBrushParameter> BrushVectorParameters;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "")
		TMap<FString, FTextureBrushParameter> BrushTextureParameters;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "")
		TMap<FString, FVolumeTextureBrushParameter> BrushVolumeTextureParameters;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "")
		TObjectPtr < UMaterialInterface> BrushMaterial = nullptr;
	UPROPERTY(Transient)
		TObjectPtr < UMaterialInstanceDynamic> BrushMaterialDyn = nullptr;


	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer Write")
		bool DrawToLayer = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer Write",meta = (EditCondition = DrawToLayer))
		FString NameOfLayerTarget = "";
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer Write",meta = (EditCondition = DrawToLayer))
		TObjectPtr < UMaterialInterface> LayerBrushMaterial = nullptr;
	UPROPERTY(Transient)
		TObjectPtr < UMaterialInstanceDynamic> LayerBrushMaterialDyn = nullptr;



#if WITH_EDITOR

	bool ShouldTickIfViewportsOnly() const override;

#endif

	UFUNCTION(BlueprintCallable,Category = "Default")
		void SetBrushParameter(EBrushParameterType BrushType,FString ParameterName,FVector4 vector_value,float float_value = 0.f,UTexture2D* texture_value = nullptr,UVolumeTexture* Volume_Texture_value = nullptr);

	UFUNCTION(BlueprintImplementableEvent,CallInEditor, Category = "Recurrent Events")
		void ResetBrush();

	/**
	* If your brush is using blueprint code, you can implement this method in blueprint to determine if 'RedrawNeed' should be set to true or not
	*/
	UFUNCTION(BlueprintImplementableEvent,CallInEditor, Category = "Recurrent Events")
		void DoesBrushNeedRedraw();

	/**
	* Can be nullptr
	*/
	UFUNCTION(BlueprintCallable, Category = "WorldData")
		UMaterialInstanceDynamic* GetBrushDynamicMaterial();

	/**
	* Can be nullptr
	*/
	UFUNCTION(BlueprintCallable, Category = "WorldData")
		UMaterialInstanceDynamic* GetLayerBrushDynamicMaterial();

	FBox2D GetBrushFootPrint();



	/**
	* For c++ brushes override those functions
	*/
	virtual bool NeedRedraw(bool LayerEnabled,bool BrushEnabled, float LayerInfluence,float BrushInfluence, bool IncludeBP);	
	void SetRedrawNeed();
	/**
	* For c++ brushes override those functions
	*/
	virtual void ResetB(bool LayerEnabled,bool BrushEnabled, float LayerInfluence,float BrushInfluence);
	/**
	* For c++ brushes override those functions
	*/
	virtual void ApplyBrushAt(UTextureRenderTarget2D* Destination_RT,UTextureRenderTarget2D* Source_RT,float LayerInfluence,float BrushInfluence, FVector RingLocation, int32 GridScaling, int N,bool CollisionMesh, bool IsLayer, bool IsReadback=false, UTextureRenderTarget2D* Location_RT=nullptr);
	
	virtual bool IsValidBrush();

	void SetLastDrawnFootPrint(FBox2D& FootPrint);

	FBox2D& GetLastDrawFootprint(){return FootPrintWhenLastDrawn;}
	
protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

	bool	EndPlayTriggered=false;

	bool	Layer_Enabled=true;
	bool	Brush_Enabled=true;

	float Influence_Layer_Material=1.f;
	float Influence_Brush_Material=1.f;
	
	FVector BrushLocation_Material = FVector(0.f,0.f,0.f);
	FVector BrushLocation = FVector(0.f,0.f,0.f);

	bool Force_Layer_influcence_update = false;
	bool Force_Brush_influcence_update = false;
	bool Force_Position_update = false;


	FBox2D FootPrintWhenLastDrawn = FBox2D(ForceInit);

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

};

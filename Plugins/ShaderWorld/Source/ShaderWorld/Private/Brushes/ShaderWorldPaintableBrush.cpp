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

#include "Brushes/ShaderWorldPaintableBrush.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Components/BoxComponent.h"
#include "Kismet/KismetRenderingLibrary.h"

#include "SWorldSubsystem.h"

// Sets default values
AShaderWorldPaintableBrush::AShaderWorldPaintableBrush(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = false;
}

#if WITH_EDITOR
bool AShaderWorldPaintableBrush::ShouldTickIfViewportsOnly() const
{
	return true;
}

#endif

// Called when the game starts or when spawned
void AShaderWorldPaintableBrush::BeginPlay()
{
	Super::BeginPlay();
}


#if WITH_EDITOR
void AShaderWorldPaintableBrush::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	FProperty* Property = PropertyChangedEvent.MemberProperty;

	if (Property && PropertyChangedEvent.ChangeType != EPropertyChangeType::Interactive)
	{
		FString PropName = PropertyChangedEvent.Property->GetName();

		if (PropName == TEXT("RenderTargetDimension") ||
			PropName == TEXT("WorldDimensionMeters"))
		{
			Recreate_RenderTarget = true;
		}
		
		if(PropName == TEXT("WorldDimensionMeters"))
		{
			BoxBound->SetBoxExtent(FVector(WorldDimensionMeters,WorldDimensionMeters,100.f));
			SetActorScale3D(FVector(1.f,1.f,1.f));
		}
		
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

void AShaderWorldPaintableBrush::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);
}

void AShaderWorldPaintableBrush::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
	Super::AddReferencedObjects(InThis, Collector);

	const AShaderWorldPaintableBrush* This = CastChecked<AShaderWorldPaintableBrush>(InThis);

	if (!This)
		return;

	SW_TOCOLLECTOR(This->RT_A)
	SW_TOCOLLECTOR(This->RT_B)
}

// Called every frame
void AShaderWorldPaintableBrush::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);


}

void AShaderWorldPaintableBrush::ApplyBrushAt(UTextureRenderTarget2D* Destination_RT,UTextureRenderTarget2D* Source_RT,float LayerInfluence,float BrushInfluence, FVector RingLocation, int32 GridScaling, int N,bool CollisionMesh, bool IsLayer, bool IsReadback, UTextureRenderTarget2D* Location_RT)
{
	UWorld* World = GetWorld();

	if(Recreate_RenderTarget)
	{
		RT_A=nullptr;
		RT_B=nullptr;
	}

	if (!RT_A && World)
	{
		SW_RT(RT_A, World, RenderTargetDimension, TF_Nearest, RTF_RGBA8)
	}

	if (!RT_B && World)
	{
		SW_RT(RT_B, World, RenderTargetDimension, TF_Nearest, RTF_RGBA8)
	}

	Super::ApplyBrushAt( Destination_RT, Source_RT, LayerInfluence, BrushInfluence,  RingLocation,  GridScaling,  N, CollisionMesh,  IsLayer);
}

void AShaderWorldPaintableBrush::ResetB(bool LayerEnabled,bool BrushEnabled, float LayerInfluence,float BrushInfluence)
{
	RT_A = nullptr;
	RT_B = nullptr;

	Super::ResetB(LayerEnabled,BrushEnabled,LayerInfluence,BrushInfluence);

}


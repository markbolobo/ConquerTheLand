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

#include "Actor/ShaderWorldBrushManager.h"

#include "SWorldSubsystem.h"
#include "SWStats.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Actor/ShaderWorldActor.h"

// Sets default values
AShaderWorldBrushManager::AShaderWorldBrushManager()
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

}

void AShaderWorldBrushManager::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	EndPlayCalled = true;

	RuntimeDynamicBrushLayers.Empty();

	Super::EndPlay(EndPlayReason);
}

void AShaderWorldBrushManager::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	
}

void AShaderWorldBrushManager::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
	Super::AddReferencedObjects(InThis, Collector);

	const AShaderWorldBrushManager* This = CastChecked<AShaderWorldBrushManager>(InThis);

	if (!This)
		return;

	SW_TOCOLLECTOR(This->WorkRT)
	SW_TOCOLLECTOR(This->CollisionWorkRT)
	SW_TOCOLLECTOR(This->ReadBackRT)
		
	SW_TOCOLLECTOR(This->LayerRT)
	for (auto& El : This->RenderTargetPool)
	{
		SW_TOCOLLECTOR(El)
	}
}

#if WITH_EDITOR
bool AShaderWorldBrushManager::ShouldTickIfViewportsOnly() const
{
	return true;
}

void AShaderWorldBrushManager::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	FProperty* Property = PropertyChangedEvent.MemberProperty;

	
	if (Property && PropertyChangedEvent.ChangeType != EPropertyChangeType::Interactive)
	{
		TimeAcuRemoveUnused = 0.f;

		FString PropName = PropertyChangedEvent.Property->GetName();

		if (PropName == TEXT("BrushLayers"))
		{
			//ForceRedraw = true;
		}		
	}

	for(auto& L: BrushLayers)
	{
		Algo::Sort(L.Brushes, [](const FBrushElement& A, const FBrushElement& B)
		{
			if(!A.Brush)
				return false;
			if (!B.Brush)
				return true;
			return A.Brush->Priority <= B.Brush->Priority;
		});
	}
	Super::PostEditChangeProperty(PropertyChangedEvent);
}

#endif


void AShaderWorldBrushManager::ResetB()
{

	WorkRT = nullptr;
	CollisionWorkRT = nullptr;
	ReadBackRT = nullptr;
	LayerRT = nullptr;

	RenderTargetPool.Empty();
	DimensionToID.Empty();

	ForceRedraw = false;
	ExogeneReDrawBox.Empty();
	BPUpdateCounter = 0;

	for (FBrushLayer& Layer : BrushLayers)
	{		
		for(int32 b_ID = Layer.Brushes.Num()-1; b_ID>=0; b_ID--)
		{
			FBrushElement& BrushEl = Layer.Brushes[b_ID];
			if(!IsValid(BrushEl.Brush))
				Layer.Brushes.RemoveAt(b_ID);
			else
		{			
			if(BrushEl.Brush)
				BrushEl.Brush->ResetB(Layer.Enabled,BrushEl.Enabled, Layer.Influence,BrushEl.Influence);
		}			
	}
}
}

void AShaderWorldBrushManager::ApplyBrushStackToHeightMap(AShaderWorldActor* SourceWorld,int LOD, UTextureRenderTarget2D* Heightmap_RT, FVector RingLocation, int32 GridScaling, int& N,bool CollisionMesh, bool ReadBack, UTextureRenderTarget2D* Location_RT)
{
	if(!SourceWorld || !Heightmap_RT)
	{
		return;	
	}

	if(!ShaderWorldOwner || ShaderWorldOwner!=SourceWorld)
	{
		ShaderWorldOwner = SourceWorld;	
	}

	if(Heightmap_RT)
	{
		UWorld* World = GetWorld();

		if(!DimensionToID.Contains(Heightmap_RT->SizeX))
		{
			const int32 IDInPool = RenderTargetPool.Num();

			UTextureRenderTarget2D* NewRT = nullptr;
			SW_RT(NewRT, GetWorld(), Heightmap_RT->SizeX, TF_Nearest, RTF_RGBA8)

			RenderTargetPool.Add(NewRT);
			DimensionToID.Add(Heightmap_RT->SizeX, IDInPool);
		}



	
		if(!CollisionMesh /* && !WorkRT */ )
		{
			if (ShaderWorldDebug != 0)
			{
				UE_LOG(LogTemp, Warning, TEXT("AShaderWorldBrushManager Create WorkRT"));
			}

			WorkRT = RenderTargetPool[(*DimensionToID.Find(Heightmap_RT->SizeX))];

		}
		if (CollisionMesh /* && !CollisionWorkRT */ )
		{
			if (ShaderWorldDebug != 0)
			{
				UE_LOG(LogTemp, Warning, TEXT("AShaderWorldBrushManager Create CollisionWorkRT"));
			}

			CollisionWorkRT = RenderTargetPool[(*DimensionToID.Find(Heightmap_RT->SizeX))];

		}
		if(ReadBack)
		{
			if (ShaderWorldDebug != 0)
			{
				UE_LOG(LogTemp, Warning, TEXT("AShaderWorldBrushManager Create CollisionWorkRT"));
			}

			ReadBackRT = RenderTargetPool[(*DimensionToID.Find(Heightmap_RT->SizeX))];
		}


		FVector2D Location(RingLocation.X, RingLocation.Y);
		float Size = GridScaling * (N - 1) / 2.0;

		FBox2D FootPrintRT(Location - Size * FVector2D(1.f, 1.f), Location + Size * FVector2D(1.f, 1.f));

		FString NoName = "";
		ApplyStackForFootprint(FootPrintRT, Heightmap_RT, RingLocation, GridScaling, N, CollisionMesh,false,NoName,ReadBack,Location_RT);
		
	}
	else
	{
		UE_LOG(LogTemp,Warning,TEXT("AShaderWorldBrushManager Heightmap_RT && HeightmapCopyPostWork FALSE"));
	}
}

void AShaderWorldBrushManager::ApplyBrushStackToLayer(AShaderWorldActor* SourceWorld, int LOD, UTextureRenderTarget2D* Layer_RT, FVector RingLocation, int32& GridScaling, int& N, FString& LayerName)
{
	if (!SourceWorld || !Layer_RT)
	{
		return;
	}

	
	{
		UWorld* World = GetWorld();

		if (!DimensionToID.Contains(Layer_RT->SizeX))
		{
			const int32 IDInPool = RenderTargetPool.Num();

			UTextureRenderTarget2D* NewRT = nullptr;
			SW_RT(NewRT, GetWorld(), Layer_RT->SizeX, TF_Nearest, RTF_RGBA8)

			RenderTargetPool.Add(NewRT);
			DimensionToID.Add(Layer_RT->SizeX, IDInPool);
		}

		LayerRT = RenderTargetPool[(*DimensionToID.Find(Layer_RT->SizeX))];
		/*
		if (!LayerRT)
		{
			
			if (ShaderWorldDebug != 0)
			{
				UE_LOG(LogTemp,Warning,TEXT("AShaderWorldBrushManager Create LayerRT"));
			}

			SW_RT(LayerRT, GetWorld(), Layer_RT->SizeX, TF_Nearest)

		}
		*/
		/*
		if (LayerRT)
			UKismetRenderingLibrary::ClearRenderTarget2D(this, LayerRT, FLinearColor::Black);
			*/

		FVector2D Location(RingLocation.X, RingLocation.Y);
		float Size = GridScaling * (N - 1) / 2.0;

		FBox2D FootPrintRT(Location - Size * FVector2D(1.f, 1.f), Location + Size * FVector2D(1.f, 1.f));

		ApplyStackForFootprint(FootPrintRT, Layer_RT, RingLocation, GridScaling, N, false,true,LayerName);

	}
}

void AShaderWorldBrushManager::ApplyStackForFootprint(FBox2D FootPrint , UTextureRenderTarget2D* Heightmap_RT, FVector RingLocation, int32 GridScaling, int N,bool CollisionMesh, bool IsLayer, FString& LayerName, bool ReadBack, UTextureRenderTarget2D* Location_RT)
{
	if(!Heightmap_RT || CollisionMesh && !CollisionWorkRT|| (!CollisionMesh && !ReadBack && !WorkRT) || (ReadBack && !ReadBackRT) || IsLayer && !LayerRT)
	{
		UE_LOG(LogTemp,Warning,TEXT("ERROR ApplyStackForFootprint OUT !Heightmap_RT || CollisionMesh && !CollisionWorkRT|| !CollisionMesh && !WorkRT|| Layer && !LayerRT"));
		return;
	}
	

	uint8 Altern = 0;

	for(FBrushLayer& Layer : BrushLayers)
	{
		if(Layer.Enabled && Layer.Influence>0.001f)
		{
			if(Layer.BrushManagerOwner!=this)
				Layer.BrushManagerOwner=this;

			for (FBrushElement& BrushEl : Layer.Brushes)
			{
				if(BrushEl.Brush)
					BrushEl.Brush->RedrawNeed=false;

				FBox2D BrushFootPrint(ForceInit);

				if(BrushEl.Enabled && BrushEl.IsValid())
					BrushFootPrint = BrushEl.Brush->GetBrushFootPrint();

				if(BrushEl.Enabled && BrushEl.Influence>0.001f && BrushEl.IsValid() && FootPrint.Intersect(BrushFootPrint) && (!IsLayer || IsLayer && BrushEl.Brush->DrawToLayer && BrushEl.Brush->NameOfLayerTarget == LayerName))
				{
					if (BrushEl.BrushManagerOwner != this)
						BrushEl.BrushManagerOwner = this;

					UTextureRenderTarget2D* Dest = Altern == 0 ? (IsLayer?LayerRT:(CollisionMesh ? CollisionWorkRT : (ReadBack? ReadBackRT:WorkRT))) : Heightmap_RT;
					UTextureRenderTarget2D* Src = Altern == 0 ? Heightmap_RT : (IsLayer?LayerRT:(CollisionMesh ? CollisionWorkRT : (ReadBack ? ReadBackRT : WorkRT)));

					BrushEl.Brush->ApplyBrushAt(Dest, Src,Layer.Influence,BrushEl.Influence, RingLocation, GridScaling, N, CollisionMesh,IsLayer, ReadBack, Location_RT);

					BrushEl.Brush->SetLastDrawnFootPrint(BrushFootPrint);
					
					Altern = Altern == 0 ? 1 : 0;				
				}

			}		
		}
	}

	for (FBrushLayer& Layer : RuntimeDynamicBrushLayers)
	{
		if (Layer.Enabled && Layer.Influence > 0.001f)
		{
			if (Layer.BrushManagerOwner != this)
				Layer.BrushManagerOwner = this;

			for (FBrushElement& BrushEl : Layer.Brushes)
			{
				if (BrushEl.Brush)
						BrushEl.Brush->RedrawNeed = false;

				FBox2D BrushFootPrint(ForceInit);

				if (BrushEl.Enabled && BrushEl.IsValid())
					BrushFootPrint = BrushEl.Brush->GetBrushFootPrint();

				if (BrushEl.Enabled && BrushEl.Influence > 0.001f && BrushEl.IsValid() && FootPrint.Intersect(BrushFootPrint) && (!IsLayer || IsLayer && BrushEl.Brush->DrawToLayer && BrushEl.Brush->NameOfLayerTarget == LayerName))
				{
					if (BrushEl.BrushManagerOwner != this)
						BrushEl.BrushManagerOwner = this;

					UTextureRenderTarget2D* Dest = Altern == 0 ? (IsLayer ? LayerRT : (CollisionMesh ? CollisionWorkRT : (ReadBack ? ReadBackRT : WorkRT))) : Heightmap_RT;
					UTextureRenderTarget2D* Src = Altern == 0 ? Heightmap_RT : (IsLayer ? LayerRT : (CollisionMesh ? CollisionWorkRT : (ReadBack ? ReadBackRT : WorkRT)));

					BrushEl.Brush->ApplyBrushAt(Dest, Src, Layer.Influence, BrushEl.Influence, RingLocation, GridScaling, N, CollisionMesh, IsLayer, ReadBack, Location_RT);

					BrushEl.Brush->SetLastDrawnFootPrint(BrushFootPrint);

					Altern = Altern == 0 ? 1 : 0;
				}
				else
				{
					
				}

			}
		}
	}

	if (Altern == 1)
	{
		if (USWorldSubsystem* ShaderWorldSubsystem = GetWorld()->GetSubsystem<USWorldSubsystem>())
			ShaderWorldSubsystem->CopyAtoB(IsLayer ? LayerRT : (CollisionMesh ? CollisionWorkRT : (ReadBack ? ReadBackRT : WorkRT)), Heightmap_RT, 0);
	}
}

// Called when the game starts or when spawned
void AShaderWorldBrushManager::BeginPlay()
{
	Super::BeginPlay();
	
	RuntimeDynamicBrushLayers.Empty();
	
}

// Called every frame
void AShaderWorldBrushManager::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	TimeAcuRemoveUnused += DeltaTime;
	if (TimeAcuRemoveUnused >= 20.f)
	{
		TimeAcuRemoveUnused=0.f;
		for (auto& Layer : BrushLayers)
		{
			for (int32 b_ID = Layer.Brushes.Num() - 1; b_ID >= 0; b_ID--)
			{
				FBrushElement& BrushEl = Layer.Brushes[b_ID];
				if (!IsValid(BrushEl.Brush))
					Layer.Brushes.RemoveAt(b_ID);
			}
		}
	}

	TimeAcuReorder+= DeltaTime;

	if(TimeAcuReorder>=1.5f)
	{
		TimeAcuReorder=0.f;

		for (auto& L : BrushLayers)
		{
			Algo::Sort(L.Brushes, [](const FBrushElement& A, const FBrushElement& B)
				{
					if (!A.Brush)
						return false;
					if (!B.Brush)
						return true;
					return A.Brush->Priority <= B.Brush->Priority;
				});
		}
	}
	TimeAcu+=DeltaTime;

	if(TimeAcu>1.0f/(RedrawCheckPerSecond<1.f?1.f:RedrawCheckPerSecond))
	{
		TimeAcu=0.f;
		bool IncludeBlueprintInUpdate = false;
		BPUpdateCounter++;
		if(BPUpdateCounter>=IncludeBlueprintUpdateEvery)
		{
			IncludeBlueprintInUpdate=true;
			BPUpdateCounter=0;
		}


		if(ShaderWorldOwner && (ShaderWorldOwner->GetMeshNum()>0) && ShaderWorldOwner->BrushManagerRedrawScopes.Num() >= 100)
		{
			SW_LOG("BrushManager : ShaderWorldOwner->BrushManagerRedrawScopes.Num() >= 100 %d", ShaderWorldOwner->BrushManagerRedrawScopes.Num())
		}

		if (ShaderWorldOwner && (ShaderWorldOwner->GetMeshNum() <= 0))
		{
			for (FBrushLayer& Layer : BrushLayers)
			{
				if (Layer.BrushManagerOwner != this)
					Layer.BrushManagerOwner = this;

				for (FBrushElement& BrushEl : Layer.Brushes)
				{
					if (BrushEl.BrushManagerOwner != this)
						BrushEl.BrushManagerOwner = this;

					if (BrushEl.IsValid())
						BrushEl.Brush->RedrawNeed = false;
				}
			}
		}
		else if(ShaderWorldOwner && (ShaderWorldOwner->GetMeshNum() > 0) && ShaderWorldOwner->BrushManagerRedrawScopes.Num()<100)
		{
			//Lets check if any of the brush require the owner to update the terrain, and warn it if this is the case

			TArray<FBox2D> RedrawScope;

			FBox2D WorldFootprint = ShaderWorldOwner->GetHighestLOD_FootPrint();
			bool WorldVisible = ShaderWorldOwner->HighestLOD_Visible();
			
			if(!WorldFootprint.bIsValid || !WorldVisible)
				return;

			bool RequireUpdate = false;
			for (FBrushLayer& Layer : BrushLayers)
			{			
				if (Layer.BrushManagerOwner != this)
					Layer.BrushManagerOwner = this;

				for (FBrushElement& BrushEl : Layer.Brushes)
				{
					if (BrushEl.BrushManagerOwner != this)
						BrushEl.BrushManagerOwner = this;

					if(BrushEl.IsValid())
					{
						FBox2D BrushFootPrint = BrushEl.Brush->GetBrushFootPrint();

						//i need to check that the brush is within highest LOD boundaries otherwise it wont be processed and we re wasting time
						if (WorldFootprint.Intersect(BrushFootPrint))
						{

							if (BrushEl.Brush->NeedRedraw(Layer.Enabled, BrushEl.Enabled, Layer.Influence, BrushEl.Influence, IncludeBlueprintInUpdate))
							{
								RequireUpdate = true;

								RedrawScope.Add(BrushFootPrint);

								if (BrushEl.Brush->GetLastDrawFootprint().bIsValid)
									RedrawScope.Add(BrushEl.Brush->GetLastDrawFootprint());
							}
							BrushEl.Brush->RedrawNeed = false;
						}
						else
						{
							BrushEl.Brush->RedrawNeed = false;
						}
						
					}

				}				
			}
			for (FBrushLayer& Layer : RuntimeDynamicBrushLayers)
			{
				if (Layer.BrushManagerOwner != this)
					Layer.BrushManagerOwner = this;

				for (FBrushElement& BrushEl : Layer.Brushes)
				{

					if (BrushEl.BrushManagerOwner != this)
						BrushEl.BrushManagerOwner = this;

					if (BrushEl.IsValid())
					{
						FBox2D BrushFootPrint = BrushEl.Brush->GetBrushFootPrint();

						//i need to check that the brush is within highest LOD boundaries otherwise it wont be processed and we re wasting time
						if (WorldFootprint.Intersect(BrushFootPrint))
						{
							if (BrushEl.Brush->NeedRedraw(Layer.Enabled, BrushEl.Enabled, Layer.Influence, BrushEl.Influence, IncludeBlueprintInUpdate))
							{
								RequireUpdate = true;

								RedrawScope.Add(BrushFootPrint);

								if(BrushEl.Brush->GetLastDrawFootprint().bIsValid)
									RedrawScope.Add(BrushEl.Brush->GetLastDrawFootprint());
							}
							BrushEl.Brush->RedrawNeed=false;
						}
						
					}
					

				}
			}
			

			if(RequireUpdate || ExogeneReDrawBox.Num()>0)
			{

				for(FBox2D& Box:ExogeneReDrawBox)
					RedrawScope.Add(Box);

				ShaderWorldOwner->BrushManagerRequestRedraw(RedrawScope);				

				ExogeneReDrawBox.Empty();
			}
						
		}
	}

}

bool FBrushElement::IsValid()
{
	if(Brush && IsValidChecked(Brush) && Brush->IsValidBrush())
		return true;

	return false;
}

FBrushElement::~FBrushElement()
{
	if (BrushManagerOwner && !BrushManagerOwner->EndPlayCalled && BrushManagerOwner->GetWorld())
	{
	FBox2D RedrawScope(ForceInit);
	if (Brush)
	{
		FBox2D Footprint = Brush->GetBrushFootPrint();
		if(Footprint.bIsValid)
			RedrawScope += Footprint;
	}

		if (RedrawScope.bIsValid)
			BrushManagerOwner->AddExogeneReDrawBox(RedrawScope);
	}
}

FBrushLayer::~FBrushLayer()
{
	if (BrushManagerOwner && !BrushManagerOwner->EndPlayCalled && BrushManagerOwner->GetWorld())
	{
	FBox2D RedrawScope(ForceInit);

	for (FBrushElement& BrushEl : Brushes)
	{
		if (BrushEl.IsValid())
		{
			RedrawScope += BrushEl.Brush->GetBrushFootPrint();
		}
	}

		if(RedrawScope.bIsValid)
			BrushManagerOwner->AddExogeneReDrawBox(RedrawScope);
	}
		
}

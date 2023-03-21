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

#include "Data/SWStructs.h"

#include "SWorldSubsystem.h"

void FSWTexture2DCacheGroup::UpdateReferencePoints(UWorld* World, double& CurrentTime, TArray<FVector>& CameraLocations)
{

	if (World && (((CurrentTime - CamerasUpdateTime) > 0.1) || (CamerasUpdateTime > CurrentTime)))
	{
		CamerasUpdateTime = CurrentTime;
		All_Cameras.Empty();

		for (const auto& CamLoc : CameraLocations)
		{
			const double Cam_X = (CamLoc.X + World->OriginLocation.X) / (CacheSizeMeters * 100.0);
			const double Cam_Y = (CamLoc.Y + World->OriginLocation.Y) / (CacheSizeMeters * 100.0);
			//double Cam_Z = (CamLoc.Z + GetWorld()->OriginLocation.Z) / (Spawn.GridSizeMeters * 100.0);

			const int CamX = FMath::RoundToInt(Cam_X);
			const int CamY = FMath::RoundToInt(Cam_Y);

			All_Cameras.Add(FIntVector(CamX, CamY, 0));				
		}
	}
}

void FSWTexture2DCacheGroup::ReleaseOutOfRange()
{
	CacheManager.ReleaseBeyondRange(All_Cameras);
}

void FSWTexture2DCacheGroup::GenerateWithinRange(UWorld* World)
{
	TArray<int32> Work = CacheManager.CollectWork(All_Cameras);

	for (int32& UnInit : CacheManager.UnInitializedDataElem)
	{
		int32 AddedLocation = -1;

		/*
		 * We assume that all Cache within a same group are of the same length
		 * It means that if we add a cache to a group at runtime, it needs to be initialized to the same length as existing cache:
		 * Cache.Value.TextureCache.Num()
		 * For now we build the cache on work construction, they're all the same length
		 */
		for (auto& Cache : PerLODCaches)
		{
			
			UTextureRenderTarget2D* NewTexture = nullptr;
			SW_RT(NewTexture, World, Cache.Value.Dimension, Cache.Value.LayerFiltering, Cache.Value.Format)
			
			const int32 Index = Cache.Value.TextureCache.Add(NewTexture);

			if(AddedLocation<0)
				AddedLocation = Index;
		}

		CacheManager.AssignDataElement(UnInit, AddedLocation);
	}

	for(int32 i = 0;i < Work.Num();i++)
	{		
		FSWRingCacheManager::FCacheElem& CacheElement = CacheManager.CacheElem[Work[i]];

		for(auto& Cache : PerLODCaches)
		{
			
		}
	}
}

void FSWTexture2DCacheGroup::DrawDebugPartition(UWorld* World)
{
	for(auto& CacheEl : CacheManager.CacheElem)
	{
		FVector CacheLocationMin = FVector(CacheEl.Location * CacheSizeMeters * 100) + FVector(0.f,0.f,-(CacheSizeMeters * 100.0)/2.0);
		FVector CacheLocationMax = CacheLocationMin + CacheSizeMeters * 100 * FVector(1.0,1.0,0.0) + FVector(0.f, 0.f, (CacheSizeMeters * 100.0) / 2.0);

		const FBox NodeBounds = FBox(CacheLocationMin, CacheLocationMax).TransformBy(FTransform::Identity);
		DrawDebugBox(World, NodeBounds.GetCenter(), NodeBounds.GetExtent(), FColor::Cyan, false, 0.5f);
		
	}
}

SWStructs::SWStructs()
{
}

SWStructs::~SWStructs()
{
}

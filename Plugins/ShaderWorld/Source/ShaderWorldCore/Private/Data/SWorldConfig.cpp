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

#include "Data/SWorldConfig.h"
#include "HardwareInfo.h"
#include "DrawDebugHelpers.h"
#include "Async/Async.h"

void USWContextBase::UpdateContext(float DeltaTime, UWorld* World, TArray<FVector>& Visitor)
{
	if (bProcessing)
	{
		return;
	}

	bProcessing = true;

	Time += DeltaTime;

	PrepareVisit(DeltaTime, World, Visitor);

	Async(EAsyncExecution::ThreadPool, [this]
		{
			Visit();
			bProcessing = false;
		});
}

void USWContextBase::UpdateRenderAPI()
{
	if (RHIString == "")
	{

		FString HardwareDetails = FHardwareInfo::GetHardwareDetailsString();
		FString RHILookup = NAME_RHI.ToString() + TEXT("=");


		if (FParse::Value(*HardwareDetails, *RHILookup, RHIString))
		{
			UE_LOG(LogTemp, Warning, TEXT("RHI = %s"), *RHIString);

			if (RHIString == TEXT("D3D11"))
			{
				RendererAPI = ESWRenderingAPI::DX11;
			}
			else if (RHIString == TEXT("D3D12"))
			{
				RendererAPI = ESWRenderingAPI::DX12;
			}
			else if (RHIString == TEXT("OpenGL"))
			{
				RendererAPI = ESWRenderingAPI::OpenGL;
			}
			else if (RHIString == TEXT("Vulkan"))
			{
				RendererAPI = ESWRenderingAPI::Vulkan;
			}
			else if (RHIString == TEXT("Metal"))
			{
				RendererAPI = ESWRenderingAPI::Metal;
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("No case setup for this RHI, default to DX11"));
				RHIString = "D3D11";
				RendererAPI = ESWRenderingAPI::DX11;
			}

			//if (GEngine)
			//	GEngine->AddOnScreenDebugMessage(-1, 15.0f, FColor::Yellow, RHIString);

		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("Couldnt parse RHI, default to DX11"));
			RHIString = "D3D11";
			RendererAPI = ESWRenderingAPI::DX11;
		}
	}
}


bool USWGeoClipMapContext::Setup()
{
	return true;
}

void USWGeoClipMapContext::SetN()
{
	int N_values[8] = { 2047,1023,511,255,127,63,31,15 };
	N = N_values[(uint8)ElementSize];
}

void USWGeoClipMapContext::SetDepth()
{
	float size = 0.f;
	int32 LocalDepth=-1;

	while(size/2.f <= GenerationDistanceMeter*1000.f)
	{
		LocalDepth++;
		size = (1<< LocalDepth) * Resolution * N;

		if(LocalDepth >31)
		break;
	}

	LOD = LocalDepth;
}

void USWGeoClipMapContext::UpdateClipMap()
{

}

void USWGeoClipMapContext::InitiateWorld()
{
}

bool USWGeoClipMapContext::ProcessSegmentedComputation()
{
	return true;
}

void USWGeoClipMapContext::UpdateContext(float DeltaTime, UWorld* World, TArray<FVector>& Visitor)
{
	Super::UpdateContext(DeltaTime,World,Visitor);

	if(Visitor.Num()<=0)
	return;

	SetN();
	SetDepth();

	if(DrawDebug)
	{	
		for(uint8 IterDepth=0; IterDepth < LOD; IterDepth++)
		{
			float size = (1 << IterDepth) * Resolution * N;
			FVector extent = FVector(size / 2.f, size / 2.f, 500.f);
			FVector Center = Visitor[0];

			FBox BoxToDraw(Center - extent, Center + extent);

			DrawDebugBox(World, BoxToDraw.GetCenter(), BoxToDraw.GetExtent(), FColor::Cyan);
		}
	}
	
	if (RTUpdate.IsFenceComplete())
	{
		//ProcessCollisionsPending();		
	}

	if (!ProcessSegmentedComputation())
		return;

	if (!RTUpdate.IsFenceComplete())
		return;

	if (!Setup())
		return;

	if (Meshes.Num() == 0)
	{
		InitiateWorld();

		RTUpdate.BeginFence();
		return;
	}

	UpdateClipMap();
	RTUpdate.BeginFence();

}

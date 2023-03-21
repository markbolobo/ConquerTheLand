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
#include "Actor/OctreeActorTest.h"

#include "SWStats.h"
//#include "Components/OctreeDynamicMeshComponent.h"
//#include "Generators/MarchingCubes.h"


// Sets default values
AOctreeActorTest::AOctreeActorTest()
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("OctreeRoot"));
}

// Called when the game starts or when spawned
void AOctreeActorTest::BeginPlay()
{
	Super::BeginPlay();
	
}

// Called every frame
void AOctreeActorTest::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	/*
	if(!OctreeComp)
	{
		
		OctreeComp = NewObject<UOctreeDynamicMeshComponent>(this, NAME_None, RF_Transient);
		OctreeComp->SetupAttachment(RootComponent);
		OctreeComp->RegisterComponent();
		OctreeComp->BoundsScale = 10.f;
		OctreeComp->GetOctree()->SetMaxTreeDepth(6);

		
		float MeshCellSize = 10.f;
		bool UseBounds=false;
		double MaxRadius = 0.0;

		UE::Geometry::FMarchingCubes MarchingCubes;
		//MarchingCubes.CancelF = CancelF;
		UE::Geometry::TAxisAlignedBox3<double> LocalBounds(FVector( - 2000.f), FVector(2000.f));
		

		MarchingCubes.CubeSize = MeshCellSize;
		MarchingCubes.IsoValue = 0.0f;
		MarchingCubes.Bounds = LocalBounds;
		MarchingCubes.Bounds.Expand(2.0 * MaxRadius);
		//MarchingCubes.Bounds.Expand(32.0 * MeshCellSize);
		MarchingCubes.RootMode = UE::Geometry::ERootfindingModes::SingleLerp;
		//MarchingCubes.RootMode = ERootfindingModes::Bisection;
		MarchingCubes.RootModeSteps = 4;
		
		TArray<FVector3d> Seeds;
	

		MarchingCubes.Implicit = [&](const FVector3d& Pt) { return Pt.Z; };

		MarchingCubes.Generate();
		//MarchingCubes.GenerateContinuation(Seeds);

		// if we found zero triangles, try again w/ full grid search
		

		// clear implicit function...
		MarchingCubes.Implicit = nullptr;

		BaseMesh = MakeShared<FDynamicMesh3, ESPMode::ThreadSafe>(&MarchingCubes);

		CurrentMesh = FDynamicMesh3 (*BaseMesh);
		OctreeComp->SetMesh(MoveTemp(CurrentMesh));
		
		OctreeComp->MarkRenderStateDirty();
		
	}
	*/
}


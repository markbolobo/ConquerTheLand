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

struct FVisitor
{
public:
	FBox VisitBounds;

	FVisitor() = default;
	~FVisitor() = default;

	FVisitor(FVector Location, float Extent)
	:VisitBounds(Location - Extent*FVector(1.0), Location + Extent * FVector(1.0))
	{};
};

template<typename DataType>
class FSW_PointerTree : public TSharedFromThis<FSW_PointerTree<DataType>>
{
public:
	
	/** Stores shared per-LOD node data. */
	struct FSharedLODData
	{
		double Extent;

		FSharedLODData():Extent(0) {};
		~FSharedLODData() {};
		FSharedLODData(const FVector3f& InExtent)
		:Extent(InExtent.X)
		{};
	};
	struct FPointerNode : public TSharedFromThis<FSW_PointerTree<DataType>::FPointerNode>
	{
	public:
		FSW_PointerTree<DataType>* Owner = nullptr;
		TSharedPtr < FPointerNode> Parent;
		TStaticArray< TSharedPtr<FPointerNode>,4> Children;

		uint8 Depth = 0;
		FVector Center = FVector(0);
		FBox Bounds;

		DataType Data;

		~FPointerNode()
		{
			for (uint8 Child = 0; Child < 4; Child++)
			{
				if(Children[Child].IsValid())
					Children[Child]=nullptr;
			}				
		};

		FPointerNode(FSW_PointerTree<DataType>* Owner_,TSharedPtr < FPointerNode> Parent_ = nullptr, FVector Center_= FVector(0))
		: Owner(Owner_? Owner_:Parent_->Owner)
		, Parent(Parent_)
		, Depth(Parent_ ? Parent_->Depth + 1 : 0)
		, Center(Center_)		
		, Bounds(FBox(Center - Owner->SharedData[Depth].Extent*FVector(1.f), Center + Owner->SharedData[Depth].Extent * FVector(1.f)))
		{
			for (uint8 Child = 0; Child < 4; Child++)
			{
				Children[Child] = nullptr;
			}
		};

		bool IsLeaf(){return !Children[0].IsValid();}

		DataType& GetData(){return Data;};
	};

	FSW_PointerTree() = delete;

	FSW_PointerTree(double Extent_, uint8 Depth_, FVector TreeLocation_)
		: TreeLocation(TreeLocation_)
		, MaxDepth(Depth_)
	{
		SharedData.AddDefaulted(Depth_);
		for (int32 LOD = 0; LOD < Depth_; LOD++)
		{
			SharedData[LOD].Extent = Extent_ * (1.0 / (1 << LOD));
		}

		RootNode = MakeShared <FPointerNode>(this);
	};

	~FSW_PointerTree()
	{
		RootNode = nullptr;
	};

	FORCEINLINE FVector GetTreeLocation(){return TreeLocation;}
	FORCEINLINE uint8 GetMaxDepth() { return MaxDepth; }

	/** Used for thread safety between rendering and asset operations. */
	mutable FCriticalSection DataLock;

protected:
	TArray<FSharedLODData> SharedData;
	FVector TreeLocation = FVector(0);
	uint8 MaxDepth=0;

	TSharedPtr<FPointerNode> RootNode;

	friend struct FPointerNode;
};

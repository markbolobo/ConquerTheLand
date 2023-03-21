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
#include "RenderCommandFence.h"
#include "SWorldConfig.generated.h"
/**
 * 
 */

UENUM()
enum class ESWRenderingAPI : uint8
{
	DX11 UMETA(DisplayName = "DX11"),
	DX12 UMETA(DisplayName = "DX12"),
	OpenGL UMETA(DisplayName = "OpenGL"),
	Vulkan UMETA(DisplayName = "Vulkan"),
	Metal UMETA(DisplayName = "Metal"),
};

UENUM(BlueprintType)
enum class ENClipMapValue : uint8
{
	N2047 UMETA(DisplayName = "2047"),
	N1023 UMETA(DisplayName = "1023"),
	N511 UMETA(DisplayName = "511"),
	N255 UMETA(DisplayName = "255"),
	N127 UMETA(DisplayName = "127"),
	N63 UMETA(DisplayName = "63"),
	N31 UMETA(DisplayName = "31"),
	N15 UMETA(DisplayName = "15")
};

USTRUCT()
struct FGeoClipMapElement
{
	GENERATED_BODY()
};

class FSWorldContextBase : TSharedFromThis<FSWorldContextBase>
{
public:
	FSWorldContextBase() {}
	virtual ~FSWorldContextBase() {};

	virtual void UpdateContext(float DeltaTime, UWorld* World) = 0;
};


class FSWorldContext : public FSWorldContextBase
{
public:

	FSWorldContext() {};

	virtual ~FSWorldContext() {};

	FBox ContextBounds(EForceInit);
};


UCLASS(Abstract, BlueprintType, EditInlineNew, CollapseCategories)
class SHADERWORLDCORE_API USWContextBase : public UObject
{
	GENERATED_BODY()

public:

	virtual void UpdateContext(float DeltaTime, UWorld* World, TArray<FVector>& Visitor);

	virtual void Visit() {};
	virtual void PrepareVisit(float DeltaTime, UWorld* World, TArray<FVector>& Visitor) {};

	UPROPERTY(EditAnywhere, Category="")
		bool DrawDebug = false;
	UPROPERTY(EditAnywhere, Category = "")
		bool Bounded = false;
	UPROPERTY(EditAnywhere, Category = "",meta= (EditCondition = "Bounded"))
		FIntVector Extent = FIntVector(8000,8000,0);

	UPROPERTY(Transient)
		FString RHIString = "";
	UPROPERTY(Transient)
		ESWRenderingAPI RendererAPI = ESWRenderingAPI::DX11;

	void UpdateRenderAPI();

	bool UptoDate = false;

	/** Allows skipping processing, if another one is already in progress */
	TAtomic<bool> bProcessing;
	/** Stores cumulative time, elapsed from the creation of the manager. Used to determine nodes' lifetime. */
	float Time;
};



UCLASS(BlueprintType, EditInlineNew, CollapseCategories, meta = (DisplayName = "Geometry Clipmap"))
class SHADERWORLDCORE_API USWGeoClipMapContext : public USWContextBase
{
	GENERATED_BODY()

public:

	virtual void UpdateContext(float DeltaTime, UWorld* World, TArray<FVector>& Visitor) override;

protected:

	/** Precision of Computation - 1 meter is default */
	UPROPERTY(EditAnywhere, Category = "", meta = (ClampMin = "1.0", ClampMax = "1000"))
		float Resolution = 100.f;

	/** How far are we generating terrain in Meters */
	UPROPERTY(EditAnywhere, Category = "", meta = (ClampMin = "100", ClampMax = "1000000"))
		float GenerationDistanceMeter = 8000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "")
		ENClipMapValue ElementSize = ENClipMapValue::N31;

	/** The time it takes the orientation to catchup to the requested orientation. */
	UPROPERTY(VisibleAnywhere, Category = "")
		uint8 LOD = 8;

private:

	bool Setup();
	void SetN();
	void SetDepth();
	void UpdateClipMap();
	void InitiateWorld();
	bool ProcessSegmentedComputation();

	FRenderCommandFence RTUpdate;

protected:

	UPROPERTY(Transient)
		int N = 31;
	UPROPERTY(Transient)
		int LODNum = 0;
	UPROPERTY(Transient)
		TArray<FGeoClipMapElement> Meshes;
};



USTRUCT()
struct SHADERWORLDCORE_API FSWContextConfig
{
	GENERATED_BODY()

		FSWContextConfig() = default;
};





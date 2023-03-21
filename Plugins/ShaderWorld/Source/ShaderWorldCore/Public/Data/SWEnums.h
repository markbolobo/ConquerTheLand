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
#include "SWEnums.generated.h"
/**
 * 
 */


UENUM(BlueprintType)
enum class EWorldPresentation : uint8
{
	Smooth UMETA(DisplayName = "Triangle Based Terrain"),
	InstancedMesh UMETA(DisplayName = "InstancedMesh Shaped"),
};

UENUM(BlueprintType)
enum class EWorldShape : uint8
{
	Flat UMETA(DisplayName = "Flat"),
	Spherical UMETA(DisplayName = "Sphere Faked"),
};

/**
* Configuration of a parent Clipmap Ring, given his child relative position*
*/
UENUM()
enum class EClipMapInteriorConfig : uint8
{
	BotLeft UMETA(DisplayName = "BotLeft"),
	TopLeft UMETA(DisplayName = "TopLeft"),
	BotRight UMETA(DisplayName = "BotRight"),
	TopRight UMETA(DisplayName = "TopRight"),
	NotVisible UMETA(DisplayName = "NotVisible"),
};

UENUM()
enum class EDataMapChannel : uint8
{
	Red UMETA(DisplayName = "Red"),
	Green UMETA(DisplayName = "Green"),
	Blue UMETA(DisplayName = "Blue"),
	Alpha UMETA(DisplayName = "Alpha")
};

UENUM()
enum class EGeoRenderingAPI : uint8
{
	DX11 UMETA(DisplayName = "DX11"),
	DX12 UMETA(DisplayName = "DX12"),
	OpenGL UMETA(DisplayName = "OpenGL"),
	Vulkan UMETA(DisplayName = "Vulkan"),
	Metal UMETA(DisplayName = "Metal"),
};


UENUM()
enum class EDataLayerFiltering : uint8
{
	TF_Nearest UMETA(DisplayName = "Nearest"),
	TF_Bilinear UMETA(DisplayName = "Bi-linear"),
	TF_Trilinear UMETA(DisplayName = "Tri-linear"),
};

/**
* Number of vertices per side for a given Clipmap ring
*
*/
UENUM(BlueprintType)
enum class ENValue : uint8
{
	N2047 UMETA(DisplayName = "2047"),
	N1023 UMETA(DisplayName = "1023"),
	N511 UMETA(DisplayName = "511"),
	N255 UMETA(DisplayName = "255"),
	N127 UMETA(DisplayName = "127"),
	N63 UMETA(DisplayName = "63"),
	N31 UMETA(DisplayName = "31"),
	N15 UMETA(DisplayName = "15"),
};

/**
* Selection of Spawnable type. Either we using hierarchical instanced mesh on a given set of Static Meshes,
* or we simply spawn actors of the given class
*/
UENUM(BlueprintType)
enum class ESpawnableType : uint8
{
	Undefined UMETA(DisplayName = "Select Spawn Type"),
	Grass UMETA(DisplayName = "Grass"),
	Mesh UMETA(DisplayName = "Meshes"),
	Foliage UMETA(DisplayName = "Foliages"),
	Actor UMETA(DisplayName = "Actors"),
};


class SHADERWORLDCORE_API SWEnums
{
public:
	SWEnums();
	~SWEnums();
};

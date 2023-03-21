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
#include "Components/PrimitiveComponent.h"
#include "Data/SWorldConfig.h"
#include "SWorldRoot.generated.h"

/**
 * 
 */
UCLASS(hideCategories(Object,Rendering,Physics,HLOD,Transform,Lighting, TextureStreaming,RayTracing,Navigation,Activation,Mobile, Input, Actor, Game, LOD, Replication, Cooking))
class SHADERWORLDCORE_API USWorldRoot : public UPrimitiveComponent
{
	GENERATED_BODY()

public:

	USWorldRoot(const FObjectInitializer& ObjectInitializer);


	TSharedPtr<FSWorldContext> SWContext;

	/** This is a list of physical materials that is actually used by a cooked HeightField */
	UPROPERTY()
	TArray<TObjectPtr<UPhysicalMaterial>>PhysicalMaterials;
	
};

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

#include "ShaderWorldAssetTypeActions.h"
#include "AssetTypeCategories.h"
#include "Engine/EngineTypes.h"
#include "Data/ShaderWorld_Material.h"
#include "Data/ShaderWorld_Material_Collection.h"
#include "Data/ShaderWorld_Spawnable.h"
#include "Data/ShaderWorld_Spawnable_Collection.h"
#include "Modules/ModuleManager.h"
#include "Internationalization/Internationalization.h"
#include "Storage/SWStorageBase.h"


#define LOCTEXT_NAMESPACE "SW_StorageBase"

FText FSW_StorageBaseAssetTypeActions::GetName() const
{
	return LOCTEXT("FSW_StorageBaseAssetTypeActionsName", "SW_StorageBase");
}

FColor FSW_StorageBaseAssetTypeActions::GetTypeColor() const
{
	return FColor::Cyan;
}

UClass* FSW_StorageBaseAssetTypeActions::GetSupportedClass() const
{
	return USWStorageBase::StaticClass();
}

uint32 FSW_StorageBaseAssetTypeActions::GetCategories()
{
	return EAssetTypeCategories::Misc;
}

#undef LOCTEXT_NAMESPACE

#define LOCTEXT_NAMESPACE "ShaderWorld_Material"

FText FSW_MaterialAssetTypeActions::GetName() const
{
	return LOCTEXT("FSW_MaterialAssetTypeActionsName", "ShaderWorld_Material");
}

FColor FSW_MaterialAssetTypeActions::GetTypeColor() const
{
	return FColor::Cyan;
}

UClass* FSW_MaterialAssetTypeActions::GetSupportedClass() const
{
	return UShaderWorld_Material::StaticClass();
}

uint32 FSW_MaterialAssetTypeActions::GetCategories()
{
		return EAssetTypeCategories::Misc;
}

#undef LOCTEXT_NAMESPACE

#define LOCTEXT_NAMESPACE "ShaderWorld_Material_Collection"

FText FSW_Material_CollectionAssetTypeActions::GetName() const
{
	return LOCTEXT("FSW_Material_CollectionAssetTypeActionsName", "ShaderWorld_Material_Collection");
}

FColor FSW_Material_CollectionAssetTypeActions::GetTypeColor() const
{
	return FColor::Cyan;
}

UClass* FSW_Material_CollectionAssetTypeActions::GetSupportedClass() const
{
	return UShaderWorld_Material_Collection::StaticClass();
}

uint32 FSW_Material_CollectionAssetTypeActions::GetCategories()
{
		return EAssetTypeCategories::Misc;
}

#undef LOCTEXT_NAMESPACE

#define LOCTEXT_NAMESPACE "ShaderWorld_Spawnable"


FText FSW_SpawnableAssetTypeActions::GetName() const
{
	return LOCTEXT("FSW_SpawnableAssetTypeActionsName", "ShaderWorld_Spawnable");
}

FColor FSW_SpawnableAssetTypeActions::GetTypeColor() const
{
	return FColor::Cyan;
}

UClass* FSW_SpawnableAssetTypeActions::GetSupportedClass() const
{
	return UShaderWorld_Spawnable::StaticClass();
}

uint32 FSW_SpawnableAssetTypeActions::GetCategories()
{
		return EAssetTypeCategories::Misc;
}

#undef LOCTEXT_NAMESPACE

#define LOCTEXT_NAMESPACE "ShaderWorld_Biom"


FText FSW_Spawnable_collectionAssetTypeActions::GetName() const
{
	return LOCTEXT("FSW_Spawnable_collectionAssetTypeActionsName", "ShaderWorld_Biom");
}

FColor FSW_Spawnable_collectionAssetTypeActions::GetTypeColor() const
{
	return FColor::Cyan;
}

UClass* FSW_Spawnable_collectionAssetTypeActions::GetSupportedClass() const
{
	return UShaderWorld_Biom::StaticClass();
}

uint32 FSW_Spawnable_collectionAssetTypeActions::GetCategories()
{
		return EAssetTypeCategories::Misc;
}

#undef LOCTEXT_NAMESPACE
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

#include "ShaderWorldAssetFactories.h"
#include "ShaderWorldEditor.h"

#include "Data/ShaderWorld_Material.h"
#include "Data/ShaderWorld_Material_Collection.h"
#include "Data/ShaderWorld_Spawnable.h"
#include "Data/ShaderWorld_Spawnable_Collection.h"
#include "Storage/SWStorageBase.h"

//The asset type categories will let us access the various asset categories inside the Editor
#include "AssetTypeCategories.h"


#define LOCTEXT_NAMESPACE "SW_StorageBaseFactory"

USW_StorageBaseFactory::USW_StorageBaseFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bCreateNew = true;
	bEditAfterNew = true;
	SupportedClass = USWStorageBase::StaticClass();
}


uint32 USW_StorageBaseFactory::GetMenuCategories() const
{
	//Let's place this asset in the Blueprints category in the Editor
	return FShaderWorldEditorModule::GetAssetCategory();
}

UObject* USW_StorageBaseFactory::FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	//Create the editor asset 
	USWStorageBase* OrfeasEditorAsset = NewObject<USWStorageBase>(InParent, InClass, InName, Flags);
	return OrfeasEditorAsset;
}

#undef LOCTEXT_NAMESPACE

#define LOCTEXT_NAMESPACE "ShaderWorld_MaterialFactory"

UShaderWorld_MaterialFactory::UShaderWorld_MaterialFactory(const FObjectInitializer & ObjectInitializer)
		: Super(ObjectInitializer)
{
	bCreateNew = true;
	bEditAfterNew = true;
	SupportedClass = UShaderWorld_Material::StaticClass();
}


uint32 UShaderWorld_MaterialFactory::GetMenuCategories() const
{
	//Let's place this asset in the Blueprints category in the Editor
	return FShaderWorldEditorModule::GetAssetCategory();
}

UObject* UShaderWorld_MaterialFactory::FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	//Create the editor asset 
	UShaderWorld_Material* OrfeasEditorAsset = NewObject<UShaderWorld_Material>(InParent, InClass, InName, Flags);
	return OrfeasEditorAsset;
}

#undef LOCTEXT_NAMESPACE

#define LOCTEXT_NAMESPACE "ShaderWorld_Material_CollectionFactory"


UShaderWorld_Material_CollectionFactory::UShaderWorld_Material_CollectionFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bCreateNew = true;
	bEditAfterNew = true;
	SupportedClass = UShaderWorld_Material_Collection::StaticClass();
}


uint32 UShaderWorld_Material_CollectionFactory::GetMenuCategories() const
{
	//Let's place this asset in the Blueprints category in the Editor
	return FShaderWorldEditorModule::GetAssetCategory();
}

UObject* UShaderWorld_Material_CollectionFactory::FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	//Create the editor asset 
	UShaderWorld_Material_Collection* OrfeasEditorAsset = NewObject<UShaderWorld_Material_Collection>(InParent, InClass, InName, Flags);
	return OrfeasEditorAsset;
}

#undef LOCTEXT_NAMESPACE

#define LOCTEXT_NAMESPACE "ShaderWorld_SpawnableFactory"

UShaderWorld_SpawnableFactory::UShaderWorld_SpawnableFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bCreateNew = true;
	bEditAfterNew = true;
	SupportedClass = UShaderWorld_Spawnable::StaticClass();
}


uint32 UShaderWorld_SpawnableFactory::GetMenuCategories() const
{
	//Let's place this asset in the Blueprints category in the Editor
	return FShaderWorldEditorModule::GetAssetCategory();
}

UObject* UShaderWorld_SpawnableFactory::FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	//Create the editor asset 
	UShaderWorld_Spawnable* OrfeasEditorAsset = NewObject<UShaderWorld_Spawnable>(InParent, InClass, InName, Flags);
	return OrfeasEditorAsset;
}

#undef LOCTEXT_NAMESPACE

#define LOCTEXT_NAMESPACE "ShaderWorld_Spawnable_CollectionFactory"


UShaderWorld_Spawnable_CollectionFactory::UShaderWorld_Spawnable_CollectionFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bCreateNew = true;
	bEditAfterNew = true;
	SupportedClass = UShaderWorld_Biom::StaticClass();
}


uint32 UShaderWorld_Spawnable_CollectionFactory::GetMenuCategories() const
{
	//Let's place this asset in the Blueprints category in the Editor
	return FShaderWorldEditorModule::GetAssetCategory();
}

UObject* UShaderWorld_Spawnable_CollectionFactory::FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	//Create the editor asset 
	UShaderWorld_Biom* OrfeasEditorAsset = NewObject<UShaderWorld_Biom>(InParent, InClass, InName, Flags);
	return OrfeasEditorAsset;
}

#undef LOCTEXT_NAMESPACE


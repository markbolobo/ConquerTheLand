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

#include "ShaderWorldEditor.h"
#include "ShaderWorldEdEdMode.h"

#include "ShaderWorldAssetTypeActions.h"
#include "Interfaces/IPluginManager.h"

#define LOCTEXT_NAMESPACE "FShaderWorldEdModule"

EAssetTypeCategories::Type FShaderWorldEditorModule::ShaderWorldAssetCategory;

void FShaderWorldEditorModule::StartupModule()
{
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module
	FEditorModeRegistry::Get().RegisterMode<FShaderWorldEdEdMode>(FShaderWorldEdEdMode::EM_ShaderWorldEdEdModeId, LOCTEXT("ShaderWorldName", "ShaderWorld"), FSlateIcon(), true);

	// Only register once
	if (!StyleSet.IsValid())
	{
		StyleSet = MakeShareable(new FSlateStyleSet("ShaderWorldStyle"));
		//"../../../Resources"

		const FString PluginDir = IPluginManager::Get().FindPlugin(TEXT("ShaderWorld"))->GetBaseDir();

		FString ContentDir = FPaths::Combine(PluginDir, TEXT("Resources"));
		StyleSet->SetContentRoot(ContentDir);


		//Create a brush from the icon
		FSlateImageBrush* IconBrush_Spawn = new FSlateImageBrush(StyleSet->RootToContentDir(TEXT("SW_Spawnable_16"), TEXT(".png")), FVector2D(16.f, 16.f));
		FSlateImageBrush* ThumbnailBrush_Spawn = new FSlateImageBrush(StyleSet->RootToContentDir(TEXT("SW_Spawnable_128"), TEXT(".png")), FVector2D(128.f, 128.f));

		if (IconBrush_Spawn)
			StyleSet->Set("ClassIcon.ShaderWorld_Spawnable", IconBrush_Spawn);
		if (ThumbnailBrush_Spawn)
			StyleSet->Set("ClassThumbnail.ShaderWorld_Spawnable", ThumbnailBrush_Spawn);

		FSlateImageBrush* IconBrush_SpawnCollection = new FSlateImageBrush(StyleSet->RootToContentDir(TEXT("SW_Spawnable_collection_16"), TEXT(".png")), FVector2D(16.f, 16.f));
		FSlateImageBrush* ThumbnailBrush_SpawnCollection = new FSlateImageBrush(StyleSet->RootToContentDir(TEXT("SW_Spawnable_collection_128"), TEXT(".png")), FVector2D(128.f, 128.f));

		if (IconBrush_SpawnCollection)
			StyleSet->Set("ClassIcon.ShaderWorld_Biom", IconBrush_SpawnCollection);
		if (ThumbnailBrush_SpawnCollection)
			StyleSet->Set("ClassThumbnail.ShaderWorld_Biom", ThumbnailBrush_SpawnCollection);

		FSlateImageBrush* IconBrush_Mat = new FSlateImageBrush(StyleSet->RootToContentDir(TEXT("SW_Material_16"), TEXT(".png")), FVector2D(16.f, 16.f));
		FSlateImageBrush* ThumbnailBrush_Mat = new FSlateImageBrush(StyleSet->RootToContentDir(TEXT("SW_Material_128"), TEXT(".png")), FVector2D(128.f, 128.f));

		if (IconBrush_Mat)
			StyleSet->Set("ClassIcon.ShaderWorld_Material", IconBrush_Mat);
		if (ThumbnailBrush_Mat)
			StyleSet->Set("ClassThumbnail.ShaderWorld_Material", ThumbnailBrush_Mat);

		FSlateImageBrush* IconBrush_MatCollection = new FSlateImageBrush(StyleSet->RootToContentDir(TEXT("SW_Material_collection_16"), TEXT(".png")), FVector2D(16.f, 16.f));
		FSlateImageBrush* ThumbnailBrush_MatCollection = new FSlateImageBrush(StyleSet->RootToContentDir(TEXT("SW_Material_collection_128"), TEXT(".png")), FVector2D(128.f, 128.f));

		if (IconBrush_MatCollection)
			StyleSet->Set("ClassIcon.ShaderWorld_Material_Collection", IconBrush_MatCollection);
		if (ThumbnailBrush_MatCollection)
			StyleSet->Set("ClassThumbnail.ShaderWorld_Material_Collection", ThumbnailBrush_MatCollection);


		FSlateStyleRegistry::RegisterSlateStyle(*StyleSet.Get());

	}




	// Register asset types
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	ShaderWorldAssetCategory = AssetTools.RegisterAdvancedAssetCategory(FName(TEXT("ShaderWorld")), LOCTEXT("ShaderWorldAssetCategory", "ShaderWorld"));

	// Helper lambda for registering asset type actions for automatic cleanup on shutdown
	auto RegisterAssetTypeAction = [&](TSharedRef<IAssetTypeActions> Action)
	{
		AssetTools.RegisterAssetTypeActions(Action);
		CreatedAssetTypeActions.Add(Action);
	};

	// Register type actions
	RegisterAssetTypeAction(MakeShareable(new FSW_StorageBaseAssetTypeActions));
	RegisterAssetTypeAction(MakeShareable(new FSW_MaterialAssetTypeActions));
	RegisterAssetTypeAction(MakeShareable(new FSW_Material_CollectionAssetTypeActions));
	RegisterAssetTypeAction(MakeShareable(new FSW_SpawnableAssetTypeActions));
	RegisterAssetTypeAction(MakeShareable(new FSW_Spawnable_collectionAssetTypeActions));
}

void FShaderWorldEditorModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.
	FEditorModeRegistry::Get().UnregisterMode(FShaderWorldEdEdMode::EM_ShaderWorldEdEdModeId);

	FSlateStyleRegistry::UnRegisterSlateStyle(StyleSet->GetStyleSetName());

	if (FModuleManager::Get().IsModuleLoaded("AssetTools"))
	{
		IAssetTools& AssetTools = FModuleManager::GetModuleChecked<FAssetToolsModule>("AssetTools").Get();
		for (auto CreatedAssetTypeAction : CreatedAssetTypeActions)
		{
			AssetTools.UnregisterAssetTypeActions(CreatedAssetTypeAction.ToSharedRef());
		}
	}
	CreatedAssetTypeActions.Empty();
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FShaderWorldEditorModule, ShaderWorldEditor)
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

#include "ShaderWorld.h"
#include "ShaderCore.h"
#include "Interfaces/IPluginManager.h"


#define LOCTEXT_NAMESPACE "FShaderWorldModule"


void FShaderWorldModule::StartupModule()
{
    // This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module
    FString ShaderDirectory = FPaths::Combine(IPluginManager::Get().FindPlugin(TEXT("ShaderWorld"))->GetBaseDir(), TEXT("Shaders/Private"));
    AddShaderSourceDirectoryMapping(TEXT("/ShaderWorld"), ShaderDirectory);

	SWToolBox = new(ShaderWorldGPUTools::SWShaderToolBox)();
}

void FShaderWorldModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.
	if(SWToolBox)
		delete SWToolBox;

	SWToolBox=nullptr;
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FShaderWorldModule, ShaderWorld)
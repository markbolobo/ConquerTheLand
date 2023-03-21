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
 * ii. sell, rent, lease, or transfer Licensed Content on a �stand-alone basis�
 * (Projects must reasonably add value beyond the value of the Licensed Content,
 * and the Licensed Content must be merely a component of the Project and not the primary focus of the Project);
 *
 */

/*
 * Main authors: Maxime Dupart (https://twitter.com/Max_Dupt)
 */
// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ShaderWorldRules : ModuleRules
{
	public ShaderWorldRules(ReadOnlyTargetRules Target) : base(Target)
	{
		//OptimizeCode = CodeOptimization.Always;

		//To verify that all of your source files include all of their required dependencies
		bool VerifyDependencies = false;

		if(VerifyDependencies)
		{
			bUseUnity = false;
			PCHUsage = PCHUsageMode.NoPCHs;
		}
		else
		{			
        	bUseUnity = true;
        	PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		}
		

		PublicIncludePaths.AddRange(
			new string[] {
				// ... add public include paths required here ...
			}
			);
				
		
		PrivateIncludePaths.AddRange(
			new string[] {
				// ... add other private include paths required here ...
			}
			);
			
		
		PublicDependencyModuleNames.AddRange(
			new string[]
			{				
				"Engine",
				"Core",
				"CoreUObject",

				// ... add other public dependencies that you statically link with here ...
			}
			);

		/*
		*	Include UnrealEd in Editor build
		*/
		if(Target.bBuildEditor)
			PublicDependencyModuleNames.Add("UnrealEd");
			
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				
				// ... add private dependencies that you statically link with here ...	
			}
			);
		
		
		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
				// ... add any modules that your module loads dynamically here ...
			}
			);

		if(!GetType().Name.EndsWith("ShaderWorldCore"))
		{
			PublicDependencyModuleNames.Add("ShaderWorldCore");

			if(!GetType().Name.EndsWith("ShaderWorld"))
			{
				PublicDependencyModuleNames.Add("ShaderWorld");
			}
		}
		
		if(GetType().Name.EndsWith("Editor"))
		{
			PublicDependencyModuleNames.AddRange(
			new string[]
			{
				
			}
			);

			PrivateDependencyModuleNames.AddRange(
			new string[]
			{		
				"UnrealEd",		
				"Slate",
				"SlateCore",
				"Projects",												
				"InputCore",
				"EditorStyle",				
				"ToolMenus",
				"LevelEditor",
                "InputCore",
                "EditorFramework"
			//	"InteractiveToolsFramework",
			//	"EditorInteractiveToolsFramework",
				 
				//UE5 start
				//"EditorFramework",
				//UE5 end
				
				// ... add private dependencies that you statically link with here ...	
			}
			);

			if(!GetType().Name.EndsWith("ShaderWorldEditor"))
			{
				PublicDependencyModuleNames.Add("ShaderWorldEditor");
			}
		}
	}
}

public class ShaderWorldCore : ShaderWorldRules
{
	public ShaderWorldCore(ReadOnlyTargetRules Target) : base(Target)
	{

		bool bIsDevelopmentContext = true;

		if (Target.Configuration == UnrealTargetConfiguration.Shipping)
			bIsDevelopmentContext = false;

		PublicDefinitions.Add(bIsDevelopmentContext?"SWDEBUG=1": "SWDEBUG=0");

		PublicIncludePaths.AddRange(
			new string[] {
				// ... add public include paths required here ...
			}
			);
				
		
		PrivateIncludePaths.AddRange(
			new string[] {
				// ... add other private include paths required here ...
			}
			);

		

		PublicDependencyModuleNames.AddRange(
			new string[]
			{				
				"InputCore" ,
				"RenderCore",
				"RHI",
				"MeshDescription",
				"StaticMeshDescription",
				"IntelISPC",
				//"GeometryFramework",
				//"GeometryCore",
				//"ModelingComponents",
				"PhysicsCore",				 
				// ... add other public dependencies that you statically link with here ...
			}
			);
			
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Projects",
				// ... add private dependencies that you statically link with here ...	
			}
			);
		
		
		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
				// ... add any modules that your module loads dynamically here ...
			}
			);

		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"DesktopPlatform"
			}
			);
		}
	}
}

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

#include "Data/ShaderWorld_Material.h"

bool UShaderWorld_Material::IsValidMaterial()
{
	if(Albedo!=nullptr && NormalMap!=nullptr && PackedMaps!=nullptr)
	{
		if(Albedo->CompressionSettings!=TextureCompressionSettings::TC_Default || !Albedo->SRGB || !(Albedo->GetSizeX()>0 && Albedo->GetSizeY()>0))
		{
			UE_LOG(LogTemp,Warning,TEXT("NonValid Material | Albedo | CompressionSettings!=TC_Default || !SRGB || !(SizeX>0 && SizeY>0)"));
			return false;
		}
		if (NormalMap->CompressionSettings != TextureCompressionSettings::TC_Normalmap || NormalMap->SRGB || !(NormalMap->GetSizeX()>0 && NormalMap->GetSizeY()>0))
		{
			UE_LOG(LogTemp, Warning, TEXT("NonValid Material | NormalMap | CompressionSettings!=TC_Normalmap || SRGB || !(SizeX>0 && SizeY>0)"));
			return false;
		}
		if (PackedMaps->SRGB || !(PackedMaps->GetSizeX()>0 && PackedMaps->GetSizeY()>0))
		{
			UE_LOG(LogTemp, Warning, TEXT("NonValid Material | PackedMaps | SRGB || !(SizeX>0 && SizeY>0)"));
			return false;
		}

		return true;
	}
	else
	{
		UE_LOG(LogTemp,Warning,TEXT("Invalid Material NULL entry"));
	}

	

	return false;
}

bool UShaderWorld_Material::IsCompatibleWith(UShaderWorld_Material* Other)
{

	if(IsValidMaterial() && Other && Other->IsValidMaterial())
	{
		if(Albedo->GetSizeX()!=Other->Albedo->GetSizeX() || Albedo->GetSizeY()!=Other->Albedo->GetSizeY())
		{
			UE_LOG(LogTemp,Warning,TEXT("UShaderWorld_Material Albedo Dimensions are not Compatible"));
			return false;
		}

		if (NormalMap->GetSizeX() != Other->NormalMap->GetSizeX() || NormalMap->GetSizeY() != Other->NormalMap->GetSizeY())
		{
			UE_LOG(LogTemp, Warning, TEXT("UShaderWorld_Material NormalMap Dimensions are not Compatible"));
			return false;
		}

		if (PackedMaps->GetSizeX() != Other->PackedMaps->GetSizeX() || PackedMaps->GetSizeY() != Other->PackedMaps->GetSizeY())
		{
			UE_LOG(LogTemp, Warning, TEXT("UShaderWorld_Material PackedMaps Dimensions are not Compatible"));
			return false;
		}
		if(PackedMaps->CompressionSettings != Other->PackedMaps->CompressionSettings)
		{
			UE_LOG(LogTemp, Warning, TEXT("UShaderWorld_Material PackedMaps CompressionSettings are not Compatible"));
			return false;
		}

		return true;
	
	}
	return false;
}

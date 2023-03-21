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

#include "SWStats.h"



FString SWGetNameOfFunction(const FString& Function)
{
	TArray<FString> StringArray;
	Function.ParseIntoArray(StringArray, TEXT("::"));

	FString FunctionName = "";

	for (auto& El : StringArray)
	{
		if (!(El.StartsWith("ShaderWorld") || El.StartsWith("<") || El.StartsWith(">")))
			FunctionName += El;
	}


	return FunctionName;
}

FString SWGetDescriptionFromFunctionName(const FString& FunctionName)
{
	TArray<FString> StringArray;
	FunctionName.ParseIntoArray(StringArray, TEXT("::"));

	FString Description = "";
	int32 counter = 0;
	for(auto& s : StringArray)
	{
		counter++;
		//Criteria Might arise?
		Description += s;
		if(counter< StringArray.Num())
		Description += "::";

		if(Description.Len()>100)
			break;
		
	}

	return Description;
}

FString SWReturnBestDescription(const FString& AutoDescriptionFromFunctionName, const FString& CustomDescription)
{
	if(CustomDescription.Len()>0)
		return CustomDescription;

	return AutoDescriptionFromFunctionName;
}

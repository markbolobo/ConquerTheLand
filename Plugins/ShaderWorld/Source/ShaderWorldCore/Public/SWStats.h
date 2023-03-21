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
#include "Stats/Stats.h"

#ifndef SWDEBUG
#define SWDEBUG 0
#endif

 // Logs
DECLARE_LOG_CATEGORY_EXTERN(LogShaderWorld, Log, All)

// Stats
DECLARE_STATS_GROUP(TEXT("ShaderWorld"), STATGROUP_SW, STATCAT_Advanced);



SHADERWORLDCORE_API FString SWGetNameOfFunction(const FString& Function);
SHADERWORLDCORE_API FString SWGetDescriptionFromFunctionName(const FString& FunctionName);
SHADERWORLDCORE_API FString SWReturnBestDescription(const FString& AutoDescriptionFromFunctionName, const FString& CustomDescription = "");


#ifdef __COUNTER__
#define COUNTER_DEF 1
#else
#define COUNTER_DEF 0
#endif

#ifdef __FUNCTION__
#define FUNCTION_DEF 1
#else
#define FUNCTION_DEF 0
#endif

/** Whether Shader World debugging is enabled. */
#ifndef WITH_SW_DEBUG
#define WITH_SW_DEBUG !(UE_BUILD_SHIPPING || UE_BUILD_TEST) && SWDEBUG && COUNTER_DEF && FUNCTION_DEF
#else
#define WITH_SW_DEBUG 0
#endif


/** Performs the operation if WITH_SW_DEBUG is enabled. Useful for one-line checks without explicitly wrapping in #if. */
#if WITH_SW_DEBUG
#define IF_SW_ENABLE_DEBUG(Op) Op
#else
#define IF_SW_ENABLE_DEBUG(Op)
#endif

/**
 * Performs DebugEnabledOp if WITH_SW_DEBUG is enabled, otherwise performs DebugDisabledOp.
 * Useful for one-line checks without explicitly wrapping in #if.
 */
#if WITH_SW_DEBUG
#define IF_SW_ENABLE_DEBUG_ELSE(DebugEnabledOp, DebugDisabledOp) DebugEnabledOp
#else
#define IF_SW_ENABLE_DEBUG_ELSE(DebugEnabledOp, DebugDisabledOp) DebugDisabledOp
#endif

#if WITH_SW_DEBUG

 // Logging helper
#define SW_LOG(Format, ...) \
	{ \
		CA_CONSTANT_IF((ELogVerbosity::Log& ELogVerbosity::VerbosityMask) <= ELogVerbosity::COMPILED_IN_MINIMUM_VERBOSITY && (ELogVerbosity::Warning & ELogVerbosity::VerbosityMask) <= ELogVerbosity::All) \
		{ \
			UE_INTERNAL_LOG_IMPL(LogShaderWorld, Log, TEXT(Format), ##__VA_ARGS__); \
		} \
	}

#define SW_FUNCTION_NAME SWGetNameOfFunction(ANSI_TO_TCHAR(__FUNCTION__))
#define SW_DESCRIPTION_FROM_FUNCTION_NAME SWGetDescriptionFromFunctionName(ANSI_TO_TCHAR(__FUNCTION__))
#define SW_STAT_BEST_DESC(...) SWReturnBestDescription(SW_DESCRIPTION_FROM_FUNCTION_NAME,__VA_ARGS__)

#define SW_FUNCTION_CYCLE_DECLARE(Description, StatName) \
DECLARE_STAT(Description, StatName, STATGROUP_SW, EStatDataType::ST_int64, EStatFlags::ClearEveryFrame | EStatFlags::CycleStat, FPlatformMemory::MCR_Invalid);\
static DEFINE_STAT(StatName)\
SCOPE_CYCLE_COUNTER(StatName);\

#define SW_REGISTER_STAT(SWStatName,SWStatDesc) \
static TCHAR* SWStatDescTCHAR_##SWStatName = SWStatDesc.GetCharArray().GetData();\
static TCHAR* SWStatNameTCHAR_##SWStatName = SWStatName.GetCharArray().GetData();\
SW_FUNCTION_CYCLE_DECLARE(SWStatDescTCHAR_##SWStatName, SWStatNameTCHAR_##SWStatName)

#define SW_APPEND(A,B) A##B

#define SW_REGISTER_STAT_NAMES_DESC(A,B)\
		SW_REGISTER_STAT(A,B)\

#define SW_STAT_SET_NAME_DESC(A,B,C)\
	FString SW_APPEND(SW_StatName,C) = A;\
	FString SW_APPEND(SW_StatDesc,C) = B;\
	SW_REGISTER_STAT_NAMES_DESC(SW_APPEND(SW_StatName,C),SW_APPEND(SW_StatDesc,C))\

#else
 // Logging helper
#define SW_LOG(Format, ...)

#endif

#define SW_FCT_CYCLE(...) IF_SW_ENABLE_DEBUG(SW_STAT_SET_NAME_DESC(SW_FUNCTION_NAME, SW_STAT_BEST_DESC(__VA_ARGS__),__COUNTER__))







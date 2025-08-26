// Copyright Epic Games, Inc. All Rights Reserved.
/*===========================================================================
	Generated code exported from UnrealHeaderTool.
	DO NOT modify this manually! Edit the corresponding .h files instead!
===========================================================================*/

// IWYU pragma: private, include "MMDGameMode.h"
#include "UObject/ObjectMacros.h"
#include "UObject/ScriptMacros.h"

PRAGMA_DISABLE_DEPRECATION_WARNINGS
#ifdef MMD_MMDGameMode_generated_h
#error "MMDGameMode.generated.h already included, missing '#pragma once' in MMDGameMode.h"
#endif
#define MMD_MMDGameMode_generated_h

#define FID_MMD_Source_MMD_MMDGameMode_h_12_INCLASS_NO_PURE_DECLS \
private: \
	static void StaticRegisterNativesAMMDGameMode(); \
	friend struct Z_Construct_UClass_AMMDGameMode_Statics; \
public: \
	DECLARE_CLASS(AMMDGameMode, AGameModeBase, COMPILED_IN_FLAGS(0 | CLASS_Transient | CLASS_Config), CASTCLASS_None, TEXT("/Script/MMD"), MMD_API) \
	DECLARE_SERIALIZER(AMMDGameMode)


#define FID_MMD_Source_MMD_MMDGameMode_h_12_ENHANCED_CONSTRUCTORS \
private: \
	/** Private move- and copy-constructors, should never be used */ \
	AMMDGameMode(AMMDGameMode&&); \
	AMMDGameMode(const AMMDGameMode&); \
public: \
	DECLARE_VTABLE_PTR_HELPER_CTOR(MMD_API, AMMDGameMode); \
	DEFINE_VTABLE_PTR_HELPER_CTOR_CALLER(AMMDGameMode); \
	DEFINE_DEFAULT_CONSTRUCTOR_CALL(AMMDGameMode) \
	MMD_API virtual ~AMMDGameMode();


#define FID_MMD_Source_MMD_MMDGameMode_h_9_PROLOG
#define FID_MMD_Source_MMD_MMDGameMode_h_12_GENERATED_BODY \
PRAGMA_DISABLE_DEPRECATION_WARNINGS \
public: \
	FID_MMD_Source_MMD_MMDGameMode_h_12_INCLASS_NO_PURE_DECLS \
	FID_MMD_Source_MMD_MMDGameMode_h_12_ENHANCED_CONSTRUCTORS \
private: \
PRAGMA_ENABLE_DEPRECATION_WARNINGS


template<> MMD_API UClass* StaticClass<class AMMDGameMode>();

#undef CURRENT_FILE_ID
#define CURRENT_FILE_ID FID_MMD_Source_MMD_MMDGameMode_h


PRAGMA_ENABLE_DEPRECATION_WARNINGS

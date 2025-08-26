// Copyright Epic Games, Inc. All Rights Reserved.
/*===========================================================================
	Generated code exported from UnrealHeaderTool.
	DO NOT modify this manually! Edit the corresponding .h files instead!
===========================================================================*/

// IWYU pragma: private, include "MMDAsset.h"
#include "UObject/ObjectMacros.h"
#include "UObject/ScriptMacros.h"

PRAGMA_DISABLE_DEPRECATION_WARNINGS
#ifdef MMD_MMDAsset_generated_h
#error "MMDAsset.generated.h already included, missing '#pragma once' in MMDAsset.h"
#endif
#define MMD_MMDAsset_generated_h

#define FID_MMD_Source_MMD_MMDAsset_h_15_INCLASS_NO_PURE_DECLS \
private: \
	static void StaticRegisterNativesUMMDAsset(); \
	friend struct Z_Construct_UClass_UMMDAsset_Statics; \
public: \
	DECLARE_CLASS(UMMDAsset, UObject, COMPILED_IN_FLAGS(0), CASTCLASS_None, TEXT("/Script/MMD"), NO_API) \
	DECLARE_SERIALIZER(UMMDAsset)


#define FID_MMD_Source_MMD_MMDAsset_h_15_ENHANCED_CONSTRUCTORS \
	/** Standard constructor, called after all reflected properties have been initialized */ \
	NO_API UMMDAsset(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get()); \
private: \
	/** Private move- and copy-constructors, should never be used */ \
	UMMDAsset(UMMDAsset&&); \
	UMMDAsset(const UMMDAsset&); \
public: \
	DECLARE_VTABLE_PTR_HELPER_CTOR(NO_API, UMMDAsset); \
	DEFINE_VTABLE_PTR_HELPER_CTOR_CALLER(UMMDAsset); \
	DEFINE_DEFAULT_OBJECT_INITIALIZER_CONSTRUCTOR_CALL(UMMDAsset) \
	NO_API virtual ~UMMDAsset();


#define FID_MMD_Source_MMD_MMDAsset_h_12_PROLOG
#define FID_MMD_Source_MMD_MMDAsset_h_15_GENERATED_BODY \
PRAGMA_DISABLE_DEPRECATION_WARNINGS \
public: \
	FID_MMD_Source_MMD_MMDAsset_h_15_INCLASS_NO_PURE_DECLS \
	FID_MMD_Source_MMD_MMDAsset_h_15_ENHANCED_CONSTRUCTORS \
private: \
PRAGMA_ENABLE_DEPRECATION_WARNINGS


template<> MMD_API UClass* StaticClass<class UMMDAsset>();

#undef CURRENT_FILE_ID
#define CURRENT_FILE_ID FID_MMD_Source_MMD_MMDAsset_h


PRAGMA_ENABLE_DEPRECATION_WARNINGS

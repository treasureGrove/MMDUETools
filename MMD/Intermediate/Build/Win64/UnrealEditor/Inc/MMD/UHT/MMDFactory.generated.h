// Copyright Epic Games, Inc. All Rights Reserved.
/*===========================================================================
	Generated code exported from UnrealHeaderTool.
	DO NOT modify this manually! Edit the corresponding .h files instead!
===========================================================================*/

// IWYU pragma: private, include "MMDFactory.h"
#include "UObject/ObjectMacros.h"
#include "UObject/ScriptMacros.h"

PRAGMA_DISABLE_DEPRECATION_WARNINGS
#ifdef MMD_MMDFactory_generated_h
#error "MMDFactory.generated.h already included, missing '#pragma once' in MMDFactory.h"
#endif
#define MMD_MMDFactory_generated_h

#define FID_MMD_Source_MMD_MMDFactory_h_15_INCLASS_NO_PURE_DECLS \
private: \
	static void StaticRegisterNativesUMMDFactory(); \
	friend struct Z_Construct_UClass_UMMDFactory_Statics; \
public: \
	DECLARE_CLASS(UMMDFactory, UFactory, COMPILED_IN_FLAGS(0), CASTCLASS_None, TEXT("/Script/MMD"), NO_API) \
	DECLARE_SERIALIZER(UMMDFactory)


#define FID_MMD_Source_MMD_MMDFactory_h_15_ENHANCED_CONSTRUCTORS \
private: \
	/** Private move- and copy-constructors, should never be used */ \
	UMMDFactory(UMMDFactory&&); \
	UMMDFactory(const UMMDFactory&); \
public: \
	DECLARE_VTABLE_PTR_HELPER_CTOR(NO_API, UMMDFactory); \
	DEFINE_VTABLE_PTR_HELPER_CTOR_CALLER(UMMDFactory); \
	DEFINE_DEFAULT_CONSTRUCTOR_CALL(UMMDFactory) \
	NO_API virtual ~UMMDFactory();


#define FID_MMD_Source_MMD_MMDFactory_h_12_PROLOG
#define FID_MMD_Source_MMD_MMDFactory_h_15_GENERATED_BODY \
PRAGMA_DISABLE_DEPRECATION_WARNINGS \
public: \
	FID_MMD_Source_MMD_MMDFactory_h_15_INCLASS_NO_PURE_DECLS \
	FID_MMD_Source_MMD_MMDFactory_h_15_ENHANCED_CONSTRUCTORS \
private: \
PRAGMA_ENABLE_DEPRECATION_WARNINGS


template<> MMD_API UClass* StaticClass<class UMMDFactory>();

#undef CURRENT_FILE_ID
#define CURRENT_FILE_ID FID_MMD_Source_MMD_MMDFactory_h


PRAGMA_ENABLE_DEPRECATION_WARNINGS

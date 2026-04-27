// Copyright Epic Games, Inc. All Rights Reserved.
/*===========================================================================
	Generated code exported from UnrealHeaderTool.
	DO NOT modify this manually! Edit the corresponding .h files instead!
===========================================================================*/

// IWYU pragma: private, include "AMMDActor.h"
#include "UObject/ObjectMacros.h"
#include "UObject/ScriptMacros.h"

PRAGMA_DISABLE_DEPRECATION_WARNINGS
class USkeletalMeshComponent;
#ifdef UE5MMDTOOLS_AMMDActor_generated_h
#error "AMMDActor.generated.h already included, missing '#pragma once' in AMMDActor.h"
#endif
#define UE5MMDTOOLS_AMMDActor_generated_h

#define FID_MMD_Plugins_Ue5MMDTools_Source_Ue5MMDTools_Public_AMMDActor_h_14_RPC_WRAPPERS_NO_PURE_DECLS \
	DECLARE_FUNCTION(execGetMeshComponent);


#define FID_MMD_Plugins_Ue5MMDTools_Source_Ue5MMDTools_Public_AMMDActor_h_14_INCLASS_NO_PURE_DECLS \
private: \
	static void StaticRegisterNativesAMMDActor(); \
	friend struct Z_Construct_UClass_AMMDActor_Statics; \
public: \
	DECLARE_CLASS(AMMDActor, AActor, COMPILED_IN_FLAGS(0 | CLASS_Config), CASTCLASS_None, TEXT("/Script/Ue5MMDTools"), NO_API) \
	DECLARE_SERIALIZER(AMMDActor)


#define FID_MMD_Plugins_Ue5MMDTools_Source_Ue5MMDTools_Public_AMMDActor_h_14_ENHANCED_CONSTRUCTORS \
private: \
	/** Private move- and copy-constructors, should never be used */ \
	AMMDActor(AMMDActor&&); \
	AMMDActor(const AMMDActor&); \
public: \
	DECLARE_VTABLE_PTR_HELPER_CTOR(NO_API, AMMDActor); \
	DEFINE_VTABLE_PTR_HELPER_CTOR_CALLER(AMMDActor); \
	DEFINE_DEFAULT_CONSTRUCTOR_CALL(AMMDActor) \
	NO_API virtual ~AMMDActor();


#define FID_MMD_Plugins_Ue5MMDTools_Source_Ue5MMDTools_Public_AMMDActor_h_11_PROLOG
#define FID_MMD_Plugins_Ue5MMDTools_Source_Ue5MMDTools_Public_AMMDActor_h_14_GENERATED_BODY \
PRAGMA_DISABLE_DEPRECATION_WARNINGS \
public: \
	FID_MMD_Plugins_Ue5MMDTools_Source_Ue5MMDTools_Public_AMMDActor_h_14_RPC_WRAPPERS_NO_PURE_DECLS \
	FID_MMD_Plugins_Ue5MMDTools_Source_Ue5MMDTools_Public_AMMDActor_h_14_INCLASS_NO_PURE_DECLS \
	FID_MMD_Plugins_Ue5MMDTools_Source_Ue5MMDTools_Public_AMMDActor_h_14_ENHANCED_CONSTRUCTORS \
private: \
PRAGMA_ENABLE_DEPRECATION_WARNINGS


template<> UE5MMDTOOLS_API UClass* StaticClass<class AMMDActor>();

#undef CURRENT_FILE_ID
#define CURRENT_FILE_ID FID_MMD_Plugins_Ue5MMDTools_Source_Ue5MMDTools_Public_AMMDActor_h


PRAGMA_ENABLE_DEPRECATION_WARNINGS

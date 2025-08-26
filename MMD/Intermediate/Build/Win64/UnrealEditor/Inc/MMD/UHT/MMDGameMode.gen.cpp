// Copyright Epic Games, Inc. All Rights Reserved.
/*===========================================================================
	Generated code exported from UnrealHeaderTool.
	DO NOT modify this manually! Edit the corresponding .h files instead!
===========================================================================*/

#include "UObject/GeneratedCppIncludes.h"
#include "MMD/MMDGameMode.h"
PRAGMA_DISABLE_DEPRECATION_WARNINGS
void EmptyLinkFunctionForGeneratedCodeMMDGameMode() {}

// Begin Cross Module References
ENGINE_API UClass* Z_Construct_UClass_AGameModeBase();
MMD_API UClass* Z_Construct_UClass_AMMDGameMode();
MMD_API UClass* Z_Construct_UClass_AMMDGameMode_NoRegister();
UPackage* Z_Construct_UPackage__Script_MMD();
// End Cross Module References

// Begin Class AMMDGameMode
void AMMDGameMode::StaticRegisterNativesAMMDGameMode()
{
}
IMPLEMENT_CLASS_NO_AUTO_REGISTRATION(AMMDGameMode);
UClass* Z_Construct_UClass_AMMDGameMode_NoRegister()
{
	return AMMDGameMode::StaticClass();
}
struct Z_Construct_UClass_AMMDGameMode_Statics
{
#if WITH_METADATA
	static constexpr UECodeGen_Private::FMetaDataPairParam Class_MetaDataParams[] = {
		{ "HideCategories", "Info Rendering MovementReplication Replication Actor Input Movement Collision Rendering HLOD WorldPartition DataLayers Transformation" },
		{ "IncludePath", "MMDGameMode.h" },
		{ "ModuleRelativePath", "MMDGameMode.h" },
		{ "ShowCategories", "Input|MouseInput Input|TouchInput" },
	};
#endif // WITH_METADATA
	static UObject* (*const DependentSingletons[])();
	static constexpr FCppClassTypeInfoStatic StaticCppClassTypeInfo = {
		TCppClassTypeTraits<AMMDGameMode>::IsAbstract,
	};
	static const UECodeGen_Private::FClassParams ClassParams;
};
UObject* (*const Z_Construct_UClass_AMMDGameMode_Statics::DependentSingletons[])() = {
	(UObject* (*)())Z_Construct_UClass_AGameModeBase,
	(UObject* (*)())Z_Construct_UPackage__Script_MMD,
};
static_assert(UE_ARRAY_COUNT(Z_Construct_UClass_AMMDGameMode_Statics::DependentSingletons) < 16);
const UECodeGen_Private::FClassParams Z_Construct_UClass_AMMDGameMode_Statics::ClassParams = {
	&AMMDGameMode::StaticClass,
	"Game",
	&StaticCppClassTypeInfo,
	DependentSingletons,
	nullptr,
	nullptr,
	nullptr,
	UE_ARRAY_COUNT(DependentSingletons),
	0,
	0,
	0,
	0x008802ACu,
	METADATA_PARAMS(UE_ARRAY_COUNT(Z_Construct_UClass_AMMDGameMode_Statics::Class_MetaDataParams), Z_Construct_UClass_AMMDGameMode_Statics::Class_MetaDataParams)
};
UClass* Z_Construct_UClass_AMMDGameMode()
{
	if (!Z_Registration_Info_UClass_AMMDGameMode.OuterSingleton)
	{
		UECodeGen_Private::ConstructUClass(Z_Registration_Info_UClass_AMMDGameMode.OuterSingleton, Z_Construct_UClass_AMMDGameMode_Statics::ClassParams);
	}
	return Z_Registration_Info_UClass_AMMDGameMode.OuterSingleton;
}
template<> MMD_API UClass* StaticClass<AMMDGameMode>()
{
	return AMMDGameMode::StaticClass();
}
DEFINE_VTABLE_PTR_HELPER_CTOR(AMMDGameMode);
AMMDGameMode::~AMMDGameMode() {}
// End Class AMMDGameMode

// Begin Registration
struct Z_CompiledInDeferFile_FID_MMD_Source_MMD_MMDGameMode_h_Statics
{
	static constexpr FClassRegisterCompiledInInfo ClassInfo[] = {
		{ Z_Construct_UClass_AMMDGameMode, AMMDGameMode::StaticClass, TEXT("AMMDGameMode"), &Z_Registration_Info_UClass_AMMDGameMode, CONSTRUCT_RELOAD_VERSION_INFO(FClassReloadVersionInfo, sizeof(AMMDGameMode), 1876134322U) },
	};
};
static FRegisterCompiledInInfo Z_CompiledInDeferFile_FID_MMD_Source_MMD_MMDGameMode_h_1316165480(TEXT("/Script/MMD"),
	Z_CompiledInDeferFile_FID_MMD_Source_MMD_MMDGameMode_h_Statics::ClassInfo, UE_ARRAY_COUNT(Z_CompiledInDeferFile_FID_MMD_Source_MMD_MMDGameMode_h_Statics::ClassInfo),
	nullptr, 0,
	nullptr, 0);
// End Registration
PRAGMA_ENABLE_DEPRECATION_WARNINGS

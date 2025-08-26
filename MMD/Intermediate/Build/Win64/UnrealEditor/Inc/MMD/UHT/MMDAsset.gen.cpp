// Copyright Epic Games, Inc. All Rights Reserved.
/*===========================================================================
	Generated code exported from UnrealHeaderTool.
	DO NOT modify this manually! Edit the corresponding .h files instead!
===========================================================================*/

#include "UObject/GeneratedCppIncludes.h"
#include "MMD/MMDAsset.h"
PRAGMA_DISABLE_DEPRECATION_WARNINGS
void EmptyLinkFunctionForGeneratedCodeMMDAsset() {}

// Begin Cross Module References
COREUOBJECT_API UClass* Z_Construct_UClass_UObject();
MMD_API UClass* Z_Construct_UClass_UMMDAsset();
MMD_API UClass* Z_Construct_UClass_UMMDAsset_NoRegister();
UPackage* Z_Construct_UPackage__Script_MMD();
// End Cross Module References

// Begin Class UMMDAsset
void UMMDAsset::StaticRegisterNativesUMMDAsset()
{
}
IMPLEMENT_CLASS_NO_AUTO_REGISTRATION(UMMDAsset);
UClass* Z_Construct_UClass_UMMDAsset_NoRegister()
{
	return UMMDAsset::StaticClass();
}
struct Z_Construct_UClass_UMMDAsset_Statics
{
#if WITH_METADATA
	static constexpr UECodeGen_Private::FMetaDataPairParam Class_MetaDataParams[] = {
#if !UE_BUILD_SHIPPING
		{ "Comment", "/**\n * \n */" },
#endif
		{ "IncludePath", "MMDAsset.h" },
		{ "ModuleRelativePath", "MMDAsset.h" },
	};
#endif // WITH_METADATA
	static UObject* (*const DependentSingletons[])();
	static constexpr FCppClassTypeInfoStatic StaticCppClassTypeInfo = {
		TCppClassTypeTraits<UMMDAsset>::IsAbstract,
	};
	static const UECodeGen_Private::FClassParams ClassParams;
};
UObject* (*const Z_Construct_UClass_UMMDAsset_Statics::DependentSingletons[])() = {
	(UObject* (*)())Z_Construct_UClass_UObject,
	(UObject* (*)())Z_Construct_UPackage__Script_MMD,
};
static_assert(UE_ARRAY_COUNT(Z_Construct_UClass_UMMDAsset_Statics::DependentSingletons) < 16);
const UECodeGen_Private::FClassParams Z_Construct_UClass_UMMDAsset_Statics::ClassParams = {
	&UMMDAsset::StaticClass,
	nullptr,
	&StaticCppClassTypeInfo,
	DependentSingletons,
	nullptr,
	nullptr,
	nullptr,
	UE_ARRAY_COUNT(DependentSingletons),
	0,
	0,
	0,
	0x001000A0u,
	METADATA_PARAMS(UE_ARRAY_COUNT(Z_Construct_UClass_UMMDAsset_Statics::Class_MetaDataParams), Z_Construct_UClass_UMMDAsset_Statics::Class_MetaDataParams)
};
UClass* Z_Construct_UClass_UMMDAsset()
{
	if (!Z_Registration_Info_UClass_UMMDAsset.OuterSingleton)
	{
		UECodeGen_Private::ConstructUClass(Z_Registration_Info_UClass_UMMDAsset.OuterSingleton, Z_Construct_UClass_UMMDAsset_Statics::ClassParams);
	}
	return Z_Registration_Info_UClass_UMMDAsset.OuterSingleton;
}
template<> MMD_API UClass* StaticClass<UMMDAsset>()
{
	return UMMDAsset::StaticClass();
}
UMMDAsset::UMMDAsset(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {}
DEFINE_VTABLE_PTR_HELPER_CTOR(UMMDAsset);
UMMDAsset::~UMMDAsset() {}
// End Class UMMDAsset

// Begin Registration
struct Z_CompiledInDeferFile_FID_MMD_Source_MMD_MMDAsset_h_Statics
{
	static constexpr FClassRegisterCompiledInInfo ClassInfo[] = {
		{ Z_Construct_UClass_UMMDAsset, UMMDAsset::StaticClass, TEXT("UMMDAsset"), &Z_Registration_Info_UClass_UMMDAsset, CONSTRUCT_RELOAD_VERSION_INFO(FClassReloadVersionInfo, sizeof(UMMDAsset), 4124051162U) },
	};
};
static FRegisterCompiledInInfo Z_CompiledInDeferFile_FID_MMD_Source_MMD_MMDAsset_h_2441145368(TEXT("/Script/MMD"),
	Z_CompiledInDeferFile_FID_MMD_Source_MMD_MMDAsset_h_Statics::ClassInfo, UE_ARRAY_COUNT(Z_CompiledInDeferFile_FID_MMD_Source_MMD_MMDAsset_h_Statics::ClassInfo),
	nullptr, 0,
	nullptr, 0);
// End Registration
PRAGMA_ENABLE_DEPRECATION_WARNINGS

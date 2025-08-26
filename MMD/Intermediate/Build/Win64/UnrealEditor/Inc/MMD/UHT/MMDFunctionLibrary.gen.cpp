// Copyright Epic Games, Inc. All Rights Reserved.
/*===========================================================================
	Generated code exported from UnrealHeaderTool.
	DO NOT modify this manually! Edit the corresponding .h files instead!
===========================================================================*/

#include "UObject/GeneratedCppIncludes.h"
#include "MMD/MMDFunctionLibrary.h"
PRAGMA_DISABLE_DEPRECATION_WARNINGS
void EmptyLinkFunctionForGeneratedCodeMMDFunctionLibrary() {}

// Begin Cross Module References
BLUTILITY_API UClass* Z_Construct_UClass_UEditorUtilityLibrary();
MMD_API UClass* Z_Construct_UClass_UMMDFunctionLibrary();
MMD_API UClass* Z_Construct_UClass_UMMDFunctionLibrary_NoRegister();
UPackage* Z_Construct_UPackage__Script_MMD();
// End Cross Module References

// Begin Class UMMDFunctionLibrary
void UMMDFunctionLibrary::StaticRegisterNativesUMMDFunctionLibrary()
{
}
IMPLEMENT_CLASS_NO_AUTO_REGISTRATION(UMMDFunctionLibrary);
UClass* Z_Construct_UClass_UMMDFunctionLibrary_NoRegister()
{
	return UMMDFunctionLibrary::StaticClass();
}
struct Z_Construct_UClass_UMMDFunctionLibrary_Statics
{
#if WITH_METADATA
	static constexpr UECodeGen_Private::FMetaDataPairParam Class_MetaDataParams[] = {
#if !UE_BUILD_SHIPPING
		{ "Comment", "/**\n * \n */" },
#endif
		{ "IncludePath", "MMDFunctionLibrary.h" },
		{ "ModuleRelativePath", "MMDFunctionLibrary.h" },
	};
#endif // WITH_METADATA
	static UObject* (*const DependentSingletons[])();
	static constexpr FCppClassTypeInfoStatic StaticCppClassTypeInfo = {
		TCppClassTypeTraits<UMMDFunctionLibrary>::IsAbstract,
	};
	static const UECodeGen_Private::FClassParams ClassParams;
};
UObject* (*const Z_Construct_UClass_UMMDFunctionLibrary_Statics::DependentSingletons[])() = {
	(UObject* (*)())Z_Construct_UClass_UEditorUtilityLibrary,
	(UObject* (*)())Z_Construct_UPackage__Script_MMD,
};
static_assert(UE_ARRAY_COUNT(Z_Construct_UClass_UMMDFunctionLibrary_Statics::DependentSingletons) < 16);
const UECodeGen_Private::FClassParams Z_Construct_UClass_UMMDFunctionLibrary_Statics::ClassParams = {
	&UMMDFunctionLibrary::StaticClass,
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
	METADATA_PARAMS(UE_ARRAY_COUNT(Z_Construct_UClass_UMMDFunctionLibrary_Statics::Class_MetaDataParams), Z_Construct_UClass_UMMDFunctionLibrary_Statics::Class_MetaDataParams)
};
UClass* Z_Construct_UClass_UMMDFunctionLibrary()
{
	if (!Z_Registration_Info_UClass_UMMDFunctionLibrary.OuterSingleton)
	{
		UECodeGen_Private::ConstructUClass(Z_Registration_Info_UClass_UMMDFunctionLibrary.OuterSingleton, Z_Construct_UClass_UMMDFunctionLibrary_Statics::ClassParams);
	}
	return Z_Registration_Info_UClass_UMMDFunctionLibrary.OuterSingleton;
}
template<> MMD_API UClass* StaticClass<UMMDFunctionLibrary>()
{
	return UMMDFunctionLibrary::StaticClass();
}
UMMDFunctionLibrary::UMMDFunctionLibrary(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {}
DEFINE_VTABLE_PTR_HELPER_CTOR(UMMDFunctionLibrary);
UMMDFunctionLibrary::~UMMDFunctionLibrary() {}
// End Class UMMDFunctionLibrary

// Begin Registration
struct Z_CompiledInDeferFile_FID_MMD_Source_MMD_MMDFunctionLibrary_h_Statics
{
	static constexpr FClassRegisterCompiledInInfo ClassInfo[] = {
		{ Z_Construct_UClass_UMMDFunctionLibrary, UMMDFunctionLibrary::StaticClass, TEXT("UMMDFunctionLibrary"), &Z_Registration_Info_UClass_UMMDFunctionLibrary, CONSTRUCT_RELOAD_VERSION_INFO(FClassReloadVersionInfo, sizeof(UMMDFunctionLibrary), 4146239321U) },
	};
};
static FRegisterCompiledInInfo Z_CompiledInDeferFile_FID_MMD_Source_MMD_MMDFunctionLibrary_h_1830244227(TEXT("/Script/MMD"),
	Z_CompiledInDeferFile_FID_MMD_Source_MMD_MMDFunctionLibrary_h_Statics::ClassInfo, UE_ARRAY_COUNT(Z_CompiledInDeferFile_FID_MMD_Source_MMD_MMDFunctionLibrary_h_Statics::ClassInfo),
	nullptr, 0,
	nullptr, 0);
// End Registration
PRAGMA_ENABLE_DEPRECATION_WARNINGS

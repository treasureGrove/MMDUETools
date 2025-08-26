// Copyright Epic Games, Inc. All Rights Reserved.
/*===========================================================================
	Generated code exported from UnrealHeaderTool.
	DO NOT modify this manually! Edit the corresponding .h files instead!
===========================================================================*/

#include "UObject/GeneratedCppIncludes.h"
#include "MMD/MMDFactory.h"
PRAGMA_DISABLE_DEPRECATION_WARNINGS
void EmptyLinkFunctionForGeneratedCodeMMDFactory() {}

// Begin Cross Module References
MMD_API UClass* Z_Construct_UClass_UMMDFactory();
MMD_API UClass* Z_Construct_UClass_UMMDFactory_NoRegister();
UNREALED_API UClass* Z_Construct_UClass_UFactory();
UPackage* Z_Construct_UPackage__Script_MMD();
// End Cross Module References

// Begin Class UMMDFactory
void UMMDFactory::StaticRegisterNativesUMMDFactory()
{
}
IMPLEMENT_CLASS_NO_AUTO_REGISTRATION(UMMDFactory);
UClass* Z_Construct_UClass_UMMDFactory_NoRegister()
{
	return UMMDFactory::StaticClass();
}
struct Z_Construct_UClass_UMMDFactory_Statics
{
#if WITH_METADATA
	static constexpr UECodeGen_Private::FMetaDataPairParam Class_MetaDataParams[] = {
#if !UE_BUILD_SHIPPING
		{ "Comment", "/**\n * \n */" },
#endif
		{ "IncludePath", "MMDFactory.h" },
		{ "ModuleRelativePath", "MMDFactory.h" },
	};
#endif // WITH_METADATA
	static UObject* (*const DependentSingletons[])();
	static constexpr FCppClassTypeInfoStatic StaticCppClassTypeInfo = {
		TCppClassTypeTraits<UMMDFactory>::IsAbstract,
	};
	static const UECodeGen_Private::FClassParams ClassParams;
};
UObject* (*const Z_Construct_UClass_UMMDFactory_Statics::DependentSingletons[])() = {
	(UObject* (*)())Z_Construct_UClass_UFactory,
	(UObject* (*)())Z_Construct_UPackage__Script_MMD,
};
static_assert(UE_ARRAY_COUNT(Z_Construct_UClass_UMMDFactory_Statics::DependentSingletons) < 16);
const UECodeGen_Private::FClassParams Z_Construct_UClass_UMMDFactory_Statics::ClassParams = {
	&UMMDFactory::StaticClass,
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
	METADATA_PARAMS(UE_ARRAY_COUNT(Z_Construct_UClass_UMMDFactory_Statics::Class_MetaDataParams), Z_Construct_UClass_UMMDFactory_Statics::Class_MetaDataParams)
};
UClass* Z_Construct_UClass_UMMDFactory()
{
	if (!Z_Registration_Info_UClass_UMMDFactory.OuterSingleton)
	{
		UECodeGen_Private::ConstructUClass(Z_Registration_Info_UClass_UMMDFactory.OuterSingleton, Z_Construct_UClass_UMMDFactory_Statics::ClassParams);
	}
	return Z_Registration_Info_UClass_UMMDFactory.OuterSingleton;
}
template<> MMD_API UClass* StaticClass<UMMDFactory>()
{
	return UMMDFactory::StaticClass();
}
DEFINE_VTABLE_PTR_HELPER_CTOR(UMMDFactory);
UMMDFactory::~UMMDFactory() {}
// End Class UMMDFactory

// Begin Registration
struct Z_CompiledInDeferFile_FID_MMD_Source_MMD_MMDFactory_h_Statics
{
	static constexpr FClassRegisterCompiledInInfo ClassInfo[] = {
		{ Z_Construct_UClass_UMMDFactory, UMMDFactory::StaticClass, TEXT("UMMDFactory"), &Z_Registration_Info_UClass_UMMDFactory, CONSTRUCT_RELOAD_VERSION_INFO(FClassReloadVersionInfo, sizeof(UMMDFactory), 64618550U) },
	};
};
static FRegisterCompiledInInfo Z_CompiledInDeferFile_FID_MMD_Source_MMD_MMDFactory_h_2271642323(TEXT("/Script/MMD"),
	Z_CompiledInDeferFile_FID_MMD_Source_MMD_MMDFactory_h_Statics::ClassInfo, UE_ARRAY_COUNT(Z_CompiledInDeferFile_FID_MMD_Source_MMD_MMDFactory_h_Statics::ClassInfo),
	nullptr, 0,
	nullptr, 0);
// End Registration
PRAGMA_ENABLE_DEPRECATION_WARNINGS

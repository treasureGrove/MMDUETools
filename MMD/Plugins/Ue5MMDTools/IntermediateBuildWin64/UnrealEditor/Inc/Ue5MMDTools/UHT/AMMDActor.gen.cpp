// Copyright Epic Games, Inc. All Rights Reserved.
/*===========================================================================
	Generated code exported from UnrealHeaderTool.
	DO NOT modify this manually! Edit the corresponding .h files instead!
===========================================================================*/

#include "UObject/GeneratedCppIncludes.h"
#include "Ue5MMDTools/Public/AMMDActor.h"
PRAGMA_DISABLE_DEPRECATION_WARNINGS
void EmptyLinkFunctionForGeneratedCodeAMMDActor() {}

// Begin Cross Module References
ENGINE_API UClass* Z_Construct_UClass_AActor();
ENGINE_API UClass* Z_Construct_UClass_UCapsuleComponent_NoRegister();
ENGINE_API UClass* Z_Construct_UClass_USceneComponent_NoRegister();
ENGINE_API UClass* Z_Construct_UClass_USkeletalMeshComponent_NoRegister();
UE5MMDTOOLS_API UClass* Z_Construct_UClass_AMMDActor();
UE5MMDTOOLS_API UClass* Z_Construct_UClass_AMMDActor_NoRegister();
UPackage* Z_Construct_UPackage__Script_Ue5MMDTools();
// End Cross Module References

// Begin Class AMMDActor Function GetMeshComponent
struct Z_Construct_UFunction_AMMDActor_GetMeshComponent_Statics
{
	struct MMDActor_eventGetMeshComponent_Parms
	{
		USkeletalMeshComponent* ReturnValue;
	};
#if WITH_METADATA
	static constexpr UECodeGen_Private::FMetaDataPairParam Function_MetaDataParams[] = {
		{ "Category", "MMD" },
		{ "ModuleRelativePath", "Public/AMMDActor.h" },
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_ReturnValue_MetaData[] = {
		{ "EditInline", "true" },
	};
#endif // WITH_METADATA
	static const UECodeGen_Private::FObjectPropertyParams NewProp_ReturnValue;
	static const UECodeGen_Private::FPropertyParamsBase* const PropPointers[];
	static const UECodeGen_Private::FFunctionParams FuncParams;
};
const UECodeGen_Private::FObjectPropertyParams Z_Construct_UFunction_AMMDActor_GetMeshComponent_Statics::NewProp_ReturnValue = { "ReturnValue", nullptr, (EPropertyFlags)0x0010000000080588, UECodeGen_Private::EPropertyGenFlags::Object, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(MMDActor_eventGetMeshComponent_Parms, ReturnValue), Z_Construct_UClass_USkeletalMeshComponent_NoRegister, METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_ReturnValue_MetaData), NewProp_ReturnValue_MetaData) };
const UECodeGen_Private::FPropertyParamsBase* const Z_Construct_UFunction_AMMDActor_GetMeshComponent_Statics::PropPointers[] = {
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_AMMDActor_GetMeshComponent_Statics::NewProp_ReturnValue,
};
static_assert(UE_ARRAY_COUNT(Z_Construct_UFunction_AMMDActor_GetMeshComponent_Statics::PropPointers) < 2048);
const UECodeGen_Private::FFunctionParams Z_Construct_UFunction_AMMDActor_GetMeshComponent_Statics::FuncParams = { (UObject*(*)())Z_Construct_UClass_AMMDActor, nullptr, "GetMeshComponent", nullptr, nullptr, Z_Construct_UFunction_AMMDActor_GetMeshComponent_Statics::PropPointers, UE_ARRAY_COUNT(Z_Construct_UFunction_AMMDActor_GetMeshComponent_Statics::PropPointers), sizeof(Z_Construct_UFunction_AMMDActor_GetMeshComponent_Statics::MMDActor_eventGetMeshComponent_Parms), RF_Public|RF_Transient|RF_MarkAsNative, (EFunctionFlags)0x54020401, 0, 0, METADATA_PARAMS(UE_ARRAY_COUNT(Z_Construct_UFunction_AMMDActor_GetMeshComponent_Statics::Function_MetaDataParams), Z_Construct_UFunction_AMMDActor_GetMeshComponent_Statics::Function_MetaDataParams) };
static_assert(sizeof(Z_Construct_UFunction_AMMDActor_GetMeshComponent_Statics::MMDActor_eventGetMeshComponent_Parms) < MAX_uint16);
UFunction* Z_Construct_UFunction_AMMDActor_GetMeshComponent()
{
	static UFunction* ReturnFunction = nullptr;
	if (!ReturnFunction)
	{
		UECodeGen_Private::ConstructUFunction(&ReturnFunction, Z_Construct_UFunction_AMMDActor_GetMeshComponent_Statics::FuncParams);
	}
	return ReturnFunction;
}
DEFINE_FUNCTION(AMMDActor::execGetMeshComponent)
{
	P_FINISH;
	P_NATIVE_BEGIN;
	*(USkeletalMeshComponent**)Z_Param__Result=P_THIS->GetMeshComponent();
	P_NATIVE_END;
}
// End Class AMMDActor Function GetMeshComponent

// Begin Class AMMDActor
void AMMDActor::StaticRegisterNativesAMMDActor()
{
	UClass* Class = AMMDActor::StaticClass();
	static const FNameNativePtrPair Funcs[] = {
		{ "GetMeshComponent", &AMMDActor::execGetMeshComponent },
	};
	FNativeFunctionRegistrar::RegisterFunctions(Class, Funcs, UE_ARRAY_COUNT(Funcs));
}
IMPLEMENT_CLASS_NO_AUTO_REGISTRATION(AMMDActor);
UClass* Z_Construct_UClass_AMMDActor_NoRegister()
{
	return AMMDActor::StaticClass();
}
struct Z_Construct_UClass_AMMDActor_Statics
{
#if WITH_METADATA
	static constexpr UECodeGen_Private::FMetaDataPairParam Class_MetaDataParams[] = {
		{ "IncludePath", "AMMDActor.h" },
		{ "ModuleRelativePath", "Public/AMMDActor.h" },
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_SourcePMXFilePath_MetaData[] = {
		{ "Category", "MMD" },
#if !UE_BUILD_SHIPPING
		{ "Comment", "/*void InitializeMMDPhysics(UAnimGraphNode_MMDSkeletalControl* MMDNode,const PMXDatas& PMXData);*/" },
#endif
		{ "ModuleRelativePath", "Public/AMMDActor.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "void InitializeMMDPhysics(UAnimGraphNode_MMDSkeletalControl* MMDNode,const PMXDatas& PMXData);" },
#endif
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_SkeletalMeshComponent_MetaData[] = {
		{ "Category", "MMD" },
#if !UE_BUILD_SHIPPING
		{ "Comment", "//\xef\xbf\xbd\xef\xbf\xbd\xcd\xbc\xef\xbf\xbd\xef\xbf\xbd\xef\xbf\xbd\n" },
#endif
		{ "EditInline", "true" },
		{ "ModuleRelativePath", "Public/AMMDActor.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "\xef\xbf\xbd\xef\xbf\xbd\xcd\xbc\xef\xbf\xbd\xef\xbf\xbd\xef\xbf\xbd" },
#endif
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_CapsuleComponent_MetaData[] = {
		{ "Category", "MMD" },
		{ "EditInline", "true" },
		{ "ModuleRelativePath", "Public/AMMDActor.h" },
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_RootSceneComponent_MetaData[] = {
		{ "Category", "MMD" },
		{ "EditInline", "true" },
		{ "ModuleRelativePath", "Public/AMMDActor.h" },
	};
#endif // WITH_METADATA
	static const UECodeGen_Private::FStrPropertyParams NewProp_SourcePMXFilePath;
	static const UECodeGen_Private::FObjectPropertyParams NewProp_SkeletalMeshComponent;
	static const UECodeGen_Private::FObjectPropertyParams NewProp_CapsuleComponent;
	static const UECodeGen_Private::FObjectPropertyParams NewProp_RootSceneComponent;
	static const UECodeGen_Private::FPropertyParamsBase* const PropPointers[];
	static UObject* (*const DependentSingletons[])();
	static constexpr FClassFunctionLinkInfo FuncInfo[] = {
		{ &Z_Construct_UFunction_AMMDActor_GetMeshComponent, "GetMeshComponent" }, // 1098291583
	};
	static_assert(UE_ARRAY_COUNT(FuncInfo) < 2048);
	static constexpr FCppClassTypeInfoStatic StaticCppClassTypeInfo = {
		TCppClassTypeTraits<AMMDActor>::IsAbstract,
	};
	static const UECodeGen_Private::FClassParams ClassParams;
};
const UECodeGen_Private::FStrPropertyParams Z_Construct_UClass_AMMDActor_Statics::NewProp_SourcePMXFilePath = { "SourcePMXFilePath", nullptr, (EPropertyFlags)0x0010000000000001, UECodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(AMMDActor, SourcePMXFilePath), METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_SourcePMXFilePath_MetaData), NewProp_SourcePMXFilePath_MetaData) };
const UECodeGen_Private::FObjectPropertyParams Z_Construct_UClass_AMMDActor_Statics::NewProp_SkeletalMeshComponent = { "SkeletalMeshComponent", nullptr, (EPropertyFlags)0x00400000000a0009, UECodeGen_Private::EPropertyGenFlags::Object, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(AMMDActor, SkeletalMeshComponent), Z_Construct_UClass_USkeletalMeshComponent_NoRegister, METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_SkeletalMeshComponent_MetaData), NewProp_SkeletalMeshComponent_MetaData) };
const UECodeGen_Private::FObjectPropertyParams Z_Construct_UClass_AMMDActor_Statics::NewProp_CapsuleComponent = { "CapsuleComponent", nullptr, (EPropertyFlags)0x00400000000a0009, UECodeGen_Private::EPropertyGenFlags::Object, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(AMMDActor, CapsuleComponent), Z_Construct_UClass_UCapsuleComponent_NoRegister, METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_CapsuleComponent_MetaData), NewProp_CapsuleComponent_MetaData) };
const UECodeGen_Private::FObjectPropertyParams Z_Construct_UClass_AMMDActor_Statics::NewProp_RootSceneComponent = { "RootSceneComponent", nullptr, (EPropertyFlags)0x00400000000a0009, UECodeGen_Private::EPropertyGenFlags::Object, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(AMMDActor, RootSceneComponent), Z_Construct_UClass_USceneComponent_NoRegister, METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_RootSceneComponent_MetaData), NewProp_RootSceneComponent_MetaData) };
const UECodeGen_Private::FPropertyParamsBase* const Z_Construct_UClass_AMMDActor_Statics::PropPointers[] = {
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UClass_AMMDActor_Statics::NewProp_SourcePMXFilePath,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UClass_AMMDActor_Statics::NewProp_SkeletalMeshComponent,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UClass_AMMDActor_Statics::NewProp_CapsuleComponent,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UClass_AMMDActor_Statics::NewProp_RootSceneComponent,
};
static_assert(UE_ARRAY_COUNT(Z_Construct_UClass_AMMDActor_Statics::PropPointers) < 2048);
UObject* (*const Z_Construct_UClass_AMMDActor_Statics::DependentSingletons[])() = {
	(UObject* (*)())Z_Construct_UClass_AActor,
	(UObject* (*)())Z_Construct_UPackage__Script_Ue5MMDTools,
};
static_assert(UE_ARRAY_COUNT(Z_Construct_UClass_AMMDActor_Statics::DependentSingletons) < 16);
const UECodeGen_Private::FClassParams Z_Construct_UClass_AMMDActor_Statics::ClassParams = {
	&AMMDActor::StaticClass,
	"Engine",
	&StaticCppClassTypeInfo,
	DependentSingletons,
	FuncInfo,
	Z_Construct_UClass_AMMDActor_Statics::PropPointers,
	nullptr,
	UE_ARRAY_COUNT(DependentSingletons),
	UE_ARRAY_COUNT(FuncInfo),
	UE_ARRAY_COUNT(Z_Construct_UClass_AMMDActor_Statics::PropPointers),
	0,
	0x009000A4u,
	METADATA_PARAMS(UE_ARRAY_COUNT(Z_Construct_UClass_AMMDActor_Statics::Class_MetaDataParams), Z_Construct_UClass_AMMDActor_Statics::Class_MetaDataParams)
};
UClass* Z_Construct_UClass_AMMDActor()
{
	if (!Z_Registration_Info_UClass_AMMDActor.OuterSingleton)
	{
		UECodeGen_Private::ConstructUClass(Z_Registration_Info_UClass_AMMDActor.OuterSingleton, Z_Construct_UClass_AMMDActor_Statics::ClassParams);
	}
	return Z_Registration_Info_UClass_AMMDActor.OuterSingleton;
}
template<> UE5MMDTOOLS_API UClass* StaticClass<AMMDActor>()
{
	return AMMDActor::StaticClass();
}
DEFINE_VTABLE_PTR_HELPER_CTOR(AMMDActor);
AMMDActor::~AMMDActor() {}
// End Class AMMDActor

// Begin Registration
struct Z_CompiledInDeferFile_FID_MMD_Plugins_Ue5MMDTools_Source_Ue5MMDTools_Public_AMMDActor_h_Statics
{
	static constexpr FClassRegisterCompiledInInfo ClassInfo[] = {
		{ Z_Construct_UClass_AMMDActor, AMMDActor::StaticClass, TEXT("AMMDActor"), &Z_Registration_Info_UClass_AMMDActor, CONSTRUCT_RELOAD_VERSION_INFO(FClassReloadVersionInfo, sizeof(AMMDActor), 1078633235U) },
	};
};
static FRegisterCompiledInInfo Z_CompiledInDeferFile_FID_MMD_Plugins_Ue5MMDTools_Source_Ue5MMDTools_Public_AMMDActor_h_1120399541(TEXT("/Script/Ue5MMDTools"),
	Z_CompiledInDeferFile_FID_MMD_Plugins_Ue5MMDTools_Source_Ue5MMDTools_Public_AMMDActor_h_Statics::ClassInfo, UE_ARRAY_COUNT(Z_CompiledInDeferFile_FID_MMD_Plugins_Ue5MMDTools_Source_Ue5MMDTools_Public_AMMDActor_h_Statics::ClassInfo),
	nullptr, 0,
	nullptr, 0);
// End Registration
PRAGMA_ENABLE_DEPRECATION_WARNINGS

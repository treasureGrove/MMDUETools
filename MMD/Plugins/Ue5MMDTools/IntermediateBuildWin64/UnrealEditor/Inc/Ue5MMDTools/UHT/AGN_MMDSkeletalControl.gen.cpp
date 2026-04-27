// Copyright Epic Games, Inc. All Rights Reserved.
/*===========================================================================
	Generated code exported from UnrealHeaderTool.
	DO NOT modify this manually! Edit the corresponding .h files instead!
===========================================================================*/

#include "UObject/GeneratedCppIncludes.h"
#include "Ue5MMDTools/Public/AGN_MMDSkeletalControl.h"
PRAGMA_DISABLE_DEPRECATION_WARNINGS
void EmptyLinkFunctionForGeneratedCodeAGN_MMDSkeletalControl() {}

// Begin Cross Module References
ANIMGRAPH_API UClass* Z_Construct_UClass_UAnimGraphNode_SkeletalControlBase();
ANIMGRAPHRUNTIME_API UScriptStruct* Z_Construct_UScriptStruct_FAnimNode_SkeletalControlBase();
COREUOBJECT_API UScriptStruct* Z_Construct_UScriptStruct_FVector();
UE5MMDTOOLS_API UClass* Z_Construct_UClass_UAnimGraphNode_MMDSkeletalControl();
UE5MMDTOOLS_API UClass* Z_Construct_UClass_UAnimGraphNode_MMDSkeletalControl_NoRegister();
UE5MMDTOOLS_API UScriptStruct* Z_Construct_UScriptStruct_FAGN_MMDSkeletalControl();
UE5MMDTOOLS_API UScriptStruct* Z_Construct_UScriptStruct_FMMDPhysicsJointData();
UE5MMDTOOLS_API UScriptStruct* Z_Construct_UScriptStruct_FMMDPhysicsRigidBodyData();
UPackage* Z_Construct_UPackage__Script_Ue5MMDTools();
// End Cross Module References

// Begin ScriptStruct FMMDPhysicsRigidBodyData
static FStructRegistrationInfo Z_Registration_Info_UScriptStruct_MMDPhysicsRigidBodyData;
class UScriptStruct* FMMDPhysicsRigidBodyData::StaticStruct()
{
	if (!Z_Registration_Info_UScriptStruct_MMDPhysicsRigidBodyData.OuterSingleton)
	{
		Z_Registration_Info_UScriptStruct_MMDPhysicsRigidBodyData.OuterSingleton = GetStaticStruct(Z_Construct_UScriptStruct_FMMDPhysicsRigidBodyData, (UObject*)Z_Construct_UPackage__Script_Ue5MMDTools(), TEXT("MMDPhysicsRigidBodyData"));
	}
	return Z_Registration_Info_UScriptStruct_MMDPhysicsRigidBodyData.OuterSingleton;
}
template<> UE5MMDTOOLS_API UScriptStruct* StaticStruct<FMMDPhysicsRigidBodyData>()
{
	return FMMDPhysicsRigidBodyData::StaticStruct();
}
struct Z_Construct_UScriptStruct_FMMDPhysicsRigidBodyData_Statics
{
#if WITH_METADATA
	static constexpr UECodeGen_Private::FMetaDataPairParam Struct_MetaDataParams[] = {
		{ "BlueprintType", "true" },
		{ "ModuleRelativePath", "Public/AGN_MMDSkeletalControl.h" },
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_Name_MetaData[] = {
		{ "ModuleRelativePath", "Public/AGN_MMDSkeletalControl.h" },
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_NameEN_MetaData[] = {
		{ "ModuleRelativePath", "Public/AGN_MMDSkeletalControl.h" },
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_RelatedBoneIndex_MetaData[] = {
		{ "ModuleRelativePath", "Public/AGN_MMDSkeletalControl.h" },
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_ShapeType_MetaData[] = {
		{ "ModuleRelativePath", "Public/AGN_MMDSkeletalControl.h" },
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_ShapeSize_MetaData[] = {
#if !UE_BUILD_SHIPPING
		{ "Comment", "// Sphere/Box/Capsule\n" },
#endif
		{ "ModuleRelativePath", "Public/AGN_MMDSkeletalControl.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "Sphere/Box/Capsule" },
#endif
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_ShapePosition_MetaData[] = {
		{ "ModuleRelativePath", "Public/AGN_MMDSkeletalControl.h" },
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_ShapeRotation_MetaData[] = {
		{ "ModuleRelativePath", "Public/AGN_MMDSkeletalControl.h" },
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_Mass_MetaData[] = {
		{ "ModuleRelativePath", "Public/AGN_MMDSkeletalControl.h" },
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_Friction_MetaData[] = {
		{ "ModuleRelativePath", "Public/AGN_MMDSkeletalControl.h" },
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_Restitution_MetaData[] = {
		{ "ModuleRelativePath", "Public/AGN_MMDSkeletalControl.h" },
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_CollisionGroup_MetaData[] = {
		{ "ModuleRelativePath", "Public/AGN_MMDSkeletalControl.h" },
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_CollisionMask_MetaData[] = {
		{ "ModuleRelativePath", "Public/AGN_MMDSkeletalControl.h" },
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_PhysicsMode_MetaData[] = {
		{ "ModuleRelativePath", "Public/AGN_MMDSkeletalControl.h" },
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_LinearDamping_MetaData[] = {
		{ "ModuleRelativePath", "Public/AGN_MMDSkeletalControl.h" },
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_AngularDamping_MetaData[] = {
		{ "ModuleRelativePath", "Public/AGN_MMDSkeletalControl.h" },
	};
#endif // WITH_METADATA
	static const UECodeGen_Private::FStrPropertyParams NewProp_Name;
	static const UECodeGen_Private::FStrPropertyParams NewProp_NameEN;
	static const UECodeGen_Private::FIntPropertyParams NewProp_RelatedBoneIndex;
	static const UECodeGen_Private::FIntPropertyParams NewProp_ShapeType;
	static const UECodeGen_Private::FStructPropertyParams NewProp_ShapeSize;
	static const UECodeGen_Private::FStructPropertyParams NewProp_ShapePosition;
	static const UECodeGen_Private::FStructPropertyParams NewProp_ShapeRotation;
	static const UECodeGen_Private::FFloatPropertyParams NewProp_Mass;
	static const UECodeGen_Private::FFloatPropertyParams NewProp_Friction;
	static const UECodeGen_Private::FFloatPropertyParams NewProp_Restitution;
	static const UECodeGen_Private::FIntPropertyParams NewProp_CollisionGroup;
	static const UECodeGen_Private::FIntPropertyParams NewProp_CollisionMask;
	static const UECodeGen_Private::FIntPropertyParams NewProp_PhysicsMode;
	static const UECodeGen_Private::FFloatPropertyParams NewProp_LinearDamping;
	static const UECodeGen_Private::FFloatPropertyParams NewProp_AngularDamping;
	static const UECodeGen_Private::FPropertyParamsBase* const PropPointers[];
	static void* NewStructOps()
	{
		return (UScriptStruct::ICppStructOps*)new UScriptStruct::TCppStructOps<FMMDPhysicsRigidBodyData>();
	}
	static const UECodeGen_Private::FStructParams StructParams;
};
const UECodeGen_Private::FStrPropertyParams Z_Construct_UScriptStruct_FMMDPhysicsRigidBodyData_Statics::NewProp_Name = { "Name", nullptr, (EPropertyFlags)0x0010000000000000, UECodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(FMMDPhysicsRigidBodyData, Name), METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_Name_MetaData), NewProp_Name_MetaData) };
const UECodeGen_Private::FStrPropertyParams Z_Construct_UScriptStruct_FMMDPhysicsRigidBodyData_Statics::NewProp_NameEN = { "NameEN", nullptr, (EPropertyFlags)0x0010000000000000, UECodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(FMMDPhysicsRigidBodyData, NameEN), METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_NameEN_MetaData), NewProp_NameEN_MetaData) };
const UECodeGen_Private::FIntPropertyParams Z_Construct_UScriptStruct_FMMDPhysicsRigidBodyData_Statics::NewProp_RelatedBoneIndex = { "RelatedBoneIndex", nullptr, (EPropertyFlags)0x0010000000000000, UECodeGen_Private::EPropertyGenFlags::Int, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(FMMDPhysicsRigidBodyData, RelatedBoneIndex), METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_RelatedBoneIndex_MetaData), NewProp_RelatedBoneIndex_MetaData) };
const UECodeGen_Private::FIntPropertyParams Z_Construct_UScriptStruct_FMMDPhysicsRigidBodyData_Statics::NewProp_ShapeType = { "ShapeType", nullptr, (EPropertyFlags)0x0010000000000000, UECodeGen_Private::EPropertyGenFlags::Int, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(FMMDPhysicsRigidBodyData, ShapeType), METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_ShapeType_MetaData), NewProp_ShapeType_MetaData) };
const UECodeGen_Private::FStructPropertyParams Z_Construct_UScriptStruct_FMMDPhysicsRigidBodyData_Statics::NewProp_ShapeSize = { "ShapeSize", nullptr, (EPropertyFlags)0x0010000000000000, UECodeGen_Private::EPropertyGenFlags::Struct, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(FMMDPhysicsRigidBodyData, ShapeSize), Z_Construct_UScriptStruct_FVector, METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_ShapeSize_MetaData), NewProp_ShapeSize_MetaData) };
const UECodeGen_Private::FStructPropertyParams Z_Construct_UScriptStruct_FMMDPhysicsRigidBodyData_Statics::NewProp_ShapePosition = { "ShapePosition", nullptr, (EPropertyFlags)0x0010000000000000, UECodeGen_Private::EPropertyGenFlags::Struct, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(FMMDPhysicsRigidBodyData, ShapePosition), Z_Construct_UScriptStruct_FVector, METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_ShapePosition_MetaData), NewProp_ShapePosition_MetaData) };
const UECodeGen_Private::FStructPropertyParams Z_Construct_UScriptStruct_FMMDPhysicsRigidBodyData_Statics::NewProp_ShapeRotation = { "ShapeRotation", nullptr, (EPropertyFlags)0x0010000000000000, UECodeGen_Private::EPropertyGenFlags::Struct, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(FMMDPhysicsRigidBodyData, ShapeRotation), Z_Construct_UScriptStruct_FVector, METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_ShapeRotation_MetaData), NewProp_ShapeRotation_MetaData) };
const UECodeGen_Private::FFloatPropertyParams Z_Construct_UScriptStruct_FMMDPhysicsRigidBodyData_Statics::NewProp_Mass = { "Mass", nullptr, (EPropertyFlags)0x0010000000000000, UECodeGen_Private::EPropertyGenFlags::Float, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(FMMDPhysicsRigidBodyData, Mass), METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_Mass_MetaData), NewProp_Mass_MetaData) };
const UECodeGen_Private::FFloatPropertyParams Z_Construct_UScriptStruct_FMMDPhysicsRigidBodyData_Statics::NewProp_Friction = { "Friction", nullptr, (EPropertyFlags)0x0010000000000000, UECodeGen_Private::EPropertyGenFlags::Float, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(FMMDPhysicsRigidBodyData, Friction), METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_Friction_MetaData), NewProp_Friction_MetaData) };
const UECodeGen_Private::FFloatPropertyParams Z_Construct_UScriptStruct_FMMDPhysicsRigidBodyData_Statics::NewProp_Restitution = { "Restitution", nullptr, (EPropertyFlags)0x0010000000000000, UECodeGen_Private::EPropertyGenFlags::Float, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(FMMDPhysicsRigidBodyData, Restitution), METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_Restitution_MetaData), NewProp_Restitution_MetaData) };
const UECodeGen_Private::FIntPropertyParams Z_Construct_UScriptStruct_FMMDPhysicsRigidBodyData_Statics::NewProp_CollisionGroup = { "CollisionGroup", nullptr, (EPropertyFlags)0x0010000000000000, UECodeGen_Private::EPropertyGenFlags::Int, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(FMMDPhysicsRigidBodyData, CollisionGroup), METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_CollisionGroup_MetaData), NewProp_CollisionGroup_MetaData) };
const UECodeGen_Private::FIntPropertyParams Z_Construct_UScriptStruct_FMMDPhysicsRigidBodyData_Statics::NewProp_CollisionMask = { "CollisionMask", nullptr, (EPropertyFlags)0x0010000000000000, UECodeGen_Private::EPropertyGenFlags::Int, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(FMMDPhysicsRigidBodyData, CollisionMask), METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_CollisionMask_MetaData), NewProp_CollisionMask_MetaData) };
const UECodeGen_Private::FIntPropertyParams Z_Construct_UScriptStruct_FMMDPhysicsRigidBodyData_Statics::NewProp_PhysicsMode = { "PhysicsMode", nullptr, (EPropertyFlags)0x0010000000000000, UECodeGen_Private::EPropertyGenFlags::Int, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(FMMDPhysicsRigidBodyData, PhysicsMode), METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_PhysicsMode_MetaData), NewProp_PhysicsMode_MetaData) };
const UECodeGen_Private::FFloatPropertyParams Z_Construct_UScriptStruct_FMMDPhysicsRigidBodyData_Statics::NewProp_LinearDamping = { "LinearDamping", nullptr, (EPropertyFlags)0x0010000000000000, UECodeGen_Private::EPropertyGenFlags::Float, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(FMMDPhysicsRigidBodyData, LinearDamping), METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_LinearDamping_MetaData), NewProp_LinearDamping_MetaData) };
const UECodeGen_Private::FFloatPropertyParams Z_Construct_UScriptStruct_FMMDPhysicsRigidBodyData_Statics::NewProp_AngularDamping = { "AngularDamping", nullptr, (EPropertyFlags)0x0010000000000000, UECodeGen_Private::EPropertyGenFlags::Float, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(FMMDPhysicsRigidBodyData, AngularDamping), METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_AngularDamping_MetaData), NewProp_AngularDamping_MetaData) };
const UECodeGen_Private::FPropertyParamsBase* const Z_Construct_UScriptStruct_FMMDPhysicsRigidBodyData_Statics::PropPointers[] = {
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UScriptStruct_FMMDPhysicsRigidBodyData_Statics::NewProp_Name,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UScriptStruct_FMMDPhysicsRigidBodyData_Statics::NewProp_NameEN,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UScriptStruct_FMMDPhysicsRigidBodyData_Statics::NewProp_RelatedBoneIndex,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UScriptStruct_FMMDPhysicsRigidBodyData_Statics::NewProp_ShapeType,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UScriptStruct_FMMDPhysicsRigidBodyData_Statics::NewProp_ShapeSize,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UScriptStruct_FMMDPhysicsRigidBodyData_Statics::NewProp_ShapePosition,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UScriptStruct_FMMDPhysicsRigidBodyData_Statics::NewProp_ShapeRotation,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UScriptStruct_FMMDPhysicsRigidBodyData_Statics::NewProp_Mass,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UScriptStruct_FMMDPhysicsRigidBodyData_Statics::NewProp_Friction,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UScriptStruct_FMMDPhysicsRigidBodyData_Statics::NewProp_Restitution,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UScriptStruct_FMMDPhysicsRigidBodyData_Statics::NewProp_CollisionGroup,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UScriptStruct_FMMDPhysicsRigidBodyData_Statics::NewProp_CollisionMask,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UScriptStruct_FMMDPhysicsRigidBodyData_Statics::NewProp_PhysicsMode,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UScriptStruct_FMMDPhysicsRigidBodyData_Statics::NewProp_LinearDamping,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UScriptStruct_FMMDPhysicsRigidBodyData_Statics::NewProp_AngularDamping,
};
static_assert(UE_ARRAY_COUNT(Z_Construct_UScriptStruct_FMMDPhysicsRigidBodyData_Statics::PropPointers) < 2048);
const UECodeGen_Private::FStructParams Z_Construct_UScriptStruct_FMMDPhysicsRigidBodyData_Statics::StructParams = {
	(UObject* (*)())Z_Construct_UPackage__Script_Ue5MMDTools,
	nullptr,
	&NewStructOps,
	"MMDPhysicsRigidBodyData",
	Z_Construct_UScriptStruct_FMMDPhysicsRigidBodyData_Statics::PropPointers,
	UE_ARRAY_COUNT(Z_Construct_UScriptStruct_FMMDPhysicsRigidBodyData_Statics::PropPointers),
	sizeof(FMMDPhysicsRigidBodyData),
	alignof(FMMDPhysicsRigidBodyData),
	RF_Public|RF_Transient|RF_MarkAsNative,
	EStructFlags(0x00000201),
	METADATA_PARAMS(UE_ARRAY_COUNT(Z_Construct_UScriptStruct_FMMDPhysicsRigidBodyData_Statics::Struct_MetaDataParams), Z_Construct_UScriptStruct_FMMDPhysicsRigidBodyData_Statics::Struct_MetaDataParams)
};
UScriptStruct* Z_Construct_UScriptStruct_FMMDPhysicsRigidBodyData()
{
	if (!Z_Registration_Info_UScriptStruct_MMDPhysicsRigidBodyData.InnerSingleton)
	{
		UECodeGen_Private::ConstructUScriptStruct(Z_Registration_Info_UScriptStruct_MMDPhysicsRigidBodyData.InnerSingleton, Z_Construct_UScriptStruct_FMMDPhysicsRigidBodyData_Statics::StructParams);
	}
	return Z_Registration_Info_UScriptStruct_MMDPhysicsRigidBodyData.InnerSingleton;
}
// End ScriptStruct FMMDPhysicsRigidBodyData

// Begin ScriptStruct FMMDPhysicsJointData
static FStructRegistrationInfo Z_Registration_Info_UScriptStruct_MMDPhysicsJointData;
class UScriptStruct* FMMDPhysicsJointData::StaticStruct()
{
	if (!Z_Registration_Info_UScriptStruct_MMDPhysicsJointData.OuterSingleton)
	{
		Z_Registration_Info_UScriptStruct_MMDPhysicsJointData.OuterSingleton = GetStaticStruct(Z_Construct_UScriptStruct_FMMDPhysicsJointData, (UObject*)Z_Construct_UPackage__Script_Ue5MMDTools(), TEXT("MMDPhysicsJointData"));
	}
	return Z_Registration_Info_UScriptStruct_MMDPhysicsJointData.OuterSingleton;
}
template<> UE5MMDTOOLS_API UScriptStruct* StaticStruct<FMMDPhysicsJointData>()
{
	return FMMDPhysicsJointData::StaticStruct();
}
struct Z_Construct_UScriptStruct_FMMDPhysicsJointData_Statics
{
#if WITH_METADATA
	static constexpr UECodeGen_Private::FMetaDataPairParam Struct_MetaDataParams[] = {
		{ "BlueprintType", "true" },
		{ "ModuleRelativePath", "Public/AGN_MMDSkeletalControl.h" },
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_Name_MetaData[] = {
		{ "ModuleRelativePath", "Public/AGN_MMDSkeletalControl.h" },
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_NameEN_MetaData[] = {
		{ "ModuleRelativePath", "Public/AGN_MMDSkeletalControl.h" },
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_JointType_MetaData[] = {
		{ "ModuleRelativePath", "Public/AGN_MMDSkeletalControl.h" },
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_RigidBodyIndexA_MetaData[] = {
		{ "ModuleRelativePath", "Public/AGN_MMDSkeletalControl.h" },
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_RigidBodyIndexB_MetaData[] = {
		{ "ModuleRelativePath", "Public/AGN_MMDSkeletalControl.h" },
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_Position_MetaData[] = {
		{ "ModuleRelativePath", "Public/AGN_MMDSkeletalControl.h" },
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_Rotation_MetaData[] = {
		{ "ModuleRelativePath", "Public/AGN_MMDSkeletalControl.h" },
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_LimitPositionMin_MetaData[] = {
		{ "ModuleRelativePath", "Public/AGN_MMDSkeletalControl.h" },
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_LimitPositionMax_MetaData[] = {
		{ "ModuleRelativePath", "Public/AGN_MMDSkeletalControl.h" },
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_LimitRotationMin_MetaData[] = {
		{ "ModuleRelativePath", "Public/AGN_MMDSkeletalControl.h" },
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_LimitRotationMax_MetaData[] = {
		{ "ModuleRelativePath", "Public/AGN_MMDSkeletalControl.h" },
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_SpringPosition_MetaData[] = {
		{ "ModuleRelativePath", "Public/AGN_MMDSkeletalControl.h" },
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_SpringRotation_MetaData[] = {
		{ "ModuleRelativePath", "Public/AGN_MMDSkeletalControl.h" },
	};
#endif // WITH_METADATA
	static const UECodeGen_Private::FStrPropertyParams NewProp_Name;
	static const UECodeGen_Private::FStrPropertyParams NewProp_NameEN;
	static const UECodeGen_Private::FIntPropertyParams NewProp_JointType;
	static const UECodeGen_Private::FIntPropertyParams NewProp_RigidBodyIndexA;
	static const UECodeGen_Private::FIntPropertyParams NewProp_RigidBodyIndexB;
	static const UECodeGen_Private::FStructPropertyParams NewProp_Position;
	static const UECodeGen_Private::FStructPropertyParams NewProp_Rotation;
	static const UECodeGen_Private::FStructPropertyParams NewProp_LimitPositionMin;
	static const UECodeGen_Private::FStructPropertyParams NewProp_LimitPositionMax;
	static const UECodeGen_Private::FStructPropertyParams NewProp_LimitRotationMin;
	static const UECodeGen_Private::FStructPropertyParams NewProp_LimitRotationMax;
	static const UECodeGen_Private::FStructPropertyParams NewProp_SpringPosition;
	static const UECodeGen_Private::FStructPropertyParams NewProp_SpringRotation;
	static const UECodeGen_Private::FPropertyParamsBase* const PropPointers[];
	static void* NewStructOps()
	{
		return (UScriptStruct::ICppStructOps*)new UScriptStruct::TCppStructOps<FMMDPhysicsJointData>();
	}
	static const UECodeGen_Private::FStructParams StructParams;
};
const UECodeGen_Private::FStrPropertyParams Z_Construct_UScriptStruct_FMMDPhysicsJointData_Statics::NewProp_Name = { "Name", nullptr, (EPropertyFlags)0x0010000000000000, UECodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(FMMDPhysicsJointData, Name), METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_Name_MetaData), NewProp_Name_MetaData) };
const UECodeGen_Private::FStrPropertyParams Z_Construct_UScriptStruct_FMMDPhysicsJointData_Statics::NewProp_NameEN = { "NameEN", nullptr, (EPropertyFlags)0x0010000000000000, UECodeGen_Private::EPropertyGenFlags::Str, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(FMMDPhysicsJointData, NameEN), METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_NameEN_MetaData), NewProp_NameEN_MetaData) };
const UECodeGen_Private::FIntPropertyParams Z_Construct_UScriptStruct_FMMDPhysicsJointData_Statics::NewProp_JointType = { "JointType", nullptr, (EPropertyFlags)0x0010000000000000, UECodeGen_Private::EPropertyGenFlags::Int, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(FMMDPhysicsJointData, JointType), METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_JointType_MetaData), NewProp_JointType_MetaData) };
const UECodeGen_Private::FIntPropertyParams Z_Construct_UScriptStruct_FMMDPhysicsJointData_Statics::NewProp_RigidBodyIndexA = { "RigidBodyIndexA", nullptr, (EPropertyFlags)0x0010000000000000, UECodeGen_Private::EPropertyGenFlags::Int, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(FMMDPhysicsJointData, RigidBodyIndexA), METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_RigidBodyIndexA_MetaData), NewProp_RigidBodyIndexA_MetaData) };
const UECodeGen_Private::FIntPropertyParams Z_Construct_UScriptStruct_FMMDPhysicsJointData_Statics::NewProp_RigidBodyIndexB = { "RigidBodyIndexB", nullptr, (EPropertyFlags)0x0010000000000000, UECodeGen_Private::EPropertyGenFlags::Int, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(FMMDPhysicsJointData, RigidBodyIndexB), METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_RigidBodyIndexB_MetaData), NewProp_RigidBodyIndexB_MetaData) };
const UECodeGen_Private::FStructPropertyParams Z_Construct_UScriptStruct_FMMDPhysicsJointData_Statics::NewProp_Position = { "Position", nullptr, (EPropertyFlags)0x0010000000000000, UECodeGen_Private::EPropertyGenFlags::Struct, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(FMMDPhysicsJointData, Position), Z_Construct_UScriptStruct_FVector, METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_Position_MetaData), NewProp_Position_MetaData) };
const UECodeGen_Private::FStructPropertyParams Z_Construct_UScriptStruct_FMMDPhysicsJointData_Statics::NewProp_Rotation = { "Rotation", nullptr, (EPropertyFlags)0x0010000000000000, UECodeGen_Private::EPropertyGenFlags::Struct, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(FMMDPhysicsJointData, Rotation), Z_Construct_UScriptStruct_FVector, METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_Rotation_MetaData), NewProp_Rotation_MetaData) };
const UECodeGen_Private::FStructPropertyParams Z_Construct_UScriptStruct_FMMDPhysicsJointData_Statics::NewProp_LimitPositionMin = { "LimitPositionMin", nullptr, (EPropertyFlags)0x0010000000000000, UECodeGen_Private::EPropertyGenFlags::Struct, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(FMMDPhysicsJointData, LimitPositionMin), Z_Construct_UScriptStruct_FVector, METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_LimitPositionMin_MetaData), NewProp_LimitPositionMin_MetaData) };
const UECodeGen_Private::FStructPropertyParams Z_Construct_UScriptStruct_FMMDPhysicsJointData_Statics::NewProp_LimitPositionMax = { "LimitPositionMax", nullptr, (EPropertyFlags)0x0010000000000000, UECodeGen_Private::EPropertyGenFlags::Struct, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(FMMDPhysicsJointData, LimitPositionMax), Z_Construct_UScriptStruct_FVector, METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_LimitPositionMax_MetaData), NewProp_LimitPositionMax_MetaData) };
const UECodeGen_Private::FStructPropertyParams Z_Construct_UScriptStruct_FMMDPhysicsJointData_Statics::NewProp_LimitRotationMin = { "LimitRotationMin", nullptr, (EPropertyFlags)0x0010000000000000, UECodeGen_Private::EPropertyGenFlags::Struct, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(FMMDPhysicsJointData, LimitRotationMin), Z_Construct_UScriptStruct_FVector, METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_LimitRotationMin_MetaData), NewProp_LimitRotationMin_MetaData) };
const UECodeGen_Private::FStructPropertyParams Z_Construct_UScriptStruct_FMMDPhysicsJointData_Statics::NewProp_LimitRotationMax = { "LimitRotationMax", nullptr, (EPropertyFlags)0x0010000000000000, UECodeGen_Private::EPropertyGenFlags::Struct, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(FMMDPhysicsJointData, LimitRotationMax), Z_Construct_UScriptStruct_FVector, METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_LimitRotationMax_MetaData), NewProp_LimitRotationMax_MetaData) };
const UECodeGen_Private::FStructPropertyParams Z_Construct_UScriptStruct_FMMDPhysicsJointData_Statics::NewProp_SpringPosition = { "SpringPosition", nullptr, (EPropertyFlags)0x0010000000000000, UECodeGen_Private::EPropertyGenFlags::Struct, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(FMMDPhysicsJointData, SpringPosition), Z_Construct_UScriptStruct_FVector, METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_SpringPosition_MetaData), NewProp_SpringPosition_MetaData) };
const UECodeGen_Private::FStructPropertyParams Z_Construct_UScriptStruct_FMMDPhysicsJointData_Statics::NewProp_SpringRotation = { "SpringRotation", nullptr, (EPropertyFlags)0x0010000000000000, UECodeGen_Private::EPropertyGenFlags::Struct, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(FMMDPhysicsJointData, SpringRotation), Z_Construct_UScriptStruct_FVector, METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_SpringRotation_MetaData), NewProp_SpringRotation_MetaData) };
const UECodeGen_Private::FPropertyParamsBase* const Z_Construct_UScriptStruct_FMMDPhysicsJointData_Statics::PropPointers[] = {
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UScriptStruct_FMMDPhysicsJointData_Statics::NewProp_Name,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UScriptStruct_FMMDPhysicsJointData_Statics::NewProp_NameEN,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UScriptStruct_FMMDPhysicsJointData_Statics::NewProp_JointType,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UScriptStruct_FMMDPhysicsJointData_Statics::NewProp_RigidBodyIndexA,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UScriptStruct_FMMDPhysicsJointData_Statics::NewProp_RigidBodyIndexB,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UScriptStruct_FMMDPhysicsJointData_Statics::NewProp_Position,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UScriptStruct_FMMDPhysicsJointData_Statics::NewProp_Rotation,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UScriptStruct_FMMDPhysicsJointData_Statics::NewProp_LimitPositionMin,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UScriptStruct_FMMDPhysicsJointData_Statics::NewProp_LimitPositionMax,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UScriptStruct_FMMDPhysicsJointData_Statics::NewProp_LimitRotationMin,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UScriptStruct_FMMDPhysicsJointData_Statics::NewProp_LimitRotationMax,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UScriptStruct_FMMDPhysicsJointData_Statics::NewProp_SpringPosition,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UScriptStruct_FMMDPhysicsJointData_Statics::NewProp_SpringRotation,
};
static_assert(UE_ARRAY_COUNT(Z_Construct_UScriptStruct_FMMDPhysicsJointData_Statics::PropPointers) < 2048);
const UECodeGen_Private::FStructParams Z_Construct_UScriptStruct_FMMDPhysicsJointData_Statics::StructParams = {
	(UObject* (*)())Z_Construct_UPackage__Script_Ue5MMDTools,
	nullptr,
	&NewStructOps,
	"MMDPhysicsJointData",
	Z_Construct_UScriptStruct_FMMDPhysicsJointData_Statics::PropPointers,
	UE_ARRAY_COUNT(Z_Construct_UScriptStruct_FMMDPhysicsJointData_Statics::PropPointers),
	sizeof(FMMDPhysicsJointData),
	alignof(FMMDPhysicsJointData),
	RF_Public|RF_Transient|RF_MarkAsNative,
	EStructFlags(0x00000201),
	METADATA_PARAMS(UE_ARRAY_COUNT(Z_Construct_UScriptStruct_FMMDPhysicsJointData_Statics::Struct_MetaDataParams), Z_Construct_UScriptStruct_FMMDPhysicsJointData_Statics::Struct_MetaDataParams)
};
UScriptStruct* Z_Construct_UScriptStruct_FMMDPhysicsJointData()
{
	if (!Z_Registration_Info_UScriptStruct_MMDPhysicsJointData.InnerSingleton)
	{
		UECodeGen_Private::ConstructUScriptStruct(Z_Registration_Info_UScriptStruct_MMDPhysicsJointData.InnerSingleton, Z_Construct_UScriptStruct_FMMDPhysicsJointData_Statics::StructParams);
	}
	return Z_Registration_Info_UScriptStruct_MMDPhysicsJointData.InnerSingleton;
}
// End ScriptStruct FMMDPhysicsJointData

// Begin ScriptStruct FAGN_MMDSkeletalControl
static_assert(std::is_polymorphic<FAGN_MMDSkeletalControl>() == std::is_polymorphic<FAnimNode_SkeletalControlBase>(), "USTRUCT FAGN_MMDSkeletalControl cannot be polymorphic unless super FAnimNode_SkeletalControlBase is polymorphic");
static FStructRegistrationInfo Z_Registration_Info_UScriptStruct_AGN_MMDSkeletalControl;
class UScriptStruct* FAGN_MMDSkeletalControl::StaticStruct()
{
	if (!Z_Registration_Info_UScriptStruct_AGN_MMDSkeletalControl.OuterSingleton)
	{
		Z_Registration_Info_UScriptStruct_AGN_MMDSkeletalControl.OuterSingleton = GetStaticStruct(Z_Construct_UScriptStruct_FAGN_MMDSkeletalControl, (UObject*)Z_Construct_UPackage__Script_Ue5MMDTools(), TEXT("AGN_MMDSkeletalControl"));
	}
	return Z_Registration_Info_UScriptStruct_AGN_MMDSkeletalControl.OuterSingleton;
}
template<> UE5MMDTOOLS_API UScriptStruct* StaticStruct<FAGN_MMDSkeletalControl>()
{
	return FAGN_MMDSkeletalControl::StaticStruct();
}
struct Z_Construct_UScriptStruct_FAGN_MMDSkeletalControl_Statics
{
#if WITH_METADATA
	static constexpr UECodeGen_Private::FMetaDataPairParam Struct_MetaDataParams[] = {
		{ "BlueprintInternalUseOnly", "true" },
		{ "BlueprintType", "true" },
		{ "ModuleRelativePath", "Public/AGN_MMDSkeletalControl.h" },
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_bEnablePhysics_MetaData[] = {
		{ "Category", "Settings" },
		{ "ModuleRelativePath", "Public/AGN_MMDSkeletalControl.h" },
		{ "PinShownByDefault", "" },
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_bDrawDebug_MetaData[] = {
		{ "Category", "Debug" },
#if !UE_BUILD_SHIPPING
		{ "Comment", "// \xe5\x9c\xa8\xe8\x93\x9d\xe5\x9b\xbe\xe9\x87\x8c\xe5\xbc\x80\xe5\x85\xb3\xe8\xb0\x83\xe8\xaf\x95\xe7\xbb\x98\xe5\x88\xb6\n" },
#endif
		{ "ModuleRelativePath", "Public/AGN_MMDSkeletalControl.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "\xe5\x9c\xa8\xe8\x93\x9d\xe5\x9b\xbe\xe9\x87\x8c\xe5\xbc\x80\xe5\x85\xb3\xe8\xb0\x83\xe8\xaf\x95\xe7\xbb\x98\xe5\x88\xb6" },
#endif
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_UnitScale_MetaData[] = {
		{ "Category", "Settings" },
#if !UE_BUILD_SHIPPING
		{ "Comment", "// \xe5\x9f\xba\xe6\x9c\xac\xe7\x89\xa9\xe7\x90\x86\xe5\x8f\x82\xe6\x95\xb0\xef\xbc\x88\xe4\xb8\x8e\xe6\xa8\xa1\xe6\x8b\x9f\xe5\x99\xa8\xe9\xbb\x98\xe8\xae\xa4\xe4\xb8\x80\xe8\x87\xb4\xef\xbc\x8c\xe5\x8f\xaf\xe5\x9c\xa8\xe8\x93\x9d\xe5\x9b\xbe\xe8\xb0\x83\xef\xbc\x89\n" },
#endif
		{ "ModuleRelativePath", "Public/AGN_MMDSkeletalControl.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "\xe5\x9f\xba\xe6\x9c\xac\xe7\x89\xa9\xe7\x90\x86\xe5\x8f\x82\xe6\x95\xb0\xef\xbc\x88\xe4\xb8\x8e\xe6\xa8\xa1\xe6\x8b\x9f\xe5\x99\xa8\xe9\xbb\x98\xe8\xae\xa4\xe4\xb8\x80\xe8\x87\xb4\xef\xbc\x8c\xe5\x8f\xaf\xe5\x9c\xa8\xe8\x93\x9d\xe5\x9b\xbe\xe8\xb0\x83\xef\xbc\x89" },
#endif
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_MaxSubSteps_MetaData[] = {
		{ "Category", "Settings" },
		{ "ModuleRelativePath", "Public/AGN_MMDSkeletalControl.h" },
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_FixedTimeStep_MetaData[] = {
		{ "Category", "Settings" },
		{ "ModuleRelativePath", "Public/AGN_MMDSkeletalControl.h" },
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_RigidBodySaveDataArray_MetaData[] = {
		{ "Category", "Settings" },
		{ "ModuleRelativePath", "Public/AGN_MMDSkeletalControl.h" },
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_JointSaveDataArray_MetaData[] = {
		{ "Category", "Settings" },
		{ "ModuleRelativePath", "Public/AGN_MMDSkeletalControl.h" },
	};
#endif // WITH_METADATA
	static void NewProp_bEnablePhysics_SetBit(void* Obj);
	static const UECodeGen_Private::FBoolPropertyParams NewProp_bEnablePhysics;
	static void NewProp_bDrawDebug_SetBit(void* Obj);
	static const UECodeGen_Private::FBoolPropertyParams NewProp_bDrawDebug;
	static const UECodeGen_Private::FFloatPropertyParams NewProp_UnitScale;
	static const UECodeGen_Private::FIntPropertyParams NewProp_MaxSubSteps;
	static const UECodeGen_Private::FFloatPropertyParams NewProp_FixedTimeStep;
	static const UECodeGen_Private::FStructPropertyParams NewProp_RigidBodySaveDataArray_Inner;
	static const UECodeGen_Private::FArrayPropertyParams NewProp_RigidBodySaveDataArray;
	static const UECodeGen_Private::FStructPropertyParams NewProp_JointSaveDataArray_Inner;
	static const UECodeGen_Private::FArrayPropertyParams NewProp_JointSaveDataArray;
	static const UECodeGen_Private::FPropertyParamsBase* const PropPointers[];
	static void* NewStructOps()
	{
		return (UScriptStruct::ICppStructOps*)new UScriptStruct::TCppStructOps<FAGN_MMDSkeletalControl>();
	}
	static const UECodeGen_Private::FStructParams StructParams;
};
void Z_Construct_UScriptStruct_FAGN_MMDSkeletalControl_Statics::NewProp_bEnablePhysics_SetBit(void* Obj)
{
	((FAGN_MMDSkeletalControl*)Obj)->bEnablePhysics = 1;
}
const UECodeGen_Private::FBoolPropertyParams Z_Construct_UScriptStruct_FAGN_MMDSkeletalControl_Statics::NewProp_bEnablePhysics = { "bEnablePhysics", nullptr, (EPropertyFlags)0x0010000000000005, UECodeGen_Private::EPropertyGenFlags::Bool | UECodeGen_Private::EPropertyGenFlags::NativeBool, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, sizeof(bool), sizeof(FAGN_MMDSkeletalControl), &Z_Construct_UScriptStruct_FAGN_MMDSkeletalControl_Statics::NewProp_bEnablePhysics_SetBit, METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_bEnablePhysics_MetaData), NewProp_bEnablePhysics_MetaData) };
void Z_Construct_UScriptStruct_FAGN_MMDSkeletalControl_Statics::NewProp_bDrawDebug_SetBit(void* Obj)
{
	((FAGN_MMDSkeletalControl*)Obj)->bDrawDebug = 1;
}
const UECodeGen_Private::FBoolPropertyParams Z_Construct_UScriptStruct_FAGN_MMDSkeletalControl_Statics::NewProp_bDrawDebug = { "bDrawDebug", nullptr, (EPropertyFlags)0x0010000000000005, UECodeGen_Private::EPropertyGenFlags::Bool | UECodeGen_Private::EPropertyGenFlags::NativeBool, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, sizeof(bool), sizeof(FAGN_MMDSkeletalControl), &Z_Construct_UScriptStruct_FAGN_MMDSkeletalControl_Statics::NewProp_bDrawDebug_SetBit, METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_bDrawDebug_MetaData), NewProp_bDrawDebug_MetaData) };
const UECodeGen_Private::FFloatPropertyParams Z_Construct_UScriptStruct_FAGN_MMDSkeletalControl_Statics::NewProp_UnitScale = { "UnitScale", nullptr, (EPropertyFlags)0x0010000000000005, UECodeGen_Private::EPropertyGenFlags::Float, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(FAGN_MMDSkeletalControl, UnitScale), METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_UnitScale_MetaData), NewProp_UnitScale_MetaData) };
const UECodeGen_Private::FIntPropertyParams Z_Construct_UScriptStruct_FAGN_MMDSkeletalControl_Statics::NewProp_MaxSubSteps = { "MaxSubSteps", nullptr, (EPropertyFlags)0x0010000000000005, UECodeGen_Private::EPropertyGenFlags::Int, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(FAGN_MMDSkeletalControl, MaxSubSteps), METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_MaxSubSteps_MetaData), NewProp_MaxSubSteps_MetaData) };
const UECodeGen_Private::FFloatPropertyParams Z_Construct_UScriptStruct_FAGN_MMDSkeletalControl_Statics::NewProp_FixedTimeStep = { "FixedTimeStep", nullptr, (EPropertyFlags)0x0010000000000005, UECodeGen_Private::EPropertyGenFlags::Float, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(FAGN_MMDSkeletalControl, FixedTimeStep), METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_FixedTimeStep_MetaData), NewProp_FixedTimeStep_MetaData) };
const UECodeGen_Private::FStructPropertyParams Z_Construct_UScriptStruct_FAGN_MMDSkeletalControl_Statics::NewProp_RigidBodySaveDataArray_Inner = { "RigidBodySaveDataArray", nullptr, (EPropertyFlags)0x0000000000000000, UECodeGen_Private::EPropertyGenFlags::Struct, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, 0, Z_Construct_UScriptStruct_FMMDPhysicsRigidBodyData, METADATA_PARAMS(0, nullptr) }; // 222573323
const UECodeGen_Private::FArrayPropertyParams Z_Construct_UScriptStruct_FAGN_MMDSkeletalControl_Statics::NewProp_RigidBodySaveDataArray = { "RigidBodySaveDataArray", nullptr, (EPropertyFlags)0x0010000000000001, UECodeGen_Private::EPropertyGenFlags::Array, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(FAGN_MMDSkeletalControl, RigidBodySaveDataArray), EArrayPropertyFlags::None, METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_RigidBodySaveDataArray_MetaData), NewProp_RigidBodySaveDataArray_MetaData) }; // 222573323
const UECodeGen_Private::FStructPropertyParams Z_Construct_UScriptStruct_FAGN_MMDSkeletalControl_Statics::NewProp_JointSaveDataArray_Inner = { "JointSaveDataArray", nullptr, (EPropertyFlags)0x0000000000000000, UECodeGen_Private::EPropertyGenFlags::Struct, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, 0, Z_Construct_UScriptStruct_FMMDPhysicsJointData, METADATA_PARAMS(0, nullptr) }; // 41197581
const UECodeGen_Private::FArrayPropertyParams Z_Construct_UScriptStruct_FAGN_MMDSkeletalControl_Statics::NewProp_JointSaveDataArray = { "JointSaveDataArray", nullptr, (EPropertyFlags)0x0010000000000001, UECodeGen_Private::EPropertyGenFlags::Array, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(FAGN_MMDSkeletalControl, JointSaveDataArray), EArrayPropertyFlags::None, METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_JointSaveDataArray_MetaData), NewProp_JointSaveDataArray_MetaData) }; // 41197581
const UECodeGen_Private::FPropertyParamsBase* const Z_Construct_UScriptStruct_FAGN_MMDSkeletalControl_Statics::PropPointers[] = {
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UScriptStruct_FAGN_MMDSkeletalControl_Statics::NewProp_bEnablePhysics,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UScriptStruct_FAGN_MMDSkeletalControl_Statics::NewProp_bDrawDebug,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UScriptStruct_FAGN_MMDSkeletalControl_Statics::NewProp_UnitScale,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UScriptStruct_FAGN_MMDSkeletalControl_Statics::NewProp_MaxSubSteps,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UScriptStruct_FAGN_MMDSkeletalControl_Statics::NewProp_FixedTimeStep,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UScriptStruct_FAGN_MMDSkeletalControl_Statics::NewProp_RigidBodySaveDataArray_Inner,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UScriptStruct_FAGN_MMDSkeletalControl_Statics::NewProp_RigidBodySaveDataArray,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UScriptStruct_FAGN_MMDSkeletalControl_Statics::NewProp_JointSaveDataArray_Inner,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UScriptStruct_FAGN_MMDSkeletalControl_Statics::NewProp_JointSaveDataArray,
};
static_assert(UE_ARRAY_COUNT(Z_Construct_UScriptStruct_FAGN_MMDSkeletalControl_Statics::PropPointers) < 2048);
const UECodeGen_Private::FStructParams Z_Construct_UScriptStruct_FAGN_MMDSkeletalControl_Statics::StructParams = {
	(UObject* (*)())Z_Construct_UPackage__Script_Ue5MMDTools,
	Z_Construct_UScriptStruct_FAnimNode_SkeletalControlBase,
	&NewStructOps,
	"AGN_MMDSkeletalControl",
	Z_Construct_UScriptStruct_FAGN_MMDSkeletalControl_Statics::PropPointers,
	UE_ARRAY_COUNT(Z_Construct_UScriptStruct_FAGN_MMDSkeletalControl_Statics::PropPointers),
	sizeof(FAGN_MMDSkeletalControl),
	alignof(FAGN_MMDSkeletalControl),
	RF_Public|RF_Transient|RF_MarkAsNative,
	EStructFlags(0x00000201),
	METADATA_PARAMS(UE_ARRAY_COUNT(Z_Construct_UScriptStruct_FAGN_MMDSkeletalControl_Statics::Struct_MetaDataParams), Z_Construct_UScriptStruct_FAGN_MMDSkeletalControl_Statics::Struct_MetaDataParams)
};
UScriptStruct* Z_Construct_UScriptStruct_FAGN_MMDSkeletalControl()
{
	if (!Z_Registration_Info_UScriptStruct_AGN_MMDSkeletalControl.InnerSingleton)
	{
		UECodeGen_Private::ConstructUScriptStruct(Z_Registration_Info_UScriptStruct_AGN_MMDSkeletalControl.InnerSingleton, Z_Construct_UScriptStruct_FAGN_MMDSkeletalControl_Statics::StructParams);
	}
	return Z_Registration_Info_UScriptStruct_AGN_MMDSkeletalControl.InnerSingleton;
}
// End ScriptStruct FAGN_MMDSkeletalControl

// Begin Class UAnimGraphNode_MMDSkeletalControl
void UAnimGraphNode_MMDSkeletalControl::StaticRegisterNativesUAnimGraphNode_MMDSkeletalControl()
{
}
IMPLEMENT_CLASS_NO_AUTO_REGISTRATION(UAnimGraphNode_MMDSkeletalControl);
UClass* Z_Construct_UClass_UAnimGraphNode_MMDSkeletalControl_NoRegister()
{
	return UAnimGraphNode_MMDSkeletalControl::StaticClass();
}
struct Z_Construct_UClass_UAnimGraphNode_MMDSkeletalControl_Statics
{
#if WITH_METADATA
	static constexpr UECodeGen_Private::FMetaDataPairParam Class_MetaDataParams[] = {
		{ "IncludePath", "AGN_MMDSkeletalControl.h" },
		{ "ModuleRelativePath", "Public/AGN_MMDSkeletalControl.h" },
	};
#if WITH_EDITORONLY_DATA
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_Node_MetaData[] = {
		{ "Category", "Settings" },
		{ "ModuleRelativePath", "Public/AGN_MMDSkeletalControl.h" },
	};
#endif // WITH_EDITORONLY_DATA
#endif // WITH_METADATA
#if WITH_EDITORONLY_DATA
	static const UECodeGen_Private::FStructPropertyParams NewProp_Node;
	static const UECodeGen_Private::FPropertyParamsBase* const PropPointers[];
#endif // WITH_EDITORONLY_DATA
	static UObject* (*const DependentSingletons[])();
	static constexpr FCppClassTypeInfoStatic StaticCppClassTypeInfo = {
		TCppClassTypeTraits<UAnimGraphNode_MMDSkeletalControl>::IsAbstract,
	};
	static const UECodeGen_Private::FClassParams ClassParams;
};
#if WITH_EDITORONLY_DATA
const UECodeGen_Private::FStructPropertyParams Z_Construct_UClass_UAnimGraphNode_MMDSkeletalControl_Statics::NewProp_Node = { "Node", nullptr, (EPropertyFlags)0x0010000800000001, UECodeGen_Private::EPropertyGenFlags::Struct, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(UAnimGraphNode_MMDSkeletalControl, Node), Z_Construct_UScriptStruct_FAGN_MMDSkeletalControl, METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_Node_MetaData), NewProp_Node_MetaData) }; // 3920726924
#endif // WITH_EDITORONLY_DATA
#if WITH_EDITORONLY_DATA
const UECodeGen_Private::FPropertyParamsBase* const Z_Construct_UClass_UAnimGraphNode_MMDSkeletalControl_Statics::PropPointers[] = {
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UClass_UAnimGraphNode_MMDSkeletalControl_Statics::NewProp_Node,
};
static_assert(UE_ARRAY_COUNT(Z_Construct_UClass_UAnimGraphNode_MMDSkeletalControl_Statics::PropPointers) < 2048);
#endif // WITH_EDITORONLY_DATA
UObject* (*const Z_Construct_UClass_UAnimGraphNode_MMDSkeletalControl_Statics::DependentSingletons[])() = {
	(UObject* (*)())Z_Construct_UClass_UAnimGraphNode_SkeletalControlBase,
	(UObject* (*)())Z_Construct_UPackage__Script_Ue5MMDTools,
};
static_assert(UE_ARRAY_COUNT(Z_Construct_UClass_UAnimGraphNode_MMDSkeletalControl_Statics::DependentSingletons) < 16);
const UECodeGen_Private::FClassParams Z_Construct_UClass_UAnimGraphNode_MMDSkeletalControl_Statics::ClassParams = {
	&UAnimGraphNode_MMDSkeletalControl::StaticClass,
	nullptr,
	&StaticCppClassTypeInfo,
	DependentSingletons,
	nullptr,
	IF_WITH_EDITORONLY_DATA(Z_Construct_UClass_UAnimGraphNode_MMDSkeletalControl_Statics::PropPointers, nullptr),
	nullptr,
	UE_ARRAY_COUNT(DependentSingletons),
	0,
	IF_WITH_EDITORONLY_DATA(UE_ARRAY_COUNT(Z_Construct_UClass_UAnimGraphNode_MMDSkeletalControl_Statics::PropPointers), 0),
	0,
	0x008800A0u,
	METADATA_PARAMS(UE_ARRAY_COUNT(Z_Construct_UClass_UAnimGraphNode_MMDSkeletalControl_Statics::Class_MetaDataParams), Z_Construct_UClass_UAnimGraphNode_MMDSkeletalControl_Statics::Class_MetaDataParams)
};
UClass* Z_Construct_UClass_UAnimGraphNode_MMDSkeletalControl()
{
	if (!Z_Registration_Info_UClass_UAnimGraphNode_MMDSkeletalControl.OuterSingleton)
	{
		UECodeGen_Private::ConstructUClass(Z_Registration_Info_UClass_UAnimGraphNode_MMDSkeletalControl.OuterSingleton, Z_Construct_UClass_UAnimGraphNode_MMDSkeletalControl_Statics::ClassParams);
	}
	return Z_Registration_Info_UClass_UAnimGraphNode_MMDSkeletalControl.OuterSingleton;
}
template<> UE5MMDTOOLS_API UClass* StaticClass<UAnimGraphNode_MMDSkeletalControl>()
{
	return UAnimGraphNode_MMDSkeletalControl::StaticClass();
}
UAnimGraphNode_MMDSkeletalControl::UAnimGraphNode_MMDSkeletalControl(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {}
DEFINE_VTABLE_PTR_HELPER_CTOR(UAnimGraphNode_MMDSkeletalControl);
UAnimGraphNode_MMDSkeletalControl::~UAnimGraphNode_MMDSkeletalControl() {}
// End Class UAnimGraphNode_MMDSkeletalControl

// Begin Registration
struct Z_CompiledInDeferFile_FID_MMD_Plugins_Ue5MMDTools_Source_Ue5MMDTools_Public_AGN_MMDSkeletalControl_h_Statics
{
	static constexpr FStructRegisterCompiledInInfo ScriptStructInfo[] = {
		{ FMMDPhysicsRigidBodyData::StaticStruct, Z_Construct_UScriptStruct_FMMDPhysicsRigidBodyData_Statics::NewStructOps, TEXT("MMDPhysicsRigidBodyData"), &Z_Registration_Info_UScriptStruct_MMDPhysicsRigidBodyData, CONSTRUCT_RELOAD_VERSION_INFO(FStructReloadVersionInfo, sizeof(FMMDPhysicsRigidBodyData), 222573323U) },
		{ FMMDPhysicsJointData::StaticStruct, Z_Construct_UScriptStruct_FMMDPhysicsJointData_Statics::NewStructOps, TEXT("MMDPhysicsJointData"), &Z_Registration_Info_UScriptStruct_MMDPhysicsJointData, CONSTRUCT_RELOAD_VERSION_INFO(FStructReloadVersionInfo, sizeof(FMMDPhysicsJointData), 41197581U) },
		{ FAGN_MMDSkeletalControl::StaticStruct, Z_Construct_UScriptStruct_FAGN_MMDSkeletalControl_Statics::NewStructOps, TEXT("AGN_MMDSkeletalControl"), &Z_Registration_Info_UScriptStruct_AGN_MMDSkeletalControl, CONSTRUCT_RELOAD_VERSION_INFO(FStructReloadVersionInfo, sizeof(FAGN_MMDSkeletalControl), 3920726924U) },
	};
	static constexpr FClassRegisterCompiledInInfo ClassInfo[] = {
		{ Z_Construct_UClass_UAnimGraphNode_MMDSkeletalControl, UAnimGraphNode_MMDSkeletalControl::StaticClass, TEXT("UAnimGraphNode_MMDSkeletalControl"), &Z_Registration_Info_UClass_UAnimGraphNode_MMDSkeletalControl, CONSTRUCT_RELOAD_VERSION_INFO(FClassReloadVersionInfo, sizeof(UAnimGraphNode_MMDSkeletalControl), 2681090393U) },
	};
};
static FRegisterCompiledInInfo Z_CompiledInDeferFile_FID_MMD_Plugins_Ue5MMDTools_Source_Ue5MMDTools_Public_AGN_MMDSkeletalControl_h_3993018881(TEXT("/Script/Ue5MMDTools"),
	Z_CompiledInDeferFile_FID_MMD_Plugins_Ue5MMDTools_Source_Ue5MMDTools_Public_AGN_MMDSkeletalControl_h_Statics::ClassInfo, UE_ARRAY_COUNT(Z_CompiledInDeferFile_FID_MMD_Plugins_Ue5MMDTools_Source_Ue5MMDTools_Public_AGN_MMDSkeletalControl_h_Statics::ClassInfo),
	Z_CompiledInDeferFile_FID_MMD_Plugins_Ue5MMDTools_Source_Ue5MMDTools_Public_AGN_MMDSkeletalControl_h_Statics::ScriptStructInfo, UE_ARRAY_COUNT(Z_CompiledInDeferFile_FID_MMD_Plugins_Ue5MMDTools_Source_Ue5MMDTools_Public_AGN_MMDSkeletalControl_h_Statics::ScriptStructInfo),
	nullptr, 0);
// End Registration
PRAGMA_ENABLE_DEPRECATION_WARNINGS

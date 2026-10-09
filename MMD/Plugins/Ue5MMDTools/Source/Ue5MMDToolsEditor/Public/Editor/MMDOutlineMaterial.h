#pragma once

#include "CoreMinimal.h"

class UMaterial;
class UMaterialInterface;
class UMaterialInstanceConstant;
class UPackage;
class FJsonObject;
class USkeletalMesh;

// 创建图和参数，不保存资产；导入器负责沿用当前导入沙盒的保存流程。
UE5MMDTOOLSEDITOR_API UMaterial* CreateMMDOutlineMaterial(
	UObject* Outer, FName Name = NAME_None, EObjectFlags Flags = RF_Transient);

// 导入时创建唯一父材质；旧五输入图追加迁移，源码签名变化时重新保存。
UE5MMDTOOLSEDITOR_API UMaterial* GetOrCreateMMDOutlineMaterial();

UE5MMDTOOLSEDITOR_API UMaterialInstanceConstant* CreateMMDOutlineMaterialInstance(
	UPackage* Package, FName Name, UMaterialInterface* Parent, const FLinearColor& EdgeColor,
	float EdgeSize, EObjectFlags Flags = RF_Public | RF_Standalone);

UE5MMDTOOLSEDITOR_API bool IsMMDOutlineMaterialGraphValid(const UMaterial* Material);
// 必须传入本次从顶点计算的原始边界；纯函数避免重复导入不断叠加 padding。
UE5MMDTOOLSEDITOR_API FBoxSphereBounds MakeMMDOutlineImportBounds(
	const USkeletalMesh* Mesh, const FBoxSphereBounds& GeometryBounds);
UE5MMDTOOLSEDITOR_API TSharedRef<FJsonObject> ValidateMMDOutlineSlots();

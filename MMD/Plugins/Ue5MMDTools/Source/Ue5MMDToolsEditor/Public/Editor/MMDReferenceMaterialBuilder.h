#pragma once

#include "CoreMinimal.h"

class UMaterialInterface;
struct FMMDStageMaterialSet;

struct FMMDReferenceMaterialFamily
{
	UMaterialInterface* Opaque = nullptr;
	UMaterialInterface* Masked = nullptr;
	UMaterialInterface* Translucent = nullptr;
};

struct FMMDReferenceMaterialSet
{
	// 固定顺序：原神参考、鸣潮参考、终末地参考、白银之城参考。
	FMMDReferenceMaterialFamily Families[4];
};

// 生成独立的 Stages 角色父材质，并保留已有参数进行幂等输入迁移。
UE5MMDTOOLSEDITOR_API bool EnsureMMDReferenceCharacterMaterials(const FMMDStageMaterialSet& BaseMaterials,
	FMMDReferenceMaterialSet& OutMaterials, FString& OutError);

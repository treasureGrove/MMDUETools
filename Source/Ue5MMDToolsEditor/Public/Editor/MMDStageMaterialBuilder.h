#pragma once

#include "CoreMinimal.h"

class UMaterialInterface;

struct FMMDStageMaterialSet
{
	UMaterialInterface* CharacterOpaque = nullptr;
	UMaterialInterface* CharacterMasked = nullptr;
	UMaterialInterface* CharacterTranslucent = nullptr;
	UMaterialInterface* Floor = nullptr;
	UMaterialInterface* Panel = nullptr;
	UMaterialInterface* Accent = nullptr;
	UMaterialInterface* Beam = nullptr;
	UMaterialInterface* Backdrop = nullptr;
};

// 仅处理 Stages 父材质；已有图保留，补充新增输入与必要用法。
UE5MMDTOOLSEDITOR_API bool EnsureMMDStageMaterials(FMMDStageMaterialSet& OutMaterials, FString& OutError);

#pragma once

#include "CoreMinimal.h"

class UMaterialInterface;

struct FMMDGardenMaterialSet
{
	UMaterialInterface* Surface = nullptr;
	UMaterialInterface* Water = nullptr;
};

// 创建或升级庭园父材质；可选纹理缺失时仍有完整的几何着色。
UE5MMDTOOLSEDITOR_API bool EnsureMMDGardenMaterials(FMMDGardenMaterialSet& Out, FString& Error);

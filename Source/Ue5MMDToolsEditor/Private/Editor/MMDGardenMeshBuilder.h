#pragma once

#include "CoreMinimal.h"

// 原创庭院网格，V2花簇含手工三级LOD；已有资源只校验，不覆盖。
struct FMMDGardenMeshBuilder
{
	static bool BuildAssets(bool bSave, FString& Error);
};

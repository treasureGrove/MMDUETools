#pragma once

#include "CoreMinimal.h"

class UTexture2D;

// 返回独立 V2 通用细节资产，不覆盖旧图，不写入模型 UV、部位遮罩或专属贴图信息。
bool EnsureMMDReferenceDetailTextures(UTexture2D*& OutHairNormal, UTexture2D*& OutClothNormal,
	UTexture2D*& OutClothData, FString& OutError);

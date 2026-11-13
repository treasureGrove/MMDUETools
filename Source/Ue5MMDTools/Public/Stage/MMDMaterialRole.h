#pragma once

#include "CoreMinimal.h"
#include "MMDMaterialRole.generated.h"

UENUM(BlueprintType)
enum class EMMDMaterialRole : uint8
{
	Generic = 0 UMETA(DisplayName = "通用"),
	Skin = 1 UMETA(DisplayName = "皮肤与脸部"),
	Hair = 2 UMETA(DisplayName = "头发"),
	Cloth = 3 UMETA(DisplayName = "布料"),
	Metal = 4 UMETA(DisplayName = "金属"),
	Eye = 5 UMETA(DisplayName = "眼球")
};

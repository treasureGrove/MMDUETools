#pragma once

#include "Runtime/Launch/Resources/Version.h"

// 统一跨 UE 5.5 ~ 5.8 C++ API 差异判定的版本宏
#define MMD_UE_VERSION_AT_LEAST(Major, Minor) \
	((ENGINE_MAJOR_VERSION > (Major)) || (ENGINE_MAJOR_VERSION == (Major) && ENGINE_MINOR_VERSION >= (Minor)))

#define MMD_UE_VERSION_BEFORE(Major, Minor) \
	(!MMD_UE_VERSION_AT_LEAST(Major, Minor))

#pragma once

#include "Templates/SharedPointer.h"

class FJsonObject;

/** 用 transient 副本验证真实子系统的 RT 初始化，不保存或修改源资产。 */
TSharedRef<FJsonObject> ValidateMMDRenderTargets();

/** 从后台导入沙箱加载真实模型，离屏输出描边开关对比图。 */
TSharedRef<FJsonObject> CaptureMMDPreviewScreenshots();

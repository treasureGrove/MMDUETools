#pragma once

#include "CoreMinimal.h"
#include "Animation/AGN_MMDSkeletalControl.h"

/** 保存 PMX 原始物理单位，与现有 Bullet 节点使用同一转换约定。 */
UE5MMDTOOLS_API void BuildMMDPhysicsData(const PMXDatas& PMXData,
	TArray<FMMDPhysicsRigidBodyData>& Rigids, TArray<FMMDPhysicsJointData>& Joints);

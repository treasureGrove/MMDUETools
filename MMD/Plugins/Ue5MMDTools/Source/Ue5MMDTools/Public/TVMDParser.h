#pragma once

#include "CoreMinimal.h"

// =============================================
// VMD 文件格式（Vocaloid Motion Data）
// =============================================
// 二进制、小端 uint32 / float。字符串固定宽度，不足补 0x00。
// 文本：骨骼名 / Morph 名 / 模型名 等为 Shift_JIS (CP932)。
//
// VMD file layout —— 严格按读取顺序
//
// 1) Header
// - char[30] magic
//     "Vocaloid Motion Data file"  (旧 MMD，约 1.30)
//     "Vocaloid Motion Data 0002"  (多模型版及以后)
//     不足 30 字节用 0x00 填充；判断新旧只看 ASCII 前缀。
// - char[10] or char[20] modelName  // Shift_JIS；旧头后接 10 字节，新头(0002)后接 20 字节
//
// 2) Bone Keyframes
// - uint32 boneKeyframeCount
// - repeat boneKeyframeCount times:  // 每条固定 111 字节
//   - char[15]                // Shift_JIS
//   - uint32 frameNumber
//   - float[3] position               // 相对 bind pose 的平移增量（与 MMD 一致）
//   - float[4] rotation               // 四元数 x,y,z,w
//   - uint8[64] interpolation         // MMD 贝塞尔插值参数（可先整体保存）
//
// 3) Morph Keyframes
// - uint32 morphKeyframeCount
// - repeat morphKeyframeCount times:  // 每条 23 字节
//   - char[15] morphName
//   - uint32 frameNumber
//   - float weight                    // 通常 0.0 ~ 1.0
//
// 4) Camera Keyframes
// - uint32 cameraKeyframeCount
// - repeat cameraKeyframeCount times: // 每条 61 字节
//   - uint32 frameNumber
//   - float distance                  // 相机到目标距离
//   - float[3] interest               // 目标点 XYZ
//   - float[3] rotation               // 欧拉角 XYZ（弧度，顺序同 MMD）
//   - uint8[24] interpolation
//   - float viewAngle                 // 视角 / FOV 相关（度或弧度依 MMD 约定）
//   - uint8 perspective               // 透视开关 0/1
//
// 5) Light Keyframes
// - uint32 lightKeyframeCount
// - repeat lightKeyframeCount times:  // 每条 28 字节
//   - uint32 frameNumber
//   - float[3] color                  // RGB，常 0~1
//   - float[3] position               // 光源位置 XYZ（与 MMD 一致；部分文档称 direction，以你对照工具为准）
//
// 6) Shadow Keyframes
// - uint32 shadowKeyframeCount
// - repeat shadowKeyframeCount times: // 每条 9 字节
//   - uint32 frameNumber
//   - uint8 mode
//   - float distance
//
// 7) IK Keyframes
// - uint32 ikGroupCount
// - repeat ikGroupCount times:
//   - uint32 frameNumber
//   - uint8 display                   // 显示/总开关类
//   - uint32 ikInfoCount
//   - repeat ikInfoCount times:
//     - char[20] ikBoneName           // IK 骨名 20 字节（与骨骼轨道的 15 不同）
//     - uint8 enabled

struct VMDBoneKeyframe{
	FString BoneName;
	uint32 FrameNumber;
	FVector Position;
	FQuat Rotation;
	uint8 Interpolation[64];
};

struct VMDMorphKeyframe{
	FString MorphName;
	uint32 FrameNumber;
	float Weight;
};

struct VMDCameraKeyframe{
	uint32 FrameNumber;
	float Distance;
	FVector Interest;
	FVector Rotation;
	uint8 Interpolation[24];
	float ViewAngle;
	uint8 Perspective;
};

struct VMDLightKeyframe{
	uint32 FrameNumber;
	FVector Color;
	FVector Position;
};

struct VMDShadowKeyframe{
	uint32 FrameNumber;
	uint8 Mode;
	float Distance;
};

struct VMDIKKeyframe{
	uint32 FrameNumber;
	uint8 Display;
	uint32 IKInfoCount;
	TArray<TPair<FString, uint8>> IKInfos;
};

struct VMDData{
	FString Header;
	FString ModelName;

	bool bNewFormatHeader =true;

	TArray<VMDBoneKeyframe> BoneKeyframes;
	TArray<VMDMorphKeyframe> MorphKeyframes;
	TArray<VMDCameraKeyframe> CameraKeyframes;
	TArray<VMDLightKeyframe> LightKeyframes;
	TArray<VMDShadowKeyframe> ShadowKeyframes;
	TArray<VMDIKKeyframe> IKKeyframes;
};

class UE5MMDTOOLS_API TVMDParser{
public:
	bool ParseVMDFile(const FString &FilePath);

	VMDData VMDInfo;
};

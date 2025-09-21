#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"

// =============================================
// VMD文件解析格式
// =============================================
// VMD file layout — read order

// 1) Header
// - char[30] magic // "Vocaloid Motion Data 0002" or "Vocaloid Motion Data file" (older)
// - char[20] modelName // 使用SJIS编码的模型名称

// 2) Bone Keyframes
// - uint32 boneKeyframeCount
// - repeat boneKeyframeCount times:
//   - char[15] boneName // SJIS编码的骨骼名称
//   - uint32 frameNumber // 帧号
//   - float[3] position // XYZ位置偏移
//   - float[4] rotation // XYZW四元数旋转 
//   - uint8[64] interpolation // 贝塞尔曲线插值参数，每个骨骼坐标和旋转各4个参数，共16组，每组4字节
struct VMDBoneKeyframe
{
	FString BoneName;
	uint32 FrameNumber = 0;
	FVector Position = FVector::ZeroVector;
	FQuat Rotation = FQuat::Identity;
	uint8 Interpolation[64] = { 0 };
};

// 3) Morph Keyframes (表情)
// - uint32 morphKeyframeCount
// - repeat morphKeyframeCount times:
//   - char[15] morphName // SJIS编码的表情名称
//   - uint32 frameNumber // 帧号
//   - float weight // 表情权重，通常0.0-1.0
struct VMDMorphKeyframe
{
	FString MorphName;
	uint32 FrameNumber = 0;
	float Weight = 0.0f;
};

// 4) Camera Keyframes (相机)
// - uint32 cameraKeyframeCount
// - repeat cameraKeyframeCount times:
//   - uint32 frameNumber // 帧号
//   - float distance // 相机距离
//   - float[3] position // XYZ位置
//   - float[3] rotation // XYZ旋转(弧度)
//   - uint8[24] interpolation // 相机插值参数
//   - uint32 viewAngle // 视角(FOV)
//   - uint8 perspective // 是否使用透视投影
struct VMDCameraKeyframe
{
	uint32 frameNumber;
	float distance;
	FVector position;
	FVector rotation;
	uint8 interpolation[24];
	uint32 viewAngle;
	uint8 perspective;
};
// 5) Light Keyframes (光照) - 可能不存在于某些VMD文件
// - uint32 lightKeyframeCount
// - repeat lightKeyframeCount times:
//   - uint32 frameNumber // 帧号
//   - float[3] color // RGB颜色 (0.0-1.0)
//   - float[3] direction // 光照方向向量
struct VMDLightKeyframe
{
	uint32 FrameNumber;
	float Distance;
	FVector Position;
	FVector Rotation;
	uint8 Interpolation[24];
	uint32 ViewAngle;
	uint8 Perspective;
};

// 6) Shadow Keyframes (阴影) - 可能不存在于某些VMD文件
// - uint32 shadowKeyframeCount
// - repeat shadowKeyframeCount times:
//   - uint32 frameNumber // 帧号
//   - uint8 mode // 阴影类型
//   - float distance // 阴影距离
struct VMDShadowKeyframe
{
	uint32 FrameNumber;
	uint8 Mode;
	float Distance;
};
// 7) IK Keyframes (IK开关) - 仅存在于较新的VMD文件
// - uint32 ikKeyframeCount
// - repeat ikKeyframeCount times:
//   - uint32 frameNumber // 帧号
//   - uint8 display // 显示/隐藏
//   - uint32 ikCount
//   - repeat ikCount times:
//     - char[20] ikBoneName // IK骨骼名称
//     - uint8 enabled // 是否启用
struct VMDIKKeyframe
{
	uint32 FrameNumber = 0;
	uint8 Display = 0;
	TArray<TPair<FString, bool>> IKInfos;
};
struct VMDData {
	FString ModelName;
	int32 NextByteLength = 0; // 下一个数据块的字节长度
	TArray<VMDBoneKeyframe> BoneFrames; 
	TArray<VMDMorphKeyframe> MorphFrames; 
	TArray<VMDCameraKeyframe> CameraFrames;
	TArray<VMDLightKeyframe> LightFrames;
	TArray<VMDShadowKeyframe> ShadowFrames; 
	TArray<VMDIKKeyframe> IKFrames;
};

class UE5MMDTOOLS_API TVMDParser
{
public:
	bool ParserVMDFile(const FString& FilePath);
};

#include "TVMDParser.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "Serialization/MemoryReader.h"

#include <windows.h>

#pragma region  VMD
FString DecodeSJISToString(const char* SJISStr, int32 Length)
{
	if (Length <= 0 || SJISStr == nullptr)
		return FString();

	int32 WideCharLen = MultiByteToWideChar(932, 0, SJISStr, Length, nullptr, 0);
	if (WideCharLen <= 0)
		return FString();

	TArray<wchar_t> WideChars;
	WideChars.SetNumUninitialized(WideCharLen);
	MultiByteToWideChar(932, 0, SJISStr, Length, WideChars.GetData(), WideCharLen);

	return FString(WideCharLen, WideChars.GetData());
}
bool ReadCharArray(FMemoryReader& Reader, FString& OutString, VMDData& VMDInfo)
{
	OutString.Reset();

	const int32 StringLength = VMDInfo.NextStringLength;
	if (StringLength <= 0)
	{
		UE_LOG(LogTemp, Error, TEXT("ReadCharArray: NextStringByteLen not set or invalid: %d"), StringLength);
		return false;
	}

	const int64 Remaining = Reader.TotalSize() - Reader.Tell();
	if (Remaining < StringLength)
	{
		UE_LOG(LogTemp, Error, TEXT("ReadCharArray: not enough bytes to read string. required=%d, remaining=%lld"), StringLength, Remaining);
		return false;
	}

	TArray<uint8> RawData;
	RawData.SetNumUninitialized(StringLength);
	Reader.Serialize(RawData.GetData(), StringLength);

	int32 ActualLength = 0;
	for (int32 i = 0; i < StringLength; i++)
	{
		if (RawData[i] == 0)
		{
			break;
		}
		ActualLength++;
	}

	if (ActualLength == 0) {
		OutString = TEXT("");
		return true;
	}

	TArray<char> AnsiChars;
	AnsiChars.SetNum(ActualLength + 1);
	for (int32 i = 0; i < ActualLength; i++)
	{
		AnsiChars[i] = static_cast<char>(RawData[i]);
	}
	AnsiChars[ActualLength] = '\0';
	OutString = DecodeSJISToString(AnsiChars.GetData(), ActualLength);
	return true;

}
bool ReadVMDBoneKeyframe(FMemoryReader& Reader, VMDData& VMDInfo)
{
	uint32 BoneKeyFrameCount = 0;
	Reader << BoneKeyFrameCount;
	VMDInfo.BoneFrames.Reserve(BoneKeyFrameCount);
	for (uint32 i = 0; i < BoneKeyFrameCount; ++i)
	{
		VMDBoneKeyframe KeyFrame;

		VMDInfo.NextStringLength = 15;
		if (!ReadCharArray(Reader, KeyFrame.BoneName, VMDInfo))
		{
			UE_LOG(LogTemp, Error, TEXT("Failed to read BoneName for bone keyframe %d"), i);
			return false;
		}
		Reader << KeyFrame.FrameNumber;
		float px, py, pz;
		Reader << px << py << pz;
		KeyFrame.Position = FVector(px, py, pz);
		float qx, qy, qz, qw;
		Reader << qx << qy << qz << qw;
		KeyFrame.Rotation = FQuat(qx, qy, qz, qw);
		Reader.Serialize(KeyFrame.Interpolation, 64);
		VMDInfo.BoneFrames.Add(KeyFrame);
		if (i < 3) // 只打印前3个以避免日志过长
		{
			UE_LOG(LogTemp, Log, TEXT("BoneKeyFrame[%d]: Bone=%s, Frame=%d, Pos=(%f,%f,%f), Rot=(%f,%f,%f,%f)"),
				i, *KeyFrame.BoneName, KeyFrame.FrameNumber,
				KeyFrame.Position.X, KeyFrame.Position.Y, KeyFrame.Position.Z,
				KeyFrame.Rotation.X, KeyFrame.Rotation.Y, KeyFrame.Rotation.Z, KeyFrame.Rotation.W);
		}
	}
	return true;
}
bool ReadVMDMorphKeyframe(FMemoryReader& Reader, VMDData& VMDInfo)
{
	uint32 BoneKeyFrameCount = 0;
	Reader << BoneKeyFrameCount;
	VMDInfo.MorphFrames.Reserve(BoneKeyFrameCount);

	for (uint32 i = 0; i < BoneKeyFrameCount; ++i)
	{
		VMDMorphKeyframe KeyFrame;

		VMDInfo.NextStringLength = 15;
		if (!ReadCharArray(Reader, KeyFrame.MorphName, VMDInfo))
		{
			UE_LOG(LogTemp, Error, TEXT("Failed to read MorphName for morph keyframe %d"), i);
			return false;
		}
		Reader << KeyFrame.FrameNumber;
		Reader << KeyFrame.Weight;
		VMDInfo.MorphFrames.Add(KeyFrame);
		if (i < 3) // 只打印前3个以避免日志过长
		{
			UE_LOG(LogTemp, Log, TEXT("MorphKeyFrame[%d]: Frame=%d, MorphName=%s, Weight=%f"),
				i, KeyFrame.FrameNumber, *KeyFrame.MorphName, KeyFrame.Weight);
		}
	}


	return true;
}
bool ReadVMDCameraKeyframe(FMemoryReader& Reader, VMDData& VMDInfo)
{
	uint32 CameraKeyFrameCount = 0;
	Reader << CameraKeyFrameCount;
	VMDInfo.CameraFrames.Reserve(CameraKeyFrameCount);
	for (uint32 i = 0; i < CameraKeyFrameCount; ++i)
	{
		VMDCameraKeyframe KeyFrame;
		Reader << KeyFrame.frameNumber;
		Reader << KeyFrame.distance;
		float px, py, pz;
		Reader << px << py << pz;
		KeyFrame.position = FVector(px, py, pz);
		float rx, ry, rz;
		Reader << rx << ry << rz;
		KeyFrame.rotation = FVector(rx, ry, rz);
		Reader.Serialize(KeyFrame.interpolation, 24);
		Reader << KeyFrame.viewAngle;
		Reader << KeyFrame.perspective;
		VMDInfo.CameraFrames.Add(KeyFrame);
		if (i < 3) // 只打印前3个以避免日志过长
		{
			UE_LOG(LogTemp, Log, TEXT("CameraKeyFrame[%d]: Frame=%d, Dist=%f, Pos=(%f,%f,%f), Rot=(%f,%f,%f), FOV=%d, Persp=%d"),
				i, KeyFrame.frameNumber, KeyFrame.distance,
				KeyFrame.position.X, KeyFrame.position.Y, KeyFrame.position.Z,
				KeyFrame.rotation.X, KeyFrame.rotation.Y, KeyFrame.rotation.Z,
				KeyFrame.viewAngle, KeyFrame.perspective);
		}
	}
	return true;
}
bool ReadVMDLightKeyframe(FMemoryReader& Reader, VMDData& VMDInfo) {
	uint32 LightKeyFrameCount = 0;
	Reader << LightKeyFrameCount;
	VMDInfo.LightFrames.Reserve(LightKeyFrameCount);
	for (uint32 i = 0; i < LightKeyFrameCount; ++i) {
		VMDLightKeyframe KeyFrame;
		Reader << KeyFrame.FrameNumber;
		Reader << KeyFrame.Distance;
		float px, py, pz;
		Reader << px << py << pz;
		KeyFrame.Position = FVector(px, py, pz);
		float rx, ry, rz;
		Reader << rx << ry << rz;
		KeyFrame.Rotation = FVector(rx, ry, rz);
		Reader.Serialize(KeyFrame.Interpolation, 24);
		Reader << KeyFrame.ViewAngle;
		Reader << KeyFrame.Perspective;
		VMDInfo.LightFrames.Add(KeyFrame);
		if (i < 3) // 只打印前3个以避免日志过长
		{
			UE_LOG(LogTemp, Log, TEXT("LightKeyFrame[%d]: Frame=%d, Dist=%f, Pos=(%f,%f,%f), Rot=(%f,%f,%f), ViewAngle=%d, Persp=%d"),
				i, KeyFrame.FrameNumber, KeyFrame.Distance,
				KeyFrame.Position.X, KeyFrame.Position.Y, KeyFrame.Position.Z,
				KeyFrame.Rotation.X, KeyFrame.Rotation.Y, KeyFrame.Rotation.Z,
				KeyFrame.ViewAngle, KeyFrame.Perspective);
		}
	}
	return true;
}
bool ReadVMDShadowKeyframe(FMemoryReader& Reader, VMDData& VMDInfo) {
	uint32 ShadowKeyFrameCount = 0;
	Reader << ShadowKeyFrameCount;
	VMDInfo.ShadowFrames.Reserve(ShadowKeyFrameCount);
	for (uint32 i = 0; i < ShadowKeyFrameCount; ++i) {
		VMDShadowKeyframe KeyFrame;
		Reader << KeyFrame.FrameNumber;
		Reader << KeyFrame.Mode;
		Reader << KeyFrame.Distance;
		VMDInfo.ShadowFrames.Add(KeyFrame);
		if (i < 3) // 只打印前3个以避免日志过长
		{
			UE_LOG(LogTemp, Log, TEXT("ShadowKeyFrame[%d]: Frame=%d, Mode=%d, Dist=%f"),
				i, KeyFrame.FrameNumber, KeyFrame.Mode, KeyFrame.Distance);
		}
	}
	return true;
}
bool ReadVMDIKKeyframe(FMemoryReader& Reader, VMDData& VMDInfo) {
	uint32 IKKeyFrameCount = 0;
	Reader << IKKeyFrameCount;
	VMDInfo.IKFrames.Reserve(IKKeyFrameCount);
	for (uint32 i = 0; i < IKKeyFrameCount; ++i) {
		VMDIKKeyframe KeyFrame;
		Reader << KeyFrame.FrameNumber;
		Reader << KeyFrame.Display;
		uint32 ikCount = 0;
		Reader << ikCount;
		for (uint32 j = 0; j < ikCount; ++j) {
			FString ikBoneName;
			VMDInfo.NextStringLength = 20;
			if (!ReadCharArray(Reader, ikBoneName, VMDInfo)) {
				UE_LOG(LogTemp, Error, TEXT("Failed to read IK BoneName for IK keyframe %d, IK %d"), i, j);
				return false;
			}
			uint8 enabled = 0;
			Reader << enabled;
			KeyFrame.IKInfos.Add(TPair<FString, bool>(ikBoneName, enabled != 0));
		}
		VMDInfo.IKFrames.Add(KeyFrame);
		if (i < 3) // 只打印前3个以避免日志过长
		{
			FString IKInfoStr;
			for (const auto& Pair : KeyFrame.IKInfos) {
				IKInfoStr += FString::Printf(TEXT("%s=%s; "), *Pair.Key, Pair.Value ? TEXT("On") : TEXT("Off"));
			}
			UE_LOG(LogTemp, Log, TEXT("IKKeyFrame[%d]: Frame=%d, Display=%d, IKCount=%d, IKs={%s}"),
				i, KeyFrame.FrameNumber, KeyFrame.Display, KeyFrame.IKInfos.Num(), *IKInfoStr);
		}
	}
	return true;
}

bool TVMDParser::ParseVMDFile(const FString& FilePath)
{
	VMDInfo = VMDData();
	if (!FPaths::FileExists(FilePath))
	{
		UE_LOG(LogTemp, Error, TEXT("VMD文件不存在: %s"), *FilePath);
		return false;
	}

	TArray<uint8> FileData;
	if (!FFileHelper::LoadFileToArray(FileData, *FilePath))
	{
		UE_LOG(LogTemp, Error, TEXT("无法加载VMD文件: %s"), *FilePath);
		return false;
	}
	FMemoryReader Reader(FileData, true);

	VMDInfo.NextStringLength = 30;
	if (ReadCharArray(Reader, VMDInfo.Header, VMDInfo))
	{

		UE_LOG(LogTemp, Error, TEXT("VMD: %s"), *VMDInfo.Header);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("NO VMD: %s"), *FilePath);
		return false;
	}
	VMDInfo.NextStringLength = 20;
	if (ReadCharArray(Reader, VMDInfo.ModelName, VMDInfo))
	{
		UE_LOG(LogTemp, Error, TEXT("ModelName: %s"), *VMDInfo.ModelName);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("无法读取模型名称: %s"), *FilePath);
		return false;
	}
	if (!ReadVMDBoneKeyframe(Reader, VMDInfo))
	{
		UE_LOG(LogTemp, Error, TEXT("无法读取骨骼关键帧: %s"), *FilePath);
		return false;
	}
	if (!ReadVMDMorphKeyframe(Reader, VMDInfo))
	{
		UE_LOG(LogTemp, Error, TEXT("无法读取变形关键帧: %s"), *FilePath);
		return false;
	}
	if (!ReadVMDCameraKeyframe(Reader, VMDInfo))
	{
		UE_LOG(LogTemp, Error, TEXT("无法读取摄像机关键帧: %s"), *FilePath);
		return false;
	}
	if (!ReadVMDLightKeyframe(Reader, VMDInfo)) {
		UE_LOG(LogTemp, Error, TEXT("无法读取光照关键帧: %s"), *FilePath);
		return false;
	}
	if (!ReadVMDShadowKeyframe(Reader, VMDInfo)) {
		UE_LOG(LogTemp, Error, TEXT("无法读取阴影关键帧: %s"), *FilePath);
		return false;
	}
	if (!ReadVMDIKKeyframe(Reader, VMDInfo)) {
		UE_LOG(LogTemp, Error, TEXT("无法读取IK关键帧: %s"), *FilePath);
		return false;
	}


	return true;
}

#pragma endregion



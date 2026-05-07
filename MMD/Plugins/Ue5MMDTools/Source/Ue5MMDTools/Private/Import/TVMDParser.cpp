#include "TVMDParser.h"

#include "Misc/FileHelper.h"
#include "Serialization/MemoryReader.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <Windows.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

namespace
{
	constexpr int32 VMDMagicSize = 30;
	constexpr int32 VMDModelNameSizeLegacy = 10;
	constexpr int32 VMDModelNameSize0002 = 20;
	constexpr uint32 VMDMaxReasonableKeyCount = 10 * 1000 * 1000;

	bool ReadBytes(FMemoryReader& Reader, void* Destination, int64 ByteCount, const TCHAR* Context)
	{
		if (ByteCount < 0)
		{
			UE_LOG(LogTemp, Error, TEXT("VMD: Invalid byte count for %s"), Context);
			return false;
		}

		const int64 Remaining = Reader.TotalSize() - Reader.Tell();
		if (Remaining < ByteCount)
		{
			UE_LOG(LogTemp, Error, TEXT("VMD: Not enough bytes for %s. Need=%lld Remaining=%lld Offset=%lld"),
				Context, ByteCount, Remaining, Reader.Tell());
			return false;
		}

		Reader.Serialize(Destination, ByteCount);
		return true;
	}

	template<typename T>
	bool ReadValue(FMemoryReader& Reader, T& OutValue, const TCHAR* Context)
	{
		return ReadBytes(Reader, &OutValue, sizeof(T), Context);
	}

	bool ReadVector3(FMemoryReader& Reader, FVector& OutVector, const TCHAR* Context)
	{
		float X = 0.0f;
		float Y = 0.0f;
		float Z = 0.0f;
		if (!ReadValue(Reader, X, Context)
			|| !ReadValue(Reader, Y, Context)
			|| !ReadValue(Reader, Z, Context))
		{
			return false;
		}

		OutVector = FVector(X, Y, Z);
		return true;
	}

	bool ReadQuat(FMemoryReader& Reader, FQuat& OutQuat, const TCHAR* Context)
	{
		float X = 0.0f;
		float Y = 0.0f;
		float Z = 0.0f;
		float W = 1.0f;
		if (!ReadValue(Reader, X, Context)
			|| !ReadValue(Reader, Y, Context)
			|| !ReadValue(Reader, Z, Context)
			|| !ReadValue(Reader, W, Context))
		{
			return false;
		}

		OutQuat = FQuat(X, Y, Z, W);
		return true;
	}

	bool ReadCount(FMemoryReader& Reader, uint32& OutCount, uint32 MinRecordSize, const TCHAR* Context)
	{
		if (!ReadValue(Reader, OutCount, Context))
		{
			return false;
		}

		if (OutCount > VMDMaxReasonableKeyCount)
		{
			UE_LOG(LogTemp, Error, TEXT("VMD: %s count is unreasonably large: %u"), Context, OutCount);
			return false;
		}

		if (MinRecordSize > 0)
		{
			const uint64 Remaining = static_cast<uint64>(Reader.TotalSize() - Reader.Tell());
			const uint64 RequiredMin = static_cast<uint64>(OutCount) * static_cast<uint64>(MinRecordSize);
			if (RequiredMin > Remaining)
			{
				UE_LOG(LogTemp, Error, TEXT("VMD: %s count %u exceeds remaining bytes. NeedAtLeast=%llu Remaining=%llu"),
					Context, OutCount, RequiredMin, Remaining);
				return false;
			}
		}

		return true;
	}

	FString ShiftJIS_ToFString(const uint8* Data, int32 Length)
	{
		if (Data == nullptr || Length <= 0)
		{
			return FString();
		}

		int32 EffectiveLength = 0;
		while (EffectiveLength < Length && Data[EffectiveLength] != 0)
		{
			++EffectiveLength;
		}

		if (EffectiveLength == 0)
		{
			return FString();
		}

		auto FallbackAnsiString = [Data, EffectiveLength]() -> FString
		{
			TArray<ANSICHAR> AnsiBuffer;
			AnsiBuffer.SetNumZeroed(EffectiveLength + 1);
			for (int32 Index = 0; Index < EffectiveLength; ++Index)
			{
				AnsiBuffer[Index] = static_cast<ANSICHAR>(Data[Index]);
			}
			return FString(ANSI_TO_TCHAR(AnsiBuffer.GetData()));
		};

#if PLATFORM_WINDOWS
		const int32 WideLength = ::MultiByteToWideChar(932, 0, reinterpret_cast<LPCCH>(Data), EffectiveLength, nullptr, 0);
		if (WideLength <= 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("VMD: Failed to decode Shift-JIS string with CP932, falling back to ANSI conversion."));
			return FallbackAnsiString();
		}

		TArray<WIDECHAR> WideBuffer;
		WideBuffer.SetNumZeroed(WideLength + 1);
		::MultiByteToWideChar(932, 0, reinterpret_cast<LPCCH>(Data), EffectiveLength, WideBuffer.GetData(), WideLength);
		return FString(WideBuffer.GetData());
#else
		return FallbackAnsiString();
#endif
	}

	FString ReadFixedShiftJISString(const uint8* Data, int32 Length)
	{
		return ShiftJIS_ToFString(Data, Length).TrimStartAndEnd();
	}

	bool ReadFixedShiftJISField(FMemoryReader& Reader, int32 FieldSize, FString& OutString, const TCHAR* Context)
	{
		TArray<uint8> Buffer;
		Buffer.SetNumZeroed(FieldSize);
		if (!ReadBytes(Reader, Buffer.GetData(), FieldSize, Context))
		{
			return false;
		}

		OutString = ReadFixedShiftJISString(Buffer.GetData(), FieldSize);
		return true;
	}

	bool TryReadOptionalCount(FMemoryReader& Reader, uint32& OutCount, uint32 MinRecordSize, const TCHAR* Context)
	{
		const int64 Remaining = Reader.TotalSize() - Reader.Tell();
		if (Remaining == 0)
		{
			OutCount = 0;
			return false;
		}

		if (Remaining < static_cast<int64>(sizeof(uint32)))
		{
			UE_LOG(LogTemp, Warning, TEXT("VMD: Trailing bytes too short for %s count. Remaining=%lld"), Context, Remaining);
			OutCount = 0;
			return false;
		}

		return ReadCount(Reader, OutCount, MinRecordSize, Context);
	}
}

bool TVMDParser::ParseVMDFile(const FString& FilePath)
{
	VMDInfo = VMDData{};

	if (FilePath.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("VMD: File path is empty."));
		return false;
	}

	if (!FPaths::FileExists(FilePath))
	{
		UE_LOG(LogTemp, Error, TEXT("VMD: File does not exist: %s"), *FilePath);
		return false;
	}

	TArray<uint8> VMDFileData;
	if (!FFileHelper::LoadFileToArray(VMDFileData, *FilePath))
	{
		UE_LOG(LogTemp, Error, TEXT("VMD: Failed to load file: %s"), *FilePath);
		return false;
	}

	FMemoryReader Reader(VMDFileData, true);

	uint8 MagicBytes[VMDMagicSize] = {};
	if (!ReadBytes(Reader, MagicBytes, VMDMagicSize, TEXT("HeaderMagic")))
	{
		return false;
	}

	VMDInfo.Header = ReadFixedShiftJISString(MagicBytes, VMDMagicSize);
	VMDInfo.bNewFormatHeader = VMDInfo.Header.StartsWith(TEXT("Vocaloid Motion Data 0002"));

	if (!VMDInfo.Header.StartsWith(TEXT("Vocaloid Motion Data")))
	{
		UE_LOG(LogTemp, Error, TEXT("VMD: Unsupported header: %s"), *VMDInfo.Header);
		return false;
	}

	const int32 ModelNameFieldSize = VMDInfo.bNewFormatHeader ? VMDModelNameSize0002 : VMDModelNameSizeLegacy;
	if (!ReadFixedShiftJISField(Reader, ModelNameFieldSize, VMDInfo.ModelName, TEXT("ModelName")))
	{
		return false;
	}

	uint32 BoneKeyframeCount = 0;
	if (!ReadCount(Reader, BoneKeyframeCount, 111, TEXT("BoneKeyframes")))
	{
		return false;
	}
	VMDInfo.BoneKeyframes.Reserve(BoneKeyframeCount);
	for (uint32 Index = 0; Index < BoneKeyframeCount; ++Index)
	{
		VMDBoneKeyframe Keyframe{};
		if (!ReadFixedShiftJISField(Reader, 15, Keyframe.BoneName, TEXT("BoneName"))
			|| !ReadValue(Reader, Keyframe.FrameNumber, TEXT("BoneFrame"))
			|| !ReadVector3(Reader, Keyframe.Position, TEXT("BonePosition"))
			|| !ReadQuat(Reader, Keyframe.Rotation, TEXT("BoneRotation"))
			|| !ReadBytes(Reader, Keyframe.Interpolation, UE_ARRAY_COUNT(Keyframe.Interpolation), TEXT("BoneInterpolation")))
		{
			UE_LOG(LogTemp, Error, TEXT("VMD: Failed to read bone keyframe %u"), Index);
			return false;
		}
		VMDInfo.BoneKeyframes.Add(MoveTemp(Keyframe));
	}

	uint32 MorphKeyframeCount = 0;
	if (!ReadCount(Reader, MorphKeyframeCount, 23, TEXT("MorphKeyframes")))
	{
		return false;
	}
	VMDInfo.MorphKeyframes.Reserve(MorphKeyframeCount);
	for (uint32 Index = 0; Index < MorphKeyframeCount; ++Index)
	{
		VMDMorphKeyframe Keyframe{};
		if (!ReadFixedShiftJISField(Reader, 15, Keyframe.MorphName, TEXT("MorphName"))
			|| !ReadValue(Reader, Keyframe.FrameNumber, TEXT("MorphFrame"))
			|| !ReadValue(Reader, Keyframe.Weight, TEXT("MorphWeight")))
		{
			UE_LOG(LogTemp, Error, TEXT("VMD: Failed to read morph keyframe %u"), Index);
			return false;
		}
		VMDInfo.MorphKeyframes.Add(MoveTemp(Keyframe));
	}

	uint32 CameraKeyframeCount = 0;
	if (TryReadOptionalCount(Reader, CameraKeyframeCount, 61, TEXT("CameraKeyframes")))
	{
		VMDInfo.CameraKeyframes.Reserve(CameraKeyframeCount);
		for (uint32 Index = 0; Index < CameraKeyframeCount; ++Index)
		{
			VMDCameraKeyframe Keyframe{};
			if (!ReadValue(Reader, Keyframe.FrameNumber, TEXT("CameraFrame"))
				|| !ReadValue(Reader, Keyframe.Distance, TEXT("CameraDistance"))
				|| !ReadVector3(Reader, Keyframe.Interest, TEXT("CameraInterest"))
				|| !ReadVector3(Reader, Keyframe.Rotation, TEXT("CameraRotation"))
				|| !ReadBytes(Reader, Keyframe.Interpolation, UE_ARRAY_COUNT(Keyframe.Interpolation), TEXT("CameraInterpolation"))
				|| !ReadValue(Reader, Keyframe.ViewAngle, TEXT("CameraViewAngle"))
				|| !ReadValue(Reader, Keyframe.Perspective, TEXT("CameraPerspective")))
			{
				UE_LOG(LogTemp, Error, TEXT("VMD: Failed to read camera keyframe %u"), Index);
				return false;
			}
			VMDInfo.CameraKeyframes.Add(MoveTemp(Keyframe));
		}
	}

	uint32 LightKeyframeCount = 0;
	if (TryReadOptionalCount(Reader, LightKeyframeCount, 28, TEXT("LightKeyframes")))
	{
		VMDInfo.LightKeyframes.Reserve(LightKeyframeCount);
		for (uint32 Index = 0; Index < LightKeyframeCount; ++Index)
		{
			VMDLightKeyframe Keyframe{};
			if (!ReadValue(Reader, Keyframe.FrameNumber, TEXT("LightFrame"))
				|| !ReadVector3(Reader, Keyframe.Color, TEXT("LightColor"))
				|| !ReadVector3(Reader, Keyframe.Position, TEXT("LightPosition")))
			{
				UE_LOG(LogTemp, Error, TEXT("VMD: Failed to read light keyframe %u"), Index);
				return false;
			}
			VMDInfo.LightKeyframes.Add(MoveTemp(Keyframe));
		}
	}

	uint32 ShadowKeyframeCount = 0;
	if (TryReadOptionalCount(Reader, ShadowKeyframeCount, 9, TEXT("ShadowKeyframes")))
	{
		VMDInfo.ShadowKeyframes.Reserve(ShadowKeyframeCount);
		for (uint32 Index = 0; Index < ShadowKeyframeCount; ++Index)
		{
			VMDShadowKeyframe Keyframe{};
			if (!ReadValue(Reader, Keyframe.FrameNumber, TEXT("ShadowFrame"))
				|| !ReadValue(Reader, Keyframe.Mode, TEXT("ShadowMode"))
				|| !ReadValue(Reader, Keyframe.Distance, TEXT("ShadowDistance")))
			{
				UE_LOG(LogTemp, Error, TEXT("VMD: Failed to read shadow keyframe %u"), Index);
				return false;
			}
			VMDInfo.ShadowKeyframes.Add(MoveTemp(Keyframe));
		}
	}

	uint32 IKGroupCount = 0;
	if (TryReadOptionalCount(Reader, IKGroupCount, 9, TEXT("IKKeyframes")))
	{
		VMDInfo.IKKeyframes.Reserve(IKGroupCount);
		for (uint32 GroupIndex = 0; GroupIndex < IKGroupCount; ++GroupIndex)
		{
			VMDIKKeyframe Keyframe{};
			if (!ReadValue(Reader, Keyframe.FrameNumber, TEXT("IKFrame"))
				|| !ReadValue(Reader, Keyframe.Display, TEXT("IKDisplay"))
				|| !ReadCount(Reader, Keyframe.IKInfoCount, 21, TEXT("IKInfos")))
			{
				UE_LOG(LogTemp, Error, TEXT("VMD: Failed to read IK keyframe header %u"), GroupIndex);
				return false;
			}

			Keyframe.IKInfos.Reserve(Keyframe.IKInfoCount);
			for (uint32 IKIndex = 0; IKIndex < Keyframe.IKInfoCount; ++IKIndex)
			{
				FString IKBoneName;
				uint8 bEnabled = 0;
				if (!ReadFixedShiftJISField(Reader, 20, IKBoneName, TEXT("IKBoneName"))
					|| !ReadValue(Reader, bEnabled, TEXT("IKEnabled")))
				{
					UE_LOG(LogTemp, Error, TEXT("VMD: Failed to read IK info %u in group %u"), IKIndex, GroupIndex);
					return false;
				}
				Keyframe.IKInfos.Emplace(MoveTemp(IKBoneName), bEnabled);
			}

			VMDInfo.IKKeyframes.Add(MoveTemp(Keyframe));
		}
	}

	const int64 Remaining = Reader.TotalSize() - Reader.Tell();
	if (Remaining > 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("VMD: %lld trailing bytes remain after parsing %s"), Remaining, *FilePath);
	}

	UE_LOG(LogTemp, Log, TEXT("VMD: Parsed %s | Model=%s | Bone=%d Morph=%d Camera=%d Light=%d Shadow=%d IK=%d"),
		*FilePath,
		*VMDInfo.ModelName,
		VMDInfo.BoneKeyframes.Num(),
		VMDInfo.MorphKeyframes.Num(),
		VMDInfo.CameraKeyframes.Num(),
		VMDInfo.LightKeyframes.Num(),
		VMDInfo.ShadowKeyframes.Num(),
		VMDInfo.IKKeyframes.Num());

	return true;
}

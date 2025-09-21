#include "TVMDParser.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "Serialization/MemoryReader.h"

FString DecodeSJISToString(const char* SJISStr, int32 Length)
{
    return FString(ANSI_TO_TCHAR(SJISStr));
}
bool ReadCharArray(FMemoryReader& Reader, FString& OutString, VMDData& VMDInfo)
{
	OutString.Reset();

	const int32 StringLength = VMDInfo.NextByteLength;
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
	OutString = FString(ANSI_TO_TCHAR(AnsiChars.GetData()));

	return true;

}
bool TVMDParser::ParserVMDFile(const FString& FilePath)
{

	return false;
}

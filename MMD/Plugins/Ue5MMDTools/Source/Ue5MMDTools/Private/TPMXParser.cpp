#include "TPMXParser.h"
#include "Misc/FileHelper.h"
#include "Serialization/MemoryReader.h"
bool TPMXParser::ParsePMXFile(const FString &FilePath)
{

    if (FilePath.IsEmpty()) // 如果路径为空就返回false
    {
        UE_LOG(LogTemp, Error, TEXT("ParsePMXFile: FilePath is empty"));
        return false;
    }
    IFileManager &FileManager = IFileManager::Get();
    if (!FileManager.FileExists(*FilePath))
    {
        UE_LOG(LogTemp, Error, TEXT("ParsePMXFile: File does not exist: %s"), *FilePath);
        return false;
    }

    return true;
}
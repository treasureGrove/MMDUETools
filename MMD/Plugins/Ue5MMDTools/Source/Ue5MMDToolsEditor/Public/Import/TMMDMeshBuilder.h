#pragma once

#include "CoreMinimal.h"
#include "TPMXParser.h"
#include "TVMDParser.h"
#if WITH_EDITOR              
#include "RetargetEditor/IKRetargeterController.h"    
#endif
class UIKRigDefinition;
class USkeleton;
class USkeletalMesh;
class UAnimSequence;
class UMMDModelDataAsset;

struct FMMDResolvedBoneTrack
{
	FString SourceBoneName;
	FName TargetBoneName = NAME_None;
	int32 TargetBoneIndex = INDEX_NONE;
	int32 KeyCount = 0;
	bool bMatched = false;
};

struct FMMDResolvedMorphTrack
{
	FString SourceMorphName;
	FName TargetMorphName = NAME_None;
	int32 KeyCount = 0;
	bool bMatched = false;
};

struct FMMDAnimationImportSettings
{
	float FrameRate = 30.0f;
	float PositionScale = 8.0f;
	bool bImportBoneTracks = true;
	bool bImportMorphCurves = true;
	bool bBakeMMDIKToFK = true;
	bool bSkipUnmatchedTracks = true;
};

struct FMMDAnimationImportContext
{
	USkeletalMesh* SkeletalMesh = nullptr;
	USkeleton* Skeleton = nullptr;
	const PMXDatas* PMXData = nullptr;
	// 保留源骨骼到目标骨架的映射，区别清理后重名的骨骼，并供同模型多段动作复用。
	TArray<int32> PMXBoneToSkeleton;
	TObjectPtr<UMMDModelDataAsset> ModelDataAsset = nullptr;
	FString SourcePMXFilePath;
	FString SourceVMDFilePath;
};

struct FMMDAnimationImportReport
{
	FString PackagePath;
	FString AssetName;
	int32 SourceBoneTrackCount = 0;
	int32 MatchedBoneTrackCount = 0;
	int32 SourceMorphTrackCount = 0;
	int32 MatchedMorphTrackCount = 0;
	int32 MaxFrame = 0;
	TArray<FMMDResolvedBoneTrack> BoneTracks;
	TArray<FMMDResolvedMorphTrack> MorphTracks;
	TArray<FString> Warnings;
	TArray<FString> Errors;

	bool HasErrors() const
	{
		return Errors.Num() > 0;
	}
};

struct FMMDAsyncMeshImportState;

/** 异步网格导入完成回调（游戏线程调用）。SkeletalMesh 为 null 表示解析失败/被取消。 */
using FMMDMeshImportFinished = TFunction<void(USkeletalMesh* /*SkeletalMesh*/, const PMXDatas* /*PMXData*/)>;

/** 异步网格导入句柄。Cancel() 请求取消，实际停止发生在下一个阶段边界。 */
struct FMMDAsyncMeshImportHandle
{
public:
	/** 请求取消导入。 */
	UE5MMDTOOLSEDITOR_API void Cancel();

	/** 是否已请求取消。 */
	UE5MMDTOOLSEDITOR_API bool IsCancelled() const;

private:
	friend class TMMDMeshBuilder;
	TWeakPtr<FMMDAsyncMeshImportState, ESPMode::ThreadSafe> StatePtr;
};

class UE5MMDTOOLSEDITOR_API TMMDMeshBuilder
{
public:
    // MMDStandard 材质导入开关：false（默认）走现有材质链；true 时导入 toon 贴图
    // （共享 toon 用插件内置 ramp，自定义 toon 导入贴图文件）并挂 M_MMDStandard_* 父材质。
    static bool bUseStandardMaterials;
    static void SetUseStandardMaterials(bool bEnabled) { bUseStandardMaterials = bEnabled; }
    static bool IsUseStandardMaterials() { return bUseStandardMaterials; }

    static USkeletalMesh *BuildSkeletalMeshFromPMX(const PMXDatas &PMXInfo, const FString &PackagePath, const FString &AssetName, const FString& PMXFilePath);

	/** 异步导入 PMX 为骨骼网格：后台线程解析+数据准备 → 游戏线程分帧导入贴图/材质 → 游戏线程收尾构建。
	 *  导入期间编辑器保持响应；完成/失败/取消时在游戏线程回调 OnFinished。
	 *  同一时间只允许一个异步导入，已有进行中时返回空句柄。 */
	static TSharedPtr<FMMDAsyncMeshImportHandle> ImportSkeletalMeshFromPMXAsync(
		const FString& PMXFilePath, const FString& PackagePath, const FString& AssetName,
		FMMDMeshImportFinished OnFinished);

	/** 游戏线程初始化异步导入服务；关闭后只能显式重新初始化。 */
	static void InitializeAsyncImports();

	/** 游戏线程取消当前批次，等待已提交的后台工作并排空收尾回调。 */
	static void ShutdownAsyncImports();

	/** 取 PMX 解析数据（带缓存）：同一路径且文件未修改时直接复用上次解析结果，
	 *  供 VMD 导入/物理烘焙等重复使用同一模型的操作加速；模型导入不走缓存（始终读盘取最新）。
	 *  返回指针仅在后续 GetOrParsePMXData 调用或缓存淘汰前有效；游戏线程使用。 */
	static const PMXDatas* GetOrParsePMXData(const FString& PMXFilePath);

    
	static UIKRigDefinition* BuildIKRigFromPMX(USkeletalMesh* SkeletalMesh,const FString& PMXFilePath);

    static UAnimBlueprint* BuildAnimBlueprint(USkeletalMesh* SkeletalMesh,const FString& PMXFilePath);

	static UIKRetargeter* BuildIKRetargeterFromPMX(UIKRigDefinition* IKRigTarget, const FString& PMXFilePath);

	static bool BuildAnimationImportContext(USkeletalMesh* SkeletalMesh, const PMXDatas* PMXData, const FString& PMXFilePath, const FString& VMDFilePath, FMMDAnimationImportContext& OutContext, FMMDAnimationImportReport* OutReport = nullptr);

	static bool AnalyzeVMDAnimationImport(const VMDData& VmdData, const FMMDAnimationImportContext& Context, const FMMDAnimationImportSettings& Settings, FMMDAnimationImportReport& OutReport);

	static UAnimSequence* BuildVMDAnimation(const VMDData& VmdData, const FMMDAnimationImportContext& Context, const FMMDAnimationImportSettings& Settings, FMMDAnimationImportReport* OutReport = nullptr);

	static bool AppendVMDMorphCurvesToAnimSequence(UAnimSequence* AnimSequence, const VMDData& VmdData, const FMMDAnimationImportContext& Context, const FMMDAnimationImportSettings& Settings, FMMDAnimationImportReport* OutReport = nullptr);

	static UAnimSequence* BuildVMDAnimation(const VMDData &VmdData,const FString& VMDFilePath);
}; 


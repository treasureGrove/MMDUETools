#include "FMMDActorImportService.h"
#include "AMMDActor.h"
#include "Rendering/MMDOutlineLibrary.h"
#include "TMMDMeshBuilder.h"
#include "MMDImportSetting.h"
#include "AGN_MMDSkeletalControlEditor.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Animation/AnimBlueprint.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"
// 控制台命令：重新导入关卡里所有 MMD 角色（验证 mesh 构建改动如 bCastShadow 时用）。
// 用法：编辑器输出日志里输入 MMD.Reimport
static FAutoConsoleCommand GMMDReimportCommand(
	TEXT("MMD.Reimport"),
	TEXT("Re-import all MMD actors in the current level from their source PMX files."),
	FConsoleCommandDelegate::CreateLambda([]()
	{
		UWorld* World = GWorld;
		if (!World)
		{
			UE_LOG(LogTemp, Warning, TEXT("[MMD.Reimport] No world available."));
			return;
		}
		int32 Count = 0;
		for (TActorIterator<AMMDActor> It(World); It; ++It)
		{
			AMMDActor* MMD = *It;
			if (MMD && !MMD->SourcePMXFilePath.IsEmpty())
			{
				MMD->SetupComponents(MMD->SourcePMXFilePath);
				Count++;
			}
		}
		UE_LOG(LogTemp, Log, TEXT("[MMD.Reimport] Re-imported %d MMD actor(s)."), Count);
	})
);
void FMMDActorImportService::SetupComponents(AMMDActor* Actor, const FString& FilePath)
{
    // 记住路径：保存为蓝图时会复制到 CDO，供蓝图实例 OnConstruction 使用
    Actor->SourcePMXFilePath = FilePath;

    // 异步导入：后台线程解析+数据准备，游戏线程分帧导入贴图/材质，编辑器不再整段冻结；
    // 完成后在 FinishSetupComponents 里继续装配组件/动画蓝图/IK 资产。
    TWeakObjectPtr<AMMDActor> WeakActor(Actor);
    const FString PathCopy = FilePath;
    PendingImports.FindOrAdd(Actor) = TMMDMeshBuilder::ImportSkeletalMeshFromPMXAsync(
        FilePath, TEXT("/Game/MMDModels"), FString(),
        [WeakActor, PathCopy](USkeletalMesh* BuiltMesh, const PMXDatas* PMXData)
        {
            if (AMMDActor* CompletedActor = WeakActor.Get())
            {
                CompletedActor->FinishSetupComponents(PathCopy, BuiltMesh, PMXData);
            }
        });

    if (PendingImports.FindChecked(Actor).IsValid())
    {
        MMDImportSetting::ShowGlobalImportProgress(
            FString::Printf(TEXT("正在后台导入 PMX: %s（期间编辑器可继续操作，完成后自动装配）"), *FPaths::GetCleanFilename(FilePath)));
    }
    else
    {
        MMDImportSetting::ShowGlobalImportProgress(TEXT("已有 MMD 导入进行中，请等待完成后再试"), EMMDMessageType::Warning);
    }
}

void FMMDActorImportService::FinishSetupComponents(AMMDActor* Actor, const FString& FilePath, USkeletalMesh* BuiltMesh, const PMXDatas* PMXData)
{
    PendingImports.Remove(Actor);
    USkeletalMeshComponent* SkeletalMeshComponent = Actor->GetMeshComponent();

    if (!BuiltMesh || !PMXData)
    {
        UE_LOG(LogTemp, Error, TEXT("[AMMDActor] PMX 异步导入失败: %s"), *FilePath);
        MMDImportSetting::ShowGlobalImportProgress(TEXT("MMD 模型导入失败（详见输出日志）"), EMMDMessageType::Error);
        Actor->OnSetupFinished().Broadcast(Actor);
        Actor->OnSetupFinished().Clear();
        return;
    }

    MMDImportSetting::ShowGlobalImportProgress(
        FString::Printf(TEXT("PMX 解析与材质导入完成，正在装配: %s"), *FPaths::GetCleanFilename(FilePath)));

    if (!SkeletalMeshComponent)
    {
        UE_LOG(LogTemp, Error, TEXT("MMD 角色缺少骨骼网格组件"));
        Actor->OnSetupFinished().Broadcast(Actor);
        Actor->OnSetupFinished().Clear();
        return;
    }
    SkeletalMeshComponent->SetSkeletalMesh(BuiltMesh);
    UMMDOutlineLibrary::ApplyMMDOutline(SkeletalMeshComponent);

    TMMDMeshBuilder MeshBuilder;
    UAnimBlueprint* MMDAnimBP = MeshBuilder.BuildAnimBlueprint(BuiltMesh, FilePath);
    if (!MMDAnimBP)
    {
        UE_LOG(LogTemp, Error, TEXT("[AMMDActor] 动画蓝图创建失败: %s"), *FilePath);
        MMDImportSetting::ShowGlobalImportProgress(TEXT("MMD 动画蓝图创建失败"), EMMDMessageType::Error);
        Actor->OnSetupFinished().Broadcast(Actor);
        Actor->OnSetupFinished().Clear();
        return;
    }

    FMMDAnimGraphHelper::AddMMDNodeToAnimBP(MMDAnimBP, *PMXData, true);
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(MMDAnimBP);
    FKismetEditorUtilities::CompileBlueprint(MMDAnimBP);

    SkeletalMeshComponent->SetAnimInstanceClass(MMDAnimBP->GeneratedClass);
    if (!SkeletalMeshComponent->GetAnimInstance())
    {
        SkeletalMeshComponent->InitAnim(true);
    }

#if WITH_EDITOR
    {
        UIKRigDefinition* IKRig = MeshBuilder.BuildIKRigFromPMX(BuiltMesh, FilePath);
        UIKRetargeter* IKRetargeter = MeshBuilder.BuildIKRetargeterFromPMX(IKRig, FilePath);
        UE_LOG(LogTemp, Log, TEXT("[AMMDActor] IK assets built: IKRig=%s, Retargeter=%s"),
            IKRig ? *IKRig->GetName() : TEXT("None"),
            IKRetargeter ? *IKRetargeter->GetName() : TEXT("None"));
    }
#endif

    MMDImportSetting::ShowGlobalImportProgress(
        FString::Printf(TEXT("MMD 模型导入并装配完成: %s"), *FPaths::GetCleanFilename(FilePath)), EMMDMessageType::Success);

    // 一次性事件：通知 UI 保存蓝图/刷新预览；广播后清空订阅
    Actor->OnSetupFinished().Broadcast(Actor);
    Actor->OnSetupFinished().Clear();
}

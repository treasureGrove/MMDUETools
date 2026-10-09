#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Async/TaskGraphInterfaces.h"
#include "Containers/Ticker.h"
#include "Engine/SkeletalMesh.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "TMMDMeshBuilder.h"
#include "UObject/Package.h"
#include "UObject/UObjectIterator.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMMDAsyncImportCancellationTest, "Ue5MMDTools.Import.AsyncCancellation",
    EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FMMDAsyncImportCancellationTest::RunTest(const FString& Parameters)
{
    if (!IsRunningCommandlet() || !IsInGameThread())
    {
        AddError(TEXT("异步取消沙盒回归只允许 Commandlet 游戏线程"));
        return false;
    }
    const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Ue5MMDTools"));
    if (!Plugin.IsValid()) return false;
    const FString UniqueName = TEXT("MMDBackgroundValidation_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    const FString Root = TEXT("/Game/MMDModels/") + UniqueName + TEXT("/");
    const FString Directory = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("BackgroundValidation/ImportedAssets") / UniqueName) + TEXT("/");
    const FString FixtureDirectory = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("BackgroundValidation/Fixtures"));
    const FString FixturePath = FixtureDirectory / (UniqueName + TEXT(".pmx"));
    const FString Source = Plugin->GetBaseDir() / TEXT("Resources/YYB Assistant Gen. Works Librarian Miku/Librarian's Sword.pmx");
    if (!TestTrue(TEXT("创建异步沙盒"), IFileManager::Get().MakeDirectory(*Directory, true)
        && IFileManager::Get().MakeDirectory(*FixtureDirectory, true))
        || !TestTrue(TEXT("复制最小真实 PMX 到 Saved"), IFileManager::Get().Copy(*FixturePath, *Source) == COPY_OK)) return false;
    struct FMountScope
    {
        FString Root, Directory;
        ~FMountScope() { FPackageName::UnRegisterMountPoint(Root, Directory); }
    } Mount{Root, Directory};
    FPackageName::RegisterMountPoint(Root, Directory);
    FString ProposedPath;
    if (!TestTrue(TEXT("异步产物路径严格映射 Saved"), FPackageName::TryConvertLongPackageNameToFilename(
        Root + TEXT("Model/") + UniqueName, ProposedPath, FPackageName::GetAssetPackageExtension())
        && FPaths::IsUnderDirectory(FPaths::ConvertRelativePathToFull(ProposedPath), Directory))) return false;

    struct FResults
    {
        int32 Completions = 0;
        bool UnexpectedMesh = false;
        bool AllCallbacksOnGameThread = true;
        bool RestartAccepted = false;
        bool RestartCancelled = false;
        bool RefusedCallbackInvoked = false;
        TSharedPtr<FMMDAsyncMeshImportHandle> Restart;
    };
    // 回调只捕获共享结果，超时后即使后台任务迟到也不会引用已销毁的测试栈。
    const TSharedRef<FResults> Results = MakeShared<FResults>();
    TSharedPtr<FMMDAsyncMeshImportHandle> Handle = TMMDMeshBuilder::ImportSkeletalMeshFromPMXAsync(
        FixturePath, Root, UniqueName, [Results, FixturePath, Root, UniqueName](USkeletalMesh* Mesh, const PMXDatas*)
        {
            ++Results->Completions;
            Results->UnexpectedMesh |= Mesh != nullptr;
            Results->AllCallbacksOnGameThread &= IsInGameThread();
            Results->Restart = TMMDMeshBuilder::ImportSkeletalMeshFromPMXAsync(FixturePath, Root, UniqueName,
                [Results](USkeletalMesh* SecondMesh, const PMXDatas*)
                {
                    ++Results->Completions;
                    Results->UnexpectedMesh |= SecondMesh != nullptr;
                    Results->AllCallbacksOnGameThread &= IsInGameThread();
                });
            Results->RestartAccepted = Results->Restart.IsValid();
            if (Results->Restart.IsValid())
            {
                Results->Restart->Cancel();
                Results->RestartCancelled = Results->Restart->IsCancelled();
            }
        });
    if (!TestTrue(TEXT("真实异步导入请求接受"), Handle.IsValid())) return false;
    AddExpectedMessagePlain(TEXT("已有导入进行中，忽略本次请求"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
    const TSharedPtr<FMMDAsyncMeshImportHandle> Refused = TMMDMeshBuilder::ImportSkeletalMeshFromPMXAsync(
        FixturePath, Root, UniqueName, [Results](USkeletalMesh*, const PMXDatas*) { Results->RefusedCallbackInvoked = true; });
    TestFalse(TEXT("进行中的重复导入拒绝"), Refused.IsValid());
    Handle->Cancel();
    TestTrue(TEXT("取消请求即时可见"), Handle->IsCancelled());
    const double Deadline = FPlatformTime::Seconds() + 30.0;
    while (Results->Completions < 2 && FPlatformTime::Seconds() < Deadline)
    {
        FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
        FTSTicker::GetCoreTicker().Tick(1.0f / 60.0f);
        FPlatformProcess::Sleep(0.001f);
    }
    if (!TestEqual(TEXT("两次取消各完成一次回调"), Results->Completions, 2)) return false;
    TestTrue(TEXT("完成回调中下一批导入可接受"), Results->RestartAccepted);
    TestTrue(TEXT("回调中新批次也可以取消"), Results->RestartCancelled);
    TestTrue(TEXT("完成回调在游戏线程"), Results->AllCallbacksOnGameThread);
    TestFalse(TEXT("被拒绝请求没有回调"), Results->RefusedCallbackInvoked);
    TestFalse(TEXT("阶段边界取消不生成网格"), Results->UnexpectedMesh);
    int32 Packages = 0;
    for (TObjectIterator<UPackage> It; It; ++It) Packages += It->GetName().StartsWith(Root) ? 1 : 0;
    TArray<FString> Files;
    IFileManager::Get().FindFilesRecursive(Files, *Directory, TEXT("*"), true, false);
    TestEqual(TEXT("取消没有创建 UObject 包"), Packages, 0);
    TestEqual(TEXT("取消没有保存资产文件"), Files.Num(), 0);
    TSharedRef<FJsonObject> Report = MakeShared<FJsonObject>();
    Report->SetStringField(TEXT("source_pmx"), Source);
    Report->SetStringField(TEXT("output_directory"), Directory);
    Report->SetNumberField(TEXT("cancellation_callbacks"), Results->Completions);
    Report->SetNumberField(TEXT("asset_count"), Files.Num());
    Report->SetBoolField(TEXT("callback_restart_accepted"), Results->RestartAccepted);
    FString Json;
    TestTrue(TEXT("保存后台取消清单"), FJsonSerializer::Serialize(Report, TJsonWriterFactory<>::Create(&Json))
        && FFileHelper::SaveStringToFile(Json, *(Directory + TEXT("cancel-manifest.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMMDAsyncImportShutdownTest, "Ue5MMDTools.Import.AsyncShutdown",
    EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FMMDAsyncImportShutdownTest::RunTest(const FString& Parameters)
{
    if (!IsRunningCommandlet() || !IsInGameThread()) return false;
    struct FServiceScope
    {
        ~FServiceScope()
        {
            TMMDMeshBuilder::ShutdownAsyncImports();
            TMMDMeshBuilder::InitializeAsyncImports();
        }
    } ServiceScope;
    TMMDMeshBuilder::InitializeAsyncImports();
    const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Ue5MMDTools"));
    if (!Plugin.IsValid()) return false;
    const FString Name = TEXT("MMDBackgroundValidation_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    const FString Root = TEXT("/Game/MMDModels/") + Name + TEXT("/");
    const FString Directory = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("BackgroundValidation/ImportedAssets") / Name) + TEXT("/");
    const FString FixtureDirectory = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("BackgroundValidation/Fixtures"));
    const FString Path = FixtureDirectory / (Name + TEXT(".pmx"));
    const FString Source = Plugin->GetBaseDir() / TEXT("Resources/YYB Assistant Gen. Works Librarian Miku/Librarian's Sword.pmx");
    if (!TestTrue(TEXT("创建关闭服务沙盒"), IFileManager::Get().MakeDirectory(*Directory, true)
        && IFileManager::Get().MakeDirectory(*FixtureDirectory, true))
        || !TestTrue(TEXT("复制真实 PMX 到关闭服务 fixture"), IFileManager::Get().Copy(*Path, *Source) == COPY_OK)) return false;
    struct FMountScope
    {
        FString Root, Directory;
        ~FMountScope() { FPackageName::UnRegisterMountPoint(Root, Directory); }
    } Mount{Root, Directory};
    FPackageName::RegisterMountPoint(Root, Directory);
    const TSharedRef<int32> CallbackCount = MakeShared<int32>(0);
    auto Start = [Path, Root, Name, CallbackCount]()
    {
        return TMMDMeshBuilder::ImportSkeletalMeshFromPMXAsync(Path, Root, Name,
            [CallbackCount](USkeletalMesh*, const PMXDatas*) { ++*CallbackCount; });
    };
    TSharedPtr<FMMDAsyncMeshImportHandle> Handle = Start();
    if (!TestTrue(TEXT("关闭前真实异步请求接受"), Handle.IsValid())) return false;
    Handle->Cancel();
    const double Begin = FPlatformTime::Seconds();
    TMMDMeshBuilder::ShutdownAsyncImports();
    TestTrue(TEXT("有限样本关闭收尾及时返回"), FPlatformTime::Seconds() - Begin < 30.0);
    TestEqual(TEXT("关闭抑制原完成回调"), *CallbackCount, 0);
    AddExpectedMessagePlain(TEXT("服务已关闭，忽略本次请求"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
    TestFalse(TEXT("关闭终态拒绝新导入"), Start().IsValid());
    TMMDMeshBuilder::ShutdownAsyncImports();
    TestEqual(TEXT("重复关闭无迟到回调"), *CallbackCount, 0);
    TMMDMeshBuilder::InitializeAsyncImports();
    Handle = Start();
    if (!TestTrue(TEXT("显式初始化恢复导入能力"), Handle.IsValid())) return false;
    Handle->Cancel();
    TMMDMeshBuilder::ShutdownAsyncImports();
    TestEqual(TEXT("重启后取消关闭仍抑制回调"), *CallbackCount, 0);
    TArray<FString> Files;
    IFileManager::Get().FindFilesRecursive(Files, *Directory, TEXT("*"), true, false);
    TestEqual(TEXT("关闭没有保存任何导入资产"), Files.Num(), 0);
    int32 PackageCount = 0;
    for (TObjectIterator<UPackage> It; It; ++It) PackageCount += It->GetName().StartsWith(Root) ? 1 : 0;
    TestEqual(TEXT("关闭没有创建任何导入包"), PackageCount, 0);
    return true;
}

#endif

#include "MMDImportSetting.h"
#include "MMDViewPanel.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "DesktopPlatformModule.h"
#include "IDesktopPlatform.h"
#include "Engine/Engine.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SBorder.h"
#include "HAL/PlatformFilemanager.h"
#include "Misc/Paths.h"

void MMDImportSetting::Construct(const FArguments &InArgs)
{
    // 保存ViewPanel引用
    ViewPanel = InArgs._ViewPanel;

    ChildSlot
        [SNew(SHorizontalBox) + SHorizontalBox::Slot().FillWidth(1.0f)[SNew(SVerticalBox) + SVerticalBox::Slot().AutoHeight().Padding(2.0f)[SNew(STextBlock).Text(FText::FromString(TEXT("设置区 - MMD模型导入"))).Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))] + SVerticalBox::Slot().AutoHeight().Padding(2.0f)[SAssignNew(StatusText, STextBlock).Text(FText::FromString(TEXT("准备就绪..."))).ColorAndOpacity(FSlateColor(FLinearColor::Green))]] + SHorizontalBox::Slot().AutoWidth()[SNew(SHorizontalBox) + SHorizontalBox::Slot().AutoWidth().Padding(5.0f, 2.0f)[SNew(SButton).Text(FText::FromString(TEXT("导入MMD模型"))).ToolTipText(FText::FromString(TEXT("导入.pmx/.pmd/.fbx等MMD模型文件"))).OnClicked(this, &MMDImportSetting::OnImportModelClicked)] + SHorizontalBox::Slot().AutoWidth().Padding(5.0f, 2.0f)[SNew(SButton).Text(FText::FromString(TEXT("导入静态网格"))).ToolTipText(FText::FromString(TEXT("导入.fbx/.obj等静态网格文件"))).OnClicked_Lambda([this]() -> FReply
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                      {
                    ImportStaticMesh();
                    return FReply::Handled(); })] +
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                 SHorizontalBox::Slot().AutoWidth().Padding(5.0f, 2.0f)[SNew(SButton).Text(FText::FromString(TEXT("选中"))).ToolTipText(FText::FromString(TEXT("切换到选择模式"))).OnClicked_Lambda([this]() -> FReply
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    {
                    if (ViewPanel.IsValid())
                    {
                        // 通知视口切换到选择模式
                        ShowImportProgress(TEXT("切换到选择模式"));
                    }
                    return FReply::Handled(); })] +
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                 SHorizontalBox::Slot().AutoWidth().Padding(5.0f, 2.0f)[SNew(SButton).Text(FText::FromString(TEXT("移动"))).ToolTipText(FText::FromString(TEXT("切换到移动模式"))).OnClicked_Lambda([this]() -> FReply
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    {
                    if (ViewPanel.IsValid())
                    {
                        // 通知视口切换到移动模式
                        ShowImportProgress(TEXT("切换到移动模式"));
                    }
                    return FReply::Handled(); })] +
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                 SHorizontalBox::Slot().AutoWidth().Padding(5.0f, 2.0f)[SNew(SButton).Text(FText::FromString(TEXT("缩放"))).ToolTipText(FText::FromString(TEXT("切换到缩放模式"))).OnClicked_Lambda([this]() -> FReply
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    {
                    if (ViewPanel.IsValid())
                    {
                        // 通知视口切换到缩放模式
                        ShowImportProgress(TEXT("切换到缩放模式"));
                    }
                    return FReply::Handled(); })]]];
}

FReply MMDImportSetting::OnImportModelClicked()
{
    ImportMMDModel();
    return FReply::Handled();
}

void MMDImportSetting::ImportMMDModel()
{
    ShowImportProgress(TEXT("打开文件选择对话框..."));

    // 使用文件对话框选择MMD模型文件
    IDesktopPlatform *DesktopPlatform = FDesktopPlatformModule::Get();
    if (DesktopPlatform)
    {
        TArray<FString> OpenedFiles;
        const FString FileTypes = TEXT("MMD Model Files (*.pmx;*.pmd;*.fbx)|*.pmx;*.pmd;*.fbx|PMX Files (*.pmx)|*.pmx|PMD Files (*.pmd)|*.pmd|FBX Files (*.fbx)|*.fbx|All Files (*.*)|*.*");

        bool bOpened = DesktopPlatform->OpenFileDialog(
            FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
            TEXT("导入MMD模型"),
            TEXT(""), // 默认路径
            TEXT(""), // 默认文件名
            FileTypes,
            EFileDialogFlags::None,
            OpenedFiles);

        if (bOpened && OpenedFiles.Num() > 0)
        {
            FString SelectedFile = OpenedFiles[0];
            FString FileName = FPaths::GetCleanFilename(SelectedFile);

            ShowImportProgress(FString::Printf(TEXT("已选择文件: %s"), *FileName));

            // 通过ViewPanel加载模型到场景
            if (ViewPanel.IsValid())
            {
                ViewPanel->LoadMMDModel(SelectedFile);
                ShowImportProgress(FString::Printf(TEXT("正在加载模型: %s"), *FileName));
            }
            else
            {
                ShowImportProgress(TEXT("错误: ViewPanel无效"));
            }
        }
        else
        {
            ShowImportProgress(TEXT("导入已取消"));
        }
    }
    else
    {
        ShowImportProgress(TEXT("无法打开文件对话框"));
    }
}

void MMDImportSetting::ImportStaticMesh()
{
    ShowImportProgress(TEXT("打开静态网格选择对话框..."));

    IDesktopPlatform *DesktopPlatform = FDesktopPlatformModule::Get();
    if (DesktopPlatform)
    {
        TArray<FString> OpenedFiles;
        const FString FileTypes = TEXT("Static Mesh Files (*.fbx;*.obj;*.3ds)|*.fbx;*.obj;*.3ds|FBX Files (*.fbx)|*.fbx|OBJ Files (*.obj)|*.obj|All Files (*.*)|*.*");

        bool bOpened = DesktopPlatform->OpenFileDialog(
            FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
            TEXT("导入静态网格"),
            TEXT(""),
            TEXT(""),
            FileTypes,
            EFileDialogFlags::None,
            OpenedFiles);

        if (bOpened && OpenedFiles.Num() > 0)
        {
            FString SelectedFile = OpenedFiles[0];
            FString FileName = FPaths::GetCleanFilename(SelectedFile);

            ShowImportProgress(FString::Printf(TEXT("已选择静态网格: %s"), *FileName));

            if (GEngine)
            {
                GEngine->AddOnScreenDebugMessage(
                    -1,
                    10.0f,
                    FColor::Blue,
                    FString::Printf(TEXT("静态网格导入: %s"), *SelectedFile));
            }
        }
        else
        {
            ShowImportProgress(TEXT("导入已取消"));
        }
    }
}

void MMDImportSetting::ShowImportProgress(const FString &Message)
{
    if (StatusText.IsValid())
    {
        StatusText->SetText(FText::FromString(Message));
    }
}

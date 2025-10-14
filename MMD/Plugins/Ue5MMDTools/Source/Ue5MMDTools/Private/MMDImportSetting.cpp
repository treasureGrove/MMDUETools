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
#include "TMMDMeshBuilder.h"
#include "TPMXParser.h"
#include "TVMDParser.h"
#if WITH_EDITOR
#include "Factories/BlueprintFactory.h"
#include "Editor.h"
#include "AMMDActor.h"
#include "Modules/ModuleManager.h" 
#include "Kismet2/KismetEditorUtilities.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"
#endif // WITH_EDITOR

// Define helper now (was accidentally removed)
static UBlueprint* SaveMMDBlueprintAsset(AActor* TargetActor, const FString& FolderPath, const FString& AssetName, bool bReplaceInLevel)
{
	if (!TargetActor)
	{
		UE_LOG(LogTemp, Warning, TEXT("SaveMMDBlueprintAsset: TargetActor is null."));
		return nullptr;
	}
#if WITH_EDITOR
	// 1) 生成唯一包名和资源名
	FString UniquePackageName, UniqueAssetName;
	{
		FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
		const FString TargetLongPackageName = FolderPath / AssetName; // 例如 /Game/MMDModels/Foo
		AssetToolsModule.Get().CreateUniqueAssetName(TargetLongPackageName, TEXT(""), UniquePackageName, UniqueAssetName);
	}

	// 2) 创建包与蓝图（使用 CreateBlueprint，规避 CreateBlueprintFromActor 的重载差异）
	UPackage* Package = CreatePackage(*UniquePackageName);
	if (!Package)
	{
		UE_LOG(LogTemp, Error, TEXT("CreatePackage failed: %s"), *UniquePackageName);
		return nullptr;
	}

	UBlueprint* NewBP = FKismetEditorUtilities::CreateBlueprint(
		TargetActor->GetClass(),      // 父类：与实例相同
		Package,                      // 外部：包
		*UniqueAssetName,             // 资源名
		BPTYPE_Normal,                // 蓝图类型
		UBlueprint::StaticClass(),
		UBlueprintGeneratedClass::StaticClass()
	);
	if (!NewBP)
	{
		UE_LOG(LogTemp, Error, TEXT("CreateBlueprint failed for: %s/%s"), *UniquePackageName, *UniqueAssetName);
		return nullptr;
	}

	// 3) 编译，确保 GeneratedClass/CDO 可用
	FKismetEditorUtilities::CompileBlueprint(NewBP);

	// 4) 将实例属性复制到蓝图 CDO（参数顺序：Old=实例，New=CDO）
	if (UClass* GenClass = NewBP->GeneratedClass)
	{
		if (UObject* BPCDO = GenClass->GetDefaultObject(/*bCreateIfNeeded*/true))
		{
			UEngine::FCopyPropertiesForUnrelatedObjectsParams Params; // 按现有 API，移除不存在的字段设置
			UEngine::CopyPropertiesForUnrelatedObjects(/*OldObject=*/TargetActor, /*NewObject=*/BPCDO, Params);
		}
	}

	// 5) 标记和注册
	NewBP->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(NewBP);

	// 6) 保存（使用 FSavePackageArgs 新 API）
	const EObjectFlags TopLevelFlags = RF_Public | RF_Standalone;
	const FString FilePath = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = TopLevelFlags;
	SaveArgs.SaveFlags = SAVE_None;               // 视需要设置 SAVE_NoError 等
	SaveArgs.Error = GError;                      // 错误输出
	SaveArgs.bWarnOfLongFilename = false;

	if (!UPackage::SavePackage(Package, /*InBase*/nullptr, *FilePath, SaveArgs))
	{
		UE_LOG(LogTemp, Warning, TEXT("SavePackage failed: %s"), *FilePath);
	}

	// 7) 关注蓝图
	FKismetEditorUtilities::BringKismetToFocusAttentionOnObject(NewBP);
	return NewBP;
#else
	UE_LOG(LogTemp, Error, TEXT("SaveActorInstanceAsBlueprint: editor-only function"));
	return nullptr;
#endif
}

static UAnimationAsset* SaveMMDAnimationAsset(UAnimSequence* AnimSeq, const FString& FolderPath, const FString& AssetName)
{
	if (!AnimSeq)
	{
		UE_LOG(LogTemp, Warning, TEXT("SaveMMDAnimationAsset: AnimSeq is null."));
		return nullptr;
	}
}


TWeakPtr<MMDImportSetting> MMDImportSetting::CurrentInstance = nullptr; // 静态成员初始化

void MMDImportSetting::RegisterInstance(const TSharedRef<MMDImportSetting>& InstanceRef)
{
	CurrentInstance = InstanceRef;
}

void MMDImportSetting::Construct(const FArguments& InArgs)
{
	// 保存ViewPanel引用
	ViewPanel = InArgs._ViewPanel;
	// 不要在 Construct 里调用 SharedThis(this)，避免 TSharedFromThis 断言
	ChildSlot
		[SNew(SHorizontalBox) + SHorizontalBox::Slot().FillWidth(1.0f)[SNew(SVerticalBox) + SVerticalBox::Slot().AutoHeight().Padding(2.0f)[SNew(STextBlock).Text(FText::FromString(TEXT("设置区 - MMD模型导入"))).Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))] + SVerticalBox::Slot().AutoHeight().Padding(2.0f)[SAssignNew(StatusText, STextBlock).Text(FText::FromString(TEXT("准备就绪..."))).ColorAndOpacity(FSlateColor(FLinearColor::Green))]] + SHorizontalBox::Slot().AutoWidth()[SNew(SHorizontalBox) + SHorizontalBox::Slot().AutoWidth().Padding(5.0f, 2.0f)[SNew(SButton).Text(FText::FromString(TEXT("导入MMD模型"))).ToolTipText(FText::FromString(TEXT("导入.pmx/.pmd/.fbx等MMD模型文件"))).OnClicked(this, &MMDImportSetting::OnImportModelClicked)] + SHorizontalBox::Slot()  // 改为 SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(5.0f, 2.0f)
		[
			SNew(SButton)
				.Text(FText::FromString(TEXT("导入VMD动画")))
				.OnClicked(this, &MMDImportSetting::OnImportVMDClicked)
		] + SHorizontalBox::Slot().AutoWidth().Padding(5.0f, 2.0f)[SNew(SButton).Text(FText::FromString(TEXT("导入静态网格"))).ToolTipText(FText::FromString(TEXT("导入.fbx/.obj等静态网格文件"))).OnClicked_Lambda([this]() -> FReply
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
FReply MMDImportSetting::OnImportVMDClicked()
{
	ImportVMDAnimation();
	return FReply::Handled();
}
void MMDImportSetting::ImportMMDModel()
{
	ShowImportProgress(TEXT("打开文件选择对话框..."));

	// 使用文件对话框选择MMD模型文件
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
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

			if (ViewPanel.IsValid())
			{
				ViewPanel->LoadMMDModel(SelectedFile);
				ShowImportProgress(FString::Printf(TEXT("正在加载模型: %s"), *FileName));

				if (SelectedFile.EndsWith(TEXT(".pmx")))
				{
					ShowImportProgress(TEXT("开始解析PMX文件..."));
					UE_LOG(LogTemp, Warning, TEXT("开始解析PMX文件: %s"), *SelectedFile);
#if WITH_EDITOR
					UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
					if (!EditorWorld) {
						ShowImportProgress(TEXT("未找到编辑器世界，无法生成Actor"), EMMDMessageType::Error);
						return;
					}
					FActorSpawnParameters SpawnParams;
					SpawnParams.Name = MakeUniqueObjectName(EditorWorld, AMMDActor::StaticClass(), FName(TEXT("MMDActor")));
					AMMDActor* NewMMDActor = EditorWorld->SpawnActor<AMMDActor>(AMMDActor::StaticClass(), FTransform::Identity, SpawnParams);
					if (!NewMMDActor)
					{
						ShowImportProgress(TEXT("生成AMMDActor失败"), EMMDMessageType::Error);
						return;
					}
					NewMMDActor->SetupComponents(SelectedFile);
					// 保存为蓝图资产
					FString AssetFolder = TEXT("/Game/MMDModels");
					FString AssetName = FPaths::GetBaseFilename(FileName);
					if (UBlueprint* NewBP = SaveMMDBlueprintAsset(NewMMDActor, AssetFolder + TEXT("/") + AssetName + TEXT("/BluePrint"), AssetName, true))
					{
						if (NewBP->GeneratedClass)
						{
							// 关键：这里调用预览函数在插件预览窗口生成 Actor
							ViewPanel->CreatePreviewActor(NewBP->GeneratedClass);
						}
					}

					GEditor->SelectNone(false, true);
					GEditor->SelectActor(NewMMDActor, true, true);
					GEditor->MoveViewportCamerasToActor(*NewMMDActor, false);


					ShowImportProgress(TEXT("已在关卡中生成AMMDActor并加载PMX"), EMMDMessageType::Success);

#else
					ShowImportProgress(TEXT("仅在编辑器中可生成Actor"), EMMDMessageType::Warning);
#endif
				}
				else
				{
					ShowImportProgress(FString::Printf(TEXT("文件类型: %s (非PMX)"), *FPaths::GetExtension(SelectedFile)));
				}
			}
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

	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
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
void MMDImportSetting::ImportVMDAnimation()
{
	ShowImportProgress(TEXT("打开VMD动画选择对话框..."));
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (DesktopPlatform)
	{
		TArray<FString> OpenedFiles;
		const FString FileTypes = TEXT("VMD Files (*.vmd)|*.vmd|All Files (*.*)|*.*");

		bool bOpened = DesktopPlatform->OpenFileDialog(
			FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
			TEXT("导入VMD动画"),
			TEXT(""),
			TEXT(""),
			FileTypes,
			EFileDialogFlags::None,
			OpenedFiles);

		if (bOpened && OpenedFiles.Num() > 0)
		{
			FString SelectedFile = OpenedFiles[0];
			FString FileName = FPaths::GetCleanFilename(SelectedFile);

			ShowImportProgress(FString::Printf(TEXT("已选择VMD文件: %s"), *FileName));

			// 调用你的 VMD 解析器
			TVMDParser VMDParser;
			if (VMDParser.ParseVMDFile(SelectedFile))
			{
				const VMDData& VMDInfo = VMDParser.VMDInfo;
				ShowImportProgress(FString::Printf(TEXT("VMD解析成功: %s, 骨骼帧:%d, 表情帧:%d"),
					*VMDInfo.ModelName, VMDInfo.BoneFrames.Num(), VMDInfo.MorphFrames.Num()),
					EMMDMessageType::Success);

				// 这里可以将VMD数据传递给你的视口面板或动画系统
				// ViewPanel->LoadVMDAnimation(VMDInfo);
			}
			else
			{
				ShowImportProgress(TEXT("VMD文件解析失败"), EMMDMessageType::Error);
			}
		}
		else
		{
			ShowImportProgress(TEXT("导入已取消"));
		}
	}
}
void MMDImportSetting::ShowImportProgress(const FString& Message, EMMDMessageType Type)
{
	if (StatusText.IsValid())
	{
		switch (Type)
		{
		case EMMDMessageType::Info:
			StatusText->SetColorAndOpacity(FSlateColor(FLinearColor::Green));
			StatusText->SetText(FText::FromString(Message));
			break;

		case EMMDMessageType::Warning:
			StatusText->SetColorAndOpacity(FSlateColor(FLinearColor::Yellow));
			StatusText->SetText(FText::FromString(Message));
			break;

		case EMMDMessageType::Error:
			StatusText->SetColorAndOpacity(FSlateColor(FLinearColor::Red));
			StatusText->SetText(FText::FromString(Message));
			break;

		case EMMDMessageType::Success:
			StatusText->SetColorAndOpacity(FSlateColor(FLinearColor::Green));
			StatusText->SetText(FText::FromString(Message));
			break;
		default:
			StatusText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
			StatusText->SetText(FText::FromString("No Type: " + Message));
			break;
		}
	}
}
void MMDImportSetting::ShowGlobalImportProgress(const FString& Message, EMMDMessageType Type)
{
	TSharedPtr<MMDImportSetting> Instance = CurrentInstance.Pin();

	if (Instance.IsValid())
	{
		// 如果实例存在，调用实例方法
		Instance->ShowImportProgress(Message, Type);
	}
	else
	{
		// 如果实例不存在，至少输出到日志
		switch (Type)
		{
		case EMMDMessageType::Info:
			UE_LOG(LogTemp, Log, TEXT("[MMD导入] %s"), *Message);
			break;
		case EMMDMessageType::Warning:
			UE_LOG(LogTemp, Warning, TEXT("[MMD导入] %s"), *Message);
			break;
		case EMMDMessageType::Error:
			UE_LOG(LogTemp, Error, TEXT("[MMD导入] %s"), *Message);
			break;
		case EMMDMessageType::Success:
			UE_LOG(LogTemp, Warning, TEXT("[MMD导入成功] %s"), *Message);
			break;
		}
	}
}


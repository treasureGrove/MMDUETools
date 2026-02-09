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
#include "Animation/AnimSequence.h"
#include "Animation/AnimData/IAnimationDataController.h"
#include "Misc/FrameRate.h"
#if WITH_EDITOR
#include "Factories/BlueprintFactory.h"
#include "Editor.h"
#include "Engine/Selection.h"
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
	(void)FolderPath;
	(void)AssetName;
	if (!AnimSeq->GetOutermost())
	{
		UE_LOG(LogTemp, Warning, TEXT("SaveMMDAnimationAsset: AnimSeq has no outer package."));
		return nullptr;
	}
#if WITH_EDITOR
	AnimSeq->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(AnimSeq);

	const FString FilePath = FPackageName::LongPackageNameToFilename(
		AnimSeq->GetOutermost()->GetName(),
		FPackageName::GetAssetPackageExtension());

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_None;
	SaveArgs.Error = GError;
	SaveArgs.bWarnOfLongFilename = false;

	if (!UPackage::SavePackage(AnimSeq->GetOutermost(), AnimSeq, *FilePath, SaveArgs))
	{
		UE_LOG(LogTemp, Warning, TEXT("SaveMMDAnimationAsset: SavePackage failed: %s"), *FilePath);
	}
#endif
	return AnimSeq;
}

static FString FixAnimAssetName(const FString& InName)
{
	FString Name = InName;
	Name = Name.Replace(TEXT(" "), TEXT("_"))
		.Replace(TEXT("."), TEXT("_"))
		.Replace(TEXT("-"), TEXT("_"))
		.Replace(TEXT("("), TEXT("_"))
		.Replace(TEXT(")"), TEXT("_"))
		.Replace(TEXT("["), TEXT("_"))
		.Replace(TEXT("]"), TEXT("_"))
		.Replace(TEXT("<"), TEXT("_"))
		.Replace(TEXT(">"), TEXT("_"))
		.Replace(TEXT(":"), TEXT("_"))
		.Replace(TEXT("*"), TEXT("_"))
		.Replace(TEXT("?"), TEXT("_"))
		.Replace(TEXT("\""), TEXT("_"))
		.Replace(TEXT("|"), TEXT("_"))
		.Replace(TEXT(","), TEXT("_"))
		.Replace(TEXT("&"), TEXT("_"))
		.Replace(TEXT("!"), TEXT("_"))
		.Replace(TEXT("~"), TEXT("_"))
		.Replace(TEXT("@"), TEXT("_"))
		.Replace(TEXT("#"), TEXT("_"))
		.Replace(TEXT("'"), TEXT("_"));
	while (Name.Contains(TEXT("__")))
	{
		Name = Name.Replace(TEXT("__"), TEXT("_"));
	}
	if (Name.IsEmpty())
	{
		Name = TEXT("MMD_Anim");
	}
	return Name;
}

static FVector ConvertMMDPositionToUnreal(const FVector& InPos, float Scale)
{
	const FVector Scaled(InPos.X * Scale, InPos.Y * Scale, InPos.Z * Scale);
	return FVector(Scaled.X, -Scaled.Z, Scaled.Y);
}

static FQuat ConvertMMDQuatToUnreal(const FQuat& InQuat)
{
	static const FQuat BasisQuat = FQuat(FMatrix(
		FPlane(1, 0, 0, 0),
		FPlane(0, 0, 1, 0),
		FPlane(0, -1, 0, 0),
		FPlane(0, 0, 0, 1)));
	const FQuat Result = BasisQuat * InQuat * BasisQuat.Inverse();
	return Result.GetNormalized();
}

#if WITH_EDITOR
static USkeletalMeshComponent* FindSelectedSkeletalMeshComponent()
{
	if (!GEditor)
	{
		return nullptr;
	}
	USelection* SelectedActors = GEditor->GetSelectedActors();
	if (!SelectedActors || SelectedActors->Num() == 0)
	{
		return nullptr;
	}
	for (FSelectionIterator Iter(*SelectedActors); Iter; ++Iter)
	{
		if (AActor* Actor = Cast<AActor>(*Iter))
		{
			if (USkeletalMeshComponent* SkelComp = Actor->FindComponentByClass<USkeletalMeshComponent>())
			{
				return SkelComp;
			}
		}
	}
	return nullptr;
}

static UAnimSequence* BuildAnimSequenceFromVMD(const VMDData& VMDInfo, USkeletalMesh* SkeletalMesh, const FString& VMDFilePath)
{
	if (!SkeletalMesh)
	{
		UE_LOG(LogTemp, Warning, TEXT("BuildAnimSequenceFromVMD: SkeletalMesh is null."));
		return nullptr;
	}
	USkeleton* Skeleton = SkeletalMesh->GetSkeleton();
	if (!Skeleton)
	{
		UE_LOG(LogTemp, Warning, TEXT("BuildAnimSequenceFromVMD: Skeleton is null."));
		return nullptr;
	}

	const FReferenceSkeleton& RefSkeleton = Skeleton->GetReferenceSkeleton();
	const TArray<FTransform>& RefPose = RefSkeleton.GetRefBonePose();

	struct FVMDBoneKey
	{
		int32 Frame = 0;
		FVector Position = FVector::ZeroVector;
		FQuat Rotation = FQuat::Identity;
	};

	TMap<FName, TArray<FVMDBoneKey>> TrackMap;
	int32 MaxFrame = 0;
	const float PositionScale = 8.0f;

	for (const VMDBoneKeyframe& KeyFrame : VMDInfo.BoneFrames)
	{
		const FName BoneName(*KeyFrame.BoneName);
		if (RefSkeleton.FindBoneIndex(BoneName) == INDEX_NONE)
		{
			continue;
		}
		FVMDBoneKey NewKey;
		NewKey.Frame = static_cast<int32>(KeyFrame.FrameNumber);
		NewKey.Position = ConvertMMDPositionToUnreal(KeyFrame.Position, PositionScale);
		NewKey.Rotation = ConvertMMDQuatToUnreal(KeyFrame.Rotation);
		TrackMap.FindOrAdd(BoneName).Add(NewKey);
		MaxFrame = FMath::Max(MaxFrame, NewKey.Frame);
	}

	if (TrackMap.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("BuildAnimSequenceFromVMD: No matching bone tracks found."));
		return nullptr;
	}

	const int32 NumFrames = MaxFrame + 1;
	if (NumFrames <= 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("BuildAnimSequenceFromVMD: Invalid frame count."));
		return nullptr;
	}

	const FString MeshPackagePath = FPackageName::GetLongPackagePath(SkeletalMesh->GetOutermost()->GetName());
	const FString AnimFolder = MeshPackagePath / TEXT("Animation");
	const FString BaseName = FixAnimAssetName(FPaths::GetBaseFilename(VMDFilePath));

	FString UniquePackageName;
	FString UniqueAssetName;
	{
		FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
		AssetToolsModule.Get().CreateUniqueAssetName(AnimFolder / BaseName, TEXT(""), UniquePackageName, UniqueAssetName);
	}

	UPackage* Package = CreatePackage(*UniquePackageName);
	if (!Package)
	{
		UE_LOG(LogTemp, Warning, TEXT("BuildAnimSequenceFromVMD: CreatePackage failed: %s"), *UniquePackageName);
		return nullptr;
	}

	UAnimSequence* AnimSeq = NewObject<UAnimSequence>(Package, *UniqueAssetName, RF_Public | RF_Standalone);
	if (!AnimSeq)
	{
		UE_LOG(LogTemp, Warning, TEXT("BuildAnimSequenceFromVMD: Failed to create AnimSequence."));
		return nullptr;
	}

	AnimSeq->SetSkeleton(Skeleton);
	AnimSeq->SetPreviewMesh(SkeletalMesh);

	IAnimationDataController& Controller = AnimSeq->GetController();
	Controller.OpenBracket(FText::FromString(TEXT("Import VMD Animation")));
	Controller.InitializeModel();
	Controller.SetFrameRate(FFrameRate(30, 1), true);
	Controller.SetNumberOfFrames(NumFrames, true);

	for (auto& Pair : TrackMap)
	{
		const FName BoneName = Pair.Key;
		TArray<FVMDBoneKey>& Keys = Pair.Value;
		Keys.Sort([](const FVMDBoneKey& A, const FVMDBoneKey& B)
			{
				return A.Frame < B.Frame;
			});

		const int32 BoneIndex = RefSkeleton.FindBoneIndex(BoneName);
		const FTransform DefaultTransform = RefPose.IsValidIndex(BoneIndex) ? RefPose[BoneIndex] : FTransform::Identity;
		FVector CurrentPos = DefaultTransform.GetTranslation();
		FQuat CurrentRot = DefaultTransform.GetRotation();
		FVector CurrentScale = DefaultTransform.GetScale3D();

		TArray<FVector3f> PosKeys;
		TArray<FQuat4f> RotKeys;
		TArray<FVector3f> ScaleKeys;
		PosKeys.SetNum(NumFrames);
		RotKeys.SetNum(NumFrames);
		ScaleKeys.SetNum(NumFrames);

		int32 KeyIndex = 0;
		for (int32 FrameIndex = 0; FrameIndex < NumFrames; ++FrameIndex)
		{
			while (KeyIndex < Keys.Num() && Keys[KeyIndex].Frame == FrameIndex)
			{
				CurrentPos = Keys[KeyIndex].Position;
				CurrentRot = Keys[KeyIndex].Rotation;
				++KeyIndex;
			}
			PosKeys[FrameIndex] = FVector3f(CurrentPos);
			RotKeys[FrameIndex] = FQuat4f(CurrentRot);
			ScaleKeys[FrameIndex] = FVector3f(CurrentScale);
		}

		Controller.AddBoneTrack(BoneName);
		Controller.SetBoneTrackKeys(BoneName, PosKeys, RotKeys, ScaleKeys, false);
	}

	Controller.CloseBracket();
	SaveMMDAnimationAsset(AnimSeq, AnimFolder, UniqueAssetName);
	return AnimSeq;
}
#endif


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
					SpawnParams.NameMode = FActorSpawnParameters::ESpawnActorNameMode::Requested;
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
						if (UClass* GenClass = NewBP->GeneratedClass)
						{
							// 关键：把 PMX 源路径设置到蓝图 CDO，保证蓝图实例 OnConstruction 能拿到
							if (AActor* CDOActor = Cast<AActor>(GenClass->GetDefaultObject()))
							{
								if (AMMDActor* CDO = Cast<AMMDActor>(CDOActor))
								{
									CDO->SourcePMXFilePath = SelectedFile; // 绝对路径
									CDO->Modify(true);
									UE_LOG(LogTemp, Log, TEXT("[Import] Set CDO SourcePMXFilePath: %s"), *SelectedFile);
								}
							}

							// 重新编译，使默认值生效
							FKismetEditorUtilities::CompileBlueprint(NewBP);

							// 预览中生成实例（其 OnConstruction 将基于 CDO 的路径自动初始化）
							ViewPanel->CreatePreviewActor(GenClass);
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

#if WITH_EDITOR
				USkeletalMeshComponent* TargetComp = FindSelectedSkeletalMeshComponent();
				if (!TargetComp)
				{
					ShowImportProgress(TEXT("未找到选中的SkeletalMesh组件，无法生成动画"), EMMDMessageType::Error);
					return;
				}

				USkeletalMesh* TargetMesh = TargetComp->GetSkeletalMeshAsset();
				if (!TargetMesh || !TargetMesh->GetSkeleton())
				{
					ShowImportProgress(TEXT("选中的SkeletalMesh无Skeleton，无法生成动画"), EMMDMessageType::Error);
					return;
				}

				if (UAnimSequence* AnimSeq = BuildAnimSequenceFromVMD(VMDInfo, TargetMesh, SelectedFile))
				{
					ShowImportProgress(FString::Printf(TEXT("VMD动画已生成: %s"), *AnimSeq->GetName()),
						EMMDMessageType::Success);
				}
				else
				{
					ShowImportProgress(TEXT("VMD解析成功，但动画生成失败"), EMMDMessageType::Error);
				}
#endif
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


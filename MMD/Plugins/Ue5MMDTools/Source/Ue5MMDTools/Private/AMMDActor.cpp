#include "AMMDActor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SceneComponent.h"
#include "TMMDMeshBuilder.h"
#include "MMDImportSetting.h"

#if WITH_EDITOR
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimBlueprintGeneratedClass.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "AGN_MMDSkeletalControl.h"

#include "Kismet2/BlueprintEditorUtils.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"
#endif
AMMDActor::AMMDActor()
{
	PrimaryActorTick.bCanEverTick = true;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("RootComponent"));
	SkeletalMeshComponent = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("MMD_SkeletalMesh"));
	SkeletalMeshComponent->SetupAttachment(RootComponent);
	SkeletalMeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	SkeletalMeshComponent->SetCollisionResponseToAllChannels(ECR_Ignore);
	SkeletalMeshComponent->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	SkeletalMeshComponent->SetAnimationMode(EAnimationMode::AnimationBlueprint);
}

void AMMDActor::SetupComponents(const FString& FilePath)
{
	static TUniquePtr<TPMXParser> StaticParser = MakeUnique<TPMXParser>();
	bool bSuccess = StaticParser->ParsePMXFile(FilePath);
	if (bSuccess) {
		const PMXDatas& PMXData = StaticParser->PMXInfo;

		MMDImportSetting::ShowGlobalImportProgress(FString::Printf(TEXT("Successfully loaded PMX file: %s"), *FilePath), EMMDMessageType::Success);
		TMMDMeshBuilder meshbuilder;
		USkeletalMesh* BuiltMesh = meshbuilder.BuildSkeletalMeshFromPMX(PMXData, FString("/Game/MMDModels"), PMXData.ModelNameEN, FilePath);
#pragma region SetupBlueprint
		if (!SkeletalMeshComponent)
		{
			SkeletalMeshComponent = NewObject<USkeletalMeshComponent>(this, TEXT("MMD_SkeletalMesh_RT"));
			SkeletalMeshComponent->SetupAttachment(RootComponent);
			SkeletalMeshComponent->RegisterComponent();
			SkeletalMeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			SkeletalMeshComponent->SetCollisionResponseToAllChannels(ECR_Ignore);
			SkeletalMeshComponent->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
			SkeletalMeshComponent->SetAnimationMode(EAnimationMode::AnimationBlueprint);
		}
		SkeletalMeshComponent->SetSkeletalMesh(BuiltMesh);
#pragma endregion

#pragma region SetupIKRig
	//使用mmd默认的骨骼名创建

#pragma endregion

#pragma region SetupPhysicsAsset

#pragma endregion

#pragma region SetupAnimationBlueprint
		MMDImportSetting::ShowGlobalImportProgress(TEXT("正在生成MMD AnimationBlueprint..."), EMMDMessageType::Info);
		FString PMXFileName = FPaths::GetBaseFilename(FilePath); // 从文件路径提取文件名
		FString AnimBPName = PMXFileName + TEXT("_AnimBP");
		UAnimBlueprint* GeneratedAnimBP = GenerateMMDAnimationBlueprint(FilePath, AnimBPName);

		if (GeneratedAnimBP) {
			//自动应用于SkeletalMeshComponent
			SkeletalMeshComponent->SetAnimClass(GeneratedAnimBP->GeneratedClass);
			MMDImportSetting::ShowGlobalImportProgress(FString::Printf(TEXT("成功生成并应用AnimationBlueprint: %s"), *AnimBPName), EMMDMessageType::Success);
		}
		else {
			MMDImportSetting::ShowGlobalImportProgress(TEXT("AnimationBlueprint生成失败"), EMMDMessageType::Warning);
		}
#pragma endregion



	}
	else {
		MMDImportSetting::ShowGlobalImportProgress(FString::Printf(TEXT("MMD文件解析不成功")), EMMDMessageType::Error);
		return;
	}
}
UAnimBlueprint* AMMDActor::GenerateMMDAnimationBlueprint(const FString& FilePath, const FString& AssetName)
{
#if WITH_EDITOR
	if (!SkeletalMeshComponent || !SkeletalMeshComponent->GetSkeletalMeshAsset())
	{
		UE_LOG(LogTemp, Warning, TEXT("GenerateMMDAnimationBlueprint: No valid SkeletalMesh found"));
		return nullptr;
	}

	USkeletalMesh* TargetMesh = SkeletalMeshComponent->GetSkeletalMeshAsset();
	FString PMXFileName = FPaths::GetBaseFilename(FilePath); // 从文件路径提取文件名
	FString PackagePath = FString("/Game/MMDModels/") + PMXFileName + TEXT("/Animation");

	return CreateAnimBlueprintWithPhysics(TargetMesh, PackagePath, AssetName);
#else
	UE_LOG(LogTemp, Error, TEXT("GenerateMMDAnimationBlueprint: editor-only function"));
	return nullptr;
#endif
}
void AMMDActor::BeginPlay()
{
	Super::BeginPlay();
}
void AMMDActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
}
void AMMDActor::Cleanup()
{
	if (SkeletalMeshComponent) {
		SkeletalMeshComponent->SetSkeletalMesh(nullptr);
	}
	LoadedPMX = PMXDatas();
	LoadedPMXPath.Reset();
}
UAnimBlueprint* AMMDActor::CreateAnimBlueprintWithPhysics(USkeletalMesh* TargetMesh, const FString& PackagePath, const FString& AssetName)
{
#if WITH_EDITOR
	if (!TargetMesh) {
		UE_LOG(LogTemp, Warning, TEXT("CreateAnimBlueprintWithPhysics: TargetMesh is null."));
		return nullptr;
	}

	//生成唯一包名和资源名
	FString UniquePackageName, UniqueAssetName;
	{
		FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
		AssetToolsModule.Get().CreateUniqueAssetName(PackagePath + TEXT("/") + AssetName, TEXT(""), UniquePackageName, UniqueAssetName);
	}

	//创建包与蓝图
	UPackage* Package = CreatePackage(*UniquePackageName);
	if (!Package) {
		UE_LOG(LogTemp, Error, TEXT("CreatePackage failed: %s"), *UniquePackageName);
		return nullptr;
	}

	//创建AnimBlueprint - 注意类型转换
	UBlueprint* CreatedBlueprint = FKismetEditorUtilities::CreateBlueprint(
		UAnimInstance::StaticClass(),           // 父类
		Package,                               // 包
		*UniqueAssetName,                      // 资源名
		BPTYPE_Normal,                         // 蓝图类型
		UAnimBlueprint::StaticClass(),         // 蓝图类
		UAnimBlueprintGeneratedClass::StaticClass() // 生成的类
	);

	// ✅ 进行类型转换
	UAnimBlueprint* NewAnimBP = Cast<UAnimBlueprint>(CreatedBlueprint);
	if (!NewAnimBP) {
		UE_LOG(LogTemp, Error, TEXT("CreateAnimBlueprintWithPhysics: Failed to cast to UAnimBlueprint"));
		return nullptr;
	}

	// 设置目标骨骼
	NewAnimBP->TargetSkeleton = TargetMesh->GetSkeleton();
	if (!NewAnimBP->TargetSkeleton) {
		UE_LOG(LogTemp, Warning, TEXT("CreateAnimBlueprintWithPhysics: TargetMesh has no skeleton"));
	}

	// 保存PMX数据供后续使用
	LoadedPMX = LoadedPMX; // 确保LoadedPMX已在SetupComponents中设置
	LoadedPMXPath = LoadedPMXPath;

	// 编译蓝图
	FKismetEditorUtilities::CompileBlueprint(NewAnimBP);

	// 标记和注册
	NewAnimBP->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(NewAnimBP);

	// 保存到磁盘
	const FString FilePath = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_None;
	SaveArgs.Error = GError;
	SaveArgs.bWarnOfLongFilename = false;

	if (UPackage::SavePackage(Package, nullptr, *FilePath, SaveArgs)) {
		UE_LOG(LogTemp, Log, TEXT("Successfully created and saved AnimBlueprint: %s"), *FilePath);
	}
	else {
		UE_LOG(LogTemp, Warning, TEXT("Failed to save AnimBlueprint: %s"), *FilePath);
	}

	return NewAnimBP;
#else
	UE_LOG(LogTemp, Error, TEXT("CreateAnimBlueprintWithPhysics: editor-only function"));
	return nullptr;
#endif
}
void AMMDActor::BuildFromPMXData(const PMXDatas& PMXInfo, const FString& PMXFilePath)
{
	Cleanup();
	LoadedPMX = PMXInfo;
	LoadedPMXPath = PMXFilePath;

	USkeletalMesh* NewMesh = TMMDMeshBuilder::BuildSkeletalMeshFromPMX(
		PMXInfo,
		TEXT("/Game/MMDTools/Runtime"),
		PMXInfo.ModelNameEN + TEXT("_Runtime"),
		PMXFilePath);
	if(!NewMesh){
		UE_LOG(LogTemp, Warning, TEXT("Failed to build skeletal mesh from PMX data."));
		return;
	}
	SkeletalMeshComponent->SetSkeletalMesh(NewMesh);
	UE_LOG(LogTemp, Log, TEXT("Successfully built skeletal mesh from PMX data."));
}





#include "AMMDActor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SceneComponent.h"
#include "TMMDMeshBuilder.h"
#include "MMDImportSetting.h"

#include "Animation/AnimBlueprint.h"
#include "AGN_MMDSkeletalControl.h"

AMMDActor::AMMDActor()
{
	PrimaryActorTick.bCanEverTick = true;

	RootSceneComponent = CreateDefaultSubobject<USceneComponent>(TEXT("RootComponent"));
	RootComponent = RootSceneComponent;

	CapsuleComponent = CreateDefaultSubobject<UCapsuleComponent>(TEXT("MMD_Capsule"));
	CapsuleComponent->SetupAttachment(RootComponent);

	CapsuleComponent->InitCapsuleSize(20.0f, 80.0f);
	CapsuleComponent->SetRelativeLocation(FVector(0.0f, 0.0f, 80.0f));

	CapsuleComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	CapsuleComponent->SetCollisionProfileName(TEXT("Pawn"));
	CapsuleComponent->SetCollisionResponseToAllChannels(ECR_Block);
	CapsuleComponent->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);

	SkeletalMeshComponent = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("MMD_SkeletalMesh"));
	SkeletalMeshComponent->SetupAttachment(CapsuleComponent);
	SkeletalMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SkeletalMeshComponent->SetCollisionResponseToAllChannels(ECR_Ignore);

	SkeletalMeshComponent->SetAnimationMode(EAnimationMode::AnimationBlueprint);
	SkeletalMeshComponent->SetRelativeLocation(FVector(0.0f, 0.0f, -80.0f));
}

void AMMDActor::SetupComponents(const FString& FilePath)
{
	FString PMXFileName = FPaths::GetBaseFilename(FilePath);
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
			SkeletalMeshComponent->SetupAttachment(CapsuleComponent);  // ✅ 附加到胶囊体
			SkeletalMeshComponent->RegisterComponent();

			// ✅ 设置位置（脚底对齐原点）
			SkeletalMeshComponent->SetRelativeLocation(FVector(0.0f, 0.0f, -80.0f));

			SkeletalMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			SkeletalMeshComponent->SetCollisionResponseToAllChannels(ECR_Ignore);
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
		UAnimBlueprint* MMDAnimBP = meshbuilder.BuildAnimBlueprint(BuiltMesh, FilePath);
		UAnimGraphNode_MMDSkeletalControl* MMDNode= FMMDAnimGraphHelper::AddMMDNodeToAnimBP(MMDAnimBP, true);
		if (MMDNode)
		{
			MMDNode->Node.bEnablePhysics = true;
			UE_LOG(LogTemp, Log, TEXT("MMD node added successfully!"));
		}
		SkeletalMeshComponent->SetAnimInstanceClass(MMDAnimBP->GeneratedClass);
#pragma endregion



	}
	else {
		MMDImportSetting::ShowGlobalImportProgress(FString::Printf(TEXT("MMD文件解析不成功")), EMMDMessageType::Error);
		return;
	}
}
void AMMDActor::BeginPlay()
{
	Super::BeginPlay();
}
void AMMDActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
}



#include "AMMDActor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SceneComponent.h"
#include "TMMDMeshBuilder.h"
#include "MMDImportSetting.h"
#include "MMDPhysicsComponent.h"
#include "MMDPhysicsDebugDraw.h"

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

	PhysicsComponent = CreateDefaultSubobject<UMMDPhysicsComponent>(TEXT("MMD_PhysicsComponent"));
	DebugDrawComponent = CreateDefaultSubobject<UMMDPhysicsDebugDraw>(TEXT("MMD_DebugDrawComponent"));
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
	//ʹ��mmdĬ�ϵĹ���������

#pragma endregion

#pragma region SetupPhysicsAsset

#pragma endregion

#pragma region SetupAnimationBlueprint

#pragma endregion



	}
	else {
		MMDImportSetting::ShowGlobalImportProgress(FString::Printf(TEXT("MMD�ļ��������ɹ�")), EMMDMessageType::Error);
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
void AMMDActor::Cleanup()
{
	if (SkeletalMeshComponent) {
		SkeletalMeshComponent->SetSkeletalMesh(nullptr);
	}
	LoadedPMX = PMXDatas();
	LoadedPMXPath.Reset();
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

	// Initialize physics from PMX data
	if (PhysicsComponent)
	{
		PhysicsComponent->InitializeFromPMXData(PMXInfo);
		UE_LOG(LogTemp, Log, TEXT("Initialized MMD physics component with %d rigid bodies and %d joints"), 
			PMXInfo.ModelRigidCount, PMXInfo.ModelJointCount);
	}
}

void AMMDActor::SetPhysicsEnabled(bool bEnabled)
{
	if (PhysicsComponent)
	{
		PhysicsComponent->SetPhysicsEnabled(bEnabled);
	}
}

bool AMMDActor::IsPhysicsEnabled() const
{
	return PhysicsComponent ? PhysicsComponent->IsPhysicsEnabled() : false;
}

void AMMDActor::SetPhysicsDebugDrawEnabled(bool bEnabled)
{
	if (DebugDrawComponent)
	{
		DebugDrawComponent->SetDebugDrawEnabled(bEnabled);
	}
}





#include "AMMDActor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SceneComponent.h"
#include "TMMDMeshBuilder.h"

AMMDActor::AMMDActor()
{
	PrimaryActorTick.bCanEverTick = true;
	SetupComponents();
}
void AMMDActor::SetupComponents()
{
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("RootComponent"));
	SkeletalMeshComponent = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("MMD_SkeletalMesh"));
	SkeletalMeshComponent->SetupAttachment(RootComponent);
	SkeletalMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SkeletalMeshComponent->SetAnimationMode(EAnimationMode::AnimationBlueprint);
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
}





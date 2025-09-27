#include "AMMDPreviewActor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SceneComponent.h"
#include "TMMDMeshBuilder.h"

AAMMDPreviewActor::AAMMDPreviewActor()
{
	PrimaryActorTick.bCanEverTick = true;
	SetupComponents();
}
void AAMMDPreviewActor::SetupComponents()
{
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("RootComponent"));
	SkeletalMeshComponent = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("MMD_SkeletalMesh"));
	SkeletalMeshComponent->SetupAttachment(RootComponent);
	SkeletalMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SkeletalMeshComponent->SetAnimationMode(EAnimationMode::AnimationBlueprint);
}
void AAMMDPreviewActor::BeginPlay()
{
	Super::BeginPlay();
}
void AAMMDPreviewActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
}
void AAMMDPreviewActor::Cleanup()
{
	if (SkeletalMeshComponent) {
		SkeletalMeshComponent->SetSkeletalMesh(nullptr);
	}
	LoadedPMX = PMXDatas();
	LoadedPMXPath.Reset();
}
void AAMMDPreviewActor::BuildFromPMXData(const PMXDatas& PMXInfo, const FString& PMXFilePath)
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





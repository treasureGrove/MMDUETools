#include "AMMDPreviewActor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/Engine.h"
#include "TMMDMeshBuilder.h"




AAMMDPreviewActor::AAMMDPreviewActor()
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

	SetupMMDComponents();

}

// Called when the game starts or when spawned
void AAMMDPreviewActor::BeginPlay()
{
	Super::BeginPlay();
	UE_LOG(LogTemp, Log, TEXT("MMD Preview Actor: BeginPlay in Plugin Environment"));
}

// Called every frame
void AAMMDPreviewActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (bIsPlaying)
	{
		CurrentAnimTime += DeltaTime;
		// 这里后续会实现真正的VMD动画播放
	}
}

void AAMMDPreviewActor::SetupMMDComponents()
{
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("MMDRootComponent"));

	MMDSkeletalMeshComponent = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("MMDSkeletalMeshComponent"));
	MMDSkeletalMeshComponent->SetupAttachment(RootComponent);

	MMDSkeletalMeshComponent->SetCastShadow(true);
	MMDSkeletalMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MMDSkeletalMeshComponent->SetVisibility(true);
	
}
void AAMMDPreviewActor::BuildFromPMXData(const PMXDatas& PMXData, const FString& PMXFilePath)
{
	CleanupPreviousModel();

	LoadedPMXData = PMXData;
	LoadedPMXPath = PMXFilePath;
	//这里路径要修
	USkeletalMesh* NewSkeletalMesh = TMMDMeshBuilder::BuildSkeletalMeshFromPMX(PMXData,
		FString("/Game/MMDTools/Runtime"),
		PMXData.ModelNameEN + TEXT("_PluginPreview"),
		PMXFilePath);
	if (MMDSkeletalMeshComponent) {

	}

}

void AAMMDPreviewActor::PlayVMDAnimation(const VMDData& PMXInfo)
{
}

void AAMMDPreviewActor::PlayAnimation(bool bPlay)
{
}

void AAMMDPreviewActor::SetAnimationTime(float Time)
{
}
void AAMMDPreviewActor::CleanupPreviousModel()
{
}



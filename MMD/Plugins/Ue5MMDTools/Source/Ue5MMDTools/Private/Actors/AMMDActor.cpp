#include "AMMDActor.h"
#include "Animation/MMDPhysicsPlaybackAnimInstance.h"
#include "Rendering/MMDOutlineLibrary.h"
#include "EditorBridge/MMDActorImportService.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CapsuleComponent.h"
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
    if (IMMDActorImportService* Service = IMMDActorImportService::Get()) { Service->SetupComponents(this, FilePath); }
    else { UE_LOG(LogTemp, Warning, TEXT("PMX 资产导入需要编辑器模块；运行游戏时请使用已导入的资产")); }
}

bool AMMDActor::PlayMMDAnimation(UAnimationAsset* Animation, bool bLoop, bool bEnablePhysics)
{
	return UMMDPhysicsPlaybackAnimInstance::PlayMMDAnimation(SkeletalMeshComponent, Animation, bLoop, bEnablePhysics) != nullptr;
}

void AMMDActor::SetMMDAnimationPlaying(bool bPlaying)
{
	if (UMMDPhysicsPlaybackAnimInstance* Playback = Cast<UMMDPhysicsPlaybackAnimInstance>(SkeletalMeshComponent->GetAnimInstance()))
	{
		Playback->SetPlaying(bPlaying);
	}
}

void AMMDActor::SetMMDPhysicsEnabled(bool bEnabled)
{
	if (UMMDPhysicsPlaybackAnimInstance* Playback = Cast<UMMDPhysicsPlaybackAnimInstance>(SkeletalMeshComponent->GetAnimInstance()))
	{
		Playback->SetPhysicsEnabled(bEnabled);
	}
}

void AMMDActor::ResetMMDPhysics()
{
	if (UMMDPhysicsPlaybackAnimInstance* Playback = Cast<UMMDPhysicsPlaybackAnimInstance>(SkeletalMeshComponent->GetAnimInstance()))
	{
		Playback->ResetPhysics();
	}
}
void AMMDActor::FinishSetupComponents(const FString& FilePath, USkeletalMesh* BuiltMesh, const PMXDatas* PMXData)
{
    if (IMMDActorImportService* Service = IMMDActorImportService::Get()) { Service->FinishSetupComponents(this, FilePath, BuiltMesh, PMXData); }
}
#if WITH_EDITOR
void AMMDActor::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    UMMDOutlineLibrary::ApplyMMDOutline(SkeletalMeshComponent);

    if (UWorld* W = GetWorld())
    {
        const EWorldType::Type WT = W->WorldType;
        if (WT == EWorldType::Editor || WT == EWorldType::EditorPreview)
        {
            InitSimulatorForPreviewIfNeeded();
        }
    }
}

void AMMDActor::InitSimulatorForPreviewIfNeeded()
{
    UE_LOG(LogTemp, Verbose, TEXT("[AMMDActor] OnConstruction: SrcPMXPath='%s' Actor=%s"),
        *SourcePMXFilePath, *GetName());

    if (!SkeletalMeshComponent) return;

    if (!SkeletalMeshComponent->GetAnimInstance())
    {
        SkeletalMeshComponent->InitAnim(true);
    }

   
}
#endif

void AMMDActor::BeginPlay()
{
    Super::BeginPlay();
    UMMDOutlineLibrary::ApplyMMDOutline(SkeletalMeshComponent);
}

void AMMDActor::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
}

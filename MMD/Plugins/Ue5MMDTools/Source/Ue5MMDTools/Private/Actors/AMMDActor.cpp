#include "AMMDActor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "TMMDMeshBuilder.h"
#include "MMDImportSetting.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Animation/AnimBlueprint.h"
#include "AGN_MMDSkeletalControl.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "TPMXParser.h"
#include "Rendering/MMDAnimeStencilValues.h"

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

    // Enable custom depth / stencil so the anime post-process can identify MMD meshes.
    SkeletalMeshComponent->SetRenderCustomDepth(true);
    SkeletalMeshComponent->SetCustomDepthStencilValue(MMDAnimeStencil::BodyCloth);
}

void AMMDActor::SetupComponents(const FString& FilePath)
{
    // 记住路径：保存为蓝图时会复制到 CDO，供蓝图实例 OnConstruction 使用
    SourcePMXFilePath = FilePath;

    FString PMXFileName = FPaths::GetBaseFilename(FilePath);
    static TUniquePtr<TPMXParser> StaticParser = MakeUnique<TPMXParser>();
    bool bSuccess = StaticParser->ParsePMXFile(FilePath);
    if (!bSuccess)
    {
        MMDImportSetting::ShowGlobalImportProgress(TEXT("MMD文件解析不成功"), EMMDMessageType::Error);
        return;
    }

    const PMXDatas& PMXData = StaticParser->PMXInfo;

    MMDImportSetting::ShowGlobalImportProgress(
        FString::Printf(TEXT("Successfully loaded PMX file: %s"), *FilePath),
        EMMDMessageType::Success);

    TMMDMeshBuilder MeshBuilder;
    USkeletalMesh* BuiltMesh = MeshBuilder.BuildSkeletalMeshFromPMX(PMXData, TEXT("/Game/MMDModels"), PMXData.ModelNameEN, FilePath);

    if (!SkeletalMeshComponent)
    {
        SkeletalMeshComponent = NewObject<USkeletalMeshComponent>(this, TEXT("MMD_SkeletalMesh_RT"));
        SkeletalMeshComponent->SetupAttachment(CapsuleComponent);
        SkeletalMeshComponent->RegisterComponent();
        SkeletalMeshComponent->SetRelativeLocation(FVector(0.0f, 0.0f, -80.0f));
        SkeletalMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        SkeletalMeshComponent->SetCollisionResponseToAllChannels(ECR_Ignore);
        SkeletalMeshComponent->SetAnimationMode(EAnimationMode::AnimationBlueprint);
    }
    SkeletalMeshComponent->SetSkeletalMesh(BuiltMesh);

    // Ensure custom depth / stencil stays enabled after mesh assignment.
    SkeletalMeshComponent->SetRenderCustomDepth(true);
    SkeletalMeshComponent->SetCustomDepthStencilValue(MMDAnimeStencil::BodyCloth);

    UAnimBlueprint* MMDAnimBP = MeshBuilder.BuildAnimBlueprint(BuiltMesh, FilePath);

    FMMDAnimGraphHelper::AddMMDNodeToAnimBP(MMDAnimBP,PMXData,true);
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(MMDAnimBP);
    FKismetEditorUtilities::CompileBlueprint(MMDAnimBP);

    SkeletalMeshComponent->SetAnimInstanceClass(MMDAnimBP->GeneratedClass);
    if (!SkeletalMeshComponent->GetAnimInstance())
    {
        SkeletalMeshComponent->InitAnim(true);
    }



    //if (UMMDAnimInstance* MMDInst = Cast<UMMDAnimInstance>(SkeletalMeshComponent->GetAnimInstance()))
    //{
    //    MMDInst->ProvideMMDConfigAndInit(PMXData, SkeletalMeshComponent);
    //    UE_LOG(LogTemp, Log, TEXT("[AMMDActor] SetupComponents: Simulator built for %s"), *GetName());
    //}
#if WITH_EDITOR
    {
        UIKRigDefinition* IKRig = MeshBuilder.BuildIKRigFromPMX(BuiltMesh, FilePath);
        UIKRetargeter* IKRetargeter = MeshBuilder.BuildIKRetargeterFromPMX(IKRig, FilePath);
        UE_LOG(LogTemp, Log, TEXT("[AMMDActor] IK assets built: IKRig=%s, Retargeter=%s"),
            IKRig ? *IKRig->GetName() : TEXT("None"),
            IKRetargeter ? *IKRetargeter->GetName() : TEXT("None"));
    }
#endif
}

#if WITH_EDITOR
void AMMDActor::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);

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
}

void AMMDActor::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
}
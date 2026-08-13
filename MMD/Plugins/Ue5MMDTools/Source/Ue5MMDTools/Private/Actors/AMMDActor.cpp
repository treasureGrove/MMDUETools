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
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"

// 控制台命令：重新导入关卡里所有 MMD 角色（验证 mesh 构建改动如 bCastShadow 时用）。
// 用法：编辑器输出日志里输入 MMD.Reimport
static FAutoConsoleCommand GMMDReimportCommand(
	TEXT("MMD.Reimport"),
	TEXT("Re-import all MMD actors in the current level from their source PMX files."),
	FConsoleCommandDelegate::CreateLambda([]()
	{
		UWorld* World = GWorld;
		if (!World)
		{
			UE_LOG(LogTemp, Warning, TEXT("[MMD.Reimport] No world available."));
			return;
		}
		int32 Count = 0;
		for (TActorIterator<AMMDActor> It(World); It; ++It)
		{
			AMMDActor* MMD = *It;
			if (MMD && !MMD->SourcePMXFilePath.IsEmpty())
			{
				MMD->SetupComponents(MMD->SourcePMXFilePath);
				Count++;
			}
		}
		UE_LOG(LogTemp, Log, TEXT("[MMD.Reimport] Re-imported %d MMD actor(s)."), Count);
	})
);
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
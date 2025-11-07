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

#pragma region SetupIKRig
		UIKRigDefinition* IKRig = meshbuilder.BuildIKRigFromPMX(BuiltMesh, FilePath);
		UIKRetargeter* IKRetargeter = meshbuilder.BuildIKRetargeterFromPMX(IKRig, FilePath);
#pragma endregion
//#pragma region SetupPhysicsAsset
//
//#pragma endregion

	}
	else {
		MMDImportSetting::ShowGlobalImportProgress(FString::Printf(TEXT("MMD文件解析不成功")), EMMDMessageType::Error);
		return;
	}
}

void AMMDActor::InitializeMMDPhysics(UAnimGraphNode_MMDSkeletalControl* MMDNode, const PMXDatas& PMXData)
{
	if(!MMDNode)
	{
		UE_LOG(LogTemp, Error, TEXT("MMDNode is null"));
		return;
	}
	MMDNode->Node.RuntimeRigidBodies.Reset();
	for (const PMXRigid& Rigid : PMXData.ModelRigids)
	{
		FMMDRigidBodyRuntime RuntimeRigid;
		RuntimeRigid.NameEN = Rigid.NameEN;
		RuntimeRigid.NameJP = Rigid.NameJP;
		RuntimeRigid.RelatedBoneIndex = Rigid.RelatedBoneIndex;
		RuntimeRigid.NameJP = Rigid.NameJP;
		RuntimeRigid.NameEN = Rigid.NameEN;
		RuntimeRigid.RelatedBoneIndex = Rigid.RelatedBoneIndex;
		RuntimeRigid.Group = Rigid.Group;
		RuntimeRigid.CollisionMask = Rigid.CollisionMask;
		RuntimeRigid.ShapeType = Rigid.ShapeType;
		RuntimeRigid.Size = Rigid.Size;
		RuntimeRigid.Position = Rigid.Position;
		RuntimeRigid.Rotation = Rigid.Rotation;
		RuntimeRigid.Mass = Rigid.Mass;
		RuntimeRigid.LinearDamping = Rigid.LinearDamping;
		RuntimeRigid.AngularDamping = Rigid.AngularDamping;
		RuntimeRigid.Restitution = Rigid.Restitution;
		RuntimeRigid.Friction = Rigid.Friction;
		RuntimeRigid.PhysicsMode = Rigid.PhysicsMode;

		MMDNode->Node.RuntimeRigidBodies.Add(RuntimeRigid);
	}

	MMDNode->Node.RuntimeJoints.Reset();
	for (const PMXJoint& Joint : PMXData.ModelJoints)
	{
		FMMDJointRuntime RuntimeJoint;
		RuntimeJoint.NameJP = Joint.NameJP;
		RuntimeJoint.NameEN = Joint.NameEN;
		RuntimeJoint.JointType = Joint.JointType;
		RuntimeJoint.RigidA = Joint.RigidA;
		RuntimeJoint.RigidB = Joint.RigidB;
		RuntimeJoint.Position = Joint.Position;
		RuntimeJoint.Rotation = Joint.Rotation;
		RuntimeJoint.LimitPosLower = Joint.LimitPosLower;
		RuntimeJoint.LimitPosUpper = Joint.LimitPosUpper;
		RuntimeJoint.LimitRotLower = Joint.LimitRotLower;
		RuntimeJoint.LimitRotUpper = Joint.LimitRotUpper;
		RuntimeJoint.SpringPos = Joint.SpringPos;
		RuntimeJoint.SpringRot = Joint.SpringRot;

		MMDNode->Node.RuntimeJoints.Add(RuntimeJoint);
	}

	MMDNode->Node.RuntimeSoftBodies.Reset();
	for (const PMXSoftBody& SoftBody : PMXData.ModelSoftBodies)
	{
		FMMDSoftBodyRuntime RuntimeSoftBody;
		RuntimeSoftBody.NameJP = SoftBody.NameJP;
		RuntimeSoftBody.NameEN = SoftBody.NameEN;
		RuntimeSoftBody.ShapeType = SoftBody.ShapeType;
		RuntimeSoftBody.MaterialIndex = SoftBody.MaterialIndex;
		RuntimeSoftBody.Group = SoftBody.Group;
		RuntimeSoftBody.CollisionMask = SoftBody.CollisionMask;
		RuntimeSoftBody.Flags = SoftBody.Flags;
		RuntimeSoftBody.BLinkDistance = SoftBody.BLinkDistance;
		RuntimeSoftBody.ClusterCount = SoftBody.ClusterCount;
		RuntimeSoftBody.TotalMass = SoftBody.TotalMass;
		RuntimeSoftBody.Margin = SoftBody.Margin;
		RuntimeSoftBody.AeroModel = SoftBody.AeroModel;
		RuntimeSoftBody.VCF = SoftBody.VCF;
		RuntimeSoftBody.DP = SoftBody.DP;
		RuntimeSoftBody.DG = SoftBody.DG;
		RuntimeSoftBody.LF = SoftBody.LF;
		RuntimeSoftBody.PR = SoftBody.PR;
		RuntimeSoftBody.VC = SoftBody.VC;
		RuntimeSoftBody.DF = SoftBody.DF;
		RuntimeSoftBody.MT = SoftBody.MT;
		RuntimeSoftBody.CHR = SoftBody.CHR;
		RuntimeSoftBody.KHR = SoftBody.KHR;
		RuntimeSoftBody.SHR = SoftBody.SHR;
		RuntimeSoftBody.AHR = SoftBody.AHR;
		RuntimeSoftBody.SRHR_CL = SoftBody.SRHR_CL;
		RuntimeSoftBody.SKHR_CL = SoftBody.SKHR_CL;
		RuntimeSoftBody.SSHR_CL = SoftBody.SSHR_CL;
		RuntimeSoftBody.SR_SPLT_CL = SoftBody.SR_SPLT_CL;
		RuntimeSoftBody.SK_SPLT_CL = SoftBody.SK_SPLT_CL;
		RuntimeSoftBody.SS_SPLT_CL = SoftBody.SS_SPLT_CL;
		RuntimeSoftBody.V_IT = SoftBody.V_IT;
		RuntimeSoftBody.P_IT = SoftBody.P_IT;
		RuntimeSoftBody.D_IT = SoftBody.D_IT;
		RuntimeSoftBody.C_IT = SoftBody.C_IT;
		RuntimeSoftBody.LST = SoftBody.LST;
		RuntimeSoftBody.AST = SoftBody.AST;
		RuntimeSoftBody.VST = SoftBody.VST;
		RuntimeSoftBody.Anchors.Reset();
		for (const auto& Anchor : SoftBody.Anchors)
		{
			FMMDSoftBodyAnchor RuntimeAnchor;
			RuntimeAnchor.RigidIndex = Anchor.RigidIndex;
			RuntimeAnchor.VertexIndex = Anchor.VertexIndex;
			RuntimeAnchor.NearMode = Anchor.NearMode;
			RuntimeSoftBody.Anchors.Add(RuntimeAnchor);
		}
		RuntimeSoftBody.PinVertices = SoftBody.PinVertices;
		MMDNode->Node.RuntimeSoftBodies.Add(RuntimeSoftBody);
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



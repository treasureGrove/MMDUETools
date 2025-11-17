// Clean header (no asset user data dependency)
#include "MMDAnimInstance.h"
#include "MMDPhysicsSimulator.h"
#include "Animation/AnimInstanceProxy.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "MMDPhysicsSimulatorHolder.h"
#include "MMDPhysicsRegistry.h"
#include "Engine/SkeletalMesh.h"
#include "TPMXParser.h"

static FString GetSnapshotSavePath(const USkeletalMeshComponent* SkelComp)
{
    FString BaseDir = FPaths::ProjectSavedDir() / TEXT("MMDPhys");
    FString MeshName = SkelComp && SkelComp->GetSkinnedAsset() ? SkelComp->GetSkinnedAsset()->GetName() : TEXT("UnknownMesh");
    IFileManager::Get().MakeDirectory(*BaseDir, true);
    return BaseDir / (MeshName + TEXT("_PhysSnapshot.json"));
}

static void SerializeSnapshotToJson(const FMMDPhysicsSimSnapshot& Snapshot, FString& OutJson)
{
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJson);
    Writer->WriteObjectStart();
    Writer->WriteValue(TEXT("UnitScale"), Snapshot.UnitScale);
    Writer->WriteValue(TEXT("MaxSubSteps"), Snapshot.MaxSubSteps);
    Writer->WriteValue(TEXT("FixedTimeStep"), Snapshot.FixedTimeStep);
    Writer->WriteArrayStart(TEXT("Bodies"));
    for (const FMMDPhysicsBodyState& B : Snapshot.Bodies)
    {
        Writer->WriteObjectStart();
        Writer->WriteValue(TEXT("BodyIndex"), B.BodyIndex);
        const FVector Loc = B.WorldUE.GetLocation();
        const FQuat Rot = B.WorldUE.GetRotation();
        Writer->WriteObjectStart(TEXT("WorldUE"));
        Writer->WriteValue(TEXT("X"), Loc.X); Writer->WriteValue(TEXT("Y"), Loc.Y); Writer->WriteValue(TEXT("Z"), Loc.Z);
        Writer->WriteValue(TEXT("QX"), Rot.X); Writer->WriteValue(TEXT("QY"), Rot.Y); Writer->WriteValue(TEXT("QZ"), Rot.Z); Writer->WriteValue(TEXT("QW"), Rot.W);
        Writer->WriteObjectEnd();
        Writer->WriteObjectStart(TEXT("LinearVelocityUE"));
        Writer->WriteValue(TEXT("X"), B.LinearVelocityUE.X); Writer->WriteValue(TEXT("Y"), B.LinearVelocityUE.Y); Writer->WriteValue(TEXT("Z"), B.LinearVelocityUE.Z);
        Writer->WriteObjectEnd();
        Writer->WriteObjectStart(TEXT("AngularVelocityUE"));
        Writer->WriteValue(TEXT("X"), B.AngularVelocityUE.X); Writer->WriteValue(TEXT("Y"), B.AngularVelocityUE.Y); Writer->WriteValue(TEXT("Z"), B.AngularVelocityUE.Z);
        Writer->WriteObjectEnd();
        Writer->WriteValue(TEXT("bKinematic"), B.bKinematic);
        Writer->WriteValue(TEXT("bSleeping"), B.bSleeping);
        Writer->WriteObjectEnd();
    }
    Writer->WriteArrayEnd();
    Writer->WriteObjectEnd();
    Writer->Close();
}

static bool DeserializeSnapshotFromJson(const FString& InJson, FMMDPhysicsSimSnapshot& OutSnapshot)
{
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(InJson);
    TSharedPtr<FJsonObject> Root;
    if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid()) return false;
    OutSnapshot.UnitScale = Root->GetNumberField(TEXT("UnitScale"));
    OutSnapshot.MaxSubSteps = (int32)Root->GetNumberField(TEXT("MaxSubSteps"));
    OutSnapshot.FixedTimeStep = Root->GetNumberField(TEXT("FixedTimeStep"));
    const TArray<TSharedPtr<FJsonValue>>* BodiesArr;
    if (!Root->TryGetArrayField(TEXT("Bodies"), BodiesArr)) return false;
    OutSnapshot.Bodies.Reset(BodiesArr->Num());
    for (const TSharedPtr<FJsonValue>& Val : *BodiesArr)
    {
        const TSharedPtr<FJsonObject>* ObjPtr;
        if (!Val->TryGetObject(ObjPtr)) continue;
        FMMDPhysicsBodyState B; const TSharedPtr<FJsonObject>& Obj = *ObjPtr;
        B.BodyIndex = (int32)Obj->GetNumberField(TEXT("BodyIndex"));
        const TSharedPtr<FJsonObject>* TObj; if (Obj->TryGetObjectField(TEXT("WorldUE"), TObj))
        {
            FVector Loc((*TObj)->GetNumberField(TEXT("X")), (*TObj)->GetNumberField(TEXT("Y")), (*TObj)->GetNumberField(TEXT("Z")));
            FQuat Rot((*TObj)->GetNumberField(TEXT("QX")), (*TObj)->GetNumberField(TEXT("QY")), (*TObj)->GetNumberField(TEXT("QZ")), (*TObj)->GetNumberField(TEXT("QW")));
            B.WorldUE = FTransform(Rot, Loc, FVector(1,1,1));
        }
        if (Obj->TryGetObjectField(TEXT("LinearVelocityUE"), TObj))
        {
            B.LinearVelocityUE = FVector((*TObj)->GetNumberField(TEXT("X")), (*TObj)->GetNumberField(TEXT("Y")), (*TObj)->GetNumberField(TEXT("Z")));
        }
        if (Obj->TryGetObjectField(TEXT("AngularVelocityUE"), TObj))
        {
            B.AngularVelocityUE = FVector((*TObj)->GetNumberField(TEXT("X")), (*TObj)->GetNumberField(TEXT("Y")), (*TObj)->GetNumberField(TEXT("Z")));
        }
        B.bKinematic = Obj->GetBoolField(TEXT("bKinematic"));
        B.bSleeping = Obj->GetBoolField(TEXT("bSleeping"));
        OutSnapshot.Bodies.Add(B);
    }
    return true;
}

void UMMDAnimInstance::AcquireSharedHolder()
{
    if (!SkeletalMeshComp.IsValid()) SkeletalMeshComp = GetSkelMeshComponent();
    if (!SkeletalMeshComp.IsValid()) return;
    const USkeletalMesh* Mesh = SkeletalMeshComp->GetSkeletalMeshAsset();
    if (!Mesh) return;

    UMMDPhysicsRegistry* Reg = UMMDPhysicsRegistry::Get();
    const FString Key = UMMDPhysicsRegistry::BuildKeyFromMesh(Mesh);
    Holder = Reg->GetOrCreateHolder(Key);
}

void UMMDAnimInstance::ProvideMMDConfigAndInit(const PMXDatas& InPMXData, USkeletalMeshComponent* InSkelComp)
{
    SkeletalMeshComp = InSkelComp;
    EnsureSimulator();
    BuildSimulatorNow(InPMXData);
    if (Holder && Holder->Simulator.IsValid() && SkeletalMeshComp.IsValid())
    {
        FString Path = GetSnapshotSavePath(SkeletalMeshComp.Get());
        FString Json; if (FPaths::FileExists(Path) && FFileHelper::LoadFileToString(Json, *Path))
        {
            FMMDPhysicsSimSnapshot Snapshot; if (DeserializeSnapshotFromJson(Json, Snapshot))
            { Holder->Simulator->ForceApplySnapshot(Snapshot); UE_LOG(LogTemp, Log, TEXT("[MMDAnimInstance] Loaded physics snapshot: %s"), *Path); }
        }
    }
    SyncProxySimulator();
}

void UMMDAnimInstance::EnsureSimulator()
{
    if (!SkeletalMeshComp.IsValid()) SkeletalMeshComp = GetSkelMeshComponent();
    if (!Holder)
    {
        AcquireSharedHolder();
    }
    if (!Holder && !SourcePMXFilePath.IsEmpty())
    {
        // fallback: local holder if registry missed
        Holder = NewObject<UMMDPhysicsSimulatorHolder>(GetTransientPackage());
        Holder->AddToRoot();
    }
}

TSharedPtr<FMMDPhysicsSimulator, ESPMode::ThreadSafe> UMMDAnimInstance::GetSimulator() const
{
    return Holder ? Holder->Simulator : nullptr;
}

void UMMDAnimInstance::NativeInitializeAnimation()
{
    Super::NativeInitializeAnimation();
    EnsureSimulator();
    if (!GetSimulator().IsValid() && !SourcePMXFilePath.IsEmpty())
    {
        // optional: parse and build from PMX if provided
        TPMXParser Parser; if (Parser.ParsePMXFile(SourcePMXFilePath))
        {
            BuildSimulatorNow(Parser.PMXInfo);
            if (SkeletalMeshComp.IsValid())
            {
                FString Path = GetSnapshotSavePath(SkeletalMeshComp.Get());
                FString Json; if (FPaths::FileExists(Path) && FFileHelper::LoadFileToString(Json, *Path))
                {
                    FMMDPhysicsSimSnapshot Snapshot; if (DeserializeSnapshotFromJson(Json, Snapshot))
                    { Holder->Simulator->ForceApplySnapshot(Snapshot); }
                }
            }
        }
    }
    SyncProxySimulator();
}

void UMMDAnimInstance::NativeUninitializeAnimation()
{
    Super::NativeUninitializeAnimation();
    if (Holder && Holder->Simulator.IsValid() && SkeletalMeshComp.IsValid())
    {
        FMMDPhysicsSimSnapshot Snapshot; Holder->Simulator->CaptureSnapshot(Snapshot); FString Json; SerializeSnapshotToJson(Snapshot, Json); FString Path = GetSnapshotSavePath(SkeletalMeshComp.Get()); FFileHelper::SaveStringToFile(Json, *Path);
    }
    if (Holder)
    {
        FMMDAnimInstanceProxy& Proxy = GetProxyOnGameThread<FMMDAnimInstanceProxy>(); Proxy.CacheSimulator.Reset();
    }
}

void UMMDAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
    Super::NativeUpdateAnimation(DeltaSeconds);
    EnsureSimulator();
    SyncProxySimulator();
}

void UMMDAnimInstance::BuildSimulatorNow(const PMXDatas& InPMXData)
{
    EnsureSimulator(); if (!Holder) return;
    if (!Holder->Simulator.IsValid())
    {
        Holder->Simulator = MakeShared<FMMDPhysicsSimulator, ESPMode::ThreadSafe>();
    }
    if (!SkeletalMeshComp.IsValid()) SkeletalMeshComp = GetSkelMeshComponent();
    if (!SkeletalMeshComp.IsValid()) return;

    // Choose UnitScale consistent with mesh build (ConvertPMXVectorToUnreal uses 8.0f)
    const float UnitScaleUECmPerPMX = 8.f;
    const int32 MaxSubSteps = 5;
    const float FixedTimeStep = 1.f/60.f;
    const bool bOk = Holder->Simulator->InitializeFromPMX(InPMXData, SkeletalMeshComp.Get(), UnitScaleUECmPerPMX, MaxSubSteps, FixedTimeStep);
    if (!bOk) { Holder->Simulator.Reset(); }
}

void UMMDAnimInstance::DestroySimulatorNow()
{
    if (Holder && Holder->Simulator.IsValid())
    {
        Holder->Simulator->Shutdown();
        Holder->Simulator.Reset();
    }
}

void UMMDAnimInstance::SyncProxySimulator()
{
    FMMDAnimInstanceProxy& Proxy = GetProxyOnGameThread<FMMDAnimInstanceProxy>();
    Proxy.CacheSimulator = Holder ? Holder->Simulator : nullptr;
}

FAnimInstanceProxy* UMMDAnimInstance::CreateAnimInstanceProxy()
{
    return new FMMDAnimInstanceProxy(this);
}

void UMMDAnimInstance::DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy)
{
    delete static_cast<FMMDAnimInstanceProxy*>(InProxy);
}

void FMMDAnimInstanceProxy::Initialize(UAnimInstance* InAnimInstance)
{
    FAnimInstanceProxy::Initialize(InAnimInstance);
    if(UMMDAnimInstance* MMDInst = Cast<UMMDAnimInstance>(InAnimInstance))
    {
        MMDInst->EnsureSimulator();
        CacheSimulator = MMDInst->GetSimulator();
    }
}

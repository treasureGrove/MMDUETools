#include "MMDPhysicsSimulatorHolder.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

void UMMDPhysicsSimulatorHolder::CaptureNow()
{
    if (Simulator) { Simulator->CaptureSnapshot(LastSnapshot); }
}

bool UMMDPhysicsSimulatorHolder::ApplyNow(bool bRespectKinematic)
{
    return Simulator ? Simulator->ApplySnapshot(LastSnapshot, bRespectKinematic) : false;
}

FString UMMDPhysicsSimulatorHolder::BuildPath(const FString& Identifier) const
{
    FString BaseDir = FPaths::ProjectSavedDir() / TEXT("MMDPhys");
    IFileManager::Get().MakeDirectory(*BaseDir, true);
    return BaseDir / (Identifier + TEXT(".json"));
}

bool UMMDPhysicsSimulatorHolder::SerializeToJson(const FMMDPhysicsSimSnapshot& Snapshot, FString& OutJson) const
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
    return true;
}

bool UMMDPhysicsSimulatorHolder::DeserializeFromJson(const FString& InJson, FMMDPhysicsSimSnapshot& OutSnapshot) const
{
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(InJson);
    TSharedPtr<FJsonObject> Root; if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid()) return false;
    OutSnapshot.UnitScale = Root->GetNumberField(TEXT("UnitScale"));
    OutSnapshot.MaxSubSteps = (int32)Root->GetNumberField(TEXT("MaxSubSteps"));
    OutSnapshot.FixedTimeStep = Root->GetNumberField(TEXT("FixedTimeStep"));
    const TArray<TSharedPtr<FJsonValue>>* BodiesArr; if (!Root->TryGetArrayField(TEXT("Bodies"), BodiesArr)) return false;
    OutSnapshot.Bodies.Reset(BodiesArr->Num());
    for (const TSharedPtr<FJsonValue>& Val : *BodiesArr)
    {
        const TSharedPtr<FJsonObject>* ObjPtr; if (!Val->TryGetObject(ObjPtr)) continue; const TSharedPtr<FJsonObject>& Obj = *ObjPtr; FMMDPhysicsBodyState B; B.BodyIndex = (int32)Obj->GetNumberField(TEXT("BodyIndex"));
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

bool UMMDPhysicsSimulatorHolder::SaveSnapshotToDisk(const FString& Identifier)
{
    CaptureNow(); FString Json; if(!SerializeToJson(LastSnapshot, Json)) return false; const FString Path = BuildPath(Identifier); const bool bOk = FFileHelper::SaveStringToFile(Json, *Path); if(bOk){ UE_LOG(LogTemp, Log, TEXT("[MMDPhysHolder] Saved snapshot: %s"), *Path);} return bOk;
}

bool UMMDPhysicsSimulatorHolder::LoadSnapshotFromDisk(const FString& Identifier, bool bApply, bool bForce)
{
    const FString Path = BuildPath(Identifier); FString Json; if(!FPaths::FileExists(Path) || !FFileHelper::LoadFileToString(Json, *Path)) return false; FMMDPhysicsSimSnapshot Loaded; if(!DeserializeFromJson(Json, Loaded)) return false; LastSnapshot = Loaded; if(bApply && Simulator){ if(bForce){ Simulator->ForceApplySnapshot(LastSnapshot); } else { Simulator->ApplySnapshot(LastSnapshot, true); } } UE_LOG(LogTemp, Log, TEXT("[MMDPhysHolder] Loaded snapshot: %s"), *Path); return true;
}

void UMMDPhysicsSimulatorHolder::BeginDestroy()
{
    Simulator.Reset(); Super::BeginDestroy();
}

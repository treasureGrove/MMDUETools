#include "MMDPhysicsRegistry.h"
#include "Engine/SkeletalMesh.h"

static TWeakObjectPtr<UMMDPhysicsRegistry> GRegistry;

UMMDPhysicsRegistry* UMMDPhysicsRegistry::Get()
{
    if (!GRegistry.IsValid())
    {
        GRegistry = NewObject<UMMDPhysicsRegistry>(GetTransientPackage(), TEXT("MMDPhysicsRegistry"));
        GRegistry->AddToRoot();
    }
    return GRegistry.Get();
}

UMMDPhysicsSimulatorHolder* UMMDPhysicsRegistry::FindHolder(const FString& Key) const
{
    if (const TObjectPtr<UMMDPhysicsSimulatorHolder>* Found = Holders.Find(Key))
    {
        return Found->Get();
    }
    return nullptr;
}

UMMDPhysicsSimulatorHolder* UMMDPhysicsRegistry::GetOrCreateHolder(const FString& Key)
{
    if (UMMDPhysicsSimulatorHolder* Existing = FindHolder(Key))
    {
        return Existing;
    }
    UMMDPhysicsSimulatorHolder* NewHolder = NewObject<UMMDPhysicsSimulatorHolder>(GetTransientPackage());
    NewHolder->AddToRoot();
    Holders.Add(Key, NewHolder);
    return NewHolder;
}

void UMMDPhysicsRegistry::RemoveHolder(const FString& Key)
{
    if (TObjectPtr<UMMDPhysicsSimulatorHolder>* Found = Holders.Find(Key))
    {
        if (Found->Get())
        {
            Found->Get()->RemoveFromRoot();
        }
        Holders.Remove(Key);
    }
}

FString UMMDPhysicsRegistry::BuildKeyFromMesh(const USkeletalMesh* Mesh)
{
    if (!Mesh) return TEXT("None");
    return Mesh->GetPathName();
}

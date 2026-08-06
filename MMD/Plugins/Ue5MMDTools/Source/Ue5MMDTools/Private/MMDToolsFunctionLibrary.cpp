#include "MMDToolsFunctionLibrary.h"
#include "Components/MeshComponent.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "UObject/Package.h"
#include "HAL/IConsoleManager.h"

#if WITH_EDITOR
#include "Editor.h"
#include "Engine/Selection.h"
#include "GameFramework/Actor.h"
#endif

static UMaterial* GetHighlightMaterial()
{
	static UMaterial* HighlightMat = nullptr;
	if (!HighlightMat)
	{
		HighlightMat = NewObject<UMaterial>(GetTransientPackage(), TEXT("MMD_DebugHighlightMat"));
		HighlightMat->SetFlags(RF_Transient);
		HighlightMat->SetShadingModel(EMaterialShadingModel::MSM_Unlit);

		auto& Col = HighlightMat->GetExpressionCollection();

		// 实心洋红，整块 submesh 替换后一眼看清范围
		UMaterialExpressionConstant3Vector* Emissive = NewObject<UMaterialExpressionConstant3Vector>(HighlightMat);
		Emissive->Constant = FLinearColor(1.0f, 0.0f, 1.0f, 1.0f);
		Col.Expressions.Add(Emissive);

		HighlightMat->GetEditorOnlyData()->EmissiveColor.Expression = Emissive;

		HighlightMat->PostEditChange();
	}
	return HighlightMat;
}

static TMap<TWeakObjectPtr<UMeshComponent>, TMap<int32, TWeakObjectPtr<UMaterialInterface>>> GHighlightOriginals;

void UMMDToolsFunctionLibrary::HighlightMaterialSlot(UMeshComponent* Mesh, int32 SlotIndex)
{
	if (!Mesh)
	{
		return;
	}

	TMap<int32, TWeakObjectPtr<UMaterialInterface>>& SlotMap = GHighlightOriginals.FindOrAdd(Mesh);
	if (!SlotMap.Contains(SlotIndex))
	{
		SlotMap.Add(SlotIndex, Mesh->GetMaterial(SlotIndex));
	}

	Mesh->SetMaterial(SlotIndex, GetHighlightMaterial());
}

void UMMDToolsFunctionLibrary::ClearMaterialHighlight(UMeshComponent* Mesh, int32 SlotIndex)
{
	if (!Mesh)
	{
		return;
	}

	TMap<int32, TWeakObjectPtr<UMaterialInterface>>* SlotMap = GHighlightOriginals.Find(Mesh);
	if (SlotMap && SlotMap->Contains(SlotIndex))
	{
		if (UMaterialInterface* Original = SlotMap->FindAndRemoveChecked(SlotIndex).Get())
		{
			Mesh->SetMaterial(SlotIndex, Original);
		}
	}
	if (SlotMap && SlotMap->Num() == 0)
	{
		GHighlightOriginals.Remove(Mesh);
	}
}

void UMMDToolsFunctionLibrary::ClearAllMaterialHighlights(UMeshComponent* Mesh)
{
	if (!Mesh)
	{
		return;
	}

	TMap<int32, TWeakObjectPtr<UMaterialInterface>>* SlotMap = GHighlightOriginals.Find(Mesh);
	if (SlotMap)
	{
		for (auto& Pair : *SlotMap)
		{
			if (UMaterialInterface* Original = Pair.Value.Get())
			{
				Mesh->SetMaterial(Pair.Key, Original);
			}
		}
		GHighlightOriginals.Remove(Mesh);
	}
}

#if WITH_EDITOR

// 编辑器控制台命令：选中角色，敲 MMDHighlightMaterial <SlotIndex> 高亮对应子网格
static UMeshComponent* GetSelectedMesh()
{
	if (!GEditor)
	{
		return nullptr;
	}
	USelection* Selection = GEditor->GetSelectedActors();
	AActor* Actor = Selection ? Selection->GetTop<AActor>() : nullptr;
	if (!Actor)
	{
		UE_LOG(LogTemp, Warning, TEXT("MMDHighlightMaterial: 请先在视口选中一个 Actor"));
		return nullptr;
	}
	UMeshComponent* Mesh = Actor->FindComponentByClass<UMeshComponent>();
	if (!Mesh)
	{
		UE_LOG(LogTemp, Warning, TEXT("MMDHighlightMaterial: %s 没有 MeshComponent"), *Actor->GetName());
	}
	return Mesh;
}

static void ExecuteHighlightMaterial(const TArray<FString>& Args)
{
	if (Args.Num() < 1)
	{
		UE_LOG(LogTemp, Warning, TEXT("用法: MMDHighlightMaterial <SlotIndex>"));
		return;
	}
	UMeshComponent* Mesh = GetSelectedMesh();
	if (!Mesh)
	{
		return;
	}
	const int32 SlotIndex = FCString::Atoi(*Args[0]);
	UMMDToolsFunctionLibrary::HighlightMaterialSlot(Mesh, SlotIndex);
	UE_LOG(LogTemp, Log, TEXT("MMDHighlightMaterial: 高亮 %s 的槽位 %d"), *Mesh->GetOwner()->GetName(), SlotIndex);
}

static void ExecuteClearHighlight(const TArray<FString>& Args)
{
	UMeshComponent* Mesh = GetSelectedMesh();
	if (!Mesh)
	{
		return;
	}
	UMMDToolsFunctionLibrary::ClearAllMaterialHighlights(Mesh);
	UE_LOG(LogTemp, Log, TEXT("MMDClearHighlight: 清除 %s 的高亮"), *Mesh->GetOwner()->GetName());
}

static FAutoConsoleCommand CmdMMDHighlightMaterial(
	TEXT("MMDHighlightMaterial"),
	TEXT("高亮选中 Actor 的指定材质槽位（子网格）。用法: MMDHighlightMaterial <SlotIndex>"),
	FConsoleCommandWithArgsDelegate::CreateStatic(ExecuteHighlightMaterial));

static FAutoConsoleCommand CmdMMDClearHighlight(
	TEXT("MMDClearHighlight"),
	TEXT("清除选中 Actor 的材质高亮。"),
	FConsoleCommandWithArgsDelegate::CreateStatic(ExecuteClearHighlight));

#endif // WITH_EDITOR

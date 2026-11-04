#pragma once

#include "CoreMinimal.h"
#include "UI/MMDStudioDisplayStyle.h"
#include "UObject/StrongObjectPtr.h"

class FPreviewScene;
class UStaticMesh;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;
enum class EMMDLightingEnvironment : uint8;

// 默认预览与离线样片共用的清爽展示场景，不创建灯光、不保存资源。
class FMMDStudioDisplayScene
{
public:
	FMMDStudioDisplayScene(FPreviewScene& InScene, const FBox& ModelBounds, EMMDStudioDisplayStyle InStyle = EMMDStudioDisplayStyle::Studio);
	~FMMDStudioDisplayScene();
	bool IsValid() const { return bInitialized; }
	void SetStyle(EMMDStudioDisplayStyle InStyle);
	EMMDStudioDisplayStyle GetStyle() const { return Style; }
	void UpdateBounds(const FBox& ModelBounds);
	void SetVisible(bool bVisible);
	static TConstArrayView<EMMDStudioDisplayStyle> GetStyles();
	static EMMDLightingEnvironment GetLightingEnvironment(EMMDStudioDisplayStyle InStyle);
	static FString GetDisplayName(EMMDStudioDisplayStyle InStyle);
	static FString GetDescription(EMMDStudioDisplayStyle InStyle);
	static FString GetId(EMMDStudioDisplayStyle InStyle);
	static bool TryParse(const FString& Id, EMMDStudioDisplayStyle& OutStyle);

private:
	FPreviewScene* Scene = nullptr;
	TStrongObjectPtr<UStaticMesh> PrimitiveCylinder;
	TArray<TStrongObjectPtr<UStaticMeshComponent>> Components;
	TArray<TStrongObjectPtr<UMaterialInstanceDynamic>> Materials;
	TArray<TStrongObjectPtr<UMaterialInstanceDynamic>> BackdropMaterials;
	TArray<bool> StyleVisibility;
	FBox CurrentBounds = FBox(ForceInit);
	EMMDStudioDisplayStyle Style = EMMDStudioDisplayStyle::Studio;
	bool bInitialized = false;
	bool bSceneVisible = true;
};

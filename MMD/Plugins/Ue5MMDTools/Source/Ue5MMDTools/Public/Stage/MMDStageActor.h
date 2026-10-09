#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Stage/MMDStageRenderPreset.h"
#include "MMDStageActor.generated.h"

class UBoxComponent;
class UDirectionalLightComponent;
class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UPointLightComponent;
class UPostProcessComponent;
class USkeletalMeshComponent;
class USkyLightComponent;
class UStaticMesh;
class UTextureCube;

UCLASS(Blueprintable)
class UE5MMDTOOLS_API AMMDStageActor : public AActor
{
	GENERATED_BODY()

public:
	AMMDStageActor();
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void Tick(float DeltaSeconds) override;
#if WITH_EDITOR
	virtual bool ShouldTickIfViewportsOnly() const override { return bPreviewAnimation; }
#endif

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD|Stage")
	EMMDStageTheme Theme = EMMDStageTheme::NeonConcert;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD|Stage")
	TObjectPtr<UMMDStageRenderPreset> RenderPreset;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category = "MMD|Animation", meta = (ClampMin = "1", ClampMax = "300"))
	float BPM = 118.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category = "MMD|Animation", meta = (ClampMin = "0", ClampMax = "4"))
	float AnimationSpeed = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD|Animation")
	bool bAnimate = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD|Animation")
	bool bPreviewAnimation = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD|Animation")
	bool bUseManualTime = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category = "MMD|Animation", meta = (ClampMin = "0"))
	float ManualTimeSeconds = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MMD|Stage")
	TObjectPtr<USceneComponent> StageRoot;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MMD|Lighting")
	TObjectPtr<UDirectionalLightComponent> KeyLight;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MMD|Lighting")
	TObjectPtr<UDirectionalLightComponent> FillLight;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MMD|Lighting")
	TObjectPtr<UDirectionalLightComponent> RimLight;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MMD|Lighting")
	TObjectPtr<USkyLightComponent> GardenSkyLight;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MMD|PostProcess")
	TObjectPtr<UBoxComponent> PostProcessBounds;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MMD|PostProcess")
	TObjectPtr<UPostProcessComponent> PostProcess;

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "MMD|Stage")
	void RebuildStage();
	/** 固定时间评估，不依赖世界时钟，适合 Sequencer 与后台截图。 */
	UFUNCTION(BlueprintCallable, Category = "MMD|Animation")
	void EvaluateStageAtTime(float TimeSeconds);
	UFUNCTION(BlueprintCallable, Category = "MMD|Stage")
	void ApplyStageRenderPreset(UMMDStageRenderPreset* Preset);
	UFUNCTION(BlueprintCallable, Category = "MMD|Stage")
	bool ApplyCharacterRenderPreset(USkeletalMeshComponent* Component, bool bReplaceMaterials = true);
	UFUNCTION(BlueprintCallable, Category = "MMD|Stage")
	bool RestoreCharacterMaterials(USkeletalMeshComponent* Component);

	UFUNCTION(BlueprintPure, Category = "MMD|Stage")
	FVector GetCharacterStandingLocation() const { return GetActorTransform().TransformPosition(FVector::ZeroVector); }

	UMaterialInterface* GetDefaultCharacterMaterial(bool bMasked, bool bTranslucent) const
	{
		return bMasked ? DefaultCharacterMaterialMasked.Get()
			: bTranslucent ? DefaultCharacterMaterialTranslucent.Get() : DefaultCharacterMaterialOpaque.Get();
	}

protected:
	virtual void BeginPlay() override;

private:
	enum class EMotion : uint8 { Equalizer, Orbit, Petal, Ring, Ripple, Beam };
	struct FAnimatedInstance
	{
		TWeakObjectPtr<UInstancedStaticMeshComponent> Component;
		int32 Index = INDEX_NONE;
		FTransform BaseTransform;
		EMotion Motion = EMotion::Orbit;
		float Phase = 0.0f;
	};
	struct FStyledCharacter
	{
		TWeakObjectPtr<USkeletalMeshComponent> Component;
		bool bReplaceMaterials = true;
	};

	UPROPERTY()
	TObjectPtr<UStaticMesh> CubeMesh;
	UPROPERTY()
	TObjectPtr<UStaticMesh> CylinderMesh;
	UPROPERTY()
	TObjectPtr<UStaticMesh> SphereMesh;
	UPROPERTY()
	TObjectPtr<UStaticMesh> ConeMesh;
	UPROPERTY()
	TObjectPtr<UStaticMesh> GardenRoofHipMesh;
	UPROPERTY()
	TObjectPtr<UStaticMesh> GardenRoofTileMesh;
	UPROPERTY()
	TObjectPtr<UStaticMesh> GardenCurvedBeamMesh;
	UPROPERTY()
	TObjectPtr<UStaticMesh> GardenBranchMesh;
	UPROPERTY()
	TObjectPtr<UStaticMesh> GardenTreeTrunkV2Mesh;
	UPROPERTY()
	TObjectPtr<UStaticMesh> GardenBranchV2Mesh;
	UPROPERTY()
	TObjectPtr<UStaticMesh> GardenBlossomClusterV2Mesh;
	UPROPERTY()
	TObjectPtr<UStaticMesh> GardenReedClumpV2Mesh;
	UPROPERTY()
	TObjectPtr<UStaticMesh> GardenBlossomClusterMesh;
	UPROPERTY()
	TObjectPtr<UStaticMesh> GardenPetalMesh;
	UPROPERTY()
	TObjectPtr<UStaticMesh> GardenRockMesh;
	UPROPERTY()
	TObjectPtr<UStaticMesh> GardenLanternCapMesh;
	UPROPERTY()
	TObjectPtr<UStaticMesh> GardenLanternPedestalMesh;
	UPROPERTY()
	TObjectPtr<UStaticMesh> GardenWaterRippleMesh;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> DefaultSurfaceMaterial;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> DefaultPanelMaterial;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> DefaultGlowMaterial;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> DefaultBeamMaterial;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> DefaultBackdropMaterial;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> DefaultGardenSurfaceMaterial;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> DefaultGardenWaterMaterial;
	UPROPERTY()
	TObjectPtr<UTextureCube> GardenSkyCubemap;
	UPROPERTY()
	TObjectPtr<UTextureCube> FallbackSkyCubemap;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> DefaultCharacterMaterialOpaque;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> DefaultCharacterMaterialMasked;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> DefaultCharacterMaterialTranslucent;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UInstancedStaticMeshComponent>> StageMeshes;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> StageMaterials;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UPointLightComponent>> AccentLights;
	TArray<FAnimatedInstance> AnimatedInstances;
	TArray<FStyledCharacter> StyledCharacters;
	TArray<float> MaterialEmissions;
	TArray<float> AccentLightScales;
	TArray<float> AccentLightOverrides;
	double ElapsedSeconds = 0.0;

	UInstancedStaticMeshComponent* MakeGroup(const TCHAR* Name, UStaticMesh* Mesh, const FLinearColor& Color,
		bool bGlow, float Emission = 1.0f, bool bPanel = false);
	UInstancedStaticMeshComponent* MakeBeamGroup(const TCHAR* Name, const FLinearColor& Color);
	void AddInstance(UInstancedStaticMeshComponent* Group, const FVector& Position, const FVector& Scale,
		const FRotator& Rotation = FRotator::ZeroRotator);
	void AddAnimated(UInstancedStaticMeshComponent* Group, const FVector& Position, const FVector& Scale,
		EMotion Motion, float Phase, const FRotator& Rotation = FRotator::ZeroRotator);
	void AddRing(UInstancedStaticMeshComponent* Group, const FVector& Center, float Radius, float Width,
		bool bVertical, int32 Segments = 64, EMotion Motion = EMotion::Ring, bool bAnimated = false);
	void AddArc(UInstancedStaticMeshComponent* Group, const FVector& Center, float Radius, float Width,
		float StartAngleDegrees, float EndAngleDegrees, int32 Segments = 48, float Depth = 10.0f);
	void AddCylinderSegment(UInstancedStaticMeshComponent* Group, const FVector& Start, const FVector& End, float Radius);
	void BuildNeonConcert(const FLinearColor& Base, const FLinearColor& Accent, const FLinearColor& Secondary);
	void BuildMoonMirror(const FLinearColor& Base, const FLinearColor& Accent, const FLinearColor& Secondary);
	void BuildSakuraTheater(const FLinearColor& Base, const FLinearColor& Accent, const FLinearColor& Secondary);
	void LoadGardenMeshReferences();
	void BuildStageBackdrop();
	void ConfigureLighting();
	void ClearGeneratedComponents();
};

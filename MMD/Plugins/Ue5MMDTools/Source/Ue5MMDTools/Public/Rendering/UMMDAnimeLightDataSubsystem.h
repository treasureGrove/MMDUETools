#pragma once
#include "CoreMinimal.h"
#include "Subsystems/EngineSubsystem.h"
#include "UMMDAnimeLightDataSubsystem.generated.h"

class UTextureRenderTarget2D;
class FPostOpaqueRenderParameters;

/**
 * Collects scene lights on the game thread (triggered from a view extension's
 * SetupViewFamily, so it stays synchronized with the current frame), packs them
 * into a light data buffer (16 lights x 4 float4 per light), and writes it into
 * a user-provided UTextureRenderTarget2D via a compute pass hooked to the
 * renderer's PostOpaque delegate.
 *
 * Light data texture layout (width = 16 * 4 = 64, height = 1, RGBA32F):
 *   slot i occupies texels x = i*4+0 .. i*4+3:
 *     +0 : float4(position.rgb, type)   type: 0=empty 1=point 2=spot 3=directional 4=rect
 *     +1 : float4(color.rgb, intensity)
 *     +2 : float4(direction.rgb, radius)  directional uses direction, point/spot use radius
 *     +3 : float4(innerCos, outerCos, falloff, 0)   rect: (sizeX, sizeY, 0, 0)
 */
UCLASS()
class UE5MMDTOOLS_API UMMDAnimeLightDataSubsystem : public UEngineSubsystem
{
	GENERATED_BODY()

public:
	static constexpr int32 MaxLights = 16;

	/** Accessor for the game-thread subsystem (returns null if the engine isn't ready). */
	static UMMDAnimeLightDataSubsystem* Get();

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Collects lights and pushes them to the render thread for the current frame.
	 *  Called from a view extension's SetupViewFamily on the game thread. */
	void CollectLightsForFrame();

	/** Register the render target that will receive the packed light data. */
	UFUNCTION(BlueprintCallable, Category = "MMD Anime|LightData")
	void SetLightDataRenderTarget(UTextureRenderTarget2D* InRenderTarget);

	UFUNCTION(BlueprintPure, Category = "MMD Anime|LightData")
	UTextureRenderTarget2D* GetLightDataRenderTarget() const { return LightDataRT; }

	UFUNCTION(BlueprintCallable, Category = "MMD Anime|LightData")
	void SetLightDataCollectionEnabled(bool bInEnabled) { bCollectionEnabled = bInEnabled; }

private:
	void CollectLights(UWorld* World, TArray<FVector4f>& OutData);
	void OnPostOpaque(FPostOpaqueRenderParameters& Parameters);

	/** Auto-loads (or auto-creates in the editor) the light data RT asset and registers it. */
	void AutoSetupLightDataRT();

	bool bCollectionEnabled = true;
	bool bAutoSetupDone = false;

	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> LightDataRT = nullptr;

	FDelegateHandle PostOpaqueDelegateHandle;

	// Render-thread-owned copy of the latest packed light data.
	TArray<FVector4f> RenderThreadLightData;
};

#pragma once
#include "CoreMinimal.h"
#include "SceneViewExtension.h"
#include "Rendering/MMDAnimeEnvironmentUniformBuffer.h"

class FMMDAnimeViewExtension : public FSceneViewExtensionBase
{
public:
    FMMDAnimeViewExtension(const FAutoRegister& AutoRegister);
    virtual ~FMMDAnimeViewExtension() = default;

    virtual bool IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const override;
    virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) override;
    virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override;
    virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override;

    virtual void SubscribeToPostProcessingPass(
        EPostProcessingPass PassId,
        const FSceneView& View,
        FAfterPassCallbackDelegateArray& InOutPassCallbacks,
        bool bIsPassEnabled) override;

    FScreenPassTexture PostProcessCallback_RenderThread(
        FRDGBuilder& GraphBuilder,
        const FSceneView& View,
        const FPostProcessMaterialInputs& Inputs);

    void SetEnabled(bool bInEnabled) { bEnabled = bInEnabled; }
    bool IsEnabled() const { return bEnabled; }

    const FAnimeEnvironmentParameters& GetEnvParams() const { return EnvParams; }

private:
    void CollectLights(const FSceneViewFamily& InViewFamily);

    bool bEnabled = true;
    mutable bool bDataReady = false;
    FAnimeEnvironmentParameters EnvParams;
};

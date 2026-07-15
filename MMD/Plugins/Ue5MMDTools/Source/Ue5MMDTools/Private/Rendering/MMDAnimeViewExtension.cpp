#include "Rendering/MMDAnimeViewExtension.h"
#include "PostProcess/PostProcessMaterialInputs.h"
#include "ScreenPass.h"
#include "EngineUtils.h"
#include "Components/SpotLightComponent.h"

FMMDAnimeViewExtension::FMMDAnimeViewExtension(const FAutoRegister& AutoRegister)
	: FSceneViewExtensionBase(AutoRegister) {}

bool FMMDAnimeViewExtension::IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const {
		
	return bEnabled && bDataReady;
}

void FMMDAnimeViewExtension::SetupViewFamily(FSceneViewFamily& InViewFamily) {
	UWorld* World = InViewFamily.Scene ? InViewFamily.Scene->GetWorld():nullptr;
	if (!World) {
		bDataReady = false;
		return;
	}
	FMemory::Memzero(&EnvParams,sizeof(EnvParams));

	int PtIdx = 0, SpIdx = 0, RtIdx = 0;

	for (TActorIterator<AActor> It(World); It; ++It) {
		AActor* Actor = *It;
		if (!Actor || Actor->IsHidden())
			continue;

		if (SpIdx < MMD_ANIME_MAX_POINT_LIGHTS) {
			TArray<USpotLightComponent*> SpotComps;
			Actor->GetComponents<USpotLightComponent>(SpotComps);
			for (USpotLightComponent* L : SpotComps)
			{
				if (!L || !L->IsVisible()||SpIdx >= MMD_ANIME_MAX_SPOT_LIGHTS)
					continue;

				const FLinearColor SpotLightColor = L->GetLightColor()*L->Intensity;
				const float HalfRad = FMath::DegreesToRadians(L->OuterConeAngle*0.5f);
			}
		}
	}

	bDataReady = (EnvParams.PointLightCount.X + EnvParams.SpotLightCount.X + EnvParams.RectLightCount.X) > 0;
}

void FMMDAnimeViewExtension::SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) {}

void FMMDAnimeViewExtension::BeginRenderViewFamily(FSceneViewFamily& InViewFamily) {}

void FMMDAnimeViewExtension::SubscribeToPostProcessingPass(EPostProcessingPass PassId, const FSceneView& View, FAfterPassCallbackDelegateArray& InOutPassCallbacks, bool bIsPassEnabled) {}

FScreenPassTexture FMMDAnimeViewExtension::PostProcessCallback_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& View, const FPostProcessMaterialInputs& Inputs)
{
	return Inputs.OverrideOutput;
}
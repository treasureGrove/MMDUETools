#include "Rendering/MMDAnimeViewExtension.h"
#include "PostProcess/PostProcessMaterialInputs.h"
#include "ScreenPass.h"
#include "Rendering/MMDAnimePostProcessPass.h"
#include "EngineUtils.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/PointLightComponent.h"

static TAutoConsoleVariable<int32> CVarMMDAnimeToonEnabled(
    TEXT("r.MMDAnimeToon.Enabled"),
    0,
    TEXT("0 = disable MMD Anime Toon post-process (default), 1 = enable"),
    ECVF_RenderThreadSafe);

FMMDAnimeViewExtension::FMMDAnimeViewExtension(const FAutoRegister& AutoRegister)
    : FSceneViewExtensionBase(AutoRegister)
{
    FMemory::Memzero(&EnvParams, sizeof(EnvParams));
    EnvParams.ToonShadowColor = FVector4f(0.3f, 0.35f, 0.45f, 0.0f);
    EnvParams.ToonShadowStep  = FVector4f(0.3f, 0.7f, 0.0f, 0.0f);
    EnvParams.ToonRimColor    = FVector4f(0.8f, 0.9f, 1.0f, 0.0f);
    EnvParams.ToonRimParams   = FVector4f(3.0f, 0.2f, 0.0f, 0.0f);
}

bool FMMDAnimeViewExtension::IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const
{
    return bEnabled && bDataReady && CVarMMDAnimeToonEnabled.GetValueOnGameThread() != 0;
}

void FMMDAnimeViewExtension::SetupViewFamily(FSceneViewFamily& InViewFamily)
{
    CollectLights(InViewFamily);
}

void FMMDAnimeViewExtension::SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView)
{
    EnvParams.ViewPosition = FVector4f((FVector3f)InView.ViewMatrices.GetViewOrigin(), 0.0f);
}

void FMMDAnimeViewExtension::BeginRenderViewFamily(FSceneViewFamily& InViewFamily) {}

void FMMDAnimeViewExtension::SubscribeToPostProcessingPass(
    EPostProcessingPass PassId,
    const FSceneView& View,
    FAfterPassCallbackDelegateArray& InOutPassCallbacks,
    bool bIsPassEnabled)
{
    if (PassId == EPostProcessingPass::Tonemap)
    {
        InOutPassCallbacks.Add(
            FAfterPassCallbackDelegate::CreateRaw(this, &FMMDAnimeViewExtension::PostProcessCallback_RenderThread));
    }
}

FScreenPassTexture FMMDAnimeViewExtension::PostProcessCallback_RenderThread(
    FRDGBuilder& GraphBuilder,
    const FSceneView& View,
    const FPostProcessMaterialInputs& Inputs)
{
    if (!bDataReady || CVarMMDAnimeToonEnabled.GetValueOnRenderThread() == 0)
    {
        return Inputs.OverrideOutput;
    }

    FAnimeEnvironmentParameters* UBData = GraphBuilder.AllocParameters<FAnimeEnvironmentParameters>();
    *UBData = EnvParams;
    TRDGUniformBufferRef<FAnimeEnvironmentParameters> ToonUB = GraphBuilder.CreateUniformBuffer(UBData);

    return AddMMDAnimePostProcessPass(GraphBuilder, View, Inputs, ToonUB);
}

// ==================================================================
// Light Collection (frustum cull + distance sort)
// ==================================================================

struct FSpotCandidate
{
    FVector Position;
    FVector Direction;
    FLinearColor Color;
    float Radius;
    float InnerConeCos;
    float OuterConeCos;
    float DistSq;
};

struct FPointCandidate
{
    FVector Position;
    FLinearColor Color;
    float Radius;
    float DistSq;
};

void FMMDAnimeViewExtension::CollectLights(const FSceneViewFamily& InViewFamily)
{
    UWorld* World = InViewFamily.Scene ? InViewFamily.Scene->GetWorld() : nullptr;
    if (!World)
    {
        bDataReady = false;
        return;
    }
    FMemory::Memzero(&EnvParams, sizeof(EnvParams));

    FVector CameraPos = FVector::ZeroVector;
    FConvexVolume ViewFrustum;
    bool bHasFrustum = false;

    if (InViewFamily.Views.Num() > 0 && InViewFamily.Views[0])
    {
        CameraPos = InViewFamily.Views[0]->ViewMatrices.GetViewOrigin();
        ViewFrustum = InViewFamily.Views[0]->ViewFrustum;
        bHasFrustum = true;
    }

    const float MaxLightDistance = 5000.0f;
    const float MaxDistSq = MaxLightDistance * MaxLightDistance;

    TArray<FSpotCandidate>  SpotCandidates;
    TArray<FPointCandidate> PointCandidates;
    bool bHasDirectional = false;

    for (TActorIterator<AActor> It(World); It; ++It)
    {
        AActor* Actor = *It;
        if (!Actor || Actor->IsHidden())
            continue;

        // ----- Directional light (first visible only) -----
        if (!bHasDirectional)
        {
            UDirectionalLightComponent* DirComp = Actor->FindComponentByClass<UDirectionalLightComponent>();
            if (DirComp && DirComp->IsVisible())
            {
                FVector Forward = Actor->GetActorForwardVector();
                EnvParams.DirectionalLightDirection = FVector4f((FVector3f)Forward, 0.0f);
                FLinearColor Col = DirComp->GetLightColor() * DirComp->Intensity;
                EnvParams.DirectionalLightColor = FVector4f((FVector3f)Col, Col.A);
                bHasDirectional = true;
            }
        }

        // ----- Spot lights -----
        {
            TArray<USpotLightComponent*> SpotComps;
            Actor->GetComponents<USpotLightComponent>(SpotComps);
            for (USpotLightComponent* L : SpotComps)
            {
                if (!L || !L->IsVisible())
                    continue;

                FVector Pos = L->GetComponentLocation();
                float DistSq = FVector::DistSquared(Pos, CameraPos);
                if (DistSq > MaxDistSq)
                    continue;

                if (bHasFrustum && !ViewFrustum.IntersectSphere(Pos, L->AttenuationRadius))
                    continue;

                FSpotCandidate& C = SpotCandidates.AddDefaulted_GetRef();
                C.Position     = Pos;
                C.Direction    = L->GetForwardVector();
                C.Color        = L->GetLightColor() * L->Intensity;
                C.Radius       = L->AttenuationRadius;
                float HalfInner = FMath::DegreesToRadians(L->InnerConeAngle * 0.5f);
                float HalfOuter = FMath::DegreesToRadians(L->OuterConeAngle * 0.5f);
                C.InnerConeCos = FMath::Cos(HalfInner);
                C.OuterConeCos = FMath::Cos(HalfOuter);
                C.DistSq       = DistSq;
            }
        }

        // ----- Point lights -----
        {
            TArray<UPointLightComponent*> PointComps;
            Actor->GetComponents<UPointLightComponent>(PointComps);
            for (UPointLightComponent* L : PointComps)
            {
                if (!L || !L->IsVisible())
                    continue;

                FVector Pos = L->GetComponentLocation();
                float DistSq = FVector::DistSquared(Pos, CameraPos);
                if (DistSq > MaxDistSq)
                    continue;

                if (bHasFrustum && !ViewFrustum.IntersectSphere(Pos, L->AttenuationRadius))
                    continue;

                FPointCandidate& C = PointCandidates.AddDefaulted_GetRef();
                C.Position = Pos;
                C.Color    = L->GetLightColor() * L->Intensity;
                C.Radius   = L->AttenuationRadius;
                C.DistSq   = DistSq;
            }
        }
    }

    // ----- sort by distance (closest first) -----
    SpotCandidates .Sort([](const FSpotCandidate&  A, const FSpotCandidate&  B) { return A.DistSq < B.DistSq; });
    PointCandidates.Sort([](const FPointCandidate& A, const FPointCandidate& B) { return A.DistSq < B.DistSq; });

    // ----- Spot: fill EnvParams (top N) -----
    int32 SpIdx = 0;
    for (const FSpotCandidate& C : SpotCandidates)
    {
        if (SpIdx >= MMD_ANIME_MAX_SPOT_LIGHTS) break;
        EnvParams.SpotLightPosition[SpIdx]      = FVector4f((FVector3f)C.Position, 0.0f);
        EnvParams.SpotLightDirection[SpIdx]     = FVector4f((FVector3f)C.Direction, 0.0f);
        EnvParams.SpotLightColor[SpIdx]         = FVector4f((FVector3f)C.Color, C.Color.A);
        EnvParams.SpotLightRadius[SpIdx]        = FVector4f(C.Radius, 0.0f, 0.0f, 0.0f);
        EnvParams.SpotLightInnerConeCos[SpIdx]  = FVector4f(C.InnerConeCos, 0.0f, 0.0f, 0.0f);
        EnvParams.SpotLightOuterConeCos[SpIdx]  = FVector4f(C.OuterConeCos, 0.0f, 0.0f, 0.0f);
        SpIdx++;
    }

    // ----- Point: fill EnvParams (top N) -----
    int32 PtIdx = 0;
    for (const FPointCandidate& C : PointCandidates)
    {
        if (PtIdx >= MMD_ANIME_MAX_POINT_LIGHTS) break;
        EnvParams.PointLightPosition[PtIdx] = FVector4f((FVector3f)C.Position, 0.0f);
        EnvParams.PointLightColor[PtIdx]    = FVector4f((FVector3f)C.Color, C.Color.A);
        EnvParams.PointLightRadius[PtIdx]   = FVector4f(C.Radius, 0.0f, 0.0f, 0.0f);
        PtIdx++;
    }

    int32 RtIdx = 0; // RectLight placeholder

    EnvParams.PointLightCount = FVector4f((float)PtIdx, 0.0f, 0.0f, 0.0f);
    EnvParams.SpotLightCount  = FVector4f((float)SpIdx, 0.0f, 0.0f, 0.0f);
    EnvParams.RectLightCount  = FVector4f((float)RtIdx, 0.0f, 0.0f, 0.0f);

    EnvParams.FogColor  = FVector4f(0.5f, 0.5f, 0.5f, 1.0f);
    EnvParams.FogParams = FVector4f(0.0f, 0.0f, 0.0f, 0.0f);

    bDataReady = (PtIdx + SpIdx + RtIdx) > 0 || bHasDirectional;

    // debug: log once when light config changes
    static int32 LastLoggedKey = -1;
    const int32 Key = (bHasDirectional ? 1 : 0) | (PtIdx << 8) | (SpIdx << 16);
    if (Key != LastLoggedKey)
    {
        LastLoggedKey = Key;
        UE_LOG(LogTemp, Warning, TEXT("[MMDAnimeToon] Lights collected -> Directional=%d Point=%d Spot=%d (bDataReady=%d)"),
            bHasDirectional ? 1 : 0, PtIdx, SpIdx, bDataReady ? 1 : 0);
    }
}

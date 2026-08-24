#include "FMMDAnimeLightViewExtension.h"
#include "Rendering/UMMDAnimeLightDataSubsystem.h"
#include "Rendering/UMMDShadowMapSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/EngineTypes.h"
#include "SceneView.h"

void FMMDAnimeLightViewExtension::SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView)
{
	// SetupView 已有准确的主相机位置、旋转与投影矩阵，可直接按真实视锥构造 CSM。
	// 只处理 ViewFamily 的第一个非 SceneCapture 视图，避免分屏/立体视图重复提交。
	if (InView.bIsSceneCapture ||
		(InViewFamily.Views.Num() > 0 && InViewFamily.Views[0] != &InView))
	{
		return;
	}

	// 1. 先提交方向光深度 CustomRenderPass（写 MMDShadowMapRT）+ 设置阴影相机基（写入 LightDataSubsystem）。
	//    必须在 CollectLightsForFrame 之前，保证同一帧的相机基被一起打包进 LightDataRT。
	if (UMMDShadowMapSubsystem* ShadowSubsystem = UMMDShadowMapSubsystem::Get())
	{
		ShadowSubsystem->UpdateShadowForFrame(&InViewFamily, &InView);
	}

	// 2. 收集场景灯光 + 阴影相机基，一起推到 render thread，本帧 BasePass 前写入 LightDataRT。
	if (UMMDAnimeLightDataSubsystem* Subsystem = UMMDAnimeLightDataSubsystem::Get())
	{
		Subsystem->CollectLightsForFrame();
	}
}

void FMMDAnimeLightViewExtension::PreRenderViewFamily_RenderThread(
	FRDGBuilder& GraphBuilder,
	FSceneViewFamily& InViewFamily)
{
	if (InViewFamily.Views.Num() == 0 || InViewFamily.Views[0]->bIsSceneCapture)
	{
		return;
	}

	if (UMMDAnimeLightDataSubsystem* Subsystem = UMMDAnimeLightDataSubsystem::Get())
	{
		Subsystem->WriteLightData_RenderThread(GraphBuilder);
	}
}

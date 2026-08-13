#include "FMMDAnimeLightViewExtension.h"
#include "Rendering/UMMDAnimeLightDataSubsystem.h"
#include "Rendering/UMMDShadowMapSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/EngineTypes.h"
#include "SceneView.h"

void FMMDAnimeLightViewExtension::SetupViewFamily(FSceneViewFamily& InViewFamily)
{
	// 只在主视角 ViewFamily 上注入阴影 CustomRenderPass 和收集灯光数据。
	//
	// 区分主视角 vs SceneCapture 的方法：
	// FSceneViewFamily 没有 bIsSceneCapture 字段，且 bIsMainViewFamily 只在 PIE/Game 视口才被设 true，
	// 编辑器视口里永远是 false，不能用。FSceneView::bIsSceneCapture 此时还没初始化（Views 还没建）。
	// 只能用 ViewFamily.SceneCaptureSource：构造默认值是 SCS_FinalColorLDR，
	// SceneCaptureComponent 在 SetupViewFamilyForSceneCapture 里会改成它自己的 CaptureSource
	// （SCS_SceneDepth / SCS_FinalColorLDR 等）。但 SCS_FinalColorLDR 也是 FinalColor 类 capture 的值，
	// 所以此判断会把 FinalColor 类 capture 也放过去（保守安全，宁可漏注也不能错注入导致 RDG 崩溃）。
	if (InViewFamily.SceneCaptureSource != ESceneCaptureSource::SCS_FinalColorLDR)
	{
		return;
	}

	// 1. 先提交方向光深度 CustomRenderPass（写 MMDShadowMapRT）+ 设置阴影相机基（写入 LightDataSubsystem）。
	//    必须在 CollectLightsForFrame 之前，保证同一帧的相机基被一起打包进 LightDataRT。
	if (UMMDShadowMapSubsystem* ShadowSubsystem = UMMDShadowMapSubsystem::Get())
	{
		ShadowSubsystem->UpdateShadowForFrame(&InViewFamily);
	}

	// 2. 收集场景灯光 + 阴影相机基，一起打包推到 render thread，本帧 PostOpaque 写入 LightDataRT。
	if (UMMDAnimeLightDataSubsystem* Subsystem = UMMDAnimeLightDataSubsystem::Get())
	{
		Subsystem->CollectLightsForFrame();
	}
}

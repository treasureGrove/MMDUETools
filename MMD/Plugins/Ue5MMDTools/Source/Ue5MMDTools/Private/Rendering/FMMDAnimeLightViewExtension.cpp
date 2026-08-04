#include "FMMDAnimeLightViewExtension.h"
#include "Rendering/UMMDAnimeLightDataSubsystem.h"
#include "Engine/Engine.h"

void FMMDAnimeLightViewExtension::SetupViewFamily(FSceneViewFamily& InViewFamily)
{
	if (UMMDAnimeLightDataSubsystem* Subsystem = UMMDAnimeLightDataSubsystem::Get())
	{
		Subsystem->CollectLightsForFrame();
	}
}

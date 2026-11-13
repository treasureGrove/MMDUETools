#include "Ue5MMDTools.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Paths.h"
#include "Rendering/FMMDAnimeLightViewExtension.h"
#include "Rendering/MMDEndfieldNativeBackend.h"
#include "SceneViewExtension.h"
#include "ShaderCore.h"
void FUe5MMDToolsModule::StartupModule()
{
    if (const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Ue5MMDTools")))
    {
        AddShaderSourceDirectoryMapping(TEXT("/Plugin/Ue5MMDTools"), FPaths::Combine(Plugin->GetBaseDir(), TEXT("Shaders")));
    }
    MMDEndfieldNativeBackend::Initialize();
    OnPostEngineInitHandle = FCoreDelegates::GetOnPostEngineInit().AddRaw(this, &FUe5MMDToolsModule::OnPostEngineInit);
}
void FUe5MMDToolsModule::OnPostEngineInit()
{
    MMDEndfieldNativeBackend::VerifyCookedShaderProtocol();
    if (!IsRunningCommandlet()) { LightViewExtension = FSceneViewExtensions::NewExtension<FMMDAnimeLightViewExtension>(); }
}
void FUe5MMDToolsModule::ShutdownModule()
{
    if (OnPostEngineInitHandle.IsValid()) { FCoreDelegates::GetOnPostEngineInit().Remove(OnPostEngineInitHandle); }
    LightViewExtension.Reset();
}
IMPLEMENT_MODULE(FUe5MMDToolsModule, Ue5MMDTools)

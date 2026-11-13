#pragma once
#include "Modules/ModuleManager.h"
class FUe5MMDToolsModule : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;
private:
    void OnPostEngineInit();
    TSharedPtr<class FMMDAnimeLightViewExtension> LightViewExtension;
    FDelegateHandle OnPostEngineInitHandle;
};

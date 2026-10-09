#include "Modules/ModuleManager.h"
#include "UObject/CoreRedirects.h"
class FMMDEditorToolsModule : public IModuleInterface
{
public:
    virtual void StartupModule() override
    {
        TArray<FCoreRedirect> Redirects;
        for (const TCHAR* ClassName : { TEXT("MMDFactory"), TEXT("MMDFunctionLibrary") })
        {
            Redirects.Emplace(ECoreRedirectFlags::Type_Class, FString(TEXT("/Script/MMD.")) + ClassName, FString(TEXT("/Script/MMDEditorTools.")) + ClassName);
        }
        FCoreRedirects::AddRedirectList(Redirects, TEXT("MMD editor module split"));
    }
};
IMPLEMENT_MODULE(FMMDEditorToolsModule, MMDEditorTools)

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "UObject/CoreRedirects.h"
#include "UObject/SoftObjectPath.h"
#include "Actors/AMMDActor.h"
#include "Animation/AGN_MMDSkeletalControl.h"
#include "Animation/AGN_MMDSkeletalControlEditor.h"
#include "Import/MMDModelDataAsset.h"
#include "Rendering/UMMDAnimeLightDataSubsystem.h"
#include "Rendering/UMMDShadowMapSubsystem.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMMDModuleBoundaryTest, "Ue5MMDTools.Import.ModuleBoundary",
    EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FMMDModuleBoundaryTest::RunTest(const FString& Parameters)
{
    // 已保存角色、动画节点和模型数据的运行时类型路径保持稳定。
    for (const UStruct* Type : {static_cast<const UStruct*>(AMMDActor::StaticClass()),
        static_cast<const UStruct*>(FAGN_MMDSkeletalControl::StaticStruct()),
        static_cast<const UStruct*>(UMMDModelDataAsset::StaticClass()),
        static_cast<const UStruct*>(UMMDSkeletalMeshUserData::StaticClass()),
        static_cast<const UStruct*>(UMMDAnimeLightDataSubsystem::StaticClass()),
        static_cast<const UStruct*>(UMMDShadowMapSubsystem::StaticClass())})
    {
        TestTrue(TEXT("运行时类型仍在原模块"), Type->GetPathName().StartsWith(TEXT("/Script/Ue5MMDTools.")));
    }
    const UClass* GraphNodeClass = UAnimGraphNode_MMDSkeletalControl::StaticClass();
    TestEqual(TEXT("编辑器节点移入编辑器模块"), GraphNodeClass->GetPathName(),
        FString(TEXT("/Script/Ue5MMDToolsEditor.AnimGraphNode_MMDSkeletalControl")));
    const FCoreRedirectObjectName OldName(TEXT("/Script/Ue5MMDTools.AnimGraphNode_MMDSkeletalControl"));
    const FCoreRedirectObjectName Redirected = FCoreRedirects::GetRedirectedName(ECoreRedirectFlags::Type_Class, OldName);
    TestEqual(TEXT("旧蓝图节点引用能重定向"), Redirected.ToString(), GraphNodeClass->GetPathName());
    FSoftClassPath LegacyClassPath(TEXT("/Script/Ue5MMDTools.AnimGraphNode_MMDSkeletalControl"));
    TestTrue(TEXT("旧软类引用执行引擎重定向"), LegacyClassPath.FixupCoreRedirects());
    TestEqual(TEXT("重定向后的旧类引用可实际加载"), LegacyClassPath.TryLoadClass<UObject>(),
        const_cast<UClass*>(GraphNodeClass));
    return true;
}

#endif

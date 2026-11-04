#include "UI/MMDImportSetting.h"

#include "Misc/ConfigCacheIni.h"
#include "Widgets/Layout/SWidgetSwitcher.h"

FReply MMDImportSetting::SelectWorkflowPage(int32 Index)
{
    if (!WorkflowSwitcher || Index < 0 || Index >= WorkflowSwitcher->GetNumWidgets()
        || WorkflowSwitcher->GetActiveWidgetIndex() == Index)
    {
        return FReply::Handled();
    }
    // 快速连续切页从当前指示条位置接续；实际内容立即可操作。
    NavigationStart = GetNavigationPosition();
    NavigationTarget = static_cast<float>(Index);
    WorkflowSwitcher->SetActiveWidgetIndex(Index);
    if (bUIAnimationsEnabled) PageTransition.Play(AsShared());
    else PageTransition.JumpToEnd();
    return FReply::Handled();
}

void MMDImportSetting::SetUIAnimationsEnabled(bool bEnabled, bool bSavePreference)
{
    bUIAnimationsEnabled = bEnabled;
    if (!bEnabled)
    {
        PageTransition.JumpToEnd();
        StatusTransition.JumpToEnd();
    }
    Invalidate(EInvalidateWidgetReason::Paint);
    if (bSavePreference)
    {
        GConfig->SetBool(TEXT("MMDToolPanel"), TEXT("EnableUIAnimations"), bEnabled, GEditorPerProjectIni);
        GConfig->Flush(false, GEditorPerProjectIni);
    }
}

float MMDImportSetting::GetNavigationPosition() const
{
    // 自动化和调用方可直接切换 Switcher，也应保持指示条正确。
    const float CurrentPage = WorkflowSwitcher ? static_cast<float>(WorkflowSwitcher->GetActiveWidgetIndex()) : 0.0f;
    if (!bUIAnimationsEnabled || CurrentPage != NavigationTarget) return CurrentPage;
    return FMath::Lerp(NavigationStart, NavigationTarget, PageTransition.GetLerp());
}

float MMDImportSetting::GetPageTransitionAlpha() const
{
    return bUIAnimationsEnabled ? PageTransition.GetLerp() : 1.0f;
}

float MMDImportSetting::GetStatusTransitionAlpha() const
{
    return bUIAnimationsEnabled ? StatusTransition.GetLerp() : 1.0f;
}

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"

class MMDViewPanel;

/**
 * MMD导入设置面板 - 处理模型导入和相关设置
 */
enum class EMMDMessageType : uint8
{
    Info,
    Warning,
    Error,
    Success  // 可以添加成功状态
};

class UE5MMDTOOLS_API MMDImportSetting : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(MMDImportSetting) {}
    SLATE_ARGUMENT(TSharedPtr<MMDViewPanel>, ViewPanel)
    SLATE_END_ARGS()

    /** 构造设置面板 */
    void Construct(const FArguments &InArgs);
        /** 显示导入进度 */
    void ShowImportProgress(const FString &Message,EMMDMessageType Type = EMMDMessageType::Info);

    static void ShowGlobalImportProgress(const FString &Message, EMMDMessageType Type = EMMDMessageType::Info);

private:
    /** 导入模型按钮点击事件 */
    FReply OnImportModelClicked();

    /** 导入MMD模型文件 */
    void ImportMMDModel();

    /** 导入静态网格模型 */
    void ImportStaticMesh();



private:
    /** 关联的视口面板 */
    TSharedPtr<MMDViewPanel> ViewPanel;

    /** 状态文本显示 */
    TSharedPtr<STextBlock> StatusText;
        
    // 🔧 添加静态成员，保存当前实例的弱引用
    static TWeakPtr<MMDImportSetting> CurrentInstance;
};

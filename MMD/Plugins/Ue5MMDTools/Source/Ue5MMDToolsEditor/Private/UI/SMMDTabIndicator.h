#pragma once

#include "UI/MMDToolPanelStyle.h"
#include "Rendering/DrawElements.h"
#include "Widgets/SLeafWidget.h"

template <>
struct TWidgetTypeTraits<class SMMDTabIndicator>
{
    static constexpr bool SupportsInvalidation() { return true; }
};

// 独立绘制指示条，切页过渡不改变按钮布局与命中区域。
class SMMDTabIndicator : public SLeafWidget
{
    SLATE_DECLARE_WIDGET(SMMDTabIndicator, SLeafWidget)

public:
    SLATE_BEGIN_ARGS(SMMDTabIndicator) : _Position(0.0f) {}
        SLATE_ATTRIBUTE(float, Position)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs)
    {
        Position.Assign(*this, InArgs._Position);
        SetVisibility(EVisibility::HitTestInvisible);
        SetCanTick(false);
    }

    virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(1, 3); }

    virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& CullingRect,
        FSlateWindowElementList& Elements, int32 LayerId, const FWidgetStyle& Style, bool bParentEnabled) const override
    {
        const float Width = Geometry.GetLocalSize().X;
        const FLinearColor Tint = Style.GetColorAndOpacityTint();
        FSlateDrawElement::MakeBox(Elements, LayerId, Geometry.ToPaintGeometry(),
            FCoreStyle::Get().GetBrush("WhiteBrush"), ESlateDrawEffect::None, MMDPanelStyle::Line * Tint);
        const FVector2D Size(FMath::Max(1.0f, Width / 3.0f - 4.0f), 3.0f);
        const FVector2D Offset(FMath::Clamp(Position.Get(), 0.0f, 2.0f) * Width / 3.0f + 2.0f, 0);
        FSlateDrawElement::MakeBox(Elements, LayerId + 1,
            Geometry.ToPaintGeometry(Size, FSlateLayoutTransform(Offset)),
            FCoreStyle::Get().GetBrush("WhiteBrush"), ESlateDrawEffect::None, MMDPanelStyle::Accent * Tint);
        return LayerId + 1;
    }

protected:
    SMMDTabIndicator();

private:
    TSlateAttribute<float> Position;
};

#pragma once

#include "UI/MMDToolPanelStyle.h"
#include "Rendering/DrawElements.h"
#include "Widgets/SLeafWidget.h"

class SMMDStudioMark : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SMMDStudioMark) {} SLATE_END_ARGS()
	void Construct(const FArguments&) {}
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(44, 44); }
	virtual int32 OnPaint(const FPaintArgs&, const FGeometry& Geometry, const FSlateRect&, FSlateWindowElementList& Elements,
		int32 Layer, const FWidgetStyle&, bool) const override
	{
		auto Stroke = [&](TArray<FVector2D> Points, const FLinearColor& Color, float Width = 1.2f)
		{
			FSlateDrawElement::MakeLines(Elements, Layer, Geometry.ToPaintGeometry(), Points, ESlateDrawEffect::None, Color, true, Width);
		};
		// 原创菱形星标用矢量绘制，DPI 缩放时保持清晰。
		Stroke({{22, 1}, {28, 16}, {43, 22}, {28, 28}, {22, 43}, {16, 28}, {1, 22}, {16, 16}, {22, 1}}, MMDPanelStyle::Accent, 1.8f);
		Stroke({{22, 9}, {35, 22}, {22, 35}, {9, 22}, {22, 9}}, MMDPanelStyle::Text);
		Stroke({{22, 3}, {22, 41}}, MMDPanelStyle::Accent);
		Stroke({{3, 22}, {41, 22}}, MMDPanelStyle::Accent);
		return Layer;
	}
};

class SMMDStudioHeaderArt : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SMMDStudioHeaderArt) {} SLATE_END_ARGS()
	void Construct(const FArguments&) {}
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D::ZeroVector; }
	virtual int32 OnPaint(const FPaintArgs&, const FGeometry& Geometry, const FSlateRect&, FSlateWindowElementList& Elements,
		int32 Layer, const FWidgetStyle&, bool) const override
	{
		const FVector2D Size = Geometry.GetLocalSize();
		FSlateDrawElement::MakeGradient(Elements, Layer, Geometry.ToPaintGeometry(),
			{{FVector2D(0, 0), MMDPanelStyle::Panel}, {FVector2D(Size.X * 0.55f, 0), MMDPanelStyle::Card},
			 {FVector2D(Size.X, 0), MMDPanelStyle::AccentSoft}}, Orient_Vertical);
		const float X = FMath::Max(Size.X - 290.0f, 0.0f);
		FSlateDrawElement::MakeLines(Elements, Layer + 1, Geometry.ToPaintGeometry(),
			TArray<FVector2D>{{0, Size.Y - 1}, {X, Size.Y - 1}, {X + 20, Size.Y - 15}, {Size.X - 16, Size.Y - 15}},
			ESlateDrawEffect::None, MMDPanelStyle::Line, true, 1.0f);
		return Layer + 1;
	}
};

#include "UI/SMMDTabIndicator.h"

SLATE_IMPLEMENT_WIDGET(SMMDTabIndicator)

void SMMDTabIndicator::PrivateRegisterAttributes(FSlateAttributeInitializer& AttributeInitializer)
{
    SLATE_ADD_MEMBER_ATTRIBUTE_DEFINITION(AttributeInitializer, Position, EInvalidateWidgetReason::Paint);
}

SMMDTabIndicator::SMMDTabIndicator()
    : Position(*this, 0.0f)
{
}

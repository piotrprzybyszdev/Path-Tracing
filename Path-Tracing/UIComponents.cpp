#include "UIComponents.h"

namespace PathTracing
{

void Content::SetLeftMargin(float value)
{
    m_LeftMargin = value;
}

void Content::ApplyLeftMargin() const
{
    ImGui::Dummy({ m_LeftMargin, 0.0f });
    ImGui::SameLine();
}

}

#pragma once

#include "imgui.h"

#include <span>
#include <string>

namespace PathTracing
{

class Content
{
public:
    virtual ~Content() = default;

    void SetLeftMargin(float value);

    virtual void Render() = 0;

protected:
    float m_LeftMargin = 0.0f;

protected:
    void ApplyLeftMargin() const;
};

template<typename O, typename T> class Options : public Content
{
public:
    Options(std::span<const O> options, T &value) : m_Options(options), m_Value(value)
    {
    }
    virtual ~Options() = default;

    virtual void Render() = 0;
    [[nodiscard]] bool HasChanged() const;

protected:
    const std::span<const O> m_Options;
    T &m_Value;
    bool m_Changed = false;
};

template<typename T> struct CheckboxOption
{
    const T Value;
    const char *Name;
    bool IsEnabled = false;
};

template<typename T> class CheckboxOptions : public Options<CheckboxOption<T>, T>
{
public:
    using Base = Options<CheckboxOption<T>, T>;

public:
    using Base::Options, Base::m_Options, Base::m_Value, Base::m_Changed, Base::ApplyLeftMargin;
    ~CheckboxOptions() override = default;

    void Render() override;
};

template<typename T> struct RadioOption
{
    const T Value;
    const char *Name;
};

template<typename T> class RadioOptions : public Options<RadioOption<T>, T>
{
public:
    using Base = Options<RadioOption<T>, T>;

public:
    using Base::Options, Base::m_Options, Base::m_Value, Base::m_Changed, Base::ApplyLeftMargin;
    ~RadioOptions() override = default;

    void Render() override;
};

template<typename T> struct ComboOption
{
    const T Value;
    const char *Name;
};

template<typename T> class ComboOptions : public Options<ComboOption<T>, T>
{
public:
    using Base = Options<ComboOption<T>, T>;

public:
    ComboOptions(std::span<const ComboOption<T>> options, T &value, const std::string &title)
        : Base(options, value), m_Current(options.front().Name), m_Title(title), m_Id("##" + title)
    {
        for (const auto &option : options)
            if (option.Value == value)
                m_Current = option.Name;
    }

    using Base::m_Options, Base::m_Value, Base::m_Changed, Base::ApplyLeftMargin;
    ~ComboOptions() override = default;

    void Render() override;

private:
    const std::string m_Title;
    const std::string m_Id;
    const char *m_Current;
};

template<typename T, size_t N> class Widget
{
public:
    Widget(const std::string &title, std::array<T, N> &&contents, float leftMargin, float topMargin);

    void Render();

    [[nodiscard]] std::span<const T> GetContents() const;

private:
    const std::string m_Title;
    std::array<T, N> m_Contents;
    float m_TopMargin;
};

class Tab
{
public:
    Tab(const char *name) : m_Name(name)
    {
    }
    virtual ~Tab() = default;

    void Render()
    {
        if (ImGui::BeginTabItem(m_Name))
        {
            RenderContent();
            ImGui::EndTabItem();
        }
    }

protected:
    virtual void RenderContent() = 0;

private:
    const char *m_Name;
};

template<typename T> void ComboOptions<T>::Render()
{
    for (const auto &option : m_Options)
        if (option.Value == m_Value)
            m_Current = option.Name;

    ApplyLeftMargin();
    ImGui::Text(m_Title.c_str());
    ImGui::SameLine();
    m_Changed = false;
    if (ImGui::BeginCombo(m_Id.c_str(), m_Current))
    {
        for (int i = 0; i < m_Options.size(); i++)
        {
            const auto &option = m_Options[i];

            ImGui::PushID(i);
            if (ImGui::Selectable(option.Name, m_Value == option.Value))
            {
                m_Value = option.Value;
                m_Current = option.Name;
                m_Changed = true;
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
}

template<typename O, typename T> bool Options<O, T>::HasChanged() const
{
    return m_Changed;
}

template<typename T> void CheckboxOptions<T>::Render()
{
    assert(m_Options.size() <= sizeof(T) * 8);

    m_Changed = false;
    for (int i = 0; i < m_Options.size(); i++)
    {
        const auto &option = m_Options[i];

        ImGui::PushID(i);
        bool isEnabled = m_Value & option.Value;
        ApplyLeftMargin();
        if (ImGui::Checkbox(option.Name, &isEnabled))
        {
            m_Value ^= option.Value;
            m_Changed = true;
        }
        ImGui::PopID();
    }
}

template<typename T> void RadioOptions<T>::Render()
{
    m_Changed = false;
    for (int i = 0; i < m_Options.size(); i++)
    {
        const auto &option = m_Options[i];

        ImGui::PushID(i);
        ApplyLeftMargin();
        if (ImGui::RadioButton(option.Name, option.Value == m_Value))
        {
            m_Value = option.Value;
            m_Changed = true;
        }
        ImGui::PopID();
    }
}

template<typename T, size_t N>
Widget<T, N>::Widget(const std::string &title, std::array<T, N> &&contents, float leftMargin, float topMargin)
    : m_Title(title), m_Contents(std::move(contents)), m_TopMargin(topMargin)
{
    for (auto &content : m_Contents)
        content.SetLeftMargin(leftMargin);
}

template<typename T, size_t N> void Widget<T, N>::Render()
{
    ImGui::Dummy({ 0.0f, m_TopMargin });
    ImGui::Text(m_Title.c_str());
    ImGui::Dummy({ 0.0f, 2.0f });
    for (auto &content : m_Contents)
        content.Render();
}

template<typename T, size_t N> std::span<const T> Widget<T, N>::GetContents() const
{
    return m_Contents;
}

}

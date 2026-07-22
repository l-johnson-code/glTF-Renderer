#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "imgui.h"

namespace ImGui {

bool BitflagCheckbox(const char* label, uint32_t* bits, uint32_t flag)
{
	bool temp_bool = *bits & flag;
	bool result = ImGui::Checkbox(label, &temp_bool);
	*bits = temp_bool ? *bits | flag : *bits & ~flag;
	return result;
}

bool BeginEnum(const char* label, int* value, const char** strings, int num_of_strings, int* values = nullptr)
{
	int current_value = 0;
	if (values) {
		for (int i = 0; i < num_of_strings; i++) {
			if (values[i] == *value) {
				current_value = i;
				break;
			}
		}
	} else {
		current_value = *value;
	}
	return ImGui::BeginCombo(label, strings[current_value]);
}

bool AddEnumItem(int i, int* value, const char** strings, int* values = nullptr)
{
	bool value_changed = false;
	bool is_selected = values ? values[i] == *value : i == *value;
	if (ImGui::Selectable(strings[i], &is_selected)) {
		value_changed = values ? values[i] != *value : i != *value;
		*value = values ? values[i] : i;
	}
	return value_changed;
}

void EndEnum()
{
	ImGui::EndCombo();
}

bool Enum(const char* label, int* value, const char** strings, int num_of_strings, int* values = nullptr)
{
	bool value_changed = false;
	if (BeginEnum(label, value, strings, num_of_strings, values)) {
		for (int i = 0; i < num_of_strings; i++) {
			value_changed |= AddEnumItem(i, value, strings, values);
		}
		EndEnum();
	}
	return value_changed;
}

template<typename T, int N>
bool Enum(const char* label, T* value, const char* (&strings)[N], int* values = nullptr)
{
	int cast = static_cast<int>(*value);
	bool result = Enum(label, &cast, strings, N, values);
	*value = (T)cast;
	return result;
}

bool BeginSection(const char* label)
{
	bool result = ImGui::CollapsingHeader(label);
	if (result) {
		ImGui::PushID(label);
	}
	return result;
}

void EndSection()
{
	ImGui::PopID();
}

template<typename T, typename G, typename S>
bool InputFloat(const char* label, T* object, G getter, S setter, ImGuiInputFlags flags = ImGuiInputTextFlags_None)
{
	float v = (object->*getter)();
	bool value_changed = ImGui::InputFloat(label, &v, 0.0f, 0.0f, "%.3f", flags);
	if (value_changed) {
		(object->*setter)(v);
	}
	return value_changed;
}

bool Input(const char* label, glm::vec3* v, ImGuiInputFlags flags = ImGuiInputTextFlags_None)
{
	return ImGui::InputFloat3(label, &v->x, "%.3f", flags);
}

bool Input(const char* label, glm::vec4* v, ImGuiInputFlags flags = ImGuiInputTextFlags_None)
{
	return ImGui::InputFloat4(label, &v->x, "%.3f", flags);
}

bool Input(const char* label, glm::quat* v, ImGuiInputFlags flags = ImGuiInputTextFlags_None)
{
	return ImGui::InputFloat4(label, &v->x, "%.3f", flags);
}

bool ColorEdit(const char* label, glm::vec3* col)
{
	return ImGui::ColorEdit3(label, &col->x);
}

bool ColorPicker(const char* label, glm::vec3* col)
{
	return ImGui::ColorPicker3(label, &col->x);
}

}
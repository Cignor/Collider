#pragma once

#include <imgui.h>
#include <map>
#include <juce_core/juce_core.h>

#include "../../audio/modules/ModuleProcessor.h" // For PinDataType

// Shared module category enum for theming (mirrors UI categories)
enum class ModuleCategory { Source, Effect, Modulator, Utility, Seq, MIDI, Analysis, TTS_Voice, Special_Exp, OpenCV, Sys, Comment, Plugin, Default };

struct TriStateColor
{
	ImU32 base { 0 };
	ImU32 hovered { 0 };
	ImU32 active { 0 };
};

struct Theme
{
	// ImGui base style (padding/rounding etc.)
	ImGuiStyle style {};

	// Global accent color used across highlights
	ImVec4 accent { 0.0f, 0.8f, 1.0f, 1.0f };

	struct TextColors
	{
		ImVec4 section_header { 0.7f, 0.7f, 0.7f, 1.0f };
		ImVec4 warning { 1.0f, 0.8f, 0.0f, 1.0f };
		ImVec4 success { 0.0f, 1.0f, 0.0f, 1.0f };
		ImVec4 error { 1.0f, 0.5f, 0.0f, 1.0f };
		ImVec4 disabled { 100/255.f, 100/255.f, 100/255.f, 1.0f };
		ImVec4 active { 100/255.f, 255/255.f, 100/255.f, 1.0f };
		float tooltip_wrap_standard { 35.0f };
		float tooltip_wrap_compact { 25.0f };
	} text;

	struct StatusColors
	{
		ImVec4 edited { 1.0f, 1.0f, 0.0f, 1.0f };
		ImVec4 saved { 0.0f, 1.0f, 0.0f, 1.0f };
	} status;

	struct HeaderColors
	{
		TriStateColor recent {};
		TriStateColor samples {};
		TriStateColor presets {};
		TriStateColor system {};
	} headers;

	struct ImNodesColors
	{
		std::map<ModuleCategory, ImU32> category_colors;
		std::map<PinDataType, ImU32> pin_colors;
		ImU32 pin_connected { 0 };
		ImU32 pin_disconnected { 0 };
		ImU32 node_muted { 0 };
		float node_muted_alpha { 0.5f };
		ImU32 node_hovered_link_highlight { 0 };
	} imnodes;

	struct LinkColors
	{
		ImU32 link_hovered { 0 };
		ImU32 link_selected { 0 };
		ImU32 link_highlighted { 0 };
		ImU32 preview_color { 0 };
		float preview_width { 3.0f };
		ImU32 label_background { 0 };
		ImU32 label_text { 0 };
	} links;

	struct CanvasColors
	{
		ImU32 grid_color { 0 };
		ImU32 grid_origin_color { 0 };
		float grid_size { 64.0f };
		ImU32 scale_text_color { 0 };
		float scale_interval { 400.0f };
		ImU32 drop_target_overlay { 0 };
		ImU32 mouse_position_text { 0 };
	} canvas;

	struct LayoutSettings
	{
		float sidebar_width { 260.0f };
		float window_padding { 10.0f };
		float node_vertical_padding { 50.0f };
		float preset_vertical_padding { 100.0f };
		float node_default_width { 240.0f };
		ImVec2 node_default_padding { 8.0f, 8.0f };
		ImVec2 node_muted_padding { 8.0f, 8.0f };
	} layout;

	struct FontSettings
	{
		float default_size { 16.0f };
		juce::String default_path; // empty = ImGui default
		float chinese_size { 16.0f };
		juce::String chinese_path { "../../Source/assets/NotoSansSC-VariableFont_wght.ttf" };
	} fonts;

	struct WindowSettings
	{
		float status_overlay_alpha { 0.5f };
		float probe_scope_alpha { 0.85f };
		float preset_status_alpha { 0.7f };
		float notifications_alpha { 0.92f };
		float probe_scope_width { 260.0f };
		float probe_scope_height { 180.0f };
	} windows;

	struct ModulationColors
	{
		ImVec4 frequency { 0.4f, 0.8f, 1.0f, 1.0f };
		ImVec4 timbre { 1.0f, 0.8f, 0.4f, 1.0f };
		ImVec4 amplitude { 1.0f, 0.4f, 1.0f, 1.0f };
		ImVec4 filter { 0.4f, 1.0f, 0.4f, 1.0f };
	} modulation;

	struct MeterColors
	{
		ImVec4 safe { 0.2f, 0.8f, 0.2f, 1.0f };
		ImVec4 warning { 0.9f, 0.7f, 0.0f, 1.0f };
		ImVec4 clipping { 0.9f, 0.2f, 0.2f, 1.0f };
	} meters;

	struct TimelineColors
	{
		ImU32 marker_start_end { 0 };
		ImU32 marker_gate { 0 };
		ImU32 marker_trigger { 0 };
	} timeline;

	struct ModuleColors
	{
		ImVec4 videofx_section_header { 0.7f, 0.7f, 0.7f, 1.0f };
		ImVec4 videofx_section_subheader { 0.9f, 0.9f, 0.5f, 1.0f };
		ImVec4 scope_section_header { 0.7f, 0.7f, 0.7f, 1.0f };
		ImU32 scope_plot_bg { 0 };
		ImU32 scope_plot_fg { 0 };
		ImU32 scope_plot_max { 0 };
		ImU32 scope_plot_min { 0 };
		ImVec4 scope_text_max { 1.0f, 0.3f, 0.3f, 1.0f };
		ImVec4 scope_text_min { 1.0f, 0.86f, 0.31f, 1.0f };
		ImU32 stroke_seq_border { 0 };
		ImU32 stroke_seq_canvas_bg { 0 };
		ImU32 stroke_seq_line_inactive { 0 };
		ImU32 stroke_seq_line_active { 0 };
		ImU32 stroke_seq_playhead { 0 };
		ImU32 stroke_seq_thresh_floor { 0 };
		ImU32 stroke_seq_thresh_mid { 0 };
		ImU32 stroke_seq_thresh_ceil { 0 };
		ImVec4 stroke_seq_frame_bg { 0.3f, 0.28f, 0.1f, 0.7f };
		ImVec4 stroke_seq_frame_bg_hovered { 0.4f, 0.38f, 0.15f, 0.8f };
		ImVec4 stroke_seq_frame_bg_active { 0.5f, 0.48f, 0.2f, 0.9f };
	} modules;
};



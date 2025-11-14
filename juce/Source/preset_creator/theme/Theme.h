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
		bool enable_text_glow { false };
		ImVec4 text_glow_color { 0.0f, 0.0f, 0.0f, 0.5f };
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
		// Canvas background (drawn behind grid)
		ImU32 canvas_background { 0 };
		// Grid settings (custom drawn)
		ImU32 grid_color { 0 };
		ImU32 grid_origin_color { 0 };
		float grid_size { 64.0f };
		ImU32 scale_text_color { 0 };
		float scale_interval { 400.0f };
		ImU32 drop_target_overlay { 0 };
		ImU32 mouse_position_text { 0 };
		// Node styling (ImNodes)
		ImU32 node_background { 0 };
		ImU32 node_frame { 0 };
		ImU32 node_frame_hovered { 0 };
		ImU32 node_frame_selected { 0 };
		float node_rounding { 0.0f };
		float node_border_width { 0.0f };
		ImU32 selection_rect { 0 };
		ImU32 selection_rect_outline { 0 };
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
		ImVec4 sequencer_section_header { 0.5f, 1.0f, 0.7f, 1.0f };
		ImVec4 sequencer_step_active_frame { 0.3f, 0.7f, 1.0f, 1.0f };
		ImVec4 sequencer_step_active_grab { 0.9f, 0.9f, 0.9f, 1.0f };
		ImVec4 sequencer_gate_active_frame { 1.0f, 0.7f, 0.3f, 1.0f };
		ImU32 sequencer_threshold_line { IM_COL32(255, 255, 0, 200) };
		ImVec4 stroke_seq_title { 0.9f, 0.95f, 0.2f, 1.0f };
		ImVec4 stroke_seq_section { 0.85f, 0.9f, 0.3f, 1.0f };
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
		
		struct FrequencyGraphColors
		{
			ImU32 background { IM_COL32(20, 22, 24, 255) };
			ImU32 grid { IM_COL32(50, 55, 60, 255) };
			ImU32 label { IM_COL32(150, 150, 150, 255) };
			ImU32 peak_line { IM_COL32(255, 150, 80, 150) };
			ImU32 live_line { IM_COL32(120, 170, 255, 220) };
			ImU32 border { IM_COL32(80, 80, 80, 255) };
			ImU32 threshold { IM_COL32(255, 100, 100, 150) };
		} frequency_graph;
		
		struct PhysicsColors
		{
			ImVec4 sandbox_title { 0.3f, 0.9f, 1.0f, 1.0f };
			ImVec4 stroke_label { 0.9f, 0.9f, 0.5f, 1.0f };
			ImVec4 physics_section { 0.7f, 0.9f, 0.7f, 1.0f };
			ImVec4 spawn_section { 0.5f, 0.9f, 1.0f, 1.0f };
			ImVec4 count_ok { 0.6f, 0.9f, 0.6f, 1.0f };
			ImVec4 count_warn { 1.0f, 0.9f, 0.4f, 1.0f };
			ImVec4 count_alert { 1.0f, 0.4f, 0.4f, 1.0f };
			ImVec4 stroke_metal { 0.53f, 0.81f, 0.92f, 1.0f };
			ImVec4 stroke_wood { 0.96f, 0.64f, 0.38f, 1.0f };
			ImVec4 stroke_soil { 0.0f, 0.39f, 0.0f, 1.0f };
			ImVec4 stroke_conveyor { 0.58f, 0.44f, 0.86f, 1.0f };
			ImVec4 stroke_bouncy { 0.0f, 0.98f, 0.6f, 1.0f };
			ImVec4 stroke_sticky { 0.54f, 0.27f, 0.07f, 1.0f };
			ImVec4 stroke_emitter { 1.0f, 0.84f, 0.0f, 1.0f };
			ImVec4 spawn_ball { 1.0f, 0.4f, 0.4f, 1.0f };
			ImVec4 spawn_square { 0.4f, 1.0f, 0.4f, 1.0f };
			ImVec4 spawn_triangle { 0.4f, 0.4f, 1.0f, 1.0f };
			ImVec4 spawn_vortex { 0.7f, 0.4f, 1.0f, 1.0f };
			ImVec4 spawn_clear { 0.6f, 0.2f, 0.2f, 0.8f };
			ImVec4 spawn_clear_hover { 0.8f, 0.3f, 0.3f, 1.0f };
			ImVec4 spawn_clear_active { 1.0f, 0.4f, 0.4f, 1.0f };
			ImVec4 canvas_background { 0.12f, 0.12f, 0.12f, 1.0f };
			ImVec4 canvas_border { 0.39f, 0.39f, 0.39f, 1.0f };
			ImVec4 drag_indicator_fill { 1.0f, 1.0f, 0.0f, 0.4f };
			ImVec4 drag_indicator_outline { 1.0f, 1.0f, 0.0f, 0.8f };
			ImVec4 eraser_fill { 1.0f, 0.39f, 0.39f, 0.24f };
			ImVec4 eraser_outline { 0.86f, 0.08f, 0.08f, 0.7f };
			ImVec4 crosshair_idle { 1.0f, 1.0f, 1.0f, 0.5f };
			ImVec4 crosshair_active { 1.0f, 1.0f, 0.0f, 1.0f };
			ImVec4 magnet_north { 1.0f, 0.39f, 0.39f, 0.78f };
			ImVec4 magnet_south { 0.39f, 0.39f, 1.0f, 0.78f };
			ImVec4 magnet_link { 1.0f, 1.0f, 0.0f, 0.78f };
			ImVec4 vector_outline { 1.0f, 1.0f, 1.0f, 0.78f };
			ImVec4 vector_fill { 1.0f, 1.0f, 1.0f, 0.6f };
			ImVec4 soil_detail { 0.55f, 0.27f, 0.07f, 0.7f };
			ImVec4 overlay_text { 0.0f, 0.0f, 0.0f, 0.78f };
			ImVec4 overlay_line { 1.0f, 1.0f, 1.0f, 0.5f };
			ImVec4 separator_line { 1.0f, 0.84f, 0.0f, 0.78f };
		} physics;
		
		// PanVol module settings
		float panvol_node_width { 180.0f };  // Custom compact width (default 180px)
		ImU32 panvol_grid_background { IM_COL32(20, 20, 20, 255) };
		ImU32 panvol_grid_border { IM_COL32(100, 100, 100, 255) };
		ImU32 panvol_grid_lines { IM_COL32(50, 50, 50, 255) };
		ImU32 panvol_crosshair { IM_COL32(80, 80, 80, 200) };
		ImU32 panvol_circle_manual { IM_COL32(255, 200, 100, 255) };  // Orange when manual
		ImU32 panvol_circle_modulated { IM_COL32(100, 200, 255, 255) };  // Cyan when modulated
		ImU32 panvol_label_text { IM_COL32(150, 150, 150, 200) };
		ImU32 panvol_value_text { IM_COL32(100, 100, 100, 120) };
	} modules;
};



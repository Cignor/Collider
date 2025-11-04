#include "ThemeManager.h"

#include <imgui.h>
#include <imnodes.h>
#include <map>

// Singleton
ThemeManager& ThemeManager::getInstance()
{
	static ThemeManager instance;
	return instance;
}

ThemeManager::ThemeManager()
{
	loadDefaultTheme();
	currentTheme = defaultTheme;
}

void ThemeManager::applyTheme()
{
	applyImGuiStyle();
	// Apply accent to common ImGui colors
	ImVec4 acc = currentTheme.accent;
	ImGuiStyle& st = ImGui::GetStyle();
	st.Colors[ImGuiCol_CheckMark] = acc;
	st.Colors[ImGuiCol_SliderGrabActive] = acc;
	st.Colors[ImGuiCol_TextSelectedBg] = ImVec4(acc.x, acc.y, acc.z, 0.35f);
	st.Colors[ImGuiCol_DragDropTarget] = ImVec4(acc.x, acc.y, acc.z, 0.95f);
	st.Colors[ImGuiCol_SeparatorHovered] = ImVec4(acc.x, acc.y, acc.z, 0.9f);
	st.Colors[ImGuiCol_TabHovered] = ImVec4(acc.x, acc.y, acc.z, 0.8f);
	st.Colors[ImGuiCol_ButtonHovered] = ImVec4(acc.x, acc.y, acc.z, 1.0f);
	// Note: ImNodes colors are applied per-draw via PushColorStyle, not here
}

void ThemeManager::resetToDefault()
{
	currentTheme = defaultTheme;
	applyTheme();
}

const Theme& ThemeManager::getCurrentTheme() const
{
	return currentTheme;
}

ImU32 ThemeManager::getCategoryColor(ModuleCategory cat, bool hovered)
{
	auto it = currentTheme.imnodes.category_colors.find(cat);
	ImU32 base = (it != currentTheme.imnodes.category_colors.end())
		? it->second
		: IM_COL32(70, 70, 70, 255);

	if (hovered)
	{
		ImVec4 c = ImGui::ColorConvertU32ToFloat4(base);
		c.x = juce::jmin(c.x * 1.3f, 1.0f);
		c.y = juce::jmin(c.y * 1.3f, 1.0f);
		c.z = juce::jmin(c.z * 1.3f, 1.0f);
		return ImGui::ColorConvertFloat4ToU32(c);
	}
	return base;
}

ImU32 ThemeManager::getPinColor(PinDataType type)
{
	auto it = currentTheme.imnodes.pin_colors.find(type);
	if (it != currentTheme.imnodes.pin_colors.end())
		return it->second;
	return IM_COL32(150, 150, 150, 255);
}

ImU32 ThemeManager::getPinConnectedColor()
{
	return currentTheme.imnodes.pin_connected;
}

ImU32 ThemeManager::getPinDisconnectedColor()
{
	return currentTheme.imnodes.pin_disconnected;
}

float ThemeManager::getSidebarWidth() const
{
	return currentTheme.layout.sidebar_width;
}

float ThemeManager::getNodeDefaultWidth() const
{
	return currentTheme.layout.node_default_width;
}

float ThemeManager::getWindowPadding() const
{
	return currentTheme.layout.window_padding;
}

// JSON save/load (Phase 5)
bool ThemeManager::loadTheme(const juce::File& themeFile)
{
	if (!themeFile.existsAsFile())
		return false;
	juce::var parsed = juce::JSON::parse(themeFile);
	if (parsed.isVoid() || !parsed.isObject())
		return false;
	auto* root = parsed.getDynamicObject();
	Theme t = defaultTheme; // start from defaults

	// Load ImGui style from JSON
	if (auto styleVar = root->getProperty("style"); styleVar.isObject())
	{
		auto* styleObj = styleVar.getDynamicObject();
		
		// Load style properties
		auto loadVec2 = [&](const char* name, ImVec2& dst)
		{
			if (auto v = styleObj->getProperty(name); v.isArray())
			{
				auto* arr = v.getArray();
				if (arr->size() >= 2)
					dst = ImVec2((float) arr->getReference(0), (float) arr->getReference(1));
			}
		};
		
		auto loadFloat = [&](const char* name, float& dst)
		{
			if (styleObj->hasProperty(name))
				dst = (float) styleObj->getProperty(name);
		};
		
		loadVec2("WindowPadding", t.style.WindowPadding);
		loadVec2("FramePadding", t.style.FramePadding);
		loadVec2("ItemSpacing", t.style.ItemSpacing);
		loadVec2("ItemInnerSpacing", t.style.ItemInnerSpacing);
		loadFloat("WindowRounding", t.style.WindowRounding);
		loadFloat("ChildRounding", t.style.ChildRounding);
		loadFloat("FrameRounding", t.style.FrameRounding);
		loadFloat("PopupRounding", t.style.PopupRounding);
		loadFloat("ScrollbarRounding", t.style.ScrollbarRounding);
		loadFloat("GrabRounding", t.style.GrabRounding);
		loadFloat("TabRounding", t.style.TabRounding);
		loadFloat("WindowBorderSize", t.style.WindowBorderSize);
		loadFloat("FrameBorderSize", t.style.FrameBorderSize);
		loadFloat("PopupBorderSize", t.style.PopupBorderSize);
		
		// Load ImGui colors - map string names to ImGuiCol enum
		if (auto colorsVar = styleObj->getProperty("Colors"); colorsVar.isObject())
		{
			auto* colorsObj = colorsVar.getDynamicObject();
			static std::map<juce::String, ImGuiCol> colorMap = {
				{ "Text", ImGuiCol_Text }, { "TextDisabled", ImGuiCol_TextDisabled },
				{ "WindowBg", ImGuiCol_WindowBg }, { "ChildBg", ImGuiCol_ChildBg },
				{ "PopupBg", ImGuiCol_PopupBg }, { "Border", ImGuiCol_Border },
				{ "BorderShadow", ImGuiCol_BorderShadow }, { "FrameBg", ImGuiCol_FrameBg },
				{ "FrameBgHovered", ImGuiCol_FrameBgHovered }, { "FrameBgActive", ImGuiCol_FrameBgActive },
				{ "TitleBg", ImGuiCol_TitleBg }, { "TitleBgActive", ImGuiCol_TitleBgActive },
				{ "TitleBgCollapsed", ImGuiCol_TitleBgCollapsed }, { "MenuBarBg", ImGuiCol_MenuBarBg },
				{ "ScrollbarBg", ImGuiCol_ScrollbarBg }, { "ScrollbarGrab", ImGuiCol_ScrollbarGrab },
				{ "ScrollbarGrabHovered", ImGuiCol_ScrollbarGrabHovered }, { "ScrollbarGrabActive", ImGuiCol_ScrollbarGrabActive },
				{ "CheckMark", ImGuiCol_CheckMark }, { "SliderGrab", ImGuiCol_SliderGrab },
				{ "SliderGrabActive", ImGuiCol_SliderGrabActive }, { "Button", ImGuiCol_Button },
				{ "ButtonHovered", ImGuiCol_ButtonHovered }, { "ButtonActive", ImGuiCol_ButtonActive },
				{ "Header", ImGuiCol_Header }, { "HeaderHovered", ImGuiCol_HeaderHovered },
				{ "HeaderActive", ImGuiCol_HeaderActive }, { "Separator", ImGuiCol_Separator },
				{ "SeparatorHovered", ImGuiCol_SeparatorHovered }, { "SeparatorActive", ImGuiCol_SeparatorActive },
				{ "ResizeGrip", ImGuiCol_ResizeGrip }, { "ResizeGripHovered", ImGuiCol_ResizeGripHovered },
				{ "ResizeGripActive", ImGuiCol_ResizeGripActive }, { "Tab", ImGuiCol_Tab },
				{ "TabHovered", ImGuiCol_TabHovered }, { "TabActive", ImGuiCol_TabActive },
				{ "TabUnfocused", ImGuiCol_TabUnfocused }, { "TabUnfocusedActive", ImGuiCol_TabUnfocusedActive },
				{ "PlotLines", ImGuiCol_PlotLines }, { "PlotLinesHovered", ImGuiCol_PlotLinesHovered },
				{ "PlotHistogram", ImGuiCol_PlotHistogram }, { "PlotHistogramHovered", ImGuiCol_PlotHistogramHovered },
				{ "TableHeaderBg", ImGuiCol_TableHeaderBg }, { "TableBorderStrong", ImGuiCol_TableBorderStrong },
				{ "TableBorderLight", ImGuiCol_TableBorderLight }, { "TableRowBg", ImGuiCol_TableRowBg },
				{ "TableRowBgAlt", ImGuiCol_TableRowBgAlt }, { "TextSelectedBg", ImGuiCol_TextSelectedBg },
				{ "DragDropTarget", ImGuiCol_DragDropTarget }, { "NavHighlight", ImGuiCol_NavHighlight },
				{ "NavWindowingHighlight", ImGuiCol_NavWindowingHighlight }, { "NavWindowingDimBg", ImGuiCol_NavWindowingDimBg },
				{ "ModalWindowDimBg", ImGuiCol_ModalWindowDimBg }
			};
			
			for (auto& prop : colorsObj->getProperties())
			{
				auto it = colorMap.find(prop.name.toString());
				if (it != colorMap.end())
				{
					t.style.Colors[it->second] = varToVec4(prop.value, t.style.Colors[it->second]);
				}
			}
		}
	}

	// accent
	if (auto v = root->getProperty("accent"); v.isArray())
	{
		t.accent = varToVec4(v, t.accent);
	}

	// text
	if (auto v = root->getProperty("text"); v.isObject())
	{
		auto* o = v.getDynamicObject();
		t.text.section_header = varToVec4(o->getProperty("section_header"), t.text.section_header);
		t.text.warning = varToVec4(o->getProperty("warning"), t.text.warning);
		t.text.success = varToVec4(o->getProperty("success"), t.text.success);
		t.text.error = varToVec4(o->getProperty("error"), t.text.error);
		t.text.disabled = varToVec4(o->getProperty("disabled"), t.text.disabled);
		t.text.active = varToVec4(o->getProperty("active"), t.text.active);
		// Floats with default fallbacks
		if (o->hasProperty("tooltip_wrap_standard")) t.text.tooltip_wrap_standard = (float) o->getProperty("tooltip_wrap_standard");
		if (o->hasProperty("tooltip_wrap_compact"))  t.text.tooltip_wrap_compact  = (float) o->getProperty("tooltip_wrap_compact");
	}

	// status
	if (auto v = root->getProperty("status"); v.isObject())
	{
		auto* o = v.getDynamicObject();
		t.status.edited = varToVec4(o->getProperty("edited"), t.status.edited);
		t.status.saved = varToVec4(o->getProperty("saved"), t.status.saved);
	}

	// headers (TriState)
	if (auto v = root->getProperty("headers"); v.isObject())
	{
		auto* o = v.getDynamicObject();
		auto loadTri = [&](const char* name, TriStateColor& dst)
		{
			if (auto* h = o->getProperty(name).getDynamicObject())
			{
				dst.base = varToColor(h->getProperty("base"), dst.base);
				dst.hovered = varToColor(h->getProperty("hovered"), dst.hovered);
				dst.active = varToColor(h->getProperty("active"), dst.active);
			}
		};
		loadTri("recent", t.headers.recent);
		loadTri("samples", t.headers.samples);
		loadTri("presets", t.headers.presets);
		loadTri("system", t.headers.system);
	}

	// imnodes.category_colors
	if (auto v = root->getProperty("imnodes"); v.isObject())
	{
		auto* o = v.getDynamicObject();
		if (auto cats = o->getProperty("category_colors"); cats.isObject())
		{
			auto* m = cats.getDynamicObject();
			for (auto& p : m->getProperties())
			{
				ModuleCategory catEnum;
				bool ok = stringToModuleCategory(p.name.toString(), catEnum);
				if (!ok)
				{
					int id = p.name.toString().getIntValue();
					catEnum = (ModuleCategory) id;
					ok = true;
				}
				if (ok)
					t.imnodes.category_colors[catEnum] = varToColor(p.value, t.imnodes.category_colors[catEnum]);
			}
		}
		if (auto pins = o->getProperty("pin_colors"); pins.isObject())
		{
			auto* m = pins.getDynamicObject();
			for (auto& p : m->getProperties())
			{
				PinDataType tp;
				bool ok = stringToPinType(p.name.toString(), tp);
				if (!ok)
				{
					int id = p.name.toString().getIntValue();
					tp = (PinDataType) id;
					ok = true;
				}
				if (ok)
					t.imnodes.pin_colors[tp] = varToColor(p.value, t.imnodes.pin_colors[tp]);
			}
		}
		t.imnodes.pin_connected = varToColor(o->getProperty("pin_connected"), t.imnodes.pin_connected);
		t.imnodes.pin_disconnected = varToColor(o->getProperty("pin_disconnected"), t.imnodes.pin_disconnected);
		t.imnodes.node_muted = varToColor(o->getProperty("node_muted"), t.imnodes.node_muted);
		t.imnodes.node_muted_alpha = o->hasProperty("node_muted_alpha") ? (float) o->getProperty("node_muted_alpha") : t.imnodes.node_muted_alpha;
		t.imnodes.node_hovered_link_highlight = varToColor(o->getProperty("node_hovered_link_highlight"), t.imnodes.node_hovered_link_highlight);
	}

	// links
	if (auto v = root->getProperty("links"); v.isObject())
	{
		auto* o = v.getDynamicObject();
		t.links.link_hovered = varToColor(o->getProperty("link_hovered"), t.links.link_hovered);
		t.links.link_selected = varToColor(o->getProperty("link_selected"), t.links.link_selected);
		t.links.link_highlighted = varToColor(o->getProperty("link_highlighted"), t.links.link_highlighted);
		t.links.preview_color = varToColor(o->getProperty("preview_color"), t.links.preview_color);
		t.links.preview_width = o->hasProperty("preview_width") ? (float) o->getProperty("preview_width") : t.links.preview_width;
		t.links.label_background = varToColor(o->getProperty("label_background"), t.links.label_background);
		t.links.label_text = varToColor(o->getProperty("label_text"), t.links.label_text);
	}

	// canvas
	if (auto v = root->getProperty("canvas"); v.isObject())
	{
		auto* o = v.getDynamicObject();
		t.canvas.grid_color = varToColor(o->getProperty("grid_color"), t.canvas.grid_color);
		t.canvas.grid_origin_color = varToColor(o->getProperty("grid_origin_color"), t.canvas.grid_origin_color);
		t.canvas.grid_size = o->hasProperty("grid_size") ? (float) o->getProperty("grid_size") : t.canvas.grid_size;
		t.canvas.scale_text_color = varToColor(o->getProperty("scale_text_color"), t.canvas.scale_text_color);
		t.canvas.scale_interval = o->hasProperty("scale_interval") ? (float) o->getProperty("scale_interval") : t.canvas.scale_interval;
		t.canvas.drop_target_overlay = varToColor(o->getProperty("drop_target_overlay"), t.canvas.drop_target_overlay);
		t.canvas.mouse_position_text = varToColor(o->getProperty("mouse_position_text"), t.canvas.mouse_position_text);
	}

	// layout
	if (auto v = root->getProperty("layout"); v.isObject())
	{
		auto* o = v.getDynamicObject();
		// layout floats
		if (o->hasProperty("sidebar_width"))          t.layout.sidebar_width = (float) o->getProperty("sidebar_width");
		if (o->hasProperty("window_padding"))         t.layout.window_padding = (float) o->getProperty("window_padding");
		if (o->hasProperty("node_vertical_padding"))  t.layout.node_vertical_padding = (float) o->getProperty("node_vertical_padding");
		if (o->hasProperty("preset_vertical_padding"))t.layout.preset_vertical_padding = (float) o->getProperty("preset_vertical_padding");
		if (o->hasProperty("node_default_width"))    t.layout.node_default_width = (float) o->getProperty("node_default_width");
		if (auto pad = o->getProperty("node_default_padding"); pad.isArray())
		{
			auto* a = pad.getArray();
			if (a->size() >= 2) t.layout.node_default_padding = ImVec2((float) a->getReference(0), (float) a->getReference(1));
		}
		if (auto padm = o->getProperty("node_muted_padding"); padm.isArray())
		{
			auto* a = padm.getArray();
			if (a->size() >= 2) t.layout.node_muted_padding = ImVec2((float) a->getReference(0), (float) a->getReference(1));
		}
	}

	// fonts
	if (auto v = root->getProperty("fonts"); v.isObject())
	{
		auto* o = v.getDynamicObject();
		if (o->hasProperty("default_size"))  t.fonts.default_size = (float) o->getProperty("default_size");
		if (o->hasProperty("default_path"))  t.fonts.default_path = o->getProperty("default_path");
		if (o->hasProperty("chinese_size"))  t.fonts.chinese_size = (float) o->getProperty("chinese_size");
		if (o->hasProperty("chinese_path"))  t.fonts.chinese_path = o->getProperty("chinese_path");
	}

	// windows
	if (auto v = root->getProperty("windows"); v.isObject())
	{
		auto* o = v.getDynamicObject();
		if (o->hasProperty("status_overlay_alpha"))  t.windows.status_overlay_alpha = (float) o->getProperty("status_overlay_alpha");
		if (o->hasProperty("probe_scope_alpha"))     t.windows.probe_scope_alpha = (float) o->getProperty("probe_scope_alpha");
		if (o->hasProperty("preset_status_alpha"))   t.windows.preset_status_alpha = (float) o->getProperty("preset_status_alpha");
		if (o->hasProperty("notifications_alpha"))   t.windows.notifications_alpha = (float) o->getProperty("notifications_alpha");
		if (o->hasProperty("probe_scope_width"))     t.windows.probe_scope_width = (float) o->getProperty("probe_scope_width");
		if (o->hasProperty("probe_scope_height"))    t.windows.probe_scope_height = (float) o->getProperty("probe_scope_height");
	}

	// modulation
	if (auto v = root->getProperty("modulation"); v.isObject())
	{
		auto* o = v.getDynamicObject();
		t.modulation.frequency = varToVec4(o->getProperty("frequency"), t.modulation.frequency);
		t.modulation.timbre = varToVec4(o->getProperty("timbre"), t.modulation.timbre);
		t.modulation.amplitude = varToVec4(o->getProperty("amplitude"), t.modulation.amplitude);
		t.modulation.filter = varToVec4(o->getProperty("filter"), t.modulation.filter);
	}

	// meters
	if (auto v = root->getProperty("meters"); v.isObject())
	{
		auto* o = v.getDynamicObject();
		t.meters.safe = varToVec4(o->getProperty("safe"), t.meters.safe);
		t.meters.warning = varToVec4(o->getProperty("warning"), t.meters.warning);
		t.meters.clipping = varToVec4(o->getProperty("clipping"), t.meters.clipping);
	}

	// timeline
	if (auto v = root->getProperty("timeline"); v.isObject())
	{
		auto* o = v.getDynamicObject();
		t.timeline.marker_start_end = varToColor(o->getProperty("marker_start_end"), t.timeline.marker_start_end);
		t.timeline.marker_gate = varToColor(o->getProperty("marker_gate"), t.timeline.marker_gate);
		t.timeline.marker_trigger = varToColor(o->getProperty("marker_trigger"), t.timeline.marker_trigger);
	}

	// modules
	if (auto v = root->getProperty("modules"); v.isObject())
	{
		auto* o = v.getDynamicObject();
		t.modules.videofx_section_header = varToVec4(o->getProperty("videofx_section_header"), t.modules.videofx_section_header);
		t.modules.videofx_section_subheader = varToVec4(o->getProperty("videofx_section_subheader"), t.modules.videofx_section_subheader);
		t.modules.scope_section_header = varToVec4(o->getProperty("scope_section_header"), t.modules.scope_section_header);
		t.modules.scope_plot_bg = varToColor(o->getProperty("scope_plot_bg"), t.modules.scope_plot_bg);
		t.modules.scope_plot_fg = varToColor(o->getProperty("scope_plot_fg"), t.modules.scope_plot_fg);
		t.modules.scope_plot_max = varToColor(o->getProperty("scope_plot_max"), t.modules.scope_plot_max);
		t.modules.scope_plot_min = varToColor(o->getProperty("scope_plot_min"), t.modules.scope_plot_min);
		t.modules.scope_text_max = varToVec4(o->getProperty("scope_text_max"), t.modules.scope_text_max);
		t.modules.scope_text_min = varToVec4(o->getProperty("scope_text_min"), t.modules.scope_text_min);
		t.modules.stroke_seq_border = varToColor(o->getProperty("stroke_seq_border"), t.modules.stroke_seq_border);
		t.modules.stroke_seq_canvas_bg = varToColor(o->getProperty("stroke_seq_canvas_bg"), t.modules.stroke_seq_canvas_bg);
		t.modules.stroke_seq_line_inactive = varToColor(o->getProperty("stroke_seq_line_inactive"), t.modules.stroke_seq_line_inactive);
		t.modules.stroke_seq_line_active = varToColor(o->getProperty("stroke_seq_line_active"), t.modules.stroke_seq_line_active);
		t.modules.stroke_seq_playhead = varToColor(o->getProperty("stroke_seq_playhead"), t.modules.stroke_seq_playhead);
		t.modules.stroke_seq_thresh_floor = varToColor(o->getProperty("stroke_seq_thresh_floor"), t.modules.stroke_seq_thresh_floor);
		t.modules.stroke_seq_thresh_mid = varToColor(o->getProperty("stroke_seq_thresh_mid"), t.modules.stroke_seq_thresh_mid);
		t.modules.stroke_seq_thresh_ceil = varToColor(o->getProperty("stroke_seq_thresh_ceil"), t.modules.stroke_seq_thresh_ceil);
		t.modules.stroke_seq_frame_bg = varToVec4(o->getProperty("stroke_seq_frame_bg"), t.modules.stroke_seq_frame_bg);
		t.modules.stroke_seq_frame_bg_hovered = varToVec4(o->getProperty("stroke_seq_frame_bg_hovered"), t.modules.stroke_seq_frame_bg_hovered);
		t.modules.stroke_seq_frame_bg_active = varToVec4(o->getProperty("stroke_seq_frame_bg_active"), t.modules.stroke_seq_frame_bg_active);
	}

	currentTheme = t;
	applyTheme();
	return true;
}

bool ThemeManager::saveTheme(const juce::File& themeFile)
{
	juce::DynamicObject::Ptr root = new juce::DynamicObject();
	// headers
	{
		juce::DynamicObject::Ptr o = new juce::DynamicObject();
		auto putTri = [&](const char* name, const TriStateColor& tsc)
		{
			juce::DynamicObject::Ptr h = new juce::DynamicObject();
			h->setProperty("base", colorToVar(tsc.base));
			h->setProperty("hovered", colorToVar(tsc.hovered));
			h->setProperty("active", colorToVar(tsc.active));
			o->setProperty(name, juce::var(h.get()));
		};
		putTri("recent", currentTheme.headers.recent);
		putTri("samples", currentTheme.headers.samples);
		putTri("presets", currentTheme.headers.presets);
		putTri("system", currentTheme.headers.system);
		root->setProperty("headers", juce::var(o.get()));
	}

	// text
	{
		juce::DynamicObject::Ptr o = new juce::DynamicObject();
		o->setProperty("section_header", vec4ToVar(currentTheme.text.section_header));
		o->setProperty("warning", vec4ToVar(currentTheme.text.warning));
		o->setProperty("success", vec4ToVar(currentTheme.text.success));
		o->setProperty("error", vec4ToVar(currentTheme.text.error));
		o->setProperty("disabled", vec4ToVar(currentTheme.text.disabled));
		o->setProperty("active", vec4ToVar(currentTheme.text.active));
		o->setProperty("tooltip_wrap_standard", currentTheme.text.tooltip_wrap_standard);
		o->setProperty("tooltip_wrap_compact", currentTheme.text.tooltip_wrap_compact);
		root->setProperty("text", juce::var(o.get()));
	}

	// accent
	root->setProperty("accent", vec4ToVar(currentTheme.accent));

	// status
	{
		juce::DynamicObject::Ptr o = new juce::DynamicObject();
		o->setProperty("edited", vec4ToVar(currentTheme.status.edited));
		o->setProperty("saved", vec4ToVar(currentTheme.status.saved));
		root->setProperty("status", juce::var(o.get()));
	}

	// imnodes
	{
		juce::DynamicObject::Ptr imn = new juce::DynamicObject();
		juce::DynamicObject::Ptr cats = new juce::DynamicObject();
		for (auto& kv : currentTheme.imnodes.category_colors)
			cats->setProperty(moduleCategoryToString(kv.first), colorToVar(kv.second));
		imn->setProperty("category_colors", juce::var(cats.get()));

		juce::DynamicObject::Ptr pins = new juce::DynamicObject();
		for (auto& kv : currentTheme.imnodes.pin_colors)
			pins->setProperty(pinTypeToString(kv.first), colorToVar(kv.second));
		imn->setProperty("pin_colors", juce::var(pins.get()));

		imn->setProperty("pin_connected", colorToVar(currentTheme.imnodes.pin_connected));
		imn->setProperty("pin_disconnected", colorToVar(currentTheme.imnodes.pin_disconnected));
		imn->setProperty("node_muted", colorToVar(currentTheme.imnodes.node_muted));
		imn->setProperty("node_muted_alpha", currentTheme.imnodes.node_muted_alpha);
		imn->setProperty("node_hovered_link_highlight", colorToVar(currentTheme.imnodes.node_hovered_link_highlight));
		root->setProperty("imnodes", juce::var(imn.get()));
	}

	// links
	{
		juce::DynamicObject::Ptr o = new juce::DynamicObject();
		o->setProperty("link_hovered", colorToVar(currentTheme.links.link_hovered));
		o->setProperty("link_selected", colorToVar(currentTheme.links.link_selected));
		o->setProperty("link_highlighted", colorToVar(currentTheme.links.link_highlighted));
		o->setProperty("preview_color", colorToVar(currentTheme.links.preview_color));
		o->setProperty("preview_width", currentTheme.links.preview_width);
		o->setProperty("label_background", colorToVar(currentTheme.links.label_background));
		o->setProperty("label_text", colorToVar(currentTheme.links.label_text));
		root->setProperty("links", juce::var(o.get()));
	}

	// canvas
	{
		juce::DynamicObject::Ptr o = new juce::DynamicObject();
		o->setProperty("grid_color", colorToVar(currentTheme.canvas.grid_color));
		o->setProperty("grid_origin_color", colorToVar(currentTheme.canvas.grid_origin_color));
		o->setProperty("grid_size", currentTheme.canvas.grid_size);
		o->setProperty("scale_text_color", colorToVar(currentTheme.canvas.scale_text_color));
		o->setProperty("scale_interval", currentTheme.canvas.scale_interval);
		o->setProperty("drop_target_overlay", colorToVar(currentTheme.canvas.drop_target_overlay));
		o->setProperty("mouse_position_text", colorToVar(currentTheme.canvas.mouse_position_text));
		root->setProperty("canvas", juce::var(o.get()));
	}

	// layout
	{
		juce::DynamicObject::Ptr o = new juce::DynamicObject();
		o->setProperty("sidebar_width", currentTheme.layout.sidebar_width);
		o->setProperty("window_padding", currentTheme.layout.window_padding);
		o->setProperty("node_vertical_padding", currentTheme.layout.node_vertical_padding);
		o->setProperty("preset_vertical_padding", currentTheme.layout.preset_vertical_padding);
		o->setProperty("node_default_width", currentTheme.layout.node_default_width);
		juce::Array<juce::var> pad; pad.add(currentTheme.layout.node_default_padding.x); pad.add(currentTheme.layout.node_default_padding.y);
		o->setProperty("node_default_padding", juce::var(pad));
		juce::Array<juce::var> padm; padm.add(currentTheme.layout.node_muted_padding.x); padm.add(currentTheme.layout.node_muted_padding.y);
		o->setProperty("node_muted_padding", juce::var(padm));
		root->setProperty("layout", juce::var(o.get()));
	}

	// fonts
	{
		juce::DynamicObject::Ptr o = new juce::DynamicObject();
		o->setProperty("default_size", currentTheme.fonts.default_size);
		o->setProperty("default_path", currentTheme.fonts.default_path);
		o->setProperty("chinese_size", currentTheme.fonts.chinese_size);
		o->setProperty("chinese_path", currentTheme.fonts.chinese_path);
		root->setProperty("fonts", juce::var(o.get()));
	}

	// windows
	{
		juce::DynamicObject::Ptr o = new juce::DynamicObject();
		o->setProperty("status_overlay_alpha", currentTheme.windows.status_overlay_alpha);
		o->setProperty("probe_scope_alpha", currentTheme.windows.probe_scope_alpha);
		o->setProperty("preset_status_alpha", currentTheme.windows.preset_status_alpha);
		o->setProperty("notifications_alpha", currentTheme.windows.notifications_alpha);
		o->setProperty("probe_scope_width", currentTheme.windows.probe_scope_width);
		o->setProperty("probe_scope_height", currentTheme.windows.probe_scope_height);
		root->setProperty("windows", juce::var(o.get()));
	}

	// modulation
	{
		juce::DynamicObject::Ptr o = new juce::DynamicObject();
		o->setProperty("frequency", vec4ToVar(currentTheme.modulation.frequency));
		o->setProperty("timbre", vec4ToVar(currentTheme.modulation.timbre));
		o->setProperty("amplitude", vec4ToVar(currentTheme.modulation.amplitude));
		o->setProperty("filter", vec4ToVar(currentTheme.modulation.filter));
		root->setProperty("modulation", juce::var(o.get()));
	}

	// meters
	{
		juce::DynamicObject::Ptr o = new juce::DynamicObject();
		o->setProperty("safe", vec4ToVar(currentTheme.meters.safe));
		o->setProperty("warning", vec4ToVar(currentTheme.meters.warning));
		o->setProperty("clipping", vec4ToVar(currentTheme.meters.clipping));
		root->setProperty("meters", juce::var(o.get()));
	}

	// timeline
	{
		juce::DynamicObject::Ptr o = new juce::DynamicObject();
		o->setProperty("marker_start_end", colorToVar(currentTheme.timeline.marker_start_end));
		o->setProperty("marker_gate", colorToVar(currentTheme.timeline.marker_gate));
		o->setProperty("marker_trigger", colorToVar(currentTheme.timeline.marker_trigger));
		root->setProperty("timeline", juce::var(o.get()));
	}

	// modules
	{
		juce::DynamicObject::Ptr o = new juce::DynamicObject();
		o->setProperty("videofx_section_header", vec4ToVar(currentTheme.modules.videofx_section_header));
		o->setProperty("videofx_section_subheader", vec4ToVar(currentTheme.modules.videofx_section_subheader));
		o->setProperty("scope_section_header", vec4ToVar(currentTheme.modules.scope_section_header));
		o->setProperty("scope_plot_bg", colorToVar(currentTheme.modules.scope_plot_bg));
		o->setProperty("scope_plot_fg", colorToVar(currentTheme.modules.scope_plot_fg));
		o->setProperty("scope_plot_max", colorToVar(currentTheme.modules.scope_plot_max));
		o->setProperty("scope_plot_min", colorToVar(currentTheme.modules.scope_plot_min));
		o->setProperty("scope_text_max", vec4ToVar(currentTheme.modules.scope_text_max));
		o->setProperty("scope_text_min", vec4ToVar(currentTheme.modules.scope_text_min));
		o->setProperty("stroke_seq_border", colorToVar(currentTheme.modules.stroke_seq_border));
		o->setProperty("stroke_seq_canvas_bg", colorToVar(currentTheme.modules.stroke_seq_canvas_bg));
		o->setProperty("stroke_seq_line_inactive", colorToVar(currentTheme.modules.stroke_seq_line_inactive));
		o->setProperty("stroke_seq_line_active", colorToVar(currentTheme.modules.stroke_seq_line_active));
		o->setProperty("stroke_seq_playhead", colorToVar(currentTheme.modules.stroke_seq_playhead));
		o->setProperty("stroke_seq_thresh_floor", colorToVar(currentTheme.modules.stroke_seq_thresh_floor));
		o->setProperty("stroke_seq_thresh_mid", colorToVar(currentTheme.modules.stroke_seq_thresh_mid));
		o->setProperty("stroke_seq_thresh_ceil", colorToVar(currentTheme.modules.stroke_seq_thresh_ceil));
		o->setProperty("stroke_seq_frame_bg", vec4ToVar(currentTheme.modules.stroke_seq_frame_bg));
		o->setProperty("stroke_seq_frame_bg_hovered", vec4ToVar(currentTheme.modules.stroke_seq_frame_bg_hovered));
		o->setProperty("stroke_seq_frame_bg_active", vec4ToVar(currentTheme.modules.stroke_seq_frame_bg_active));
		root->setProperty("modules", juce::var(o.get()));
	}

	juce::String json = juce::JSON::toString(juce::var(root.get()), true);
	return themeFile.replaceWithText(json);
}

void ThemeManager::applyImGuiStyle()
{
	ImGui::GetStyle() = currentTheme.style;
}

// Helpers
juce::var ThemeManager::colorToVar(ImU32 c)
{
	// Store as float array [r,g,b,a] in 0..1 for interoperability
	ImVec4 f = ImGui::ColorConvertU32ToFloat4(c);
	juce::Array<juce::var> a;
	a.add(f.x);
	a.add(f.y);
	a.add(f.z);
	a.add(f.w);
	return juce::var(a);
}

ImU32 ThemeManager::varToColor(const juce::var& v, ImU32 fallback)
{
	if (!v.isArray()) return fallback;
	auto* arr = v.getArray(); if (arr->size() < 4) return fallback;
	// Accept either 0..1 floats or 0..255 ints
	auto getf = [&](int idx) -> float {
		auto& ref = arr->getReference(idx);
		if (ref.isDouble() || ref.isInt() || ref.isInt64())
		{
			double d = (double) ref;
			if (d > 1.0) return (float) (d / 255.0);
			return (float) d;
		}
		return 0.0f;
	};
	float r = getf(0), g = getf(1), b = getf(2), apha = getf(3);
	return ImGui::ColorConvertFloat4ToU32(ImVec4(r, g, b, apha));
}

juce::var ThemeManager::vec4ToVar(const ImVec4& v)
{
	juce::Array<juce::var> a; a.add(v.x); a.add(v.y); a.add(v.z); a.add(v.w);
	return juce::var(a);
}

ImVec4 ThemeManager::varToVec4(const juce::var& v, const ImVec4& fallback)
{
	if (!v.isArray()) return fallback;
	auto* a = v.getArray(); if (a->size() < 4) return fallback;
	return ImVec4((float) a->getReference(0), (float) a->getReference(1), (float) a->getReference(2), (float) a->getReference(3));
}

juce::String ThemeManager::moduleCategoryToString(ModuleCategory c)
{
	switch (c)
	{
		case ModuleCategory::Source: return "Source";
		case ModuleCategory::Effect: return "Effect";
		case ModuleCategory::Modulator: return "Modulator";
		case ModuleCategory::Utility: return "Utility";
		case ModuleCategory::Seq: return "Seq";
		case ModuleCategory::MIDI: return "MIDI";
		case ModuleCategory::Analysis: return "Analysis";
		case ModuleCategory::TTS_Voice: return "TTS_Voice";
		case ModuleCategory::Special_Exp: return "Special_Exp";
		case ModuleCategory::OpenCV: return "OpenCV";
		case ModuleCategory::Sys: return "Sys";
		case ModuleCategory::Comment: return "Comment";
		case ModuleCategory::Plugin: return "Plugin";
		default: return "Default";
	}
}

bool ThemeManager::stringToModuleCategory(const juce::String& s, ModuleCategory& out)
{
	static std::map<juce::String, ModuleCategory> m = {
		{ "Source", ModuleCategory::Source },
		{ "Effect", ModuleCategory::Effect },
		{ "Modulator", ModuleCategory::Modulator },
		{ "Utility", ModuleCategory::Utility },
		{ "Seq", ModuleCategory::Seq },
		{ "MIDI", ModuleCategory::MIDI },
		{ "Analysis", ModuleCategory::Analysis },
		{ "TTS_Voice", ModuleCategory::TTS_Voice },
		{ "Special_Exp", ModuleCategory::Special_Exp },
		{ "OpenCV", ModuleCategory::OpenCV },
		{ "Sys", ModuleCategory::Sys },
		{ "Comment", ModuleCategory::Comment },
		{ "Plugin", ModuleCategory::Plugin },
		{ "Default", ModuleCategory::Default }
	};
	if (auto it = m.find(s); it != m.end()) { out = it->second; return true; }
	return false;
}

juce::String ThemeManager::pinTypeToString(PinDataType t)
{
	switch (t)
	{
		case PinDataType::CV: return "CV";
		case PinDataType::Audio: return "Audio";
		case PinDataType::Gate: return "Gate";
		case PinDataType::Raw: return "Raw";
		case PinDataType::Video: return "Video";
		default: return "Default";
	}
}

bool ThemeManager::stringToPinType(const juce::String& s, PinDataType& out)
{
	static std::map<juce::String, PinDataType> m = {
		{ "CV", PinDataType::CV },
		{ "Audio", PinDataType::Audio },
		{ "Gate", PinDataType::Gate },
		{ "Raw", PinDataType::Raw },
		{ "Video", PinDataType::Video },
		{ "Default", PinDataType::Raw }
	};
	if (auto it = m.find(s); it != m.end()) { out = it->second; return true; }
	return false;
}

void ThemeManager::loadDefaultTheme()
{
	ImGui::StyleColorsDark(&defaultTheme.style);
	defaultTheme.accent = ImVec4(0.0f, 0.8f, 1.0f, 1.0f);

	// Category colors
	defaultTheme.imnodes.category_colors[ModuleCategory::Source] = IM_COL32(50, 120, 50, 255);
	defaultTheme.imnodes.category_colors[ModuleCategory::Effect] = IM_COL32(130, 60, 60, 255);
	defaultTheme.imnodes.category_colors[ModuleCategory::Modulator] = IM_COL32(50, 50, 130, 255);
	defaultTheme.imnodes.category_colors[ModuleCategory::Utility] = IM_COL32(110, 80, 50, 255);
	defaultTheme.imnodes.category_colors[ModuleCategory::Seq] = IM_COL32(90, 140, 90, 255);
	defaultTheme.imnodes.category_colors[ModuleCategory::MIDI] = IM_COL32(180, 120, 255, 255);
	defaultTheme.imnodes.category_colors[ModuleCategory::Analysis] = IM_COL32(100, 50, 110, 255);
	defaultTheme.imnodes.category_colors[ModuleCategory::TTS_Voice] = IM_COL32(255, 180, 100, 255);
	defaultTheme.imnodes.category_colors[ModuleCategory::Special_Exp] = IM_COL32(50, 200, 200, 255);
	defaultTheme.imnodes.category_colors[ModuleCategory::OpenCV] = IM_COL32(255, 140, 0, 255);
	defaultTheme.imnodes.category_colors[ModuleCategory::Sys] = IM_COL32(120, 100, 140, 255);
	defaultTheme.imnodes.category_colors[ModuleCategory::Comment] = IM_COL32(80, 80, 80, 255);
	defaultTheme.imnodes.category_colors[ModuleCategory::Plugin] = IM_COL32(50, 110, 110, 255);
	defaultTheme.imnodes.category_colors[ModuleCategory::Default] = IM_COL32(70, 70, 70, 255);

	// Pin colors
	defaultTheme.imnodes.pin_colors[PinDataType::CV] = IM_COL32(100, 150, 255, 255);
	defaultTheme.imnodes.pin_colors[PinDataType::Audio] = IM_COL32(100, 255, 150, 255);
	defaultTheme.imnodes.pin_colors[PinDataType::Gate] = IM_COL32(255, 220, 100, 255);
	defaultTheme.imnodes.pin_colors[PinDataType::Raw] = IM_COL32(255, 100, 100, 255);
	defaultTheme.imnodes.pin_colors[PinDataType::Video] = IM_COL32(0, 200, 255, 255);
	defaultTheme.imnodes.pin_connected = IM_COL32(120, 255, 120, 255);
	defaultTheme.imnodes.pin_disconnected = IM_COL32(150, 150, 150, 255);

	// Links
	defaultTheme.links.link_hovered = IM_COL32(255, 255, 0, 255);
	defaultTheme.links.link_selected = IM_COL32(255, 255, 0, 255);
	defaultTheme.links.link_highlighted = IM_COL32(255, 255, 0, 255);
	defaultTheme.links.preview_color = IM_COL32(255, 255, 0, 200);
	defaultTheme.links.preview_width = 3.0f;
	defaultTheme.links.label_background = IM_COL32(50, 50, 50, 200);
	defaultTheme.links.label_text = IM_COL32(255, 255, 100, 255);

	// Canvas
	defaultTheme.canvas.grid_color = IM_COL32(50, 50, 50, 255);
	defaultTheme.canvas.grid_origin_color = IM_COL32(80, 80, 80, 255);
	defaultTheme.canvas.grid_size = 64.0f;
	defaultTheme.canvas.scale_text_color = IM_COL32(150, 150, 150, 80);
	defaultTheme.canvas.scale_interval = 400.0f;
	defaultTheme.canvas.drop_target_overlay = IM_COL32(218, 165, 32, 80);
	defaultTheme.canvas.mouse_position_text = IM_COL32(200, 200, 200, 150);

	// Windows
	defaultTheme.windows.status_overlay_alpha = 0.5f;
	defaultTheme.windows.probe_scope_alpha = 0.85f;
	defaultTheme.windows.preset_status_alpha = 0.7f;
	defaultTheme.windows.notifications_alpha = 0.92f;
	defaultTheme.windows.probe_scope_width = 260.0f;
	defaultTheme.windows.probe_scope_height = 180.0f;
}



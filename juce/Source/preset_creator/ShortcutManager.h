#pragma once

#include <imgui.h>
#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include <functional>
#include <unordered_map>
#include <vector>

namespace collider
{
    /** Represents a single key chord (modifiers + main key). */
    struct KeyChord
    {
        ImGuiKey key { ImGuiKey_None };
        bool ctrl { false };
        bool shift { false };
        bool alt { false };
        bool superKey { false };

        [[nodiscard]] bool isValid() const noexcept { return key != ImGuiKey_None; }

        bool operator==(const KeyChord& other) const noexcept
        {
            return key == other.key && ctrl == other.ctrl && shift == other.shift &&
                   alt == other.alt && superKey == other.superKey;
        }

        [[nodiscard]] juce::String toString() const;

        static KeyChord fromImGui(const ImGuiIO& io, ImGuiKey keyPressed);
    };

    struct KeyChordHash
    {
        std::size_t operator()(const KeyChord& chord) const noexcept
        {
            std::size_t seed = static_cast<std::size_t>(chord.key);
            seed = (seed * 1315423911u) ^ (chord.ctrl ? 0x1u : 0x0u);
            seed = (seed * 1315423911u) ^ (chord.shift ? 0x10u : 0x0u);
            seed = (seed * 1315423911u) ^ (chord.alt ? 0x100u : 0x0u);
            seed = (seed * 1315423911u) ^ (chord.superKey ? 0x1000u : 0x0u);
            return seed;
        }
    };

    struct ShortcutAction
    {
        juce::Identifier id;
        juce::String name;
        juce::String description;
        juce::String category;
    };

    struct ShortcutBinding
    {
        juce::Identifier actionId;
        juce::Identifier context; // e.g., "Global", "NodeEditor"
        KeyChord chord;
    };

    class ShortcutManager final
    {
    public:
        using ActionCallback = std::function<void()>;

        static ShortcutManager& getInstance();

        void clear();

        void registerAction(const ShortcutAction& action, ActionCallback onTrigger);
        void unregisterAction(const juce::Identifier& actionId);

        void setDefaultBinding(const juce::Identifier& actionId,
                               const juce::Identifier& context,
                               const KeyChord& chord);

        void setUserBinding(const juce::Identifier& actionId,
                            const juce::Identifier& context,
                            const KeyChord& chord);

        bool removeUserBinding(const juce::Identifier& actionId,
                               const juce::Identifier& context);

        [[nodiscard]] std::vector<ShortcutBinding> getBindingsForAction(const juce::Identifier& actionId) const;
        [[nodiscard]] KeyChord getActiveBinding(const juce::Identifier& actionId) const;

        void setContext(const juce::Identifier& newContext);
        [[nodiscard]] juce::Identifier getContext() const noexcept { return currentContext; }

        void rebuildActiveMap();

        bool processKeyChord(const KeyChord& chord);
        bool processImGuiIO(const ImGuiIO& io);

        void loadDefaultBindingsFromFile(const juce::File& file);
        void loadUserBindingsFromFile(const juce::File& file);
        void saveUserBindingsToFile(const juce::File& file) const;

        [[nodiscard]] const std::unordered_map<juce::Identifier, ShortcutAction>& getRegistry() const noexcept
        {
            return actionRegistry;
        }

        [[nodiscard]] const std::unordered_map<KeyChord, juce::Identifier, KeyChordHash>&
        getActiveKeymap() const noexcept
        {
            return activeKeymap;
        }

    private:
        ShortcutManager() = default;
        ~ShortcutManager() = default;

        static juce::var bindingToVar(const ShortcutBinding& binding);
        static juce::Optional<ShortcutBinding> bindingFromVar(const juce::var& value);

        std::unordered_map<juce::Identifier, ShortcutAction> actionRegistry;
        std::unordered_map<juce::Identifier, ActionCallback> actionCallbacks;
        std::unordered_map<juce::Identifier, std::vector<ShortcutBinding>> defaultBindings;
        std::unordered_map<juce::Identifier, std::vector<ShortcutBinding>> userBindings;

        std::unordered_map<KeyChord, juce::Identifier, KeyChordHash> activeKeymap;

        juce::Identifier currentContext { "Global" };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ShortcutManager)
    };

} // namespace collider

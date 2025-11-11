#include "ShortcutManager.h"
#include <imgui_internal.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <unordered_set>

namespace std
{
    template <>
    struct hash<juce::Identifier>
    {
        size_t operator()(const juce::Identifier& id) const noexcept
        {
            return static_cast<size_t>(id.toString().hashCode64());
        }
    };
}

namespace collider
{
    namespace
    {
        constexpr const char* kBindingsKey = "bindings";
        constexpr const char* kOverridesKey = "overrides";
        const juce::Identifier kGlobalContext { "Global" };

        juce::String keyToString(ImGuiKey key)
        {
#if IMGUI_VERSION_NUM >= 18900
            return juce::String(ImGui::GetKeyName(key));
#else
            return juce::String((int)key);
#endif
        }
    } // namespace

    //==== KeyChord ==================================================================================

    juce::String KeyChord::toString() const
    {
        if (!isValid())
            return "Unassigned";

        juce::StringArray parts;
        if (ctrl)      parts.add("Ctrl");
        if (shift)     parts.add("Shift");
        if (alt)       parts.add("Alt");
        if (superKey)  parts.add("Super");

        auto keyName = keyToString(key);
        if (keyName.isNotEmpty())
            parts.add(keyName);

        return parts.joinIntoString("+");
    }

    KeyChord KeyChord::fromImGui(const ImGuiIO& io, ImGuiKey keyPressed)
    {
        KeyChord chord;
        chord.key = keyPressed;
        chord.ctrl = io.KeyCtrl;
        chord.shift = io.KeyShift;
        chord.alt = io.KeyAlt;
#if JUCE_MAC
        chord.superKey = io.KeySuper || io.KeyCtrl;
#else
        chord.superKey = io.KeySuper;
#endif
        return chord;
    }

    //==== ShortcutManager ==========================================================================

    ShortcutManager& ShortcutManager::getInstance()
    {
        static ShortcutManager instance;
        return instance;
    }

    void ShortcutManager::clear()
    {
        actionRegistry.clear();
        actionCallbacks.clear();
        defaultBindings.clear();
        userBindings.clear();
        activeKeymap.clear();
    }

    void ShortcutManager::registerAction(const ShortcutAction& action, ActionCallback onTrigger)
    {
        actionRegistry[action.id] = action;
        actionCallbacks[action.id] = std::move(onTrigger);
    }

    void ShortcutManager::unregisterAction(const juce::Identifier& actionId)
    {
        actionRegistry.erase(actionId);
        actionCallbacks.erase(actionId);
        defaultBindings.erase(actionId);
        userBindings.erase(actionId);
        rebuildActiveMap();
    }

    void ShortcutManager::setDefaultBinding(const juce::Identifier& actionId,
                                            const juce::Identifier& context,
                                            const KeyChord& chord)
    {
        auto& bindings = defaultBindings[actionId];
        auto it = std::find_if(bindings.begin(), bindings.end(), [&](const ShortcutBinding& b) {
            return b.context == context;
        });
        if (it != bindings.end())
            it->chord = chord;
        else
            bindings.push_back({ actionId, context, chord });
        rebuildActiveMap();
    }

    void ShortcutManager::setUserBinding(const juce::Identifier& actionId,
                                         const juce::Identifier& context,
                                         const KeyChord& chord)
    {
        auto& bindings = userBindings[actionId];
        auto it = std::find_if(bindings.begin(), bindings.end(), [&](const ShortcutBinding& b) {
            return b.context == context;
        });
        if (it != bindings.end())
            it->chord = chord;
        else
            bindings.push_back({ actionId, context, chord });
        rebuildActiveMap();
    }

    bool ShortcutManager::removeUserBinding(const juce::Identifier& actionId,
                                            const juce::Identifier& context)
    {
        auto found = userBindings.find(actionId);
        if (found == userBindings.end())
            return false;

        auto& bindings = found->second;
        auto sizeBefore = bindings.size();
        bindings.erase(std::remove_if(bindings.begin(), bindings.end(), [&](const ShortcutBinding& binding) {
                          return binding.context == context;
                      }),
                      bindings.end());

        if (bindings.empty())
            userBindings.erase(found);

        if (sizeBefore != bindings.size())
        {
            rebuildActiveMap();
            return true;
        }
        return false;
    }

    std::vector<ShortcutBinding> ShortcutManager::getBindingsForAction(const juce::Identifier& actionId) const
    {
        std::vector<ShortcutBinding> result;
        if (auto it = defaultBindings.find(actionId); it != defaultBindings.end())
            result.insert(result.end(), it->second.begin(), it->second.end());
        if (auto it = userBindings.find(actionId); it != userBindings.end())
            result.insert(result.end(), it->second.begin(), it->second.end());
        return result;
    }

    KeyChord ShortcutManager::getActiveBinding(const juce::Identifier& actionId) const
    {
        auto preferredContext = currentContext;
        KeyChord best;

        if (auto it = userBindings.find(actionId); it != userBindings.end())
        {
            for (const auto& binding : it->second)
            {
                if ((binding.context == preferredContext || binding.context == kGlobalContext) && binding.chord.isValid())
                    return binding.chord;
            }
        }

        if (auto it = defaultBindings.find(actionId); it != defaultBindings.end())
        {
            for (const auto& binding : it->second)
            {
                if ((binding.context == preferredContext || binding.context == kGlobalContext) && binding.chord.isValid())
                    return binding.chord;
            }
        }

        return best;
    }

    void ShortcutManager::setContext(const juce::Identifier& newContext)
    {
        if (currentContext == newContext)
            return;

        currentContext = newContext;
        rebuildActiveMap();
    }

    const juce::Identifier& ShortcutManager::getGlobalContextIdentifier() noexcept
    {
        return kGlobalContext;
    }

    void ShortcutManager::rebuildActiveMap()
    {
        activeKeymap.clear();

        std::unordered_map<juce::Identifier, std::unordered_set<juce::Identifier>, IdentifierHash> overriddenContexts;
        for (const auto& [actionId, bindings] : userBindings)
        {
            auto& contextSet = overriddenContexts[actionId];
            for (const auto& binding : bindings)
                contextSet.insert(binding.context);
        }

        const auto isContextOverridden = [&](const juce::Identifier& actionId, const juce::Identifier& context)
        {
            if (auto it = overriddenContexts.find(actionId); it != overriddenContexts.end())
                return it->second.count(context) > 0;
            return false;
        };

        const auto insertBindings = [&](const auto& source, bool isDefault) {
            for (const auto& [actionId, bindings] : source)
            {
                for (const auto& binding : bindings)
                {
                    if (isDefault && isContextOverridden(actionId, binding.context))
                        continue;

                    if (!binding.chord.isValid())
                        continue;

                    if (binding.context != kGlobalContext && binding.context != currentContext)
                        continue;

                    activeKeymap[binding.chord] = actionId;
                }
            }
        };

        insertBindings(defaultBindings, true);
        insertBindings(userBindings, false);
    }

    juce::Optional<KeyChord> ShortcutManager::getUserBinding(const juce::Identifier& actionId,
                                                             const juce::Identifier& context) const
    {
        if (auto it = userBindings.find(actionId); it != userBindings.end())
        {
            for (const auto& binding : it->second)
            {
                if (binding.context == context)
                    return binding.chord;
            }
        }
        return {};
    }

    juce::Optional<KeyChord> ShortcutManager::getDefaultBinding(const juce::Identifier& actionId,
                                                                const juce::Identifier& context) const
    {
        if (auto it = defaultBindings.find(actionId); it != defaultBindings.end())
        {
            for (const auto& binding : it->second)
            {
                if (binding.context == context)
                    return binding.chord;
            }
        }
        return {};
    }

    KeyChord ShortcutManager::getBindingForContext(const juce::Identifier& actionId,
                                                   const juce::Identifier& context) const
    {
        if (auto user = getUserBinding(actionId, context))
        {
            if (user->isValid())
                return *user;
        }

        if (auto defaults = getDefaultBinding(actionId, context))
        {
            if (defaults->isValid())
                return *defaults;
        }

        return {};
    }

    bool ShortcutManager::processKeyChord(const KeyChord& chord)
    {
        if (!chord.isValid())
            return false;

        if (auto it = activeKeymap.find(chord); it != activeKeymap.end())
        {
            auto actionId = it->second;
            if (auto cb = actionCallbacks.find(actionId); cb != actionCallbacks.end())
            {
                cb->second();
                return true;
            }
        }
        return false;
    }

    bool ShortcutManager::processImGuiIO(const ImGuiIO& io)
    {
        if (io.WantCaptureKeyboard)
            return false;

        bool handled = false;
        for (int keyIndex = ImGuiKey_NamedKey_BEGIN; keyIndex < ImGuiKey_NamedKey_END; ++keyIndex)
        {
            const ImGuiKey key = static_cast<ImGuiKey>(keyIndex);
            const ImGuiKeyData* data = ImGui::GetKeyData(key);
            if (data == nullptr)
                continue;

            if (data->Down && data->DownDuration == 0.0f)
            {
                auto chord = KeyChord::fromImGui(io, key);
                handled = processKeyChord(chord) || handled;
            }
        }
        return handled;
    }

    bool ShortcutManager::loadDefaultBindingsFromFile(const juce::File& file)
    {
        if (!file.existsAsFile())
        {
            juce::Logger::writeToLog("[ShortcutManager] Default bindings file not found: " + file.getFullPathName());
            return false;
        }

        auto json = juce::JSON::parse(file);
        if (json.isVoid() || !json.isObject())
        {
            juce::Logger::writeToLog("[ShortcutManager] Failed to parse default bindings JSON: " + file.getFullPathName());
            return false;
        }

        const auto* object = json.getDynamicObject();
        if (object == nullptr)
        {
            juce::Logger::writeToLog("[ShortcutManager] Default bindings JSON root is not an object: " + file.getFullPathName());
            return false;
        }

        if (auto* list = object->getProperty(kBindingsKey).getArray())
        {
            for (const auto& entry : *list)
            {
                if (auto binding = bindingFromVar(entry))
                    setDefaultBinding(binding->actionId, binding->context, binding->chord);
            }
        }

        return true;
    }

    bool ShortcutManager::loadUserBindingsFromFile(const juce::File& file)
    {
        if (!file.existsAsFile())
            return false;

        auto json = juce::JSON::parse(file);
        if (json.isVoid() || !json.isObject())
        {
            juce::Logger::writeToLog("[ShortcutManager] Failed to parse user bindings JSON: " + file.getFullPathName());
            return false;
        }

        const auto* object = json.getDynamicObject();
        if (object == nullptr)
        {
            juce::Logger::writeToLog("[ShortcutManager] User bindings JSON root is not an object: " + file.getFullPathName());
            return false;
        }

        if (auto* list = object->getProperty(kOverridesKey).getArray())
        {
            for (const auto& entry : *list)
            {
                if (auto binding = bindingFromVar(entry))
                    setUserBinding(binding->actionId, binding->context, binding->chord);
            }
        }

        return true;
    }

    bool ShortcutManager::saveUserBindingsToFile(const juce::File& file) const
    {
        juce::DynamicObject::Ptr root(new juce::DynamicObject());
        juce::Array<juce::var> overrides;

        for (const auto& [actionId, bindings] : userBindings)
        {
            for (const auto& binding : bindings)
                overrides.add(bindingToVar(binding));
        }

        root->setProperty(kOverridesKey, overrides);
        const auto json = juce::JSON::toString(juce::var(root));
        if (!file.replaceWithText(json))
        {
            juce::Logger::writeToLog("[ShortcutManager] Failed to write user bindings JSON: " + file.getFullPathName());
            return false;
        }

        return true;
    }

    juce::var ShortcutManager::bindingToVar(const ShortcutBinding& binding)
    {
        juce::DynamicObject::Ptr obj(new juce::DynamicObject());
        obj->setProperty("actionId", binding.actionId.toString());
        obj->setProperty("context", binding.context.toString());
        obj->setProperty("key", static_cast<int>(binding.chord.key));
        obj->setProperty("ctrl", binding.chord.ctrl);
        obj->setProperty("shift", binding.chord.shift);
        obj->setProperty("alt", binding.chord.alt);
        obj->setProperty("super", binding.chord.superKey);
        return obj.get();
    }

    juce::Optional<ShortcutBinding> ShortcutManager::bindingFromVar(const juce::var& value)
    {
        if (!value.isObject())
            return {};

        if (const auto* obj = value.getDynamicObject())
        {
            ShortcutBinding binding;
            binding.actionId = juce::Identifier(obj->getProperty("actionId"));
            binding.context = juce::Identifier(obj->getProperty("context"));
            binding.chord.key = static_cast<ImGuiKey>(static_cast<int>(obj->getProperty("key")));
            binding.chord.ctrl = static_cast<bool>(obj->getProperty("ctrl"));
            binding.chord.shift = static_cast<bool>(obj->getProperty("shift"));
            binding.chord.alt = static_cast<bool>(obj->getProperty("alt"));
            binding.chord.superKey = static_cast<bool>(obj->getProperty("super"));
            return binding;
        }
        return {};
    }

} // namespace collider

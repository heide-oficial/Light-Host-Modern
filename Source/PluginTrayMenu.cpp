#include <JuceHeader.h>
#include "PluginTrayMenu.h"
#include "RuntimeProfile.h"
#include "DebugLog.h"
#include "GpuMemorySampler.h"
#include "IpcPipe.h"
#include "ProcessMetrics.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <mutex>
#include <roapi.h>
#include <windows.ui.viewmanagement.h>
#include <wrl/client.h>
#include <wrl/wrappers/corewrappers.h>

#pragma comment(lib, "runtimeobject.lib")

namespace lightHostModern
{
juce::String trayText(const juce::var& locale, const char* key, const char* fallback)
{
    const auto value = locale[key].toString();
    return value.isEmpty() ? juce::String(fallback) : value;
}

namespace
{
float windowsTextScale()
{
    // Accessibility text size is independent of monitor DPI. Read the public
    // Windows setting each time, even when the WinUI process is not running.
    const auto initialized = RoInitialize(RO_INIT_SINGLETHREADED);
    if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE) return 1.0f;
    double scale = 1.0;
    {
        Microsoft::WRL::ComPtr<IInspectable> instance;
        Microsoft::WRL::ComPtr<ABI::Windows::UI::ViewManagement::IUISettings2> settings;
        if (SUCCEEDED(RoActivateInstance(Microsoft::WRL::Wrappers::HStringReference(
                RuntimeClass_Windows_UI_ViewManagement_UISettings).Get(), &instance))
            && SUCCEEDED(instance.As(&settings)))
        {
            if (FAILED(settings->get_TextScaleFactor(&scale))) scale = 1.0;
        }
    } // Release WinRT objects before balancing our apartment initialization.
    if (SUCCEEDED(initialized)) RoUninitialize();
    return std::isfinite(scale) && scale >= 1.0 && scale <= 2.25 ? static_cast<float>(scale) : 1.0f;
}

class TrayLookAndFeel final : public LookAndFeel_V4
{
public:
    explicit TrayLookAndFeel(float textScale) : fontHeight(14.0f * textScale)
    {
        const auto settings = RuntimeProfile::current().uiSettings().wstring();
        wchar_t preference[32] {};
        GetPrivateProfileStringW(L"Appearance", L"ThemeMode", L"Dark", preference, 32, settings.c_str());
        const String mode(preference);
        const bool dark = mode == "System" ? Desktop::getInstance().isDarkModeActive() : mode != "Light";
        HIGHCONTRASTW contrast { sizeof(HIGHCONTRASTW) };
        const bool highContrast = SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0)
            && (contrast.dwFlags & HCF_HIGHCONTRASTON) != 0;
        const auto systemColour = [](int index)
        {
            const auto value = GetSysColor(index);
            return Colour(GetRValue(value), GetGValue(value), GetBValue(value));
        };
        const auto background = highContrast ? systemColour(COLOR_MENU) : Colour(dark ? 0xff292929 : 0xfffafafa);
        const auto foreground = highContrast ? systemColour(COLOR_MENUTEXT) : Colour(dark ? 0xfff5f5f5 : 0xff1a1a1a);
        disabled = highContrast ? systemColour(COLOR_GRAYTEXT) : Colour(dark ? 0xff999999 : 0xff777777);
        border = highContrast ? foreground : Colour(dark ? 0xff454545 : 0xffd6d6d6);
        setColour(PopupMenu::backgroundColourId, background);
        setColour(PopupMenu::textColourId, foreground);
        setColour(PopupMenu::headerTextColourId, foreground);
        setColour(PopupMenu::highlightedBackgroundColourId,
                  highContrast ? systemColour(COLOR_HIGHLIGHT) : Colour(dark ? 0xff3d3d3d : 0xffeaeaea));
        setColour(PopupMenu::highlightedTextColourId, highContrast ? systemColour(COLOR_HIGHLIGHTTEXT) : foreground);
    }

    Font getPopupMenuFont() override { return Font(FontOptions("Segoe UI", fontHeight, Font::plain)); }
    int itemHeight() const { return jmax(30, static_cast<int>(std::ceil(fontHeight * 1.35f + 12.0f))); }
    void drawPopupMenuBackground(Graphics& g, int width, int height) override
    {
        g.fillAll(findColour(PopupMenu::backgroundColourId));
        g.setColour(border);
        g.drawRect(0, 0, width, height);
    }
    void drawPopupMenuItem(Graphics& g, const juce::Rectangle<int>& area, bool separator, bool active,
                           bool highlighted, bool ticked, bool submenu, const String& text,
                           const String& shortcut, const Drawable* icon, const Colour* colour) override
    {
        // Preserve the actual system disabled colour in contrast themes rather
        // than applying JUCE's default 50% alpha to normal menu text.
        LookAndFeel_V4::drawPopupMenuItem(g, area, separator, true, highlighted && active,
                                         ticked, submenu, text, shortcut, icon, active ? colour : &disabled);
    }
private:
    float fontHeight;
    Colour border, disabled;
};

// PDH queries may take time. Only their owned data enters the Windows worker
// pool; closing a menu never waits for a query or leaves an engine/UI reference
// in the worker. One query per panel can be outstanding at a time.
struct TrayGpuReading : std::enable_shared_from_this<TrayGpuReading>
{
    struct Request { std::shared_ptr<TrayGpuReading> state; std::set<DWORD> processes; };

    std::optional<uint64_t> readAndRefresh(const std::set<DWORD>& processes)
    {
        std::optional<uint64_t> result;
        auto request = std::make_unique<Request>(Request{ shared_from_this(), processes });
        {
            std::lock_guard lock(mutex);
            if (lastProcesses == processes && GetTickCount64() - completedAt <= 3000) result = bytes;
            if (pending) return result;
            pending = true;
        }
        auto* context = request.release();
        if (!TrySubmitThreadpoolCallback(collect, context, nullptr))
        {
            delete context;
            std::lock_guard lock(mutex);
            pending = false;
        }
        return result;
    }

private:
    static void CALLBACK collect(PTP_CALLBACK_INSTANCE, void* context)
    {
        std::unique_ptr<Request> request(static_cast<Request*>(context));
        auto& state = *request->state;
        std::optional<uint64_t> result;
        try { result = state.sampler.sample(request->processes); } catch (...) {}
        std::lock_guard lock(state.mutex);
        state.bytes = result;
        state.lastProcesses = std::move(request->processes);
        state.completedAt = GetTickCount64();
        state.pending = false;
    }

    GpuMemorySampler sampler;
    std::mutex mutex;
    std::set<DWORD> lastProcesses;
    std::optional<uint64_t> bytes;
    uint64_t completedAt = 0;
    bool pending = false;
};

class TrayPerformanceItem final : public PopupMenu::CustomComponent, private Timer
{
public:
    TrayPerformanceItem(AudioEngine& engineIn, Component& ownerIn, std::function<std::optional<DWORD>()> uiProcessIdIn,
                        const var& locale, TrayLookAndFeel& appearance)
        : CustomComponent(false), engine(engineIn), owner(&ownerIn), uiProcessId(std::move(uiProcessIdIn)),
          unavailable(trayText(locale, "common.unavailable", "Unavailable")),
          disabled(trayText(locale, "dashboard.metricsDisabled", "Disabled")), rowHeight(appearance.itemHeight())
    {
        const auto font = appearance.getPopupMenuFont();
        const auto colour = appearance.findColour(PopupMenu::textColourId);
        const std::array<String, 3> titles {
            trayText(locale, "dashboard.cpuUsage", "CPU usage"),
            trayText(locale, "dashboard.vramUsage", "VRAM usage"),
            trayText(locale, "dashboard.ramUsage", "RAM usage")
        };
        int titleWidth = 0;
        for (size_t i = 0; i < titles.size(); ++i)
        {
            auto& label = labels[i];
            auto& value = values[i];
            label.setText(titles[i], dontSendNotification);
            value.setText(unavailable, dontSendNotification);
            value.setTitle(titles[i]);
            value.setJustificationType(Justification::centredRight);
            for (auto* field : { &label, &value })
            {
                field->setFont(font);
                field->setColour(Label::textColourId, colour);
                field->setInterceptsMouseClicks(false, false);
                addAndMakeVisible(field);
            }
            titleWidth = jmax(titleWidth, GlyphArrangement::getStringWidthInt(font, titles[i]));
        }
        valueWidth = jmax(GlyphArrangement::getStringWidthInt(font, "999999.9 MiB"),
                         GlyphArrangement::getStringWidthInt(font, unavailable),
                         GlyphArrangement::getStringWidthInt(font, disabled)) + 12;
        // Reserve both columns so changing numbers never resize the submenu.
        idealWidth = titleWidth + valueWidth + 52;
        startTimer(1000);
    }

    ~TrayPerformanceItem() override { stopTimer(); }
    void getIdealSize(int& width, int& height) override { width = idealWidth; height = rowHeight * 3; }
    void resized() override
    {
        auto area = getLocalBounds().reduced(12, 0);
        for (size_t i = 0; i < labels.size(); ++i)
        {
            auto row = area.removeFromTop(rowHeight);
            values[i].setBounds(row.removeFromRight(valueWidth));
            row.removeFromRight(16);
            labels[i].setBounds(row);
        }
    }
    void parentHierarchyChanged() override { startTimer(1); }

private:
    void timerCallback() override
    {
        startTimer(1000);
        if (!owner || !isShowing())
        {
            uiCpu.reset();
            return;
        }
        if (!engine.isDiagnosticsEnabled())
        {
            uiCpu.reset();
            for (auto& value : values) value.setText(disabled, dontSendNotification);
            return;
        }

        // Same visibility lease and host/helper totals used by the Dashboard.
        const auto now = GetTickCount64();
        diagnosticsVisibleUntil.store(now + 2000);
        const auto snapshot = engine.getDiagnosticsSnapshot();
        const auto identity = uiProcessId();
        const auto pid = identity.value_or(0);
        if (identity != previousUiPid) { uiCpu.reset(); previousUiPid = identity; }
        std::optional<double> uiCpuPercent = identity ? std::optional<double>(0.0) : std::nullopt;
        std::optional<double> uiRamMiB = uiCpuPercent;
        if (pid != 0)
        {
            ipc::Handle process(OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid));
            uiCpuPercent = uiCpu.sample(process ? processCpuTicks(process.get()) : std::nullopt, now, processorCount());
            const auto memory = process ? processMemory(process.get()) : ProcessMemory{};
            uiRamMiB = memory.resident ? std::optional<double>(*memory.resident / 1048576.0) : std::nullopt;
        }
        const auto total = [](std::optional<double> host, std::optional<double> workers,
                              std::optional<double> ui) -> std::optional<double>
        {
            return host && workers && ui ? std::optional<double>(*host + *workers + *ui) : std::nullopt;
        };
        const auto cpu = total(snapshot.hostCpuPercent, snapshot.workerCpuPercent, uiCpuPercent);
        const auto ram = total(snapshot.hostResidentMiB, snapshot.workerResidentMiB, uiRamMiB);
        values[0].setText(cpu ? String(jlimit(0.0, 100.0, *cpu), 1) + "%" : unavailable, dontSendNotification);
        values[2].setText(ram ? String(*ram, 1) + " MiB" : unavailable, dontSendNotification);

        std::set<DWORD> processes { GetCurrentProcessId() };
        if (pid != 0) processes.insert(pid);
        const auto isolated = engine.isolatedPluginDiagnostics();
        if (const auto* workers = isolated.getArray())
            for (const auto& worker : *workers)
            {
                const auto workerPid = static_cast<int64>(worker["pid"]);
                if (workerPid > 0 && workerPid <= UINT32_MAX) processes.insert(static_cast<DWORD>(workerPid));
            }
        const auto vram = identity ? gpu->readAndRefresh(processes) : std::nullopt;
        values[1].setText(vram ? String(*vram / 1048576.0, 1) + " MiB" : unavailable, dontSendNotification);
    }

    AudioEngine& engine;
    Component::SafePointer<Component> owner;
    std::function<std::optional<DWORD>()> uiProcessId;
    const String unavailable, disabled;
    int rowHeight, idealWidth = 0, valueWidth = 0;
    std::array<Label, 3> labels, values;
    std::optional<DWORD> previousUiPid = DWORD{0};
    CpuUsageSampler uiCpu;
    std::shared_ptr<TrayGpuReading> gpu = std::make_shared<TrayGpuReading>();
};

class TrayMenu
{
public:
    void item(const String& title, int id = -1, bool enabled = false, bool checked = false)
    {
        // JUCE renders ampersands literally. Only remove layout control chars.
        menu.addItem(id, title.replaceCharacters("\r\n\t", "   "), enabled, checked);
    }
    void separator() { menu.addSeparator(); }
    void submenu(const String& title, TrayMenu& child)
    { menu.addSubMenu(title.replaceCharacters("\r\n\t", "   "), child.menu); }
    PopupMenu menu;
};

// Keep large databases/chains navigable without dropping entries. Each level
// has at most 30 entries, including manufacturer groups and action submenus.
template <typename AddItem>
void paged(TrayMenu& menu, size_t begin, size_t end, const AddItem& add)
{
    constexpr size_t pageSize = 30;
    if (end - begin <= pageSize)
    {
        for (auto i = begin; i < end; ++i) add(menu, i);
        return;
    }
    size_t span = pageSize;
    while ((end - begin - 1) / span + 1 > pageSize) span *= pageSize;
    for (auto i = begin; i < end; i += span)
    {
        TrayMenu page;
        const auto last = std::min(end, i + span);
        paged(page, i, last, add);
        menu.submenu(String(static_cast<int64>(i + 1)) + " - " + String(static_cast<int64>(last)), page);
    }
}

String shortName(const String& name)
{
    return name.length() > 72 ? name.substring(0, 69) + "..." : name;
}

struct InstalledEntry
{
    String id, name, manufacturer, format;
};
}

void showPluginTrayMenu(AudioEngine& engine, const var& locale, Component& owner, int x, int y,
                        std::function<std::optional<DWORD>()> uiProcessId,
                        std::function<void(std::optional<TrayAction>)> completion)
{
    // The callback retains this per-popup appearance until every submenu has
    // closed. Never change the global LookAndFeel used by plugin editors.
    const auto textScale = windowsTextScale();
    auto appearance = std::make_shared<TrayLookAndFeel>(textScale);
    TrayMenu root;
    std::vector<TrayAction> actions;
    const auto text = [&locale](const char* key, const char* fallback) { return trayText(locale, key, fallback); };
    const auto command = [&actions](TrayMenu& menu, const String& title, TrayAction action,
                                   bool enabled = true, bool checked = false)
    {
        actions.push_back(std::move(action));
        menu.item(title, static_cast<int>(actions.size()), enabled, checked);
    };
    const bool writable = engine.isSessionWritable();
    const auto running = engine.getPluginInstances();
    const auto runningOrder = engine.getPluginProcessingOrder();
    command(root, text("tray.openUi", "Open app UI"), { TrayAction::Kind::openUi });
    root.separator();

    TrayMenu runningMenu;
    const auto runningName = [&](size_t i)
    {
        const auto& record = running[runningOrder[i]];
        auto title = String(static_cast<int64>(i + 1)) + ". " + shortName(record.displayName())
            + " [" + record.description.pluginFormatName + "]";
        if (record.loading != "loaded") title += " (" + text("common.unavailable", "Unavailable") + ")";
        else if (record.bypassed) title += " (" + text("plugins.status.bypassed", "Bypassed") + ")";
        return title;
    };
    if (running.empty()) runningMenu.item(text("tray.noRunning", "No running plugins"));
    else
    {
        paged(runningMenu, 0, running.size(), [&](TrayMenu& target, size_t i)
        {
            const auto& record = running[runningOrder[i]];
            const bool loaded = record.loading == "loaded";
            TrayMenu pluginMenu;
            command(pluginMenu, text("plugins.openEditor", "Open editor"), { TrayAction::Kind::openEditor, record.id }, loaded);
            command(pluginMenu, text("plugins.bypass", "Bypass"),
                    { TrayAction::Kind::bypassPlugin, record.id, !record.bypassed }, writable && loaded, record.bypassed);
            command(pluginMenu, text("plugins.duplicate", "Duplicate"), { TrayAction::Kind::duplicatePlugin, record.id }, writable && loaded);
            pluginMenu.separator();
            if (!engine.isChainMode()) {
                command(pluginMenu, text("tray.moveUp", "Move up"), { TrayAction::Kind::moveUp, record.id }, writable && i > 0);
                command(pluginMenu, text("tray.moveDown", "Move down"), { TrayAction::Kind::moveDown, record.id }, writable && i + 1 < running.size());
            }
            pluginMenu.separator();
            command(pluginMenu, text("tray.removeFromChain", "Remove from chain..."), { TrayAction::Kind::removePlugin, record.id }, writable);
            target.submenu(runningName(i), pluginMenu);
        });
    }
    root.submenu((engine.isChainMode() ? text("nav.plugins", "Plugins") : text("plugins.running", "Running")) + " (" + String(static_cast<int>(running.size())) + ")", runningMenu);

    std::vector<InstalledEntry> installed;
    for (const auto& description : engine.getKnownPluginsSorted())
    {
        auto name = engine.getKnownPluginCustomName(description);
        if (name.isEmpty()) name = description.name;
        installed.push_back({ knownPluginId(description), name,
            description.manufacturerName.isEmpty() ? text("plugins.unknownManufacturer", "Unknown manufacturer") : description.manufacturerName,
            description.pluginFormatName });
    }
    std::sort(installed.begin(), installed.end(), [](const auto& a, const auto& b)
    {
        if (const auto order = a.name.compareNatural(b.name)) return order < 0;
        if (const auto order = a.manufacturer.compareNatural(b.manufacturer)) return order < 0;
        if (const auto order = a.format.compareNatural(b.format)) return order < 0;
        return a.id < b.id;
    });
    TrayMenu installedMenu;
    const auto addInstalled = [&](TrayMenu& target, size_t i)
    {
        const auto& entry = installed[i];
        command(target, shortName(entry.name) + " [" + entry.format + "] - " + shortName(entry.manufacturer),
                { TrayAction::Kind::addPlugin, entry.id }, writable);
    };
    if (installed.empty()) installedMenu.item(text("tray.noInstalled", "No installed plugins. Scan in the app."));
    else
    {
        installedMenu.item(text("tray.addAndOpenHint", "Select a plugin to add it and open its editor"));
        installedMenu.separator();
        const auto settings = RuntimeProfile::current().uiSettings().wstring();
        const bool grouped = GetPrivateProfileIntW(L"Plugins", L"GroupByManufacturer", 0, settings.c_str()) != 0;
        if (grouped)
        {
            std::map<String, std::vector<size_t>> groups;
            for (size_t i = 0; i < installed.size(); ++i) groups[installed[i].manufacturer].push_back(i);
            std::vector<String> manufacturers;
            for (const auto& group : groups) manufacturers.push_back(group.first);
            std::sort(manufacturers.begin(), manufacturers.end(), [](const auto& a, const auto& b) { return a.compareNatural(b) < 0; });
            paged(installedMenu, 0, manufacturers.size(), [&](TrayMenu& target, size_t i)
            {
                const auto& indices = groups.at(manufacturers[i]);
                TrayMenu group;
                paged(group, 0, indices.size(), [&](TrayMenu& page, size_t j) { addInstalled(page, indices[j]); });
                target.submenu(shortName(manufacturers[i]) + " (" + String(static_cast<int>(indices.size())) + ")", group);
            });
        }
        else paged(installedMenu, 0, installed.size(), addInstalled);
    }
    root.submenu(text("plugins.installed", "Installed") + " (" + String(static_cast<int>(installed.size())) + ")", installedMenu);
    root.separator();
    command(root, text("audio.globalMute", "Mute output"), { TrayAction::Kind::mute, {}, !engine.isGlobalMuted() }, true, engine.isGlobalMuted());
    command(root, text("audio.globalBypass", "Bypass chain"), { TrayAction::Kind::bypassChain, {}, !engine.isGlobalBypassed() }, true, engine.isGlobalBypassed());
    root.separator();
    TrayMenu performanceMenu;
    const auto performanceTitle = text("diagnostics.group.performance", "Performance");
    performanceMenu.menu.addCustomItem(std::numeric_limits<int>::max(),
        std::make_unique<TrayPerformanceItem>(engine, owner, std::move(uiProcessId), locale, *appearance),
        nullptr, performanceTitle);
    root.submenu(performanceTitle, performanceMenu);
    root.separator();
    command(root, text("tray.quit", "Quit"), { TrayAction::Kind::quit });

    root.menu.setLookAndFeel(appearance.get());
    const auto position = Desktop::getInstance().getDisplays()
        .physicalToLogical(Point<float>(static_cast<float>(x), static_cast<float>(y))).roundToInt();
    // The hidden tray component normally lives at the primary monitor's origin.
    // Move only that invisible anchor to the clicked monitor before supplying it
    // as JUCE's target. Native per-monitor DPI then scales the whole popup once;
    // the font above includes only the independent accessibility text factor.
    owner.setBounds(position.x, position.y, 1, 1);
    lightHostModernLog("Tray layout textScale=" + String(textScale)
        + " monitorScale=" + String(owner.getPeer() ? owner.getPeer()->getPlatformScaleFactor() : 1.0)
        + " itemHeight=" + String(appearance->itemHeight()));
    SetForegroundWindow(static_cast<HWND>(owner.getWindowHandle()));
    root.menu.showMenuAsync(PopupMenu::Options().withTargetComponent(owner)
                            .withTargetScreenArea({ position.x, position.y, 1, 1 })
                            .withDeletionCheck(owner).withStandardItemHeight(appearance->itemHeight()),
        [appearance, actions = std::move(actions), completion = std::move(completion)](int selected) mutable
        {
            std::optional<TrayAction> action;
            if (selected > 0 && static_cast<size_t>(selected) <= actions.size()) action = actions[static_cast<size_t>(selected - 1)];
            // Complete after JUCE has destroyed its menu windows. This also
            // keeps editor opening/host shutdown outside menu teardown.
            MessageManager::callAsync([appearance, completion = std::move(completion), action] { completion(action); });
        });
}
}

#include "PreferenceMigration.h"
#include "BoundedProperties.h"
#include "SettingsReset.h"
#include <iostream>
#include <stdexcept>
#include <winioctl.h>

static bool createJunction(const juce::File& link, const juce::File& target)
{
    if (link.createDirectory().failed()) return false;
    const std::wstring substitute=L"\\??\\"+std::wstring(target.getFullPathName().toWideCharPointer());
    const std::wstring print(target.getFullPathName().toWideCharPointer());
    struct Header { DWORD tag; WORD length,reserved,substituteOffset,substituteLength,printOffset,printLength; };
    const auto payload=(substitute.size()+print.size()+2)*sizeof(wchar_t);
    std::vector<unsigned char> bytes(sizeof(Header)+payload,0);
    auto& h=*reinterpret_cast<Header*>(bytes.data()); h.tag=IO_REPARSE_TAG_MOUNT_POINT; h.length=static_cast<WORD>(payload+8);
    h.substituteLength=static_cast<WORD>(substitute.size()*sizeof(wchar_t)); h.printOffset=h.substituteLength+sizeof(wchar_t); h.printLength=static_cast<WORD>(print.size()*sizeof(wchar_t));
    std::memcpy(bytes.data()+sizeof(Header),substitute.c_str(),(substitute.size()+1)*sizeof(wchar_t));
    std::memcpy(bytes.data()+sizeof(Header)+h.printOffset,print.c_str(),(print.size()+1)*sizeof(wchar_t));
    const auto handle=CreateFileW(link.getFullPathName().toWideCharPointer(),GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_BACKUP_SEMANTICS,nullptr);
    if(handle==INVALID_HANDLE_VALUE)return false; DWORD returned=0;
    const bool ok=DeviceIoControl(handle,FSCTL_SET_REPARSE_POINT,bytes.data(),static_cast<DWORD>(bytes.size()),nullptr,0,&returned,nullptr)!=FALSE;
    CloseHandle(handle); return ok;
}

static void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
int main()
{
    using namespace juce;
    const auto root = File::getSpecialLocation(File::tempDirectory).getNonexistentChildFile("lhm-migration-test", "", false);
    struct Cleanup { File root; ~Cleanup() { root.deleteRecursively(); } } cleanup{root};
    try {
        root.createDirectory();
        const auto old = root.getChildFile("old/Light Host Modern.settings"), current = root.getChildFile("new/LightHostModern.settings");
        old.getParentDirectory().createDirectory(); current.getParentDirectory().createDirectory();
        old.replaceWithText("preferences-byte-for-byte");
        for (const auto* suffix : {".session.json", ".session.json.bak", ".session.json.pending", ".session.json.backup-pending", ".session.json.damaged-123", ".pre-session.bak"})
            old.getSiblingFile(old.getFileName() + suffix).replaceWithText(String("session ") + suffix);
        require(lightHostModern::migratePreferences(old, current).wasOk(), "Migration failed");
        require(current.loadFileAsString() == old.loadFileAsString(), "Preferences changed");
        require(current.getSiblingFile(current.getFileName() + ".session.json.pending").loadFileAsString() == "session .session.json.pending", "Pending recovery lost");
        require(old.existsAsFile(), "Legacy data removed");
        current.replaceWithText("newer-canonical-even-if-invalid");
        require(lightHostModern::migratePreferences(old, current).wasOk() && current.loadFileAsString() == "newer-canonical-even-if-invalid", "Canonical data overwritten");
        // Simulate interruption after the durable manifest, before all files publish.
        const auto stage = current.getSiblingFile(current.getFileName() + ".identity-migration");
        current.replaceWithText(old.loadFileAsString());
        require(stage.getChildFile("completed.json").moveFileTo(stage.getChildFile("manifest.json")), "Fixture manifest failed");
        const auto recovery = current.getSiblingFile(current.getFileName() + ".session.json.bak");
        require(recovery.deleteFile(), "Fixture delete failed");
        recovery.getSiblingFile(recovery.getFileName() + ".copy-pending").replaceWithText("interrupted partial copy");
        require(lightHostModern::migratePreferences(old, current).wasOk() && recovery.loadFileAsString() == "session .session.json.bak", "Interrupted publication not resumed");
        for (const auto& file : current.getParentDirectory().findChildFiles(File::findFiles, false, current.getFileName() + "*"))
            require(file.deleteFile(), "Fixture reset failed");
        require(lightHostModern::migratePreferences(old, current).wasOk() && !current.existsAsFile(), "Completed migration repeated after reset");
        {
            const auto file = root.getChildFile("bounded.settings");
            const auto deep = String::repeatedString("<a>", 100000) + String::repeatedString("</a>", 100000);
            PropertiesFile::Options options; options.millisecondsBeforeSaving = -1;
            lightHostModern::useBoundedPreferences(options);
            require(file.replaceWithText("<PROPERTIES><VALUE name=\"safe\" val=\"original\"/></PROPERTIES>"), "Write bounded preferences fixture");
            // Replace the file immediately after its validated tree is captured.
            // PropertiesFile must consume that tree without reopening the path.
            options.xmlFileReader = [&](const File& path) {
                auto value = lightHostModern::parseBoundedXml(path);
                require(path.replaceWithText(deep), "Replace XML during preferences load");
                return value;
            };
            { PropertiesFile prefs(file, options); require(prefs.isValidFile() && prefs.getValue("safe") == "original", "Preferences reread unvalidated replacement bytes"); }
            lightHostModern::useBoundedPreferences(options);
            { PropertiesFile prefs(file, options); require(!prefs.isValidFile(), "Deep initial preferences accepted"); }
            require(file.loadFileAsString() == deep, "Rejected preferences were overwritten");
            require(file.replaceWithText("<PROPERTIES/>"), "Write fresh preferences");
            { PropertiesFile prefs(file, options); prefs.setValue("escapedXml", deep); require(prefs.save(), "Save escaped deeply nested value without recursive parse"); }
            { PropertiesFile prefs(file, options); require(prefs.isValidFile() && prefs.getValue("escapedXml") == deep, "Escaped XML value lost during bounded save"); }
            String bounded;
            require(!lightHostModern::readBoundedText(file, bounded, 1024), "Bounded file reader accepted oversized input");
        }
        {
            namespace reset = lightHostModern::settingsReset;
            const auto preferences = root.getChildFile("reset/LightHostModern.settings");
            preferences.getParentDirectory().createDirectory();
            const reset::Paths paths{std::filesystem::path(preferences.getFullPathName().toWideCharPointer()),
                std::filesystem::path(root.getChildFile("reset/ui-settings.ini").getFullPathName().toWideCharPointer()),
                std::filesystem::path(root.getChildFile("reset/Logs/Captures").getFullPathName().toWideCharPointer())};
            preferences.replaceWithText("old preferences");
            for (const auto* suffix : {".profiles.xml", ".profiles.xml.bak", ".session.json.bak", ".session.json.pending"})
                preferences.getSiblingFile(preferences.getFileName()+suffix).replaceWithText("old profile or recovery");
            const auto recovery=root.getChildFile("reset/chain-edit-recovery-0123456789abcdef0123456789abcdef.json");
            recovery.replaceWithText("old graph");
            const auto exported=root.getChildFile("reset/My exported profile.xml"); exported.replaceWithText("keep export");
            const auto capture=root.getChildFile("reset/Logs/Captures/session/export.txt"); capture.getParentDirectory().createDirectory(); capture.replaceWithText("keep capture");
            reset::request(paths);
            const auto backup=preferences.getSiblingFile(preferences.getFileName()+".profiles.xml.bak");
            HANDLE blocked=CreateFileW(backup.getFullPathName().toWideCharPointer(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
            require(blocked!=INVALID_HANDLE_VALUE,"Lock reset fixture");
            bool failed=false; try{reset::perform(paths);}catch(...){failed=true;} CloseHandle(blocked);
            require(failed && std::filesystem::exists(reset::marker(paths)),"Partial reset failure lost retry marker");
            reset::perform(paths);
            require(!backup.existsAsFile() && !recovery.existsAsFile() && !std::filesystem::exists(reset::marker(paths)),"Reset retained profile backup or pending UI edits");
            require(exported.loadFileAsString()=="keep export" && capture.loadFileAsString()=="keep capture","Reset removed user exports");
            require(lightHostModern::parseBoundedXml(preferences)->hasTagName("PROPERTIES"),"Reset did not publish valid preferences");
            const auto migration=preferences.getSiblingFile(preferences.getFileName()+".identity-migration"); migration.createDirectory();
            migration.getChildFile("manifest.json").replaceWithText("corrupt pending migration");
            require(lightHostModern::migratePreferences(old,preferences).wasOk(),"Reset resumed an old identity migration");
            reset::request(paths); reset::perform(paths);
            require(exported.existsAsFile(),"Repeated reset removed an export");
            const auto linked=root.getChildFile("reset-linked");
            require(createJunction(linked,preferences.getParentDirectory()),"Create reset reparse fixture");
            bool rejected=false;
            try { auto unsafe=paths; unsafe.preferences=std::filesystem::path(linked.getChildFile(preferences.getFileName()).getFullPathName().toWideCharPointer()); reset::request(unsafe); }
            catch(...) { rejected=true; }
            const bool removed=RemoveDirectoryW(linked.getFullPathName().toWideCharPointer())!=FALSE;
            require(rejected && removed && exported.existsAsFile(),"Reset followed a junction");
        }
        std::cout << "Migration, recovery family, canonical precedence and interrupted copy passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}

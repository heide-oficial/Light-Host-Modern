#include "ScanCacheJournal.h"
#include <iostream>

using namespace juce;
using lightHostModern::scan::CacheJournal;
static void require(bool condition, const char* reason)
{ if (!condition) throw std::runtime_error(reason); }

int main(int argc, char** argv)
{
    const auto folder = File::getSpecialLocation(File::tempDirectory).getChildFile("LightHostCacheTest-" + Uuid().toString());
    const auto file = folder.getChildFile("module.xml");
    const auto journal = file.withFileExtension("journal");
    struct Cleanup { File folder; ~Cleanup() { folder.deleteRecursively(); } } cleanup{folder};
    try {
        require(folder.createDirectory().wasOk(), "create private fixture directory");
        if (argc == 2 && String(argv[1]) == "--hostile-xml") {
            const auto deep=String::repeatedString("<a>",100000)+String::repeatedString("</a>",100000);
            require(file.replaceWithText(deep),"Write hostile cache");
            require(!CacheJournal::load(file),"Deep cache reached recursive parser");
            require(file.loadFileAsString()==deep,"Rejected cache was changed");
            XmlElement baseline("SCAN"); baseline.setAttribute("cacheVersion",lightHostModern::scan::metadataCacheVersion);
            baseline.setAttribute("complete",false); baseline.setAttribute("checkpointId","test");
            require(baseline.writeTo(file),"Write journal baseline");
            {
                FileOutputStream output(journal); const auto bytes=deep.getNumBytesAsUTF8();
                require(output.writeInt(static_cast<int>(bytes)) && output.write(deep.toRawUTF8(),bytes),"Write hostile frame");
                const auto hash=SHA256(deep.toRawUTF8(),bytes).toHexString();
                require(output.write(hash.toRawUTF8(),64),"Write valid hostile frame checksum"); output.flush();
            }
            auto restored=CacheJournal::load(file);
            require(restored && restored->getNumChildElements()==0,"Deep journal corrupted valid baseline");
            require(file.replaceWithText(String::repeatedString(" ",lightHostModern::scan::maximumResponseBytes+1)),"Write oversized cache");
            require(!CacheJournal::load(file),"Oversized cache accepted");
            return 0;
        }
        ChildProcess hostile;
        require(hostile.start(StringArray{File::getSpecialLocation(File::currentExecutableFile).getFullPathName(),"--hostile-xml"}),"Launch hostile XML child");
        const auto finished=hostile.waitForProcessToFinish(10000);
        if(!finished)hostile.kill();
        require(finished && hostile.getExitCode()==0,"Hostile XML cache/journal child crashed or hung");
        for (const int length : {0, 63, 64, 65, 262143, 262144, 262145, 1048583}) {
            MemoryBlock bytes(static_cast<size_t>(length));
            for (int index = 0; index < length; ++index)
                static_cast<unsigned char*>(bytes.getData())[index] = static_cast<unsigned char>(index * 37);
            const auto binary = folder.getChildFile("buffer-boundary.dll");
            require(binary.replaceWithData(bytes.getData(), bytes.getSize()), "write hash fixture");
            // JUCE replaces zero bytes by deleting the file. This case needs an
            // existing empty file to exercise the buffered hash boundary.
            require(binary.create().wasOk(), "create empty hash fixture");
            MemoryOutputStream expected;
            expected.writeString(".");
            expected.writeInt64(binary.getSize());
            expected.writeInt64(binary.getLastModificationTime().toMilliseconds());
            expected.writeString(SHA256(bytes).toHexString());
            require(lightHostModern::scan::fingerprint(binary) == SHA256(expected.getData(), expected.getDataSize()).toHexString(),
                "buffering changed fingerprint bytes");
        }
        XmlElement baseline("SCAN");
        baseline.setAttribute("version", lightHostModern::scan::scannerProtocolVersion);
        baseline.setAttribute("mode", "probe");
        baseline.setAttribute("path", "fixture.vst3");
        baseline.setAttribute("fingerprint", "original");
        CacheJournal writer(file);
        require(writer.begin(baseline), "begin checkpoint");
        const auto initialSize = file.getSize();
        int64 oldRewriteBytes = 0;
        XmlElement oldSnapshot(baseline);
        for (int index = 0; index < 256; ++index) {
            XmlElement entry("ENTRY");
            const auto key = SHA256(String(index).toRawUTF8(), String(index).getNumBytesAsUTF8()).toHexString();
            entry.setAttribute("knownId", key);
            entry.setAttribute("verifiedMetadata", "verified");
            entry.createNewChildElement("PLUGIN")->setAttribute("name", "Fixture " + String(index));
            require(writer.append(entry), "append checkpoint");
            oldSnapshot.addChildElement(new XmlElement(entry));
            oldRewriteBytes += static_cast<int64>(oldSnapshot.toString().getNumBytesAsUTF8());
        }
        require(file.getSize() == initialSize, "snapshot rewritten during class loop");
        require(journal.getSize() < oldRewriteBytes / 10, "checkpoint writes should grow linearly");
        auto restored = CacheJournal::load(file);
        require(restored && restored->getNumChildElements() == 256, "restore all classes before compaction");
        // Compaction closes the writer before exercising truncated records.
        restored->setAttribute("complete", false);
        require(writer.compact(*restored), "compact partial cache");
        require(!journal.existsAsFile(), "remove compacted journal");
        XmlElement updated(*restored->getFirstChildElement());
        updated.setAttribute("revision", 2);
        {
            CacheJournal resumed(file);
            require(resumed.begin(*restored), "resume partial checkpoint");
            require(resumed.append(updated), "update one class");
        }
        // Simulate interruption after the first bytes of a subsequent frame.
        require(journal.appendData("bad", 3), "append torn tail");
        auto recovered = CacheJournal::load(file);
        require(recovered && recovered->getNumChildElements() == 256, "torn tail lost earlier records");
        require(recovered->getFirstChildElement()->getIntAttribute("revision") == 2, "latest valid class delta lost");
        MemoryBlock previousJournal;
        require(journal.loadFileAsData(previousJournal), "read journal fixture");
        auto corrupted = previousJournal;
        static_cast<char*>(corrupted.getData())[4] ^= 1;
        require(journal.replaceWithData(corrupted.getData(), corrupted.getSize()), "corrupt journal fixture");
        auto withoutCorrupt = CacheJournal::load(file);
        require(withoutCorrupt && withoutCorrupt->getNumChildElements() == 256
            && !withoutCorrupt->getFirstChildElement()->hasAttribute("revision"), "corrupt delta was accepted");
        recovered->setAttribute("complete", true);
        require(writer.compact(*recovered), "compact completed module");
        auto complete = CacheJournal::load(file);
        require(complete && complete->getBoolAttribute("complete") && complete->getNumChildElements() == 256, "complete cache");
        // A stale journal from an older module/fingerprint must not attach.
        baseline.setAttribute("fingerprint", "changed");
        {
            CacheJournal changed(file);
            require(changed.begin(baseline), "begin changed module");
        }
        require(journal.appendData(previousJournal.getData(), previousJournal.getSize()), "restore stale journal fixture");
        auto rejected = CacheJournal::load(file);
        require(rejected && rejected->getNumChildElements() == 0, "stale journal resurrected old classes");
        require(writer.compact(baseline), "close changed journal");
        const auto blocked = folder.getChildFile("blocked");
        require(blocked.replaceWithText("file, not directory"), "blocked destination fixture");
        CacheJournal unavailable(blocked.getChildFile("module.xml"));
        require(!unavailable.begin(baseline), "unwritable destination reported success");
        std::cout << "Cache journal recovery, compaction, identity and linear writes passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}

#pragma once
#include <algorithm>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace lightHostModern::ui
{
class DialogLease
{
public:
    explicit DialogLease(void* root):identity(root) {std::lock_guard<std::mutex> guard(mutex);owned=root&&roots.insert(root).second;}
    DialogLease(DialogLease const&)=delete;
    DialogLease& operator=(DialogLease const&)=delete;
    ~DialogLease(){if(owned){std::lock_guard<std::mutex> guard(mutex);roots.erase(identity);}}
    explicit operator bool()const{return owned;}
private:
    inline static std::mutex mutex;
    inline static std::set<void*> roots;
    void* identity=nullptr;bool owned=false;
};
// UI-apartment intent admission, before a command can be built from confirmed
// state. Wire requests still use the single bounded HostTransport FIFO.
class IntentAdmission
{
public:
    struct Entry { std::string key; size_t bytes; bool started=false, superseded=false; };
    using Ticket = std::shared_ptr<Entry>;
    Ticket reserve(std::string key, size_t size)
    {
        if (size > maximumBytes) return {};
        if (!key.empty()) for (size_t i=entries.size();i>0;--i) {
            const auto& entry=entries[i-1];
            if(entry->started||entry->key.empty())break;
            if(entry->key==key){entry->superseded=true;bytes-=entry->bytes;entries.erase(entries.begin()+i-1);break;}
        }
        if (entries.size()>=64 || size>maximumBytes-bytes) return {};
        auto ticket=std::make_shared<Entry>(Entry{std::move(key),size});
        entries.push_back(ticket);bytes+=size;return ticket;
    }
    bool ready(Ticket const& ticket) const { return !entries.empty() && entries.front()==ticket; }
    bool pending(std::string const& key) const { return std::any_of(entries.begin(),entries.end(),[&](auto const& entry){return entry->key==key;}); }
    void release(Ticket const& ticket)
    {
        auto found=std::find(entries.begin(),entries.end(),ticket);
        if(found!=entries.end()){bytes-=ticket->bytes;entries.erase(found);}
    }
private:
    static constexpr size_t maximumBytes=8*1024*1024;
    size_t bytes=0;
    std::deque<Ticket> entries;
};

struct EditRevision
{
    uint64_t local=0, confirmed=0;
    void changed() { ++local; }
    bool acknowledge(uint64_t revision) { confirmed=(std::max)(confirmed,revision);return confirmed==local; }
    void reset() { local=confirmed=0; }
};

class GainDeltas
{
public:
    struct Value { std::wstring id; uint32_t lane; double gain; uint64_t revision; };
    void change(std::wstring id,uint32_t lane,double gain) {
        const auto key=std::make_pair(id,lane);pending[key]={std::move(id),lane,gain,++revision};
    }
    std::vector<Value> batch(size_t limit=1024) const {
        std::vector<Value> result;for(const auto& item:pending){if(result.size()==limit)break;result.push_back(item.second);}return result;
    }
    void acknowledge(std::vector<Value> const& values) {
        for(const auto& value:values){auto it=pending.find({value.id,value.lane});if(it!=pending.end()&&it->second.revision==value.revision)pending.erase(it);}
    }
    bool empty() const {return pending.empty();}
    void clear(){pending.clear();}
private:
    uint64_t revision=0;
    std::map<std::pair<std::wstring,uint32_t>,Value> pending;
};
}

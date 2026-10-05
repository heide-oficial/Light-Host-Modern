#pragma once
#include <array>
#include <cstddef>

namespace lightHostModern
{
// A paginated host document can exceed one transport frame. Keep these
// contracts separate: callers parsing wire/external data retain the small cap.
inline constexpr size_t maximumMessageJsonBytes = 4u * 1024 * 1024;
inline constexpr size_t maximumSnapshotJsonBytes = 32u * 1024 * 1024;
template<class Text> bool boundedJsonStructure(const Text& text)
{
    std::array<char,64> stack{};size_t depth=0,structures=0;bool quoted=false,escaped=false;
    for(auto c:text) {
        if(quoted){if(escaped)escaped=false;else if(c=='\\')escaped=true;else if(c=='"')quoted=false;continue;}
        if(c=='"'){quoted=true;continue;}
        if(c=='['||c=='{'){if(depth==stack.size()||++structures>131072)return false;stack[depth++]=static_cast<char>(c);}
        else if(c==']'||c=='}'){if(!depth||stack[--depth]!=(c==']'?'[':'{'))return false;}
    }
    return !quoted&&depth==0;
}
}

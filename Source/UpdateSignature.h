#pragma once
#include "UpdateWindows.h"
#include "UpdateTrustKeys.h"
#include <cstring>

namespace lightHostModern::update
{
inline std::vector<unsigned char> decodeHex(std::string_view value)
{
    require(value.size() <= 32768 && value.size() % 2 == 0, "signature_invalid");
    auto digit=[](char c) -> unsigned {
        if(c>='0'&&c<='9') return unsigned(c-'0');
        if(c>='a'&&c<='f') return unsigned(c-'a'+10);
        if(c>='A'&&c<='F') return unsigned(c-'A'+10);
        throw Error("signature_invalid");
    };
    std::vector<unsigned char> bytes; bytes.reserve(value.size()/2);
    for(size_t i=0;i<value.size();i+=2) bytes.push_back((unsigned char)(digit(value[i])*16+digit(value[i+1])));
    return bytes;
}
inline void verifyManifestSignature(std::string_view body, std::string_view signatureHex,
    std::string_view keyId, const TrustedUpdateKey* keys = trustedUpdateKeys.data(), size_t keyCount = trustedUpdateKeys.size())
{
    require(body.size()>0 && body.size()<=4*1024*1024, "metadata_invalid");
    const TrustedUpdateKey* trusted=nullptr;
    for(size_t index=0;index<keyCount;++index) { const auto& key=keys[index]; if(key.id==keyId) { require(!trusted,"signature_invalid"); trusted=&key; } }
    require(trusted!=nullptr,"signature_untrusted");
    const auto blob=decodeHex(trusted->publicBlobHex), signature=decodeHex(signatureHex);
    require(blob.size()>=sizeof(BCRYPT_RSAKEY_BLOB),"signature_invalid");
    BCRYPT_RSAKEY_BLOB header{};std::memcpy(&header,blob.data(),sizeof(header));
    require(header.Magic==BCRYPT_RSAPUBLIC_MAGIC && header.BitLength>=2048 && header.BitLength<=8192
        && header.cbPrime1==0 && header.cbPrime2==0 && signature.size()==header.BitLength/8,"signature_invalid");
    struct Keys { BCRYPT_ALG_HANDLE algorithm=nullptr; BCRYPT_KEY_HANDLE key=nullptr;
        ~Keys(){if(key)BCryptDestroyKey(key);if(algorithm)BCryptCloseAlgorithmProvider(algorithm,0);} } handles;
    require(BCryptOpenAlgorithmProvider(&handles.algorithm,BCRYPT_RSA_ALGORITHM,nullptr,0)>=0,"signature_invalid");
    require(BCryptImportKeyPair(handles.algorithm,nullptr,BCRYPT_RSAPUBLIC_BLOB,&handles.key,
        const_cast<PUCHAR>(blob.data()),(ULONG)blob.size(),0)>=0,"signature_invalid");
    Sha256 hash;hash.append(body.data(),body.size());const auto wide=hash.finish();
    auto digest=decodeHex(std::string(wide.begin(),wide.end()));
    BCRYPT_PKCS1_PADDING_INFO padding{BCRYPT_SHA256_ALGORITHM};
    require(BCryptVerifySignature(handles.key,&padding,digest.data(),(ULONG)digest.size(),
        const_cast<PUCHAR>(signature.data()),(ULONG)signature.size(),BCRYPT_PAD_PKCS1)>=0,"signature_invalid");
}
inline std::string readSmallFile(const std::filesystem::path& path, size_t limit=4*1024*1024)
{
    require(std::filesystem::file_size(extendedFilePath(path))<=limit,"metadata_invalid");
    FileInput source(path);std::string result;std::array<char,16384> buffer{};
    while(auto count=source.read(buffer.data(),buffer.size())) {
        require(count<=limit-result.size(),"metadata_invalid");result.append(buffer.data(),count);
    }
    return result;
}
}

#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <compressapi.h>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace re4craft {
// Decoded catalog size differs from resident x86 storage. A single original
// map is reconstructed only for its GPU upload or device Reset. No pixels,
// dimensions, channels, UVs or sRGB semantics change through this storage.
inline constexpr size_t MaxVehicleDecodedTextureBytes=512u*1024u*1024u;
inline constexpr size_t MaxVehicleTextureStorageBytes=384u*1024u*1024u;
inline constexpr size_t MaxVehicleTextureMapBytes=64u*1024u*1024u;

namespace vehicle_texture_storage_detail {
struct CompressionApi {
    decltype(&CreateCompressor) createCompressor{};
    decltype(&CloseCompressor) closeCompressor{};
    decltype(&Compress) compress{};
    decltype(&ResetCompressor) resetCompressor{};
    decltype(&CreateDecompressor) createDecompressor{};
    decltype(&CloseDecompressor) closeDecompressor{};
    decltype(&Decompress) decompress{};
    CompressionApi(){
        // Never search a game's working directory or load a mod's DLL with
        // this name. The system implementation requires no packaged DLL.
        wchar_t system[MAX_PATH]{};const auto count=GetSystemDirectoryW(system,MAX_PATH);
        if(!count||count>=MAX_PATH)return;
        const std::wstring path=std::wstring(system,count)+L"\\cabinet.dll";
        const auto library=LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
        if(!library)return;
        createCompressor=reinterpret_cast<decltype(createCompressor)>(GetProcAddress(library,"CreateCompressor"));
        closeCompressor=reinterpret_cast<decltype(closeCompressor)>(GetProcAddress(library,"CloseCompressor"));
        compress=reinterpret_cast<decltype(compress)>(GetProcAddress(library,"Compress"));
        resetCompressor=reinterpret_cast<decltype(resetCompressor)>(GetProcAddress(library,"ResetCompressor"));
        createDecompressor=reinterpret_cast<decltype(createDecompressor)>(GetProcAddress(library,"CreateDecompressor"));
        closeDecompressor=reinterpret_cast<decltype(closeDecompressor)>(GetProcAddress(library,"CloseDecompressor"));
        decompress=reinterpret_cast<decltype(decompress)>(GetProcAddress(library,"Decompress"));
        // Retain the single system module reference for process lifetime;
        // transport and render threads may still use its function pointers.
    }
    bool available()const{return createCompressor&&closeCompressor&&compress&&resetCompressor&&createDecompressor&&closeDecompressor&&decompress;}
};
inline const CompressionApi& api(){static const CompressionApi instance;return instance;}
struct Compressor {
    const CompressionApi& api;COMPRESSOR_HANDLE handle{};
    explicit Compressor(const CompressionApi& value):api(value){api.createCompressor(COMPRESS_ALGORITHM_XPRESS_HUFF,nullptr,&handle);}
    ~Compressor(){if(handle)api.closeCompressor(handle);}
};
struct Decompressor {
    const CompressionApi& api;DECOMPRESSOR_HANDLE handle{};
    explicit Decompressor(const CompressionApi& value):api(value){api.createDecompressor(COMPRESS_ALGORITHM_XPRESS_HUFF,nullptr,&handle);}
    ~Decompressor(){if(handle)api.closeDecompressor(handle);}
};
inline void discard(std::vector<uint8_t>& bytes){std::vector<uint8_t>().swap(bytes);}
}

// False means retain the actual raw source, never a partial compressed result.
// Allocation failure propagates to the existing owning-worker recovery path.
inline bool compressedTexture(const std::vector<uint8_t>& raw,std::vector<uint8_t>& output){
    if(&raw==&output)return false;
    using namespace vehicle_texture_storage_detail;
    discard(output);
    if(raw.empty()||raw.size()>MaxVehicleTextureMapBytes)return false;
    const auto& functions=api();if(!functions.available())return false;
    Compressor compressor(functions);if(!compressor.handle)return false;
    if(raw.size()<2)return false;
    // A size query reports the worst case (rawBytes+40), not useful packed
    // size. Start at one quarter of the raw map, bounded by its original size,
    // and retry with separate reset state instead of reserving a second full
    // 64MiB map for every normally compressible source.
    size_t capacity=raw.size()/4;if(capacity<65536)capacity=65536;
    if(capacity>=raw.size())capacity=raw.size()-1;
    std::vector<uint8_t> candidate;SIZE_T written{};
    while(true){
        candidate=std::vector<uint8_t>(capacity);written=0;
        if(functions.compress(compressor.handle,raw.data(),raw.size(),candidate.data(),candidate.size(),&written))break;
        const auto error=GetLastError();
        if(error!=ERROR_INSUFFICIENT_BUFFER||capacity==raw.size()-1||!functions.resetCompressor(compressor.handle))return false;
        const size_t doubled=capacity*2;capacity=doubled<raw.size()?doubled:raw.size()-1;
    }
    if(!written||written>candidate.size()||written>=raw.size())return false;
    // resize() alone would retain the worst-case64MiB capacity. Copy only
    // the actual result so resident bytes track allocated storage as well.
    std::vector<uint8_t> compact(candidate.begin(),candidate.begin()+written);
    output=std::move(compact);return true;
}

// Internal storage reconstruction. Exact caller-owned dimensions establish
// expected size before allocating; malformed/truncated data cannot be shown.
inline bool decodedTexture(const std::vector<uint8_t>& packed,size_t expected,std::vector<uint8_t>& output){
    if(&packed==&output)return false;
    using namespace vehicle_texture_storage_detail;
    discard(output);
    if(packed.empty()||packed.size()>MaxVehicleTextureMapBytes||!expected||expected>MaxVehicleTextureMapBytes)return false;
    const auto& functions=api();if(!functions.available())return false;
    Decompressor decompressor(functions);if(!decompressor.handle)return false;
    std::vector<uint8_t> candidate(expected);SIZE_T written{};
    if(!functions.decompress(decompressor.handle,packed.data(),packed.size(),candidate.data(),candidate.size(),&written)
        ||written!=expected)return false;
    output=std::move(candidate);return true;
}
}

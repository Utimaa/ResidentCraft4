#include "../native/vehicle_texture_storage.hpp"
#include <cstdio>
#include <random>
#include <stdexcept>

namespace {
unsigned checks{};
void check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
void exact(const std::vector<uint8_t>& raw){
    std::vector<uint8_t> packed,restored;
    check(re4craft::compressedTexture(raw,packed),"useful source compression failed");
    check(!packed.empty()&&packed.size()<raw.size(),"compressed storage must be smaller");
    check(packed.capacity()==packed.size(),"packed map retained oversized scratch capacity");
    check(re4craft::decodedTexture(packed,raw.size(),restored),"source reconstruction failed");
    check(restored==raw,"source bytes changed");
    check(!re4craft::decodedTexture(packed,raw.size()-1,restored)&&restored.empty(),"wrong decoded dimensions accepted");
    check(!re4craft::decodedTexture(packed,raw.size()+1,restored)&&restored.empty(),"oversized decoded dimensions accepted");
    packed.resize(packed.size()/2);
    check(!re4craft::decodedTexture(packed,raw.size(),restored)&&restored.empty(),"truncated packed map accepted");
}
}
int main(){
    try{
        check(re4craft::vehicle_texture_storage_detail::api().available(),"system Cabinet API unavailable");
        check(re4craft::MaxVehicleDecodedTextureBytes==512u*1024u*1024u,"logical catalog bound changed");
        check(re4craft::MaxVehicleTextureStorageBytes==384u*1024u*1024u,"actual resident bound changed");
        std::vector<uint8_t> output{1,2,3};
        check(!re4craft::compressedTexture({},output)&&output.empty(),"empty raw input accepted");
        check(!re4craft::decodedTexture({},64,output)&&output.empty(),"empty compressed input accepted");
        check(!re4craft::decodedTexture({1},0,output)&&output.empty(),"zero decoded count accepted");
        check(!re4craft::decodedTexture({1},re4craft::MaxVehicleTextureMapBytes+1,output)&&output.empty(),"decoded allocation limit bypassed");
        std::vector<uint8_t> tooLarge(re4craft::MaxVehicleTextureMapBytes+1);
        check(!re4craft::compressedTexture(tooLarge,output)&&output.empty(),"raw allocation limit bypassed");
        check(!re4craft::decodedTexture(tooLarge,1,output)&&output.empty(),"packed allocation limit bypassed");
        std::vector<uint8_t>().swap(tooLarge);
        exact(std::vector<uint8_t>(1024*1024,173)); // Exact scalar R8 data.
        std::vector<uint8_t> normals(2*1024*1024);
        for(size_t i=0;i<normals.size();i+=2){normals[i]=uint8_t((i/2)%256);normals[i+1]=128;}
        exact(normals); // Exact RG8 normal planes.
        std::vector<uint8_t> color(re4craft::MaxVehicleTextureMapBytes);
        for(size_t i=0;i<color.size();i+=4){color[i]=11;color[i+1]=23;color[i+2]=45;color[i+3]=uint8_t((i/4)%256);}
        exact(color); // Complete4096² RGBA source including alpha, no resized map.
        std::vector<uint8_t>().swap(color);
        std::mt19937 random(149);
        std::vector<uint8_t> noise(1024*1024);
        for(auto& byte:noise)byte=uint8_t(random());
        const auto original=noise;
        const bool useful=re4craft::compressedTexture(noise,output);
        check(noise==original,"raw fallback altered original noise bytes");
        if(useful){std::vector<uint8_t> restored;check(re4craft::decodedTexture(output,noise.size(),restored)&&restored==noise,"noise roundtrip changed bytes");}
        else check(output.empty(),"raw fallback leaked failed packed result");
        check(!re4craft::compressedTexture(noise,noise)&&noise==original,"aliased compression changed raw source");
        check(!re4craft::decodedTexture(noise,noise.size(),noise)&&noise==original,"aliased decompression changed source");
        const std::vector<uint8_t> corrupt{0x11,0x23,0x45,0x67,0x89};
        check(!re4craft::decodedTexture(corrupt,1024,output)&&output.empty(),"malformed compressed map shown");
        std::printf("Vehicle texture storage: %u CPU assertions passed (x86, no graphics/game)\n",checks);return 0;
    }catch(const std::exception& error){std::fprintf(stderr,"FAIL: %s\n",error.what());return 1;}
}

#include "../native/vehicle_display_color.hpp"
#include "../native/vehicle_daylight.hpp"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3dcompiler.h>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <string>

using namespace re4craft;
namespace {
unsigned checks{};
void check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
void expectNear(float actual,double expected,const char* message){check(std::abs(double(actual)-expected)<.000002,message);}
void compileDisplayShader(){
    const auto compiler=LoadLibraryExW(L"d3dcompiler_47.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    check(compiler!=nullptr,"system shader compiler unavailable");
    const auto compile=reinterpret_cast<decltype(&D3DCompile)>(GetProcAddress(compiler,"D3DCompile"));
    check(compile!=nullptr,"CPU shader compiler entry unavailable");
    const std::string shader=std::string(VehicleDisplayToneMapHlsl)+std::string(VehicleDaylightHlsl)+
        "float4 main(float3 hdr:TEXCOORD0):COLOR0{return float4(vehicleDisplayLinear(hdr*vehicleDaylightAmbientScale+max(vehicleDaylightSunDirection().y,0)*vehicleDaylightDirectScale*vehicleDaylightDirectSpecularScale+hdr*vehicleDaylightReflectionScale),.25);}";
    ID3DBlob* bytecode{},*errors{};
    const auto result=compile(shader.data(),shader.size(),"Pure vehicle display test",nullptr,nullptr,"main","ps_3_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&bytecode,&errors);
    if(FAILED(result)&&errors)std::fprintf(stderr,"%.*s\n",int(errors->GetBufferSize()),static_cast<const char*>(errors->GetBufferPointer()));
    const bool valid=SUCCEEDED(result)&&bytecode&&bytecode->GetBufferSize()>0;
    if(bytecode)bytecode->Release();if(errors)errors->Release();FreeLibrary(compiler);
    check(valid,"production display transform is not shader-model3 compatible");
}
}
int main(){
    try {
        // Independent double-precision golden outputs of the installed ACES
        // transform. The original bridge hard-clipped every value above one.
        const auto black=vehicleToneMapDisplayLinear({0,0,0});
        for(auto value:black)check(value==0,"black gained display light");
        const auto coloredHighlight=vehicleToneMapDisplayLinear({4,2,1});
        expectNear(coloredHighlight[0],.928132447742561,"source ACES red highlight");
        expectNear(coloredHighlight[1],.811037343923067,"source ACES green highlight");
        expectNear(coloredHighlight[2],.669907174472798,"source ACES blue highlight");
        // Independent double-precision reference: exposure is applied BEFORE
        // the unchanged source ACES fit, never to encoded SDR or source paint.
        check(VehicleDisplayExposureScale==.5f,"bridge calibration is not exactly minus one EV");
        const auto exposed=vehicleDisplayLinear({4,2,1});
        expectNear(exposed[0],.8297920761526782,"pre-tone exposure red golden");
        expectNear(exposed[1],.6302816008333370,"pre-tone exposure green golden");
        expectNear(exposed[2],.4302783197565232,"pre-tone exposure blue golden");
        for(unsigned i=0;i<3;i++)check(exposed[i]<coloredHighlight[i],"exposure failed to reduce highlight energy");
        const auto unitExposure=vehicleDisplayLinear({4,2,1},1.f);
        for(unsigned i=0;i<3;i++)check(unitExposure[i]==coloredHighlight[i],"unit exposure changed the original ACES transform");
        const auto encoded=vehicleDisplaySrgb({4,2,1});
        expectNear(encoded[0],.9210895450148260,"exposed SDR red golden");
        expectNear(encoded[1],.8154127055845819,"exposed SDR green golden");
        expectNear(encoded[2],.6874172765374370,"exposed SDR blue golden");
        check(encoded[0]>encoded[1]&&encoded[1]>encoded[2],"bright paint lost its source hue");
        for(auto value:encoded)check(value<1&&value>0,"ordinary HDR highlight clipped to white");
        // Actual captured factory Sea Blue values, with no tint or recolor.
        const std::array<float,3> blue{.16f,.28f,.56f};
        const auto originalBlue=blue;
        const auto blueDisplay=vehicleDisplaySrgb(blue);
        expectNear(blueDisplay[0],.1907745817673033,"actual Sea Blue exposed red");
        expectNear(blueDisplay[1],.2969460069154371,"actual Sea Blue exposed green");
        expectNear(blueDisplay[2],.4626030317048637,"actual Sea Blue exposed blue");
        check(blue==originalBlue,"display calibration mutated actual source paint");
        check(blueDisplay[2]>blueDisplay[1]&&blueDisplay[1]>blueDisplay[0],"neutral exposure lost actual blue paint ordering");
        const auto brighterBlue=vehicleDisplaySrgb(blue,1.f);
        for(unsigned i=0;i<3;i++)check(blueDisplay[i]<brighterBlue[i],"neutral calibration failed to reduce ordinary painted brightness");
        // Independent hemisphere anchors, then meaningful dark-side paint
        // comparisons. Daylight increases neutral shadow fill while softening
        // specular illumination; source paint values, emission and alpha remain.
        const auto ground=vehicleDaylightEnvironment(-1),horizon=vehicleDaylightEnvironment(0),zenith=vehicleDaylightEnvironment(1);
        for(unsigned channel=0;channel<3;channel++){
            expectNear(ground[channel],std::array<double,3>{.26,.255,.245}[channel],"daylight ground anchor");
            expectNear(horizon[channel],std::array<double,3>{.62,.63,.64}[channel],"daylight horizon anchor");
            expectNear(zenith[channel],std::array<double,3>{.62,.64,.68}[channel],"daylight zenith anchor");
        }
        check(VehicleDaylightDirectScale==2.2f,"daylight changed direct diffuse energy");
        check(VehicleDaylightAmbientScale==1.1f,"daylight fill calibration changed");
        check(VehicleDaylightDirectSpecularScale==.6f,"soft daylight direct specular calibration changed");
        check(VehicleDaylightReflectionScale==.55f,"soft daylight reflection calibration changed");
        const auto oldShadow=vehicleDisplaySrgb({blue[0]*.34f*.65f,blue[1]*.375f*.65f,blue[2]*.43f*.65f});
        const auto dayShadow=vehicleDisplaySrgb({blue[0]*horizon[0]*VehicleDaylightAmbientScale,blue[1]*horizon[1]*VehicleDaylightAmbientScale,blue[2]*horizon[2]*VehicleDaylightAmbientScale});
        float oldLuma=0,dayLuma=0;
        for(unsigned channel=0;channel<3;channel++){
            check(dayShadow[channel]>oldShadow[channel],"daylight failed to brighten dark-side factory paint");
            check(dayShadow[channel]<.85f,"daylight shadow fill washed source paint to white");
            oldLuma+=oldShadow[channel]*std::array<float,3>{.2126f,.7152f,.0722f}[channel];
            dayLuma+=dayShadow[channel]*std::array<float,3>{.2126f,.7152f,.0722f}[channel];
        }
        check(dayLuma>oldLuma*1.5f,"daylight fill improvement is too small");
        check(dayShadow[2]>dayShadow[1]&&dayShadow[1]>dayShadow[0],"daylight fill lost factory blue hue");
        for(float vertical:{-2.f,-1.f,-.75f,-.25f,0.f,.25f,.75f,1.f,2.f}){
            const auto environment=vehicleDaylightEnvironment(vertical);
            for(float value:environment)check(std::isfinite(value)&&value>=.245f-.000001f&&value<=.68f+.000001f,"daylight reflection radiance outside conservative bounds");
            const auto range=std::minmax_element(environment.begin(),environment.end());
            check(*range.second-*range.first<=.06f+.000001f,"daylight hemisphere introduced a strong color cast");
        }
        const auto neutralGray=vehicleDisplaySrgb({horizon[0]*.5f*VehicleDaylightAmbientScale,horizon[1]*.5f*VehicleDaylightAmbientScale,horizon[2]*.5f*VehicleDaylightAmbientScale});
        const auto neutralRange=std::minmax_element(neutralGray.begin(),neutralGray.end());
        check(*neutralRange.second-*neutralRange.first<.02f,"neutral gray acquired a visible daylight color cast");
        check(vehicleDaylightEnvironment(-2)==ground&&vehicleDaylightEnvironment(2)==zenith,"daylight did not clamp hemisphere bounds");
        for(float scale:{VehicleDisplayExposureMinimum,VehicleDisplayExposureScale,1.f,VehicleDisplayExposureMaximum}){
            check(validVehicleDisplayExposure(scale),"valid finite exposure rejected");
            const auto output=vehicleDisplaySrgb(blue,scale);
            for(auto value:output)check(std::isfinite(value)&&value>=0&&value<=1,"bounded exposure left finite SDR limits");
        }
        for(float scale:{0.f,-1.f,VehicleDisplayExposureMinimum*.5f,VehicleDisplayExposureMaximum*2.f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}){
            check(!validVehicleDisplayExposure(scale),"invalid exposure accepted");
            bool rejected=false;try{vehicleDisplayLinear(blue,scale);}catch(const std::invalid_argument&){rejected=true;}
            check(rejected,"invalid exposure silently changed output");
        }
        for(float value:{std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}){
            bool rejected=false;try{vehicleDisplayLinear({value,1,1});}catch(const std::invalid_argument&){rejected=true;}
            check(rejected,"nonfinite display radiance accepted");
        }
        for(auto value:vehicleDisplayLinear({0,0,0}))check(value==0,"neutral exposure added light to black");
        // Preserve hue across a lighting range, including the actual captured
        // Champagne paint. Only lit output changes, never source map texels.
        const std::array<float,3> champagne{.663f,.594f,.533f};
        // The unchanged ACES black toe clips two nearly-black channels at
        // this energy. Check its independent golden result rather than
        // requiring distinct hues below the transform's black threshold.
        const auto darkChampagne=vehicleDisplayLinear({.00663f,.00594f,.00533f});
        expectNear(darkChampagne[0],.000003814601801404,"unchanged ACES near-black toe");
        check(darkChampagne[1]==0&&darkChampagne[2]==0,"ACES near-black toe gained unsupported light");
        float previous=0;
        for(float energy:{.02f,.1f,.5f,1.f,2.f,4.f}){
            const auto display=vehicleDisplaySrgb({champagne[0]*energy,champagne[1]*energy,champagne[2]*energy});
            check(display[0]>display[1]&&display[1]>display[2],"Champagne highlight lost warm paint ordering");
            check(display[0]>=previous,"display transform reversed increasing energy");previous=display[0];
            for(auto value:display)check(std::isfinite(value)&&value>=0&&value<=1,"display color left SDR bounds");
        }
        const auto intense=vehicleDisplaySrgb({100000,100000,100000});
        for(auto value:intense)check(std::isfinite(value)&&value<=1,"genuine high-nits emission overflowed");
        compileDisplayShader();
        std::printf("PASS: %u pure display-color checks; unchanged source ACES and minus-one-EV calibration, brighter daytime hemisphere fill with factory Sea Blue hue, strict radiance bounds, HDR hue and CPU ps_3_0 compilation. Shadow display luminance %.6f -> %.6f (%.3fx). No GPU/game used.\n",checks,oldLuma,dayLuma,dayLuma/oldLuma);
        return 0;
    }catch(const std::exception& error){std::fprintf(stderr,"Display-color test failed: %s\n",error.what());return 1;}
}

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

// Load the exact x86 plugin without a game process. DllMain must keep its game
// startup dormant when the executable is not bio4.exe.
int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: native_harness.exe plugin.asi [--transport PORT]\n");
        return 2;
    }
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    const HMODULE plugin = LoadLibraryA(argv[1]);
    if (!plugin) { std::fprintf(stderr, "LoadLibrary failed: %lu\n", GetLastError()); return 3; }
    using SelfTest = int (__cdecl*)();
    using TransportTest = int (__cdecl*)(std::uint16_t, std::uint32_t);
    const auto selftest = reinterpret_cast<SelfTest>(GetProcAddress(plugin, "RE4CraftProtocolSelfTest"));
    if (!selftest) { std::fprintf(stderr, "Missing RE4CraftProtocolSelfTest export\n"); FreeLibrary(plugin); return 4; }
    int status = selftest();
    std::printf("protocol selftest: %d\n", status);
    if (!status && argc == 4 && std::string(argv[2]) == "--transport") {
        const auto transport = reinterpret_cast<TransportTest>(GetProcAddress(plugin, "RE4CraftTransportSelfTest"));
        if (!transport) { std::fprintf(stderr, "Missing RE4CraftTransportSelfTest export\n"); status = 5; }
        else { status = transport(static_cast<std::uint16_t>(std::strtoul(argv[3], nullptr, 10)), 10000); std::printf("transport selftest: %d\n", status); }
    }
    FreeLibrary(plugin);
    return status;
}

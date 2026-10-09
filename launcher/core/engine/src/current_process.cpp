// No includes: core code includes no OS header, and these declarations must not meet the system's.
#ifdef _WIN32
extern "C" __declspec(dllimport) unsigned long __stdcall GetCurrentProcessId();
#else
extern "C" int getpid();
#endif

namespace rb::engine {

// Matches current_process.hpp: u32 is unsigned int on every supported target.
unsigned int current_process_id() noexcept {
#ifdef _WIN32
    return static_cast<unsigned int>(GetCurrentProcessId());
#else
    return static_cast<unsigned int>(getpid());
#endif
}

}  // namespace rb::engine

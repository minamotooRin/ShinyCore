#include "net_token.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#elif defined(__APPLE__)
#include <Security/SecRandom.h>
#elif defined(__linux__)
#include <sys/random.h>
#include <cerrno>
#endif

ScResult<ScNetToken> sc_platform_net_token() {
    std::array<unsigned char,16> bytes{};
#ifdef _WIN32
    auto status=BCryptGenRandom(nullptr,bytes.data(),static_cast<ULONG>(bytes.size()),BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    if(status<0) return std::unexpected("OS token entropy failed: "+std::to_string(status));
#elif defined(__APPLE__)
    auto status=SecRandomCopyBytes(kSecRandomDefault,bytes.size(),bytes.data());
    if(status!=0) return std::unexpected("OS token entropy failed: "+std::to_string(status));
#elif defined(__linux__)
    std::size_t done=0;
    while(done<bytes.size()) {
        auto count=getrandom(bytes.data()+done,bytes.size()-done,GRND_NONBLOCK);
        if(count<0 && errno==EINTR) continue;
        if(count<=0) return std::unexpected("OS token entropy failed: "+std::to_string(count<0?errno:0));
        done+=static_cast<std::size_t>(count);
    }
#else
    return std::unexpected("OS token entropy unavailable on this platform");
#endif
    constexpr char hex[]="0123456789abcdef";
    ScNetToken result{};
    for(std::size_t i=0;i<bytes.size();++i) {
        result[2*i]=hex[bytes[i]>>4];
        result[2*i+1]=hex[bytes[i]&15];
    }
    return result;
}

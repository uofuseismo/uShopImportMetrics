#include <string>
#include "uShopImportMetrics/version.hpp"
using namespace UShopImportMetrics;
    
int Version::getMajor() noexcept
{
    return uShopImportMetrics_MAJOR;
}
    
int Version::getMinor() noexcept
{
    return uShopImportMetrics_MINOR;
}

int Version::getPatch() noexcept
{
    return uShopImportMetrics_PATCH;
}

std::string Version::getVersion() noexcept
{
    return std::string {uShopImportMetrics_VERSION};
}

bool Version::isAtLeast(int major, int minor, int patch) noexcept
{
    if (uShopImportMetrics_MAJOR < major){return false;}
    if (uShopImportMetrics_MAJOR > major){return true;}
    if (uShopImportMetrics_MINOR < minor){return false;}
    if (uShopImportMetrics_MINOR > minor){return true;}
    if (uShopImportMetrics_PATCH < patch){return false;}
    return true;
}

#include <string>
#include <algorithm>
#include "uShopImportMetrics/streamIdentifier.hpp"

using namespace UShopImportMetrics;

namespace
{
/// @result True indicates that the string is empty or full of blanks.
[[maybe_unused]] [[nodiscard]]
bool isEmpty(const std::string &s) 
{
    if (s.empty()){return true;}
    return std::all_of(s.begin(), s.end(), [](const char c)
                       {
                           return std::isspace(c);
                       });
}

[[nodiscard]] std::string convertString(const std::string &s) 
{
    std::string temp{s};
    temp.erase(std::remove(temp.begin(), temp.end(), ' '), temp.end());
    std::transform(temp.begin(), temp.end(), temp.begin(), ::toupper);
    return temp;
}
}

class StreamIdentifier::StreamIdentifierImpl
{
public:
    void setString()
    {
        mString.clear();
        if (!mNetwork.empty() &&
            !mStation.empty() &&
            !mChannel.empty() &&
            !mLocationCode.empty())
        {
            mString = mNetwork + "."
                    + mStation + "."
                    + mChannel + "."
                    + mLocationCode;
        }
    }
    std::string mNetwork;
    std::string mStation;
    std::string mChannel;
    std::string mLocationCode;   
    std::string mString;
};

/// Constructor
StreamIdentifier::StreamIdentifier() :
    pImpl(std::make_unique<StreamIdentifierImpl> ())
{
}

/// Constructor
StreamIdentifier::StreamIdentifier(
    const std::string &network,
    const std::string &station,
    const std::string &channel,
    const std::string &locationCode)
{
    StreamIdentifier temp;
    temp.setNetwork(network);
    temp.setStation(station);
    temp.setChannel(channel);
    temp.setLocationCode(locationCode);
    *this = std::move(temp);
}

/// Copy constructor
StreamIdentifier::StreamIdentifier(const StreamIdentifier &identifier)
{
    *this = identifier;
}

/// Move constructor
StreamIdentifier::StreamIdentifier(StreamIdentifier &&identifier) noexcept
{
    *this = std::move(identifier);
}

/// Copy assignment
StreamIdentifier& 
StreamIdentifier::operator=(const StreamIdentifier &identifier)
{
    if (&identifier == this){return *this;}
    pImpl = std::make_unique<StreamIdentifierImpl> (*identifier.pImpl);
    return *this;
}

/// Move assignment
StreamIdentifier& 
StreamIdentifier::operator=(StreamIdentifier &&identifier) noexcept
{
    if (&identifier == this){return *this;}
    pImpl = std::move(identifier.pImpl);
    return *this;
}

/// Reset class
void StreamIdentifier::clear() noexcept
{
    pImpl->mNetwork.clear();
    pImpl->mStation.clear();
    pImpl->mChannel.clear();
    pImpl->mLocationCode.clear();
    pImpl->setString();
}

/// Destructor
StreamIdentifier::~StreamIdentifier() = default;

/// Network
void StreamIdentifier::setNetwork(const std::string &network)
{
    auto s = ::convertString(network);
    if (::isEmpty(s)){throw std::invalid_argument("Network is empty");}
    pImpl->mNetwork = std::move(s);
    pImpl->setString();
}

std::string StreamIdentifier::getNetwork() const
{
    if (!hasNetwork()){throw std::runtime_error("Network not set yet");}
    return pImpl->mNetwork;
}

bool StreamIdentifier::hasNetwork() const noexcept
{
    return !pImpl->mNetwork.empty();
}

/// Station
void StreamIdentifier::setStation(const std::string &station)
{
    auto s = ::convertString(station);
    if (::isEmpty(s)){throw std::invalid_argument("Station is empty");}
    pImpl->mStation = std::move(s);
    pImpl->setString();
}

std::string StreamIdentifier::getStation() const
{
    if (!hasStation()){throw std::runtime_error("Station not set yet");}
    return pImpl->mStation;
}

bool StreamIdentifier::hasStation() const noexcept
{
    return !pImpl->mStation.empty();
}

/// Channel
void StreamIdentifier::setChannel(const std::string &channel)
{
    auto s = ::convertString(channel);
    if (::isEmpty(s)){throw std::invalid_argument("Channel is empty");}
    pImpl->mChannel = std::move(s);
    pImpl->setString();
}

std::string StreamIdentifier::getChannel() const
{
    if (!hasChannel()){throw std::runtime_error("Channel not set yet");}
    return pImpl->mChannel;
}

bool StreamIdentifier::hasChannel() const noexcept
{
    return !pImpl->mChannel.empty();
}

/// Location code
void StreamIdentifier::setLocationCode(const std::string &locationCode)
{
    auto s = ::convertString(locationCode);
    if (::isEmpty(locationCode))
    {
        pImpl->mLocationCode = "--";
    }
    else
    {
        pImpl->mLocationCode = std::move(s);
    }
    pImpl->setString();
}

std::string StreamIdentifier::getLocationCode() const
{
    if (!hasLocationCode())
    {   
        throw std::runtime_error("Location code not set yet");
    }   
    return pImpl->mLocationCode;
}

bool StreamIdentifier::hasLocationCode() const noexcept
{
    return !pImpl->mLocationCode.empty();
}

const std::string &StreamIdentifier::getStringReference() const
{
    if (pImpl->mString.empty())
    {   
        if (!hasNetwork()){throw std::runtime_error("Network not set");}
        if (!hasStation()){throw std::runtime_error("Station not set");}
        if (!hasChannel()){throw std::runtime_error("Channel not set");}
        if (!hasLocationCode())
        {   
            throw std::runtime_error("Location code not set");
        }   
    }   
    return *&pImpl->mString;
}

std::string UShopImportMetrics::toMetricsName(
    const StreamIdentifier &identifier)
{
    auto network = identifier.getNetwork();
    auto station = identifier.getStation();
    auto channel = identifier.getChannel();
    auto result = network + "_" + station + "_" + channel;
    if (identifier.hasLocationCode())
    {
        result = result + "_" + identifier.getLocationCode();
    }
    std::transform(result.begin(), result.end(), result.begin(), ::tolower);
    return result;
}

bool UShopImportMetrics::operator<(const StreamIdentifier &lhs,
                                   const StreamIdentifier &rhs)
{
    return lhs.getStringReference() < rhs.getStringReference();
}

bool UShopImportMetrics::operator==(const StreamIdentifier &lhs,
                                    const StreamIdentifier &rhs)
{
    return lhs.getStringReference() == rhs.getStringReference();
}


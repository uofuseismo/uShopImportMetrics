#include <vector>
#include <set>
#include <map>
#include <cmath>
#include <string>
#include <chrono>
#include <limits>
#include "uShopImportMetrics/packet.hpp"
#include "uShopImportMetrics/streamIdentifier.hpp"
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

TEST_CASE("UShopImportMetrics::StreamIdentifier", "[streamIdentifier]")
{
    using namespace UShopImportMetrics;
    const std::string network{"UU"};
    const std::string station{"FTU"};
    const std::string channel{"HHN"};
    const std::string locationCode{"01"};
    StreamIdentifier identifier;

    REQUIRE_NOTHROW(identifier.setNetwork(network));
    REQUIRE_NOTHROW(identifier.setStation(station));
    REQUIRE_NOTHROW(identifier.setChannel(channel));
    REQUIRE_NOTHROW(identifier.setLocationCode(locationCode));

    REQUIRE(identifier.getNetwork() == network);
    REQUIRE(identifier.getStation() == station);
    REQUIRE(identifier.getChannel() == channel);
    REQUIRE(identifier.getLocationCode() == locationCode);
    REQUIRE(toMetricsName(identifier) == "uu_ftu_hhn_01");

    SECTION("Set")
    {
        StreamIdentifier id1;
        id1.setNetwork("UU"); id1.setStation("CWU");
        id1.setChannel("HHZ"); id1.setLocationCode("01");

        StreamIdentifier id2;
        id2.setNetwork("UU"); id2.setStation("CWU"); 
        id2.setChannel("HHN"); id2.setLocationCode("01");

        StreamIdentifier id3;
        id3.setNetwork("UU"); id3.setStation("CWU"); 
        id3.setChannel("HHE"); id3.setLocationCode("01");

        StreamIdentifier id4;
        id4.setNetwork("UU"); id4.setStation("TMU"); 
        id4.setChannel("HHZ"); id4.setLocationCode("01");

        std::set<StreamIdentifier> ids;
        ids.insert(id1);
        ids.insert(id2);
        ids.insert(id3);
        REQUIRE(ids.size() == 3);
        REQUIRE(ids.contains(id1));
        REQUIRE(ids.contains(id2));
        REQUIRE(ids.contains(id3));
        REQUIRE(!ids.contains(id4)); 
    }
      
}

TEST_CASE("UShopImportMetrics::Packet", "[packet]")
{
    using namespace UShopImportMetrics;
    const std::string network{"UU"};
    const std::string station{"FTU"};
    const std::string channel{"HHN"};
    const std::string locationCode{"01"};
    const double samplingRate{100};
    const std::chrono::microseconds startTime{1759952887000000};
    StreamIdentifier identifier;

    REQUIRE_NOTHROW(identifier.setNetwork(network));
    REQUIRE_NOTHROW(identifier.setStation(station));
    REQUIRE_NOTHROW(identifier.setChannel(channel));
    REQUIRE_NOTHROW(identifier.setLocationCode(locationCode));
 
    Packet packet;
    REQUIRE_NOTHROW(packet.setStreamIdentifier(identifier));
    REQUIRE_NOTHROW(packet.setSamplingRate(samplingRate));
    REQUIRE_NOTHROW(packet.setStartTime(startTime));
     
    REQUIRE(packet.getStreamIdentifierReference().getNetwork() == network); 
    REQUIRE(packet.getStreamIdentifier().getNetwork() == network); 
    REQUIRE(std::abs(packet.getSamplingRate() - samplingRate) < 1.e-14);
    REQUIRE(packet.getStartTime() == startTime);

    SECTION("double start time")
    {
        packet.setStartTime(startTime.count()*1.e-6);
        REQUIRE(packet.getStartTime() == startTime);
    }

    SECTION("integer32")
    {
        std::vector<int> data{1, 2, 3, 4};
        auto dataTemp = data;
        REQUIRE_NOTHROW(packet.setData(std::move(dataTemp)));
        REQUIRE(packet.getEndTime().count() == startTime.count() + 30000); 
        REQUIRE(packet.getDataType() == Packet::DataType::Integer32);
        auto dataBack = packet.getData<int> ();
        REQUIRE(dataBack.size() == data.size());
        REQUIRE(packet.getNumberOfSamples() == static_cast<int> (data.size()));
        for (int i = 0; i < static_cast<int> (data.size()); ++i)
        {
            REQUIRE(dataBack.at(i) == data.at(i));
        }
        REQUIRE(packet.getNumberOfSamples() == static_cast<int> (data.size()));
        auto [sum, sum2] = computeSumAndSumSquared(packet);
        REQUIRE(std::abs(sum - 10) < 1.e-14);
        REQUIRE(std::abs(sum2 - 30) < 1.e-14); 
    }

    SECTION("integer64")
    {   
        std::vector<int64_t> data{4, 3, 2, -1}; 
        REQUIRE_NOTHROW(packet.setData(data));
        REQUIRE(packet.getEndTime().count() == startTime.count() + 30000); 
        REQUIRE(packet.getDataType() == Packet::DataType::Integer64);
        auto dataBack = packet.getData<int64_t> (); 
        REQUIRE(dataBack.size() == data.size());
        REQUIRE(packet.getNumberOfSamples() == static_cast<int> (data.size()));
        for (int i = 0; i < static_cast<int> (data.size()); ++i)
        {
            REQUIRE(dataBack.at(i) == data.at(i));
        }
        auto [sum, sum2] = computeSumAndSumSquared(packet);
        REQUIRE(std::abs(sum - 8) < 1.e-14);
        REQUIRE(std::abs(sum2 - 30) < 1.e-14); 
    }

    SECTION("double")
    {   
        std::vector<double> data{4, 2, 3, 1}; 
        REQUIRE_NOTHROW(packet.setData(data));
        REQUIRE(packet.getEndTime().count() == startTime.count() + 30000); 
        REQUIRE(packet.getDataType() == Packet::DataType::Double); 
        auto dataBack = packet.getData<double> (); 
        REQUIRE(dataBack.size() == data.size());
        REQUIRE(packet.getNumberOfSamples() == static_cast<int> (data.size()));
        for (int i = 0; i < static_cast<int> (data.size()); ++i)
        {
            REQUIRE(std::abs(dataBack.at(i) - data.at(i)) < 1.e-14);
        }
        auto [sum, sum2] = computeSumAndSumSquared(packet);
        REQUIRE(std::abs(sum - 10) < 1.e-14);
        REQUIRE(std::abs(sum2 - 30) < 1.e-14); 
    }

    SECTION("float")
    {
        std::vector<float> data{5, -1, 2, 3};  
        REQUIRE_NOTHROW(packet.setData(data));
        REQUIRE(packet.getEndTime().count() == startTime.count() + 30000);
        REQUIRE(packet.getDataType() == Packet::DataType::Float);
        auto dataBack = packet.getData<float> (); 
        REQUIRE(dataBack.size() == data.size());
        REQUIRE(packet.getNumberOfSamples() == static_cast<int> (data.size()));
        for (int i = 0; i < static_cast<int> (data.size()); ++i)
        {
            REQUIRE(std::abs(dataBack.at(i) - data.at(i)) < 1.e-7);
        }
        auto [sum, sum2] = computeSumAndSumSquared(packet);
        REQUIRE(std::abs(sum - 9) < 1.e-14);
        REQUIRE(std::abs(sum2 - 39) < 1.e-14); 
    }
}


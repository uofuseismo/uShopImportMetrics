#ifndef USHOP_IMPORT_METRICS_WAVE_RING_HPP
#define USHOP_IMPORT_METRICS_WAVE_RING_HPP
#include <memory>
#include <string>
#include <vector>
#include <future>
#include <functional>
#include <exception>
namespace UShopImportMetrics
{
 class Packet;
}
namespace UShopImportMetrics
{
class TerminateException : public std::exception
{ 
private: 
    std::string message; 
public: 
    TerminateException(const std::string &msg) :
        message(msg)
    {
    }
    // Constructor accepts a const char* that is used to set 
    // the exception message 
    TerminateException(const char* msg) 
        : message(msg) 
    { 
    } 
    // Override the what() method to return our message 
    const char* what() const throw() 
    { 
        return message.c_str(); 
    } 
}; 

class WaveRingOptions
{
public:
    std::string ringName{"IMPORT_RING"};
    std::string moduleName{"uShopMetrics"};
    bool flushOnStart{true};
};
  
/// @class WaveRing "waveRing.hpp"
/// @brief A utility for reading and writing traceBuf2 messages from an
///        Earthworm wave ring as well as status messages.
/// @copyright Ben Baker (University of Utah) distributed under the MIT license.
class WaveRing
{
public:
    /// @name Constructors
    /// @{

    /// @brief Default constructor.
    WaveRing(const WaveRingOptions &options,
             const std::function<void (Packet &&)> &callback,
             std::shared_ptr<spdlog::logger> logger);
    /// @brief Move constructor.
    /// @param[in,out] waveRing  Initializes the ring reader from this class.
    ///                          On exit, waveRing's behavior is undefined.
    WaveRing(WaveRing &&waveRing) noexcept;
    /// @}

    /// @name Operators
    /// @{

    /// @brief Move assignment operator.
    /// @param[in,out] waveRing  The waveRing class whose memory will be moved
    ///                          to this.  On exit, waveRing's behavior is
    ///                          undefined.
    /// @result The memory from waveRing moved to this.
    WaveRing& operator=(WaveRing &&waveRing) noexcept;
    /// @} 

    /// @name Connection
    /// @{

    /// @brief Initializes the ring parameters.
    /// @param[in] ringName          The name of the earthworm ring - e.g.,
    ///                              "WAVE_RING".
    /// @param[in] milliSecondsWait  The number of milliseconds to wait after
    ///                              reading from the earthworm ring.
    /// @throws std::
    //void connect(); //const std::string &ringName, const std::string &moduleName = "");
    /// @result True indicates that this class is connected to an
    ///         earthworm ring.
    [[nodiscard]] bool isConnected() const noexcept;
    /// @result The name of the ring to which this class is attached.
    /// @throws std::runtime_error if \c isConnected() is false.
    [[nodiscard]] std::string getRingName() const;
    /// @}

    /// @name Reading
    /// @{

    /// @brief Starts the wave ring acquisition.
    std::future<void> start();
    /// @brief Stops the wave ring acquisiton.
    void stop();
    //void write(const TraceBuf2 &message);
    //void writeHeartbeat(bool terminate = false);
    

    /// @result The traceBuf2 messages read from the ring.
    //[[nodiscard]] std::vector<TraceBuf2> getTraceBuf2Messages() const noexcept;
    /// @result The number of traceBuf2 messages.
    //[[nodiscard]] int getNumberOfTraceBuf2Messages() const noexcept;
    /// @result A pointer to the array of traceBuf2 messages read from the
    ///         ring.  This has dimension [\c getNumberOfTraceBuf2Messages()].
    /// @note This is not recommended for general use.
    //[[nodiscard]] const TraceBuf2 *getTraceBuf2MessagesPointer() const noexcept;
    /// @result A reference to the array of traceBuf2 messages read from the
    ///         ring.
    /// @note This is not recommended for general use. 
    //[[nodiscard]] const std::vector<TraceBuf2> &getTraceBuf2MessagesReference() const noexcept;
    /// @result The traceBuf2 messages read from the ring moved to this.
    /// @note On exit, all read messages will have been moved and 
    ///       \c getNumberOfTraceBuf2Messages() will be 0.
    //[[nodiscard]] std::vector<TraceBuf2> moveTraceBuf2Messages() noexcept;
    /// @}

    /// @name Destructors
    /// @{

    /// @brief Disconnects from the ring.  Additionally, all memory is released.
    void disconnect();
    /// @brief Destructor.
    ~WaveRing(); 
    /// @}
 
    WaveRing() = delete;
    WaveRing& operator=(const WaveRing &waveRing) = delete;
    WaveRing(const WaveRing &waveRing) = delete;
private:
    class WaveRingImpl;
    std::unique_ptr<WaveRingImpl> pImpl;
};
}
#endif

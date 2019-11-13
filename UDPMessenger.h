#ifndef __UDP_MESSENGER_H
#define __UDP_MESSENGER_H

#include <string>
#include <utility>
#include <vector>
#include <map>
#include <deque>
#include "avmu_enums.h"
#ifdef __arm__
#include "xtime_l.h"
#include "lwip/tcp.h" // really just to capture the u64_t data type
#endif

#define ONE_MEGABYTE 1048576
#define EIGHT_MEGABYTES (ONE_MEGABYTE * 8)

namespace Akela
{
	class UDPData;
	class UDPMessenger
	{
			UDPData* d;


			// Only used internally, at the moment.
			//Akela::Err_Code receive(char* buf, unsigned int bufSize, unsigned int& bytesReceived, std::string& sourceIP, unsigned int timeout, unsigned int pollInterval) const;
			Akela::Err_Code receive(std::vector<char>& message, std::string& sourceIP, unsigned int timeout, unsigned int pollInterval = 50) const;

		public:
			UDPMessenger();
			~UDPMessenger();

			const long available_bytes(void) const;

			unsigned short getPort() const;
			unsigned long getBufferSize() const;

			std::string getBoundAddress();

			void releaseSocket();
			Akela::Err_Code bind_socket(const unsigned short Port, const unsigned long bufferSize = EIGHT_MEGABYTES);
			Akela::Err_Code send_to(const std::string& ip, const std::string message) const;
			Akela::Err_Code send_to(const std::string& ip, const std::vector<char>& message) const; // messages longer than 1400 bytes are split into multiple outgoing packets
			Akela::Err_Code receive_into(std::map<std::string, std::deque<std::vector<char> > >& packet_map, unsigned int timeout, unsigned int pollInterval = 50) const;
			Akela::Err_Code receive_waiting(std::map<std::string, std::deque<std::vector<char> > >& packet_map) const;
			//Akela::Err_Code receive_single(std::string& src_ip, std::string& content, unsigned int timeout, unsigned int pollInterval = 50) const;

			void interrupt(); //kill receive() before timeout has elapsed

			static void debugSocketError(void); // Dump socket error crap to console.
#ifdef __arm__
			static void zynqSetupLWIP();
			static void zynqProcessPackets(); // On Zynq, must be called during the idle loop!
			static u64_t zynqGetTickCount(); // one half of CPU frequency. "xtime_l.h" defines COUNTS_PER_SECOND for conversions to time
#endif
	};
}

#endif

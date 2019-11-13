#include <vector>
#include <deque>
#include <string>
#include <cstring>
#include <iostream>
#include <errno.h>
#include "UDPMessenger.h"
#include "avmu_enums.h"

#include "xparameters.h"
#include "platform.h"
#include "platform_config.h"

#include "netif/xadapter.h"
#include "lwip/init.h" // for lwip_init
#include "lwip/err.h"
#include "lwip/tcp.h"
#include "lwip/udp.h"
#include "lwip/inet.h"
#include "lwip/priv/tcp_priv.h" // for tcp_fasttmr and tcp_slowtmr
#include "xtime_l.h"
#include "xil_printf.h"
#include <sleep.h>

#define SOCKET struct udp_pcb*
#define BOOL bool
#define SOCKET_ERROR   -1
#define INVALID_SOCKET 0

extern volatile int TcpFastTmrFlag;
extern volatile int TcpSlowTmrFlag;
static struct netif server_netif;
struct netif *echo_netif;


namespace Akela
{
	class Packet
	{
	public:
		std::vector<char> bytes;
		std::string ip;
	};
	class UDPData
	{
		public:
			SOCKET s;
			unsigned short port;
			unsigned long bufferSize;
			bool isOpen;
			volatile bool go;
			std::deque< Packet > unhandledPackets;

			UDPData() : port(0), bufferSize(EIGHT_MEGABYTES), isOpen(false), go(false) {}
	};






	static void print_ip(const char *msg, const ip_addr_t *ip, const u16_t port)
	{
		print(msg);
		xil_printf("%d.%d.%d.%d:%d\n\r", ip4_addr1(ip), ip4_addr2(ip),
				ip4_addr3(ip), ip4_addr4(ip), port);
	}

	static void udp_packet_receive(void *arg, struct udp_pcb *tpcb,
			struct pbuf *p, const ip_addr_t *addr, u16_t port)
	{
		//print_ip("Received packet from ", addr, port);
		//xil_printf("len: %d\r\n", p->len);

		Packet item;
		item.bytes.resize(p->len);
		memcpy(item.bytes.data(), p->payload, p->len); // NOTE! Not null-terminated!
		//item.bytes.push_back('\0');
		item.ip = std::string(ip4addr_ntoa(addr));

		UDPData *d = (UDPData*)arg;
		if(d)
			d->unhandledPackets.push_back(item);

		//xil_printf("packet as ascii: %s\r\n",item.bytes.data());

		pbuf_free(p);
	}

	void UDPMessenger::zynqSetupLWIP()
	{
		unsigned char mac_ethernet_address[] =
		{ 0x00, 0x0a, 0x35, 0x00, 0x01, 0x02 };

		echo_netif = &server_netif;

		init_platform();

		ip_addr_t ipaddr, netmask, gw;
		IP4_ADDR(&ipaddr,  192, 168,   1, 175);
		IP4_ADDR(&netmask, 255, 255, 255,  0);
		IP4_ADDR(&gw,      192, 168,   1,  1);

		lwip_init();

		if (!xemac_add(echo_netif, &ipaddr, &netmask,
							&gw, mac_ethernet_address,
							PLATFORM_EMAC_BASEADDR)) {
			xil_printf("Error adding N/W interface\n\r");
			return;
		}

		netif_set_default(echo_netif);

		platform_enable_interrupts();

		netif_set_up(echo_netif);
	}

	void UDPMessenger::zynqProcessPackets()
	{
		if (TcpFastTmrFlag)
		{
			tcp_fasttmr();
			TcpFastTmrFlag = 0;
		}
		if (TcpSlowTmrFlag)
		{
			tcp_slowtmr();
			TcpSlowTmrFlag = 0;
		}
		xemacif_input(echo_netif);
	}



	UDPMessenger::UDPMessenger()
	{
		d = new UDPData();
	}

	UDPMessenger::~UDPMessenger()
	{
		releaseSocket();
		delete d;
	}

	unsigned short UDPMessenger::getPort() const
	{
		return d->port;
	}
	unsigned long UDPMessenger::getBufferSize() const
	{
		return 0;
	}

	void UDPMessenger::releaseSocket()
	{
		if(d->s)
		{
			udp_remove(d->s);
			d->s = NULL;
		}
		d->isOpen = false;
	}



	Akela::Err_Code UDPMessenger::bind_socket(const unsigned short Port, const unsigned long bufferSize)
	{
		err_t err;

		if(d->isOpen)
		{
			if(Port == d->port)
				return Akela::Err_Code::ERR_OK;
			else
				releaseSocket();
		}

		d->s = udp_new();
		if (!d->s)
		{
			xil_printf("Error creating PCB. Out of Memory\n\r");
			return Akela::Err_Code::ERR_SOCKET;
		}

		err = udp_bind(d->s, IP_ADDR_ANY, Port);

		if (err != ERR_OK)
		{
			xil_printf("Unable to connect to port: err = %d\n\r",err);
			return Akela::Err_Code::ERR_SOCKET;
		}

		udp_recv(d->s, udp_packet_receive, (void*)(this->d));


		d->isOpen = true;
		d->port = Port;
		return Akela::Err_Code::ERR_OK;
	}

	std::string UDPMessenger::getBoundAddress()
	{
		if (d->isOpen != true)
			return "Socket not open!";

		return "any";
	}

	static void platform_agnostic_sleep(unsigned int ms)
	{
		if (ms == 0)
			return;

		UDPMessenger::zynqProcessPackets();
		usleep(ms * 1000);
		UDPMessenger::zynqProcessPackets();
	}
	static Akela::Err_Code sendPacket(SOCKET s, const std::string& ip, u16_t port, const std::vector<char>& message)
	{
		u8_t retries = 5;
		struct pbuf *packet;
		ip_addr_t outip;
		err_t err;

		unsigned int N = message.size();
		/*
		xil_printf("Outgoing packet:\r\n",N);
		for(unsigned int i=0;i<N;++i)
		{
			xil_printf("%02x ", message[i]);
			if((i+1)%16 == 0 || (i==N-1))
				xil_printf("\r\n");
		}
		*/
		packet = pbuf_alloc(PBUF_TRANSPORT, N, PBUF_POOL);
		if (!packet) {
			xil_printf("error allocating pbuf to send\r\n");
			return Akela::Err_Code::ERR_SOCKET;
		} else {
			memcpy(packet->payload, message.data(), N);
		}

		outip.addr = inet_addr(ip.c_str());

		while (retries) {
			err = udp_sendto(s, packet, &outip, port);
			if (err != ERR_OK) {
				xil_printf("Error on udp_send: %d\r\n", err);
				retries--;
				usleep(100);
			} else {
				break;
			}
		}
		if (!retries) {
			xil_printf("Too many udp_send() retries\r\n");
			return Akela::Err_Code::ERR_SOCKET;
		}

		pbuf_free(packet);

		UDPMessenger::zynqProcessPackets();
		return Akela::Err_Code::ERR_OK;
	}

	static std::vector<char> range(std::vector<char> array, unsigned int start, unsigned int length)
	{
		std::vector<char> out;

		if(start >= array.size())
			return out;

		unsigned int N = length;
		if(start + length > array.size())
			N = array.size() - start;
		out.resize(N);
		memcpy(out.data(), array.data()+start, N);

		return out;
	}
	Akela::Err_Code UDPMessenger::send_to(const std::string& ip, const std::string message) const
	{
		std::vector<char> outgoing;
		outgoing.resize(message.length()); // length does not include the null terminator
		memcpy(outgoing.data(), message.c_str(), message.length());

		return send_to(ip, outgoing);
	}
	Akela::Err_Code UDPMessenger::send_to(const std::string& ip, const std::vector<char>& message) const
	{
		if (d->isOpen)
		{
			const int maxUserData = 1400;
			int message_size = message.size();
			if (message_size <= maxUserData)
			{
				return sendPacket(d->s, ip, d->port, message);
			}
			else
			{
				//split message up into chunks
				unsigned int offset = 0;
				while(1)
				{
					std::vector<char> part = range(message, offset, maxUserData);
					Akela::Err_Code code = sendPacket(d->s, ip, d->port, part);
					if (code != Akela::Err_Code::ERR_OK)
						return code;

					//sockets aren't happy if you don't wait a bit before sending another message
					platform_agnostic_sleep(1);

					offset += maxUserData;
					if(part.size() < maxUserData)
						break;
				}
			}
			return Akela::Err_Code::ERR_OK;
		}
		else
		{
			UDPMessenger::debugSocketError();
			return Akela::Err_Code::ERR_SOCKET;
		}
	}


	void UDPMessenger::debugSocketError(void)
	{
	}

	void UDPMessenger::interrupt()
	{
		d->go = false;
	}


	const long UDPMessenger::available_bytes(void) const
	{
		UDPMessenger::zynqProcessPackets();
		long b = 0;
		for(unsigned int i=0;i<d->unhandledPackets.size();++i)
			b += d->unhandledPackets[i].bytes.size();

		return b;
	}


	Akela::Err_Code UDPMessenger::receive_into(std::map<std::string, std::deque<std::vector<char> > >& packet_map, unsigned int timeout, unsigned int pollInterval) const
	{
		Akela::Err_Code code;
		std::vector<char> message;
		std::string sourceip;
		code = this->receive(message, sourceip, timeout, pollInterval);
		if (code != Akela::Err_Code::ERR_OK)
			return code;

		packet_map[sourceip].push_back(message);
		return code;

	}
/*
	Akela::Err_Code UDPMessenger::receive_single(std::string& src_ip, std::string& content, unsigned int timeout, unsigned int pollInterval) const
	{
		char buf[9001];
		unsigned int bytes;
		Akela::Err_Code code;
		std::string sourceip;
		code = this->receive(buf, sizeof(buf), bytes, src_ip, timeout, pollInterval);
		if (code != Akela::Err_Code::ERR_OK)
			return code;

		content = std::string(buf);

		return code;

	}
*/
	Akela::Err_Code UDPMessenger::receive_waiting(std::map<std::string, std::deque<std::vector<char> > >& packet_map) const
	{
		UDPMessenger::zynqProcessPackets();
		while(!d->unhandledPackets.empty())
		{
			Packet p = d->unhandledPackets.front();
			d->unhandledPackets.pop_front();
			packet_map[p.ip].push_back(p.bytes);
		}
		return Akela::Err_Code::ERR_OK;
	}


	Akela::Err_Code UDPMessenger::receive(std::vector<char>& message, std::string& sourceIP, unsigned int timeout, unsigned int pollInterval) const
	{
		if (!d->isOpen)
			return Akela::Err_Code::ERR_SOCKET;

		bool haveData = false;
		d->go = true;
		int ctr = 0;
		const u64_t timeout_ticks = (timeout/1000.0f)*COUNTS_PER_SECOND;
		u64_t start = UDPMessenger::zynqGetTickCount();

		while (((UDPMessenger::zynqGetTickCount()-start) <= timeout_ticks) && d->go) //go is volatile and can be set false by interrupt()
		{
			const long available_bytes = this->available_bytes(); // this calls xemacif_input() which leads to udp_packet_receive() calls

			if (available_bytes  > 0)
			{
				haveData = true;
				//printf("ctr = %d\n", ctr);
				break;
			}

			//Try with no delay 700 times, and after that transition to polling
			if (ctr > 700)
				platform_agnostic_sleep(pollInterval);

			++ctr;
			if(ctr == INT_MAX)
				break;
		}

		if (!haveData)
			return Akela::Err_Code::ERR_NO_RESPONSE;

		Packet p = d->unhandledPackets.front();
		d->unhandledPackets.pop_front();
		message = p.bytes;
		sourceIP = p.ip;
		return Akela::Err_Code::ERR_OK;
	}

/*
	Akela::Err_Code UDPMessenger::receive(char* buf, unsigned int bufSize, unsigned int& bytesReceived, std::string& sourceIP, unsigned int timeout, unsigned int pollInterval) const
	{
		Akela::Err_Code code;
		std::string message;

		code = this->receive(message, sourceIP, timeout, pollInterval);
		if (code != Akela::Err_Code::ERR_OK)
			return code;

		if(message.size()+1 > bufSize)
			xil_printf("Warning: clipping packet, provided buffer not large enough\r\n");

		unsigned int a = message.size()+1;
		unsigned int b = bufSize;
		unsigned int N = (a<b)? a : b;
		memcpy(buf, message.c_str(), N);
		bytesReceived = N;

		return Akela::Err_Code::ERR_OK;
	}
*/

	u64_t UDPMessenger::zynqGetTickCount()
	{
		XTime tCur = 0;
		XTime_GetTime(&tCur);
		return tCur;
	}
}

#include "wivrn_sockets.h"
#include "wivrn_packets.h"

#include <iostream>
#include <sys/socket.h>
#include <netdb.h>


using namespace xrt::drivers::network;

sockaddr_in6 wait_announce()
{
	typed_socket<UDP, from_headset::client_announce_packet, void> receiver;

	receiver.subscribe_multicast(announce_address);
	receiver.bind(announce_port);

	while(true)
	{
		auto [packet, sender] = receiver.receive_from();

		if (packet.magic == packet.magic_value)
		{
			receiver.unsubscribe_multicast(announce_address);
			return sender;
		}
	}
}

int main()
{
	try
	{
		auto headset_address = wait_announce();

		char host[100];
		getnameinfo((sockaddr*)&headset_address, sizeof(headset_address), host, sizeof(host), nullptr, 0, 0);
		std::cout << "Sender: " << host << std::endl;

		typed_socket<TCP, from_headset::control_packets, to_headset::control_packets> control{headset_address.sin6_addr, control_port};
	}
	catch(std::exception& e)
	{
		std::cerr << "Caught exception: " << e.what() << std::endl;
	}
}

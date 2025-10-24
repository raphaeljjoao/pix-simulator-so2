#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <netdb.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <iomanip>
#include <chrono>
#include <unordered_map>
#include "network_structs.hpp"

const int PORT = 4000;
const int INITIAL_CLIENT_BALANCE = 100;

int main(int argc, char *argv[])
{
	int sockfd, n;
	socklen_t clilen;
	struct sockaddr_in serv_addr, cli_addr;
		
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) == -1) 
		std::cout << "ERROR opening socket";

	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons(PORT);
	serv_addr.sin_addr.s_addr = INADDR_ANY;
	bzero(&(serv_addr.sin_zero), 8);    
	 
	if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(struct sockaddr)) < 0) 
		std::cout << "ERROR on binding";
	
	clilen = sizeof(struct sockaddr_in);

	network_structs::Stats stats;
	std::unordered_map<uint32_t, network_structs::Client> clients;
	
	auto now = std::chrono::system_clock::now();
	auto time_t = std::chrono::system_clock::to_time_t(now);
	std::cout << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S")
			  << " num_transactions " << stats.num_transactions
			  << " total_transferred " << stats.total_transferred
			  << " total_balance " << stats.total_balance << std::endl;
	
	while (1) {
		network_structs::Packet received_packet;

		/* receive from socket */
		n = recvfrom(sockfd, &received_packet, sizeof(network_structs::Packet), 0, (struct sockaddr *) &cli_addr, &clilen);
		if (n < 0) 
			std::cout << "ERROR on recvfrom" << std::endl;
		
		std::cout << "Received a datagram: " << static_cast<int>(received_packet.type) << std::endl;
		
		if (received_packet.type == network_structs::PacketType::DISCOVERY) {
			uint32_t client_addr = cli_addr.sin_addr.s_addr;
			
			network_structs::Client new_client;
			new_client.address = client_addr;
			new_client.balance = INITIAL_CLIENT_BALANCE;
			new_client.last_req = 0;
			if (clients.find(client_addr) == clients.end()) {
				clients[client_addr] = new_client;
			}
			
			// Create and send DISCOVERY_ACK response
			network_structs::Packet response_packet;
			response_packet.type = network_structs::PacketType::DISCOVERY_ACK;
			response_packet.sequence_number = received_packet.sequence_number;
			
			/* send response to socket */
			n = sendto(sockfd, &response_packet, sizeof(network_structs::Packet), 0, (struct sockaddr *) &cli_addr, sizeof(struct sockaddr));
			if (n < 0) 
				std::cout << "ERROR on sendto" << std::endl;
			else
				std::cout << "Got your message" << std::endl;
		} else {
			std::cout << "Ignored packet" << std::endl;
		}
	}
	
	close(sockfd);
	return 0;
}
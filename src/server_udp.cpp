#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <iostream>
#include <iomanip>
#include <chrono>
#include <map>
#include "network_structs.hpp"

const int PORT = 4000;
const int INITIAL_CLIENT_BALANCE = 100;

struct ClientInfo {
    uint32_t last_req = 0;
    uint64_t balance = INITIAL_CLIENT_BALANCE;
};

std::map<uint32_t, ClientInfo> client_table;

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
			
			if (client_table.find(client_addr) == client_table.end()) {
				ClientInfo new_client;
				client_table[client_addr] = new_client;
				
				stats.total_balance += INITIAL_CLIENT_BALANCE;
				
				std::cout << "New client registered: " << client_addr << ". Total balance: " << stats.total_balance << std::endl;
			} else {
				std::cout << "Duplicate discovery from client: " << client_addr << std::endl;
			}
			
			// In both cases (new or duplicate), send DISCOVERY_ACK response
			network_structs::Packet response_packet;
			response_packet.type = network_structs::PacketType::DISCOVERY_ACK;
			response_packet.sequence_number = received_packet.sequence_number;
			
			/* send response to socket */
			n = sendto(sockfd, &response_packet, sizeof(network_structs::Packet), 0, (struct sockaddr *) &cli_addr, sizeof(struct sockaddr));
			if (n < 0) 
				std::cout << "ERROR on sendto" << std::endl;
		} else if (received_packet.type == network_structs::PacketType::REQUEST) {
			uint32_t src_addr = cli_addr.sin_addr.s_addr;
			uint32_t dest_addr = received_packet.data.req.dest_addr;
			uint32_t value = received_packet.data.req.value;
			uint32_t req_id = received_packet.sequence_number;

			// Ensure both clients exist
			if (client_table.find(src_addr) == client_table.end()) {
				client_table[src_addr] = ClientInfo();
				stats.total_balance += INITIAL_CLIENT_BALANCE;
			}
			if (client_table.find(dest_addr) == client_table.end()) {
				client_table[dest_addr] = ClientInfo();
				stats.total_balance += INITIAL_CLIENT_BALANCE;
			}

			// Check if this is the expected next request ID
			if (req_id != client_table[src_addr].last_req + 1) {
				std::cout << "Unexpected request ID from client: " << inet_ntoa(*(in_addr*)&src_addr) 
						  << " req_id: " << req_id << " expected: " << (client_table[src_addr].last_req + 1) << std::endl;
				
				// Send REQUEST_ACK with last processed request ID
				network_structs::Packet response_packet;
				response_packet.type = network_structs::PacketType::REQUEST_ACK;
				response_packet.sequence_number = client_table[src_addr].last_req;
				response_packet.data.ack.new_balance = client_table[src_addr].balance;
				
				n = sendto(sockfd, &response_packet, sizeof(network_structs::Packet), 0, 
						  (struct sockaddr *) &cli_addr, sizeof(struct sockaddr));
				if (n < 0)
					std::cout << "ERROR on sendto" << std::endl;
				continue;
			}

			// Process transfer if sufficient balance
			if (client_table[src_addr].balance >= value) {
				client_table[src_addr].balance -= value;
				client_table[dest_addr].balance += value;
				stats.num_transactions++;
				stats.total_transferred += value;
				
				// Recalculate total_balance
				stats.total_balance = 0;
				for (const auto& entry : client_table) {
					stats.total_balance += entry.second.balance;
				}
			}
			
			// Update last_req only after processing the request (successful or not)
			client_table[src_addr].last_req = req_id;

			// Print required information for each request
			auto now = std::chrono::system_clock::now();
			auto time_t = std::chrono::system_clock::to_time_t(now);
			std::cout << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S")
					  << " client " << inet_ntoa(*(in_addr*)&src_addr)
					  << " id req " << req_id
					  << " dest " << inet_ntoa(*(in_addr*)&dest_addr)
					  << " value " << value << std::endl;
			std::cout << "num transactions " << stats.num_transactions << std::endl;
			std::cout << "total transferred " << stats.total_transferred
					  << " total balance " << stats.total_balance << std::endl;

			// Send REQUEST_ACK response
			network_structs::Packet response_packet;
			response_packet.type = network_structs::PacketType::REQUEST_ACK;
			response_packet.sequence_number = req_id;
			response_packet.data.ack.new_balance = client_table[src_addr].balance;
			
			n = sendto(sockfd, &response_packet, sizeof(network_structs::Packet), 0, 
					  (struct sockaddr *) &cli_addr, sizeof(struct sockaddr));
			if (n < 0)
				std::cout << "ERROR on sendto" << std::endl;
		} else {
			std::cout << "Ignored packet" << std::endl;
		}
	}
	
	close(sockfd);
	return 0;
}
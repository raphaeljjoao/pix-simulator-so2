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
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <atomic>
#include "network_structs.hpp"

const int PORT = 4000;
const int INITIAL_CLIENT_BALANCE = 100;

struct ClientInfo {
    uint32_t last_req = 0;
    uint64_t balance = INITIAL_CLIENT_BALANCE;
};

// Global variables for synchronization
std::map<uint32_t, ClientInfo> client_table;
network_structs::Stats stats;
std::mutex table_mutex;
std::condition_variable table_updated;
std::atomic<bool> has_update{false};
std::atomic<bool> server_running{true};

// Structure to pass request data to processing threads
struct RequestData {
    int sockfd;
    network_structs::Packet packet;
    struct sockaddr_in client_addr;
    socklen_t addr_len;
};

// Discovery service - handles client registration (first writer)
void handle_discovery(int sockfd, const network_structs::Packet& received_packet, const struct sockaddr_in& cli_addr) {
    uint32_t client_addr = cli_addr.sin_addr.s_addr;
    bool new_client = false;
    
    {
        std::lock_guard<std::mutex> lock(table_mutex);
        if (client_table.find(client_addr) == client_table.end()) {
            ClientInfo new_client_info;
            client_table[client_addr] = new_client_info;
            stats.total_balance += INITIAL_CLIENT_BALANCE;
            new_client = true;
            has_update = true;
        }
    }
    
    if (new_client) {
        table_updated.notify_all();
        std::cout << "New client registered: " << inet_ntoa(*(in_addr*)&client_addr) << std::endl;
    } else {
        std::cout << "Duplicate discovery from client: " << inet_ntoa(*(in_addr*)&client_addr) << std::endl;
    }
    
    // Send DISCOVERY_ACK response
    network_structs::Packet response_packet;
    response_packet.type = network_structs::PacketType::DISCOVERY_ACK;
    response_packet.sequence_number = received_packet.sequence_number;
    
    int n = sendto(sockfd, &response_packet, sizeof(network_structs::Packet), 0, 
                   (struct sockaddr *) &cli_addr, sizeof(struct sockaddr));
    if (n < 0)
        std::cout << "ERROR on sendto" << std::endl;
}

// Processing service - processes individual requests (second writer)
void process_request(RequestData req_data) {
    const auto& received_packet = req_data.packet;
    const auto& cli_addr = req_data.client_addr;
    int sockfd = req_data.sockfd;
    
    uint32_t src_addr = cli_addr.sin_addr.s_addr;
    uint32_t dest_addr = received_packet.data.req.dest_addr;
    uint32_t value = received_packet.data.req.value;
    uint32_t req_id = received_packet.sequence_number;
    
    bool request_processed = false;
    bool transfer_success = false;
    uint32_t new_balance = 0;
    
    {
        std::lock_guard<std::mutex> lock(table_mutex);
        
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
            // Send REQUEST_ACK with last processed request ID
            new_balance = client_table[src_addr].balance;
            
            network_structs::Packet response_packet;
            response_packet.type = network_structs::PacketType::REQUEST_ACK;
            response_packet.sequence_number = client_table[src_addr].last_req;
            response_packet.data.ack.new_balance = new_balance;
            
            int n = sendto(sockfd, &response_packet, sizeof(network_structs::Packet), 0, 
                          (struct sockaddr *) &cli_addr, sizeof(struct sockaddr));
            if (n < 0)
                std::cout << "ERROR on sendto" << std::endl;
            return;
        }
        
        // Process transfer if sufficient balance
        if (client_table[src_addr].balance >= value) {
            client_table[src_addr].balance -= value;
            client_table[dest_addr].balance += value;
            stats.num_transactions++;
            stats.total_transferred += value;
            transfer_success = true;
            
            // Recalculate total_balance
            stats.total_balance = 0;
            for (const auto& entry : client_table) {
                stats.total_balance += entry.second.balance;
            }
        }
        
        // Update last_req after processing
        client_table[src_addr].last_req = req_id;
        new_balance = client_table[src_addr].balance;
        request_processed = true;
        has_update = true;
    }
    
    if (request_processed) {
        table_updated.notify_all();
        
        // Print required information for each request
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        std::cout << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S")
                  << " client " << inet_ntoa(*(in_addr*)&src_addr)
                  << " id req " << req_id
                  << " dest " << inet_ntoa(*(in_addr*)&dest_addr)
                  << " value " << value << std::endl;
        
        // Send REQUEST_ACK response
        network_structs::Packet response_packet;
        response_packet.type = network_structs::PacketType::REQUEST_ACK;
        response_packet.sequence_number = req_id;
        response_packet.data.ack.new_balance = new_balance;
        
        int n = sendto(sockfd, &response_packet, sizeof(network_structs::Packet), 0, 
                      (struct sockaddr *) &cli_addr, sizeof(struct sockaddr));
        if (n < 0)
            std::cout << "ERROR on sendto" << std::endl;
    }
}

// Interface service - displays table updates (reader)
void interface_service() {
    while (server_running) {
        std::unique_lock<std::mutex> lock(table_mutex);
        
        // Wait for updates (blocking read)
        table_updated.wait(lock, [] { return has_update.load() || !server_running.load(); });
        
        if (!server_running) break;
        
        if (has_update) {
            // Display current stats
            auto now = std::chrono::system_clock::now();
            auto time_t = std::chrono::system_clock::to_time_t(now);
            std::cout << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S")
                      << " num_transactions " << stats.num_transactions
                      << " total_transferred " << stats.total_transferred
                      << " total_balance " << stats.total_balance << std::endl;
            
            has_update = false;
        }
    }
}

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

	// Start interface service thread (reader)
	std::thread interface_thread(interface_service);
	
	// Print initial stats
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
			// Handle discovery in discovery service
			handle_discovery(sockfd, received_packet, cli_addr);
		} else if (received_packet.type == network_structs::PacketType::REQUEST) {
			// Create request data for processing thread
			RequestData req_data;
			req_data.sockfd = sockfd;
			req_data.packet = received_packet;
			req_data.client_addr = cli_addr;
			req_data.addr_len = clilen;
			
			// Start new thread for each request (second writer)
			std::thread processing_thread(process_request, req_data);
			processing_thread.detach(); // Let it run independently
		} else {
			std::cout << "Ignored packet" << std::endl;
		}
	}
	
	// Cleanup (this won't be reached in the infinite loop)
	server_running = false;
	table_updated.notify_all();
	interface_thread.join();
	close(sockfd);
	return 0;
}
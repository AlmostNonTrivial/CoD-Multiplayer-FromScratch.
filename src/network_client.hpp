#pragma once
#include "containers.hpp"
#include "lock_free_queue.hpp"
#include "udp_socket.hpp"
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <thread>

#define MAX_PACKET_SIZE	 1500
#define MAX_PEERS		 16
#define PACKET_POOL_SIZE 256
#define WINDOW_SIZE		 32

#pragma pack(push, 1)
struct PacketHeader
{
	uint8_t	 type;
	uint8_t	 flags;
	uint16_t sequence;
	uint32_t ack_bits;
	uint16_t ack;
};

#pragma pack(pop)

/*
 * Every packet needs a header
 */
template <typename T> struct SendPacket
{
	PacketHeader header;
	T			 payload;
	static_assert(sizeof(PacketHeader) + sizeof(T) <= MAX_PACKET_SIZE, "Payload too large");
};

struct PacketBuffer
{
	uint8_t data[MAX_PACKET_SIZE];
};

struct ReceivedPacketInfo
{
	uint8_t		buffer_index;
	sockaddr_in from;
	uint16_t	size;
};

struct PendingPacket
{
	uint8_t	 buffer_idx;
	uint16_t size;
	float	 send_time;
	float	 next_retransmit_time;
	uint8_t	 retry_count;
};

struct Polled
{
	uint32_t from;
	uint8_t *buffer;
	uint16_t size;
	uint8_t	 buffer_index;
};

struct PeerState
{
	sockaddr_in address;

	uint16_t local_sequence;
	uint16_t remote_sequence;
	uint32_t remote_ack_bits;

	uint16_t	  window_start;
	uint32_t	  window_mask;
	PendingPacket window[WINDOW_SIZE];

	float last_seen_time;
	float round_trip_time;
};

struct NetworkClient
{
	UdpSocket		  socket;
	std::thread		  recv_thread;
	std::atomic<bool> running;
	double			  current_time;

	PacketBuffer										  packet_pool[PACKET_POOL_SIZE];
	lock_free_queue<uint8_t, PACKET_POOL_SIZE>			  free_indices;
	lock_free_queue<ReceivedPacketInfo, PACKET_POOL_SIZE> recv_queue;
	fixed_map<uint32_t, PeerState, MAX_PEERS>			  peers;

	void (*on_peer_removed)(uint32_t peer_id);
	bool (*on_unrecognised)(sockaddr_in address);

	void
	receive_thread_func();
};

bool
network_init(NetworkClient *net, const char *bind_ip, uint16_t bind_port);

void
network_shutdown(NetworkClient *net);

uint32_t
network_add_peer(NetworkClient *net, const char *ip, uint16_t port);

uint32_t
network_add_peer(NetworkClient *net, sockaddr_in from);

void
network_remove_peer(NetworkClient *net, uint32_t peer_id);

bool
network_poll(NetworkClient *net, Polled &polled);

void
network_update(NetworkClient *net, float dt);

inline uint32_t
hash_sockaddr(const sockaddr_in &addr)
{
	uint32_t ip = addr.sin_addr.s_addr;
	uint32_t port = ntohs(addr.sin_port);
	return ip ^ (port << 16) ^ (port >> 16);
}

inline void
network_release_buffer(NetworkClient *net, uint8_t buffer_idx)
{
	net->free_indices.try_push(buffer_idx);
}

template <typename T>
static void
network_send(NetworkClient *net, uint32_t peer_id, SendPacket<T> &packet, bool reliable)
{
	PeerState *peer = net->peers.get(peer_id);
	if (!peer)
	{
		printf("Invalid peer ID: %u\n", peer_id);
		return;
	}

	uint8_t buffer_idx;
	if (reliable)
	{
		if (!net->free_indices.try_pop(buffer_idx))
		{
			printf("No free buffers, dropping packet\n");
			return;
		}

		uint16_t next_seq = peer->local_sequence + 1;
		int16_t	 diff = (int16_t)(next_seq - peer->window_start);

		if (diff < 0 || diff >= WINDOW_SIZE)
		{
			printf("Window full, dropping packet\n");
			net->free_indices.try_push(buffer_idx);
			return;
		}
	}

	uint16_t seq = ++peer->local_sequence;
	packet.header.type = 0;
	packet.header.flags = reliable ? 0x01 : 0x00;
	packet.header.sequence = seq;
	packet.header.ack = peer->remote_sequence;
	packet.header.ack_bits = peer->remote_ack_bits;

	uint16_t total_size = sizeof(SendPacket<T>);
	udp_send(&net->socket, &packet, total_size, &peer->address);

	if (!reliable)
	{
		return;
	}

	uint8_t slot = seq & (WINDOW_SIZE - 1);
	memcpy(net->packet_pool[buffer_idx].data, &packet, total_size);

	peer->window[slot].buffer_idx = buffer_idx;
	peer->window[slot].size = total_size;
	peer->window[slot].send_time = net->current_time;

	peer->window[slot].next_retransmit_time = net->current_time + peer->round_trip_time * 1.1;
	peer->window[slot].retry_count = 0;
	peer->window_mask |= (1u << slot);
}

template <typename T>
inline void
network_send_reliable(NetworkClient *net, uint32_t peer_id, SendPacket<T> &packet)
{
	network_send(net, peer_id, packet, true);
}

template <typename T>
inline void
network_send_unreliable(NetworkClient *net, uint32_t peer_id, SendPacket<T> &packet)
{
	network_send(net, peer_id, packet, false);
}

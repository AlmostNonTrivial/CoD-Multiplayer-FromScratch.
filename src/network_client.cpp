/*
 * Reliable UDP Networking abstraction
 *
 * All networking is packet based. TCP provides reliability and ordering
 * by storing the packets until getting an explicit ack for that packet back, and resending after a certain
 * time frame, and ordering is achieved by just numbering each packet sent, so that the recipient knows how to
 * reconstruct it.
 *
 * Of the total number of messages that are sent between client and server, most won't need reliability,
 * We'd actually prefer things like user inputs to not arrive at all than arrive late, moreover it's not that big a
 * deal, for example if a snapshot is lost, you can still interpolate between one that arrived 30ms before that.
 *
 * However certain messages do need to arrive, like connecting a player, or a player dying. But rather
 * than keeping a TCP connection open, we can take advantage of the fact that there is continuous bi-directional
 * traffic between client and server (inputs <-> snapshots).
 *
 * Each packet sent via UDP has a header with an ack bitfield that acts as a sliding window where we can
 * encode which of the last 32 message we received from that peer.
 *
 * Essentially, acks for reliable messages can piggy-back off the existing traffic
 *
 * Unreliable message are send and discarded, reliable messages are kept until we get our ack and
 * then freed.
 */

#include "network_client.hpp"
#include "time.hpp"
#include <cstring>
#include <thread>

#ifdef _MSC_VER
#include <intrin.h>
#endif

#define MAX_RETRANSMIT_ATTEMPTS 10
#define PEER_INACTIVITY_TIMEOUT 4.0f

/*
 * I've tried to use descriptive macros rather than comments
 */
#define CALCULATE_SEQUENCE_OFFSET(new_seq, old_seq) ((int16_t)((new_seq) - (old_seq)))
#define IS_PACKET_NEWER_THAN_LATEST(diff)			((diff) > 0)
#define IS_PACKET_WITHIN_ACK_WINDOW(diff)			((diff) <= 32)
#define IS_PACKET_DUPLICATE(diff)					((diff) == 0)
#define IS_PACKET_TOO_OLD_FOR_ACK(diff)				((-diff) >= 32)
#define MAP_SEQUENCE_TO_WINDOW_SLOT(sequence)		((sequence) & (WINDOW_SIZE - 1))

#define SHIFT_ACK_WINDOW_FOR_NEW_PACKET(ack_bits, diff)		   ((ack_bits) <<= (diff))
#define MARK_PREVIOUS_PACKET_RECEIVED(ack_bits, diff)		   ((ack_bits) |= (1u << ((diff) - 1)))
#define CLEAR_ACK_WINDOW(ack_bits)							   ((ack_bits) = 0)
#define MARK_OUT_OF_ORDER_PACKET_RECEIVED(ack_bits, bit_index) ((ack_bits) |= (1u << (bit_index)))
#define WAS_PACKET_ALREADY_RECEIVED(ack_bits, bit_index)	   (((ack_bits) & (1u << (bit_index))) != 0)
#define HAS_PENDING_ACKS(ack_bits)							   ((ack_bits) != 0)
#define CALCULATE_ACKNOWLEDGED_SEQUENCE(most_recent, index)	   ((most_recent) - (index) - 1)
#define CLEAR_ACKNOWLEDGED_BIT(ack_bits, bit_index)			   ((ack_bits) &= ~(1u << (bit_index)))

#define IS_WINDOW_SLOT_IN_USE(window_mask, slot) (((window_mask) & (1u << (slot))) != 0)
#define CLEAR_WINDOW_SLOT(window_mask, slot)	 ((window_mask) &= ~(1u << (slot)))

#define IS_TIME_TO_RETRANSMIT(current_time, retransmit_time) ((current_time) >= (retransmit_time))
#define HAS_EXCEEDED_MAX_RETRIES(retry_count)				 ((retry_count) >= MAX_RETRANSMIT_ATTEMPTS)

static int
find_lowest_set_bit(uint32_t mask)
{
#ifdef _MSC_VER
	unsigned long index;
	_BitScanForward(&index, mask);
	return (int)index;
#else
	return __builtin_ctz(mask);
#endif
}

static void
acknowledge_packet(NetworkClient *net, PeerState *peer, uint16_t sequence)
{
	int16_t diff = CALCULATE_SEQUENCE_OFFSET(sequence, peer->window_start);

	if (diff < 0 || diff >= WINDOW_SIZE)
	{
		return;
	}

	uint8_t slot = MAP_SEQUENCE_TO_WINDOW_SLOT(sequence);

	if (!IS_WINDOW_SLOT_IN_USE(peer->window_mask, slot))
	{
		return;
	}

	float rtt = net->current_time - peer->window[slot].send_time;

	/*
	 * A real implementation would take a more adaptive view of this,
	 * smoothing the estimated RTT out so that a single dropped packet
	 * doesn't, at minimum, double the RTT
	 */
	peer->round_trip_time = rtt;

	net->free_indices.try_push(peer->window[slot].buffer_idx);
	CLEAR_WINDOW_SLOT(peer->window_mask, slot);
}

static void
process_ack_bitmask(NetworkClient *net, PeerState *peer, uint16_t most_recent_ack, uint32_t ack_bits)
{
	acknowledge_packet(net, peer, most_recent_ack);

	while (HAS_PENDING_ACKS(ack_bits))
	{
		/*
		 * If acks are [0,0,1,0,0,1...], it will find the bit index to be 2
		 */
		int		 bit_index = find_lowest_set_bit(ack_bits);
		uint16_t seq = CALCULATE_ACKNOWLEDGED_SEQUENCE(most_recent_ack, bit_index);
		acknowledge_packet(net, peer, seq);
		CLEAR_ACKNOWLEDGED_BIT(ack_bits, bit_index);
	}
}

static void
advance_window_start(PeerState *peer)
{
	while (HAS_PENDING_ACKS(peer->window_mask))
	{
		uint8_t slot = MAP_SEQUENCE_TO_WINDOW_SLOT(peer->window_start);
		if (IS_WINDOW_SLOT_IN_USE(peer->window_mask, slot))
		{
			break;
		}
		peer->window_start++;
	}

	if (!HAS_PENDING_ACKS(peer->window_mask))
	{
		peer->window_start = peer->local_sequence;
	}
}

static bool
is_new_packet(uint16_t sequence, PeerState *peer)
{
	int16_t diff = CALCULATE_SEQUENCE_OFFSET(sequence, peer->remote_sequence);

	if (IS_PACKET_NEWER_THAN_LATEST(diff))
	{
		if (IS_PACKET_WITHIN_ACK_WINDOW(diff))
		{
			SHIFT_ACK_WINDOW_FOR_NEW_PACKET(peer->remote_ack_bits, diff);
			MARK_PREVIOUS_PACKET_RECEIVED(peer->remote_ack_bits, diff);
		}
		else
		{
			CLEAR_ACK_WINDOW(peer->remote_ack_bits);
		}
		peer->remote_sequence = sequence;
		return true;
	}

	if (IS_PACKET_DUPLICATE(diff))
	{
		return false;
	}

	if (IS_PACKET_TOO_OLD_FOR_ACK(diff))
	{
		return false;
	}

	uint16_t bit_index = -diff - 1;
	bool	 already_received = WAS_PACKET_ALREADY_RECEIVED(peer->remote_ack_bits, bit_index);
	MARK_OUT_OF_ORDER_PACKET_RECEIVED(peer->remote_ack_bits, bit_index);

	return !already_received;
}

static void
check_peer_retransmits(NetworkClient *net, PeerState *peer, uint32_t peer_id)
{
	uint32_t slots_to_check = peer->window_mask;

	while (HAS_PENDING_ACKS(slots_to_check))
	{
		int			   slot = find_lowest_set_bit(slots_to_check);
		PendingPacket *pending = &peer->window[slot];

		if (IS_TIME_TO_RETRANSMIT(net->current_time, pending->next_retransmit_time))
		{
			if (HAS_EXCEEDED_MAX_RETRIES(pending->retry_count))
			{
				network_remove_peer(net, peer_id);
				return;
			}

			udp_send(&net->socket, net->packet_pool[pending->buffer_idx].data, pending->size, &peer->address);
			pending->retry_count++;
			auto retransmission_timeout = peer->round_trip_time * 1.1;
			pending->next_retransmit_time = net->current_time + retransmission_timeout;
		}

		CLEAR_WINDOW_SLOT(slots_to_check, slot);
	}
}

bool
network_init(NetworkClient *net, const char *bind_ip, uint16_t bind_port)
{
	net->running = false;
	net->current_time = 0.0f;
	net->on_peer_removed = nullptr;

	if (udp_create(&net->socket, bind_ip, bind_port, 100) != 0)
	{
		printf("Failed to initialize socket\n");
		return false;
	}

	for (uint16_t i = 0; i < PACKET_POOL_SIZE; i++)
	{
		net->free_indices.try_push(i);
	}

	net->running = true;
	net->recv_thread = std::thread(&NetworkClient::receive_thread_func, net);
	return true;
}

void
network_shutdown(NetworkClient *net)
{
	if (!net->running)
	{
		return;
	}

	net->running = false;
	if (net->recv_thread.joinable())
	{
		net->recv_thread.join();
	}
	udp_close(&net->socket);
}

uint32_t
network_add_peer(NetworkClient *net, const char *ip, uint16_t port)
{
	if (net->peers.size() >= MAX_PEERS)
	{
		printf("Cannot add peer, limit reached \n");
		return 0;
	}

	sockaddr_in addr = create_address(ip, port);
	uint32_t	peer_id = hash_sockaddr(addr);

	if (net->peers.get(peer_id))
	{
		return peer_id;
	}

	PeerState peer = {};
	peer.address = addr;
	peer.last_seen_time = net->current_time;

	net->peers.insert(peer_id, peer);
	return peer_id;
}

uint32_t
network_add_peer(NetworkClient *net, sockaddr_in from)
{
	return network_add_peer(net, inet_ntoa(from.sin_addr), ntohs(from.sin_port));
}

void
network_remove_peer(NetworkClient *net, uint32_t peer_id)
{
	PeerState *peer = net->peers.get(peer_id);
	if (!peer)
	{
		return;
	}

	uint32_t slots_to_free = peer->window_mask;
	while (HAS_PENDING_ACKS(slots_to_free))
	{
		int slot = find_lowest_set_bit(slots_to_free);
		net->free_indices.try_push(peer->window[slot].buffer_idx);
		CLEAR_WINDOW_SLOT(slots_to_free, slot);
	}

	net->peers.remove(peer_id);

	if (net->on_peer_removed)
	{
		net->on_peer_removed(peer_id);
	}
}

bool
network_poll(NetworkClient *net, Polled &polled)
{
	while (1)
	{
		ReceivedPacketInfo info;
		if (!net->recv_queue.try_pop(info))
		{
			return false;
		}

		PacketBuffer &packet = net->packet_pool[info.buffer_index];
		PacketHeader *header = (PacketHeader *)packet.data;
		uint32_t	  peer_id = hash_sockaddr(info.from);
		PeerState	 *peer = net->peers.get(peer_id);

		if (!peer)
		{
			if (!net->on_unrecognised || !net->on_unrecognised(info.from))
			{
				net->free_indices.try_push(info.buffer_index);
				continue;
			}
			peer = net->peers.get(peer_id);
		}

		peer->last_seen_time = net->current_time;

		process_ack_bitmask(net, peer, header->ack, header->ack_bits);
		advance_window_start(peer);

		bool is_new = is_new_packet(header->sequence, peer);
		if (!is_new)
		{
			net->free_indices.try_push(info.buffer_index);
			continue;
		}

		polled.from = peer_id;
		polled.buffer = packet.data + sizeof(PacketHeader);
		polled.size = info.size - sizeof(PacketHeader);
		polled.buffer_index = info.buffer_index;
		return true;
	}
}

void
network_update(NetworkClient *net, float dt)
{
	net->current_time += dt;

	for (auto &&[id, peer] : net->peers)
	{
		if (net->current_time - peer.last_seen_time > PEER_INACTIVITY_TIMEOUT)
		{
			/* This won't mess up the iterator */
			network_remove_peer(net, id);
			continue;
		}
		check_peer_retransmits(net, &peer, id);
	}
}

void
NetworkClient::receive_thread_func()
{
	/*
	 * Receive packets and place them in the packet pool shared between
	 * thread A and B (single producer, single consumer).
	 *
	 * While the pool itself is not thread safe, we acquire specific indexes into it
	 * with a thread safe queue.
	 */
	while (running)
	{
		uint8_t buffer_idx;
		if (!free_indices.try_pop(buffer_idx))
		{
			printf("No slots free, waiting \n");
			sleep_microseconds(100);
			continue;
		}

		sockaddr_in from;
		int			bytes = udp_receive(&socket, packet_pool[buffer_idx].data, MAX_PACKET_SIZE, &from);

		if (bytes >= (int)sizeof(PacketHeader))
		{
			ReceivedPacketInfo info;
			info.buffer_index = buffer_idx;
			info.from = from;
			info.size = bytes;

			if (!recv_queue.try_push(info))
			{
				free_indices.try_push(buffer_idx);
			}
		}
		else
		{
			free_indices.try_push(buffer_idx);
			udp_is_error(bytes);
		}
	}
}

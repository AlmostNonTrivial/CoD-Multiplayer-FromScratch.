/*
 * Single-producer single-consumer lock-free queue for cross-thread communication.
 *
 * In our use case, we have a thread that listens for incoming packets and stores them
 * to be polled by the main thread. To get thread safety, we could use a lock, but
 * because of the potentially high frequency of reception (the server is receiving 60 packets
 * per second per player at full load) the lock could be a bottleneck.
 *
 * To understand a lock-free queue, you need to understand atomics, and memory ordering, and presumably
 * a lot of other cool stuff: [https://www.youtube.com/watch?v=K3P_Lmq6pw0](https://www.youtube.com/watch?v=K3P_Lmq6pw0)
 *
 * The gist is: it's a ring buffer, where one thread owns the write position, another owns the read position
 * and atomics make sure they don't overlap due to a race condition.
 *
 * While not actually affecting correctness, we want entries to fit within, and aligned to a single cache line
 * to avoid something called 'false sharing', where multiple threads access variables within the same cache line.
 */

#pragma once
#include <atomic>
#include <cstddef>

template <typename T, size_t Capacity> class lock_free_queue
{
  private:
	struct PaddedT
	{
		T	 value;
		char padding[64 - (sizeof(T) % 64)];
	};
	static_assert(sizeof(PaddedT) == 64, "PaddedT must be 64 bytes");
	static_assert(Capacity > 0, "Capacity must be greater than 0");
	static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of 2");

	alignas(64) std::atomic<size_t> write_pos{0};
	alignas(64) std::atomic<size_t> read_pos{0};
	alignas(64) PaddedT buffer[Capacity];

  public:
	bool
	try_push(const T &item)
	{
		size_t write = write_pos.load(std::memory_order_relaxed);
		size_t next_write = (write + 1) & (Capacity - 1);
		if (next_write == read_pos.load(std::memory_order_acquire))
		{
			return false; /* Full */
		}
		buffer[write].value = item;
		write_pos.store(next_write, std::memory_order_release);
		return true;
	}

	bool
	try_pop(T &item)
	{
		size_t read = read_pos.load(std::memory_order_relaxed);
		if (read == write_pos.load(std::memory_order_acquire))
		{
			return false; /* Empty */
		}
		item = buffer[read].value;
		read_pos.store((read + 1) & (Capacity - 1), std::memory_order_release);
		return true;
	}
};

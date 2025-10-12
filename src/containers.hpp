/*
 * Custom fixed sized containers
 *
 * I've found in games we can often decide on an upper bound to the number of entries we'd need
 * in a container ahead time. With this projects limited scope, we can avoid dynamic allocations
 * within the frame completely
 *
 */

#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <string_view>
#include <type_traits>

/*
 * The ring_buffer is particularly useful in this project
 * because in several places, we would ideally like to push a value
 * onto a queue forever, implicitly removing entries beyond a certain age
 * (or index specifically).
 *
 * A ring buffer is essentially a queue that you do a '% capacity' such that
 * pushing wraps round, and the head and tail pointers move with it.
 * But rather than actually doing a mod, if it's a power of 2 we can use an bitwise & instead.
 */
template <typename T, size_t N> class ring_buffer
{
  private:
	static_assert(N > 0, "Capacity must be greater than 0");
	static_assert((N & (N - 1)) == 0, "Capacity must be power of 2");

	T	   buffer[N];
	size_t size_ = 0;
	size_t head = 0;
	size_t tail = 0;

  public:
	void
	push(const T &item)
	{
		buffer[tail] = item;
		if (size_ < N)
		{
			size_++;
		}
		else
		{
			head = (head + 1) & (N - 1);
		}
		tail = (tail + 1) & (N - 1);
	}

	T *
	pop()
	{
		if (empty())
		{
			return nullptr;
		}
		T *item = &buffer[head];
		head = (head + 1) & (N - 1);
		size_--;
		return item;
	}

	T *
	front()
	{
		return empty() ? nullptr : &buffer[head];
	}

	T *
	back()
	{
		if (empty())
		{
			return nullptr;
		}
		size_t back_idx = (tail == 0) ? N - 1 : tail - 1;
		return &buffer[back_idx];
	}

	T *
	at(size_t index)
	{
		if (index >= size_)
		{
			return nullptr;
		}
		size_t actual_index = (head + index) & (N - 1);
		return &buffer[actual_index];
	}

	bool
	empty() const
	{
		return size_ == 0;
	}
	bool
	full() const
	{
		return size_ == N;
	}
	size_t
	size() const
	{
		return size_;
	}
	static constexpr size_t
	capacity()
	{
		return N;
	}

	struct iterator
	{
		T	  *data;
		size_t index;
		size_t remaining;

		T &
		operator*()
		{
			return data[index];
		}
		T *
		operator->()
		{
			return &data[index];
		}

		iterator &
		operator++()
		{
			if (remaining > 0)
			{
				index = (index + 1) & (N - 1);
				remaining--;
			}
			return *this;
		}

		bool
		operator!=(const iterator &other) const
		{
			return remaining != other.remaining;
		}
	};

	iterator
	begin()
	{
		return {buffer, head, size_};
	}
	iterator
	end()
	{
		return {buffer, 0, 0};
	}
};

template <size_t N> struct fixed_string
{
	char data[N];

	void
	set(const char *str, size_t len = 0)
	{
		if (0 == len)
		{
			len = strlen(str);
		}
		if (len >= N)
		{
			len = N - 1;
		}
		memcpy(data, str, len);
		data[len] = '\0';
		if (len + 1 < N)
		{
			memset(data + len + 1, 0, N - len - 1);
		}
	}

	fixed_string &
	operator=(const char *str)
	{
		if (str)
		{
			set(str, strlen(str));
		}
		else
		{
			memset(data, 0, N);
		}
		return *this;
	}

	bool
	operator==(const fixed_string &other) const
	{
		return strcmp(data, other.data) == 0;
	}

	bool
	operator==(const char *str) const
	{
		return str && strcmp(data, str) == 0;
	}

	bool
	operator==(std::string_view sv) const
	{
		size_t my_len = strlen(data);
		return my_len == sv.size() && memcmp(data, sv.data(), my_len) == 0;
	}

	bool
	operator!=(const fixed_string &other) const
	{
		return !(*this == other);
	}

	size_t
	length() const
	{
		return strlen(data);
	}

	bool
	empty() const
	{
		return data[0] == '\0';
	}

	const char *
	c_str() const
	{
		return data;
	}

	char *
	c_str()
	{
		return data;
	}
};

template <typename T, size_t N> struct fixed_array
{
	T		 data[N];
	uint32_t m_size = 0;

	fixed_array() = default;

	fixed_array(std::initializer_list<T> init)
	{
		for (const T &value : init)
		{
			if (m_size >= N)
			{
				break;
			}
			data[m_size++] = value;
		}
	}

	bool
	push(const T &value)
	{
		if (m_size >= N)
		{
			return false;
		}
		data[m_size++] = value;
		return true;
	}

	T *
	pop_back()
	{
		if (m_size == 0)
		{
			return nullptr;
		}
		return &data[--m_size];
	}

	void
	clear()
	{
		m_size = 0;
	}

	T *
	get(uint32_t index)
	{
		if (index >= m_size)
		{
			return nullptr;
		}
		return &data[index];
	}

	T &
	operator[](uint32_t index)
	{
		return data[index];
	}

	T *
	back()
	{
		return m_size > 0 ? &data[m_size - 1] : nullptr;
	}

	T *
	front()
	{
		return m_size > 0 ? &data[0] : nullptr;
	}

	T *
	begin()
	{
		return data;
	}
	T *
	end()
	{
		return data + m_size;
	}

	bool
	empty()
	{
		return m_size == 0;
	}
	uint32_t
	size()
	{
		return m_size;
	}
	constexpr uint32_t
	capacity()
	{
		return N;
	}
};

template <typename T, size_t N> struct fixed_queue
{
	T		 data[N];
	uint32_t m_head = 0;
	uint32_t m_tail = 0;
	uint32_t m_count = 0;

	fixed_queue() = default;

	fixed_queue(std::initializer_list<T> init)
	{
		for (const T &value : init)
		{
			push(value);
		}
	}

	bool
	push(const T &value)
	{
		if (m_count >= N)
		{
			return false;
		}
		data[m_tail] = value;
		m_tail = (m_tail + 1) % N;
		m_count++;
		return true;
	}

	T *
	pop()
	{
		if (m_count == 0)
		{
			return nullptr;
		}
		T *result = &data[m_head];
		m_head = (m_head + 1) % N;
		m_count--;
		return result;
	}

	T *
	front()
	{
		return m_count > 0 ? &data[m_head] : nullptr;
	}

	T *
	back()
	{
		if (m_count == 0)
		{
			return nullptr;
		}
		uint32_t back_idx = (m_tail + N - 1) % N;
		return &data[back_idx];
	}

	void
	clear()
	{
		m_head = 0;
		m_tail = 0;
		m_count = 0;
	}

	struct queue_iterator
	{
		T		*data;
		uint32_t capacity;
		uint32_t index;
		uint32_t remaining;

		T &
		operator*()
		{
			return data[index];
		}
		T *
		operator->()
		{
			return &data[index];
		}

		queue_iterator &
		operator++()
		{
			if (remaining > 0)
			{
				index = (index + 1) % capacity;
				remaining--;
			}
			return *this;
		}

		bool
		operator!=(const queue_iterator &other) const
		{
			return remaining != other.remaining;
		}
	};

	queue_iterator
	begin()
	{
		return {data, N, m_head, m_count};
	}

	queue_iterator
	end()
	{
		return {data, N, 0, 0};
	}

	bool
	empty()
	{
		return m_count == 0;
	}
	uint32_t
	size()
	{
		return m_count;
	}
	constexpr uint32_t
	capacity()
	{
		return N;
	}
};

template <typename T>
constexpr inline T
round_up_power_of_2(T n)
{
	static_assert(std::is_unsigned_v<T>);
	n--;
	n |= n >> 1;
	n |= n >> 2;
	n |= n >> 4;
	n |= n >> 8;
	if constexpr (sizeof(T) > 1)
	{
		n |= n >> 16;
	}

	if constexpr (sizeof(T) > 4)
	{
		n |= n >> 32;
	}

	return n + 1;
}

inline uint32_t
hash_bytes(const void *data, size_t len)
{
	if (!data || len == 0)
	{
		return 1;
	}

	const uint8_t *bytes = (const uint8_t *)data;
	uint32_t	   h = 2166136261u;
	for (size_t i = 0; i < len; i++)
	{
		h ^= bytes[i];
		h *= 16777619u;
	}
	return h ? h : 1;
}

inline uint32_t
hash_int(uint64_t x)
{
	x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
	x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
	x = x ^ (x >> 31);
	return static_cast<uint32_t>(x) ? static_cast<uint32_t>(x) : 1;
}

template <typename K, typename V, size_t N> struct fixed_map
{

	static constexpr size_t table_size = round_up_power_of_2(N + N / 2);

	struct entry
	{
		K		 key;
		V		 value;
		uint32_t hash;
		uint8_t	 state = 0; /* 0=empty, 1=occupied, 2=tombstone */
	};

	entry	 m_data[table_size] = {};
	uint32_t m_size = 0;
	uint32_t m_tombstones = 0;

	fixed_map() = default;

	fixed_map(std::initializer_list<std::pair<K, V>> init)
	{
		for (const auto &[key, value] : init)
		{
			insert(key, value);
		}
	}

  private:
	template <typename T, typename = void> struct has_c_str : std::false_type
	{
	};

	template <typename T> struct has_c_str<T, std::void_t<decltype(std::declval<T>().c_str())>> : std::true_type
	{
	};

	uint32_t
	hash_key(const K &key) const
	{
		if constexpr (has_c_str<K>::value)
		{
			const char *s = key.c_str();
			return hash_bytes(s, strlen(s));
		}
		else if constexpr (std::is_same_v<K, std::string_view>)
		{
			return hash_bytes(key.data(), key.size());
		}
		else if constexpr (std::is_pointer_v<K>)
		{
			return hash_int(reinterpret_cast<uintptr_t>(key));
		}
		else if constexpr (std::is_integral_v<K>)
		{
			return hash_int(static_cast<uint64_t>(key));
		}
		else
		{
			static_assert(sizeof(K) == 0, "Unsupported key type");
			return 0;
		}
	}

  public:
	V *
	get(const K &key)
	{
		if (m_size == 0)
		{
			return nullptr;
		}

		uint32_t hash = hash_key(key);
		uint32_t mask = table_size - 1;
		uint32_t idx = hash & mask;

		while (true)
		{
			entry &e = m_data[idx];
			if (e.state == 0)
			{
				return nullptr;
			}
			if (e.state == 1 && e.hash == hash && e.key == key)
			{
				return &e.value;
			}
			idx = (idx + 1) & mask;
		}
	}

	V *
	insert(const K &key, const V &value)
	{
		if (m_size >= N)
		{
			return nullptr;
		}

		uint32_t hash = hash_key(key);
		uint32_t mask = table_size - 1;
		uint32_t idx = hash & mask;
		int32_t first_deleted = -1;

		while (true)
		{
			entry &e = m_data[idx];

			if (e.state == 0)
			{
				entry *target = (first_deleted != -1) ? &m_data[first_deleted] : &e;
				target->key = key;
				target->value = value;
				target->hash = hash;
				target->state = 1;

				if (first_deleted != -1)
				{
					m_tombstones--;
				}
				m_size++;
				return &target->value;
			}

			if (e.state == 2 && first_deleted == -1)
			{
				first_deleted = idx;
			}
			else if (e.state == 1 && e.hash == hash && e.key == key)
			{
				e.value = value;
				return &e.value;
			}

			idx = (idx + 1) & mask;
		}
	}

	bool
	remove(const K &key)
	{
		if (m_size == 0)
		{
			return false;
		}

		uint32_t hash = hash_key(key);
		uint32_t mask = table_size - 1;
		uint32_t idx = hash & mask;

		while (true)
		{
			entry &e = m_data[idx];
			if (e.state == 0)
			{
				return false;
			}
			if (e.state == 1 && e.hash == hash && e.key == key)
			{
				e.state = 2;
				m_size--;
				m_tombstones++;
				return true;
			}
			idx = (idx + 1) & mask;
		}
	}

	void
	clear()
	{
		for (size_t i = 0; i < table_size; i++)
		{
			m_data[i].state = 0;
		}
		m_size = 0;
		m_tombstones = 0;
	}

	struct map_iterator
	{
		entry	*entries;
		uint32_t capacity;
		uint32_t index;

		void
		advance_to_next_valid()
		{
			while (index < capacity && entries && entries[index].state != 1)
			{
				++index;
			}
		}

		map_iterator(entry *e, uint32_t cap, uint32_t idx) : entries(e), capacity(cap), index(idx)
		{
			advance_to_next_valid();
		}

		std::pair<K &, V &>
		operator*()
		{
			return {entries[index].key, entries[index].value};
		}

		struct arrow_proxy
		{
			std::pair<K &, V &> p;
			std::pair<K &, V &> *
			operator->()
			{
				return &p;
			}
		};

		arrow_proxy
		operator->()
		{
			return {{entries[index].key, entries[index].value}};
		}

		map_iterator &
		operator++()
		{
			++index;
			advance_to_next_valid();
			return *this;
		}

		bool
		operator!=(const map_iterator &other) const
		{
			return index != other.index;
		}
	};

	map_iterator
	begin()
	{
		return map_iterator(m_data, table_size, 0);
	}

	map_iterator
	end()
	{
		return map_iterator(m_data, table_size, table_size);
	}

	bool
	empty()
	{
		return m_size == 0;
	}
	uint32_t
	size()
	{
		return m_size;
	}
	constexpr uint32_t
	capacity()
	{
		return N;
	}
	constexpr uint32_t
	table_capacity()
	{
		return table_size;
	}
	bool
	contains(const K &key)
	{
		return get(key) != nullptr;
	}
	entry *
	data()
	{
		return m_data;
	}
};

template <typename K, size_t N> using fixed_hash_set = fixed_map<K, char, N>;

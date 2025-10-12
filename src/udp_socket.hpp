#pragma once
#include <cerrno>
#include <cstdint>
/*
 * Cross-platform UDP socket wrapper
 * Windows: Uses Winsock2, Unix: Uses BSD sockets
 */

#include <cstdio>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
typedef SOCKET SocketHandle;
#define INVALID_SOCKET_HANDLE INVALID_SOCKET
#define CLOSE_SOCKET		  closesocket
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/fcntl.h>
#include <unistd.h>
typedef int SocketHandle;
#define INVALID_SOCKET_HANDLE -1
#define CLOSE_SOCKET		  close
#endif

#include <cassert>
#include <cstring>

struct UdpSocket
{
	SocketHandle sock_fd = INVALID_SOCKET_HANDLE;
	sockaddr_in	 bound_address;
};

inline sockaddr_in
create_address(const char *ip, unsigned short port)
{
	sockaddr_in addr = {};
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	if (ip == nullptr || strcmp(ip, "0.0.0.0") == 0)
	{
		addr.sin_addr.s_addr = htonl(INADDR_ANY);
	}
	else
	{
		addr.sin_addr.s_addr = inet_addr(ip);
	}
	return addr;
}

/* timeout_ms: receive timeout in milliseconds (0 = blocking forever) */
inline int
udp_create(UdpSocket *sock, const char *ip, uint16_t port, uint32_t timeout_ms = 0)
{
#ifdef _WIN32
	/* Initialize Winsock once per program (not per socket) */
	static bool wsa_initialized = false;
	if (!wsa_initialized)
	{
		WSADATA wsa_data;
		if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0)
		{
			return -1;
		}
		wsa_initialized = true;
	}
#endif

	sock->sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock->sock_fd == INVALID_SOCKET_HANDLE)
	{
		return -1;
	}

	sock->bound_address = create_address(ip, port);
	if (bind(sock->sock_fd, (struct sockaddr *)&sock->bound_address, sizeof(sock->bound_address)) < 0)
	{
		CLOSE_SOCKET(sock->sock_fd);
		return -1;
	}

	if (timeout_ms > 0)
	{
#ifdef _WIN32
		DWORD timeout = timeout_ms;
		if (setsockopt(sock->sock_fd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout, sizeof(timeout)) < 0)
		{
#else
		struct timeval tv;
		tv.tv_sec = timeout_ms / 1000;
		tv.tv_usec = (timeout_ms % 1000) * 1000;
		if (setsockopt(sock->sock_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0)
		{
#endif
			CLOSE_SOCKET(sock->sock_fd);
			return -1;
		}
	}

	return 0;
}

/* Returns bytes sent, or -1 on error */
inline int
udp_send(UdpSocket *socket, const void *data, size_t size, const sockaddr_in *dest)
{

// #define TEST_PACKET_LOSS
#ifdef TEST_PACKET_LOSS
	if ((rand() % 100) < 5)
	{
		return size;
	}
#endif

	return sendto(socket->sock_fd, (const char *)data, (int)size, 0, (struct sockaddr *)dest, sizeof(*dest));
}

inline int
udp_receive(UdpSocket *socket, void *buffer, size_t buffer_size, sockaddr_in *sender)
{
	socklen_t sender_len = sizeof(*sender);
	return recvfrom(socket->sock_fd, (char *)buffer, (int)buffer_size, 0, (struct sockaddr *)sender, &sender_len);
}

inline void
udp_close(UdpSocket *socket)
{
	if (socket->sock_fd != INVALID_SOCKET_HANDLE)
	{
		CLOSE_SOCKET(socket->sock_fd);
		socket->sock_fd = INVALID_SOCKET_HANDLE;
	}
}

inline bool
udp_is_error(int recv_result)
{
#ifdef _WIN32
	if (recv_result < 0)
	{
		int err = WSAGetLastError();
		if (err != WSAEWOULDBLOCK)
		{
			printf("udp_receive error: %d\n", err);
			return true;
		}
	}
	return false;
#else
	if (recv_result < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
	{
		perror("udp_receive");
		return true;
	}
	return false;
#endif
}

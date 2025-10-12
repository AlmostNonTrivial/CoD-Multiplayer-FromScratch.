
#include "ai.hpp"
#include "client.hpp"
#include "game_types.hpp"
#include "server.hpp"
#include <cstdlib>
#include <cstring>

int
main(int argc, char **argv)
{

	if (argc > 1 && strcmp(argv[1], "server") == 0)
	{
		run_server();
	}
	else if (argc > 2 && strcmp(argv[1], "npcs") == 0)
	{
		uint32_t count = std::min(atoi(argv[2]), MAX_PLAYERS - 1);
		ai_run_npcs("127.0.0.1", "bot", count);
	}
	else if (argc > 1)
	{

		int port = atoi(argv[1]);

		if (port == SERVER_PORT)
		{
			printf("%d is reserved for server\n", SERVER_PORT);
			exit(0);
		}

		run_client("127.0.0.1", "markymark", port, 0, 0, 1920, 800);
	}

	return 0;
}

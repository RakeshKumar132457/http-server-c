#include "../include/error.h"
#include "../include/http_server.h"
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>

int main(int argc, char **argv) {
  struct server_opt *s_opt = parse_command_line(argc, argv);
  int server_fd = create_server_socket(s_opt);
  printf("Waiting for a client to connect...\n");
  while (1) {
    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    int client_fd =
        accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_len);
    if (client_fd < 0) {
      handle_error("Accept failed");
    }

    handle_client(client_fd);
  }

  return EXIT_SUCCESS;
}

#include "../include/error.h"
#include "../include/http_server.h"

#include <getopt.h>
#include <netinet/in.h>
#include <unistd.h>

#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#define BACKLOG         5
#define MAX_BUFFER_SIZE 4096
#define PORT            4221

struct server_opt {
    char *directory;
    int port;
};

static struct server_opt *create()
{
    struct server_opt *s = malloc(sizeof(struct server_opt));
    if (s == NULL) {
        handle_error("Failed to create server options");
    }
    return s;
}

struct server_opt *parse_command_line(int argc, char **argv)
{
    int opt;
    struct option long_options[] = {
        { "directory", required_argument, NULL, 'd' },
        { "port", required_argument, NULL, 'p' },
        { NULL, 0, NULL, 0 }
    };

    struct server_opt *s_opt = create();

    while ((opt = getopt_long(argc, argv, "d:p:", long_options, NULL)) != -1) {
        switch (opt) {
        case 'd':
            s_opt->directory = strdup(optarg);
            break;
        case 'p':
            s_opt->port = atoi(optarg);
        }
    }

    return s_opt;
}

int create_server_socket(struct server_opt *s)
{
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        handle_error("Socket creation failed");
    }

    int reuse = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse)) <
        0) {
        handle_error("SO_REUSEPORT failed");
    }

    struct sockaddr_in serv_addr = { .sin_family = AF_INET,
                                     .sin_port =
                                         htons(s->port > 0 ? s->port : PORT),
                                     .sin_addr = { htonl(INADDR_ANY) } };

    if (bind(server_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) !=
        0) {
        handle_error("Bind failed");
    }

    if (listen(server_fd, BACKLOG) != 0) {
        handle_error("Listen failed");
    }
    return server_fd;
}

void *handle_client(int arg)
{
    int client_fd = arg;

    char buffer[MAX_BUFFER_SIZE];
    ssize_t bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);
    if (bytes_read <= 0) {
        close(client_fd);
        return NULL;
    }
    buffer[bytes_read] = '\0';
    close(client_fd);
    return NULL;
}

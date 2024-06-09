#include <asm-generic/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define MAX_BUFFER_SIZE 4096
#define MAX_HEADER_SIZE 1024
#define SERVER_PORT 4221
#define BACKLOG 1

typedef struct {
    int status_code;
    const char *reason_phrase;
    const char *content_type;
    char *body;
} Response;

int create_server_socket() {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        perror("Socket creation failed");
        exit(1);
    }

    int reuse = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse)) < 0) {
        perror("SO_REUSEPORT failed");
        exit(1);
    }

    struct sockaddr_in serv_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(SERVER_PORT),
        .sin_addr = {htonl(INADDR_ANY)},
    };

    if (bind(server_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) != 0) {
        perror("Bind failed");
        exit(1);
    }

    if (listen(server_fd, BACKLOG) != 0) {
        perror("Listen failed");
        exit(1);
    }

    return server_fd;
}

char *get_header_value(const char *header_start, const char *header_name) {
    char *header_end;
    while ((header_end = strstr(header_start, "\r\n"))) {
        if (header_start == header_end) {
            break;
        }

        char header[MAX_HEADER_SIZE];
        sscanf(header_start, "%[^\r\n]", header);
        header_start = header_end + 2;

        char name[MAX_HEADER_SIZE], value[MAX_HEADER_SIZE];
        if (sscanf(header, "%[^:]: %[^\r\n]", name, value) == 2 && strcmp(name, header_name) == 0) {
            return strdup(value);
        }
    }
    return NULL;
}

Response *create_response(int status_code, const char *reason_phrase, const char *body) {
    Response *response = malloc(sizeof(Response));
    response->status_code = status_code;
    response->reason_phrase = reason_phrase;
    response->content_type = "text/plain";
    response->body = strdup(body);
    return response;
}

Response *process_request(const char *method, const char *path, const char *user_agent) {
    if (strcmp(path, "/") == 0) {
        return create_response(200, "OK", "");
    } else if (strncmp(path, "/echo/", 6) == 0) {
        return create_response(200, "OK", path + 6);
    } else if (strcmp(path, "/user-agent") == 0) {
        return create_response(200, "OK", user_agent ? user_agent : "");
    } else {
        return create_response(404, "Not Found", "");
    }
}

void send_response(int fd, Response *response) {
    char headers[MAX_HEADER_SIZE];
    snprintf(headers, sizeof(headers),
             "HTTP/1.1 %d %s\r\n"
             "Content-Type: %s\r\n"
             "Content-Length: %zu\r\n"
             "Connection: keep-alive\r\n"
             "\r\n",
             response->status_code, response->reason_phrase, response->content_type, strlen(response->body));

    ssize_t headers_len = strlen(headers);
    ssize_t body_len = strlen(response->body);

    ssize_t total_len = headers_len + body_len;
    char *response_buffer = malloc(total_len);
    if (response_buffer == NULL) {
        perror("Memory allocation failed");
        return;
    }

    memcpy(response_buffer, headers, headers_len);
    memcpy(response_buffer + headers_len, response->body, body_len);

    ssize_t bytes_sent = send(fd, response_buffer, total_len, 0);
    if (bytes_sent < 0) {
        perror("Send failed");
    }

    free(response_buffer);
}

void free_response(Response *response) {
    free(response->body);
    free(response);
}

void *handle_request(void *arg) {
    int client_fd = *(int *)arg;
    free(arg);

    char buffer[MAX_BUFFER_SIZE];
    ssize_t byte_read = read(client_fd, buffer, sizeof(buffer) - 1);
    if (byte_read <= 0) {
        if (byte_read < 0) {
            perror("Read failed");
        }
        close(client_fd);
        return NULL;
    }
    buffer[byte_read] = '\0';

    char method[MAX_BUFFER_SIZE], path[MAX_BUFFER_SIZE], version[MAX_BUFFER_SIZE];
    sscanf(buffer, "%s %s %s", method, path, version);

    char *user_agent = get_header_value(strstr(buffer, "\r\n") + 2, "User-Agent");

    Response *response = process_request(method, path, user_agent);
    send_response(client_fd, response);

    free_response(response);
    free(user_agent);

    close(client_fd);
    return NULL;
}

int main() {
    setbuf(stdout, NULL);
    printf("Logs from your program will appear here!\n");

    int server_fd = create_server_socket();
    printf("Waiting for a client to connect...\n");

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_len);
        if (client_fd < 0) {
            perror("Accept failed");
            continue;
        }

        pthread_t thread;
        int *arg = malloc(sizeof(int));
        *arg = client_fd;
        if (pthread_create(&thread, NULL, handle_request, arg) != 0) {
            perror("Thread creation failed");
            close(client_fd);
            free(arg);
        } else {
            pthread_detach(thread);
        }
    }

    close(server_fd);
    return 0;
}

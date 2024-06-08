#include <errno.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

int main() {
    // Disable output buffering
    setbuf(stdout, NULL);

    // You can use print statements as follows for debugging, they'll be visible when running tests.
    printf("Logs from your program will appear here!\n");

    // Uncomment this block to pass the first stage

    socklen_t server_fd, client_addr_len;
    struct sockaddr_in client_addr;

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        printf("Socket creation failed: %s...\n", strerror(errno));
        return 1;
    }

    // Since the tester restarts your program quite often, setting REUSE_PORT
    // ensures that we don't run into 'Address already in use' errors
    int reuse = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse)) < 0) {
        printf("SO_REUSEPORT failed: %s \n", strerror(errno));
        return 1;
    }

    struct sockaddr_in serv_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(4221),
        .sin_addr = {htonl(INADDR_ANY)},
    };

    if (bind(server_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) != 0) {
        printf("Bind failed: %s \n", strerror(errno));
        return 1;
    }

    int connection_backlog = 5;
    if (listen(server_fd, connection_backlog) != 0) {
        printf("Listen failed: %s \n", strerror(errno));
        return 1;
    }

    printf("Waiting for a client to connect...\n");
    client_addr_len = sizeof(client_addr);

    int fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_len);
    printf("Client connected\n");

    char buffer[4096];
    ssize_t byte_read = read(fd, buffer, sizeof(buffer) - 1);
    if (byte_read < 0) {
        printf("Read failed: %s\n", strerror(errno));
        close(fd);
        close(server_fd);
        return 1;
    }
    buffer[byte_read] = '\0';

    printf("Received request:\n %s\n", buffer);

    char method[8], path[1024], version[16];
    sscanf(buffer, "%s %s %s", method, path, version);

    printf("Method: %s, Path: %s, Version: %s\n", method, path, version);

    int http_status_code;
    const char *reason_phrase;
    const char *content_type = "text/plain";
    char *response_body = NULL;
    if (strncmp(path, "/echo/", 6) == 0) {
        http_status_code = 200;
        reason_phrase = "OK";
        size_t response_lenth = strlen(path + 6) + 1;
        response_body = malloc(response_lenth);
        if (response_body != NULL) {
            strcpy(response_body, path + 6);
        }
    } else {
        http_status_code = 404;
        reason_phrase = "Not Found";
    }
    char headers[1024];
    snprintf(headers, sizeof(headers),
             "HTTP/1.1 %d %s \r\n"
             "Content-Type: %s\r\n"
             "Content-Length: %zu\r\n"
             "\r\n",
             http_status_code, reason_phrase, content_type, strlen(response_body));
    char response[4096];
    snprintf(response, sizeof(response), "%s%s", headers, response_body);
    int byte_sent = send(fd, response, strlen(response), 0);
    if (byte_sent < 0) {
        printf("Send failed: %s\n", strerror(errno));
        return 1;
    }

    close(server_fd);
    return 0;
}

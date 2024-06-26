#include <getopt.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zlib.h>

#define MAX_BUFFER_SIZE 4096
#define MAX_HEADER_SIZE 1024
#define MAX_HEADERS 20
#define MAX_FILENAME_LENGTH 256
#define SERVER_PORT 4221
#define BACKLOG 1

char *dir_path = "/tmp";

typedef enum {
    HTTP_OK = 200,
    HTTP_CREATED = 201,
    HTTP_BAD_REQUEST = 400,
    HTTP_NOT_FOUND = 404,
    HTTP_INTERNAL_SERVER_ERROR = 500,
} HttpStatusCode;

typedef enum {
    HTTP_GET,
    HTTP_POST,
} HttpMethod;

typedef enum {
    EXACT_MATCH,
    PARTIAL_MATCH
} RouteMatchType;

typedef struct {
    char *key;
    char *value;
} Header;

typedef struct {
    HttpStatusCode status_code;
    const char *reason_phrase;
    const char *content_type;
    Header headers[MAX_HEADERS];
    int num_headers;
    char *body;
    size_t body_length;
} Response;

typedef struct {
    HttpMethod method;
    const char *path;
    RouteMatchType match_type;
    Response *(*handler)(const char *, const char *);
} Route;

Response *create_response(HttpStatusCode status_code, const char *content_type, const char *body) {
    Response *response = (Response *)malloc(sizeof(Response));
    response->status_code = status_code;
    response->reason_phrase = NULL;
    response->content_type = content_type;
    response->num_headers = 0;
    response->body = strdup(body);
    return response;
}

void set_header(Response *response, const char *key, const char *value) {
    if (response->num_headers < MAX_HEADERS) {
        response->headers[response->num_headers].key = strdup(key);
        response->headers[response->num_headers].value = strdup(value);
        response->num_headers++;
    }
}

char *serialize_status_line(Response *response) {
    const char *reason_phrase;
    switch (response->status_code) {
    case HTTP_OK:
        reason_phrase = "OK";
        break;
    case HTTP_CREATED:
        reason_phrase = "Created";
        break;
    case HTTP_BAD_REQUEST:
        reason_phrase = "Bad Request";
        break;
    case HTTP_NOT_FOUND:
        reason_phrase = "Not Found";
        break;
    case HTTP_INTERNAL_SERVER_ERROR:
        reason_phrase = "Internal Server Error";
        break;
    default:
        reason_phrase = "Unknown";
        break;
    }

    char *status_line = (char *)malloc(MAX_HEADER_SIZE);
    snprintf(status_line, MAX_HEADER_SIZE, "HTTP/1.1 %d %s\r\n", response->status_code, reason_phrase);
    return status_line;
}

char *serialize_headers(Response *response) {
    char *headers = (char *)malloc(MAX_HEADER_SIZE);
    strcpy(headers, "");

    char header[MAX_HEADER_SIZE];
    snprintf(header, MAX_HEADER_SIZE, "Content-Type: %s\r\n", response->content_type);
    strcat(headers, header);

    snprintf(header, MAX_HEADER_SIZE, "Content-Length: %lu\r\n", response->body_length ? response->body_length : strlen(response->body));
    strcat(headers, header);

    for (int i = 0; i < response->num_headers; i++) {
        if (strcmp(response->headers[i].key, "Content-Encoding") == 0) {
            if (strcmp(response->headers[i].value, "gzip") == 0) {
                snprintf(header, MAX_HEADER_SIZE, "%s: %s\r\n", response->headers[i].key, response->headers[i].value);
                strcat(headers, header);
            }
        } else {
            snprintf(header, MAX_HEADER_SIZE, "%s: %s\r\n", response->headers[i].key, response->headers[i].value);
            strcat(headers, header);
        }
    }

    strcat(headers, "\r\n");
    return headers;
}

char *serialize_body(Response *response) {
    size_t body_length = response->body_length;
    char *body = (char *)malloc(body_length);
    memcpy(body, response->body, body_length);
    return body;
}

char *serialize_response(Response *response) {
    char *status_line = serialize_status_line(response);
    char *headers = serialize_headers(response);
    size_t body_length = response->body_length;

    size_t response_length = strlen(status_line) + strlen(headers);
    int gzip_encoding = 0;

    for (int i = 0; i < response->num_headers; i++) {
        if (strcmp(response->headers[i].key, "Content-Encoding") == 0 &&
            strcmp(response->headers[i].value, "gzip") == 0) {
            gzip_encoding = 1;
            break;
        }
    }

    if (gzip_encoding) {
        response_length += body_length;
    } else {
        response_length += strlen(response->body);
    }

    printf("%zu %zu %zu %zu", response_length, strlen(response->body), strlen(status_line), strlen(headers));

    char *response_string = (char *)malloc(response_length + 1);

    strcpy(response_string, status_line);
    strcat(response_string, headers);

    if (gzip_encoding) {
        memcpy(response_string + strlen(status_line) + strlen(headers), response->body, body_length);
    } else {
        strcat(response_string, response->body);
    }

    response_string[response_length] = '\0';

    free(status_line);
    free(headers);

    return response_string;
}

char *get_request_body(const char *request) {
    char *request_body = strstr(request, "\r\n\r\n");
    if (request_body != NULL) {
        request_body += strlen("\r\n\r\n");
    }
    return request_body;
}

void send_response(int client_fd, Response *response) {
    char *response_str = serialize_response(response);
    size_t response_length = strlen(response_str) + response->body_length;

    send(client_fd, response_str, response_length, 0);
    free(response_str);
}

void free_response(Response *response) {
    for (int i = 0; i < response->num_headers; i++) {
        free(response->headers[i].key);
        free(response->headers[i].value);
    }
    free(response->body);
    free(response);
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

Response *handle_root(const char *path, const char *request) {
    return create_response(HTTP_OK, "text/plain", "");
}

static char *gzip_deflate(const char *data, size_t data_len, size_t *gzip_len) {
    z_stream stream = {0};
    if (deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 31, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
        return NULL;
    }

    size_t max_len = deflateBound(&stream, data_len);
    char *gzip_data = malloc(max_len);
    if (gzip_data == NULL) {
        deflateEnd(&stream);
        return NULL;
    }

    stream.next_in = (Bytef *)data;
    stream.avail_in = data_len;
    stream.next_out = (Bytef *)gzip_data;
    stream.avail_out = max_len;

    if (deflate(&stream, Z_FINISH) != Z_STREAM_END) {
        free(gzip_data);
        deflateEnd(&stream);
        return NULL;
    }

    *gzip_len = stream.total_out;
    deflateEnd(&stream);
    return gzip_data;
}

Response *handle_echo(const char *path, const char *request) {
    char *custom_headers = get_header_value(request, "Accept-Encoding");
    Response *response = create_response(HTTP_OK, "text/plain", path + 6);

    if (custom_headers != NULL) {
        char *token, *last = NULL;
        int found_gzip = 0;

        token = strtok_r(custom_headers, ",", &last);
        while (token != NULL) {
            char *trimmed_token = strtok(token, " \t\r\n");

            if (strcmp(trimmed_token, "gzip") == 0) {
                found_gzip = 1;
                break;
            }
            token = strtok_r(NULL, ",", &last);
        }
        free(custom_headers);

        if (found_gzip) {
            size_t original_body_len = strlen(response->body);
            size_t compressed_body_len;
            char *compressed_body = gzip_deflate(response->body, original_body_len, &compressed_body_len);

            if (compressed_body != NULL) {
                free(response->body);
                response->body = compressed_body;
                response->body_length = compressed_body_len;
                set_header(response, "Content-Encoding", "gzip");
            }
        } else {
            response->body_length = strlen(response->body);
        }
    } else {
        response->body_length = strlen(response->body);
    }

    return response;
}

Response *handle_user_agent(const char *path, const char *request) {
    char *user_agent = get_header_value(request, "User-Agent");
    Response *response = create_response(HTTP_OK, "text/plain", user_agent ? user_agent : "");
    free(user_agent);
    return response;
}

Response *handle_file_get(const char *path, const char *request) {
    char *filename = strstr(path, "/files/");
    if (filename == NULL) {
        return create_response(HTTP_NOT_FOUND, "text/plain", "");
    }
    filename += strlen("/files/");
    char file_path[MAX_FILENAME_LENGTH];
    snprintf(file_path, sizeof(file_path), "%s/%s", dir_path, filename);
    struct stat buffer;
    if (stat(file_path, &buffer) == 0) {
        FILE *file = fopen(file_path, "r");
        if (file == NULL) {
            perror("Error opening file\n");
            return create_response(HTTP_INTERNAL_SERVER_ERROR, "text/plain", "");
        }
        char *file_content = malloc(buffer.st_size + 1);
        if (file_content == NULL) {
            fclose(file);
            return create_response(HTTP_INTERNAL_SERVER_ERROR, "text/plain", "");
        }
        size_t file_read = fread(file_content, 1, buffer.st_size, file);
        file_content[file_read] = '\0';
        fclose(file);
        Response *response = create_response(HTTP_OK, "application/octet-stream", file_content);
        free(file_content);
        return response;
    } else {
        return create_response(HTTP_NOT_FOUND, "text/plain", "");
    }
}

Response *handle_file_post(const char *path, const char *request) {
    char *filename = strstr(path, "/files/");
    if (filename == NULL) {
        return create_response(HTTP_NOT_FOUND, "text/plain", "");
    }
    filename += strlen("/files/");
    char file_path[MAX_FILENAME_LENGTH];
    snprintf(file_path, sizeof(file_path), "%s/%s", dir_path, filename);
    printf("%s\n", file_path);
    char *request_body = get_request_body(request);
    if (request_body == NULL) {
        return create_response(HTTP_BAD_REQUEST, "text/plain", "");
    }

    FILE *file = fopen(file_path, "w");
    if (file == NULL) {
        perror("Error creating file\n");
        return create_response(HTTP_INTERNAL_SERVER_ERROR, "text/plain", "");
    }
    fwrite(request_body, 1, strlen(request_body), file);
    fclose(file);
    return create_response(HTTP_CREATED, "text/plain", "");
}

Route routes[] = {
    {HTTP_GET, "/echo/", PARTIAL_MATCH, handle_echo},
    {HTTP_GET, "/user-agent", EXACT_MATCH, handle_user_agent},
    {HTTP_GET, "/files/", PARTIAL_MATCH, handle_file_get},
    {HTTP_POST, "/files/", PARTIAL_MATCH, handle_file_post},
    {HTTP_GET, "/", EXACT_MATCH, handle_root},
};

Response *handle_request(const char *request) {
    char method_str[MAX_BUFFER_SIZE], path[MAX_BUFFER_SIZE], version[MAX_BUFFER_SIZE];
    sscanf(request, "%s %s %s", method_str, path, version);

    HttpMethod method = HTTP_GET;
    if (strcmp(method_str, "POST") == 0) {
        method = HTTP_POST;
    }
    for (int i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
        Route r = routes[i];
        if (routes[i].match_type == EXACT_MATCH) {
            if (strcmp(path, routes[i].path) == 0 && routes[i].method == method) {
                return routes[i].handler(path, request);
            }
        } else if (routes[i].match_type == PARTIAL_MATCH) {
            size_t route_path_len = strlen(routes[i].path);
            if (strncmp(path, routes[i].path, route_path_len) == 0 && routes[i].method == method) {
                return routes[i].handler(path, request);
            }
        }
    }

    return create_response(HTTP_NOT_FOUND, "text/plain", "");
}

void *handle_client(void *arg) {
    int client_fd = *(int *)arg;
    free(arg);

    char buffer[MAX_BUFFER_SIZE];
    ssize_t bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);
    if (bytes_read <= 0) {
        close(client_fd);
        return NULL;
    }
    buffer[bytes_read] = '\0';

    Response *response = handle_request(buffer);
    send_response(client_fd, response);
    free_response(response);

    close(client_fd);
    return NULL;
}

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

int main(int argc, char **argv) {
    setbuf(stdout, NULL);
    printf("Logs from your program will appear here!\n");

    int opt;
    struct option long_options[] = {
        {"directory", required_argument, NULL, 'd'},
        {NULL, 0, NULL, 0}};
    while ((opt = getopt_long(argc, argv, "d:", long_options, NULL)) != -1) {
        switch (opt) {
        case 'd':
            dir_path = optarg;
            break;
        }
    }

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
        if (pthread_create(&thread, NULL, handle_client, arg) != 0) {
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

#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

struct server_opt;

struct server_opt *parse_command_line(int argc, char **argv);
int create_server_socket(struct server_opt *s);
void *handle_client(int arg);

#endif

#include "../include/error.h"
#include <stdio.h>
#include <stdlib.h>

void handle_error(char *message) {
    perror(message);
    exit(EXIT_FAILURE);
}

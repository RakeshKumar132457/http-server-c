# Simple HTTP Server

This is a simple HTTP server implemented in C. It supports handling GET and POST requests, serving static files, and performing basic routing based on the requested path.

## Features

-   Handles GET and POST requests
-   Serves static files from a specified directory
-   Supports routing based on the requested path
-   Echoes back the requested path for the `/echo` endpoint
-   Returns the user agent string for the `/user-agent` endpoint
-   Supports concurrent request handling using threads

## Getting Started

### Prerequisites

-   C compiler (e.g., GCC)
-   POSIX-compliant operating system (e.g., Linux, macOS)

### Building the Server

1. Clone the repository:
   `git clone https://github.com/RakeshKumar132457/http-server-c`
2. Navigate to the project directory:
   `cd http-server-c`

### Running the Server

Run the bash file to start the server:
`./your_server.sh`
You can also run it with `--directory /path/to/dir` for creating and reading files.
The server will start listening on port 4221 by default.

## Usage

Once the server is running, you can send HTTP requests to it using tools like `curl` or a web browser.

### Endpoints

-   `/`: Returns a plain text response with an empty body.
-   `/echo/<message>`: Echoes back the `<message>` in the response body.
-   `/user-agent`: Returns the user agent string of the client making the request.
-   `/files/<filename>`: Serves the specified file from the directory provided during server startup.

### Examples

-   Get the user agent string:
    curl http://localhost:4221/user-agent
-   Echo a message:
    curl http://localhost:4221/echo/Hello,%20World!
-   Serve a static file:
    curl http://localhost:4221/files/example.txt

## Contributing

Contributions are welcome! If you find any issues or have suggestions for improvements, please open an issue or submit a pull request.

## License

This project is licensed under the [MIT License](LICENSE).

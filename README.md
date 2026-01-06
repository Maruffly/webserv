<p align="center">
    <img src="https://img.shields.io/badge/Score-125%2F100-success?style=for-the-badge&logo=42" alt="Score 125/100">
    <img src="https://img.shields.io/badge/Language-C++-blue?style=for-the-badge&logo=c%2B%2B" alt="Language C++">
    <img src="https://img.shields.io/badge/Platform-Linux-orange?style=for-the-badge&logo=linux" alt="Platform Linux">
</p>

# 🌐 Webserv - Nginx-like HTTP Server

**Webserv** is a 42 curriculum project where we developed a high-performance HTTP/1.1 server from scratch in C++98. Inspired by Nginx, it is designed to be non-blocking and capable of handling multiple simultaneous client connections using I/O multiplexing.

## 🏗️ Technical Architecture: The Epoll Event Loop

For this project, I implemented a non-blocking architecture based on the **Linux epoll** API. This allows the server to scale efficiently without the overhead of creating one thread per connection.

### Why Epoll?
Unlike `select()` or `poll()`, which have $O(n)$ complexity, **epoll** scales at $O(1)$ regarding the number of monitored file descriptors.
* **Event-Driven**: The server sleeps until the kernel notifies it of specific events (incoming data, new connections, or readiness to write).
* **Edge-Triggered efficiency**: We process all available data for a descriptor in one go, minimizing system calls.
* **Non-blocking CGI**: Scripts are executed in separate processes, and their output is read via non-blocking pipes integrated into the main loop, ensuring a slow script never freezes the server.

---

## 🌟 Features

### Core Logic
* **HTTP/1.1 Compliance**: Support for `GET`, `POST`, and `DELETE` methods.
* **I/O Multiplexing**: Full non-blocking server using a single `epoll` instance.
* **Nginx-style Configuration**: Advanced parsing of a `.conf` file to define multiple servers, ports, and routes.
* **Static File Serving**: Efficiently serves HTML, CSS, images, and videos with proper MIME types.
* **Custom Error Pages**: Ability to define specific HTML files for any HTTP error code.

### Advanced Functionalities
* **CGI Implementation**: Supports Python and PHP scripts through environment variable passing and pipe management.
* **File Uploads**: Native support for multipart/form-data and binary uploads via the `upload_store` directive.
* **Directory Listing**: Automatic generation of an "Autoindex" page for directories.
* **Redirections**: Support for `return` directives (301/302 redirects).
* **Body Size Limitation**: `client_max_body_size` enforcement to prevent server abuse.

---

## ⚙️ Configuration File

The server behavior is controlled by a configuration file. Here is an example of what is supported:

```nginx
server {
    listen        8080;
    server_name   localhost;
    root          /tmp/webserv/www/html;
    index         index.html;

    location /cgi-bin/ {
        cgi_pass .py /usr/bin/python3;
        cgi_pass .php /usr/bin/php-cgi;
        autoindex on;
    }

    location /uploads/ {
        limit_except GET POST DELETE;
        upload_store /tmp/webserv/www/html/uploads;
        upload_create_dirs on;
    }
}
```
## Usage

### 1. Compilation
The project compiles only Linux using make. sys/epoll.h library is not supported on macOS system so it wont compile.
```bash
make
```
### 2. Launching the Server
```
./webserv <path/to/your_config.conf>
```
### 3. Testing with Siege
An exemple to verify the stability and non-blocking nature of the server:
```
siege -b -t 1M http://localhost:8080
```



#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include "message_handler.h" // <-- THÊM MỚI
#include <errno.h>
#include <fcntl.h> // Cho non-blocking
#include "db_handler.h"
#include "user_manager.h"
#include "server.h" // File .h ta vừa tạo
#include "friend_manager.h" // <-- ADD: declare friend-related handlers

#define PORT 8888
#define MAX_EVENTS 10

// ----- Quản lý Session Toàn cục -----
ClientSession sessions[MAX_CLIENTS];
sqlite3 *db; // DB toàn cục
int epoll_fd; // epoll FD toàn cục

void init_sessions() {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        sessions[i].fd = -1; // -1 = slot trống
        sessions[i].buffer_len = 0;
        memset(sessions[i].username, 0, MAX_USERNAME);
    }
}

ClientSession* get_session(int fd) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (sessions[i].fd == fd) return &sessions[i];
    }
    return NULL;
}

void add_session(int fd) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (sessions[i].fd == -1) {
            sessions[i].fd = fd;
            sessions[i].buffer_len = 0;
            printf("New session added for fd %d\n", fd);
            return;
        }
    }
    printf("Cannot add session: server is full.\n");
    close(fd);
}

// Hàm này sẽ gửi danh sách online cho mọi người (tạm thời)
// Ngày 5 sẽ sửa lại chỉ gửi cho bạn bè
void broadcast_online_list(ClientSession* sessions) {
    printf("Broadcasting online list...\n");
    ChatPacket packet;
    memset(&packet, 0, sizeof(ChatPacket));
    packet.type = MSG_TYPE_ONLINE_LIST_UPDATE;

    // Xây dựng nội dung (body) là danh sách user, cách nhau bằng dấu phẩy
    int offset = 0;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (sessions[i].fd != -1 && sessions[i].username[0] != '\0') {
            int len = snprintf(packet.body + offset, MAX_BODY - offset, "%s,", sessions[i].username);
            if (offset + len >= MAX_BODY) break;
            offset += len;
        }
    }
    // Gửi cho tất cả mọi người đang online
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (sessions[i].fd != -1 && sessions[i].username[0] != '\0') {
            write(sessions[i].fd, &packet, sizeof(ChatPacket));
        }
    }
}

void remove_session(int fd) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (sessions[i].fd == fd) {
            printf("Session removed for fd %d (user: %s)\n", fd, sessions[i].username);
            close(sessions[i].fd);
            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL); 
            sessions[i].fd = -1; 
            sessions[i].buffer_len = 0;
            memset(sessions[i].username, 0, MAX_USERNAME);

            // THÊM MỚI: Thông báo cho mọi người user này đã offline
            broadcast_online_list(sessions); 
            return;
        }
    }
}
// ----- Hết Quản lý Session -----

// Hàm set non-blocking cho socket
void set_non_blocking(int fd) {
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
}

// Xử lý gói tin (Dispatcher)
void process_packet(int client_fd, ChatPacket* packet) {
    ClientSession* session = get_session(client_fd);
    if (!session) return;

    // Gán source_user cho các packet gửi từ client đã login
    if (packet->type != MSG_TYPE_REGISTER_REQUEST && packet->type != MSG_TYPE_LOGIN_REQUEST) {
        strncpy(packet->source_user, session->username, MAX_USERNAME);
    }

    switch (packet->type) {
        case MSG_TYPE_REGISTER_REQUEST:
            handle_register(client_fd, packet, db);
            break;
        case MSG_TYPE_LOGIN_REQUEST:
            // Sửa lại: handle_login bây giờ là void và tự xử lý gửi packet
            handle_login(client_fd, packet, db, sessions); 
            break;
        case MSG_TYPE_LOGOUT_REQUEST: // <-- THÊM CASE MỚI
            printf("User '%s' logging out.\n", session->username);
            remove_session(client_fd);
            break;
        case MSG_TYPE_PRIVATE_MESSAGE: // <-- THÊM CASE MỚI
            handle_private_message(packet, sessions, db);
            break;
        case MSG_TYPE_FRIEND_REQUEST:
            handle_friend_request(client_fd, packet, sessions, db);
            break;

        case MSG_TYPE_FRIEND_ACCEPT:
            handle_friend_accept(client_fd, packet, sessions, db);
            break;

        case MSG_TYPE_FRIEND_DECLINE:
            handle_friend_decline(client_fd, packet, sessions, db);
            break;

        case MSG_TYPE_FRIEND_UNFRIEND:
            handle_friend_unfriend(client_fd, packet, sessions, db);
            break;

        case MSG_TYPE_FRIEND_LIST_REQUEST:
            // session should be the current client's session; adjust name if different
            handle_friend_list_request(client_fd, session->username, sessions, db);
            break;

        default:
            printf("Received unknown packet type from fd %d\n", client_fd);
    }
}

// Xử lý dữ liệu từ client (Stream Handling) - NÂNG CẤP
void handle_client_data(int client_fd) {
    ClientSession* session = get_session(client_fd);
    if (!session) return;

    while (1) { // Đọc liên tục cho đến khi EAGAIN (với EPOLLET)
        int bytes_to_read = sizeof(ChatPacket) - session->buffer_len;
        if (bytes_to_read <= 0) break; // Buffer đã đầy? (lỗi)

        ssize_t bytes_read = read(client_fd, session->read_buffer + session->buffer_len, bytes_to_read);

        if (bytes_read == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Không còn dữ liệu để đọc
                break; 
            }
            // Lỗi thật
            perror("read() failed");
            remove_session(client_fd);
            return;
        }

        if (bytes_read == 0) { // Client ngắt kết nối
            printf("Client fd %d disconnected.\n", client_fd);
            remove_session(client_fd);
            return;
        }

        session->buffer_len += bytes_read;

        // Xử lý tất cả các gói tin có trong buffer
        while (session->buffer_len >= sizeof(ChatPacket)) {
            process_packet(client_fd, (ChatPacket*)session->read_buffer);

            // Di chuyển phần dữ liệu còn lại (nếu có) lên đầu buffer
            int remaining = session->buffer_len - sizeof(ChatPacket);
            if (remaining > 0) {
                memmove(session->read_buffer, session->read_buffer + sizeof(ChatPacket), remaining);
            }
            session->buffer_len = remaining;
        }
    }
}

// Xử lý kết nối mới
void handle_new_connection(int listener_fd) {
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    int client_fd = accept(listener_fd, (struct sockaddr*)&client_addr, &client_len);
    
    if (client_fd == -1) {
        perror("accept() failed");
        return;
    }

    printf("New connection accepted: fd %d\n", client_fd);
    set_non_blocking(client_fd); // Rất quan trọng cho epoll
    add_session(client_fd);

    // Thêm client socket mới vào epoll
    struct epoll_event event;
    event.events = EPOLLIN | EPOLLET; // Đọc (IN) và Edge-Triggered (ET)
    event.data.fd = client_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &event) == -1) {
        perror("epoll_ctl ADD client failed");
        close(client_fd);
    }
}

// Hàm main
int main() {
    int listener_fd;
    struct sockaddr_in server_addr;
    struct epoll_event event, events[MAX_EVENTS];

    init_sessions();

    if (db_open("server/chat.db", &db) != 0) return 1;
    
    listener_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listener_fd == -1) { perror("socket() failed"); return 1; }

    // Allow quick reuse of address/port to avoid "Address already in use" on restart
    {
        int opt = 1;
        if (setsockopt(listener_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
            perror("setsockopt(SO_REUSEADDR) failed");
        }
#ifdef SO_REUSEPORT
        if (setsockopt(listener_fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt)) == -1) {
            // not fatal; continue
        }
#endif
    }
    
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if (bind(listener_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        perror("bind() failed"); close(listener_fd); return 1;
    }

    if (listen(listener_fd, 50) == -1) {
        perror("listen() failed"); close(listener_fd); return 1;
    }

    epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) { perror("epoll_create1() failed"); close(listener_fd); return 1; }

    event.events = EPOLLIN; // Sự kiện đọc
    event.data.fd = listener_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listener_fd, &event) == -1) {
        perror("epoll_ctl ADD listener failed");
        close(listener_fd); close(epoll_fd); return 1;
    }

    printf("Server is listening on port %d\n", PORT);

    // ----- Vòng lặp Server Chính -----
    while (1) {
        int num_events = epoll_wait(epoll_fd, events, MAX_EVENTS, -1); // Chờ vô hạn
        if (num_events == -1) {
            perror("epoll_wait() failed");
            break;
        }

        // Xử lý từng sự kiện
        for (int i = 0; i < num_events; i++) {
            if (events[i].data.fd == listener_fd) {
                // Có kết nối mới
                handle_new_connection(listener_fd);
            } else {
                // Có dữ liệu từ client
                handle_client_data(events[i].data.fd);
            }
        }
    }

    close(listener_fd);
    close(epoll_fd);
    db_close(db);
    return 0;
}
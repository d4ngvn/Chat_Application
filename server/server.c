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
#include "group_manager.h"  // <-- NEW: may contain group helpers

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

// Helper: find session by username (local copy to avoid cross-file dependency)
static ClientSession* find_session_by_username_local(const char* username) {
    if (!username) return NULL;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (sessions[i].fd != -1 && sessions[i].username[0] != '\0' && strcmp(sessions[i].username, username) == 0) {
            return &sessions[i];
        }
    }
    return NULL;
}

// Context passed when notifying members of a specific group
typedef struct {
    const char* user;   // user who went offline
    const char* group;  // group name for this callback
} MemberNotifyCtx;

// db_group_member_callback signature: void (*cb)(void* arg, const char* member_name);
static void member_notify_cb(void* arg, const char* member_name) {
    MemberNotifyCtx* mc = (MemberNotifyCtx*)arg;
    if (!mc || !member_name) return;
    if (strcmp(member_name, mc->user) == 0) return;

    ClientSession* s = find_session_by_username_local(member_name);
    if (s && s->fd != -1) {
        ChatPacket pkt;
        memset(&pkt, 0, sizeof(pkt));
        pkt.type = MSG_TYPE_RECEIVE_GROUP_MESSAGE;
        strncpy(pkt.source_user, mc->user, MAX_USERNAME - 1);
        strncpy(pkt.target_user, mc->group, MAX_USERNAME - 1);
        snprintf(pkt.body, MAX_BODY, "%s went offline.", mc->user);
        write(s->fd, &pkt, sizeof(ChatPacket));
    }
}

// db_group_list_callback signature: int (*cb)(void* arg, const char* group_name);
static int group_list_cb(void* arg, const char* group_name) {
    const char* user = (const char*)arg;
    if (!user || !group_name) return 0;

    MemberNotifyCtx mc;
    mc.user = user;
    mc.group = group_name;

    // For this group, notify each member (except user)
    db_get_group_members(db, group_name, member_notify_cb, &mc);
    return 0;
}

// Helper: notify all group members (except 'user') that 'user' went offline.
// Uses db_get_groups_for_user -> group_list_cb
static void notify_user_offline_in_groups(const char* user) {
    if (!user || !db) return;
    db_get_groups_for_user(db, user, group_list_cb, (void*)user);
}

void remove_session(int fd) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (sessions[i].fd == fd) {
            printf("Session removed for fd %d (user: %s)\n", fd, sessions[i].username);
            // Notify friends that this user is going offline BEFORE clearing username
            if (sessions[i].username[0] != '\0') {
                // broadcast status to friends
                broadcast_status_to_friends(sessions[i].username, sessions, db, 0); // 0 = offline

                // Notify group members that this user went offline
                notify_user_offline_in_groups(sessions[i].username);
            }

            close(sessions[i].fd);
            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL); 
            sessions[i].fd = -1; 
            sessions[i].buffer_len = 0;
            memset(sessions[i].username, 0, MAX_USERNAME);

            // THÊM MỚI: Thông báo cho mọi người user này đã offline (online list update)
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

        case MSG_TYPE_GROUP_MESSAGE:
            handle_group_message(packet, sessions, db);
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

        // --- Group ops ---
        case MSG_TYPE_CREATE_GROUP_REQUEST:
            handle_create_group(client_fd, packet, sessions, db);
            break;
        case MSG_TYPE_JOIN_GROUP_REQUEST:
            handle_join_group_request(client_fd, packet, sessions, db);
            break;
        case MSG_TYPE_INVITE_TO_GROUP_REQUEST:
            handle_invite_to_group(client_fd, packet, sessions, db);
            break;
        case MSG_TYPE_REMOVE_FROM_GROUP_REQUEST:
            handle_remove_from_group(client_fd, packet, sessions, db);
            break;
        case MSG_TYPE_LEAVE_GROUP_REQUEST:
            handle_leave_group(client_fd, packet, sessions, db);
            break;

        // NEW: group listing requests
        case MSG_TYPE_GROUP_LIST_JOINED_REQUEST:
            handle_group_list_joined(client_fd, packet, sessions, db);
            break;
        case MSG_TYPE_GROUP_LIST_ALL_REQUEST:
            handle_group_list_all(client_fd, packet, sessions, db);
            break;

        default:
            printf("Received unknown packet type from fd %d\n", client_fd);
    }
}

// Xử lý dữ liệu từ client (Stream Handling) - NÂNG CẤP
void handle_client_data(int client_fd) {
    // We will re-query session each iteration because process_packet(...) may call remove_session()
    // which clears the session slot; using a stale pointer caused use-after-free and segfault.
    while (1) { // Đọc liên tục cho đến khi EAGAIN (với EPOLLET)
        ClientSession* session = get_session(client_fd);
        if (!session) return;
        int bytes_to_read = sizeof(ChatPacket) - session->buffer_len;
        if (bytes_to_read <= 0) return; // Buffer full or inconsistent; bail out

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

        session = get_session(client_fd);
        if (!session) return;
        session->buffer_len += bytes_read;

        // Xử lý tất cả các gói tin có trong buffer
        while (1) {
            session = get_session(client_fd);
            if (!session) return; // session may have been removed by process_packet
            if (session->buffer_len < (int)sizeof(ChatPacket)) break;

            process_packet(client_fd, (ChatPacket*)session->read_buffer);

            // If the session was removed during processing, stop immediately
            session = get_session(client_fd);
            if (!session) return;

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
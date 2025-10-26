#include "message_handler.h"
#include "db_handler.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

// Hàm này sẽ gửi danh sách online cho mọi người
extern void broadcast_online_list(ClientSession* sessions);

// Hàm callback để gửi gói tin đến client
void send_packet_callback(void* arg, ChatPacket* packet) {
    int client_fd = *(int*)arg;
    if (client_fd > 0) {
        write(client_fd, packet, sizeof(ChatPacket));
    }
}

// Hàm tìm kiếm 1 user đang online bằng username
// (Hàm này nên nằm trong server.c, nhưng để tạm ở đây cho nhanh)
ClientSession* find_session_by_username(const char* user, ClientSession* sessions) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (sessions[i].fd != -1 && strcmp(sessions[i].username, user) == 0) {
            return &sessions[i];
        }
    }
    return NULL; // Không tìm thấy (offline)
}

void handle_login(int client_fd, ChatPacket* packet, sqlite3 *db, ClientSession* sessions) {
    ClientSession* session = get_session(client_fd);
    if (!session) return;

    // Kiểm tra xem user đã đăng nhập ở session khác chưa
    if (find_session_by_username(packet->source_user, sessions) != NULL) {
        printf("Login failed: User '%s' is already logged in.\n", packet->source_user);
        ChatPacket fail_packet;
        memset(&fail_packet, 0, sizeof(ChatPacket));
        fail_packet.type = MSG_TYPE_LOGIN_FAIL;
        snprintf(fail_packet.body, MAX_BODY, "Login failed: User is already logged in elsewhere.");
        write(client_fd, &fail_packet, sizeof(ChatPacket));
        return;
    }

    // --- NEW: kiểm tra user có tồn tại trong DB trước ---
    if (!db_user_exists(db, packet->source_user)) {
        printf("Login failed: User '%s' not found.\n", packet->source_user);
        ChatPacket fail_packet;
        memset(&fail_packet, 0, sizeof(ChatPacket));
        fail_packet.type = MSG_TYPE_LOGIN_FAIL;
        snprintf(fail_packet.body, MAX_BODY, "Login failed: User not found.");
        write(client_fd, &fail_packet, sizeof(ChatPacket));
        return;
    }

    // Xác thực với DB
    if (db_authenticate_user(db, packet->source_user, packet->body)) {
        // --- ĐĂNG NHẬP THÀNH CÔNG ---
        printf("User '%s' logged in successfully from fd %d.\n", packet->source_user, client_fd);
        
        // Gán username cho session
        strncpy(session->username, packet->source_user, MAX_USERNAME);

        // Gửi gói tin thành công cho client
        ChatPacket success_packet;
        memset(&success_packet, 0, sizeof(ChatPacket));
        success_packet.type = MSG_TYPE_LOGIN_SUCCESS;
        strncpy(success_packet.source_user, packet->source_user, MAX_USERNAME);
        snprintf(success_packet.body, MAX_BODY, "Login successful! Welcome %s", packet->source_user);
        write(client_fd, &success_packet, sizeof(ChatPacket));
        
        // Gửi tin nhắn offline và broadcast danh sách online
        db_send_pending_messages(db, packet->source_user, send_packet_callback, &client_fd);
        broadcast_online_list(sessions);

    } else {
        // --- ĐĂNG NHẬP THẤT BẠI ---
        printf("Login failed for user '%s': Invalid credentials.\n", packet->source_user);
        ChatPacket fail_packet;
        memset(&fail_packet, 0, sizeof(ChatPacket));
        fail_packet.type = MSG_TYPE_LOGIN_FAIL;
        snprintf(fail_packet.body, MAX_BODY, "Login failed. Check username/password.");
        write(client_fd, &fail_packet, sizeof(ChatPacket));
    }
}

void handle_private_message(ChatPacket* packet, ClientSession* sessions, sqlite3 *db) {
    printf("Routing private message from '%s' to '%s'\n", packet->source_user, packet->target_user);

    ClientSession* target_session = find_session_by_username(packet->target_user, sessions);

    if (target_session != NULL) {
        // --- NGƯỜI NHẬN ĐANG ONLINE ---
        ChatPacket forward_packet;
        memset(&forward_packet, 0, sizeof(ChatPacket));
        
        forward_packet.type = MSG_TYPE_RECEIVE_PRIVATE;
        strncpy(forward_packet.source_user, packet->source_user, MAX_USERNAME); // Ai gửi
        strncpy(forward_packet.body, packet->body, MAX_BODY); // Nội dung

        // Gửi thẳng đến socket của người nhận
        write(target_session->fd, &forward_packet, sizeof(ChatPacket));
        printf("Message forwarded to fd %d\n", target_session->fd);

    } else {
        // --- NGƯỜI NHẬN ĐANG OFFLINE ---
        printf("User '%s' is offline. Storing message.\n", packet->target_user);
        db_store_offline_message(db, packet->source_user, packet->target_user, packet->body);
    }
}
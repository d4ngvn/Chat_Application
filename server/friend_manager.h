#ifndef FRIEND_MANAGER_H
#define FRIEND_MANAGER_H

// Ensure this header does not rely on server.h being included first.
// Forward-declare types used as pointers to avoid unknown-type errors.
typedef struct ClientSession ClientSession;
typedef struct sqlite3 sqlite3;

#include <sqlite3.h>
#include "../shared/protocol.h"
#include "server.h"

// Fix prototypes to match implementations in friend_manager.c
void handle_friend_request(int sender_fd, ChatPacket* packet, ClientSession* sessions, sqlite3 *db);
void handle_friend_accept(int accepter_fd, ChatPacket* packet, ClientSession* sessions, sqlite3 *db);
void handle_friend_decline(int decliner_fd, ChatPacket* packet, ClientSession* sessions, sqlite3 *db);
void handle_friend_unfriend(int user_fd, ChatPacket* packet, ClientSession* sessions, sqlite3 *db);

// Corrected prototype: include username parameter
void handle_friend_list_request(int user_fd, const char* username, ClientSession* sessions, sqlite3 *db);

// (Hàm quan trọng) Gửi danh sách status bạn bè
void send_friend_status_list(int user_fd, const char* username, ClientSession* sessions, sqlite3 *db);
// (Hàm quan trọng) Thông báo cho bạn bè
void broadcast_status_to_friends(const char* user, ClientSession* sessions, sqlite3 *db, int is_online);

// Provide NotifyArgs here so .c doesn't redeclare it
typedef struct {
    ClientSession* sessions;      // 1. Danh sách session
    const char* user_who_changed; // 2. Tên user
    const char* status_message;   // 3. Tin nhắn
} NotifyArgs;
#endif
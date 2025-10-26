#ifndef DB_HANDLER_H
#define DB_HANDLER_H
#include "server.h"
#include <sqlite3.h>
#include "../shared/protocol.h"

// Mở và đóng database
int db_open(const char* db_path, sqlite3** db);
void db_close(sqlite3* db);

// Xử lý đăng ký và xác thực
void handle_register(int client_fd, ChatPacket* packet, sqlite3* db);
int db_authenticate_user(sqlite3* db, const char* username, const char* password);
int db_register_user(sqlite3* db, const char* username, const char* password);
int db_login_user(sqlite3* db, const char* username, const char* password);
int db_user_exists(sqlite3* db, const char* username); // Kiểm tra sự tồn tại của người dùng

// Xử lý tin nhắn offline
int db_store_offline_message(sqlite3* db, const char* sender, const char* receiver, const char* message);
int db_send_pending_messages(sqlite3* db, const char* user, void (*callback)(void* arg, ChatPacket* packet), void* arg);

// friend 
int db_friend_request(sqlite3 *db, const char* sender, const char* receiver);
int db_friend_accept(sqlite3 *db, const char* accepter, const char* sender);
int db_friend_decline(sqlite3 *db, const char* decliner, const char* sender);
int db_friend_unfriend(sqlite3 *db, const char* user1, const char* user2);

// Hàm callback để xử lý từng dòng kết quả
typedef int (*db_friend_list_callback)(void* arg, const char* friend_name);
int db_get_friend_list(sqlite3 *db, const char* user, db_friend_list_callback callback, void* arg);

// --- NEW: Group DB APIs ---
int db_create_group(sqlite3 *db, const char* group_name, const char* owner);
int db_group_exists(sqlite3 *db, const char* group_name);
int db_add_group_member(sqlite3 *db, const char* group_name, const char* username);
int db_remove_group_member(sqlite3 *db, const char* group_name, const char* username);
int db_is_group_owner(sqlite3 *db, const char* group_name, const char* username);
/**
 * callback signature: void cb(void* arg, const char* member_name)
 */
typedef void (*db_group_member_callback)(void* arg, const char* member_name);
int db_get_group_members(sqlite3 *db, const char* group_name, db_group_member_callback callback, void* arg);

// NEW: list groups a user has joined / list all groups
typedef int (*db_group_list_callback)(void* arg, const char* group_name);
int db_get_groups_for_user(sqlite3 *db, const char* username, db_group_list_callback callback, void* arg);
int db_get_all_groups(sqlite3 *db, db_group_list_callback callback, void* arg);

// NEW: check membership
int db_is_group_member(sqlite3 *db, const char* group_name, const char* username);

#endif // DB_HANDLER_H
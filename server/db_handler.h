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
#endif // DB_HANDLER_H
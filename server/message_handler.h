#ifndef MESSAGE_HANDLER_H
#define MESSAGE_HANDLER_H

#include <sqlite3.h>
#include "../shared/protocol.h"
#include "server.h" // Để dùng ClientSession

/**
 * @brief Xử lý tin nhắn riêng tư.
 * Định tuyến tin nhắn đến user đích nếu online, hoặc lưu offline nếu không.
 */
void handle_private_message(ChatPacket* packet, ClientSession* sessions, sqlite3 *db);

/**
 * @brief Xử lý yêu cầu đăng nhập từ client.
 * Kiểm tra thông tin đăng nhập và thiết lập phiên làm việc nếu hợp lệ.
 */
void handle_login(int client_fd, ChatPacket* packet, sqlite3 *db, ClientSession* sessions);

/**
 * @brief Xử lý yêu cầu đăng ký từ client.
 * Lưu thông tin người dùng mới vào cơ sở dữ liệu.
 */
void handle_register(int client_fd, ChatPacket* packet, sqlite3* db);

#endif
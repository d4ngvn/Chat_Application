#ifndef SERVER_H
#define SERVER_H
#include "friend_manager.h"
#include "../shared/protocol.h"

#define MAX_CLIENTS 100

// Cấu trúc quản lý 1 client
typedef struct ClientSession {
    int fd;
    char username[MAX_USERNAME];
    
    // Buffer để xử lý stream (khi nhận được nửa gói tin)
    char read_buffer[sizeof(ChatPacket)];
    int buffer_len; 
} ClientSession;

// Hàm tìm session, sẽ được định nghĩa trong server.c
ClientSession* get_session(int fd);

#endif
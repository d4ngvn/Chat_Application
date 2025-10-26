#ifndef PROTOCOL_H
#define PROTOCOL_H

#define MAX_USERNAME 50
#define MAX_PASSWORD 50
#define MAX_BODY 512

// Định nghĩa các loại gói tin
typedef enum {
    // Client gửi
    MSG_TYPE_REGISTER_REQUEST,
    MSG_TYPE_LOGIN_REQUEST,
    MSG_TYPE_LOGOUT_REQUEST,
    MSG_TYPE_GROUP_MESSAGE, // <-- THÊM MỚI
    MSG_TYPE_PRIVATE_MESSAGE, // <-- Sửa/Thêm
    MSG_TYPE_FRIEND_REQUEST,
    MSG_TYPE_ACCEPT_FRIEND_REQUEST,
    MSG_TYPE_CREATE_GROUP_REQUEST,
    MSG_TYPE_JOIN_GROUP_REQUEST,
    MSG_TYPE_INVITE_TO_GROUP_REQUEST,

    // Server gửi
    MSG_TYPE_REGISTER_SUCCESS,
    MSG_TYPE_REGISTER_FAIL,
    MSG_TYPE_LOGIN_SUCCESS,
    MSG_TYPE_LOGIN_FAIL,
    MSG_TYPE_RECEIVE_PRIVATE, 
    MSG_TYPE_RECEIVE_GROUP_MESSAGE,// <-- Sửa/Thêm
    MSG_TYPE_ONLINE_LIST_UPDATE, // <-- THÊM MỚI
    MSG_TYPE_SEND_OFFLINE_MSG, // <-- THÊM MỚI
    MSG_TYPE_FRIEND_REQUEST_RESPONSE,
    MSG_TYPE_GROUP_RESPONSE,
    // ...
} MessageType;

// Cấu trúc của 1 gói tin
typedef struct {
    MessageType type;
    char source_user[MAX_USERNAME];
    char target_user[MAX_USERNAME];
    char target_group[MAX_USERNAME];
    char body[MAX_BODY];
} ChatPacket;

#endif
#include "friend_manager.h"
#include "db_handler.h"
#include "server.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

// --- Hàm Helper ---

/**
 * @brief Gửi một packet đã được định dạng đơn giản đến một file descriptor.
 * Dùng để gửi thông báo, lỗi, hoặc cập nhật.
 */
void send_packet_to_fd(int fd, MessageType type, const char* body, const char* source) {
    if (fd <= 0) return; // Không gửi đến fd không hợp lệ
    
    ChatPacket packet;
    memset(&packet, 0, sizeof(ChatPacket));
    packet.type = type;
    if (body) strncpy(packet.body, body, MAX_BODY);
    if (source) strncpy(packet.source_user, source, MAX_USERNAME);
    
    write(fd, &packet, sizeof(ChatPacket));
}

/**
 * @brief Tìm kiếm session của user dựa trên username.
 * (Hàm này PHẢI được định nghĩa trong server.c và khai báo trong server.h)
 */
extern ClientSession* find_session_by_username(const char* user, ClientSession* sessions);

// --- Logic Bạn bè Chính ---

/**
 * @brief Xử lý khi user (sender) gửi lời mời kết bạn cho (receiver).
 * Gửi: MSG_TYPE_FRIEND_REQUEST
 */
void handle_friend_request(int sender_fd, ChatPacket* packet, ClientSession* sessions, sqlite3 *db) {
    const char* sender = packet->source_user;
    const char* receiver = packet->target_user;

    if (strcmp(sender, receiver) == 0) {
        send_packet_to_fd(sender_fd, MSG_TYPE_FRIEND_UPDATE, "You cannot add yourself.", "Server");
        return;
    }

    // New: ensure receiver exists
    if (!db_user_exists(db, receiver)) {
        send_packet_to_fd(sender_fd, MSG_TYPE_FRIEND_UPDATE, "User not found.", "Server");
        return;
    }

    if (db_friend_request(db, sender, receiver) == 0) {
        send_packet_to_fd(sender_fd, MSG_TYPE_FRIEND_UPDATE, "Friend request sent.", "Server");
        
        ClientSession* receiver_session = find_session_by_username(receiver, sessions);
        if (receiver_session) {
            // Notify receiver with source_user = sender so client UI can show "/accept <sender>"
            send_packet_to_fd(receiver_session->fd, MSG_TYPE_FRIEND_REQUEST_INCOMING,
                              "You have a new friend request.", sender);
        }
    } else {
        send_packet_to_fd(sender_fd, MSG_TYPE_FRIEND_UPDATE, "Failed to send request (already sent or already friends?).", "Server");
    }
}

/**
 * @brief Xử lý khi user (accepter) chấp nhận lời mời từ (sender).
 * Gửi: MSG_TYPE_FRIEND_ACCEPT
 */
void handle_friend_accept(int accepter_fd, ChatPacket* packet, ClientSession* sessions, sqlite3 *db) {
    const char* accepter = packet->source_user;
    const char* sender = packet->target_user; // Người đã gửi request

    // Ensure sender exists
    if (!db_user_exists(db, sender)) {
        send_packet_to_fd(accepter_fd, MSG_TYPE_FRIEND_UPDATE, "User not found.", "Server");
        return;
    }

    if (db_friend_accept(db, accepter, sender) == 0) {
        char body[MAX_BODY];
        snprintf(body, MAX_BODY, "You are now friends with %s.", sender);
        send_packet_to_fd(accepter_fd, MSG_TYPE_FRIEND_UPDATE, body, "Server");
        
        ClientSession* sender_session = find_session_by_username(sender, sessions);
        if (sender_session) {
            snprintf(body, MAX_BODY, "%s accepted your friend request.", accepter);
            send_packet_to_fd(sender_session->fd, MSG_TYPE_FRIEND_UPDATE, body, "Server");
        }
        
        handle_friend_list_request(accepter_fd, accepter, sessions, db);
        if (sender_session) {
            handle_friend_list_request(sender_session->fd, sender, sessions, db);
        }
    } else {
        send_packet_to_fd(accepter_fd, MSG_TYPE_FRIEND_UPDATE, "Failed to accept request (request not found?).", "Server");
    }
}

/**
 * @brief Xử lý khi user (decliner) từ chối lời mời từ (sender).
 * Gửi: MSG_TYPE_FRIEND_DECLINE
 */
void handle_friend_decline(int decliner_fd, ChatPacket* packet, ClientSession* sessions, sqlite3 *db) {
    const char* decliner = packet->source_user;
    const char* sender = packet->target_user; // Người đã gửi request

    // Ensure sender exists
    if (!db_user_exists(db, sender)) {
        send_packet_to_fd(decliner_fd, MSG_TYPE_FRIEND_UPDATE, "User not found.", "Server");
        return;
    }

    if (db_friend_decline(db, decliner, sender) == 0) {
        char body[MAX_BODY];
        snprintf(body, MAX_BODY, "You declined the request from %s.", sender);
        send_packet_to_fd(decliner_fd, MSG_TYPE_FRIEND_UPDATE, body, "Server");
    } else {
        send_packet_to_fd(decliner_fd, MSG_TYPE_FRIEND_UPDATE, "Failed to decline request (request not found?).", "Server");
    }
}

/**
 * @brief Xử lý khi user (unfriender) hủy kết bạn với (target).
 * Gửi: MSG_TYPE_FRIEND_UNFRIEND
 */
void handle_friend_unfriend(int unfriender_fd, ChatPacket* packet, ClientSession* sessions, sqlite3 *db) {
    const char* unfriender = packet->source_user;
    const char* target = packet->target_user;

    // Ensure target exists
    if (!db_user_exists(db, target)) {
        send_packet_to_fd(unfriender_fd, MSG_TYPE_FRIEND_UPDATE, "User not found.", "Server");
        return;
    }

    if (db_friend_unfriend(db, unfriender, target) == 0) {
        char body[MAX_BODY];
        snprintf(body, MAX_BODY, "You are no longer friends with %s.", target);
        send_packet_to_fd(unfriender_fd, MSG_TYPE_FRIEND_UPDATE, body, "Server");
        
        ClientSession* target_session = find_session_by_username(target, sessions);
        if (target_session) {
            snprintf(body, MAX_BODY, "%s has unfriended you.", unfriender);
            send_packet_to_fd(target_session->fd, MSG_TYPE_FRIEND_UPDATE, body, "Server");
        }
        
        handle_friend_list_request(unfriender_fd, unfriender, sessions, db);
        if (target_session) {
            handle_friend_list_request(target_session->fd, target, sessions, db);
        }
    } else {
        // Provide clearer feedback on failure
        send_packet_to_fd(unfriender_fd, MSG_TYPE_FRIEND_UPDATE, "Failed to unfriend (not friends or DB error).", "Server");
    }
}


// --- Logic Lấy Danh sách Bạn bè + Status ---

// Cấu trúc để build chuỗi
typedef struct {
    char list_str[MAX_BODY];
    ClientSession* sessions; // Cần để kiểm tra status online
} FriendListBuilder;

/**
 * @brief Callback được gọi bởi db_get_friend_list cho mỗi người bạn.
 * Nó sẽ build chuỗi friend list KÈM STATUS (ONL/OFF).
 */
int build_friend_list_callback(void* arg, const char* friend_name) {
    FriendListBuilder* builder = (FriendListBuilder*)arg;
    
    // Kiểm tra status online
    ClientSession* friend_session = find_session_by_username(friend_name, builder->sessions);

    const char* status = (friend_session) ? "(ONL)" : "(OFF)";
    
    char entry[MAX_USERNAME + 10];
    snprintf(entry, sizeof(entry), "%s %s, ", friend_name, status);
    
    // Nối vào chuỗi kết quả, chừa 1 byte cho NULL
    if (strlen(builder->list_str) + strlen(entry) < MAX_BODY - 1) {
        strcat(builder->list_str, entry);
    }
    return 0; // Tiếp tục
}

/**
 * @brief Xử lý khi user yêu cầu danh sách bạn.
 * Gửi: MSG_TYPE_FRIEND_LIST_REQUEST
 */
void handle_friend_list_request(int user_fd, const char* username, ClientSession* sessions, sqlite3 *db) {
    FriendListBuilder builder;
    memset(&builder.list_str, 0, MAX_BODY);
    builder.sessions = sessions;

    // 1. Gọi DB, DB sẽ gọi callback `build_friend_list_callback` cho mỗi người bạn
    db_get_friend_list(db, username, build_friend_list_callback, &builder);

    char response_body[MAX_BODY];
    if (strlen(builder.list_str) > 0) {
        // Xóa dấu phẩy và khoảng trắng cuối cùng
        builder.list_str[strlen(builder.list_str) - 2] = '\0'; 
        snprintf(response_body, MAX_BODY, "Your friends: %s", builder.list_str);
    } else {
        strcpy(response_body, "You have no friends yet.");
    }
    
    // 2. Gửi list (đã kèm status) về cho client
    send_packet_to_fd(user_fd, MSG_TYPE_FRIEND_LIST_RESPONSE, response_body, "Server");
}


// --- Logic Thông báo Status (Online/Offline) ---

/**
 * @brief Callback được gọi bởi db_get_friend_list.
 * Chỉ dùng để thông báo cho từng người bạn.
 */
int notify_friend_callback(void* arg, const char* friend_name) {
    NotifyArgs* args = (NotifyArgs*)arg;
    
    ClientSession* friend_session = find_session_by_username(friend_name, args->sessions);
    
    // Nếu người bạn đó online, gửi thông báo
    if (friend_session) {
        send_packet_to_fd(friend_session->fd, MSG_TYPE_FRIEND_UPDATE, args->status_message, args->user_who_changed);
    }
    return 0; // Tiếp tục
}

/**
 * @brief Gửi thông báo cho TẤT CẢ bạn bè của 'user' rằng họ vừa online/offline.
 */
void broadcast_status_to_friends(const char* user, ClientSession* sessions, sqlite3 *db, int is_online) {
    NotifyArgs args;
    args.sessions = sessions;
    args.user_who_changed = user;
    args.status_message = is_online ? "is now online." : "is now offline.";
    
    // Gọi DB, DB sẽ gọi `notify_friend_callback` cho mỗi người bạn
    db_get_friend_list(db, user, notify_friend_callback, &args);
}
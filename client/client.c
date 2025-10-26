#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <signal.h>
#include "../shared/protocol.h"
#include "ui.h" // UI mới (đã có ClientState)
#include <ctype.h> // <-- ADDED: isspace()

#define SERVER_IP "127.0.0.1"
#define PORT 8888
#define INPUT_MAX 500

// Ensure MAX_PASSWORD is defined for the client UI password buffer
#ifndef MAX_PASSWORD
#define MAX_PASSWORD 128
#endif

// Provide a global server socket FD used by send_packet
int server_sock_fd = -1;

// Remove extern declaration and add a real implementation for send_packet
int send_packet(ChatPacket* packet) {
    if (!packet) return -1;
    if (server_sock_fd <= 0) return -1;
    ssize_t w = write(server_sock_fd, packet, sizeof(ChatPacket));
    return (w == sizeof(ChatPacket)) ? 0 : -1;
}

// --- Biến Trạng thái Toàn cục ---
ClientState current_state = STATE_PRE_LOGIN;
char current_user[MAX_USERNAME]; // Lưu tên user
volatile sig_atomic_t g_resized = 0; // Biến cờ (flag) báo hiệu

// (THÊM) Flag xác thực thực sự (server confirmed)
int is_authenticated = 0;

// (THÊM LẠI) Theo dõi đăng nhập đang chờ
char pending_login[MAX_USERNAME];
int pending_login_active = 0;

// (THÊM MỚI) Biến Ngữ cảnh Chat
typedef enum {
    CHAT_TYPE_NONE,
    CHAT_TYPE_PRIVATE,
    CHAT_TYPE_GROUP
} ChatContextType;

ChatContextType current_chat_type = CHAT_TYPE_NONE;
char current_chat_target[MAX_USERNAME]; // Sẽ lưu tên user hoặc tên group

// Khai báo hàm
int connect_to_server();
void handle_server_message(int sock_fd);
void handle_keyboard_input(int sock_fd);
void do_login_flow(int sock_fd);
void do_register_flow(int sock_fd);

// New: interactive friend flows (prompt-based)
void do_add_friend_flow(int sock_fd);
void do_accept_friend_flow(int sock_fd);
void do_decline_friend_flow(int sock_fd);
void do_unfriend_flow(int sock_fd);

// New: interactive group flows
void do_create_group_flow(int sock_fd);
void do_join_group_flow(int sock_fd, const char* groupname_inline);
void do_invite_group_flow(int sock_fd);
void do_remove_group_flow(int sock_fd);
void do_leave_group_flow(int sock_fd);

// NEW: Prompt-based /msg handler (declare before use)
void do_msg_prompt_flow(int sock_fd);

// (HÀM XỬ LÝ RESIZE TÍN HIỆU)
void handle_resize(int sig) {
    (void)sig; // Tắt cảnh báo unused parameter
    g_resized = 1; // Chỉ set cờ, không làm gì nặng
}

int main() {
    signal(SIGWINCH, handle_resize);

    int sock_fd = connect_to_server();
    if (sock_fd == -1) return 1;

    // Set global socket for send_packet
    server_sock_fd = sock_fd;

    ui_init();
    
    // Hai hàm này sẽ vẽ toàn bộ giao diện MỘT CÁCH CHÍNH XÁC
    ui_draw_layout(); 
    ui_update_options(current_state);

    // ----- Vòng lặp Chat Chính -----
    fd_set read_fds;
    while (1) {
        
        if (g_resized) {
            ui_resize(current_state); // Gọi hàm vẽ lại
            g_resized = 0; // Reset cờ
        }

        FD_ZERO(&read_fds);
        FD_SET(STDIN_FILENO, &read_fds);
        FD_SET(sock_fd, &read_fds);
        
        // <-- REMOVED: timeout-based select to allow blocking select for smoother input -->
        int max_fd = (sock_fd > STDIN_FILENO) ? sock_fd : STDIN_FILENO;
        int activity = select(max_fd + 1, &read_fds, NULL, NULL, NULL);
        
        if (activity < 0) {
            // Lỗi select (có thể bị ngắt bởi signal)
            continue; // Quay lại đầu vòng lặp để kiểm tra g_resized
        }
        
        if (FD_ISSET(STDIN_FILENO, &read_fds)) {
            handle_keyboard_input(sock_fd);
        }
        if (FD_ISSET(sock_fd, &read_fds)) {
            handle_server_message(sock_fd);
        }
    }

    ui_destroy();
    close(sock_fd);
    return 0;
}

// (HÀM MỚI) Logic hỏi và gửi packet Login
void do_login_flow(int sock_fd) {
    ChatPacket packet;
    memset(&packet, 0, sizeof(ChatPacket));
    packet.type = MSG_TYPE_LOGIN_REQUEST;

    ui_add_log("Login> Enter username:");
    if (ui_get_input(packet.source_user, MAX_USERNAME) == -1) return; // (SỬA) Kiểm tra resize
    ui_clear_input();
    
    ui_add_log("Login> Enter password:");
    if (ui_get_input(packet.body, MAX_PASSWORD) == -1) return; // (SỬA) Kiểm tra resize
    ui_clear_input();

    // NOTE: do NOT set current_user here; wait for server confirmation
    // Ghi nhớ tên user đang chờ đăng nhập
    memset(pending_login, 0, sizeof(pending_login));
    strncpy(pending_login, packet.source_user, MAX_USERNAME - 1);
    pending_login_active = 1;

    // (THÊM MỚI) Reset ngữ cảnh
    current_chat_type = CHAT_TYPE_NONE;
    memset(current_chat_target, 0, MAX_USERNAME);

    write(sock_fd, &packet, sizeof(ChatPacket));
    ui_add_log("Login request sent. Waiting for server...");
}

// (HÀM MỚI) Logic hỏi và gửi packet Register
void do_register_flow(int sock_fd) {
    ChatPacket packet;
    memset(&packet, 0, sizeof(ChatPacket));
    packet.type = MSG_TYPE_REGISTER_REQUEST;

    ui_add_log("Register> Enter username:");
    if (ui_get_input(packet.source_user, MAX_USERNAME) == -1) return; // (SỬA) Kiểm tra resize
    ui_clear_input();
    
    ui_add_log("Register> Enter password:");
    if (ui_get_input(packet.body, MAX_PASSWORD) == -1) return; // (SỬA) Kiểm tra resize
    ui_clear_input();

    // (THÊM MỚI) Reset ngữ cảnh
    current_chat_type = CHAT_TYPE_NONE;
    memset(current_chat_target, 0, MAX_USERNAME);
    
    write(sock_fd, &packet, sizeof(ChatPacket));
    ui_add_log("Register request sent. Waiting for server...");
}

// (HÀM MỚI) Logic hỏi và gửi packet Add Friend (interactive)
void do_add_friend_flow(int sock_fd) {
    ChatPacket pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.type = MSG_TYPE_FRIEND_REQUEST;

    ui_add_log("Add> Enter username to add:");
    char target[MAX_USERNAME];
    if (ui_get_input(target, MAX_USERNAME) == -1) return; // interrupted by resize
    ui_clear_input();

    if (strlen(target) == 0) {
        ui_add_log("Add cancelled: empty username.");
        return;
    }

    strncpy(pkt.target_user, target, MAX_USERNAME - 1);
    if (send_packet(&pkt) == 0) ui_add_log("Friend request sent.");
    else ui_add_log("Failed to send friend request.");
}

// (HÀM MỚI) Accept friend interactive
void do_accept_friend_flow(int sock_fd) {
    ChatPacket pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.type = MSG_TYPE_FRIEND_ACCEPT;

    ui_add_log("Accept> Enter username to accept:");
    char target[MAX_USERNAME];
    if (ui_get_input(target, MAX_USERNAME) == -1) return;
    ui_clear_input();

    if (strlen(target) == 0) {
        ui_add_log("Accept cancelled: empty username.");
        return;
    }

    strncpy(pkt.target_user, target, MAX_USERNAME - 1);
    if (send_packet(&pkt) == 0) ui_add_log("Friend accept sent.");
    else ui_add_log("Failed to send accept.");
}

// (HÀM MỚI) Decline friend interactive
void do_decline_friend_flow(int sock_fd) {
    ChatPacket pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.type = MSG_TYPE_FRIEND_DECLINE;

    ui_add_log("Decline> Enter username to decline:");
    char target[MAX_USERNAME];
    if (ui_get_input(target, MAX_USERNAME) == -1) return;
    ui_clear_input();

    if (strlen(target) == 0) {
        ui_add_log("Decline cancelled: empty username.");
        return;
    }

    strncpy(pkt.target_user, target, MAX_USERNAME - 1);
    if (send_packet(&pkt) == 0) ui_add_log("Friend decline sent.");
    else ui_add_log("Failed to send decline.");
}

// (HÀM MỚI) Unfriend interactive
void do_unfriend_flow(int sock_fd) {
    ChatPacket pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.type = MSG_TYPE_FRIEND_UNFRIEND;

    ui_add_log("Unfriend> Enter username to remove:");
    char target[MAX_USERNAME];
    if (ui_get_input(target, MAX_USERNAME) == -1) return;
    ui_clear_input();

    if (strlen(target) == 0) {
        ui_add_log("Unfriend cancelled: empty username.");
        return;
    }

    strncpy(pkt.target_user, target, MAX_USERNAME - 1);
    if (send_packet(&pkt) == 0) ui_add_log("Unfriend request sent.");
    else ui_add_log("Failed to send unfriend.");
}

// (HÀM MỚI) Tạo nhóm (Create Group) interactive
void do_create_group_flow(int sock_fd) {
    ChatPacket pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.type = MSG_TYPE_CREATE_GROUP_REQUEST;

    ui_add_log("Create Group> Enter group name:");
    if (ui_get_input(pkt.target_user, MAX_USERNAME) == -1) return;
    ui_clear_input();

    if (strlen(pkt.target_user) == 0) {
        ui_add_log("Create cancelled: empty name.");
        return;
    }

    if (send_packet(&pkt) == 0) ui_add_log("Create group request sent.");
    else ui_add_log("Failed to send create group request.");
}

// (HÀM MỚI) Tham gia nhóm (Join Group) interactive
void do_join_group_flow(int sock_fd, const char* groupname_inline) {
    ChatPacket pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.type = MSG_TYPE_JOIN_GROUP_REQUEST;

    if (groupname_inline && strlen(groupname_inline) > 0) {
        strncpy(pkt.target_user, groupname_inline, MAX_USERNAME-1);
    } else {
        ui_add_log("Join> Enter group name:");
        if (ui_get_input(pkt.target_user, MAX_USERNAME) == -1) return;
        ui_clear_input();
    }

    if (strlen(pkt.target_user) == 0) {
        ui_add_log("Join cancelled: empty name.");
        return;
    }

    if (send_packet(&pkt) == 0) {
        ui_add_log("Join group request sent.");
        // Immediately switch to group chat on client side (server will notify)
        current_chat_type = CHAT_TYPE_GROUP;
        strncpy(current_chat_target, pkt.target_user, MAX_USERNAME-1);
        char msg[MAX_USERNAME + 30]; snprintf(msg, sizeof(msg), "Chatting in group: %s", current_chat_target);
        ui_add_log(msg);
        ui_update_status(msg);
    } else ui_add_log("Failed to send join request.");
}

// (HÀM MỚI) Mời người dùng vào nhóm (Invite to Group) interactive
void do_invite_group_flow(int sock_fd) {
    ChatPacket pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.type = MSG_TYPE_INVITE_TO_GROUP_REQUEST;

    ui_add_log("Invite> Enter username to invite:");
    char user[MAX_USERNAME];
    if (ui_get_input(user, MAX_USERNAME) == -1) return;
    ui_clear_input();

    ui_add_log("Invite> Enter group name:");
    char group[MAX_USERNAME];
    if (ui_get_input(group, MAX_USERNAME) == -1) return;
    ui_clear_input();

    if (strlen(user) == 0 || strlen(group) == 0) {
        ui_add_log("Invite cancelled: missing fields.");
        return;
    }

    strncpy(pkt.target_user, user, MAX_USERNAME-1); // invitee
    strncpy(pkt.body, group, MAX_BODY-1); // group name in body
    if (send_packet(&pkt) == 0) ui_add_log("Invite request sent.");
    else ui_add_log("Failed to send invite.");
}

// (HÀM MỚI) Gỡ người dùng khỏi nhóm (Remove from Group) interactive
void do_remove_group_flow(int sock_fd) {
    ChatPacket pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.type = MSG_TYPE_REMOVE_FROM_GROUP_REQUEST;

    ui_add_log("Remove> Enter username to remove:");
    char user[MAX_USERNAME];
    if (ui_get_input(user, MAX_USERNAME) == -1) return;
    ui_clear_input();

    ui_add_log("Remove> Enter group name:");
    char group[MAX_USERNAME];
    if (ui_get_input(group, MAX_USERNAME) == -1) return;
    ui_clear_input();

    if (strlen(user) == 0 || strlen(group) == 0) {
        ui_add_log("Remove cancelled: missing fields.");
        return;
    }

    strncpy(pkt.target_user, user, MAX_USERNAME-1); // target user
    strncpy(pkt.body, group, MAX_BODY-1); // group name
    if (send_packet(&pkt) == 0) ui_add_log("Remove request sent.");
    else ui_add_log("Failed to send remove request.");
}

// (HÀM MỚI) Rời khỏi nhóm (Leave Group) interactive
void do_leave_group_flow(int sock_fd) {
    ChatPacket pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.type = MSG_TYPE_LEAVE_GROUP_REQUEST;

    ui_add_log("Leave> Enter group name to leave:");
    if (ui_get_input(pkt.target_user, MAX_USERNAME) == -1) return;
    ui_clear_input();

    if (strlen(pkt.target_user) == 0) {
        ui_add_log("Leave cancelled.");
        return;
    }

    if (send_packet(&pkt) == 0) ui_add_log("Leave request sent.");
    else ui_add_log("Failed to send leave request.");
}

// --- NEW FUNCTION ---
// Prompt user when they type "/msg" without argument.
// Sets chat context to private or group depending on input.
void do_msg_prompt_flow(int sock_fd) {
    ui_add_log("Message> Enter username or #<group>:");
    char target[MAX_USERNAME + 2];
    if (ui_get_input(target, sizeof(target)) == -1) return; // interrupted by resize
    ui_clear_input();

    if (strlen(target) == 0) {
        ui_add_log("Cancelled: empty target.");
        return;
    }

    char status_msg[MAX_USERNAME + 30];
    if (target[0] == '#') {
        // group
        current_chat_type = CHAT_TYPE_GROUP;
        strncpy(current_chat_target, target + 1, MAX_USERNAME - 1);
        current_chat_target[MAX_USERNAME - 1] = '\0';
        snprintf(status_msg, sizeof(status_msg), "GROUP: %s", current_chat_target);
        ui_add_log(status_msg);
    } else {
        // private
        current_chat_type = CHAT_TYPE_PRIVATE;
        strncpy(current_chat_target, target, MAX_USERNAME - 1);
        current_chat_target[MAX_USERNAME - 1] = '\0';
        snprintf(status_msg, sizeof(status_msg), "Chat with user: %s", current_chat_target);
        ui_add_log(status_msg);
    }
    ui_update_status(status_msg);
}

// (VIẾT LẠI HOÀN TOÀN) Bộ não điều khiển input
void handle_keyboard_input(int sock_fd) {
    char buffer[INPUT_MAX];
    memset(buffer, 0, INPUT_MAX);
    
    if (ui_get_input(buffer, INPUT_MAX) == -1) {
        return; // Bị ngắt do resize
    }
    ui_clear_input();

    if (strlen(buffer) == 0) return;

    // --- LOGIC DỰA TRÊN TRẠNG THÁI ---
    if (current_state == STATE_PRE_LOGIN) {
        if (strcmp(buffer, "/1") == 0) {
            do_login_flow(sock_fd);
        } else if (strcmp(buffer, "/2") == 0) {
            do_register_flow(sock_fd);
        } else {
            ui_add_log("Invalid option. Please use /1 or /2.");
        }
    } 
    else if (current_state == STATE_LOGGED_IN) {
        // Block actions until server actually confirms authentication
        if (!is_authenticated) {
            ui_add_log("Not authenticated yet. Wait for server confirmation.");
            return;
        }

        ChatPacket packet;
        memset(&packet, 0, sizeof(ChatPacket));
        
        // --- KIỂM TRA LỆNH (Bắt đầu bằng /) ---
        if (buffer[0] == '/') {
            // New: handle "/msg" without argument -> prompt
            if (strcmp(buffer, "/msg") == 0) {
                do_msg_prompt_flow(sock_fd);
            } else if (strncmp(buffer, "/msg ", 5) == 0) {
                // (THAY ĐỔI LỚN) Lệnh này giờ chỉ để CHUYỂN NGỮ CẢNH
                char *target = buffer + 5;
                
                // Xóa khoảng trắng cuối (nếu có)
                while(strlen(target) > 0 && isspace((unsigned char)target[strlen(target)-1])) {
                    target[strlen(target)-1] = '\0';
                }

                if (strlen(target) == 0) {
                    ui_add_log("Usage: /msg <username>|#<groupname>");
                    return;
                }

                char status_msg[MAX_USERNAME + 30];
                if (target[0] == '#') {
                    // Chuyển sang chat Group
                    current_chat_type = CHAT_TYPE_GROUP;
                    strncpy(current_chat_target, target + 1, MAX_USERNAME); // Bỏ dấu #
                    snprintf(status_msg, sizeof(status_msg), "GROUP: %s", current_chat_target);
                    ui_add_log(status_msg);
                } else {
                    // Chuyển sang chat Private
                    current_chat_type = CHAT_TYPE_PRIVATE;
                    strncpy(current_chat_target, target, MAX_USERNAME);
                    snprintf(status_msg, sizeof(status_msg), "Chat with user: %s", current_chat_target);
                    ui_add_log(status_msg);
                }
                ui_update_status(status_msg); // Cập nhật status bar

            } else if (strcmp(buffer, "/exit") == 0) {
                packet.type = MSG_TYPE_LOGOUT_REQUEST;
                write(sock_fd, &packet, sizeof(ChatPacket));
                ui_add_log("Logging out...");
                // clear auth state locally
                is_authenticated = 0;
                sleep(1);
                close(sock_fd);
                exit(0);
            }
            else if (strncmp(buffer, "/add ", 5) == 0) {
                // inline form: /add username
                char *target = buffer + 5;
                // trim leading spaces
                while (*target && isspace((unsigned char)*target)) target++;
                // trim trailing spaces
                size_t tlen = strlen(target);
                while (tlen > 0 && isspace((unsigned char)target[tlen-1])) { target[tlen-1] = '\0'; tlen--; }
                if (tlen == 0) {
                    ui_add_log("Usage: /add <username>  or type /add to prompt.");
                    return;
                }
                ChatPacket pkt;
                memset(&pkt, 0, sizeof(pkt));
                pkt.type = MSG_TYPE_FRIEND_REQUEST;
                strncpy(pkt.target_user, target, MAX_USERNAME - 1);
                if (send_packet(&pkt) == 0) ui_add_log("Friend request sent.");
                else ui_add_log("Failed to send friend request.");
            } else if (strcmp(buffer, "/add") == 0) {
                // interactive prompt form
                do_add_friend_flow(sock_fd);
            } else if (strncmp(buffer, "/accept ", 8) == 0) {
                // inline /accept username
                char *target = buffer + 8;
                while (*target && isspace((unsigned char)*target)) target++;
                size_t tlen = strlen(target);
                while (tlen > 0 && isspace((unsigned char)target[tlen-1])) { ((char*)target)[tlen-1] = '\0'; tlen--; }
                if (tlen == 0) { ui_add_log("Usage: /accept <username>  or type /accept to prompt."); return; }
                ChatPacket pkt;
                memset(&pkt, 0, sizeof(pkt));
                pkt.type = MSG_TYPE_FRIEND_ACCEPT;
                strncpy(pkt.target_user, target, MAX_USERNAME-1);
                if (send_packet(&pkt) == 0) ui_add_log("Friend accept sent.");
                else ui_add_log("Failed to send accept.");
            } else if (strcmp(buffer, "/accept") == 0) {
                do_accept_friend_flow(sock_fd);
            } else if (strncmp(buffer, "/decline ", 9) == 0) {
                // inline /decline username
                char *target = buffer + 9;
                while (*target && isspace((unsigned char)*target)) target++;
                size_t tlen = strlen(target);
                while (tlen > 0 && isspace((unsigned char)target[tlen-1])) { ((char*)target)[tlen-1] = '\0'; tlen--; }
                if (tlen == 0) { ui_add_log("Usage: /decline <username>  or type /decline to prompt."); return; }
                ChatPacket pkt;
                memset(&pkt, 0, sizeof(pkt));
                pkt.type = MSG_TYPE_FRIEND_DECLINE;
                strncpy(pkt.target_user, target, MAX_USERNAME-1);
                if (send_packet(&pkt) == 0) ui_add_log("Friend decline sent.");
                else ui_add_log("Failed to send decline.");
            } else if (strcmp(buffer, "/decline") == 0) {
                do_decline_friend_flow(sock_fd);
            } else if (strncmp(buffer, "/unfriend ", 10) == 0) {
                // inline /unfriend username
                char *target = buffer + 10;
                while (*target && isspace((unsigned char)*target)) target++;
                size_t tlen = strlen(target);
                while (tlen > 0 && isspace((unsigned char)target[tlen-1])) { ((char*)target)[tlen-1] = '\0'; tlen--; }
                if (tlen == 0) { ui_add_log("Usage: /unfriend <username>  or type /unfriend to prompt."); return; }
                ChatPacket pkt;
                memset(&pkt, 0, sizeof(pkt));
                pkt.type = MSG_TYPE_FRIEND_UNFRIEND;
                strncpy(pkt.target_user, target, MAX_USERNAME-1);
                if (send_packet(&pkt) == 0) ui_add_log("Unfriend request sent.");
                else ui_add_log("Failed to send unfriend.");
            } else if (strcmp(buffer, "/unfriend") == 0) {
                do_unfriend_flow(sock_fd);
            } else if (strcmp(buffer, "/friends") == 0 || strcmp(buffer, "/2") == 0) {
                 ChatPacket pkt;
                 memset(&pkt, 0, sizeof(pkt));
                 pkt.type = MSG_TYPE_FRIEND_LIST_REQUEST;
                 send_packet(&pkt);
             }
            else if (strcmp(buffer, "/group_create") == 0 || strcmp(buffer, "/group_create ") == 0) {
                do_create_group_flow(sock_fd);
            } else if (strncmp(buffer, "/join ", 6) == 0) {
                // inline join: /join <group>
                char *g = buffer + 6;
                while (*g && isspace((unsigned char)*g)) g++;
                if (strlen(g) == 0) { ui_add_log("Usage: /join <group>"); return; }
                do_join_group_flow(sock_fd, g);
            } else if (strcmp(buffer, "/group_invite") == 0) {
                do_invite_group_flow(sock_fd);
            } else if (strncmp(buffer, "/group_invite ", 14) == 0) {
                // allow inline: /group_invite <user> <group>
                char *p = buffer + 14;
                while (*p && isspace((unsigned char)*p)) p++;
                char user[MAX_USERNAME] = {0}, group[MAX_USERNAME] = {0};
                sscanf(p, "%31s %31s", user, group);
                if (strlen(user) == 0 || strlen(group) == 0) { ui_add_log("Usage: /group_invite <user> <group>"); return; }
                ChatPacket pkt; memset(&pkt, 0, sizeof(pkt));
                pkt.type = MSG_TYPE_INVITE_TO_GROUP_REQUEST;
                strncpy(pkt.target_user, user, MAX_USERNAME-1);
                strncpy(pkt.body, group, MAX_BODY-1);
                if (send_packet(&pkt) == 0) ui_add_log("Invite sent.");
                else ui_add_log("Failed to send invite.");
            } else if (strcmp(buffer, "/group_remove") == 0) {
                do_remove_group_flow(sock_fd);
            } else if (strncmp(buffer, "/group_remove ", 14) == 0) {
                char *p = buffer + 14;
                char user[MAX_USERNAME] = {0}, group[MAX_USERNAME] = {0};
                sscanf(p, "%31s %31s", user, group);
                if (strlen(user) == 0 || strlen(group) == 0) { ui_add_log("Usage: /group_remove <user> <group>"); return; }
                ChatPacket pkt; memset(&pkt, 0, sizeof(pkt));
                pkt.type = MSG_TYPE_REMOVE_FROM_GROUP_REQUEST;
                strncpy(pkt.target_user, user, MAX_USERNAME-1);
                strncpy(pkt.body, group, MAX_BODY-1);
                if (send_packet(&pkt) == 0) ui_add_log("Remove request sent.");
                else ui_add_log("Failed to send remove request.");
            } else if (strcmp(buffer, "/group_leave") == 0) {
                do_leave_group_flow(sock_fd);
            } else if (strncmp(buffer, "/group_leave ", 13) == 0) {
                char *g = buffer + 13;
                while (*g && isspace((unsigned char)*g)) g++;
                if (strlen(g) == 0) { ui_add_log("Usage: /group_leave <group>"); return; }
                ChatPacket pkt; memset(&pkt, 0, sizeof(pkt));
                pkt.type = MSG_TYPE_LEAVE_GROUP_REQUEST;
                strncpy(pkt.target_user, g, MAX_USERNAME-1);
                if (send_packet(&pkt) == 0) ui_add_log("Leave request sent.");
                else ui_add_log("Failed to send leave request.");
            } else if (strcmp(buffer, "/group_joined") == 0) {
                ChatPacket pkt; memset(&pkt, 0, sizeof(pkt));
                pkt.type = MSG_TYPE_GROUP_LIST_JOINED_REQUEST;
                if (send_packet(&pkt) == 0) ui_add_log("Requested joined groups...");
                else ui_add_log("Failed to request joined groups.");
            } else if (strcmp(buffer, "/group_all") == 0) {
                ChatPacket pkt; memset(&pkt, 0, sizeof(pkt));
                pkt.type = MSG_TYPE_GROUP_LIST_ALL_REQUEST;
                if (send_packet(&pkt) == 0) ui_add_log("Requested all groups...");
                else ui_add_log("Failed to request groups list.");
            }
            // ...existing else branches...
        } 
        // --- KHÔNG PHẢI LỆNH -> Đây là tin nhắn chat ---
        else {
            if (current_chat_type == CHAT_TYPE_NONE) {
                ui_add_log("Error: No active chat. Use /msg <target> to start.");
                return;
            }
            
            // 1. Chuẩn bị nội dung
            strncpy(packet.body, buffer, MAX_BODY);
            char my_msg[MAX_BODY + MAX_USERNAME + 10];
            
            // 2. Gửi đi theo ngữ cảnh
            if (current_chat_type == CHAT_TYPE_PRIVATE) {
                packet.type = MSG_TYPE_PRIVATE_MESSAGE;
                strncpy(packet.target_user, current_chat_target, MAX_USERNAME);
                snprintf(my_msg, sizeof(my_msg), "[Me to %s]: %s", current_chat_target, buffer);
            } 
            else if (current_chat_type == CHAT_TYPE_GROUP) {
                packet.type = MSG_TYPE_GROUP_MESSAGE;
                strncpy(packet.target_user, current_chat_target, MAX_USERNAME);
                snprintf(my_msg, sizeof(my_msg), "[Me to #%s]: %s", current_chat_target, buffer);
            }

            // 3. Gửi packet
            write(sock_fd, &packet, sizeof(ChatPacket));

            // 4. In tin nhắn của MÌNH lên cửa sổ chat
            ui_add_message(my_msg);
        }
    }
}

// (SỬA LẠI) Bộ não xử lý phản hồi
void handle_server_message(int sock_fd) {
    ChatPacket packet;
    ssize_t bytes_read = read(sock_fd, &packet, sizeof(ChatPacket));

    if (bytes_read <= 0) {
        ui_destroy();
        printf("Server disconnected. Exiting.\n");
        exit(0);
    }

    char buffer[MAX_BODY + MAX_USERNAME + 20];

    // --- Xử lý phản hồi và THAY ĐỔI TRẠNG THÁI ---
    switch (packet.type) {
        // --- Các case thay đổi trạng thái ---
        case MSG_TYPE_LOGIN_SUCCESS: {
            ui_add_log(packet.body); // "Login successful!"

            // Chỉ chấp nhận nếu đang có yêu cầu login và tên user khớp
            if (!pending_login_active || strncmp(pending_login, packet.source_user, MAX_USERNAME) != 0) {
                ui_add_log("Received unexpected login success. Ignoring.");
                break;
            }

            // Accept login: set current_user from server and mark authenticated
            memset(current_user, 0, sizeof(current_user));
            strncpy(current_user, packet.source_user, MAX_USERNAME - 1);
            current_user[MAX_USERNAME - 1] = '\0';

            is_authenticated = 1;
            pending_login_active = 0; // Xóa cờ chờ
            current_state = STATE_LOGGED_IN; // <-- THAY ĐỔI TRẠNG THÁI
            ui_update_options(STATE_LOGGED_IN); // <-- VẼ LẠI MENU
            
            {
                char status[MAX_USERNAME + 20];
                snprintf(status, sizeof(status), "Logged in as %s", current_user);
                ui_update_status(status);
            }
        } break;
            
        case MSG_TYPE_LOGIN_FAIL:
            ui_add_log(packet.body); // In lỗi ra LOG
            // Ensure we are not authenticated
            is_authenticated = 0;
            pending_login_active = 0; // Xóa cờ chờ
            current_state = STATE_PRE_LOGIN;
            // Trạng thái vẫn là PRE_LOGIN
            break;

        case MSG_TYPE_REGISTER_FAIL:
            ui_add_log(packet.body); // In lỗi ra LOG
            break;

        case MSG_TYPE_REGISTER_SUCCESS:
            ui_add_log(packet.body); // "Register successful!"
            ui_add_log("Please login using /1.");
            // Trạng thái vẫn là PRE_LOGIN
            break;

        // --- Các case xử lý tin nhắn ---
        case MSG_TYPE_RECEIVE_PRIVATE:
            snprintf(buffer, sizeof(buffer), "[From %.*s]: %.*s",
                     (int)MAX_USERNAME, packet.source_user,
                     (int)MAX_BODY, packet.body);
            ui_add_message(buffer);
            break;

        case MSG_TYPE_RECEIVE_GROUP_MESSAGE: // (THÊM MỚI)
            snprintf(buffer, sizeof(buffer), "[#%.*s from %.*s]: %.*s",
                     (int)MAX_USERNAME, packet.target_user,
                     (int)MAX_USERNAME, packet.source_user,
                     (int)MAX_BODY, packet.body);
            ui_add_message(buffer);
            break;

        case MSG_TYPE_SEND_OFFLINE_MSG:
            snprintf(buffer, sizeof(buffer), "[Offline Msg from %.*s]: %.*s",
                     (int)MAX_USERNAME, packet.source_user,
                     (int)MAX_BODY, packet.body);
            ui_add_message(buffer);
            break;

        // --- Các case thông báo (Như cũ) ---
        case MSG_TYPE_ONLINE_LIST_UPDATE:
            // Always show online list in log when received
            snprintf(buffer, sizeof(buffer), "Online: %.*s",
                     (int)MAX_BODY, packet.body);
            ui_add_log(buffer);
            break;

        case MSG_TYPE_FRIEND_LIST_RESPONSE:
            ui_add_log(packet.body);
            break;

        case MSG_TYPE_FRIEND_REQUEST_INCOMING: {
            char buf[MAX_BODY];
            snprintf(buf, sizeof(buf), "Friend request from %s. Type: /accept %s or /decline %s", packet.source_user, packet.source_user, packet.source_user);
            ui_add_log(buf);
        } break;

        case MSG_TYPE_FRIEND_UPDATE:
            // Show "username <status>" when server provides body, otherwise fallback
            {
                char buf[MAX_BODY + MAX_USERNAME + 4];
                if (packet.body[0]) {
                    // Prefer: "<username> <message>"
                    snprintf(buf, sizeof(buf), "%s %s",
                             (packet.source_user[0] ? packet.source_user : "Server"),
                             packet.body);
                } else {
                    snprintf(buf, sizeof(buf), "Friend update: %s", packet.source_user);
                }
                ui_add_log(buf);
            }
            break;

        case MSG_TYPE_GROUP_LIST_RESPONSE:
            ui_add_log(packet.body);
            break;

        default:
            // (Phản hồi cho /friends, /group... sẽ rơi vào đây)
            snprintf(buffer, sizeof(buffer), "Server: %.*s",
                     (int)MAX_BODY, packet.body);
            ui_add_log(buffer);
    }
}

// Hàm kết nối (giữ nguyên)
int connect_to_server() {
    int sock_fd;
    struct sockaddr_in server_addr;

    sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd == -1) { perror("socket() failed"); return -1; }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr);

    if (connect(sock_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        perror("connect() failed"); close(sock_fd); return -1; }
    
    return sock_fd;
}
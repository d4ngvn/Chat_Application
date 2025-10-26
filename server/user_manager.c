#include "user_manager.h"
#include "db_handler.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

// Handle user registration
void handle_register(int client_fd, ChatPacket* packet, sqlite3* db) {
    int rc = db_register_user(db, packet->source_user, packet->body); // Giả sử pass nằm trong body

    ChatPacket reply;
    memset(&reply, 0, sizeof(reply));
    if (rc == 0) {
        reply.type = MSG_TYPE_REGISTER_SUCCESS;
        snprintf(reply.body, MAX_BODY, "Register successful. You can now login.");
    } else {
        reply.type = MSG_TYPE_REGISTER_FAIL;
        snprintf(reply.body, MAX_BODY, "Register failed (username may exist).");
    }
    write(client_fd, &reply, sizeof(ChatPacket));
}
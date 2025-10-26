#ifndef GROUP_MANAGER_H
#define GROUP_MANAGER_H

#include <sqlite3.h>
#include "../shared/protocol.h"
#include "server.h"

// Handle create/join/invite/remove/leave and group messaging
void handle_create_group(int client_fd, ChatPacket* packet, ClientSession* sessions, sqlite3 *db);
void handle_join_group_request(int client_fd, ChatPacket* packet, ClientSession* sessions, sqlite3 *db);
void handle_invite_to_group(int client_fd, ChatPacket* packet, ClientSession* sessions, sqlite3 *db);
void handle_remove_from_group(int client_fd, ChatPacket* packet, ClientSession* sessions, sqlite3 *db);
void handle_leave_group(int client_fd, ChatPacket* packet, ClientSession* sessions, sqlite3 *db);
void handle_group_message(ChatPacket* packet, ClientSession* sessions, sqlite3 *db);

// NEW: list handlers
void handle_group_list_joined(int client_fd, ChatPacket* packet, ClientSession* sessions, sqlite3 *db);
void handle_group_list_all(int client_fd, ChatPacket* packet, ClientSession* sessions, sqlite3 *db);

#endif

#ifndef USER_MANAGER_H
#define USER_MANAGER_H
#include "db_handler.h" // Hàm DB
#include <stdio.h>
#include <string.h>
#include <unistd.h> // cho write()
#include <sqlite3.h>
#include "../shared/protocol.h"
#include "server.h" 
// Forward declaration
struct ClientSession;

void handle_register(int client_fd, ChatPacket* packet, sqlite3 *db);
void handle_login(int client_fd, ChatPacket* packet, sqlite3 *db, struct ClientSession* sessions);

#endif
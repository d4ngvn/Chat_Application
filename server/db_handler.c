#include "db_handler.h"
#include <stdio.h>
#include <string.h>
// Hàm db_open từ Ngày 1
int db_open(const char* db_file, sqlite3 **db) {
    int rc = sqlite3_open(db_file, db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(*db));
        sqlite3_close(*db);
        return 1;
    }
    printf("Database connection established.\n");
    return 0;
}

// Hàm db_close từ Ngày 1
void db_close(sqlite3 *db) {
    sqlite3_close(db);
    printf("Database connection closed.\n");
}

// HÀM MỚI: Đăng ký
int db_register_user(sqlite3 *db, const char* user, const char* pass) {
    sqlite3_stmt *stmt;
    const char *sql = "INSERT INTO users (username, password) VALUES (?, ?);";
    int rc;

    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to prepare statement: %s\n", sqlite3_errmsg(db));
        return 2; // Lỗi CSDL
    }

    // Gắn giá trị vào câu lệnh SQL (tránh SQL injection)
    sqlite3_bind_text(stmt, 1, user, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, pass, -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt); // Thực thi
    if (rc == SQLITE_DONE) {
        printf("User '%s' registered successfully.\n", user);
        rc = 0; // Thành công
    } else if (rc == SQLITE_CONSTRAINT) {
        fprintf(stderr, "User '%s' already exists.\n", user);
        rc = 1; // User tồn tại (vi phạm UNIQUE)
    } else {
        fprintf(stderr, "SQL error: %s\n", sqlite3_errmsg(db));
        rc = 2; // Lỗi SQL khác
    }

    sqlite3_finalize(stmt);
    return rc;
}

// HÀM MỚI: Đăng nhập
int db_login_user(sqlite3 *db, const char* user, const char* pass) {
    sqlite3_stmt *stmt;
    const char *sql = "SELECT password FROM users WHERE username = ?;";
    int rc;

    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to prepare statement: %s\n", sqlite3_errmsg(db));
        return 2; // Lỗi CSDL
    }

    sqlite3_bind_text(stmt, 1, user, -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt); // Thực thi SELECT
    if (rc == SQLITE_ROW) { // Tìm thấy user
        const char *db_pass = (const char*)sqlite3_column_text(stmt, 0);
        if (strcmp(pass, db_pass) == 0) {
            printf("User '%s' logged in successfully.\n", user);
            rc = 0; // Thành công
        } else {
            printf("User '%s' provided wrong password.\n", user);
            rc = 1; // Sai mật khẩu
        }
    } else if (rc == SQLITE_DONE) { // Không tìm thấy user
        printf("User '%s' not found.\n", user);
        rc = 1; // User không tồn tại
    } else {
        fprintf(stderr, "SQL error: %s\n", sqlite3_errmsg(db));
        rc = 2; // Lỗi SQL khác
    }

    sqlite3_finalize(stmt);
    return rc;
}

// HÀM MỚI: Lưu tin nhắn offline
int db_store_offline_message(sqlite3 *db, const char* from, const char* to, const char* msg) {
    sqlite3_stmt *stmt;
    const char *sql = "INSERT INTO offline_messages (from_user, to_user, message) VALUES (?, ?, ?);";
    
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to prepare statement: %s\n", sqlite3_errmsg(db));
        return 1;
    }

    sqlite3_bind_text(stmt, 1, from, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, to, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, msg, -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        fprintf(stderr, "SQL error storing offline message: %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        return 1;
    }

    printf("Stored offline message from '%s' to '%s'\n", from, to);
    sqlite3_finalize(stmt);
    return 0;
}

// HÀM MỚI: Gửi tin nhắn đang chờ (phức tạp hơn)
int db_send_pending_messages(sqlite3 *db, const char* user, void (*callback)(void*, ChatPacket*), void* arg) {
    sqlite3_stmt *stmt_select, *stmt_delete;
    const char *sql_select = "SELECT from_user, message FROM offline_messages WHERE to_user = ? ORDER BY timestamp ASC;";
    const char *sql_delete = "DELETE FROM offline_messages WHERE to_user = ?;";
    int rc;

    // 1. Chuẩn bị câu lệnh SELECT
    rc = sqlite3_prepare_v2(db, sql_select, -1, &stmt_select, 0);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to prepare select pending: %s\n", sqlite3_errmsg(db));
        return 1;
    }
    sqlite3_bind_text(stmt_select, 1, user, -1, SQLITE_STATIC);

    // 2. Lặp qua từng tin nhắn và gửi đi
    while ((rc = sqlite3_step(stmt_select)) == SQLITE_ROW) {
        const char *from_user = (const char*)sqlite3_column_text(stmt_select, 0);
        const char *message = (const char*)sqlite3_column_text(stmt_select, 1);

        ChatPacket packet;
        memset(&packet, 0, sizeof(ChatPacket));
        packet.type = MSG_TYPE_SEND_OFFLINE_MSG;
        strncpy(packet.source_user, from_user, MAX_USERNAME);
        strncpy(packet.body, message, MAX_BODY);

        // Gọi callback để gửi packet (chính là gửi qua socket)
        callback(arg, &packet);
    }
    sqlite3_finalize(stmt_select);

    // 3. Chuẩn bị câu lệnh DELETE (Xóa tất cả tin nhắn đã gửi)
    rc = sqlite3_prepare_v2(db, sql_delete, -1, &stmt_delete, 0);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to prepare delete pending: %s\n", sqlite3_errmsg(db));
        return 1;
    }
    sqlite3_bind_text(stmt_delete, 1, user, -1, SQLITE_STATIC);
    
    rc = sqlite3_step(stmt_delete);
    if (rc != SQLITE_DONE) {
        fprintf(stderr, "Failed to delete pending messages: %s\n", sqlite3_errmsg(db));
    } else {
        printf("Cleared pending messages for user '%s'\n", user);
    }
    sqlite3_finalize(stmt_delete);

    return 0;
}

// Check whether a user exists in the users table.
// Returns 1 if exists, 0 otherwise.
int db_user_exists(sqlite3* db, const char* username) {
    if (!db || !username) return 0;
    const char *sql = "SELECT 1 FROM users WHERE username = ? LIMIT 1;";
    sqlite3_stmt *stmt = NULL;
    int exists = 0;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        if (stmt) sqlite3_finalize(stmt);
        return 0;
    }

    sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);
    int rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) exists = 1;
    sqlite3_finalize(stmt);
    return exists;
}

// Adapter for old name: delegate to db_login_user if available.
// Returns 1 on success (authenticated), 0 on failure.
int db_authenticate_user(sqlite3* db, const char* username, const char* password) {
    int rc = db_login_user(db, username, password);
    if (rc == 0) return 1; // db_login_user: 0 == success
    return 0; // any non-zero = failure
}

int db_friend_request(sqlite3 *db, const char* sender, const char* receiver) {
    sqlite3_stmt *stmt = NULL;
    const char *sql = "INSERT INTO friends (user_a, user_b, status) VALUES (?, ?, 0);";
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to prepare friend_request: %s\n", sqlite3_errmsg(db));
        if (stmt) sqlite3_finalize(stmt);
        return 1;
    }

    sqlite3_bind_text(stmt, 1, sender, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, receiver, -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc == SQLITE_DONE) {
        return 0;
    } else if (rc == SQLITE_CONSTRAINT) {
        // already exists or violates constraint
        return 1;
    } else {
        fprintf(stderr, "SQL error in friend_request: %s\n", sqlite3_errmsg(db));
        return 1;
    }
}

// (MỚI) Chấp nhận (status = 1)
int db_friend_accept(sqlite3 *db, const char* accepter, const char* sender) {
    sqlite3_stmt *stmt = NULL;
    const char *sql = "UPDATE friends SET status = 1 WHERE user_a = ? AND user_b = ? AND status = 0;";
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to prepare friend_accept: %s\n", sqlite3_errmsg(db));
        if (stmt) sqlite3_finalize(stmt);
        return 1;
    }
    sqlite3_bind_text(stmt, 1, sender, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, accepter, -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    int changes = sqlite3_changes(db);
    sqlite3_finalize(stmt);

    return (rc == SQLITE_DONE && changes > 0) ? 0 : 1;
}

// (MỚI) Từ chối hoặc Hủy bạn
int db_friend_decline(sqlite3 *db, const char* decliner, const char* sender) {
    sqlite3_stmt *stmt = NULL;
    const char *sql = "DELETE FROM friends WHERE user_a = ? AND user_b = ? AND status = 0;";
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to prepare friend_decline: %s\n", sqlite3_errmsg(db));
        if (stmt) sqlite3_finalize(stmt);
        return 1;
    }
    sqlite3_bind_text(stmt, 1, sender, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, decliner, -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    int changes = sqlite3_changes(db);
    sqlite3_finalize(stmt);

    return (rc == SQLITE_DONE && changes > 0) ? 0 : 1;
}

int db_friend_unfriend(sqlite3 *db, const char* user1, const char* user2) {
    sqlite3_stmt *stmt = NULL;
    const char *sql = "DELETE FROM friends WHERE status = 1 AND ((user_a = ? AND user_b = ?) OR (user_a = ? AND user_b = ?));";
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to prepare friend_unfriend: %s\n", sqlite3_errmsg(db));
        if (stmt) sqlite3_finalize(stmt);
        return 1;
    }
    sqlite3_bind_text(stmt, 1, user1, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, user2, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, user2, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, user1, -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    int changes = sqlite3_changes(db);
    sqlite3_finalize(stmt);

    return (rc == SQLITE_DONE && changes > 0) ? 0 : 1;
}

// (MỚI) Lấy danh sách bạn bè (status = 1)
int db_get_friend_list(sqlite3 *db, const char* user, db_friend_list_callback callback, void* arg) {
    sqlite3_stmt *stmt = NULL;
    const char *sql = "SELECT user_b FROM friends WHERE user_a = ? AND status = 1 "
                      "UNION "
                      "SELECT user_a FROM friends WHERE user_b = ? AND status = 1;";
    
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to prepare get_friend_list: %s\n", sqlite3_errmsg(db));
        if (stmt) sqlite3_finalize(stmt);
        return 1;
    }

    sqlite3_bind_text(stmt, 1, user, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, user, -1, SQLITE_STATIC);

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        const char *friend_name = (const char*)sqlite3_column_text(stmt, 0);
        callback(arg, friend_name); // Gọi callback cho mỗi người bạn
    }
    
    sqlite3_finalize(stmt);
    return 0;
}

// --- NEW: Group DB functions ---

int db_create_group(sqlite3 *db, const char* group_name, const char* owner) {
    sqlite3_stmt *stmt = NULL;
    const char *sql = "INSERT INTO groups (group_name, owner_username) VALUES (?, ?);";
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to prepare create_group: %s\n", sqlite3_errmsg(db));
        if (stmt) sqlite3_finalize(stmt);
        return 1;
    }
    sqlite3_bind_text(stmt, 1, group_name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, owner, -1, SQLITE_STATIC);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc == SQLITE_DONE) return 0;
    fprintf(stderr, "SQL error create_group: %s\n", sqlite3_errmsg(db));
    return 1;
}

int db_group_exists(sqlite3 *db, const char* group_name) {
    sqlite3_stmt *stmt = NULL;
    const char *sql = "SELECT 1 FROM groups WHERE group_name = ? LIMIT 1;";
    int exists = 0;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        if (stmt) sqlite3_finalize(stmt);
        return 0;
    }
    sqlite3_bind_text(stmt, 1, group_name, -1, SQLITE_STATIC);
    int rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) exists = 1;
    sqlite3_finalize(stmt);
    return exists;
}

int db_add_group_member(sqlite3 *db, const char* group_name, const char* username) {
    sqlite3_stmt *stmt = NULL;
    const char *sql_group_id = "SELECT group_id FROM groups WHERE group_name = ? LIMIT 1;";
    int rc = sqlite3_prepare_v2(db, sql_group_id, -1, &stmt, 0);
    if (rc != SQLITE_OK) { if (stmt) sqlite3_finalize(stmt); return 1; }
    sqlite3_bind_text(stmt, 1, group_name, -1, SQLITE_STATIC);
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) { sqlite3_finalize(stmt); return 1; }
    int group_id = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);

    const char *sql = "INSERT INTO group_members (group_id, username) VALUES (?, ?);";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, 0) != SQLITE_OK) { if (stmt) sqlite3_finalize(stmt); return 1; }
    sqlite3_bind_int(stmt, 1, group_id);
    sqlite3_bind_text(stmt, 2, username, -1, SQLITE_STATIC);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE) ? 0 : 1;
}

int db_remove_group_member(sqlite3 *db, const char* group_name, const char* username) {
    sqlite3_stmt *stmt = NULL;
    const char *sql_group_id = "SELECT group_id FROM groups WHERE group_name = ? LIMIT 1;";
    int rc = sqlite3_prepare_v2(db, sql_group_id, -1, &stmt, 0);
    if (rc != SQLITE_OK) { if (stmt) sqlite3_finalize(stmt); return 1; }
    sqlite3_bind_text(stmt, 1, group_name, -1, SQLITE_STATIC);
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) { sqlite3_finalize(stmt); return 1; }
    int group_id = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);

    const char *sql = "DELETE FROM group_members WHERE group_id = ? AND username = ?;";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, 0) != SQLITE_OK) { if (stmt) sqlite3_finalize(stmt); return 1; }
    sqlite3_bind_int(stmt, 1, group_id);
    sqlite3_bind_text(stmt, 2, username, -1, SQLITE_STATIC);
    rc = sqlite3_step(stmt);
    int changes = sqlite3_changes(db);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE && changes > 0) ? 0 : 1;
}

int db_is_group_owner(sqlite3 *db, const char* group_name, const char* username) {
    sqlite3_stmt *stmt = NULL;
    const char *sql = "SELECT 1 FROM groups WHERE group_name = ? AND owner_username = ? LIMIT 1;";
    int is_owner = 0;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        if (stmt) sqlite3_finalize(stmt);
        return 0;
    }
    sqlite3_bind_text(stmt, 1, group_name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, username, -1, SQLITE_STATIC);
    int rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) is_owner = 1;
    sqlite3_finalize(stmt);
    return is_owner;
}

int db_get_group_members(sqlite3 *db, const char* group_name, db_group_member_callback callback, void* arg) {
    sqlite3_stmt *stmt = NULL;
    const char *sql = "SELECT gm.username FROM group_members gm "
                      "JOIN groups g ON g.group_id = gm.group_id "
                      "WHERE g.group_name = ?;";
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
    if (rc != SQLITE_OK) {
        if (stmt) sqlite3_finalize(stmt);
        return 1;
    }
    sqlite3_bind_text(stmt, 1, group_name, -1, SQLITE_STATIC);
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        const char *member = (const char*)sqlite3_column_text(stmt, 0);
        callback(arg, member);
    }
    sqlite3_finalize(stmt);
    return 0;
}

// --- NEW: Group listing helpers ---

int db_get_groups_for_user(sqlite3 *db, const char* username, db_group_list_callback callback, void* arg) {
    if (!db || !username || !callback) return 1;
    sqlite3_stmt *stmt = NULL;
    const char *sql = "SELECT g.group_name FROM groups g "
                      "JOIN group_members gm ON g.group_id = gm.group_id "
                      "WHERE gm.username = ?;";
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        if (stmt) sqlite3_finalize(stmt);
        return 1;
    }
    sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        const char *gname = (const char*)sqlite3_column_text(stmt, 0);
        callback(arg, gname);
    }
    sqlite3_finalize(stmt);
    return 0;
}

int db_get_all_groups(sqlite3 *db, db_group_list_callback callback, void* arg) {
    if (!db || !callback) return 1;
    sqlite3_stmt *stmt = NULL;
    const char *sql = "SELECT group_name FROM groups;";
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        if (stmt) sqlite3_finalize(stmt);
        return 1;
    }
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        const char *gname = (const char*)sqlite3_column_text(stmt, 0);
        callback(arg, gname);
    }
    sqlite3_finalize(stmt);
    return 0;
}

// --- NEW: Check if a user is a member of a group ---
int db_is_group_member(sqlite3 *db, const char* group_name, const char* username) {
    if (!db || !group_name || !username) return 0;
    sqlite3_stmt *stmt = NULL;
    const char *sql = "SELECT 1 FROM group_members gm "
                      "JOIN groups g ON g.group_id = gm.group_id "
                      "WHERE g.group_name = ? AND gm.username = ? LIMIT 1;";
    int is_member = 0;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        if (stmt) sqlite3_finalize(stmt);
        return 0;
    }
    sqlite3_bind_text(stmt, 1, group_name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, username, -1, SQLITE_STATIC);
    int rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) is_member = 1;
    sqlite3_finalize(stmt);
    return is_member;
}
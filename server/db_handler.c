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
#ifndef UI_H
#define UI_H

#include <ncurses.h>
#include <signal.h> // (THÊM MỚI)

// (THÊM MỚI) Khai báo biến cờ toàn cục
extern volatile sig_atomic_t g_resized;

// (THÊM MỚI) Định nghĩa trạng thái client
typedef enum {
    STATE_PRE_LOGIN,  // Trạng thái chưa đăng nhập
    STATE_LOGGED_IN   // Trạng thái đã đăng nhập
} ClientState;

// Các cửa sổ toàn cục
extern WINDOW *win_chat, *win_log, *win_option, *win_input; // Sửa help -> option

/**
 * @brief Khởi tạo màn hình NCurses, màu sắc, và tạo 4 cửa sổ con.
 */
void ui_init();

/**
 * @brief Vẽ layout, border, và gọi hàm vẽ option.
 */
void ui_draw_layout();

/**
 * @brief Đóng và dọn dẹp NCurses.
 */
void ui_destroy();

/**
 * @brief In một dòng tin nhắn vào cửa sổ CHAT (win_chat) và xử lý cuộn.
 */
void ui_add_message(const char* message);

/**
 * @brief In một dòng thông báo vào cửa sổ LOG (win_log) và xử lý cuộn.
 */
void ui_add_log(const char* message);

/**
 * @brief Cập nhật dòng trạng thái trên viền của cửa sổ chat.
 */
void ui_update_status(const char* status);

/**
 * @brief (THÊM MỚI) Cập nhật nội dung cửa sổ OPTIONS dựa trên trạng thái.
 */
void ui_update_options(ClientState state);

/**
 * @brief Prototype cho handler resize (đã thêm) — sử dụng để gọi khi g_resized set.
 */
void ui_resize(ClientState state);

/**
 * @brief Lấy 1 dòng input từ người dùng (từ win_input).
 * @return 0 nếu thành công, -1 nếu bị ngắt do resize. (SỬA LẠI)
 */
int ui_get_input(char* buffer, int max_len);

/**
 * @brief Xóa nội dung trong cửa sổ input (sau khi gửi).
 */
void ui_clear_input();

// (optional helper, not strictly required but useful)
void ui_create_windows(void);

#endif
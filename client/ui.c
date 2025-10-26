#include "ui.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h> // added for isprint

// Các cửa sổ 'public' (dùng cho client.c) là các cửa sổ CON (content)
WINDOW *win_chat, *win_log, *win_option, *win_input;

// (MỚI) Các cửa sổ 'private' (chỉ dùng trong ui.c) là các cửa sổ CHA (border)
static WINDOW *win_chat_border, *win_log_border, *win_option_border, *win_input_border;

// Extern resize flag (được định nghĩa nơi khác)
// match the declaration in ui.h to avoid conflicting qualifiers
extern volatile sig_atomic_t g_resized;


// Tách logic tạo cửa sổ
void ui_create_windows() {
    int height, width;
    getmaxyx(stdscr, height, width); 

    int input_height = 3;
    int main_height = height - input_height; 
    int chat_width = width / 2;
    int log_width = width / 4;
    int option_width = width - chat_width - log_width; 

    // 1. Tạo các cửa sổ CHA (Border)
    win_chat_border = newwin(main_height, chat_width, 0, 0); 
    win_log_border = newwin(main_height, log_width, 0, chat_width);
    win_option_border = newwin(main_height, option_width, 0, chat_width + log_width);
    win_input_border = newwin(input_height, width, main_height, 0); 

    // 2. Tạo các cửa sổ CON (Content) nằm BÊN TRONG cửa sổ cha
    // (Lưu ý: dùng derwin() - derive window)
    // (cao-2, rộng-2, y=1, x=1 so với cửa sổ cha)
    win_chat = derwin(win_chat_border, main_height - 2, chat_width - 2, 1, 1);
    win_log = derwin(win_log_border, main_height - 2, log_width - 2, 1, 1);
    win_option = derwin(win_option_border, main_height - 2, option_width - 2, 1, 1);
    win_input = derwin(win_input_border, input_height - 2, width - 2, 1, 1);

    // 3. Bật cuộn CHỈ cho cửa sổ content
    scrollok(win_chat, TRUE);
    scrollok(win_log, TRUE);
    keypad(win_input, TRUE);
}

// ui_init chỉ khởi tạo ncurses và gọi hàm tạo cửa sổ
void ui_init() {
    initscr(); // Bắt đầu NCurses
    noecho();  // Tắt echo
    
    ui_create_windows(); // Gọi hàm mới
}

// Vẽ layout ban đầu (VÀ refresh)
void ui_draw_layout() {
    // Vẽ viền + tiêu đề lên cửa sổ CHA (Border)
    box(win_chat_border, 0, 0);
    mvwprintw(win_chat_border, 0, 2, " CHAT ");
    
    box(win_log_border, 0, 0);
    mvwprintw(win_log_border, 0, 2, " SYSTEM LOG ");

    box(win_option_border, 0, 0);
    // (Tiêu đề OPTIONS sẽ được vẽ bởi ui_update_options)
    
    box(win_input_border, 0, 0);
    mvwprintw(win_input_border, 0, 2, " MESSAGE ");
    
    // Refresh các cửa sổ CHA để hiển thị
    wrefresh(win_chat_border);
    wrefresh(win_log_border);
    wrefresh(win_option_border);
    wrefresh(win_input_border);
    
    // Refresh cửa sổ CON để con trỏ nhảy đúng
    wrefresh(win_input);
}

// Logic vẽ lại khi resize
void ui_resize(ClientState state) {
    endwin();
    clear();
    initscr(); // Khởi tạo lại
    noecho();
    refresh(); // Lấy size mới

    ui_create_windows(); // Tạo lại tất cả cửa sổ
    ui_draw_layout(); // Vẽ lại layout (Hàm này đã tự refresh)
    ui_update_options(state); // Vẽ lại menu (Hàm này đã tự refresh)
}

// Hàm dọn dẹp
void ui_destroy() {
    endwin(); 
}

// Cập nhật menu options
void ui_update_options(ClientState state) {
    // 1. Xóa nội dung cửa sổ CON
    wclear(win_option);
    
    // 2. Vẽ lại viền + tiêu đề cửa sổ CHA
    box(win_option_border, 0, 0);
    mvwprintw(win_option_border, 0, 2, " OPTIONS ");
    wrefresh(win_option_border); // Refresh cửa sổ cha

    // 3. In nội dung mới vào cửa sổ CON
    int y = 0; // Bắt đầu từ y=0 của cửa sổ CON
    if (state == STATE_PRE_LOGIN) {
        mvwprintw(win_option, y++, 1, "USAGE: /<option>"); // (y, x)
        /* replaced empty-format string to avoid compiler warning */
        mvwprintw(win_option, y++, 1, " ");
        mvwprintw(win_option, y++, 1, "1. Login");
        mvwprintw(win_option, y++, 1, "2. Register");
    } else if (state == STATE_LOGGED_IN) {
        mvwprintw(win_option, y++, 1, "USAGE: /<option>");
        mvwprintw(win_option, y++, 1, "-------------------");
        mvwprintw(win_option, y++, 1, "1. Private Chat (/msg)");
        mvwprintw(win_option, y++, 1, "2. List Friends");
        mvwprintw(win_option, y++, 1, "3. Add Friend (/friend)");
        mvwprintw(win_option, y++, 1, "4. Accept Friend (/accept)");
        mvwprintw(win_option, y++, 1, "5. Create Group (/group)");
        // ... (thêm các lệnh khác nếu cần)
        mvwprintw(win_option, y++, 1, "/exit - Thoát");
    }
    
    // 4. Refresh cửa sổ CON (để hiện nội dung) và INPUT (để di chuyển con trỏ)
    wrefresh(win_option); 
    wrefresh(win_input); 
}

// Thêm tin nhắn vào cửa sổ CHAT (Đã sửa)
void ui_add_message(const char* message) {
    // In vào cửa sổ CON
    wprintw(win_chat, "%s\n", message);
    
    // KHÔNG CẦN VẼ LẠI BOX
    
    // Chỉ cần refresh cửa sổ CON và INPUT
    wrefresh(win_chat);
    wrefresh(win_input);
}

// Thêm tin nhắn vào cửa sổ LOG (Đã sửa)
void ui_add_log(const char* message) {
    // In vào cửa sổ CON
    wprintw(win_log, "[%s]\n", message);
    
    // KHÔNG CẦN VẼ LẠI BOX
    
    // Chỉ cần refresh cửa sổ CON và INPUT
    wrefresh(win_log);
    wrefresh(win_input);
}

// Cập nhật dòng status (Đã sửa)
void ui_update_status(const char* status) {
    // In status vào cửa sổ CHA (Border) của Chat
    mvwprintw(win_chat_border, 0, 2, " CHAT | Status: %.50s ", status);
    
    // Refresh cửa sổ CHA và INPUT
    wrefresh(win_chat_border);
    wrefresh(win_input);
}

// Lấy input (Sửa để bắt KEY_RESIZE)
// Trả về 0 nếu thành công, -1 nếu resize xảy ra
int ui_get_input(char* buffer, int max_len) {
	// Hiển thị prompt và đặt con trỏ
	wmove(win_input, 0, 0);
	wclrtoeol(win_input);
	mvwprintw(win_input, 0, 0, "> ");
	wmove(win_input, 0, 2);
	wrefresh(win_input);

	keypad(win_input, TRUE);

	int pos = 0;
	int ch;
	while (1) {
		ch = wgetch(win_input);

		if (ch == KEY_RESIZE) {
			// Đánh dấu đã resize và trả về lỗi để caller xử lý
			g_resized = 1;
			return -1;
		}

		if (ch == '\n' || ch == '\r') {
			// Kết thúc input
			if (pos < max_len) buffer[pos] = '\0';
			else buffer[max_len - 1] = '\0';
			return 0;
		}

		// Backspace handling (various codes)
		if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
			if (pos > 0) {
				pos--;
				// Xóa ký tự hiển thị
				mvwprintw(win_input, 0, 2 + pos, " ");
				wmove(win_input, 0, 2 + pos);
				wrefresh(win_input);
			}
			continue;
		}

		// Printable chars
		if (isprint(ch) && pos < max_len - 1) {
			buffer[pos++] = (char)ch;
			waddch(win_input, ch);
			wrefresh(win_input);
		}
	}
}

// Xóa input (Đã sửa)
void ui_clear_input() {
    // Xóa nội dung cửa sổ CON
    wclear(win_input);
    // Refresh cửa sổ CON
    wrefresh(win_input);
}
# Tên trình biên dịch
CC = gcc

# Cờ biên dịch: -g (thêm thông tin debug), -Wall (hiện tất cả cảnh báo)
CFLAGS = -g -Wall -pthread

# Cờ cho linker: -l (link thư viện)
LFLAGS_SERVER = -lsqlite3
LFLAGS_CLIENT = -lncurses

# Tên file thực thi
TARGET_SERVER = server/server
TARGET_CLIENT = client/client

# Các file .c của server (tạm thời)
SERVER_SRCS = server/server.c server/db_handler.c server/user_manager.c server/message_handler.c server/friend_manager.c
# Các file .c của client (tạm thời)
CLIENT_SRCS = client/client.c client/ui.c
all: $(TARGET_SERVER) $(TARGET_CLIENT)

# Quy tắc build server
$(TARGET_SERVER): $(SERVER_SRCS)
	$(CC) $(CFLAGS) $^ -o $@ $(LFLAGS_SERVER)

# Quy tắc build client
$(TARGET_CLIENT): $(CLIENT_SRCS)
	$(CC) $(CFLAGS) $^ -o $@ $(LFLAGS_CLIENT)

clean:
	rm -f $(TARGET_SERVER) $(TARGET_CLIENT) server/*.o client/*.o
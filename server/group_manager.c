#include "group_manager.h"
#include "db_handler.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

// helper to send packet to fd
static void send_packet_fd(int fd, MessageType type, const char* source, const char* target, const char* body) {
    if (fd <= 0) return;
    ChatPacket p;
    memset(&p, 0, sizeof(p));
    p.type = type;
    if (source) strncpy(p.source_user, source, MAX_USERNAME-1);
    if (target) strncpy(p.target_user, target, MAX_USERNAME-1);
    if (body) strncpy(p.body, body, MAX_BODY-1);
    write(fd, &p, sizeof(ChatPacket));
}

// find_session_by_username is in message_handler.c
extern ClientSession* find_session_by_username(const char* user, ClientSession* sessions);

void handle_create_group(int client_fd, ChatPacket* packet, ClientSession* sessions, sqlite3 *db) {
    const char* owner = packet->source_user;
    const char* group_name = packet->target_user;
    if (!group_name || strlen(group_name) == 0) {
        send_packet_fd(client_fd, MSG_TYPE_GROUP_RESPONSE, "Server", NULL, "Group name required.");
        return;
    }
    if (db_group_exists(db, group_name)) {
        send_packet_fd(client_fd, MSG_TYPE_GROUP_RESPONSE, "Server", NULL, "Group already exists.");
        return;
    }
    if (db_create_group(db, group_name, owner) == 0) {
        db_add_group_member(db, group_name, owner); // owner is member
        send_packet_fd(client_fd, MSG_TYPE_GROUP_RESPONSE, "Server", NULL, "Group created successfully.");
    } else {
        send_packet_fd(client_fd, MSG_TYPE_GROUP_RESPONSE, "Server", NULL, "Failed to create group.");
    }
}

void handle_join_group_request(int client_fd, ChatPacket* packet, ClientSession* sessions, sqlite3 *db) {
    const char* user = packet->source_user;
    const char* group_name = packet->target_user;
    if (!group_name || strlen(group_name) == 0) {
        send_packet_fd(client_fd, MSG_TYPE_GROUP_RESPONSE, "Server", NULL, "Group name required.");
        return;
    }
    if (!db_group_exists(db, group_name)) {
        send_packet_fd(client_fd, MSG_TYPE_GROUP_RESPONSE, "Server", NULL, "Group not found.");
        return;
    }
    if (db_add_group_member(db, group_name, user) != 0) {
        send_packet_fd(client_fd, MSG_TYPE_GROUP_RESPONSE, "Server", NULL, "Failed to join group (maybe already a member).");
        return;
    }
    // Notify group members that user joined
    typedef struct { ClientSession* sessions; const char* joiner; const char* group; } NotifyArg;
    NotifyArg arg = { sessions, user, group_name };
    void cb(void* a, const char* member) {
        NotifyArg* na = (NotifyArg*)a;
        ClientSession* s = find_session_by_username(member, na->sessions);
        if (s && s->fd != client_fd) {
            char body[MAX_BODY];
            snprintf(body, sizeof(body), "%s joined the group %s.", na->joiner, na->group);
            send_packet_fd(s->fd, MSG_TYPE_RECEIVE_GROUP_MESSAGE, na->joiner, na->group, body);
        }
    }
    db_get_group_members(db, group_name, (db_group_member_callback)cb, &arg);

    send_packet_fd(client_fd, MSG_TYPE_GROUP_RESPONSE, "Server", NULL, "Joined group.");
}

void handle_invite_to_group(int client_fd, ChatPacket* packet, ClientSession* sessions, sqlite3 *db) {
    // packet->target_user = username to invite
    // packet->body = group_name
    const char* inviter = packet->source_user;
    const char* invitee = packet->target_user;
    const char* group_name = packet->body;
    if (!invitee || !group_name) {
        send_packet_fd(client_fd, MSG_TYPE_GROUP_RESPONSE, "Server", NULL, "Invite requires username and group name (body).");
        return;
    }
    if (!db_group_exists(db, group_name)) {
        send_packet_fd(client_fd, MSG_TYPE_GROUP_RESPONSE, "Server", NULL, "Group not found.");
        return;
    }
    if (!db_is_group_owner(db, group_name, inviter)) {
        send_packet_fd(client_fd, MSG_TYPE_GROUP_RESPONSE, "Server", NULL, "Only owner can invite.");
        return;
    }
    if (db_add_group_member(db, group_name, invitee) == 0) {
        // notify invitee if online
        ClientSession* s = find_session_by_username(invitee, sessions);
        if (s) {
            char body[MAX_BODY];
            snprintf(body, sizeof(body), "You were added to group %s by %s", group_name, inviter);
            send_packet_fd(s->fd, MSG_TYPE_GROUP_RESPONSE, inviter, group_name, body);
        }
        send_packet_fd(client_fd, MSG_TYPE_GROUP_RESPONSE, "Server", NULL, "Invite processed (user added).");
    } else {
        send_packet_fd(client_fd, MSG_TYPE_GROUP_RESPONSE, "Server", NULL, "Failed to add user to group (maybe already a member).");
    }
}

void handle_remove_from_group(int client_fd, ChatPacket* packet, ClientSession* sessions, sqlite3 *db) {
    // packet->target_user = username to remove
    // packet->body = group_name
    const char* requester = packet->source_user;
    const char* target = packet->target_user;
    const char* group_name = packet->body;
    if (!target || !group_name) {
        send_packet_fd(client_fd, MSG_TYPE_GROUP_RESPONSE, "Server", NULL, "Remove requires username and group name in body.");
        return;
    }
    if (!db_group_exists(db, group_name)) {
        send_packet_fd(client_fd, MSG_TYPE_GROUP_RESPONSE, "Server", NULL, "Group not found.");
        return;
    }
    if (!db_is_group_owner(db, group_name, requester)) {
        send_packet_fd(client_fd, MSG_TYPE_GROUP_RESPONSE, "Server", NULL, "Only owner can remove members.");
        return;
    }
    if (db_remove_group_member(db, group_name, target) == 0) {
        // notify removed user if online
        ClientSession* s = find_session_by_username(target, sessions);
        if (s) {
            char body[MAX_BODY];
            snprintf(body, sizeof(body), "You were removed from group %s by %s", group_name, requester);
            send_packet_fd(s->fd, MSG_TYPE_GROUP_RESPONSE, "Server", group_name, body);
        }
        // notify remaining members
        typedef struct { ClientSession* sessions; const char* who; const char* group; } RemArg;
        RemArg r = { sessions, target, group_name };
        void cb2(void* a, const char* member) {
            RemArg* ra = (RemArg*)a;
            ClientSession* s2 = find_session_by_username(member, ra->sessions);
            if (s2) {
                char body2[MAX_BODY];
                snprintf(body2, sizeof(body2), "%s was removed from group %s.", ra->who, ra->group);
                send_packet_fd(s2->fd, MSG_TYPE_RECEIVE_GROUP_MESSAGE, "Server", ra->group, body2);
            }
        }
        db_get_group_members(db, group_name, (db_group_member_callback)cb2, &r);
        send_packet_fd(client_fd, MSG_TYPE_GROUP_RESPONSE, "Server", NULL, "Member removed.");
    } else {
        send_packet_fd(client_fd, MSG_TYPE_GROUP_RESPONSE, "Server", NULL, "Failed to remove member (not a member?).");
    }
}

void handle_leave_group(int client_fd, ChatPacket* packet, ClientSession* sessions, sqlite3 *db) {
    const char* leaver = packet->source_user;
    const char* group_name = packet->target_user;
    if (!group_name) {
        send_packet_fd(client_fd, MSG_TYPE_GROUP_RESPONSE, "Server", NULL, "Group name required.");
        return;
    }
    if (db_remove_group_member(db, group_name, leaver) != 0) {
        send_packet_fd(client_fd, MSG_TYPE_GROUP_RESPONSE, "Server", NULL, "Failed to leave group (maybe not a member).");
        return;
    }
    // announce to others
    typedef struct { ClientSession* sessions; const char* who; const char* group; } LArg;
    LArg la = { sessions, leaver, group_name };
    void cb(void* a, const char* member) {
        LArg* lar = (LArg*)a;
        ClientSession* s = find_session_by_username(member, lar->sessions);
        if (s) {
            char body[MAX_BODY];
            snprintf(body, sizeof(body), "%s left the group %s.", lar->who, lar->group);
            send_packet_fd(s->fd, MSG_TYPE_RECEIVE_GROUP_MESSAGE, lar->who, lar->group, body);
        }
    }
    db_get_group_members(db, group_name, (db_group_member_callback)cb, &la);
    send_packet_fd(client_fd, MSG_TYPE_GROUP_RESPONSE, "Server", NULL, "You left the group.");
}

// File-scope context for forwarding to members
typedef struct {
    ClientSession* sessions;
    const char* sender;
    const char* group;
    ChatPacket* pkt;
    sqlite3* db;
} GArg_forward;

// static callback used by db_get_group_members
static void member_forward_cb(void* arg, const char* member) {
    GArg_forward* g = (GArg_forward*)arg;
    if (!g || !member) return;
    if (strcmp(member, g->sender) == 0) return;
    ClientSession* s = find_session_by_username(member, g->sessions);
    if (s && s->fd != -1) {
        ChatPacket out;
        memset(&out, 0, sizeof(out));
        out.type = MSG_TYPE_RECEIVE_GROUP_MESSAGE;
        strncpy(out.source_user, g->sender, MAX_USERNAME-1);
        strncpy(out.target_user, g->group, MAX_USERNAME-1);
        strncpy(out.body, g->pkt->body, MAX_BODY-1);
        write(s->fd, &out, sizeof(ChatPacket));
    } else {
        // offline -> store as offline message for that member
        if (g->db) db_store_offline_message(g->db, g->sender, member, g->pkt->body);
    }
}

void handle_group_message(ChatPacket* packet, ClientSession* sessions, sqlite3 *db) {
    const char* group_name = packet->target_user;
    const char* sender = packet->source_user;

    // 1. Basic validation
    if (!group_name || strlen(group_name) == 0) {
        ClientSession* s = find_session_by_username(sender, sessions);
        if (s) send_packet_fd(s->fd, MSG_TYPE_GROUP_RESPONSE, "Server", NULL, "Missing group name.");
        return;
    }

    // 2. Check group existence
    if (!db_group_exists(db, group_name)) {
        ClientSession* s = find_session_by_username(sender, sessions);
        if (s) send_packet_fd(s->fd, MSG_TYPE_GROUP_RESPONSE, "Server", NULL, "Group not found.");
        return;
    }

    // 3. Check membership: only group members may send messages
    if (!db_is_group_member(db, group_name, sender)) {
        ClientSession* s = find_session_by_username(sender, sessions);
        if (s) send_packet_fd(s->fd, MSG_TYPE_GROUP_RESPONSE, "Server", NULL, "You are not a member of this group.");
        return;
    }

    // 4. Broadcast to all group members except sender (and store offline for offline members)
    GArg_forward ga;
    ga.sessions = sessions;
    ga.sender = sender;
    ga.group = group_name;
    ga.pkt = packet;
    ga.db = db;

    db_get_group_members(db, group_name, (db_group_member_callback)member_forward_cb, &ga);
}

// --- NEW: helpers to build list responses ---

typedef struct {
    char acc[MAX_BODY];
} GroupListBuilder;

static int group_list_cb(void* arg, const char* group_name) {
    GroupListBuilder* b = (GroupListBuilder*)arg;
    if (strlen(b->acc) + strlen(group_name) + 3 < sizeof(b->acc)) {
        if (b->acc[0] != '\0') strcat(b->acc, ", ");
        strcat(b->acc, group_name);
    }
    return 0;
}

void handle_group_list_joined(int client_fd, ChatPacket* packet, ClientSession* sessions, sqlite3 *db) {
    // packet->source_user is the user; return groups this user joined
    GroupListBuilder b; b.acc[0] = '\0';
    db_get_groups_for_user(db, packet->source_user, group_list_cb, &b);

    ChatPacket resp; memset(&resp, 0, sizeof(resp));
    resp.type = MSG_TYPE_GROUP_LIST_RESPONSE;
    strncpy(resp.source_user, "Server", MAX_USERNAME-1);
    if (b.acc[0] == '\0') {
        snprintf(resp.body, MAX_BODY, "You have not joined any groups.");
    } else {
        snprintf(resp.body, MAX_BODY, "Joined groups: %s", b.acc);
    }
    write(client_fd, &resp, sizeof(ChatPacket));
}

void handle_group_list_all(int client_fd, ChatPacket* packet, ClientSession* sessions, sqlite3 *db) {
    GroupListBuilder b; b.acc[0] = '\0';
    db_get_all_groups(db, group_list_cb, &b);

    ChatPacket resp; memset(&resp, 0, sizeof(resp));
    resp.type = MSG_TYPE_GROUP_LIST_RESPONSE;
    strncpy(resp.source_user, "Server", MAX_USERNAME-1);
    if (b.acc[0] == '\0') {
        snprintf(resp.body, MAX_BODY, "No groups available.");
    } else {
        snprintf(resp.body, MAX_BODY, "Available groups: %s", b.acc);
    }
    write(client_fd, &resp, sizeof(ChatPacket));
}

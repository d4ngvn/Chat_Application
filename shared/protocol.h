#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>

#define MAX_USERNAME 32
#define MAX_BODY 1024

// Message types used by client and server.
// Extended to include registration, login, group/private messages, friend and group operations,
// and server responses/notifications.
typedef enum {
    MSG_TYPE_UNKNOWN = 0,

    // Client -> Server
    MSG_TYPE_REGISTER_REQUEST,        // client requests registration
    MSG_TYPE_LOGIN_REQUEST,           // client requests login
    MSG_TYPE_LOGOUT_REQUEST,          // client requests logout

    // Messaging
    MSG_TYPE_GROUP_MESSAGE,           // client sends a group message
    MSG_TYPE_PRIVATE_MESSAGE,         // client sends a private message
    MSG_TYPE_SEND_MESSAGE,            // generic send (legacy / alias)

    // Friend workflow
    MSG_TYPE_FRIEND_REQUEST,          // send friend request
    MSG_TYPE_FRIEND_ACCEPT,           // accept friend request (legacy name)
    MSG_TYPE_ACCEPT_FRIEND_REQUEST,   // accept friend request (alternate name)
    MSG_TYPE_FRIEND_DECLINE,          // decline friend request
    MSG_TYPE_FRIEND_UNFRIEND,         // unfriend
    MSG_TYPE_FRIEND_LIST_REQUEST,     // request friend list

    // Group workflow
    MSG_TYPE_CREATE_GROUP_REQUEST,
    MSG_TYPE_JOIN_GROUP_REQUEST,
    MSG_TYPE_INVITE_TO_GROUP_REQUEST,

    // Server -> Client
    MSG_TYPE_REGISTER_SUCCESS,
    MSG_TYPE_REGISTER_FAIL,
    MSG_TYPE_LOGIN_SUCCESS,
    MSG_TYPE_LOGIN_FAIL,

    // Delivery notifications
    MSG_TYPE_RECEIVE_PRIVATE,         // server delivers a private message
    MSG_TYPE_RECEIVE_GROUP_MESSAGE,   // server delivers a group message
    MSG_TYPE_RECEIVE_GROUP_MESSAGE_LEGACY, // legacy alias (if needed)

    // Presence / offline
    MSG_TYPE_ONLINE_LIST_UPDATE,      // server updates online list
    MSG_TYPE_SEND_OFFLINE_MSG,        // server sends stored offline message(s)

    // Friend-specific server messages
    MSG_TYPE_FRIEND_REQUEST_INCOMING, // notify receiver about incoming request
    MSG_TYPE_FRIEND_UPDATE,           // generic friend-related updates (success/fail/status)
    MSG_TYPE_FRIEND_LIST_RESPONSE,    // server replies with friend list + status
    MSG_TYPE_FRIEND_REQUEST_RESPONSE, // explicit friend request response (optional)

    // Group responses
    MSG_TYPE_GROUP_RESPONSE,          // generic group operation response

    // Expand below as needed...
} MessageType;

// Simple packet shared by client and server.
// Keep size deterministic and simple for read/write on sockets.
typedef struct {
    MessageType type;
    char source_user[MAX_USERNAME]; // origin username
    char target_user[MAX_USERNAME]; // target username (for friend commands, messages, etc.)
    char body[MAX_BODY];            // text body / notification
} ChatPacket;

#endif // PROTOCOL_H
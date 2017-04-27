// SampSharp
// Copyright 2017 Tim Potze
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "server.h"
#include "platforms.h"
#include "version.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* platform specifics */
#if SAMPSHARP_WINDOWS
#  define vsnprintf vsprintf_s
#endif

#define LEN_PRINT_BUFFER    (1024)
#define LEN_NETBUF          (20000)

/* receive */
#define CMD_PING            (0x01) /* request a pong */
#define CMD_PRINT           (0x02) /* print data */
#define CMD_RESPONSE        (0x03) /* response to public call */
#define CMD_RECONNECT       (0x04) /* expect client to reconnect */
#define CMD_REGISTER_CALL   (0x05) /* register a public call */
#define CMD_FIND_NATIVE     (0x06) /* return native id */
#define CMD_INVOKE_NATIVE   (0x07) /* invoke a native */
#define CMD_START           (0x08) /* start sending messages */

/* send */
#define CMD_TICK            (0x11) /* server tick */
#define CMD_PONG            (0x12) /* ping reply */
#define CMD_PUBLIC_CALL     (0x13) /* public call */
#define CMD_REPLY           (0x14) /* reply to find native or native invoke */
#define CMD_ANNOUNCE        (0x15) /* announce with version */

/* status marcos */
#define STATUS_SET(v) status_ = (status)(status_ | (v))
#define STATUS_UNSET(v) status_ = (status)(status_ & ~(v))
#define STATUS_ISSET(v) ((status_ & (v)) == (v))

#pragma region Constructors and loading

/** initializes and allocates required memory for the server instance */
server::server(communication_server *communication) :
    callbacks_(callbacks_map(this)),
    communication_(communication),
    natives_(natives_map(this)),
    status_(status_none) {

    buf_ = new uint8_t[LEN_NETBUF];
}

/** frees memory allocated by this instance */
server::~server() {
    if (communication_) {
        communication_->disconnect();
    }
    delete[] buf_;
}

/** starts the comms server */
void server::start() {
    communication_->setup(this);
}

#pragma endregion

#pragma region Logging

/** prints text to the output */
void server::print(const char *format, ...) {
    va_list args;
    va_start(args, format);
    sampgdk_vlogprintf(format, args);
    va_end(args);
}

/** log error */
void server::log_error(const char * format, ...) {
    va_list args;
    va_start(args, format);
    vlog("ERROR", format, args);
    va_end(args);
}

/** log debug */
void server::log_debug(const char * format, ...) {
#if (defined DEBUG) || (defined _DEBUG)
    va_list args;
    va_start(args, format);
    vlog("DEBUG", format, args);
    va_end(args);
#endif
}

/** log info */
void server::log_info(const char * format, ...) {
    va_list args;
    va_start(args, format);
    vlog("INFO", format, args);
    va_end(args);
}

/** log a message */
void server::vlog(const char* prefix, const char *format, va_list args) {
    char buffer[LEN_PRINT_BUFFER];
    vsnprintf(buffer, LEN_PRINT_BUFFER, format, args);
    buffer[LEN_PRINT_BUFFER - 1] = '\0';

    sampgdk_logprintf("[SampSharp:%s] %s", prefix, buffer);
}

#pragma endregion

#pragma region Getters

/** a value indicating whether the client is connected */
bool server::is_client_connected() {
    return communication_->is_connected() && STATUS_ISSET(status_client_connected);
}

#pragma endregion

#pragma region Commands

CMD_DEFINE(cmd_ping) {
    communication_->send(CMD_PONG, 0, NULL);
}

CMD_DEFINE(cmd_print) {
    print("%s", buf);
}

CMD_DEFINE(cmd_register_call) {
    log_debug("Register call %s", buf);
    callbacks_.register_buffer(buf);
}

CMD_DEFINE(cmd_find_native) {
    log_debug("Find native w/%d data", buflen);
    int32_t handle =  natives_.get_handle(buf);
    communication_->send(CMD_RESPONSE, sizeof(int32_t), (uint8_t *)&handle);
}

CMD_DEFINE(cmd_invoke_native) {
    log_debug("Invoke native w/%d data", buflen);
    uint32_t txlen = LEN_NETBUF;
    natives_.invoke(buf, buflen, buf_, &txlen);

    log_debug("Sending response to native w/%d data", txlen);
    communication_->send(CMD_RESPONSE, txlen, buf_);
}

CMD_DEFINE(cmd_reconnect) {
    log_info("The gamemode has is reconnecting.");
    STATUS_SET(status_client_reconnecting);
    disconnect(NULL, true);
}

CMD_DEFINE(cmd_start) {
    log_info("The gamemode has started.");
    STATUS_SET(status_client_started);
    uint8_t type = buflen == 0 ? 0 : buf[0];

    switch (type) {
    case 0:
        log_debug("Using 'none' start method");
        break;
    case 1:
        log_debug("Using 'gmx' start method");
        if (STATUS_ISSET(status_server_received_init)) {
            log_debug("Sending gmx to attach game mode.");
            sampgdk_SendRconCommand("gmx");
        }
        break;
    case 2:
        log_debug("Using 'fake gmx' start method");
        if (STATUS_ISSET(status_server_received_init)) {
            STATUS_SET(status_client_received_init);

            cell params = 0;
            uint32_t len = callbacks_.fill_call_buffer(NULL, "OnGameModeInit",
                &params, buf_, LEN_NETBUF);
            uint8_t *response = NULL;

            if (len == 0) {
                break;
            }

            /* send */
            communication_->send(CMD_PUBLIC_CALL, len, buf_);

            /* receive */
            if (!cmd_receive_unhandled(&response, &len) || !response || 
                len == 0) {
                log_error("Received no response to callback OnGameModeInit.");
                break;
            }

            delete[] response;
        }
        break;
    default:
        log_error("Invalid game mode start mode");
        break;
    }
}

#pragma endregion

#pragma region Communication

/* try to let a client connect */
bool server::connect() {
    if (communication_->is_connected()) {
        return true;
    }

    if (!communication_->is_ready() && !communication_->setup(this)) {
        return false;
    }

    if (!communication_->connect()) {
        return false;
    }

    STATUS_SET(status_client_connected);

    if (STATUS_ISSET(status_client_reconnecting)) {
        log_info("Client reconnected.");
    }
    else {
        log_info("Connected to client.");
        cmd_send_announce();
    }

    STATUS_UNSET(status_client_reconnecting);
    return true;
}

/** sends the server annoucement to the client */
void server::cmd_send_announce() {
    /* send version */
    uint32_t info[2];
    info[0] = PLUGIN_PROTOCOL_VERSION;
    info[1] = PLUGIN_VERSION;

    communication_->send(CMD_ANNOUNCE, sizeof(info), (uint8_t *)info);

    log_info("Server annoucement sent.");
}

/** disconnects from client */
void server::disconnect(const char *context, bool expected) {
    if (!is_client_connected()) {
        return;
    }
    
    if (!expected) {
        if (!context) {
            context = "";
        }
        log_error("Unexpected disconnect of client. %s", context);

        STATUS_UNSET(status_client_started);
        natives_.clear();
        callbacks_.clear();
        
    }
    else {
        log_info("Client disconnected.");
    }
    
    /* disconnect and close */
    communication_->disconnect();

    /* re-setup */
    communication_->setup(this);

    STATUS_UNSET(status_client_connected);
}

/** receives a single command if available */
cmd_status server::cmd_receive_one(uint8_t **response, uint32_t *len) {
    uint8_t command;
    uint32_t command_len = LEN_NETBUF;

    assert(response);
    assert(len);
    assert(sizeof(unsigned long) == sizeof(uint32_t));

    if (!connect()) {
        return conn_dead;
    }

    cmd_status stat = communication_->receive(&command, buf_, &command_len);

    if (stat == conn_dead || stat == no_cmd) {
        return stat;
    }
    
    return cmd_process(command, buf_, command_len, response, len);
}

/** receives commands until an unhandled command appears */
bool server::cmd_receive_unhandled(uint8_t **response, uint32_t *len) {
    assert(response);
    assert(len);

    cmd_status stat;

    do {
        *response = NULL;
        *len = 0;
        stat = cmd_receive_one(response, len);
    } while (stat == handled || 
        stat == no_cmd);

    return stat == unhandled;
}

/** processes a command */
cmd_status server::cmd_process(uint8_t cmd, uint8_t *buf, uint32_t buflen, 
    uint8_t **resp, uint32_t *resplen) {
#define MAP_COMMAND(a,b) case a:b(buf, buflen);return handled

    switch (cmd) {
        /* mapped commands */
        MAP_COMMAND(CMD_PING, cmd_ping);
        MAP_COMMAND(CMD_PRINT, cmd_print);
        MAP_COMMAND(CMD_REGISTER_CALL, cmd_register_call);
        MAP_COMMAND(CMD_FIND_NATIVE, cmd_find_native);
        MAP_COMMAND(CMD_INVOKE_NATIVE, cmd_invoke_native);
        MAP_COMMAND(CMD_RECONNECT, cmd_reconnect);
        MAP_COMMAND(CMD_START, cmd_start);

        /* unmapped commands (unhandled) */
        case CMD_RESPONSE:
        default:
            if (buflen > 0) {
                *resp = new uint8_t[buflen];
                memcpy(*resp, buf, buflen);
                *resplen = buflen;
            }
            return unhandled;
    }

#undef MAP_COMMAND
}

#pragma endregion

/** called when a public call is send from the server */
void server::public_call(AMX *amx, const char *name, cell *params, cell *retval) {
    log_debug("Received public call %s (%d)", name, params[0]);

    bool is_gmi = !strcmp(name, "OnGameModeInit");
    bool is_gme = !is_gmi && !strcmp(name, "OnGameModeExit");

    if (is_gmi) {
        STATUS_SET(status_server_received_init);
    }
    else if (is_gme) {
        STATUS_UNSET(status_server_received_init);
    }

    if (!is_client_connected() || !STATUS_ISSET(status_client_started)) {
        return;
    }

    if (is_gmi) {
        STATUS_SET(status_client_received_init);
    }
    else if (!STATUS_ISSET(status_client_received_init)) {
        return;
    }

    uint32_t len = callbacks_.fill_call_buffer(amx, name, params, buf_, 
        LEN_NETBUF);
    uint8_t *response = NULL;

    if (len == 0) {
        return;
    }

    /* send */
    communication_->send(CMD_PUBLIC_CALL, len, buf_);
    log_debug("Send public call to game mode.");

    /* receive */
    if(!cmd_receive_unhandled(&response, &len) || !response || len == 0) {
        log_error("Received no response to callback %s.", name);
        return;
    }
    log_debug("Recevied %d response from game mode.", len);

    if (len >= 5 && response[0] && retval) {
        /* get return value */
        *retval = *((uint32_t *)(response + 1));
    }

    delete[] response;
}

/** called when a server tick occurs */
void server::tick() {
    if (is_client_connected() && STATUS_ISSET(status_client_started |
        status_client_received_init)) {
        communication_->send(CMD_TICK, 0, NULL);
    }

    uint8_t *response = NULL;
    uint32_t len;
    cmd_status stat;

    /* receive calls from the game mode client */
    do {
        stat = cmd_receive_one(&response, &len);

        if (response) {
            log_error("Unhandled response in tick.");
            delete[] response;
        }
    } while (stat != no_cmd && stat != conn_dead);
}

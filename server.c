#include <stdio.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <memory.h>
#include <signal.h>
#include <sys/wait.h>

#include <sys/ioctl.h>

#ifdef USE_TPL
#include "tpl.h"
#endif

#ifdef USE_LUA
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
#define LUA_SOURCE "commands.lua"
#endif

#define MAX_COMMAND 256
#define COMMAND_START '$'
#define COMMAND_END '*'
#define UNKNOWN_COMMAND "ERR>Unknown Command\n"
#define ERROR_COMMAND "ERR>Command returned an error\n"

#define DEBUG 0

#ifdef USE_LUA
static lua_State *L;
#endif

void handle_connection(int);

void sigchld_handler(int s) {
    while(waitpid(-1, NULL, WNOHANG) > 0);
}

struct command_def {
    const char* request;
    enum type { 
        builtin, 
#ifdef USE_LUA
        lua,
#endif
        shell } type;
    const char* responder_command;
    int (*responder) (char* request, char *buffer, int buf_len);
};

int helloworld (char *request, char *buffer, int buf_len) {
    snprintf(buffer, buf_len, "You said \"%s\" so I say \"Hello world!\"\n", request);
    return strlen(buffer);
}

#ifdef USE_TPL
int testbinary(char *request, char* buffer, int buf_len) {
    tpl_node *tn;
    int id = 0;
    int sz = 0;
    struct struct_type {
        int x;
        char *string;
        char c;
    } local_struct = {
        .x = 1024,
        .string = "This is a string\n",
        .c = 'a'
    };

    tn = tpl_map("S(isc)", &local_struct);
    tpl_pack(tn, 0);
    tpl_dump(tn, TPL_GETSIZE, &sz);
    tpl_dump(tn, TPL_MEM|TPL_PREALLOCD, buffer, buf_len);
    tpl_free(tn);
    if (DEBUG) fprintf(stderr, "testbinary: returning %d\n", sz);
    return sz;
}
#endif

/* Add new commands here */
static struct command_def command_defs[] = {
    {
        .request = "ifconfig",
        .type = shell,
        .responder_command = "ifconfig -a",
        .responder = NULL
    },
    {
        .request = "reassociate",
        .type = shell,
        .responder_command = "iwpriv ath0 doth_reassoc 1",
        .responder  = NULL
    },
#ifdef USE_LUA
    {
        .request = "testlua",
        .type = lua,
        .responder_command = "testlua",
        .responder = NULL
    },
#endif
#ifdef USE_TPL
    {
        .request = "testbinary",
        .type = builtin,
        .responder_command = "",
        .responder = testbinary
    },
#endif
    {
        .request = "helloworld",
        .type = builtin,
        .responder_command = "",
        .responder = helloworld
    }
};

#define NUM_COMMANDS (sizeof(command_defs) / sizeof(struct command_def))

static void handle_shell(char * raw_request, struct command_def *request, int sock) {
     char buffer[256];
     FILE *command = NULL;
     char *retval = 0;
     int n = 0;

     if (DEBUG) fprintf(stderr, "Piping output from %s", request->responder_command);

     bzero(buffer, sizeof(buffer));

     command = popen(request->responder_command, "r");

     retval = fgets(buffer, sizeof(buffer), command);
     while (retval != NULL) {
         n = write(sock, buffer, strlen(buffer));
         if (n < 0) { 
             pclose(command); 
             fprintf(stderr, "Error writing to socket\n"); 
         }
         retval = fgets(buffer, sizeof(buffer), command);
     } 
}

static void handle_builtin(char * raw_request, struct command_def *request, int sock) {
     char buffer[1024];
     int n = 0;
     int sz = 0;

     bzero(buffer, sizeof(buffer));

     sz = (request->responder) (raw_request, buffer, sizeof(buffer));

     if (DEBUG) fprintf(stderr, "Buffer is %s\n", buffer);
     
     if (sz < 0) {
         write(sock, ERROR_COMMAND, sizeof(ERROR_COMMAND));
         return;
     }         

     n = write(sock, buffer, sz);
     if (n < 0) { 
         fprintf(stderr, "Error writing to socket\n"); 
     }
}

#ifdef USE_LUA
static void handle_lua(char * raw_request, struct command_def *request, int sock) {
     char buffer[1024];
     int n = 0;
     int sz = 0;
     int result = 0;

     bzero(buffer, sizeof(buffer));

     lua_getfield(L, LUA_GLOBALSINDEX, request->responder_command);
     lua_pushstring(L, raw_request);
     /* what are these magic numbers?? */
     lua_call(L, 1, 1);
     result = lua_toboolean(L, 1);
     lua_pop(L, 1);
     return;
}
#endif

static void handle_command(char* request, int sock) {
    int i = 0;
    int handled = 0;
    if (DEBUG) fprintf(stderr, "NUM_COMMANDS is %ld, command is %s\n", NUM_COMMANDS, request);
    for (i = 0; i < NUM_COMMANDS; i++) {
        if (strcmp(request, command_defs[i].request) == 0) {
            switch(command_defs[i].type) {
            case builtin:
                handle_builtin(request, &command_defs[i], sock);
                handled = 1;
                break;
#ifdef USE_LUA
            case lua:
                handle_lua(request, &command_defs[i], sock);
                handled = 1;
                break;
#endif
            case shell:
                handle_shell(request, &command_defs[i], sock);
                handled = 1;
                break;
            default:
                fprintf(stderr, "Unknown command type for %s\n", command_defs[i].request);
                break;
            }
        }
    }

    if (!handled) {
        write(sock, UNKNOWN_COMMAND, strlen(UNKNOWN_COMMAND));
    }

}

/* FIXME - this is called when we run out of space when parsing a command */
static void handle_error(char *command) {
    fprintf(stderr, "Command too long\n");
}

static char current_command[MAX_COMMAND+3];
static char *current_command_index = NULL;
static int currently_parsing_command = 0;

static int parse_command(const char* raw_request, int sock) {
    const char *p = NULL;

    for (p = raw_request; *p != '\0'; p++) {
        switch (*p) {
        case COMMAND_START:
            if (DEBUG) fprintf(stderr, "COMMAND_START (%d)\n", currently_parsing_command);
            memset(current_command, '\0', sizeof(current_command));
            currently_parsing_command = 1;
            current_command_index = current_command;
            break;

        default:
            if (DEBUG) fprintf(stderr, "%c (%d)\n", *p, currently_parsing_command);
            if (!currently_parsing_command) break;

            if ((current_command_index - current_command) < 
                    sizeof(current_command)) {
                *current_command_index++ = *p;
                if (DEBUG) fprintf(stderr, "current_command = %s\n", current_command);
            } else {
                handle_error(current_command);

                /* and bail and clear the command */
                memset(current_command, 0, sizeof(current_command));
                currently_parsing_command = 0;
                current_command_index = current_command;
            }
            break;

        case COMMAND_END:
            if (DEBUG) fprintf(stderr, "COMMAND_END (%d)\n", currently_parsing_command);
            if (!currently_parsing_command) break;

            handle_command(current_command, sock); 
            break;
        }
    }
}


void error(char *msg, int do_exit)
{
    perror(msg);
    if (do_exit) exit(1);
}

int main(int argc, char *argv[])
{
    int sockfd, newsockfd, portno, clilen, pid;
    struct sockaddr_in serv_addr, cli_addr;
    struct sigaction sa;
    int yes = 1;

    if (argc < 2) {
        fprintf(stderr,"usage: %s <port-number>\n", argv[0]);
        exit(1);
    }
    portno = atoi(argv[1]);

    memset(current_command, 0, sizeof(current_command));
    currently_parsing_command = 0;
    current_command_index = current_command;

    /* Reaping child processes stops zombie processes */
    sa.sa_handler = sigchld_handler; 
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) 
        error("Error setting signal handler", 1);

#ifdef USE_LUA
    L = luaL_newstate();
    luaL_openlibs(L);

    luaL_dofile(L, LUA_SOURCE);
#endif

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) 
        error("Error opening socket", 1);

    /* Setting REUSEADDR lets a quickly respawned 
     * server still use the same port */
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1) 
        error("Error settting sockoptions", 1);

    bzero((char *) &serv_addr, sizeof(serv_addr));

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(portno);

    if (bind(sockfd, (struct sockaddr *) &serv_addr,
                sizeof(serv_addr)) < 0) 
        error("Error binding address", 1);

    listen(sockfd,5);
    clilen = sizeof(cli_addr);
    while (1) {
        newsockfd = accept(sockfd, 
                (struct sockaddr *) &cli_addr, &clilen);
        if (newsockfd < 0) 
            error("Error on accept", 1);
        pid = fork();
        if (pid < 0)
            error("Error on fork", 1);
        if (pid == 0)  {
            close(sockfd);
            handle_connection(newsockfd);
            exit(0);
        }
        else close(newsockfd);
    } 
    return 0;
}

void handle_connection (int sock)
{
    int n;
    char buffer[128];
    FILE *command; 
    char *retval = NULL;
    
    bzero(buffer, sizeof(buffer));

    while (1) {

        n = read(sock, buffer, sizeof(buffer)-1);
        if (n < 0) {
            error ("Error reading from socket", 0);
            break;
        }
        if (n == 0) break;
            
        if (DEBUG) fprintf(stderr, "Read %d\n", n);

        parse_command(buffer, sock);

    }

    if (DEBUG) printf("Connection closed\n");

}

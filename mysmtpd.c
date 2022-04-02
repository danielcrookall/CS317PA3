#include "netbuffer.h"
#include "mailuser.h"
#include "server.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/utsname.h>
#include <ctype.h>

#define MAX_LINE_LENGTH 1024
#define SERVER_INITIAL_STATE 0
#define CLIENT_INITIAL_STATE 1
#define MESSAGE_INITIALIZATION_STATE 2
#define RCPT_STATE 3
#define DATA_STATE 4


#define SYNTAX_ERROR "501 Syntax error in parameters or arguments\r\n"
#define BAD_SEQUENCE "503 Bad sequence of commands\r\n"



static void handle_client(int fd);

int main(int argc, char *argv[]) {

    if (argc != 2) {
        fprintf(stderr, "Invalid arguments. Expected: %s <port>\n", argv[0]);
        return 1;
    }

    run_server(argv[1], handle_client);

    return 0;
}

// helper to check if MAIL FROM AND RCPT to correctly wrap email in angular brackets < >
int isValidArgs(char *msg){
    int strLength = strlen(msg);

    if(strchr(msg,'<') == NULL){
        return 0;
    }

    if(strchr(msg,'>') == NULL){
        return 0;
    }

    return 1;
}

void handle_client(int fd) {

    int state = SERVER_INITIAL_STATE;
    char template[] = "tempstorage-fileXXXXXX";

    char recvbuf[MAX_LINE_LENGTH + 1];
    net_buffer_t nb = nb_create(fd, MAX_LINE_LENGTH);

    struct utsname my_uname;
    int err;
    err = uname(&my_uname);

    user_list_t userList = create_user_list();
    int isUserListEmpty = 1; // flag to determine if a RCPT has been added to the userlist
    int tempFile;

    if (err != 0) {
        err = send_formatted(fd, "220\r\n");
        if (err < 0) {
            nb_destroy(nb);
            return;
        }
    } else {
        err = send_formatted(fd, "220 %s Simple Mail Transfer Service Ready\r\n", my_uname.nodename);
    }
    /* TO BE COMPLETED BY THE STUDENT */

    state = CLIENT_INITIAL_STATE;

    while ((err = nb_read_line(nb, recvbuf)) > 0) {

        if (strcasecmp(recvbuf, "QUIT\n") == 0 || strncasecmp(recvbuf, "QUIT", 5) == 0 ||
            strncasecmp(recvbuf, "QUIT\r\n", 6) == 0) {
            err = send_formatted(fd, "221 OK\r\n");

            nb_destroy(nb);
            destroy_user_list(userList);
            return;
        }

        if (strncasecmp(recvbuf, "NOOP", 5) == 0 || strncasecmp(recvbuf, "NOOP\r\n", 6) == 0) {
            err = send_formatted(fd, "250 OK\r\n");
            continue;
        }

        if (strncasecmp(recvbuf, "VRFY", 4) == 0 || strncasecmp(recvbuf, "VRFY\r\n", 6) == 0) {
            char *parts[50];
            int argsCount = split(recvbuf, &parts);
            if(argsCount != 2){
                err = send_formatted(fd, "501 invalid args to vrfy\r\n");
                continue;
            }
//            if(parts[2] != NULL || parts[1] == NULL){
//                err = send_formatted(fd, "551 invalid args to vrfy\r\n");
//                continue;
//            }

            if (is_valid_user(parts[1], NULL)) { // user is valid
                err = send_formatted(fd, "250 %s\r\n", parts[1]);

            } else { // invalid user
                err = send_formatted(fd, "550 %s does not exist.\r\n", parts[1]);
            }
            continue;
        }

        if (strncasecmp(recvbuf, "RSET", 4) == 0 || strncasecmp(recvbuf, "RSET\r\n", 6) == 0) {
            err = send_formatted(fd, "250 OK\r\n");
            destroy_user_list(userList);
            userList = create_user_list();
            state = MESSAGE_INITIALIZATION_STATE;
            continue;
        }

        switch (state) {
            case CLIENT_INITIAL_STATE: // client identification with helo or elho
                if (strncasecmp(recvbuf, "HELO", 4) == 0 || strncasecmp(recvbuf, "HELO\r\n", 6) == 0 ||
                    strncasecmp(recvbuf, "EHLO", 4) == 0 || strncasecmp(recvbuf, "EHLO\r\n", 6) == 0) {
                    char *parts[50];
                    split(recvbuf, &parts);
                    err = send_formatted(fd, "250 %s greets %s \r\n", my_uname.nodename, parts[1]);
                    state = MESSAGE_INITIALIZATION_STATE;
                } else { // hello command not issued
                    err = send_formatted(fd, BAD_SEQUENCE);
                }
                break;

            case MESSAGE_INITIALIZATION_STATE :  //expect MAIL FROM:
                if (strncasecmp(recvbuf, "MAIL FROM", 9) == 0) {
                    if(!isValidArgs(recvbuf)){
                        err = send_formatted(fd, SYNTAX_ERROR);
                    } else {
                        err = send_formatted(fd, "250 OK\r\n");
                        state = RCPT_STATE;
                    }

                } else if (!strncasecmp(recvbuf, "EHLO",5) || !strncasecmp(recvbuf, "VRFY",5) ||
                        !strncasecmp(recvbuf, "DATA\r\n",6) || !strncasecmp(recvbuf, "RCPT TO:",8)
                        ) {
                    err = send_formatted(fd, BAD_SEQUENCE);
                } else {
                    err = send_formatted(fd, SYNTAX_ERROR);
                }

                break;

            case RCPT_STATE: // we're going to expect either RCPT state or DATA here because you can add arbitrary number of RCPT
                if (strncasecmp(recvbuf, "RCPT TO", 7) == 0) {
                    char src[50];
                    char openAngle = '<';
                    char closedAngle = '>';
                    char *ret;
                    if(!isValidArgs(recvbuf)){
                        err = send_formatted(fd, SYNTAX_ERROR);
                        continue;
                    }
                    ret =  strchr (recvbuf,openAngle); // searches for first occurrence of < and returns a pointer to it
                    strncpy(src, ret+1, 50);
                    char *ret2;
                    ret2 = strchr(src, closedAngle);
                    *ret2 = '\0';
                   // src is now the email address without angle brackets
                    if(is_valid_user(src,NULL)){
                        add_user_to_list(&userList,src);
                        isUserListEmpty = 0;
                        err = send_formatted(fd,"250 OK\r\n");
                    } else { // invalid user
                        err = send_formatted(fd,"550 No such user here\r\n");
                    }

                } else if (!strncasecmp(recvbuf, "DATA", 4) || !strncasecmp(recvbuf, "DATA\r\n", 6)){
                    if(!isUserListEmpty) {
                        err = send_formatted(fd, "354 Start mail input; end with <CRLF>.<CRLF>\r\n");
                        tempFile = mkstemp(template);
                        state = DATA_STATE;
                    } else { // user list empty
                        err = send_formatted(fd, BAD_SEQUENCE);

                    }


                } else { // invalid command in this state
                    err = send_formatted(fd, SYNTAX_ERROR);
                }

                break;

            case DATA_STATE: //user is sending message
                if(!strncasecmp(recvbuf,".\r\n",3) || !strncasecmp(recvbuf, ".\n", 2)){ // message consists of a single .
                    save_user_mail(template,userList);
                    destroy_user_list(userList);
                    userList = create_user_list();
                    state = MESSAGE_INITIALIZATION_STATE;
                    send_formatted(fd, "250 OK\r\n");
                    unlink(template);

                } else { //message doesn't consist of a single .
                    char *msg;
                    if(recvbuf[0] == '.'){ // must ignore first period.
                        msg = recvbuf+1;
                    } else {
                        msg = recvbuf;
                    }
                    write(tempFile, msg, strlen(msg));
                }
                break;
        }
    }





// smaller than 0, temp variable for user list
// while
// nb_read_line(nb, recvbuf)

        nb_destroy(nb);

}
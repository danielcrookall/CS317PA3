#include "netbuffer.h"
#include "mailuser.h"
#include "server.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/utsname.h>

#define MAX_LINE_LENGTH 1024
#define AUTHORIZATION_STATE 0
#define PASS_STATE 1
#define TRANSACTION_STATE 2
#define UPDATE_STATE 3


static void handle_client(int fd);



// RFC 1939
int main(int argc, char *argv[]) {
  
    if (argc != 2) {
	fprintf(stderr, "Invalid arguments. Expected: %s <port>\n", argv[0]);
	return 1;
    }
  
    run_server(argv[1], handle_client);
  
    return 0;
}

void handle_client(int fd) {
    int resp;
    int state;
    mail_list_t mailList = NULL;

    // first need tor check greeting
  
    char recvbuf[MAX_LINE_LENGTH + 1];
    net_buffer_t nb = nb_create(fd, MAX_LINE_LENGTH);
    struct utsname my_uname;
    resp = uname(&my_uname);
    char user[MAX_USERNAME_SIZE];
  
    /* TO BE COMPLETED BY THE STUDENT */
    if (resp != 0) {
        resp = send_formatted(fd, "220\r\n");
        if (resp < 0) {
            nb_destroy(nb);
            return;
        }
    } else {
        resp = send_formatted(fd, "+OK %s POP3 server ready\r\n", my_uname.nodename);
        state = AUTHORIZATION_STATE;

        if (resp < 0) {
            nb_destroy(nb);
            return;
        }
    }



    while((resp = nb_read_line(nb, recvbuf)) > 0){
        switch (state) {
            case AUTHORIZATION_STATE: // valid commands username, pass, quit
                if (strncasecmp(recvbuf, "USER", 4) == 0 || strncasecmp(recvbuf, "USER\r\n", 6) == 0) {
                    char *parts[50];
                    int argsCount = split(recvbuf, &parts);
                    if(argsCount != 2){
                        resp = send_formatted(fd, "-ERR invalid args to user\r\n");
                        continue;
                    }
                    if (is_valid_user(parts[1], NULL)) { // username is valid
                        strcpy(user,parts[1]);
                        resp = send_formatted(fd, "+OK %s is a valid mailbox\r\n", parts[1]);
                        state = PASS_STATE;

                    } else { //invalid username
                        resp = send_formatted(fd, "-ERR sorry, no mailbox for %s here\r\n", parts[1]);
                    }
                } else if (!strncasecmp(recvbuf, "QUIT", 4) || !strncasecmp(recvbuf, "QUIT\r\n", 6)){
                    resp = send_formatted(fd, "+OK POP3 server signing off\r\n");
                    return;
                } else { //invalid command in user state
                    resp = send_formatted(fd, "-ERR invalid args to user\r\n");

                }
                break;

            case PASS_STATE:
                if (strncasecmp(recvbuf, "USER", 4) == 0 || strncasecmp(recvbuf, "USER\r\n", 6) == 0) { // reset user
                    char *parts[50];
                    int argsCount = split(recvbuf, &parts);
                    if(argsCount != 2){
                        resp = send_formatted(fd, "-ERR invalid args to user\r\n");
                        continue;
                    }
                    if (is_valid_user(parts[1], NULL)) { // username is valid
                        strcpy(user,parts[1]);
                        resp = send_formatted(fd, "+OK %s is a valid mailbox\r\n", parts[1]);
                        state = PASS_STATE;

                    } else { //invalid username
                        strcpy(user,parts[1]);
                        resp = send_formatted(fd, "-ERR sorry, no mailbox for %s here\r\n", parts[1]);
                    }

                } else if (strncasecmp(recvbuf, "PASS", 4) == 0 || strncasecmp(recvbuf, "PASS\r\n", 6) == 0) {
                    char *part[30];

                   int argsCount = split(recvbuf, &part);

                    if(argsCount != 2 || part[1] == NULL) {
                        resp = send_formatted(fd, "-ERR invalid args to PASS\r\n");
                        continue;
                    }
                    if(is_valid_user(user, part[1])){
                        resp = send_formatted(fd, "+OK %s has 2 messages\r\n",user);
                        mailList = load_user_mail(user);
                        state = TRANSACTION_STATE;
                    } else { //invalid password
                        resp = send_formatted(fd, "-ERR invalid password\r\n");
                    }

                } else { //invalid command in user state
                    resp = send_formatted(fd, "-ERR invalid args to user\r\n");

                }

                break;

            case TRANSACTION_STATE: // valid commands STAT, LIST, RETR, DELE, NOOP, RSET, QUIT(and will enter UPDATE state)

                if (strncasecmp(recvbuf, "NOOP\n", 5) == 0 || strncasecmp(recvbuf, "NOOP\r\n", 6) == 0) {
                    resp = send_formatted(fd, "+OK\r\n");
                }

                if (strncasecmp(recvbuf, "STAT\n", 5) == 0 || strncasecmp(recvbuf, "STAT\r\n", 6) == 0) {
                    int mailCount = get_mail_count(mailList,0);
                    int mailSize = get_mail_list_size(mailList);
                    resp = send_formatted(fd, "+OK %d %d\r\n",mailCount, mailSize);
                }

                if (!strncasecmp(recvbuf, "DELE", 4)) {
                    char *part[30];
                   int argsCount = split(recvbuf, &part);
                    if(argsCount != 2 || part[1] == NULL) {
                        resp = send_formatted(fd, "-ERR invalid args to DELE\r\n");
                        continue;
                    }

                    int pos = atoi(part[1]);
                    mail_item_t mail = get_mail_item(mailList,pos -1);
                    if(mail == NULL){ //already deleted
                        resp = send_formatted(fd, "-ERR message %d already deleted\r\n",pos);
                    } else {
                        mark_mail_item_deleted(mail);
                        resp = send_formatted(fd, "+OK message %d deleted\r\n", pos);
                    }
                }

                if (!strncasecmp(recvbuf, "QUIT\n", 5) || !strncasecmp(recvbuf, "QUIT\r\n", 6)) {
                    destroy_mail_list(mailList);

                    resp = send_formatted(fd, "+OK server signing off (maildrop empty)\r\n");


                    state = UPDATE_STATE;
                    continue;

                }

                if (!strncasecmp(recvbuf, "RSET\n", 5) || !strncasecmp(recvbuf, "RSET\r\n", 6  )) {
                   int recoveredMsgs = reset_mail_list_deleted_flag(mailList);
                    resp = send_formatted(fd, "+OK %d messages restored\r\n",recoveredMsgs);
                }

                if (!strncasecmp(recvbuf, "RETR", 4)) {
                    char *part[30];
                    int argsCount = split(recvbuf, &part);
                    if(argsCount != 2){
                        resp = send_formatted(fd, "-ERR invalid args to RETR\r\n");
                        continue;
                    } else {
                        int pos = atoi(part[1]);
                        mail_item_t mail = get_mail_item(mailList, pos - 1);
                        if (mail == NULL) {
                            resp = send_formatted(fd, "-ERR no such message\r\n");
                        } else {
                            int mailItemSize = get_mail_item_size(mail);
                            resp = send_formatted(fd, "+OK %d octets\r\n", mailItemSize);
                            FILE *file = get_mail_item_contents(mail);
                            char data[MAX_LINE_LENGTH];
                            int dataSize = fread(data, MAX_LINE_LENGTH, 1, file);
                            send_all(fd, data, mailItemSize);
                            resp = send_formatted(fd, ".\r\n");
                            fclose(file);
                        }
                    }


                }

                if (!strncasecmp(recvbuf, "LIST", 4)) {
                    int mailCount = get_mail_count(mailList,0);
                    if(mailCount == 0){
                        resp = send_formatted(fd, "+OK 0 messages\r\n");
                        resp = send_formatted(fd, ".\r\n");
                    } else {
                        char *part[30];

                        split(recvbuf, &part);
                        if(part[1] == NULL){ // no args after list
                            resp = send_formatted(fd, "+OK %d messages\r\n", mailCount);
                            for(int i = 0; i <= mailCount; i++){
                                printf("mail %d",i);
                                mail_item_t mail = get_mail_item(mailList,i);
                                if(mail != NULL) {
                                    int mailItemSize = get_mail_item_size(mail);
                                    int mailNum = i + 1;
                                    resp = send_formatted(fd,"%d %d\r\n",mailNum,mailItemSize);
                                }
                            }
                            resp = send_formatted(fd, ".\r\n");
                        } else { // args provided
                            int pos = atoi(part[1]);
                            mail_item_t mail = get_mail_item(mailList, pos - 1);
                            if (mail == NULL) {
                                resp = send_formatted(fd, "-ERR no such message\r\n");
                            } else {
                                int mailItemSize = get_mail_item_size(mail);
                                resp = send_formatted(fd, "+OK %d %d\r\n", pos, mailItemSize);
                            }
                        }
                    }


                }



                break;

            case UPDATE_STATE:

                break;





        }

    }
    nb_destroy(nb);
  

}

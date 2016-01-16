#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include "protocol.h"

#define MESSAGE 'm'
#define ROOM_INFO 'i'

#define CHANGE_NAME 'n'
#define QUIT 'q'

typedef struct sockaddr_in sockaddr_in;
typedef struct sockaddr sockaddr;

int comm_sock_fd;
sockaddr_in tracker_addr;
sockaddr_in self_addr;
sockaddr_in peer_list[100];
unsigned int room_num = 0;
int peer_num = 0;
char username[20];

pthread_mutex_t stdout_lock;
pthread_mutex_t peer_list_lock;


void create_room_request() {
	packet pkt;
	pkt.header.type = CREATE_ROOM;
	pkt.header.error = EMPTY;
	pkt.header.payload_length = 0;

	if (sendto(comm_sock_fd, &pkt, sizeof(pkt.header), 0, (sockaddr *)&tracker_addr, sizeof(sockaddr_in)) == -1) {
		pthread_mutex_lock(&stdout_lock);
		fprintf(stderr, "%s\n", "error - error sending packet to tracker");
		pthread_mutex_unlock(&stdout_lock);
	}
}

void join_room_request(int new_room_num) {
	packet pkt;
	pkt.header.type = JOIN_ROOM;
	pkt.header.error = EMPTY;
	pkt.header.room = new_room_num;
	pkt.header.payload_length = 0;

	if (sendto(comm_sock_fd, &pkt, sizeof(pkt.header), 0, (sockaddr *)&tracker_addr, sizeof(sockaddr_in)) == -1) {
		pthread_mutex_lock(&stdout_lock);
		fprintf(stderr, "%s\n", "error - error sending packet to tracker");
		pthread_mutex_unlock(&stdout_lock);
	}
}

void leave_room_request() {
	packet pkt;
	pkt.header.type = LEAVE_ROOM;
	pkt.header.error = EMPTY;
	pkt.header.room = room_num;
	pkt.header.payload_length = 0;

	if (sendto(comm_sock_fd, &pkt, sizeof(pkt.header), 0, (sockaddr *)&tracker_addr, sizeof(sockaddr_in)) == -1) {
		pthread_mutex_lock(&stdout_lock);
		fprintf(stderr, "%s\n", "error - error sending packet to tracker");
		pthread_mutex_unlock(&stdout_lock);
	}
}

void send_message(char *msg) {
	if (msg[0] == EMPTY) {
		pthread_mutex_lock(&stdout_lock);
		fprintf(stderr, "%s\n", "error - no message content, no package will be sent");
		pthread_mutex_unlock(&stdout_lock);
	} else {
        packet pkt;
        pkt.header.type = MESSAGE;
        pkt.header.error = EMPTY;
        pkt.header.room = room_num;
        memcpy(pkt.header.username, username, strlen(username));
        pkt.header.payload_length = strlen(msg) + 1;
        memcpy(pkt.payload, msg, pkt.header.payload_length);

        pthread_mutex_lock(&peer_list_lock);
        for (int i = 0; i < peer_num; i++) {
            if (sendto(comm_sock_fd, &pkt, sizeof(pkt.header) + pkt.header.payload_length, 0, (sockaddr *)&(peer_list[i]), sizeof(sockaddr_in)) == -1) {
                pthread_mutex_lock(&stdout_lock);
                fprintf(stderr, "%s %d\n", "error - error sending packet to peer", i);
                pthread_mutex_unlock(&stdout_lock);
            }
        }
        pthread_mutex_unlock(&peer_list_lock);
    }
}

void request_available_rooms() {
	packet pkt;
	pkt.header.type = LIST_ROOMS;
	pkt.header.error = EMPTY;
	pkt.header.room = room_num;
	pkt.header.payload_length = 0;

	if (sendto(comm_sock_fd, &pkt, sizeof(pkt.header), 0, (sockaddr *)&tracker_addr, sizeof(sockaddr_in)) == -1) {
		pthread_mutex_lock(&stdout_lock);
		fprintf(stderr, "%s\n", "error - error sending packet to tracker");
		pthread_mutex_unlock(&stdout_lock);
	}
}

void print_current_local_room_info() {
	pthread_mutex_lock(&stdout_lock);
	if (peer_num == 0) {
		fprintf(stderr, "%s\n", "error - you are not in any room!");
	} else {
		printf("%s %d\n", "you are in chatroom number", room_num);
		printf("%s\n", "member(s): ");

		char *peer_ip;
		short peer_port;
		for (int i = 0; i < peer_num; i++) {
			peer_ip = inet_ntoa(peer_list[i].sin_addr);
			peer_port = htons(peer_list[i].sin_port);
			printf("%s:%d\n", peer_ip, peer_port);
		}
	}
	pthread_mutex_unlock(&stdout_lock);
}

void change_username(char* name) {
	memcpy(username, name, strlen(name) + 1);

	pthread_mutex_lock(&stdout_lock);
	printf("%s %s\n", "Username changed to: ", username);
	pthread_mutex_unlock(&stdout_lock);
}

void process_create_room_reply(packet *pkt) {
	char error = pkt->header.error;
	if (error != EMPTY) {
		pthread_mutex_lock(&stdout_lock);
		if (error == ROOM_LIMIT_REACHED) {
			fprintf(stderr, "%s\n", "error - no more chatrooms can be opened this time!");
		}
		else if (error == 'e') {
			fprintf(stderr, "%s\n", "error - you already are in a chatroom!");
		}
		else {
			fprintf(stderr, "%s\n", "error - unspecified error.");
		}
		pthread_mutex_unlock(&stdout_lock);
		return;
	}

	pthread_mutex_lock(&peer_list_lock);
	room_num = pkt->header.room;
	peer_num = 1;
	memcpy(peer_list, &self_addr, sizeof(sockaddr_in));
	pthread_mutex_unlock(&peer_list_lock);
	pthread_mutex_lock(&stdout_lock);
	printf("%s %d\n", "You've created and joined chatroom", room_num);
	pthread_mutex_unlock(&stdout_lock);
}

void process_join_room_reply(packet *pkt) {
	char error = pkt->header.error;
	if (error != EMPTY) {
		pthread_mutex_lock(&stdout_lock);
		if (error == 'f') {
			fprintf(stderr, "%s\n", "error - the chatroom is full!");
		} else if (error == 'e') {
			fprintf(stderr, "%s\n", "error - the chatroom does not exist!");
		} else if (error == 'a') {
			fprintf(stderr, "%s\n", "error - you are already in that chatroom!");
		} else {
			fprintf(stderr, "%s\n", "error - unspecified error.");
		}
		pthread_mutex_unlock(&stdout_lock);
		return;
	}

	pthread_mutex_lock(&peer_list_lock);
	room_num = pkt->header.room;
	peer_num = (int) (pkt->header.payload_length / sizeof(sockaddr_in));

	if (peer_num <= 0) {
		pthread_mutex_lock(&stdout_lock);
		fprintf(stderr, "%s\n", "error - peer list missing, can't join chatroom, leaving old chatroom if switching.");
		pthread_mutex_unlock(&stdout_lock);
		room_num = 0;
		peer_num = 0;
	} else {
		memcpy(peer_list, pkt->payload, peer_num * sizeof(sockaddr_in));
		pthread_mutex_lock(&stdout_lock);
		printf("%s %d\n", "you have joined chatroom", room_num);
		pthread_mutex_unlock(&stdout_lock);
	}
	pthread_mutex_unlock(&peer_list_lock);
}

void process_leave_room_reply(packet *pkt) {
	char error = pkt->header.error;
	if (error != EMPTY) {
		pthread_mutex_lock(&stdout_lock);
		if (error == 'e') {
			fprintf(stderr, "%s\n", "error - you are not in any chatroom!");
		}
		else {
			fprintf(stderr, "%s\n", "error - unspecified error.");
		}
		pthread_mutex_unlock(&stdout_lock);
		return;
	}

	pthread_mutex_lock(&peer_list_lock);
	room_num = 0;
	peer_num = 0;
	pthread_mutex_unlock(&peer_list_lock);
	pthread_mutex_lock(&stdout_lock);
	printf("%s\n", "you have left the chatroom.");
	pthread_mutex_unlock(&stdout_lock);
}

void process_roommate_update(packet *pkt) {
	pthread_mutex_lock(&peer_list_lock);

	int new_peer_num = (int) (pkt->header.payload_length / sizeof(sockaddr_in));
	if (new_peer_num <= 0) {
		pthread_mutex_lock(&stdout_lock);
		fprintf(stderr, "%s\n", "error - peer list missing.");
		pthread_mutex_unlock(&stdout_lock);
	} else {
		pthread_mutex_lock(&stdout_lock);
		printf("room update recieved\n");
		pthread_mutex_unlock(&stdout_lock);
		peer_num = new_peer_num;
		memcpy(peer_list, pkt->payload, peer_num * sizeof(sockaddr_in));
	}
	pthread_mutex_unlock(&peer_list_lock);
}

void print_available_rooms(packet *pkt) {
	pthread_mutex_lock(&stdout_lock);
	printf("Room List: \n%s", pkt->payload);
	pthread_mutex_unlock(&stdout_lock);
}

void print_received_message(sockaddr_in *sender_addr, packet *pkt) {
	if(pkt->header.username[0] == EMPTY) {
		char *sender_ip = inet_ntoa(sender_addr->sin_addr);
		short sender_port = htons(sender_addr->sin_port);

		pthread_mutex_lock(&stdout_lock);
		printf("%s:%d - %s\n", sender_ip, sender_port, pkt->payload);
		pthread_mutex_unlock(&stdout_lock);
	} else {
		pthread_mutex_lock(&stdout_lock);
		printf("%s - %s\n", pkt->header.username, pkt->payload);
		pthread_mutex_unlock(&stdout_lock);
	}
}

void reply_to_ping(sockaddr_in *sender_addr) {
	packet pkt;
	pkt.header.type = PING;
	pkt.header.error = EMPTY;
	pkt.header.payload_length = 0;

	if (sendto(comm_sock_fd, &pkt, sizeof(pkt.header), 0, (sockaddr *)sender_addr, sizeof(sockaddr_in)) == -1) {
		pthread_mutex_lock(&stdout_lock);
		fprintf(stderr, "%s\n", "error - error replying to ping message");
		pthread_mutex_unlock(&stdout_lock);
	}
}

void parse_args(int argc, char **argv) {
    if (argc != 4) {
        fprintf(stderr, "%s\n", "error - Argument number not correct");
    }

    short tracker_port = atoi(argv[2]);
    short self_port = atoi(argv[3]);
    char tracker_ip[20] = "";
    memcpy(tracker_ip, argv[1], (strlen(argv[1]) + 1 > sizeof(tracker_ip)) ? sizeof(tracker_ip) : strlen(argv[1]));

    self_addr.sin_family = AF_INET;
    self_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    self_addr.sin_port = htons(self_port);

    tracker_addr.sin_family = AF_INET;
    if (inet_aton(tracker_ip, &tracker_addr.sin_addr) == 0) {
        fprintf(stderr, "%s\n", "error - error parsing tracker ip.");
        abort();
    }
    tracker_addr.sin_port = htons(tracker_port);
}

void * read_input(void *ptr) {
    char line[1000];
    while (1) {
        memset(line, 0, sizeof(line));

        if (fgets(line, sizeof(line), stdin) == NULL) {
            pthread_mutex_lock(&stdout_lock);
            fprintf(stderr, "%s\n", "error - cannot read input");
            pthread_mutex_unlock(&stdout_lock);
            continue;
        }

        if (line[strlen(line) - 1] != '\n') {
            // flush input stream to clear out long message
            scanf("%*[^\n]");
            (void) getchar();
        }

        line[strlen(line) - 1] = EMPTY;

        if (line[0] != '-') {
            pthread_mutex_lock(&stdout_lock);
            fprintf(stderr, "%s\n", "error - input format is not correct.");
            pthread_mutex_unlock(&stdout_lock);
            continue;
        }
        int new_room_num;
        switch (line[1]) {
            case CREATE_ROOM:
                create_room_request();
                break;
            case JOIN_ROOM:
                new_room_num = atoi(line + 3);
                if (new_room_num < 0) {
                    pthread_mutex_lock(&stdout_lock);
                    fprintf(stderr, "%s\n", "error - room number invalid.");
                    pthread_mutex_unlock(&stdout_lock);
                } else {
                    join_room_request(new_room_num);
                }
                break;
            case LEAVE_ROOM:
                leave_room_request();
                break;
            case MESSAGE:
                send_message(line + 3);
                break;
            case LIST_ROOMS:
                request_available_rooms();
                break;
            case ROOM_INFO:
                print_current_local_room_info();
                break;
            case CHANGE_NAME:
                change_username(line + 3);
                break;
            case QUIT:
                pthread_mutex_lock(&stdout_lock);
                printf("exiting...\n");
                pthread_mutex_unlock(&stdout_lock);
                exit(0);
            default:
                pthread_mutex_lock(&stdout_lock);
                fprintf(stderr, "%s\n", "error - request type unknown.");
                pthread_mutex_unlock(&stdout_lock);
                break;
        }
    }
    return NULL;
}

void receive_packet() {
    sockaddr_in sender_addr;
    socklen_t addrlen = 10;
    packet pkt;

    while (1) {
        if (recvfrom(comm_sock_fd, &pkt, sizeof(pkt), 0, (sockaddr *)&sender_addr, &addrlen) == -1) {
            pthread_mutex_lock(&stdout_lock);
            fprintf(stderr, "%s\n", "error - error receiving a packet, ignoring.");
            pthread_mutex_unlock(&stdout_lock);
            continue;
        }

        switch (pkt.header.type) {
            case CREATE_ROOM:
                process_create_room_reply(&pkt);
                break;
            case JOIN_ROOM:
                process_join_room_reply(&pkt);
                break;
            case LEAVE_ROOM:
                process_leave_room_reply(&pkt);
                break;
            case UPDATE_ROOM:
                process_roommate_update(&pkt);
                break;
            case LIST_ROOMS:
                print_available_rooms(&pkt);
                break;
            case MESSAGE:
                print_received_message(&sender_addr, &pkt);
                break;
            case PING:
                reply_to_ping(&sender_addr);
                break;
            default:
                pthread_mutex_lock(&stdout_lock);
                fprintf(stderr, "%s\n", "error - received packet type unknown.");
                pthread_mutex_unlock(&stdout_lock);
                break;
        }
    }
}

int main(int argc, char **argv) {

	comm_sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (comm_sock_fd < 0) {
		fprintf(stderr, "%s\n", "error - error creating socket.");
		abort();
	}

	parse_args(argc, argv);

	if (bind(comm_sock_fd, (sockaddr *)&self_addr, sizeof(self_addr))) {
		fprintf(stderr, "%s\n", "error - error binding.");
		abort();
	}

	// create a thread to read user input
	pthread_t input_thread;
	pthread_create(&input_thread, NULL, read_input, NULL);
	pthread_detach(input_thread);

	receive_packet();
}

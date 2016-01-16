#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
#include <time.h>
#include <pthread.h>
#include <stdbool.h>
#include "uthash.h"
#include "protocol.h"

#define MAX_NUM_ROOMS 5
#define MAX_ROOM_SIZE 5
#define DEFAULT_PORT 8080


typedef struct peer {
    char ip_and_port[20];
    unsigned int room;
    bool alive;
    UT_hash_handle hh;
} peer;

typedef struct sockaddr_in sockaddr_in;
typedef struct sockaddr sockaddr;

struct peer *peers;
int comm_sock_fd;
int ping_sock_fd;
pthread_mutex_t stdout_lock;
pthread_mutex_t peers_lock;

sockaddr_in get_sockaddr_in(unsigned int ip, short port) {
    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = ip;
    addr.sin_port = htons(port);
    return addr;
}

// get IP from key (1077312167:8999)
unsigned int get_ip_from_peer_key(char *ip_port) {
    int i;
    for (i = 0; i < 20; i++) {
        if (ip_port[i] == ':') {
            break;
        }
    }
    char char_ip[i + 1];
    strncpy(char_ip, ip_port, i);
    char_ip[i] = EMPTY;

    return (unsigned int) strtoul(char_ip, NULL, 0);
}

// get PORT from key (1077312167:8999)
short get_port_from_peer_key(char *ip_port) {
    int i;
    int start = -1;
    int end = -1;
    for (i = 0; i < 20; i++) {
        if (start == -1 && ip_port[i] == ':') {
            start = i + 1;
        }

        if (ip_port[i] == EMPTY) {
            end = i;
            break;
        }
    }
    char char_short[end - start + 1];
    strncpy(char_short, ip_port + start, end - start);
    char_short[end - start] = EMPTY;

    return (short) strtoul(char_short, NULL, 0);
}

int get_number_of_rooms() {
    int total = 0;

    unsigned int room_found[MAX_NUM_ROOMS];
    memset(room_found, 0, MAX_NUM_ROOMS * sizeof(room_found[0]));

    unsigned int rooms[MAX_NUM_ROOMS];
    memset(rooms, 0, MAX_NUM_ROOMS * sizeof(rooms[0]));

    peer *s;
    unsigned int a;
    for (s = peers; s != NULL; s = (peer *) s->hh.next) {
        int found = 0;
        for (a = 0; a < sizeof(rooms) / sizeof(rooms[0]); a++) {
            if (rooms[a] == s->room && room_found[a] == 1) {
                found = 1;
                break;
            }
        }

        if (found == 0) {
            room_found[total] = 1;
            rooms[total] = s->room;
            total = total + 1;
        }
    }

    return total;
}

int get_room_member_count(unsigned int room) {
    peer *s;
    int room_member_count = 0;

    for (s = peers; s != NULL; s = (peer *) s->hh.next) {
        if (s->room == room) {
            room_member_count++;
        }
    }

    return room_member_count;
}

void send_error(unsigned int ip, short port, char type, char error) {
    packet pkt;
    pkt.header.type = type;
    pkt.header.error = error;
    pkt.header.payload_length = 0;

    sockaddr_in peer_addr = get_sockaddr_in(ip, port);

    if (sendto(comm_sock_fd, &pkt, sizeof(pkt.header), 0, (sockaddr *) &peer_addr, sizeof(peer_addr)) == -1) {
        pthread_mutex_lock(&stdout_lock);
        fprintf(stderr, "%s\n", "error - error sending packet to peer");
        pthread_mutex_unlock(&stdout_lock);
    }
}

// join: join_ip and join_port for the new user in the room
// leave: join_ip=0 and join_port=-1
void send_room_update_messages(unsigned int join_ip, short join_port, unsigned int room) {
    int room_member_count = get_room_member_count(room);
    sockaddr_in room_member_addresses[room_member_count];

    int a = 0;
    peer *s;
    for (s = peers; s != NULL; s = (peer *) s->hh.next) {
        if (s->room == room) {
            sockaddr_in peer_info = get_sockaddr_in(get_ip_from_peer_key(s->ip_and_port),
                                                    get_port_from_peer_key(s->ip_and_port));
            sockaddr_in *peer_info_ptr = &peer_info;
            memcpy(&room_member_addresses[a], peer_info_ptr, sizeof(peer_info));
            a++;
        }
    }

    size_t room_members_address_list_size = room_member_count * sizeof(sockaddr_in);

    packet update_pkt;
    update_pkt.header.type = UPDATE_ROOM;
    update_pkt.header.error = EMPTY;
    update_pkt.header.payload_length = (unsigned int) room_members_address_list_size;
    memcpy(update_pkt.payload, room_member_addresses, room_members_address_list_size);

    for (s = peers; s != NULL; s = (peer *) s->hh.next) {
        if (s->room == room) {
            unsigned int peer_ip = get_ip_from_peer_key(s->ip_and_port);
            short peer_port = get_port_from_peer_key(s->ip_and_port);

            // csatlakozáskor a csatlakozónak
            if (join_port != -1 && join_ip != 0 && peer_ip == join_ip && peer_port == join_port) {
                packet join_pkt;
                join_pkt.header.type = JOIN_ROOM;
                join_pkt.header.error = EMPTY;
                join_pkt.header.room = room;
                join_pkt.header.payload_length = (unsigned int) (room_members_address_list_size);
                memcpy(join_pkt.payload, room_member_addresses, room_members_address_list_size);

                sockaddr_in peer_addr = get_sockaddr_in(peer_ip, peer_port);

                if (sendto(comm_sock_fd, &join_pkt, sizeof(join_pkt), 0, (sockaddr *) &peer_addr, sizeof(peer_addr)) == -1) {
                    pthread_mutex_lock(&stdout_lock);
                    fprintf(stderr, "%s\n", "error - error sending packet to peer");
                    pthread_mutex_unlock(&stdout_lock);
                }
            } else { // csatlakozáskor vagy kilépéskor mindenki másnak
                sockaddr_in peer_addr = get_sockaddr_in(peer_ip, peer_port);

                if (sendto(comm_sock_fd, &update_pkt, sizeof(update_pkt), 0, (sockaddr *) &peer_addr, sizeof(peer_addr)) == -1) {
                    pthread_mutex_lock(&stdout_lock);
                    fprintf(stderr, "%s\n", "error - error sending packet to peer");
                    pthread_mutex_unlock(&stdout_lock);
                }
            }
        }
    }
}

void peer_leave(unsigned int ip, short port) {
    char ip_port[20];
    memset(ip_port, 0, sizeof(ip_port));

    char *ip_and_port_format = (char *) "%d:%d";
    sprintf(ip_port, ip_and_port_format, ip, port);

    peer *s;
    HASH_FIND_STR(peers, ip_port, s);

    if (s != NULL) {
        unsigned int left_room = s->room;
        pthread_mutex_lock(&peers_lock);
        HASH_DEL(peers, s);
        free(s);
        pthread_mutex_unlock(&peers_lock);
        pthread_mutex_lock(&stdout_lock);
        fprintf(stderr, "%s left %d\n", ip_port, left_room);
        pthread_mutex_unlock(&stdout_lock);

        packet pkt;
        pkt.header.type = LEAVE_ROOM;
        pkt.header.error = EMPTY;
        pkt.header.payload_length = 0;
        sockaddr_in peer_addr = get_sockaddr_in(ip, port);

        if (sendto(comm_sock_fd, &pkt, sizeof(pkt.header), 0, (sockaddr *) &peer_addr, sizeof(peer_addr)) == -1) {
            pthread_mutex_lock(&stdout_lock);
            fprintf(stderr, "%s\n", "error - error sending packet to peer");
            pthread_mutex_unlock(&stdout_lock);
        }
        send_room_update_messages(0, -1, left_room);
    } else {
        send_error(ip, port, LEAVE_ROOM, ERROR);
    }
}



void mark_peer_alive(unsigned int ip, short port) {
    char ip_port[20];
    memset(ip_port, 0, sizeof(ip_port));

    char *ip_and_port_format = (char *) "%d:%d";
    sprintf(ip_port, ip_and_port_format, ip, port);

    peer *s;
    HASH_FIND_STR(peers, ip_port, s);
    if (s != NULL) {
        pthread_mutex_lock(&peers_lock);
        s->alive = true;
        pthread_mutex_unlock(&peers_lock);
    } else {
        pthread_mutex_lock(&stdout_lock);
        fprintf(stderr, "%s\n", "peer not found for ping response");
        pthread_mutex_unlock(&stdout_lock);
    }
}

void *ping_receive(void *ptr) {
    socklen_t addrlen = 10;
    sockaddr_in sender_addr;
    packet recv_pkt;

    while (1) {
        //check ping socket - mark sender alive
        if (recvfrom(ping_sock_fd, &recv_pkt, sizeof(recv_pkt), 0, (sockaddr *) &sender_addr, &addrlen) == -1) {
            pthread_mutex_lock(&stdout_lock);
            fprintf(stderr, "%s\n", "error - error receiving a packet, ignoring.");
            pthread_mutex_unlock(&stdout_lock);
        } else {
            unsigned int ip = sender_addr.sin_addr.s_addr;
            short port = htons(sender_addr.sin_port);
            switch (recv_pkt.header.type) {
                case PING:
                    mark_peer_alive(ip, port);
                    break;
                default:
                    pthread_mutex_lock(&stdout_lock);
                    fprintf(stderr, "%s\n", "error - received packet type unknown.");
                    pthread_mutex_unlock(&stdout_lock);
                    break;
            }
        }
    }
    return NULL;
}



void send_pings() {
    peer *s;
    for (s = peers; s != NULL; s = (peer *) s->hh.next) {
        //mark dead
        pthread_mutex_lock(&peers_lock);
        s->alive = false;
        pthread_mutex_unlock(&peers_lock);

        //send ping
        unsigned int peer_ip = get_ip_from_peer_key(s->ip_and_port);
        short peer_port = get_port_from_peer_key(s->ip_and_port);
        packet pkt;
        pkt.header.type = PING;
        pkt.header.error = EMPTY;
        pkt.header.payload_length = 0;
        sockaddr_in peer_addr = get_sockaddr_in(peer_ip, peer_port);

        if (sendto(ping_sock_fd, &pkt, sizeof(pkt.header), 0, (sockaddr *) &peer_addr, sizeof(peer_addr)) == -1) {
            pthread_mutex_lock(&stdout_lock);
            fprintf(stderr, "%s\n", "error - error sending packet to peer");
            pthread_mutex_unlock(&stdout_lock);
        }
    }
}

void delete_dead_peers() {
    peer *s;
    for (s = peers; s != NULL; s = (peer *) s->hh.next) {
        if (!(s->alive)) {
            unsigned int peer_ip = get_ip_from_peer_key(s->ip_and_port);
            short peer_port = get_port_from_peer_key(s->ip_and_port);
            peer_leave(peer_ip, peer_port);
        }
    }
}

void *ping_send_and_delete_dead_peers(void *ptr) {
    clock_t t;
    send_pings();
    t = clock();
    while (1) {
        if ((float) (clock() - t) / CLOCKS_PER_SEC >= 5) { //ping time interval over
            pthread_mutex_lock(&stdout_lock);
            fprintf(stderr, "%s\n", "Checking ping responses");
            pthread_mutex_unlock(&stdout_lock);
            delete_dead_peers();
            send_pings();
            t = clock();
        }
    }
    return NULL;
}



void peer_create_room(unsigned int ip, short port) {
    //check if room limit reached
    if (get_number_of_rooms() >= MAX_NUM_ROOMS) {
        send_error(ip, port, CREATE_ROOM, ROOM_LIMIT_REACHED);
        return;
    }

    char new_peer_ip_and_port[20];
    char *ip_and_port_format = (char *) "%d:%d";
    sprintf(new_peer_ip_and_port, ip_and_port_format, ip, port);

    //check if peer is already in a room
    peer *s;
    HASH_FIND_STR(peers, new_peer_ip_and_port, s);

    if (s != NULL) {
        send_error(ip, port, CREATE_ROOM, ERROR);
    } else {
        //get a room number
        int room_taken = 0;
        unsigned int room;
        for (room = 1; room < MAX_NUM_ROOMS * 2; room++) {
            for (s = peers; s != NULL; s = (peer *) s->hh.next) {
                if (s->room == room) {
                    room_taken = 1;
                    break;
                }
            }

            if (room_taken == 0) {
                break;
            } else {
                room_taken = 0;
            }
        }
        //create entry
        peer *new_peer;
        new_peer = (peer *) malloc(sizeof(peer));
        sprintf(new_peer->ip_and_port, ip_and_port_format, ip, port);
        new_peer->room = room;
        new_peer->alive = true;

        pthread_mutex_lock(&peers_lock);
        HASH_ADD_STR(peers, ip_and_port, new_peer);  //create room - add peer
        pthread_mutex_unlock(&peers_lock);
        pthread_mutex_lock(&stdout_lock);
        fprintf(stderr, "%s created %d\n", new_peer->ip_and_port, room);
        pthread_mutex_unlock(&stdout_lock);

        packet pkt;
        pkt.header.type = CREATE_ROOM;
        pkt.header.error = EMPTY;
        pkt.header.room = room;
        pkt.header.payload_length = 0;
        sockaddr_in peer_addr = get_sockaddr_in(ip, port);

        if (sendto(comm_sock_fd, &pkt, sizeof(pkt.header), 0, (sockaddr *) &peer_addr, sizeof(peer_addr)) == -1) {
            pthread_mutex_lock(&stdout_lock);
            fprintf(stderr, "%s\n", "error - error sending packet to peer");
            pthread_mutex_unlock(&stdout_lock);
        }
    }
}

void peer_join(unsigned int ip, short port, unsigned int room) {
    peer *s;
    int room_member_count = 0;
    bool room_exists = false;

    // létezik-e a szoba és nincs-e tele
    for (s = peers; s != NULL; s = (peer *) s->hh.next) {
        if (s->room == room) {
            room_member_count++;
            if (room_member_count >= MAX_ROOM_SIZE) {
                send_error(ip, port, JOIN_ROOM, ROOM_FULL);
                return;
            }
            room_exists = true;
        }
    }

    if (!room_exists) {
        send_error(ip, port, JOIN_ROOM, ERROR);
        return;
    }

    peer *new_peer;
    new_peer = (peer *) malloc(sizeof(peer));
    char *ip_and_port_format = (char *) "%d:%d";
    sprintf(new_peer->ip_and_port, ip_and_port_format, ip, port);
    new_peer->room = room;
    new_peer->alive = true;

    // nem-e abban a szobában van
    HASH_FIND_STR(peers, (new_peer->ip_and_port), s);
    if (s != NULL && s->room == room) {
        send_error(ip, port, JOIN_ROOM, 'a');
        return;
    }

    // új belépő vagy szobát vált
    if (s == NULL) {
        pthread_mutex_lock(&peers_lock);
        HASH_ADD_STR(peers, ip_and_port, new_peer);
        pthread_mutex_unlock(&peers_lock);

        pthread_mutex_lock(&stdout_lock);
        fprintf(stderr, "%s joined %d\n", new_peer->ip_and_port, room);
        pthread_mutex_unlock(&stdout_lock);
        send_room_update_messages(ip, port, room);
    } else {
        int old_room_to_update = s->room;
        pthread_mutex_lock(&peers_lock);
        HASH_REPLACE_STR(peers, ip_and_port, new_peer, s);
        pthread_mutex_unlock(&peers_lock);

        pthread_mutex_lock(&stdout_lock);
        fprintf(stderr, "%s peer switched from %d to %d.\n", new_peer->ip_and_port, old_room_to_update, room);
        pthread_mutex_unlock(&stdout_lock);
        send_room_update_messages(ip, port, room);
        send_room_update_messages(0, -1, old_room_to_update);
    }
}

void send_room_list(unsigned int ip, short port) {
    int number_of_rooms = get_number_of_rooms();
    char *list;
    char* list_entry;
    size_t list_size;

    if (number_of_rooms == 0) {
        list = (char *) "There are no chatrooms\n";
    } else {
        // a szobák id-jai
        unsigned int room_ids[number_of_rooms];
        memset(room_ids, 0, number_of_rooms * sizeof(room_ids[0]));

        // a szobákban lévők számossága
        int room_stats[number_of_rooms];
        memset(room_stats, 0, number_of_rooms * sizeof(room_stats[0]));

        peer *s;
        int num_rooms_indexed = 0;

        for (s = peers; s != NULL; s = (peer *) s->hh.next) { // megyek végig az összes peer-en aki vmilyen szobábanvan
            int room_inspection_index = -1;

            for (int i = 0; i < number_of_rooms; i++) {
                if (room_ids[i] == s->room) {
                    room_inspection_index = i;
                    break;
                }
            }

            if (room_inspection_index == -1) { // ha az aktuális szoba még nem vizsgált
                room_inspection_index = num_rooms_indexed;
                room_ids[room_inspection_index] = s->room;
                room_stats[room_inspection_index] = 1;
                num_rooms_indexed++;
            } else {
                room_stats[room_inspection_index]++;
            }
        }

        char *list_entry_format = (char *) "room: %d - %d/%d\n";

        size_t list_entry_size = strlen(list_entry_format);
        list_size = number_of_rooms * list_entry_size;

        list_entry = (char *) malloc(list_entry_size);
        list = (char *) malloc(list_size);
        char *list_i = list;

        for (int i = 0; i < number_of_rooms; i++) {
            sprintf(list_entry, list_entry_format, room_ids[i], room_stats[i], MAX_ROOM_SIZE);
            strcpy(list_i, list_entry);
            list_i += strlen(list_entry);
        }
    }

    packet pkt;
    pkt.header.type = LIST_ROOMS;
    pkt.header.error = EMPTY;
    pkt.header.payload_length = list_size;
    strcpy(pkt.payload, list);
    sockaddr_in peer_addr = get_sockaddr_in(ip, port);

    if (sendto(comm_sock_fd, &pkt, sizeof(pkt), 0, (sockaddr *) &peer_addr, sizeof(peer_addr)) == -1) {
        pthread_mutex_lock(&stdout_lock);
        fprintf(stderr, "%s\n", "error - error sending packet to peer");
        pthread_mutex_unlock(&stdout_lock);
    }

    if(number_of_rooms != 0) {
        free(list);
        free(list_entry);
    }
}

short parse_args(int argc, char **argv) {
    if (argc < 2) {
        return DEFAULT_PORT;
    } else {
        errno = 0;
        char *endptr = NULL;
        short ulPort = (short) strtoul(argv[1], &endptr, 10);

        if (0 == errno) {
            // érvénytelen input range
            if (EMPTY != endptr[0]) {
                errno = EINVAL;
            } else if (ulPort > USHRT_MAX) {
                errno = ERANGE;
            }
        }

        if (0 != errno) {
            fprintf(stderr, "Failed to parse port number \"%s\": %s\n", argv[1], strerror(errno));
            abort();
        }

        return ulPort;
    }
}

int main(int argc, char **argv) {
    // hash tábla a klienseknek
    peers = NULL;

    short port = parse_args(argc, argv);
    fprintf(stderr, "Starting server on ports: %d, %d\n", port, port + 1);

    // UDP socketek
    comm_sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
    ping_sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (comm_sock_fd < 0) {
        fprintf(stderr, "%s\n", "error - error creating comm_sock.");
        abort();
    }
    if (ping_sock_fd < 0) {
        fprintf(stderr, "%s\n", "error - error creating ping_sock.");
        abort();
    }

    sockaddr_in self_addr;
    self_addr.sin_family = AF_INET;
    self_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    self_addr.sin_port = htons(port);

    sockaddr_in ping_addr;
    ping_addr.sin_family = AF_INET;
    ping_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    ping_addr.sin_port = htons(port + 1);

    if (bind(comm_sock_fd, (sockaddr *) &self_addr, sizeof(self_addr))) {
        fprintf(stderr, "%s\n", "error - error binding comm_sock.");
        abort();
    }
    if (bind(ping_sock_fd, (sockaddr *) &ping_addr, sizeof(ping_addr))) {
        fprintf(stderr, "%s\n", "error - error binding ping_sock.");
        abort();
    }

    // ping válasz kezelő szál
    pthread_t ping_response_thread;
    pthread_create(&ping_response_thread, NULL, ping_receive, NULL);
    pthread_detach(ping_response_thread);

    // ping küldés és peer törlés
    pthread_t ping_output_thread;
    pthread_create(&ping_output_thread, NULL, ping_send_and_delete_dead_peers, NULL);
    pthread_detach(ping_output_thread);

    socklen_t addrlen = 10;
    sockaddr_in sender_addr;
    packet recv_pkt;

    while (1) {
        if (recvfrom(comm_sock_fd, &recv_pkt, sizeof(recv_pkt), 0, (sockaddr *) &sender_addr, &addrlen) == -1) {
            pthread_mutex_lock(&stdout_lock);
            fprintf(stderr, "%s\n", "error - error receiving a packet, ignoring.");
            pthread_mutex_unlock(&stdout_lock);
        } else {
            unsigned int ip = sender_addr.sin_addr.s_addr;
            short port = htons(sender_addr.sin_port);
            switch (recv_pkt.header.type) {
                case CREATE_ROOM:
                    peer_create_room(ip, port);
                    break;
                case JOIN_ROOM:
                    peer_join(ip, port, recv_pkt.header.room);
                    break;
                case LEAVE_ROOM:
                    peer_leave(ip, port);
                    break;
                case LIST_ROOMS:
                    send_room_list(ip, port);
                    break;
                default:
                    pthread_mutex_lock(&stdout_lock);
                    fprintf(stderr, "%s\n", "error - received packet type unknown.");
                    pthread_mutex_unlock(&stdout_lock);
                    break;
            }
        }
    }
    return 0;
}

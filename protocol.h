typedef struct packet_header {
	char type;
	char error;
	char username[20];
	unsigned int room;
	unsigned int payload_length;
} packet_header;

typedef struct packet {
	packet_header header;
	char payload[1000];
} packet;

#define UPDATE_ROOM 'u'
#define CREATE_ROOM 'c'
#define JOIN_ROOM 'j'
#define LEAVE_ROOM 'l'
#define LIST_ROOMS 'r'
#define PING 'p'

#define ROOM_FULL 'f'
#define ROOM_LIMIT_REACHED 'o'
#define ERROR 'e'

#define EMPTY '\0'

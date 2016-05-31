#pragma once

#define MAX_MSG_SIZE 256
#define MAX_SEQ_NUM 16
#define WINDOWSIZE 5
#define SWITCHSIZE 8
#define NUMNODES 7
#define TRUE 1
#define FALSE !TRUE

typedef enum { 
    DL_DATA, DL_ACK 
} FRAMEKIND;

typedef struct {
    char data[MAX_MSG_SIZE];
} MSG;

typedef struct {
    CnetAddr src_addr; /* Source Address */
    CnetAddr dest_addr; /* Destination Address */         
    MSG data;
    size_t msglen;
} Packet;

typedef struct {
    FRAMEKIND kind; /* only ever DL_DATA or DL_ACK */   
    size_t length;     /* the length of the msg field only */   
    int checksum;   /* checksum of the whole frame */
    int seqno;          /* sequence number */
    Packet packet;
} Frame;

typedef struct listNode{
        Frame frame;
        struct listNode* next;
} LinkedListNode;

typedef struct list{
        int size;
        LinkedListNode* head;
        LinkedListNode* tail;
} LinkedList;

/* static routing table     [source][destination] */
static int routing[7][7] = {{0, 1, 2, 3, 1, 1, 1},
                            {1, 0, 1, 1, 2, 3, 4},
                            {1, 1, 0, 1, 1, 1, 1},
                            {1, 1, 1, 0, 1, 1, 1},
                            {1, 1, 1, 1, 0, 1, 1},
                            {1, 1, 1, 1, 1, 0, 2},
                            {1, 1, 1, 1, 1, 2, 0}};

static const char* destination[] = {"Australia", "Indonesia", "NewZealand", "Fiji", "Brunei", "Malaysia", "Singapore"}; 

/* Function Definitions */
EVENT_HANDLER(application_downto_network);
EVENT_HANDLER(physical_upto_datalink);
EVENT_HANDLER(timeouts);
EVENT_HANDLER(showstate);

void network_downto_datalink(Packet packet);
static void datalink_downto_physical(Frame frame, int link);

void datalink_upto_network(int link, Frame frame);
void network_upto_application(int link, Packet packet);

LinkedList* createQueue();
void addFrame(LinkedList* list, Frame f);
void removeFrame(LinkedList* list);
void printQueue(LinkedList* list);
Frame peekFirst(LinkedList* list);

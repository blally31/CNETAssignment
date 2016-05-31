/****************************************************************************
** FILE: assignment.c
** AUTHOR: Brendan Lally
** STUDENT ID: 18407220
** UNIT: Computer Communication (CNCO2000) 
** PURPOSE: Contains implementations of the Application, Network and Datalink 
            layers for a simulated network in cnet.
** LAST MOD: 22/05/16
****************************************************************************
**/

#include <cnet.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "assignment.h"

static int setup = TRUE;
int* ackExpected;
int* frameExpected;
int* nextFrameToSend;
CnetTimerID** lasttimer;
LinkedList** frameQueue;
LinkedList** overflowQueue;


/***************************************************************************
 
    application_downto_network() receives a message from cnet and stores it in 
    a packet.

    network_upto_application() receives a packet and sends the message up to 
    cnet.

****************************************************************************
**/
EVENT_HANDLER(application_downto_network)
{
    CnetAddr destaddr;
    MSG message;
    Packet packet;
    size_t length = sizeof(MSG);
    /* Obtains a new message to be delivered */
    CHECK(CNET_read_application(&destaddr, (char*)&message.data, &length));

    /* Construct packet using message and source/dest addresses */
    memcpy(&packet.data, &message, length);
    packet.src_addr = nodeinfo.nodenumber;
    packet.dest_addr = destaddr;
    packet.msglen = length;
    
    network_downto_datalink(packet);
}

void network_upto_application(int link, Packet packet)
{   
    MSG message = packet.data;
    size_t length = packet.msglen;

    printf("\tApplication received %d bytes on link %d - from %s\n", length, link, destination[packet.src_addr]);

    CHECK(CNET_write_application((char*)&message.data, &length));
}


/***************************************************************************

    network_downto_datalink() receives a packet and encases it in a frame and 
    finds the link required to send it to its destination. The frame is added
    to a queue and sent down.

    datalink_upto_network() receives a frame and then checks if the packet in
    the frame is at the correct address. If its not, it is forwarded.

****************************************************************************
**/
void network_downto_datalink(Packet packet)
{
    Frame frame;
    int i;
    size_t psize = sizeof(Packet);
    
    /*Determine which link to send the frame on */
    int link = routing[nodeinfo.nodenumber][packet.dest_addr];

    /* Initialise all the fields in frame */
    frame.kind = DL_DATA;
    frame.seqno = nextFrameToSend[link];
    memcpy(&frame.packet, &packet, psize);
    frame.length = packet.msglen;
    frame.checksum = 0;
    /* Calculates the checksum */
    frame.checksum = CNET_ccitt((unsigned char *)&frame, sizeof(frame));

    /* Increment the next frame to send */
    nextFrameToSend[link] = ++nextFrameToSend[link] % MAX_SEQ_NUM;

    /* If Aus or Indo then use a larger window size */
    if (nodeinfo.nodenumber == 0 || nodeinfo.nodenumber == 1)
    {
        if (frameQueue[link]->size >= SWITCHSIZE)
        {
            for (i = 0; i < NUMNODES; i++)
            {
                if (routing[nodeinfo.address][i] == link)
                {
                    CNET_disable_application(i);
                }
            }
            /* Add the next frame to the overflow buffer */
            addFrame(overflowQueue[link], frame);
            CNET_set_LED(link, "red");
        }
        else
        {
            /*Add the newly created frame to the queue */
            addFrame(frameQueue[link], frame);
            datalink_downto_physical(frame, link);
            CNET_set_LED(link, "green");
        }
    }
    else
    {
        if(frameQueue[link]->size >= WINDOWSIZE)
        {
            /* Check each link from the current node and disable their message 
            gen to current link */
            for(i = 0; i < NUMNODES; i++)
            {
                if(routing[nodeinfo.address][i] == link)
                {
                    CNET_disable_application(i);
                }
            }
            /* Add the next frame to the overflow buffer */
            addFrame(overflowQueue[link], frame);
            CNET_set_LED(link, "red");
        }
        else
        {
            /*Add the newly created frame to the queue */
            addFrame(frameQueue[link], frame);
            datalink_downto_physical(frame, link);
            CNET_set_LED(link, "green");
        }
    }  
}

void datalink_upto_network(int link, Frame frame)
{
    Packet packet = frame.packet;

    /* Check if packet is at the correct address */
    if (nodeinfo.nodenumber == packet.dest_addr)
    {
        network_upto_application(link, packet);
    }
    else 
    {
        /* Resend packet if it hasn't reached its final destination */
        network_downto_datalink(packet);
    }
}


/***************************************************************************
 
    datalink_downto_physical() receives a frame and a link to send it on. A 
    timer is started for each data frame and the frame is written to the 
    physical layer.

    physical_upto_datalink() receives a frame from the physical layer and 
    depending on what type of frame it is, stops the timer (if ack) or creates
    an acknowledgment to send (if data).

****************************************************************************
**/
static void datalink_downto_physical(Frame frame, int link)
{
    size_t length = sizeof(frame);
    switch (frame.kind) 
    {
        /* Acknowledgment frame */
        case DL_ACK:
        {
            printf("ACK transmitted with seqno=%d, on link %d\n\n", frame.seqno, link);
            break;
        }
        /* Data frame */
        case DL_DATA: 
        {
            CnetTime timeout;

            printf("DATA transmitted with seqno=%d, on link %d\n", frame.seqno, link);
            printf("DEST: %s and SOURCE: %s\n\n", destination[frame.packet.dest_addr], destination[frame.packet.src_addr]);

            /* Calculates the time delivery time = Transmission Time + Propagation Delay */
            timeout = length*((CnetTime)8000000 / linkinfo[link].bandwidth) +
                        linkinfo[link].propagationdelay;

            /* Create a new timer based on the link the frame is traveling on */
            lasttimer[link][frame.seqno] = CNET_start_timer(EV_TIMER1, 3 * timeout, (CnetData)link);
            break;
        }
    }
    /* Pass Frame to the CNET physical layer to send Frame along link. */
    CHECK(CNET_write_physical(link, &frame, &length));
}


EVENT_HANDLER(physical_upto_datalink)
{
    Frame frame, frame1, ack;
    size_t length;
    int link, checksum, checksumExp, difference, i, j, k;

    length = sizeof(Frame);
    /* Read data frame from the physical layer*/
    CHECK(CNET_read_physical(&link, &frame, &length));

    checksum = frame.checksum;
    frame.checksum = 0;

    checksumExp = CNET_ccitt((unsigned char *)&frame, sizeof(frame));
    /* Check if the checksum is correct. ie the same as expected */
    if( checksumExp != checksum) 
    {
        printf("\t\t\t\tBAD checksum - frame ignored\n");
        return;
    }

    switch (frame.kind) 
    {
        case DL_ACK:
            
            /* Check if the the ackowledement frame is the same or later than expected*/
            if (frame.seqno >= ackExpected[link]) 
            {
                difference = frame.seqno - ackExpected[link];
                /* Stop the timer for all outstanding acknowledgments up to the received */
                for (i = difference; i >= 0; i--)
                {
                    CHECK(CNET_stop_timer(lasttimer[link][(ackExpected[link] + i) % MAX_SEQ_NUM]));
                }
            }
            else
            {
                difference = frame.seqno + (MAX_SEQ_NUM - ackExpected[link]);
                /* Stop the timer for all outstanding acknowledgments up to the received */
                for (i = difference; i >= 0; i--)
                {
                    int seq = frame.seqno - i;
                   
                    CHECK(CNET_stop_timer(lasttimer[link][(MAX_SEQ_NUM + seq) % MAX_SEQ_NUM]));
                }
            }

            printf("\t\t\t\tACK received with seqno=%d, from link %d\n\n", frame.seqno, link);
            ackExpected[link] = frame.seqno + 1;

            /* Add Frames from the overflow buffer to the queue and send them */
            for (k = 0; k <= difference; k++)
            {
                CNET_set_LED(link, "green");
                removeFrame(frameQueue[link]);
                /* Add a new frame to the frameQueue from the overflowQueue */
                if (overflowQueue[link]->size != 0)
                {
                    frame1 = peekFirst(overflowQueue[link]);
                    addFrame(frameQueue[link], frame1);
                    removeFrame(overflowQueue[link]);
                    /* Send the newly added frame */
                    datalink_downto_physical(frame1, link);
                }
                /* Enables the generation of new messages for delivery to other nodes */
                for (j = 0; j < NUMNODES; j++)
                {
                    if (routing[nodeinfo.address][j] == link)
                    {
                        CNET_enable_application(j);
                    }
                }
            } 
    	break;

        case DL_DATA:

            /* Check if the the data frame is the same as expected */
            if (frame.seqno == frameExpected[link]) 
            {    
                printf("\t\t\t\tDATA received with seqno=%d, from link %d\n", frame.seqno, link);
                printf("\t\t\t\tDEST: %s and SOURCE: %s\n\n", destination[frame.packet.dest_addr], destination[frame.packet.src_addr]);
                
                frameExpected[link] = ++frameExpected[link] % MAX_SEQ_NUM;

                /* Create an acknowledgment frame for the data frame received */
                ack.kind = DL_ACK;
                ack.seqno = frame.seqno;
                ack.length = 0;
                ack.checksum = 0;
                ack.checksum = CNET_ccitt((unsigned char *)&ack, sizeof(ack));
                
                /* Send acknowledgment frame */
                datalink_downto_physical(ack, link);
                
                /* Pass the received frame up */
                datalink_upto_network(link, frame);
            }
            /* If the frames don't match then ignore the frame */
            else
                printf("Frame out of order. Ignored\n");
    	break;
    }
}


/***************************Timeout Function********************************
                          
    PURPOSE: Function handles timeout events. The timed-out frame is resent
    along with all the frames that were sent after that frame.

****************************************************************************
**/
EVENT_HANDLER(timeouts)
{
    int link = (int)data;
    Frame frame;
    LinkedListNode* current = frameQueue[link]->head;

    /* Resend the frame that timed out */
    frame = current->frame;

    printf("Timeout, of seqno=%d on link %d\n", frame.seqno, link);
    datalink_downto_physical(frame, link);

    current = current->next;
    /* Resend all the frames after the timed out frame */
    while (current != NULL)
    {
        frame = current->frame;
        CHECK(CNET_stop_timer(lasttimer[link][frame.seqno]));

        datalink_downto_physical(frame, link);
        current = current->next;
    }   
}


/***************************Showstate Function********************************
                         
    PURPOSE: Function prints out the current state of a particular node.
    Including the expected frame number (both data and acknowledgment) and the
    contents of the sliding window.

******************************************************************************
**/
EVENT_HANDLER(showstate)
{
    int i;

    printf("\n\n##########  STATE OF NODE %d###########\n\n", nodeinfo.nodenumber);
    for (i = 1; i <= nodeinfo.nlinks; i++)
    {
        printf("\nLINK %d", i);
        printf("\n\tackExpected\t= %d\n\tnextFrameToSend\t= %d\n\tframeExpected\t= %d\n",
            ackExpected[i], nextFrameToSend[i], frameExpected[i]);
        printQueue(frameQueue[i]);
        printf("\nOVERFLOW\n");
        printQueue(overflowQueue[i]);
    }
}


/***************************Reboot Node Function********************************
                         
    PURPOSE: Sets up and initialises the queues and frame counters. Handles cnet
    events by calling the application layer and physical layer functions, and the 
    timeouts and showstate functions. 

******************************************************************************
**/
EVENT_HANDLER(reboot_node)
{
    int i;

    /* The initalisation only occurs the very first time that reboot is called */
    if (setup == TRUE)
    {
        ackExpected = (int*)calloc(nodeinfo.nlinks + 1, sizeof(int));
        frameExpected = (int*)calloc(nodeinfo.nlinks + 1, sizeof(int));
        nextFrameToSend = (int*)calloc(nodeinfo.nlinks + 1, sizeof(int));
        lasttimer = (CnetTimerID**)calloc(nodeinfo.nlinks + 1, sizeof(CnetTimerID*));
        frameQueue = (LinkedList**)calloc(nodeinfo.nlinks + 1, sizeof(LinkedList*));
        overflowQueue = (LinkedList**)calloc(nodeinfo.nlinks + 1, sizeof(LinkedList*));
        
        CNET_enable_application(ALLNODES);

        for (i = 0; i <= nodeinfo.nlinks; i++)
        {
            frameQueue[i] = createQueue();
            overflowQueue[i] = createQueue();
            lasttimer[i] = (CnetTimerID*)calloc(MAX_SEQ_NUM, sizeof(CnetTimerID));
        }
        setup = FALSE;
    }

    /* Call application_downto_network function whenever the EV_APPLICATIONREADY1 event occurs*/
    CHECK(CNET_set_handler( EV_APPLICATIONREADY, application_downto_network, 0));
    /* Call physical_ready function whenever the EV_PHYSICALREADY event occurs*/
    CHECK(CNET_set_handler( EV_PHYSICALREADY, physical_upto_datalink, 0));
    /* Call timeouts function whenever the EV_TIMER1 event occurs*/
    CHECK(CNET_set_handler( EV_TIMER1, timeouts, data));
    /* Call showstate function whenever the EV_DEBUG1 event occurs*/
    CHECK(CNET_set_handler( EV_DEBUG0, showstate, 0));
    CHECK(CNET_set_debug_string( EV_DEBUG0, "State"));
}


/**************************QUEUE FUNCTIONS********************************

    CONTAINS: Contains the necessary functions to create and manage a linked
    list implemented queue

****************************************************************************
**/
LinkedList* createQueue()
{
    LinkedList* list = (LinkedList*)malloc(sizeof(LinkedList));
    list->head = NULL;
    list->tail = NULL;
    list->size = 0;
    
    return list;
}

void addFrame(LinkedList* list, Frame frame)
{
    LinkedListNode* newNode = (LinkedListNode*)malloc(sizeof(LinkedListNode));
    newNode->frame = frame;
    newNode->next = NULL;

    ++list->size;

    /*If there are currently no nodes in the list.*/
    if (list->head == NULL)
    {
        list->head = newNode;
        list->tail = newNode;
    }
    /*If there is one or more nodes in the list.*/
    else
    {
        list->tail->next = newNode;
        list->tail = newNode;
    }
}

void removeFrame(LinkedList* list)
{
    LinkedListNode* temp = list->head;

    list->size--;

    if (list->head != NULL)
    {
        list->head = (list->head)->next;
    }
    free(temp);
}

void printQueue(LinkedList* list)
{
    /*current points to the first node in the list. */ 
    LinkedListNode* current = list->head;

    /*Iterate through the linked list and print the data field. */ 
    while (current != NULL)
    {
        printf("Seqno: %d\n", current->frame.seqno);
        current = current->next;
    }
}

Frame peekFirst(LinkedList* list)
{
    return list->head->frame;
}

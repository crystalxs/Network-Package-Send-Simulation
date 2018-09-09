#include <stdio.h>          /* for printf() and fprintf() */
#include <sys/socket.h>		/* for socket() and bind() */
#include <sys/types.h>      /* */
#include <sys/stat.h>
#include <netinet/in.h>     /* AF_INET IP address */
#include <stdlib.h>         /* for atoi() and exit() */
#include <string.h>         /* for memset() */
#include <unistd.h>         /* for close() */
#include <errno.h>          /* for errno */
#include <memory.h>         /* */
#include <time.h>           /* for time_t */

#define PORT 5208
#define SeqMaxNum 15
#define CHUNKSIZE 512

int totalSize = 0;
double totalTime;
double throughput;
time_t start;
int packetNum = 0;

struct gbnpacket
{
    int flag;   /* 0 for data packet; 1 for ACK packet */
    int seqNum;
    int length;
    char data[CHUNKSIZE];
    unsigned short checksum;
};

unsigned short CheckSum(char *addr, unsigned count);   /* Internet checksum algorithm */
void DieWithError(char *errorMessage);	/* External error handling function */

int main ()
{
    int listenSocket;                       /* Sockets */
    int senderSocket;
    struct sockaddr_in servaddr, cliaddr;   /* IP address */
    int recvMsgSize;                        /* Size of received message */
    int sendAckSize;                        /* Size of send ACK */
    int packetRcvd = -1;                    /* highest packet successfully received */
    int seqNum = 0;
    char fileName[CHUNKSIZE];
    FILE *fs;
    int writeLen;
    
    
    start = time(NULL);

    //srand48(123456789); /* seed the pseudorandom number generator */
  
    /* Create socket for sending/receiving datagrams */
    listenSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (listenSocket < 0)
        DieWithError("socket() failed");
    else
        printf("listenSocket is created.\n");

    /* Construct local address structure */
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(PORT);
    
    /* Bind to the local address */
    int status = bind(listenSocket, (struct sockaddr*)&servaddr, sizeof(servaddr));
    if (status < 0)
        DieWithError("bind() failed");
    else
        printf("Binded.\n");
    
    /* Listen to the client */
    status = listen(listenSocket, 5);
    if (status < 0)
        DieWithError("listen() failed");
    else
        printf("Listened.\n");
    
    while (1) {
        /* Accept the connection from client */
        int clilen = sizeof(cliaddr);
        senderSocket = accept(listenSocket, (struct sockaddr*)&cliaddr, (socklen_t*)&clilen);
        if (senderSocket < 0)
            DieWithError("accept() failed");
        else
            printf("Accepted.\n");
        
        /* receive file information */
        for (;;)    /* Run forever */
        {
            struct gbnpacket currPacket;    /* current packet that we're working with */
            memset(&currPacket, 0, sizeof(currPacket));
            
            /* Block until receive message from a client */
            recvMsgSize = recv(senderSocket, &currPacket, sizeof(currPacket), 0);   /* receive GBN packet */
            if (recvMsgSize == 0)
                goto END;
            else if (recvMsgSize < 0)
                DieWithError("recv() failed");
            
            currPacket.flag = ntohl(currPacket.flag);   /* convert from network to host byte order */
            currPacket.length = ntohl(currPacket.length);
            currPacket.seqNum = ntohl(currPacket.seqNum);
            currPacket.checksum = ntohl(currPacket.checksum);
            seqNum = currPacket.seqNum;
            
            /* Send ack and store in buffer */
            /* Packet arrive in order */
            
            if (CheckSum(currPacket.data, currPacket.length) == currPacket.checksum)
            {
                
                printf("Packet %d damaged\n", currPacket.seqNum);
            }
            
            else if (currPacket.flag == 2)
            {
                printf("file inforamtion received\n");
                strncpy(fileName, "receive-", strlen(currPacket.data));
                strncat(fileName, currPacket.data, strlen(currPacket.data));
                fs = fopen(fileName, "wb+");
                if (fs == NULL)
                    DieWithError("fopen() failed");
                else
                    printf("file opened.\n");
                
                /* Send back ack */
                struct gbnpacket currAck; /* ack packet */
                currAck.flag = htonl(1); /*convert to network byte order */
                seqNum = SeqMaxNum;
                currAck.seqNum = htonl(seqNum);
                currAck.length = htonl(0);
                sendAckSize = send (senderSocket, &currAck, sizeof (currAck), 0); /* send ack */
                if (sendAckSize != sizeof (currAck))
                    DieWithError("send() sent a different number of bytes than expected");
                else
                    printf ("Ack %d for file information sent\n", seqNum);
                
            }
            else if ((currPacket.flag == 0) && (seqNum == (packetRcvd + 1) % SeqMaxNum))
            {
                printf("Packet %d received\n", currPacket.seqNum);
                packetRcvd++;
                int buff_offset = 512 * packetRcvd;
                /* copy packet data to buffer */
                writeLen = fwrite(currPacket.data, sizeof(char), currPacket.length, fs);
                if (writeLen < currPacket.length)
                    DieWithError("fwriter() write a different number of bytes than expected");
                
                /* Send back ack */
                totalSize += currPacket.length;
                packetNum++;
                struct gbnpacket currAck; /* ack packet */
                currAck.flag = htonl(1); /*convert to network byte order */
                currAck.seqNum = htonl(seqNum);
                currAck.length = htonl(0);
                printf ("Ack %d sent\n", seqNum);
                printf ("Current window = [%d]\n", (seqNum + 1) % SeqMaxNum);
                sendAckSize = send (senderSocket, &currAck, sizeof (currAck), 0); /* send ack */
                if (sendAckSize != sizeof (currAck))
                    DieWithError("send() sent a different number of bytes than expected");
            }
            else if ((currPacket.flag == 0) && (seqNum < (packetRcvd + 1) % SeqMaxNum))
            {
                struct gbnpacket currAck; /* ack packet */
                currAck.flag = htonl (1); /*convert to network byte order */
                currAck.seqNum = htonl (seqNum);
                currAck.length = htonl(0);
                printf ("Ack %d sent\n", seqNum);
                printf ("Current window = [%d]\n", (seqNum + 1) % SeqMaxNum);
                sendAckSize = send (senderSocket, &currAck, sizeof (currAck), 0); /* send ack */
                if (sendAckSize != sizeof (currAck))
                    DieWithError("send() sent a different number of bytes than expected");
            }
            /* else drop packet */
        }
        /* NOT REACHED */
    END:
        fclose(fs);
        close(senderSocket);
        
        totalTime = difftime(time(NULL), start);
        throughput = totalSize / totalTime;
        printf("Effective throughput: %f\n", throughput);
        printf("Number of packets sent: %d, %d\n", packetNum, totalSize);
        printf("Total time of simulation: %.f\n", totalTime);
    }
    
    close(listenSocket);
    return 0;
}

void
DieWithError (char *errorMessage)
{
    fprintf(stderr, "%s: %s\n", errorMessage, strerror(errno));
    totalTime = difftime(time(NULL), start);
    throughput = totalSize / totalTime;
    printf("Effective throughput: %f\n", throughput);
    printf("Number of packets sent: %d, %d\n", packetNum, totalSize);
    printf("Total time of simulation: %.f\n", totalTime);
    exit (1);
}

unsigned short CheckSum(char *addr, unsigned count)
{
    register unsigned sum = 0;
    
    // Main summing loop
    while(count > 1)
    {
        sum += * (unsigned short *) addr++;
        count -= 2;
    }
    
    // Add left-over byte, if any
    if (count > 0)
        sum += * (unsigned char *) addr;
    
    // Fold 32-bit sum to 16 bits
    while (sum>>16)
        sum = (sum & 0xFFFF) + (sum >> 16);
    
    sum = ~sum;
    
    return sum;
}
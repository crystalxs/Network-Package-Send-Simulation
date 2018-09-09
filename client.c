#include <stdio.h>		/* for printf() and fprintf() */
#include <sys/socket.h>	/* for socket(), connect(), sendto(), and recvfrom() */
#include <sys/types.h>
#include <sys/stat.h>
#include <netinet/in.h> /* for AF_INET IP address */
#include <stdlib.h>		/* for atoi() and exit() */
#include <string.h>		/* for memset() */
#include <unistd.h>		/* for close() and alarm() */
#include <errno.h>		/* for errno and EINTR */
#include <signal.h>		/* for sigaction() */
#include <time.h>       /* for time_t */

#define TIMEOUT 2           /* Seconds between retransmits */
#define PORT 5208           /* Internet port */
#define SWS 3               /* Sender window size */
#define CHUNKSIZE 512       /* Size of each packet */
#define SeqMaxNum 15        /* Maximum of sequence number */
#define MaxTries 5          /* Retransmit times */

struct gbnPacket
{
    int flag;
    int seqNum;
    int length;
    char data[CHUNKSIZE];
    unsigned short checksum;
};

int tries = 0;

void DieWithError(char *errorMessage);                          /* Error handling function */
void CatchAlarm (int ignored);                                  /* Handler for SIGALRM */
int max(int a, int b);		                                    /* macros that most compilers include - used for calculating a few things */
unsigned short CheckSum(char *addr, unsigned count);   /* Internet checksum algorithm */

int main ()
{
    int senderSocket;               /* Socket descriptor */
    struct sockaddr_in servaddr;    /* IP address */
    struct sigaction myAction;	/* For setting signal handler */
    int respLen;                    /* Size of received packet */
    int sendMsgSize;                /* size of send packet */
    int packetRcvd = -1;            /* highest ack received */
    int packetSent = -1;            /* highest packet sent */
    int seqNum = 0;                 /* sequence number of packet */
    int maxPacketNum = 0;           /* number of packets to send */
    int packetNum = 0;              /* the number of packet */
    time_t start;                   /* timer */
    char fileName[CHUNKSIZE];
    int fileBlockLen;
    char buffer[512];              /* buffer */
    int totalSize = 0;
    
    printf("Please input the file name you want to send (NO MORE THAN 512):\n");
    scanf("%s", &fileName);
    getchar();
    
    /* Create a socket using TCP */
    senderSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (senderSocket < 0)
        DieWithError("socket() failed");
    else
        printf("senderSocket is created.\n");
    
    /* Set signal handler for alarm signal */
    
    myAction.sa_handler = CatchAlarm;
    if (sigfillset (&myAction.sa_mask) < 0)
        DieWithError ("sigfillset() failed");
    myAction.sa_flags = 0;
    if (sigaction (SIGALRM, &myAction, 0) < 0)
        DieWithError ("sigaction() failed for SIGALRM");
    
    /* Construct the server address structure */
    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(PORT);
    
    int status = connect(senderSocket, (struct sockaddr*)&servaddr, sizeof(servaddr));
    if (status < 0)
        DieWithError("connect() failed");
    else
        printf("Connected.\n");
    
    start = time(NULL);
    
    /* Transmit with server */
    /* Send file information */
    bzero(buffer, 512);
    strncpy(buffer, fileName, strlen(fileName));
    struct gbnPacket infoPacket;    /* current packet we're working with */
    memset(&infoPacket, 0, sizeof(infoPacket));
    infoPacket.flag = htonl(2);    /*convert to network endianness */
    int infoLength;
    infoLength = strlen(fileName);
    infoPacket.length = htonl (infoLength);
    infoPacket.seqNum = htonl(SeqMaxNum);
    memcpy(infoPacket.data, buffer, strlen(buffer));  /* copy buffer data into packet */
    infoPacket.checksum = CheckSum(infoPacket.data, infoLength); /* create internet checcksum */
    sendMsgSize = send(senderSocket, &infoPacket, sizeof(infoPacket), 0);
    if (sendMsgSize != sizeof(infoPacket))
        DieWithError("send() sent a different number of bytes than expected");
    else
        printf("File information sent with sequence number %d\n", SeqMaxNum);
    
    alarm(TIMEOUT);
    struct gbnPacket currAck;
    respLen = recv(senderSocket, &currAck, sizeof(currAck), 0);
    while (respLen < 0)
    {
        if (errno == EINTR)	/* Alarm went off  */
        {
            if (tries < MaxTries)
            {
                printf ("Packet %d *****Timed out *****\n", packetRcvd+1);
                break;
            }
            else
                DieWithError("No Response");
        }
        else
            DieWithError ("recv() failed");
    }
    /* recvfrom() got something --  cancel the timeout */
    int acktype = ntohl (currAck.flag); /* convert to host byte order */
    int ackNum = ntohl (currAck.seqNum);
    if (ackNum == 10)
        printf ("Ack %d for packet information received\n", ackNum); /* receive/handle ack */
    
    
    FILE *fs = fopen(fileName, "rb");
    if (fs == NULL)
        DieWithError("file does not found:");
    
    /* read the file */
    bzero(buffer, 512);
    fileBlockLen = 0;
    packetNum = 0;
    int ctr;    /*window size counter */
    
    while(1)
    {
        for (ctr = 0; ((ctr < SWS) && (tries < MaxTries)); ctr++)
        {
            fileBlockLen = fread(buffer, sizeof(char), 512, fs);
            if (fileBlockLen <= 0)
                goto END;
            
            packetSent = max(packetNum, packetSent); /* calc highest packet sent */
            struct gbnPacket currPacket;    /* current packet we're working with */

            memset(&currPacket, 0, sizeof(currPacket));
            currPacket.flag = htonl(0);    /*convert to network endianness */
            seqNum = packetNum % SeqMaxNum;
            currPacket.seqNum = htonl(seqNum);
            int currLength;
            currLength = strlen(buffer);
            currPacket.length = htonl (currLength);
            memcpy(infoPacket.data, buffer, currLength);  /* copy buffer data into packet */
            currPacket.checksum = CheckSum(currPacket.data, currLength); /* create internet checcksum */

            sendMsgSize = send(senderSocket, &currPacket, sizeof(currPacket), 0);
            if (sendMsgSize != sizeof(currPacket))
                DieWithError("send() sent a different number of bytes than expected");
            else
            {
                if (tries == 0)
                    printf("Packet %d sent\n", seqNum);
                else
                    printf("Packet %d Re-transmitted.\n", seqNum);
                packetNum++;
                totalSize += currLength;
            }
        }
        
        /* Get a response */
        while (packetRcvd < packetSent)
        {
            alarm(TIMEOUT);	/* Set the timeout */
            struct gbnPacket currAck;
            respLen = recv(senderSocket, &currAck, sizeof(currAck), 0);
            while (respLen < 0)
            {
                if (errno == EINTR)	/* Alarm went off  */
                {
                    if (tries < MaxTries)
                    {
                        printf ("Packet %d *****Timed out *****\n", packetRcvd+1);
                        break;
                    }
                    else
                        DieWithError("No Response");
                }
                else
                    DieWithError ("recv() failed");
            }
            /* recvfrom() got something --  cancel the timeout */
            int acktype = ntohl (currAck.flag); /* convert to host byte order */
            int ackNum = ntohl (currAck.seqNum);
            packetRcvd++;
            if ((ackNum == (packetRcvd % SeqMaxNum)) && acktype == 1)
            {
                printf ("Ack %d received\n", ackNum); /* receive/handle ack */
                printf ("Current window = [%d %d %d]\n", (ackNum + 1) % SeqMaxNum, (ackNum + 2) % SeqMaxNum, (ackNum + 3) % SeqMaxNum);
                if (packetRcvd == packetSent) /* all sent packets acked */
                {
                    alarm (0); /* clear alarm */
                    tries = 0;
                }
                else /* not all sent packets acked */
                {
                    tries = 0;
                    alarm(TIMEOUT); /* reset alarm */
                }
            }
            else
                packetRcvd--;
        }
        bzero(buffer, 512);
    }
END:
    printf("Transfer file finished!\n");
    
    fclose(fs);
    close(senderSocket);
    exit (0);
}

void CatchAlarm (int ignored)	/* Handler for SIGALRM */
{
    tries += 1;
}

void DieWithError (char *errorMessage)
{
    fprintf(stderr, "%s: %s\n", errorMessage, strerror(errno));
    exit (1);
}

int max (int a, int b)
{
    if (b > a)
        return b;
    return a;
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
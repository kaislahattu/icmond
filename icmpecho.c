/*
 * icmpecho.c - 2016 Jani Tammi <janitammi@gmail.com>
 */
#include <stdio.h>          // icmp_dump() needs printf()
#include <unistd.h>         // EXIT_SUCCESS, EXIT_FAILURE
#include <stdlib.h>         // malloc()
#include <stdbool.h>        // true, false
#include <string.h>         // memset()
#include <netdb.h>          // gethostbyname()
#include <fcntl.h>          // fcntl()
#include <netinet/in.h>     //
#include <arpa/inet.h>      // icmp_dump() needs inet_ntoa()
#include <sys/timerfd.h>    // timerfd_create()

#include "icmpecho.h"
#include "logwrite.h"

/*
 * checksum() - Standard 1s complement checksum
 * Copyright (c) 2000 Sean Walton and Macmillan Publishers.
 *
 *      Needs to be checked!
 *      RFC 791 : "The checksum field is the 16 bit one's complement of the
 *                 one's complement sum of all 16 bit words in the header.
 *                 For purposes of computing the checksum, the value of the
 *                 checksum field is zero."
 *      => This function may be called only with a header that has it's
 *         checksum field filled with zeros!
 */
static inline unsigned short checksum(void *b, size_t len)
{
    unsigned short *buf = b; /* for indexing convenience */
    unsigned int    sum;

    for (sum = 0; len > 1; len -= 2)
        sum += *buf++;
    if (len == 1)
        sum += *(unsigned char*)buf;
    sum  = (sum >> 16) + (sum & 0xFFFF);
    sum += (sum >> 16);
    return ~sum;
}


/*
 * host     name or IP
 * timeout  in milliseconds
 */
struct icmpecho_t *icmp_prepare(const char *host, int timeout)
{
    /*
     * Allocate icmpecho_t
     */
    struct icmpecho_t *icmp = malloc(sizeof(struct icmpecho_t));
    memset(icmp, 0, sizeof(struct icmpecho_t));

    /*
     * Create timer spec and fd
     */
    icmp->timeoutspec.it_value.tv_sec     = timeout / 1000;
    icmp->timeoutspec.it_value.tv_nsec    = (timeout % 1000) * 1000000;
    /* Interval = 0 (do not repeat) */
    icmp->timeoutspec.it_interval.tv_sec  = 0;
    icmp->timeoutspec.it_interval.tv_nsec = 0;
    if ((icmp->timeoutfd = timerfd_create(CLOCK_REALTIME, 0)) == -1)
    {
        logerr("timerfd_create()");
        exit(EXIT_FAILURE);
    }

    /* THIS ONE WILL CAUSE PROBLEMS WITH NAME LOOKUPS AND NOT FOUND ETC!! */
    if (!(icmp->host = gethostbyname(host)))
    {
        logerr("gethostbyname(\"%s\") could not resolve host!", host);
        exit(EXIT_FAILURE);
    }
    icmp->socket_address.sin_family      = icmp->host->h_addrtype; /* always AF_INET or AF_INET6 at present. */
    icmp->socket_address.sin_port        = 0;
    icmp->socket_address.sin_addr.s_addr = *(long*)(icmp->host->h_addr);
    /* any use for struct hostent is over at this point */

    /*
     * Set PING ready, but DO NOT LAUNCH yet
     */
     /* Open raw socket */
	if ((icmp->sendfd = socket(PF_INET, SOCK_RAW, ICMPECHO_PROTOCOL)) < 0)
	{
		logerr("sendfd = socket(PF_INET, SOCK_RAW, ICMPECHO_PROTOCOL)");
		exit(EXIT_FAILURE);
	}

    // Set TTL (at IP level; "SOL_IP").
    const int ttl_value = ICMPECHO_IP_TTL_VALUE;
	if (setsockopt(icmp->sendfd, SOL_IP, IP_TTL, &ttl_value, sizeof(ttl_value)) != 0)
    {
		logerr("setsockopt() setting TTL");
        exit(EXIT_FAILURE);
    }

    // Non-blocking... but why?
	if (fcntl(icmp->sendfd, F_SETFL, O_NONBLOCK) != 0)
    {
		logerr("fcntl() setting O_NONBLOCK");
        exit(EXIT_FAILURE);
    }
/*
    // Why do we need to read the socket now?
    struct sockaddr_in r_addr;
    socklen_t socklen = (socklen_t)sizeof(r_addr);
    if (recvfrom(
                icmp->sendfd,
                &(icmp->packet),
                sizeof(struct packet_t),
                0,
                (struct sockaddr*)&r_addr,
                &socklen
                ) > 0)
        logerr("***Got message!***\n");
*/
    /* Prepare ICMP Echo Request packet */
    icmp->packet.header.type = ICMP_ECHO;
    icmp->packet.header.un.echo.id = getpid();
    /* Payload should be timestamp */
    int i, cnt = 1;
    /* fill payload with garbage */
    for (i = 0; i < sizeof(icmp->packet.payload) - 1; i++)
        icmp->packet.payload[i] = i + '0';
    icmp->packet.payload[i] = 0; // null terminate
    icmp->packet.header.un.echo.sequence = cnt++;
    icmp->packet.header.checksum = checksum(&(icmp->packet), sizeof(struct packet_t));

    /*
     * Set up listening, but DO NOT enter pselect() yet
     */
    if ((icmp->recvfd = socket(PF_INET, SOCK_RAW, ICMPECHO_PROTOCOL)) < 0)
    {
		logerr("socket(PF_INET, SOCK_RAW, ICMPECHO_PROTOCOL)");
        exit(EXIT_FAILURE);
    }

    return icmp;
}
 
int icmp_send(struct icmpecho_t *icmp)
{
    clock_gettime(CLOCK_MONOTONIC, &(icmp->timesent));
    icmp->sent_and_listening = true;
    if (sendto(
              icmp->sendfd,
              &(icmp->packet),
              sizeof(struct packet_t),
              0,
              (struct sockaddr*)&icmp->socket_address,
              sizeof(struct sockaddr_in)
              ) <= 0 )
    {
        logerr("sendto()");
        exit(EXIT_FAILURE);
    }
    return 0; // Haven't really figured out the return value scheme...
}

int icmp_receive(struct icmpecho_t *icmp)
{
    int             bytes;
    unsigned char   buffer[1024]; // just ignored atm
    clock_gettime(CLOCK_MONOTONIC, &(icmp->timerecv));
    icmp->sent_and_listening = false;

    socklen_t socket_address_len = sizeof(struct sockaddr_in);
    memset(buffer, 0, sizeof(buffer));
    bytes = recvfrom(
                    icmp->recvfd,
                    buffer,
                    sizeof(buffer),
                    0,
                    (struct sockaddr*)&(icmp->socket_address),
                    &socket_address_len
                    );
    if (bytes > 0)
    {
//dump_mem(&buffer, bytes);
    }
    else
    {
        logerr("recvfrom()");
        exit(EXIT_FAILURE);
    }
    close(icmp->sendfd);
    close(icmp->recvfd);
    icmp->sendfd = 0;
    icmp->recvfd = 0;
    return 0;
}

void icmp_cancel(struct icmpecho_t *icmp)
{
    icmp->sent_and_listening = false;
    // reset timesent info
    icmp->timesent.tv_sec    = 0;
    icmp->timesent.tv_nsec   = 0;
    close(icmp->sendfd);
    close(icmp->recvfd);
    icmp->sendfd = 0;
    icmp->recvfd = 0;
}

/*
 * Return delay in milliseconds
 */
double icmp_getelapsed(struct icmpecho_t *icmp)
{
    if (icmp->timesent.tv_nsec > icmp->timerecv.tv_nsec)
    {
        return (icmp->timerecv.tv_sec  - icmp->timesent.tv_sec - 1) * 1.0e3 +
               (1000000000 - (icmp->timesent.tv_nsec - icmp->timerecv.tv_nsec)) / 1.0e6;
    }
    return (icmp->timerecv.tv_sec  - icmp->timesent.tv_sec) * 1.0e3 +
           (icmp->timerecv.tv_nsec - icmp->timesent.tv_nsec) / 1.0e6;
}

void icmp_dump(struct icmpecho_t *icmp)
{
    if (!icmp)
    {
        logerr("NULL pointer received!");
        return;
    }

    printf("icmpecho_t.sent_and_listening : %s\n", icmp->sent_and_listening ? "TRUE" : "FALSE");
    printf("icmpecho_t.hostent * : 0x%.8X\n", (unsigned int)icmp->host);
    if (icmp->host)
    {
        // struct hostent <netdb.h>
        printf("icmpecho_t.hostent.h_name     : \"%s\"\n", icmp->host->h_name);
/* All .h_aliases values seem to point to non-accessible memory space ??
   No matter, as printing them out is hardly necessary at this moment...
    char **ptr;
        printf("icmpecho_t.hostent.h_aliases  : \"");
//    printf("0x%.8X\"\n", (unsigned int)icmp->hostname->h_aliases);
        for (ptr = icmp->hostname->h_aliases; ptr; ptr++)
        {
            printf("%s", *ptr);
            if (ptr + 1)
                printf(",");
        }
        printf("\"\n");
*/
        printf("icmpecho_t.hostent.h_addrtype : %s\n", icmp->host->h_addrtype == AF_INET ? "AF_INET" : "AF_INET6");
        printf("icmpecho_t.hostent.h_length   : %d\n", icmp->host->h_length);
//        printf("icmpecho_t.hostent.h_addr_list  : \"TBA\"\n");
    }
    // file descriptors
    printf("icmpecho_t.sendfd    : 0x%.8X\n", icmp->sendfd);
    printf("icmpecho_t.recvfd    : 0x%.8X\n", icmp->recvfd);
    printf("icmpecho_t.timeoutfd : 0x%.8X\n", icmp->timeoutfd);
    // struct timespec
    printf("icmpecho_t.timespec.it_value.tv_sec     : %d\n", (int)icmp->timeoutspec.it_value.tv_sec);
    printf("icmpecho_t.timespec.it_value.tv_nsec    : %d\n", (int)icmp->timeoutspec.it_value.tv_nsec);
    printf("icmpecho_t.timespec.it_interval.tv_sec  : %d\n", (int)icmp->timeoutspec.it_interval.tv_sec);
    printf("icmpecho_t.timespec.it_interval.tv_nsec : %d\n", (int)icmp->timeoutspec.it_interval.tv_nsec);
    // struct sockaddr_in <netinet/in.h>
    printf("icmpecho_t.sockaddr_in.sin_family      : %s\n", icmp->socket_address.sin_family == AF_INET ? "AF_INET" : "AF_INET6");
    printf("icmpecho_t.sockaddr_in.sin_port        : %d\n", icmp->socket_address.sin_port);
    printf("icmpecho_t.sockaddr_in.sin_addr.s_addr : %s\n", inet_ntoa(icmp->socket_address.sin_addr));
    // struct packet_t.icmphdr <netinet/ip_icmp.h>
    printf("icmpecho_t.packet_t.icmphdr.type             : %d\n", icmp->packet.header.type);
    printf("icmpecho_t.packet_t.icmphdr.code             : %d\n", icmp->packet.header.code);
    printf("icmpecho_t.packet_t.icmphdr.checksum         : 0x%.4d\n", icmp->packet.header.checksum);
    printf("icmpecho_t.packet_t.icmphdr.un.echo.id       : %d\n", icmp->packet.header.un.echo.id);
    printf("icmpecho_t.packet_t.icmphdr.un.echo.sequence : %d\n", icmp->packet.header.un.echo.sequence);
    // struct packet_t.payload
    printf("icmpecho_t.packet_t.payload : \"%s\"\n", icmp->packet.payload);
    // struct timespec
    printf("icmpecho_t.timesent : %.12d.%012d\n", (int)icmp->timesent.tv_sec, (int)icmp->timesent.tv_nsec);
    printf("icmpecho_t.timerecv : %.12d.%012d\n", (int)icmp->timerecv.tv_sec, (int)icmp->timerecv.tv_nsec);
    
}
/* EOF icmpecho.c */

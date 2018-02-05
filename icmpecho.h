/*
 * icmpecho.h - 2016 Jani Tammi <janitammi@gmail.com>
 */
#include <time.h>               /* clock_gettime(), struct timespec         */
#include <netdb.h>              /* struct hostent                           */
#include <netinet/in.h>         /* struct socaddr_in                        */
#include <netinet/ip_icmp.h>    /* struct icmphdr                           */

#ifndef __ICMPECHO_H__
#define __ICMPECHO_H__

#define ICMPECHO_PACKETSIZE  	64		// This needs some re-thinking...
#define ICMPECHO_PROTOCOL		1		// As in specifications, cannot change, ever
#define ICMPECHO_IP_TTL_VALUE	255		// Number or routing hops allowed

/* TODO: remove optional data from packet */
/*
    The payload may include a timestamp indicating the time of transmission and
    a sequence number, which are not found in this example. This allows ping to
    compute the round trip time in a stateless manner without needing to record
    the time of transmission of each packet.

    icmphdr.un.echo.id and icmphdr.un.echo.sequence can be used to match send
    and receive (and that's the ONLY reason they exist), but the above is true
    in that no record keeping for send times is necessary (or rather, the record
    travels with the datagram).
*/
/* Sub-Structures:
struct hostent {        // <netdb.h>
    char *  h_name;             // official name of host
    char ** h_aliases;          // alias list
    int     h_addrtype;         // host address type
    int     h_length;           // length of address in bytes (always 4 bytes, for AF_INET?? WHAT??)
    char ** h_addr_list;        // list of addresses, NULL terminated vector
}
struct sockaddr_in {    // <netinet/in.h>
    short   sin_family;         // should be AF_INET
    u_short sin_port;           // system: 0 - 1023, IANA registered: 1024 - 49151, ephemeral: 49152 - 65535
    struct  in_addr sin_addr;   // 32-bit IP address
    char    sin_zero[8];        // not used, must be zero
};
struct in_addr {        // <netinet/in.h>
    unsigned long s_addr;       // 32-bit IP address
};
struct icmphdr          // <netinet/ip_icmp.h>
{
    u_int8_t  type;             // message type (ICMP_ECHO in our case, which is always "8")
    u_int8_t  code;             // type sub-code (always zero for ICMP_ECHO)
    u_int16_t checksum;         // one's complement ...
    union
    {
        struct
        {
            u_int16_t id;       // "may be used to match echo requests to the associated reply"
            u_int16_t sequence; // "may be used to match echo requests to the associated reply"
        } echo;                 // echo datagram (THIS IS WHAT WE HAVE!)
        u_int32_t     gateway;  // gateway address
        struct
        {
            u_int16_t __glibc_reserved;
            u_int16_t mtu;
        } frag;                 // path mtu discovery
    } un;
};

*/
struct icmpecho_t
{
    struct hostent     *host;
    int                 sendfd;
	int					recvfd;
	int					timeoutfd;
	struct itimerspec   timeoutspec;
    struct sockaddr_in  socket_address;
	int					sent_and_listening;		// TRUE when Echo Request is sent and not yet received (or timed out)

    struct packet_t
    {
        struct icmphdr  header;
        char            payload[ICMPECHO_PACKETSIZE - sizeof(struct icmphdr)];
    } packet;
	// These are simply used to record time to determine ping echo delay
    // ICMP payload should be utilized soon...
	struct timespec		timesent;		// clock_gettime() sent time
	struct timespec		timerecv;		// clock_gettime() received time
};

struct icmpecho_t * icmp_prepare(const char *, int);
int 				icmp_send(struct icmpecho_t *);
int					icmp_receive(struct icmpecho_t *);
void				icmp_cancel(struct icmpecho_t *);
double				icmp_getelapsed(struct icmpecho_t *);
void                icmp_dump(struct icmpecho_t *);

#endif /* __ICMPECHO_H__ */

/* EOF */

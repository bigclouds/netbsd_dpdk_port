/*
 * app_glue.c
 *
 *  Created on: Jul 6, 2014
 *      Author: Vadim Suraev vadim.suraev@gmail.com
 *  Contains API functions for building applications
 *  on the top of Linux TCP/IP ported to userland and integrated with DPDK 1.6
 */
//#include <stdint.h>
//#include <special_includes/sys/types.h>
#include <special_includes/sys/param.h>
#include <special_includes/sys/malloc.h>
#include <lib/libkern/libkern.h>
#include <special_includes/sys/mbuf.h>
#include <special_includes/sys/queue.h>
#include <special_includes/sys/socket.h>
#include <special_includes/sys/socketvar.h>
#include <special_includes/sys/time.h>
#include <special_includes/sys/poll.h>
#include <netbsd/netinet/in.h>

#include <netbsd/net/if.h>
#include <netbsd/net/route.h>
#include <netbsd/net/if_types.h>

#include <netbsd/netinet/in.h>
#include <netbsd/netinet/in_systm.h>
#include <netbsd/netinet/ip.h>
#include <netbsd/netinet/in_pcb.h>
#include <netbsd/netinet/in_var.h>
#include <netbsd/netinet/ip_var.h>
#include <netbsd/netinet/in_offload.h>

#ifdef INET6
#ifndef INET
#include <netbsd/netinet/in.h>
#endif
#include <netbsd/netinet/ip6.h>
#include <netbsd/netinet6/ip6_var.h>
#include <netbsd/netinet6/in6_pcb.h>
#include <netinet6/ip6_var.h>
#include <netinet6/in6_var.h>
#include <netbsd/netinet/icmp6.h>
#include <netbsd/netinet6/nd6.h>
#ifdef TCP_SIGNATURE
#include <netbsd/netinet6/scope6_var.h>
#endif
#endif

#ifndef INET6
/* always need ip6.h for IP6_EXTHDR_GET */
#include <netbsd/netinet/ip6.h>
#endif

#include <netbsd/netinet/tcp.h>
#include <netbsd/netinet/tcp_fsm.h>
#include <netbsd/netinet/tcp_seq.h>
#include <netbsd/netinet/tcp_timer.h>
#include <netbsd/netinet/tcp_var.h>
#include <netbsd/netinet/tcp_private.h>
#include <service_log.h>
#include <glue/app_glue.h>

TAILQ_HEAD(read_ready_socket_list_head, socket) read_ready_socket_list_head;
uint64_t read_sockets_queue_len = 0;
TAILQ_HEAD(closed_socket_list_head, socket) closed_socket_list_head;
TAILQ_HEAD(write_ready_socket_list_head, socket) write_ready_socket_list_head;
uint64_t write_sockets_queue_len = 0;
TAILQ_HEAD(accept_ready_socket_list_head, socket) accept_ready_socket_list_head;

/*
 * This callback function is invoked when data arrives to socket.
 * It inserts the socket into a list of readable sockets
 * which is processed in periodic function app_glue_periodic
 * Paramters: a pointer to struct sock, len (dummy)
 * Returns: void
 *
 */

static inline void app_glue_sock_readable(struct socket *so)
{
	if (so->so_state & SS_ISDISCONNECTED) {
		if(so->closed_queue_present) {
			return;
		}
		so->closed_queue_present = 1;
		TAILQ_INSERT_TAIL(&closed_socket_list_head,so,closed_queue_entry);
		return;
	}
	if(so->so_type == SOCK_STREAM) {
		struct tcpcb *tp = sototcpcb(so);

		if(tp && (tp->t_state != TCPS_ESTABLISHED)) {
			if(tp->t_state == TCPS_LISTEN) {
				if(so->accept_queue_present) {
					return;
				}
				so->accept_queue_present = 1;
				TAILQ_INSERT_TAIL(&accept_ready_socket_list_head,so,accept_queue_entry);
			}
			return;
		}
	}
	if(so->read_queue_present) {
		return;
	}
	so->read_queue_present = 1;
	TAILQ_INSERT_TAIL(&read_ready_socket_list_head,so,read_queue_entry);
        read_sockets_queue_len++;
}
/*
 * This callback function is invoked when data canbe transmitted on socket.
 * It inserts the socket into a list of writable sockets
 * which is processed in periodic function app_glue_periodic
 * Paramters: a pointer to struct sock
 * Returns: void
 *
 */
static void app_glue_sock_write_space(struct socket *so)
{
	if (so->so_state & SS_ISDISCONNECTED) {
		if(so->closed_queue_present) {
			return;
		}
		so->closed_queue_present = 1;
		TAILQ_INSERT_TAIL(&closed_socket_list_head,so,closed_queue_entry);
		return;
	}
	if(so->so_type == SOCK_STREAM) {
		struct tcpcb *tp = sototcpcb(so);

		if(tp && (tp->t_state != TCPS_ESTABLISHED)) {
			return;
		}
	}
	if (sowritable(so)) {
		if(so->write_queue_present) {
			return;
		}
		so->write_queue_present = 1;
		TAILQ_INSERT_TAIL(&write_ready_socket_list_head,so,write_queue_entry);
                write_sockets_queue_len++;
	}
}
#if 0
/*
 * This callback function is invoked when an error occurs on socket.
 * It inserts the socket into a list of closable sockets
 * which is processed in periodic function app_glue_periodic
 * Paramters: a pointer to struct sock
 * Returns: void
 *
 */
static void app_glue_sock_error_report(struct sock *sk)
{
	if(sk->sk_socket) {
		if(sk->sk_socket->closed_queue_present) {
			return;
		}
		sk->sk_socket->closed_queue_present = 1;
		TAILQ_INSERT_TAIL(&closed_socket_list_head,sk->sk_socket,closed_queue_entry);
	}
}
/*
 * This callback function is invoked when a new connection can be accepted on socket.
 * It looks up the parent (listening) socket for the newly established connection
 * and inserts it into the accept queue
 * which is processed in periodic function app_glue_periodic
 * Paramters: a pointer to struct sock
 * Returns: void
 *
 */
static void app_glue_sock_wakeup(struct sock *sk)
{
	struct sock *sock;
        struct tcp_sock *tp;
        tp = tcp_sk(sk);

	sock = __inet_lookup_listener(&init_net/*sk->sk_net*/,
			&tcp_hashinfo,
			sk->sk_daddr,
			sk->sk_dport/*__be16 sport*/,
			sk->sk_rcv_saddr,
			ntohs(tp->inet_conn.icsk_inet.inet_sport),//sk->sk_num/*const unsigned short hnum*/,
			sk->sk_bound_dev_if);
	if(sock) {
		if(sock->sk_socket->accept_queue_present) {
			return;
		}
		sock->sk_socket->accept_queue_present = 1;
		TAILQ_INSERT_TAIL(&accept_ready_socket_list_head,sock->sk_socket,accept_queue_entry);
	}
        else {
              struct tcp_sock *tp;
              tp = tcp_sk(sk);
              printf("%s %d %x %d %x %d %d \n",__FILE__,__LINE__,sk->sk_daddr,sk->sk_dport,sk->sk_rcv_saddr,sk->sk_num,tp->inet_conn.icsk_inet.inet_sport);
              return;
        }
	sock_reset_flag(sk,SOCK_USE_WRITE_QUEUE);
	sk->sk_data_ready = app_glue_sock_readable;
	sk->sk_write_space = app_glue_sock_write_space;
	sk->sk_error_report = app_glue_sock_error_report; 
}
#endif
static void app_glue_so_upcall(struct socket *so, void *arg, int events, int waitflag)
{
	if(events | POLLIN) {
		app_glue_sock_readable(so);
	}
	if(events | POLLOUT) {
		app_glue_sock_write_space(so);
	}
}
static void app_glue_so_upcall2(struct socket *sock, void *arg, int band, int flag)
{
	if(band | POLLIN) {
		app_glue_sock_readable(sock);
	}
	if(band | POLLOUT) {
		app_glue_sock_write_space(sock);
	}
}

void *app_glue_create_socket(int family,int type)
{
	struct timeval tv;
	struct socket *sock = NULL;

	if(socreate(family, &sock, type, 0/*port*/, NULL)) {
		printf("cannot create socket %s %d\n",__FILE__,__LINE__);
		return NULL;
	}
#if 0
	tv.tv_sec = -1;
	tv.tv_usec = 0;
	if (app_glue_setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, sizeof(tv), (char *)&tv)) {
		service_log(SERVICE_LOG_ERR,"%s %d cannot set notimeout option\n",__FILE__,__LINE__);
	}
	tv.tv_sec = -1;
	tv.tv_usec = 0;
	if (app_glue_setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, sizeof(tv), (char *)&tv)) {
		service_log(SERVICE_LOG_ERR,"%s %d cannot set notimeout option\n",__FILE__,__LINE__);
	}
#endif
//	sock->so_upcall = app_glue_so_upcall;
	sock->so_upcall2 = app_glue_so_upcall2;
#if 0
	if(type != SOCK_STREAM) {
		if(sock->sk) {
            		sock_reset_flag(sock->sk,SOCK_USE_WRITE_QUEUE);
            		sock->sk->sk_data_ready = app_glue_sock_readable;
            		sock->sk->sk_write_space = app_glue_sock_write_space;
            		app_glue_sock_write_space(sock->sk);
		}
	}
#endif
	return sock;
}

int app_glue_v4_bind(void *so,unsigned int ipaddr, unsigned short port)
{
	struct socket *sock = (struct socket *)so;
	struct mbuf *m;
	struct sockaddr_in *sin;
		
	m = m_get(M_WAIT, MT_SONAME);
	if (!m) {
		printf("cannot create socket %s %d\n",__FILE__,__LINE__);
		return -1;
	}
	m->m_len = sizeof(struct sockaddr_in);
	sin = mtod(m, struct sockaddr_in *);
	memset(sin, 0, sizeof(*sin));
	sin->sin_len = sizeof(struct sockaddr_in);
	sin->sin_family = AF_INET;

	sin->sin_addr.s_addr = ipaddr;
	sin->sin_port = htons(port);

	if(sobind(sock,m)) {
		printf("cannot bind %s %d\n",__FILE__,__LINE__);
		return -1;
	}
	m_freem(m);
	return 0;
}

int app_glue_v4_connect(void *so,unsigned int ipaddr,unsigned short port)
{
	struct socket *sock = (struct socket *)so;
	struct mbuf *m;
	struct sockaddr_in *sin;
	unsigned short my_port;
		
	m = m_get(M_WAIT, MT_SONAME);
	if (!m) {
		printf("cannot create socket %s %d\n",__FILE__,__LINE__);
		return -1;
	}
	m->m_len = sizeof(struct sockaddr);
	sin = mtod(m, struct sockaddr_in *);
	sin->sin_len = sizeof(struct sockaddr_in);
	sin->sin_family = AF_INET;
	while(1) {
		sin->sin_addr.s_addr = 0 /*my_ip_addr*/;
		if(my_port) {
			sin->sin_port = htons(my_port);
		}
		else {
			sin->sin_port = htons(rand() & 0xffff);
		}
		if(sobind(sock,m)) {
			printf("cannot bind %s %d\n",__FILE__,__LINE__);
			if(my_port) {
				break;
			}
			continue;
		}
		break;
	}
	sin->sin_family = AF_INET;
	sin->sin_addr.s_addr = ipaddr;
	sin->sin_port = htons(port);
#if 0
	if(client_sock->sk) {
		client_sock->sk->sk_state_change = app_glue_sock_wakeup;
	}
#endif	
	soconnect(sock, m);
	m_freem(m);	
	return 0;
}

int app_glue_v4_listen(void *so)
{
	struct socket *sock = (struct socket *)so;
//	sock->so_upcall2 = app_glue_so_upcall;
	if(solisten(sock,32000)) {
		printf("cannot listen %s %d\n",__FILE__,__LINE__);
		return -1;
	}
	return 0;
}

static void *port_2_ifp[10] = { 0 };

static void app_glue_set_port_ifp(int portnum, void *ifp)
{
	if (portnum >= 10)
		return;
	port_2_ifp[portnum] = ifp;
}
/*
 * This function polls the driver for the received packets.Called from app_glue_periodic
 * Paramters: ethernet port number.
 * Returns: None
 *
 */
void app_glue_poll(int port_num)
{
	softint_run();
	if (port_num >= 10)
		return;
	poll_rx(port_2_ifp[port_num],port_num,0);
}

/*
 * This function must be called before app_glue_periodic is called for the first time.
 * It initializes the readable, writable and acceptable (listeners) lists
 * Paramters: None
 * Returns: None
 *
 */
void app_glue_init()
{
	TAILQ_INIT(&read_ready_socket_list_head);
	TAILQ_INIT(&write_ready_socket_list_head);
	TAILQ_INIT(&accept_ready_socket_list_head);
	TAILQ_INIT(&closed_socket_list_head);
}
/*
 * This function walks on closable, acceptable and readable lists and calls.
 * the application's (user's) function. Called from app_glue_periodic
 * Paramters: None
 * Returns: None
 *
 */
void process_rx_ready_sockets()
{
	struct socket *sock;
        uint64_t idx,limit;

	while(!TAILQ_EMPTY(&closed_socket_list_head)) {
		sock = TAILQ_FIRST(&closed_socket_list_head);
//		user_on_socket_fatal(sock);
		user_on_closure(app_glue_get_glueing_block(sock));
		sock->closed_queue_present = 0;
		TAILQ_REMOVE(&closed_socket_list_head,sock,closed_queue_entry);
	}
	while(!TAILQ_EMPTY(&accept_ready_socket_list_head)) {

		sock = TAILQ_FIRST(&accept_ready_socket_list_head);
		user_on_accept(sock);
		sock->accept_queue_present = 0;
		TAILQ_REMOVE(&accept_ready_socket_list_head,sock,accept_queue_entry);
	}
        idx = 0;
        limit = read_sockets_queue_len;
	while((idx < limit)&&(!TAILQ_EMPTY(&read_ready_socket_list_head))) {
		sock = TAILQ_FIRST(&read_ready_socket_list_head);
                sock->read_queue_present = 0;
		TAILQ_REMOVE(&read_ready_socket_list_head,sock,read_queue_entry);
                user_data_available_cbk(sock, sock->glueing_block);
                read_sockets_queue_len--;
                idx++;	
	}
}
/*
 * This function walks on writable lists and calls.
 * the application's (user's) function. Called from app_glue_periodic
 * Paramters: None
 * Returns: None
 *
 */
void process_tx_ready_sockets()
{
	struct socket *sock;
        uint64_t idx,limit;
 
        idx = 0;
        limit = write_sockets_queue_len;
	while((idx < limit)&&(!TAILQ_EMPTY(&write_ready_socket_list_head))) {
		sock = TAILQ_FIRST(&write_ready_socket_list_head);
		TAILQ_REMOVE(&write_ready_socket_list_head,sock,write_queue_entry);
                sock->write_queue_present = 0;
		service_on_transmission_opportunity(sock, sock->glueing_block);
//                set_bit(SOCK_NOSPACE, &sock->flags);
                write_sockets_queue_len--;
	        idx++;
	}
}

/*
 * This function may be called to attach user's data to the socket.
 * Paramters: a pointer  to socket (returned, for example, by create_*_socket)
 * a pointer to data to be attached to the socket
 * Returns: None
 *
 */
void app_glue_set_glueing_block(void *socket,void *data)
{
	struct socket *sock = socket;

	if (sock == NULL)
		return;

	sock->glueing_block = data;
}
/*
 * This function may be called to get attached to the socket user's data .
 * Paramters: a pointer  to socket (returned, for example, by create_*_socket,)
 * Returns: pointer to data to be attached to the socket
 *
 */
void *app_glue_get_glueing_block(void *socket)
{
	struct socket *sock = socket;

	if (sock == NULL)
		return NULL;

	return sock->glueing_block;
}

/*
 * This function may be called to close socket .
 * Paramters: a pointer to socket structure
 * Returns: None
 *
 */
void app_glue_close_socket(void *sk)
{
	struct socket *sock = (struct socket *)sk;
	
	if(sock->read_queue_present) {
		TAILQ_REMOVE(&read_ready_socket_list_head,sock,read_queue_entry);
		sock->read_queue_present = 0;
	}
	if(sock->write_queue_present) {
		TAILQ_REMOVE(&write_ready_socket_list_head,sock,write_queue_entry);
		sock->write_queue_present = 0;
	}
	if(sock->accept_queue_present) {
		while(sock->so_qlen) {
	    		struct socket *so2 = TAILQ_FIRST(&sock->so_q);
			struct mbuf *addr = m_get(M_WAIT, MT_SONAME);
	
			if (soqremque(so2, 1) == 0) {
				break;
			}
		    	soaccept(so2,addr);
			m_freem(addr);
    		}

		TAILQ_REMOVE(&accept_ready_socket_list_head,sock,accept_queue_entry);
		sock->accept_queue_present = 0;
	}
	if(sock->closed_queue_present) {
		TAILQ_REMOVE(&closed_socket_list_head,sock,closed_queue_entry);
		sock->closed_queue_present = 0;
	}
#if 0
	if(sock->sk)
		sock->sk->sk_user_data = NULL;
#endif
	soclose(sock);
}
/*
 * This function may be called to estimate amount of data can be sent .
 * Paramters: a pointer to socket structure
 * Returns: number of bytes the application can send
 *
 */
int app_glue_calc_size_of_data_to_send(void *sock)
{
#if 0
	int bufs_count1,bufs_count2,bufs_count3,stream_space,bufs_min;
	struct sock *sk = ((struct socket *)sock)->sk;
	if(!sk_stream_is_writeable(sk)) {
		return 0;
	}
	bufs_count1 = kmem_cache_get_free(get_fclone_cache());
	bufs_count2 = kmem_cache_get_free(get_header_cache());
	bufs_count3 = get_buffer_count();
	if(bufs_count1 > 2) {
		bufs_count1 -= 2;
	}
	if(bufs_count2 > 2) {
		bufs_count2 -= 2;
	}
	bufs_min = min(bufs_count1,bufs_count2);
	bufs_min = min(bufs_min,bufs_count3);
	if(bufs_min <= 0) {
		return 0;
	}
	stream_space = sk_stream_wspace(((struct socket *)sock)->sk);
	return min(bufs_min << 10,stream_space);
#else
	return 0;
#endif
}
uint64_t mbufs_allocated_for_tx = 0;
extern uint64_t mbufs_allocated_for_rx;
extern uint64_t mbufs_freed_for_tx;
extern uint64_t get_mbuf_called;
extern uint64_t get_mbuf_failed;
extern  uint64_t mbuf_free_called;
extern uint64_t pmd_received;
extern uint64_t pmd_transmitted;
uint64_t mbufs_freed_for_rx = 0;
int print_stats_thread_cpuid = 0;
void app_glue_print_stats()
{
#define PRINT_TCP_STATS(name) service_log(SERVICE_LOG_INFO,"%s %lu\n",#name,(uint64_t)(tcp_stat[TCP_STAT_##name]));
	while(1) {
		uint64_t *tcp_stat = TCP_STAT_GETREF_CPUID(print_stats_thread_cpuid);
		PRINT_TCP_STATS(CONNATTEMPT);
		PRINT_TCP_STATS(ACCEPTS);
		PRINT_TCP_STATS(CONNECTS);
		PRINT_TCP_STATS(DROPS);
		PRINT_TCP_STATS(CONNDROPS);
		PRINT_TCP_STATS(CLOSED);
		PRINT_TCP_STATS(SEGSTIMED);
		PRINT_TCP_STATS(RTTUPDATED);
		PRINT_TCP_STATS(DELACK);
		PRINT_TCP_STATS(TIMEOUTDROP);
		PRINT_TCP_STATS(REXMTTIMEO);
		PRINT_TCP_STATS(PERSISTTIMEO);
		PRINT_TCP_STATS(KEEPTIMEO);
		PRINT_TCP_STATS(KEEPPROBE);
		PRINT_TCP_STATS(KEEPDROPS);
		PRINT_TCP_STATS(PERSISTDROPS);
		PRINT_TCP_STATS(CONNSDRAINED);
		PRINT_TCP_STATS(PMTUBLACKHOLE);
		PRINT_TCP_STATS(SNDTOTAL);
		PRINT_TCP_STATS(SNDPACK);
		PRINT_TCP_STATS(SNDBYTE);
		PRINT_TCP_STATS(SNDREXMITPACK);
		PRINT_TCP_STATS(SNDREXMITBYTE);
		PRINT_TCP_STATS(SNDACKS);
		PRINT_TCP_STATS(SNDPROBE);
		PRINT_TCP_STATS(SNDURG);
		PRINT_TCP_STATS(SNDWINUP);
		PRINT_TCP_STATS(SNDCTRL);
		PRINT_TCP_STATS(RCVTOTAL);
		PRINT_TCP_STATS(RCVPACK);
		PRINT_TCP_STATS(RCVBYTE);
		PRINT_TCP_STATS(RCVBADSUM);
		PRINT_TCP_STATS(RCVBADOFF);
		PRINT_TCP_STATS(RCVMEMDROP);
		PRINT_TCP_STATS(RCVSHORT);
		PRINT_TCP_STATS(RCVDUPPACK);
		PRINT_TCP_STATS(RCVDUPBYTE);
		PRINT_TCP_STATS(RCVPARTDUPPACK);
		PRINT_TCP_STATS(RCVPARTDUPBYTE);
		PRINT_TCP_STATS(RCVOOPACK);
		PRINT_TCP_STATS(RCVOOBYTE);
		PRINT_TCP_STATS(RCVPACKAFTERWIN);
		PRINT_TCP_STATS(RCVBYTEAFTERWIN);
		PRINT_TCP_STATS(RCVAFTERCLOSE);
		PRINT_TCP_STATS(RCVWINPROBE);
		PRINT_TCP_STATS(RCVDUPACK);
		PRINT_TCP_STATS(RCVACKTOOMUCH);
		PRINT_TCP_STATS(RCVACKPACK);
		PRINT_TCP_STATS(RCVACKBYTE);
		PRINT_TCP_STATS(RCVWINUPD);
		PRINT_TCP_STATS(PAWSDROP);
		PRINT_TCP_STATS(PREDACK);
		PRINT_TCP_STATS(PREDDAT);
		PRINT_TCP_STATS(PCBHASHMISS);
		PRINT_TCP_STATS(NOPORT);
		PRINT_TCP_STATS(BADSYN);
		service_log(SERVICE_LOG_INFO,"mbufs_allocated_for_tx %lu\n",mbufs_allocated_for_tx);
		service_log(SERVICE_LOG_INFO,"mbufs_allocated_for_rx %lu\n",mbufs_allocated_for_rx);
		service_log(SERVICE_LOG_INFO,"mbufs_freed_for_tx %lu\n",mbufs_freed_for_tx);
		service_log(SERVICE_LOG_INFO,"mbufs_freed_for_rx %lu\n",mbufs_freed_for_rx);
		print_user_stats();
		service_log(SERVICE_LOG_INFO,"get_mbuf_called %lu\n",get_mbuf_called);
		service_log(SERVICE_LOG_INFO,"get_mbuf_failed %lu\n",get_mbuf_failed);
		service_log(SERVICE_LOG_INFO,"mbuf_free_called %lu \n", mbuf_free_called);
		service_log(SERVICE_LOG_INFO,"md_received %lu\n",pmd_received);
		service_log(SERVICE_LOG_INFO,"pmd_transmitted %lu\n", pmd_transmitted);
		sleep(1);
	}
}

int app_glue_get_socket_type(struct socket *so)
{
	return so->so_type;
}

int app_glue_sendto(struct socket *so, void *data,int len, void *desc)
{
    struct mbuf *addr,*top;
    char *p;
    int rc;
    struct sockaddr_in *p_addr;

    addr = m_get(M_WAIT, MT_SONAME);
    if (!addr) {
	printf("cannot create socket %s %d\n",__FILE__,__LINE__);
	return NULL;
    }
    
    p = (char *)data;
    p -= sizeof(struct sockaddr_in);
    p_addr = (struct sockaddr_in *)p;
    addr->m_len = sizeof(struct sockaddr_in);
    p = mtod(addr, char *);

    memcpy(p, p_addr, sizeof(*p_addr));
    top = m_devget(data, len, 0, NULL, desc);
    if(!top) {
	m_freem(addr);
        return -1;
    }
    mbufs_allocated_for_tx++;
    rc = sosend(so, addr, top, NULL, 0);
    m_freem(addr);
    return rc;
}

int app_glue_receivefrom(struct socket *so, void **buf)
{
    struct mbuf *paddr = NULL,*mp0 = NULL,*controlp = NULL;
    int flags = 0,rc;
    char *p;

    rc = soreceive( so, &paddr,&mp0, &controlp, &flags);
    if(!rc) {
	*buf = mp0->m_paddr;
	p = mtod(mp0, char *);
	mp0->m_paddr = NULL;
	memcpy(p - sizeof(struct sockaddr_in), mtod(paddr, char *), sizeof(struct sockaddr_in));
	mbufs_freed_for_rx++;
	m_freem(mp0);
	if(paddr) {
		m_freem(paddr);
	}
    }
    return rc;
}

int app_glue_send(struct socket *so, void *data,int len, void *desc)
{
    struct mbuf *top;
    int rc;

    top = m_devget(data, len, 0, NULL, desc);
    if(!top) {
        return -1;
    } 
    rc = sosend(so, NULL, top,NULL, 0);
    mbufs_allocated_for_tx += (rc == 0);
    return rc;
}

int app_glue_receive(struct socket *so,void **buf)
{
    struct mbuf *mp0 = NULL,*controlp = NULL;
    int flags = 0,rc;

    rc = soreceive( so, NULL,&mp0, &controlp, &flags);
    if(!rc) {
	*buf = mp0->m_paddr;
	mp0->m_paddr = NULL;
	mbufs_freed_for_rx++;	
	m_free(mp0);
    }
    return rc;
}

int app_glue_setsockopt(void *so, int level, int name, size_t size, void *data)
{
	struct socket *sock = (struct socket *)so;
	struct sockopt sockoption;

	sockoption.sopt_level = level;
	sockoption.sopt_name = name;
	sockoption.sopt_size = size;
	sockoption.sopt_data = data;
	return sosetopt(so, &sockoption);
}

int app_glue_get_sock_snd_buf_space(void *so)
{
	struct socket *sock = (struct socket *)so;
	return sbspace(&sock->so_snd);
}

TAILQ_HEAD(buffers_available_notification_socket_list_head, socket) buffers_available_notification_socket_list_head;
void app_glue_process_tx_empty(void *so)
{
	struct socket *sock = (struct socket *)so;
	if(sock) {
               if(!sock->buffers_available_notification_queue_present) {
                   TAILQ_INSERT_TAIL(&buffers_available_notification_socket_list_head, sock, buffers_available_notification_queue_entry);
                   sock->buffers_available_notification_queue_present = 1;
		   if (app_glue_get_socket_type(sock) == 2)
	   	   	user_set_socket_tx_space(app_glue_get_glueing_block(sock),sbspace(&sock->so_snd));
		}
           }
}

int app_glue_is_buffers_available_waiters_empty()
{
	return TAILQ_EMPTY(&buffers_available_notification_socket_list_head);
}

void *app_glue_get_first_buffers_available_waiter()
{
	struct socket *sock = TAILQ_FIRST(&buffers_available_notification_socket_list_head);
	return sock->glueing_block;
}

void app_glue_remove_first_buffer_available_waiter(void *so)
{
	struct socket *sock = (struct socket *)so;
	sock->buffers_available_notification_queue_present = 0;
        TAILQ_REMOVE(&buffers_available_notification_socket_list_head,sock,buffers_available_notification_queue_entry);
}

void app_glue_init_buffers_available_waiters()
{
	TAILQ_INIT(&buffers_available_notification_socket_list_head);
}

void notify_app_about_accepted_sock(void *so2, void *parent_descriptor, unsigned int faddr, unsigned short fport);

void user_on_accept(struct socket *so)
{
	void *parent_descriptor;

	while(so->so_qlen) {
    		struct socket *so2 = TAILQ_FIRST(&so->so_q);
		struct mbuf *addr = m_get(M_WAIT, MT_SONAME);
	
		if (soqremque(so2, 1) == 0) {
			printf("user_on_accept\n");
			exit(1);
		}
	    	soaccept(so2,addr);
//		so2->so_upcall = app_glue_so_upcall;
		so2->so_upcall2 = app_glue_so_upcall2;
		parent_descriptor = app_glue_get_glueing_block(so);
		notify_app_about_accepted_sock(so2, parent_descriptor, 
					((struct inpcb *)so2->so_pcb)->inp_faddr.s_addr, 
					((struct inpcb *)so2->so_pcb)->inp_fport);
		m_freem(addr);
    }
}

#define COHERENCY_UNIT 64
unsigned long hz=1000;
volatile unsigned long tick = 0;

size_t  coherency_unit = COHERENCY_UNIT;
void *createInterface(int instance);
void *create_udp_socket(const char *ip_addr,unsigned short port);
void *create_client_socket(const char *my_ip_addr,unsigned short my_port,
		                   const char *peer_ip_addr,unsigned short port);
void *create_server_socket(const char *my_ip_addr,unsigned short port);
int init_device(int portid, int queue_count);
void poll_rx(void *ifp, int portid, int queue_id);
int main(int argc,char **argv)
{
    int ret;
    void *ifp;

    ret = rte_eal_init(argc, argv);
    if (ret < 0) {
        //rte_exit(EXIT_FAILURE, "Invalid EAL arguments\n");
        printf("cannot initialize EAL\n");
        exit(0);
    } 
    create_app_mbufs_mempool();
    init_device(0, 1);
    softint_init();
    callout_startup(); 
    domaininit(1);
    bpf_setops();
    rt_init();
    soinit();
    mbinit();
    app_glue_init();
    rte_timer_subsystem_init();
    init_systick();
    ifp = createInterface(0);
    app_glue_set_port_ifp(0, ifp);
    configure_if_addr(ifp, inet_addr("192.168.150.63"), inet_addr("255.255.255.0"));
#if 0
    createLoopbackInterface();
#endif
    print_stats_thread_cpuid = get_current_cpu();
    service_set_log_level(0);
    launch_threads();
    while(1) { 
	    service_main_loop();
    }
    printf("The END\n");
    return 0;
}

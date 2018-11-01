
#ifndef __UIP_APP_H__
#define __UIP_APP_H__

typedef char uip_tcp_appstate_t ;
typedef char uip_udp_appstate_t;

#ifndef     UIP_APPCALL 
	#define UIP_APPCALL  uIP_TcpAppCall
#endif


#ifndef     UIP_UDP_APPCALL 
	#define UIP_UDP_APPCALL  uIP_UdpAppCall
#endif

void uIP_UdpAppCall(void) ;

void uIP_TcpAppCall(void) ;

void uIP_AppInit(void);

void uIP_AppPoll(void);

#endif





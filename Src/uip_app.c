
#include "uip.h"
#include "uip_app.h"
#include "ustdio.h"

#include <string.h>
#include "avltree.h"
#include "shell.h"
#include "gpio.h"

#include "telnet_server.h"
#include "ftp_server.h"




void uip_tcp_app_call(void)
{
	switch(HTONS(uip_conn->lport))
	{
		case (TELNET_PORT):
			telnet_port_call();
			break;
		
		case (FTP_CTRL_PORT):
			ftp_ctrl_port_call();
			break;
		
		case (FTP_DATA_PORT):
			ftp_data_port_call();
			break;
		
		default:;
	}
}



void uip_app_poll(void)
{
	ftp_server_process();
	telnet_server_pro();
}




void uip_app_init(void)
{
	ftp_server_init();
	telnet_server_init();
}





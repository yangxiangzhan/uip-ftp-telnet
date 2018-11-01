
#include "uip.h"
#include "uip_app.h"
#include "ustdio.h"

#include <string.h>
#include "avltree.h"
#include "shell.h"
#include "gpio.h"

#include "telnet_server.h"
#include "ftp_server.h"




void uIP_TcpAppCall(void)
{
	switch(HTONS(uip_conn->lport))
	{
		case (TELNET_PORT):
			uIP_TelnetAppCall();
			break;
		
		case (FTP_CTRL_PORT):
			uIP_FtpCtrlPortCall();
			break;
		
		case (FTP_DATA_PORT):
			uIP_FtpDataPortCall();
			break;
		
		default:;
	}
}



void uIP_AppPoll(void)
{
	uIP_FtpServerPro();
	uip_TelnetServerPro();
}




void uIP_AppInit(void)
{
	uIP_FtpServerInit();
	uIP_TelnetAppInit();
}





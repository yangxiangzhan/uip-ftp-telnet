#ifndef  _FTP_SERVER_
#define _FTP_SERVER_


#define FTP_CTRL_PORT 21U      //ftp 控制端口
#define FTP_DATA_PORT 45678U //ftp 数据端口


void uIP_FtpServerInit(void);

void uIP_FtpCtrlPortCall(void);

void uIP_FtpDataPortCall(void);

void uIP_FtpServerPro(void);

#endif



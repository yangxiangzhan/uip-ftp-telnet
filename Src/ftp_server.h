#ifndef  _FTP_SERVER_
#define _FTP_SERVER_


#define FTP_CTRL_PORT 21U      //ftp ���ƶ˿�
#define FTP_DATA_PORT 45678U //ftp ���ݶ˿�


void uIP_FtpServerInit(void);

void uIP_FtpCtrlPortCall(void);

void uIP_FtpDataPortCall(void);

void uIP_FtpServerPro(void);

#endif



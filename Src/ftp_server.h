#ifndef  _FTP_SERVER_
#define _FTP_SERVER_


#define FTP_CTRL_PORT 21U      //ftp 控制端口
#define FTP_DATA_PORT 45678U //ftp 数据端口


void ftp_server_init(void);

void ftp_ctrl_port_call(void);

void ftp_data_port_call(void);

void ftp_server_process(void);

#endif



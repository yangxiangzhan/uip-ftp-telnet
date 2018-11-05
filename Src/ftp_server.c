#include <string.h>

/* uip相关 */
#include "uip.h"
#include "uip_arp.h"
#include "tapdev.h" 

/*文件系统采用 fatfs */
#include "fatfs.h" 

#include "ustdio.h"
#include "avltree.h"
#include "shell.h"
#include "gpio.h"
#include "ftp_server.h"

typedef uint32_t (*replyfunc_t)(char * arg,uint16_t arglen);
typedef struct ftp_cmd
{
	uint32_t	  index;	 //命令标识码
	replyfunc_t	  func;      //记录命令函数指针
	struct avl_node cmd_node;//avl树节点
}
ftpcmd_t;

typedef struct ftp_state
{
	struct uip_conn * ctrl_conn ;
	struct uip_conn * data_conn ;
	char polling;
	char needpoll;
}
ftpstate_t;


enum FTP_DATA_CALL
{
	FTP_DATA_NOOP = 0,
	FTP_DATA_LIST ,   // (1) 数据端口发送文件列表
	FTP_DATA_RETR  ,  // (2) 数据端口发送文件
	FTP_DATA_STOR ,   // (3) 数据端口接收文件
};


/* Private macro ------------------------------------------------------------*/

//字符串转整形，仅适用于 FTP 命令，因为一条 ftp 命令只有 3-4 个字符，刚好可以转为整型，兼容小写
#define FTP_STR2ID(str) ((*(int*)(str)) & 0xDFDFDFDF) 

// ftp 命令树构建
#define FTP_REGISTER_COMMAND(CMD) \
	do{\
		static struct ftp_cmd cmd ;      \
		cmd.index = FTP_STR2ID(#CMD);    \
		cmd.func  = ctrl_port_reply_##CMD;\
		ftp_insert_cmd(&cmd);            \
	}while(0)


// ftp 文件列表格式
#define NORMAL_LIST(listbuf,filesize,month,day,year,filename)\
	sprintf((listbuf),normal_format,(filesize),month_list[(month)],(day),(year),(filename))

#define THIS_YEAR_LIST(listbuf,filesize,month,day,hour,min,filename)\
	sprintf((listbuf),this_year_format,(filesize),month_list[(month)],(day),(hour),(min),(filename))


// ftp 格式一般为 xxxx /dir1/dir2/\r\n ,跳过空格并去掉末尾的 /\r\n 提取可用路径 	
#define LEGAL_PATH(path,pathend) 	\
	do{\
		while(*path == ' ')  ++path;         \
		if (*pathend == '\n') *pathend-- = 0;\
		if (*pathend == '\r') *pathend-- = 0;\
		if (*pathend == '/' ) *pathend = 0;\
	}while(0)


/*---------------------------------------------------------------------------*/
/* Private variables ------------------------------------------------------------*/
//static const char ftp_msg_451[] = "451 errors";
static const char ftp_msg_421[] = "421 Data port busy!\r\n";
static const char ftp_msg_226[] = "226 transfer complete\r\n";

static const char normal_format[]	 = "-rw-rw-rw-   1 user     ftp  %11ld %s %02i %5i %s\r\n";
static const char this_year_format[] = "-rw-rw-rw-   1 user     ftp  %11ld %s %02i %02i:%02i %s\r\n";
static const uint16_t list_min = sizeof(this_year_format) + 20;

static const char  * month_list[] = { //月份从 1 到 12 ，0 填充 NULL 
	NULL,
	"Jan","Feb","Mar","Apr","May","Jun",
	"Jul","Aug","Sep","Oct","Nov","Dez" };   

static struct avl_root ftp_root = {.avl_node = NULL};//命令匹配的平衡二叉树树根 

static struct ftp_state ftpstate;//ftp状态值

static char     ftp_current_dir[128] = {0};
static char     ftp_ctrl_buf[128] = {0};
static char *   ctrl_msg = ftp_ctrl_buf;
static uint16_t ctrl_msg_len = 0;

static char     data_msg[UIP_TCP_MSS];
static uint16_t data_msg_len = 0;

static FIL ftp_file;		 /* File object */


/* Gorgeous Split-line -----------------------------------------------*/

/**
	* @brief	ftp_insert_cmd 
	*			命令树插入
	* @param	ftpcmd		命令控制块
	* @return	成功返回 0
*/
static int ftp_insert_cmd(struct ftp_cmd * ftpcmd)
{
	struct avl_node **tmp = &ftp_root.avl_node;
	struct avl_node *parent = NULL;
	
	/* Figure out where to put new node */
	while (*tmp)
	{
		struct ftp_cmd *this = container_of(*tmp, struct ftp_cmd, cmd_node);

		parent = *tmp;
		if (ftpcmd->index < this->index)
			tmp = &((*tmp)->avl_left);
		else 
		if (ftpcmd->index > this->index)
			tmp = &((*tmp)->avl_right);
		else
			return 1;
	}

	/* Add new node and rebalance tree. */
	//rb_link_node(&ftpcmd->cmd_node, parent, tmp);
	//rb_insert_color(&ftpcmd->cmd_node, root);
	avl_insert(&ftp_root,&ftpcmd->cmd_node,parent,tmp);
	
	return 0;
}


/**
	* @brief	ftp_search_cmd 
	*			命令树查找，根据 index 号找到对应的控制块
	* @param	index		 命令号
	* @return	成功 index 号对应的控制块
*/
static struct ftp_cmd *ftp_search_cmd(int cmdindex)
{
	struct avl_node *node = ftp_root.avl_node;

	while (node) 
	{
		struct ftp_cmd *ftpcmd = container_of(node, struct ftp_cmd, cmd_node);

		if (cmdindex < ftpcmd->index)
			node = node->avl_left;
		else 
		if (cmdindex > ftpcmd->index)
			node = node->avl_right;
		else 
			return ftpcmd;
	}
	
	return NULL;
}




/**
	* @brief    ctrl_port_reply_USER 
	*           ftp 命令端口输入 USER ，系统登陆的用户名
	* @param    arg 命令所跟参数
	* @return   0 ，不需要调用 data port
*/
static uint32_t ctrl_port_reply_USER(char * arg,uint16_t arglen)
{
	static const char reply_msg[] = "230 Operation successful\r\n";  //230 登陆因特网
	ctrl_msg = (char*)reply_msg;
	ctrl_msg_len = sizeof(reply_msg)-1;
	return 0;
}


/**
	* @brief    ctrl_port_reply_SYST 
	*           ftp 命令端口输入 SYST ，返回服务器使用的操作系统
	* @param    arg 命令所跟参数
	* @return   0 ，不需要调用 data port
*/
static uint32_t ctrl_port_reply_SYST(char * arg,uint16_t arglen)
{
	static const char reply_msg[] = "215 UNIX Type: L8\r\n";  //215 系统类型回复
	ctrl_msg = (char*)reply_msg;
	ctrl_msg_len = sizeof(reply_msg)-1;
	return 0;
}



/**
	* @brief    ctrl_port_reply_NOOP 
	*           ftp 命令端口输入 NOOP
	* @param    arg 命令所跟参数
	* @return   0 ，不需要调用 data port
*/
static uint32_t ctrl_port_reply_NOOP(char * arg,uint16_t arglen) //显示当前工作目录
{
	static const char reply_msg[] = "200 Operation successful\r\n";
	ctrl_msg = (char*)reply_msg;
	ctrl_msg_len = sizeof(reply_msg)-1;
	return 0;
}



/**
	* @brief    ctrl_port_reply_PWD 
	*           ftp 命令端口输入 PWD
	* @param    arg 命令所跟参数
	* @return   0 ，不需要调用 data port
*/
static  uint32_t ctrl_port_reply_PWD(char * arg,uint16_t arglen) //显示当前工作目录
{
	#if 0
	sprintf(ftp_ctrl_buf,"257 \"%s/\"\r\n",ftp_current_dir);
	ctrl_msg = (char*)ftp_ctrl_buf;
	ctrl_msg_len = strlen(ftp_ctrl_buf);
	return 0;
	#else
	static const char reply_msg[] = "257 \"/\"\r\n";
	ctrl_msg = (char*)reply_msg;
	ctrl_msg_len = sizeof(reply_msg)-1;
	return 0;
	#endif
}


/**
	* @brief    ctrl_port_reply_CWD 
	*           ftp 命令端口输入 CWD
	* @param    arg 命令所跟参数
	* @return   0 ，不需要调用 data port
*/
static uint32_t ctrl_port_reply_CWD(char * arg,uint16_t arglen) //显示当前工作目录
{
	static const char reply_msg[] = "250 Operation successful\r\n"; //257 路径名建立
	DIR fsdir;
	char * file_path = arg;
	char * path_end = arg + arglen - 1;
	
	LEGAL_PATH(file_path,path_end);
	if (FR_OK != f_opendir(&fsdir,file_path))
		goto CWDdone;
	else
		f_closedir(&fsdir);
	
	if (path_end != file_path)
		memcpy(ftp_current_dir,file_path,path_end - file_path); //保存当前路径
		
	ftp_current_dir[path_end - file_path] = 0;
CWDdone:
	ctrl_msg = (char*)reply_msg;
	ctrl_msg_len = sizeof(reply_msg)-1;
	return 0;
}



/**
	* @brief    ctrl_port_reply_PASV 
	*           ftp 命令端口输入 PASV ，被动模式
	*           
	* @param    arg 命令所跟参数
	* @return   0 ，不需要调用 data port
*/
static uint32_t ctrl_port_reply_PASV(char * arg,uint16_t arglen) //显示当前工作目录
{
	static char reply_msg[64] = {0} ; //"227 PASV ok(192,168,40,104,185,198)\r\n"

	ctrl_msg = (char*)reply_msg;
	ctrl_msg_len = strlen(reply_msg);
	if (0 == ctrl_msg_len) // 未初始化信息
	{
		sprintf(reply_msg,
				"227 PASV ok(%d,%d,%d,%d,%d,%d)\r\n",
				uip_hostaddr[0]&0x00ff,uip_hostaddr[0]>>8,
				uip_hostaddr[1]&0x00ff,uip_hostaddr[1]>>8,
				FTP_DATA_PORT>>8,FTP_DATA_PORT&0x00ff);
		
		ctrl_msg_len = strlen(reply_msg);
	}
	return 0;
}

/**
	* @brief    ctrl_port_reply_SIZE
	*           ftp 命令端口输入 SIZE , 获取当前文件列表
	* @param    arg 命令所跟参数
	* @return   0 ，不需要调用 data port
*/
static uint32_t ctrl_port_reply_SIZE(char * arg,uint16_t arglen)
{	
	uint32_t file_size;
	char * file_path = arg;
	char * path_end = arg + arglen - 1;

	LEGAL_PATH(file_path,path_end);

	if (*file_path != '/')//相对路径补全为绝对路径
	{
		sprintf(ftp_ctrl_buf,"%s/%s",ftp_current_dir,file_path);
		file_path = ftp_ctrl_buf;
	}

	if (FR_OK != f_open(&SDFile,file_path,FA_READ))
	{
		sprintf(ftp_ctrl_buf,"213 0\r\n");
		goto SIZEdone;
	}

	file_size = f_size(&SDFile);
	sprintf(ftp_ctrl_buf,"213 %d\r\n",file_size);
	f_close(&SDFile);

SIZEdone:	
	ctrl_msg = (char*)ftp_ctrl_buf;
	ctrl_msg_len = strlen(ftp_ctrl_buf);
	return 0;

}


/**
	* @brief    ctrl_port_reply_LIST 
	*           ftp 命令端口输入 LIST , 获取当前文件列表
	* @param    arg 命令所跟参数
	* @return   0 ，不需要调用 data port ，否则需要调用 data port 
*/
static uint32_t ctrl_port_reply_LIST(char * arg,uint16_t arglen) //显示当前工作目录
{
	//1.在控制端口对 LIST 命令进行回复
	//2.在数据端口发送 "total 0"，这个貌似可以没有
	//3.在数据端口发送文件列表
	//4.关闭数据端口
	if (ftpstate.polling) //如果数据端口正在输出数据，控制端口返回错误，否则会出现数据冲突
	{
		ctrl_msg = (char*)ftp_msg_421;
		ctrl_msg_len = sizeof(ftp_msg_421)-1;
		return 0;
	}
	else
	{
		static const char reply_msg[] = "150 Directory listing\r\n" ;//150 打开连接
		ctrl_msg = (char*)reply_msg;
		ctrl_msg_len = sizeof(reply_msg)-1;
		return FTP_DATA_LIST;//发送此信息至数据端口任务
	}
}



/**
	* @brief    ctrl_port_reply_RETR
	*           ftp 命令端口输入 RETR
	* @param    arg 命令所跟参数
	* @return   0 ，不需要调用 data port ，否则需要调用 data port 
*/
static uint32_t ctrl_port_reply_RETR(char * arg,uint16_t arglen)
{
	if (ftpstate.polling)//如果数据端口正在输出数据，控制端口返回错误，否则会出现数据冲突
	{
		ctrl_msg = (char*)ftp_msg_421;
		ctrl_msg_len = sizeof(ftp_msg_421)-1;
		return 0;
	}
	else
	{
		static const char reply_msg[] = "108 Operation successful\r\n" ;
		char * file_path = arg;
		char * path_end = arg + arglen - 1;
		
		LEGAL_PATH(file_path,path_end);
		if (*file_path != '/')//相对路径
			sprintf(ftp_ctrl_buf,"%s/%s",ftp_current_dir,file_path);
		else
			sprintf(ftp_ctrl_buf,"%s",file_path);
		
		ctrl_msg = (char*)reply_msg;
		ctrl_msg_len = sizeof(reply_msg)-1;
		
		return FTP_DATA_RETR;
	}
}



/**
	* @brief    ctrl_port_reply_DELE
	*           ftp 命令端口输入 RETR
	* @param    arg 命令所跟参数
	* @return   0 ，不需要调用 data port
*/
static uint32_t ctrl_port_reply_DELE(char * arg,uint16_t arglen)
{
	static const char reply_msgOK[] = "250 Operation successful\r\n" ;
	static const char reply_msgError[] = "450 Operation error\r\n" ;

	char * file_path = arg;
	char * path_end = arg + arglen - 1;
	
	LEGAL_PATH(file_path,path_end);

	if (*file_path != '/')//相对路径
	{
		sprintf(ftp_ctrl_buf,"%s/%s",ftp_current_dir,file_path);
		file_path = ftp_ctrl_buf;
	}

	if (FR_OK != f_unlink(file_path))
	{
		ctrl_msg = (char*)reply_msgError;
		ctrl_msg_len = sizeof(reply_msgError)-1;
	}
	else
	{
		ctrl_msg = (char*)reply_msgOK;
		ctrl_msg_len = sizeof(reply_msgOK)-1;
	}
	
	return 0;
}



/**
	* @brief    ctrl_port_reply_STOR
	*           ftp 命令端口输入 STOR
	* @param    arg 命令所跟参数
	* @return   0 ，不需要调用 data port ，否则需要调用 data port 
*/
static uint32_t ctrl_port_reply_STOR(char * arg,uint16_t arglen)
{
	if (ftpstate.polling)//如果数据端口正在输出数据，控制端口返回错误，否则会出现数据冲突
	{
		ctrl_msg = (char*)ftp_msg_421;
		ctrl_msg_len = sizeof(ftp_msg_421)-1;
		return 0;
	}
	else
	{
		static const char reply_msg[] = "125 Waiting\r\n" ;
		char * file_path = arg;
		char * path_end = arg + arglen - 1;	
		
		LEGAL_PATH(file_path,path_end);
		
		if (*file_path != '/')//相对路径
			sprintf(ftp_ctrl_buf,"%s/%s",ftp_current_dir,file_path);
		else
			strcpy(ftp_ctrl_buf,file_path);
		
		ctrl_msg = (char*)reply_msg;
		ctrl_msg_len = sizeof(reply_msg)-1;
		return FTP_DATA_STOR;
	}
}


void ftp_ctrl_port_call(void)
{
	static const char unknown_msg[] = "500 Unknown command\r\n";
	static const char connect_msg[] = "220 Operation successful\r\n";
	
	static char needpoll = 0; //需不需要调用数据端口
	static uint16_t rexmit = 0;//如需重发的长度
	
	if(uip_connected())
	{
		ctrl_msg = (char*)connect_msg;
		ctrl_msg_len = sizeof(connect_msg)-1;
		goto FtpCtrlSend; //回复连接成功
	}

	if (uip_flags & (UIP_CLOSE|UIP_ABORT|UIP_TIMEDOUT)) //==if(uip_closed()||uip_aborted()||uip_timedout()) //
	{
		ftp_current_dir[0] = 0;//复位当前目录
		color_printk(green,"\r\n|!ftp disconnect!|\r\n");
	}

	// 数据端口传输结束，会 poll 控制端口
	if (uip_poll() && ftpstate.needpoll && (!ftpstate.data_conn) && (uip_conn == ftpstate.ctrl_conn))
	{
		ftpstate.needpoll = 0;
		ftpstate.ctrl_conn = NULL;//清空连接
		ctrl_msg = (char *)ftp_msg_226;
		ctrl_msg_len = sizeof(ftp_msg_226) - 1;
		goto FtpCtrlSend;//回复传输结束
	}
	
	/*//需要调用数据端口，收到应答以后再调用*/
	if ( uip_acked() && needpoll && (uip_conn == ftpstate.ctrl_conn))
	{
		ftpstate.needpoll = needpoll;//标记信号量，在 ftp_server_process() 调动数据端口
		needpoll = 0;
	}

	if(uip_newdata()) 
	{
 		struct ftp_cmd * ctrlcmd ;
	 	uint32_t cmdindex = FTP_STR2ID(uip_appdata);// 把命令字符串转为命令码
		char *   arg = (char *)uip_appdata + 4;        //命令字符串所跟参数
		uint16_t arglen = uip_datalen() - 4;
		
		if ( *(arg-1) < 'A' || *(arg-1) > 'z' )//有些命令只有三个字节，需要判断
			cmdindex &= 0x00ffffff;

		ctrlcmd = ftp_search_cmd(cmdindex);//匹配命令号
		if (ctrlcmd)
		{
			needpoll = ctrlcmd->func(arg,arglen);
			if ( needpoll )          //需要调用数据端口 ，先在控制端口进行信息回复，收到应答后再调用
				ftpstate.ctrl_conn = uip_conn; //记录绑定控制端口，数据端口结束传输后需要回到此端口连接
		}
		else
		{
			ctrl_msg = (char*)unknown_msg;
			ctrl_msg_len = sizeof(unknown_msg)-1;
		}
		goto FtpCtrlSend;
	}

	if(uip_rexmit())
	{
		Warnings("ftp ctrl rexmit\r\n");
		uip_send(ctrl_msg,rexmit);
	}
	
	if (ctrl_msg_len)
	{
FtpCtrlSend:
		uip_send(ctrl_msg,ctrl_msg_len);
		rexmit = ctrl_msg_len;
		ctrl_msg_len = 0;
	}
}



static uint32_t data_port_reply_LIST (void)
{
	static char continue_scan = 0;
	static DIR ftp_dir;
	
	if (0 == continue_scan) 
	{
		continue_scan = 1;
		if (FR_OK != f_opendir(&ftp_dir,ftp_current_dir))
			goto ScanDirDone ;
	}
	
	while(1)
	{
		struct FileDate * file_date ;
		struct FileTime * file_time ;
		char * buf = &data_msg[data_msg_len];
		FILINFO fno;
	    FRESULT res = f_readdir(&ftp_dir, &fno); /* Read a directory item */
		
		if (res != FR_OK || fno.fname[0] == 0) /* Break on error or end of dir */
			break; 

		if ( (fno.fattrib & AM_DIR) && (fno.fattrib != AM_DIR))//不显示只读/系统/隐藏文件夹
			continue;

		file_date = (struct FileDate *)(&fno.fdate);
		file_time = (struct FileTime *)(&fno.ftime);
		
		if (fno.fdate == 0 || fno.ftime == 0) //没有日期的文件
			NORMAL_LIST(buf,fno.fsize,1,1,1980,fno.fname);
		else
		if (file_date->Year + 1980 == 2018) //同一年的文件
			THIS_YEAR_LIST(buf,fno.fsize,file_date->Month,file_date->Day,file_time->Hour,file_time->Min,fno.fname);
		else
			NORMAL_LIST(buf,fno.fsize,file_date->Month,file_date->Day,file_date->Year+1980,fno.fname);
		
		if (fno.fattrib & AM_DIR )   /* It is a directory */
			buf[0] = 'd';

		data_msg_len += strlen(buf);
		if (uip_mss() - data_msg_len < list_min)//如果剩余空间不足以再扫描
			return FTP_DATA_LIST;
	}
	
	f_closedir(&ftp_dir); //把路径关闭

ScanDirDone:
	continue_scan = 0;
	return 0;
}



static uint32_t data_port_reply_RETR(void)
{
	static uint32_t file_size = 0;
	char * file_path = ftp_ctrl_buf;//路径在 ctrl_port_reply_RETR() 存于 ftp_ctrl_buf 中，
	uint32_t read_size;
	FRESULT res ;

	if (0 == file_size) //上一次还没发送完，不需要重新打开文件
	{
		res = f_open(&ftp_file,file_path,FA_READ);
		if (FR_OK != res)
		{
			Errors("cannot open \"%s\",code = %d",file_path,res);
			goto SendEnd;
		}

		file_size = f_size(&ftp_file);
	}
	
	while(file_size)
	{
		res = f_read(&ftp_file,data_msg,uip_mss(),&read_size);//以最大长度 mss 读取发送
		if ((FR_OK != res) || (0 == read_size)) //读取出错
		{
			file_size = 0; //跳出循环
			Errors("Cannot read \"%s\",error code :%d\r\n",file_path,res);
		}
		else
		{
			data_msg_len = read_size;
			file_size -= read_size;    //更新剩余数据大小
			if (file_size)
				return FTP_DATA_RETR; //未读取完返回，先发送此包数据
		}
	}
	
	f_close(&ftp_file);
SendEnd:
	return 0;
}



void ftp_data_port_call(void)
{
	static uint8_t need_ack = 0;//数据较多时一包发送不完，发送完一包后接收ack继续发送
	static uint16_t rexmit = 0;
	static FRESULT file_state = FR_NO_FILE;//接收文件状态
	
	if(uip_connected()) 
	{
		ftpstate.data_conn = uip_conn;//等待数据端口调用
		return ;
	}

	if(uip_newdata() && !file_state) //接收数据，一般为接收文件
	{
		uint32_t byteswritten;
		memcpy(data_msg , uip_appdata , uip_len);//uip_appdata 内存地址没有对齐，需要拷出来，否则会出现不明错误
		file_state = f_write(&ftp_file,(void*)data_msg, uip_len, &byteswritten);
		if ((byteswritten == 0) || (file_state != FR_OK))
		{
			f_close(&ftp_file);
			Errors("write file error\r\n");	
			goto CloseDataPort;
		}
	}

	//服务器接收完文件结束后，发送方会主动断开连接。
	if (uip_flags & (UIP_CLOSE|UIP_ABORT|UIP_TIMEDOUT)) //==if(uip_closed()||uip_aborted()||uip_timedout()) //
	{
		if (FR_OK == file_state)//如果文件被打开
		{
			printk("recieve file completely\r\n");
			file_state = FR_NO_FILE;
			f_close(&ftp_file);
		}
		goto CloseDataPort;
	}

	//服务器发送完一包信息收到应答，或者由控制端口调用
	if ( uip_acked() || ftpstate.needpoll) 
	{
		if (ftpstate.needpoll)
		{
			need_ack = ftpstate.needpoll;
			ftpstate.polling = ftpstate.needpoll;//保存数据端口状态值，记录正在运行事件
			ftpstate.needpoll = 0; //清空需要运行事件
		}
		
		switch(need_ack) 
		{
			case FTP_DATA_LIST:
				need_ack = data_port_reply_LIST();
				break;
			
			case FTP_DATA_RETR:
				need_ack = data_port_reply_RETR();
				break;
			
			case FTP_DATA_STOR:
				file_state = f_open(&ftp_file, ftp_ctrl_buf, FA_CREATE_ALWAYS | FA_WRITE);
				if (FR_OK != file_state)
					Errors("cannot open/create \"%s\",error code = %d\r\n",ftp_ctrl_buf,file_state);
				return ;
			default: ;	
		}
		if (!data_msg_len) //没有信息需要发送，说明传输结束,服务器主动关闭端口
			goto CloseDataPort;
	}

	if(uip_rexmit())//数据重发
	{
		Warnings("ftp data rexmit\r\n");
		uip_send(data_msg,rexmit);
		return;
	}
	
	if (data_msg_len) //有数据信息
	{
		uip_send(data_msg,data_msg_len);//发送此包数据
		rexmit = data_msg_len;//记录当前包长度，如果需要重发
		data_msg_len = 0; 
		return;
	}

	return ;

CloseDataPort:
	uip_close(); //关闭连接
	ftpstate.needpoll = ftpstate.polling;//传输完成后，需要调用控制端口进行信息回复
	ftpstate.data_conn = NULL;       //清空连接记录
	ftpstate.polling = 0;          //清空数据端口传输状态
}




/** 
	* @brief    ftp_server_process
	*           ftp 轮询
	* @param    void
*/
void ftp_server_process(void)
{
	if (ftpstate.needpoll) //有调动需求
	{
		if (ftpstate.data_conn)//如果是控制端口调动数据端口，data_conn 会有连接
			uip_poll_conn(ftpstate.data_conn);
		else                 //数据端口传输结束后（data_conn为空）才会调动控制端口
			uip_poll_conn(ftpstate.ctrl_conn);//数据端口处理结束，poll 控制端口
		
		if (uip_len)
		{
			uip_arp_out();
			tapdev_send();
		}
	}
}


void ftp_server_init(void)
{	
	replyfunc_t ctrl_port_reply_TYPE = ctrl_port_reply_NOOP;

	//生成相关的命令二叉树
	FTP_REGISTER_COMMAND(USER);
	FTP_REGISTER_COMMAND(SYST);
	FTP_REGISTER_COMMAND(PWD);
	FTP_REGISTER_COMMAND(CWD);
	FTP_REGISTER_COMMAND(PASV);
	FTP_REGISTER_COMMAND(LIST);
	FTP_REGISTER_COMMAND(NOOP);
	FTP_REGISTER_COMMAND(TYPE);
	FTP_REGISTER_COMMAND(SIZE);
	FTP_REGISTER_COMMAND(RETR);
	FTP_REGISTER_COMMAND(DELE);
	FTP_REGISTER_COMMAND(STOR);
	
	uip_listen(HTONS(FTP_CTRL_PORT));
	uip_listen(HTONS(FTP_DATA_PORT));

	memset(&ftpstate,0,sizeof(ftpstate_t));//清空状态值
}



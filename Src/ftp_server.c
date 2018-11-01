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

typedef uint32_t (*pfnFTPx_t)(char * arg,uint16_t arglen);
typedef struct ftp_cmd
{
	uint32_t	  Index;	 //命令标识码
	pfnFTPx_t	  Func;      //记录命令函数指针
	struct avl_node cmd_node;//avl树节点
}
ftpcmd_t;

typedef struct ftp_state
{
	struct uip_conn * CtrlConn ;
	struct uip_conn * DataConn ;
	char cPolling;
	char cNeedPoll;
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
#define vFTP_RegisterCommand(CMD) \
	do{\
		static struct ftp_cmd CmdBuf ;      \
		CmdBuf.Index = FTP_STR2ID(#CMD);    \
		CmdBuf.Func  = iFtpCtrl_Cmd_##CMD;\
		iFtp_InsertCmd(&CmdBuf);            \
	}while(0)


// ftp 文件列表格式
#define vFtp_NormalList(listbuf,filesize,month,day,year,filename)\
	sprintf((listbuf),acNormalListFormat,(filesize),acMonthList[(month)],(day),(year),(filename))

#define vFtp_ThisYearList(listbuf,filesize,month,day,hour,min,filename)\
	sprintf((listbuf),acThisYearListFormat,(filesize),acMonthList[(month)],(day),(hour),(min),(filename))


// ftp 格式一般为 xxxx /dir1/dir2/\r\n ,跳过空格并去掉末尾的 /\r\n 提取可用路径 	
#define vFtp_GetLegalPath(path,pathend) 	\
	do{\
		while(*path == ' ')  ++path;         \
		if (*pathend == '\n') *pathend-- = 0;\
		if (*pathend == '\r') *pathend-- = 0;\
		if (*pathend == '/' ) *pathend = 0;\
	}while(0)


/*---------------------------------------------------------------------------*/
/* Private variables ------------------------------------------------------------*/
//static const char acFtpCtrlMsg451[] = "451 errors";
static const char acFtpCtrlMsg421[] = "421 Data port busy!\r\n";
static const char acFtpCtrlMsg226[] = "226 transfer complete\r\n";

static const char acNormalListFormat[]	 = "-rw-rw-rw-   1 user     ftp  %11ld %s %02i %5i %s\r\n";
static const char acThisYearListFormat[] = "-rw-rw-rw-   1 user     ftp  %11ld %s %02i %02i:%02i %s\r\n";
static const uint16_t sListMinLen = sizeof(acThisYearListFormat) + 20;

static const char  * acMonthList[] = { //月份从 1 到 12 ，0 填充 NULL 
	NULL,"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dez" };   

static struct avl_root stFtpCmdTreeRoot = {.avl_node = NULL};//命令匹配的平衡二叉树树根 

static struct ftp_state stFtpS;//ftp状态值

static char acFtpCurrentDir[128] = {0};
static char acFtpCtrlBuf[128] = {0};
static char * pcFtpCtrlMsg = acFtpCtrlBuf;
static uint16_t sFtpCtrlMsgLen = 0;

static char acFtpDataMsg[UIP_TCP_MSS];
static uint16_t sFtpDataMsgLen = 0;

static FIL stFtpFileHandle;		 /* File object */


/* Gorgeous Split-line -----------------------------------------------*/

/**
	* @brief	iFtp_InsertCmd 
	*			命令树插入
	* @param	pCmd		命令控制块
	* @return	成功返回 0
*/
static int iFtp_InsertCmd(struct ftp_cmd * pCmd)
{
	struct avl_node **tmp = &stFtpCmdTreeRoot.avl_node;
	struct avl_node *parent = NULL;
	
	/* Figure out where to put new node */
	while (*tmp)
	{
		struct ftp_cmd *this = container_of(*tmp, struct ftp_cmd, cmd_node);

		parent = *tmp;
		if (pCmd->Index < this->Index)
			tmp = &((*tmp)->avl_left);
		else 
		if (pCmd->Index > this->Index)
			tmp = &((*tmp)->avl_right);
		else
			return 1;
	}

	/* Add new node and rebalance tree. */
	//rb_link_node(&pCmd->cmd_node, parent, tmp);
	//rb_insert_color(&pCmd->cmd_node, root);
	avl_insert(&stFtpCmdTreeRoot,&pCmd->cmd_node,parent,tmp);
	
	return 0;
}


/**
	* @brief	pFtp_SearchCmd 
	*			命令树查找，根据 Index 号找到对应的控制块
	* @param	Index		 命令号
	* @return	成功 Index 号对应的控制块
*/
static struct ftp_cmd *pFtp_SearchCmd(int iCtrlCmd)
{
	struct avl_node *node = stFtpCmdTreeRoot.avl_node;

	while (node) 
	{
		struct ftp_cmd *pCmd = container_of(node, struct ftp_cmd, cmd_node);

		if (iCtrlCmd < pCmd->Index)
			node = node->avl_left;
		else 
		if (iCtrlCmd > pCmd->Index)
			node = node->avl_right;
		else 
			return pCmd;
	}
	
	return NULL;
}




/**
	* @brief    vFtpCtrl_Cmd_USER 
	*           ftp 命令端口输入 USER ，系统登陆的用户名
	* @param    arg 命令所跟参数
	* @return   0 ，不需要调用 data port
*/
static uint32_t iFtpCtrl_Cmd_USER(char * arg,uint16_t arglen)
{
	static const char acFtpReply[] = "230 Operation successful\r\n";  //230 登陆因特网
	pcFtpCtrlMsg = (char*)acFtpReply;
	sFtpCtrlMsgLen = sizeof(acFtpReply)-1;
	return 0;
}


/**
	* @brief    iFtpCtrl_Cmd_SYST 
	*           ftp 命令端口输入 SYST ，返回服务器使用的操作系统
	* @param    arg 命令所跟参数
	* @return   0 ，不需要调用 data port
*/
static uint32_t iFtpCtrl_Cmd_SYST(char * arg,uint16_t arglen)
{
	static const char acFtpReply[] = "215 UNIX Type: L8\r\n";  //215 系统类型回复
	pcFtpCtrlMsg = (char*)acFtpReply;
	sFtpCtrlMsgLen = sizeof(acFtpReply)-1;
	return 0;
}



/**
	* @brief    vFtpCtrl_Cmd_NOOP 
	*           ftp 命令端口输入 NOOP
	* @param    arg 命令所跟参数
	* @return   0 ，不需要调用 data port
*/
static uint32_t iFtpCtrl_Cmd_NOOP(char * arg,uint16_t arglen) //显示当前工作目录
{
	static const char acFtpReply[] = "200 Operation successful\r\n";
	pcFtpCtrlMsg = (char*)acFtpReply;
	sFtpCtrlMsgLen = sizeof(acFtpReply)-1;
	return 0;
}



/**
	* @brief    iFtpCtrl_Cmd_PWD 
	*           ftp 命令端口输入 PWD
	* @param    arg 命令所跟参数
	* @return   0 ，不需要调用 data port
*/
static  uint32_t iFtpCtrl_Cmd_PWD(char * arg,uint16_t arglen) //显示当前工作目录
{
	#if 0
	sprintf(acFtpCtrlBuf,"257 \"%s/\"\r\n",acFtpCurrentDir);
	pcFtpCtrlMsg = (char*)acFtpCtrlBuf;
	sFtpCtrlMsgLen = strlen(acFtpCtrlBuf);
	return 0;
	#else
	static const char acFtpReply[] = "257 \"/\"\r\n";
	pcFtpCtrlMsg = (char*)acFtpReply;
	sFtpCtrlMsgLen = sizeof(acFtpReply)-1;
	return 0;
	#endif
}


/**
	* @brief    iFtpCtrl_Cmd_CWD 
	*           ftp 命令端口输入 CWD
	* @param    arg 命令所跟参数
	* @return   0 ，不需要调用 data port
*/
static uint32_t iFtpCtrl_Cmd_CWD(char * arg,uint16_t arglen) //显示当前工作目录
{
	static const char acFtpReply[] = "250 Operation successful\r\n"; //257 路径名建立
	DIR fsdir;
	char * pcFilePath = arg;
	char * pcPathEnd = arg + arglen - 1;
	
	vFtp_GetLegalPath(pcFilePath,pcPathEnd);
	if (FR_OK != f_opendir(&fsdir,pcFilePath))
		goto CWDdone;
	else
		f_closedir(&fsdir);
	
	if (pcPathEnd != pcFilePath)
		memcpy(acFtpCurrentDir,pcFilePath,pcPathEnd - pcFilePath); //保存当前路径
		
	acFtpCurrentDir[pcPathEnd - pcFilePath] = 0;
CWDdone:
	pcFtpCtrlMsg = (char*)acFtpReply;
	sFtpCtrlMsgLen = sizeof(acFtpReply)-1;
	return 0;
}



/**
	* @brief    vFtpCtrl_Cmd_PASV 
	*           ftp 命令端口输入 PASV ，被动模式
	*           
	* @param    arg 命令所跟参数
	* @return   0 ，不需要调用 data port
*/
static uint32_t iFtpCtrl_Cmd_PASV(char * arg,uint16_t arglen) //显示当前工作目录
{
	static char acFtpReply[64] = {0} ; //"227 PASV ok(192,168,40,104,185,198)\r\n"

	pcFtpCtrlMsg = (char*)acFtpReply;
	sFtpCtrlMsgLen = strlen(acFtpReply);
	if (0 == sFtpCtrlMsgLen) // 未初始化信息
	{
		sprintf(acFtpReply,
				"227 PASV ok(%d,%d,%d,%d,%d,%d)\r\n",
				uip_hostaddr[0]&0x00ff,uip_hostaddr[0]>>8,
				uip_hostaddr[1]&0x00ff,uip_hostaddr[1]>>8,
				FTP_DATA_PORT>>8,FTP_DATA_PORT&0x00ff);
		
		sFtpCtrlMsgLen = strlen(acFtpReply);
	}
	return 0;
}

/**
	* @brief    vFtpCtrl_Cmd_SIZE
	*           ftp 命令端口输入 SIZE , 获取当前文件列表
	* @param    arg 命令所跟参数
	* @return   0 ，不需要调用 data port
*/
static uint32_t iFtpCtrl_Cmd_SIZE(char * arg,uint16_t arglen)
{	
	uint32_t iFileSize;
	char * pcFilePath = arg;
	char * pcPathEnd = arg + arglen - 1;

	vFtp_GetLegalPath(pcFilePath,pcPathEnd);
	vFtp_GetLegalPath(pcFilePath,pcPathEnd);

	if (*pcFilePath != '/')//相对路径补全为绝对路径
	{
		sprintf(acFtpCtrlBuf,"%s/%s",acFtpCurrentDir,pcFilePath);
		pcFilePath = acFtpCtrlBuf;
	}

	if (FR_OK != f_open(&SDFile,pcFilePath,FA_READ))
	{
		sprintf(acFtpCtrlBuf,"213 0\r\n");
		goto SIZEdone;
	}

	iFileSize = f_size(&SDFile);
	sprintf(acFtpCtrlBuf,"213 %d\r\n",iFileSize);
	f_close(&SDFile);

SIZEdone:	
	pcFtpCtrlMsg = (char*)acFtpCtrlBuf;
	sFtpCtrlMsgLen = strlen(acFtpCtrlBuf);
	return 0;

}


/**
	* @brief    vFtpCtrl_Cmd_LIST 
	*           ftp 命令端口输入 LIST , 获取当前文件列表
	* @param    arg 命令所跟参数
	* @return   0 ，不需要调用 data port ，否则需要调用 data port 
*/
static uint32_t iFtpCtrl_Cmd_LIST(char * arg,uint16_t arglen) //显示当前工作目录
{
	//1.在控制端口对 LIST 命令进行回复
	//2.在数据端口发送 "total 0"，这个貌似可以没有
	//3.在数据端口发送文件列表
	//4.关闭数据端口
	if (stFtpS.cPolling) //如果数据端口正在输出数据，控制端口返回错误，否则会出现数据冲突
	{
		pcFtpCtrlMsg = (char*)acFtpCtrlMsg421;
		sFtpCtrlMsgLen = sizeof(acFtpCtrlMsg421)-1;
		return 0;
	}
	else
	{
		static const char acFtpReply[] = "150 Directory listing\r\n" ;//150 打开连接
		pcFtpCtrlMsg = (char*)acFtpReply;
		sFtpCtrlMsgLen = sizeof(acFtpReply)-1;
		return FTP_DATA_LIST;//发送此信息至数据端口任务
	}
}



/**
	* @brief    vFtpCtrl_Cmd_RETR
	*           ftp 命令端口输入 RETR
	* @param    arg 命令所跟参数
	* @return   0 ，不需要调用 data port ，否则需要调用 data port 
*/
static uint32_t iFtpCtrl_Cmd_RETR(char * arg,uint16_t arglen)
{
	if (stFtpS.cPolling)//如果数据端口正在输出数据，控制端口返回错误，否则会出现数据冲突
	{
		pcFtpCtrlMsg = (char*)acFtpCtrlMsg421;
		sFtpCtrlMsgLen = sizeof(acFtpCtrlMsg421)-1;
		return 0;
	}
	else
	{
		static const char acFtpReply[] = "108 Operation successful\r\n" ;
		char * pcFilePath = arg;
		char * pcPathEnd = arg + arglen - 1;
		
		vFtp_GetLegalPath(pcFilePath,pcPathEnd);
		if (*pcFilePath != '/')//相对路径
			sprintf(acFtpCtrlBuf,"%s/%s",acFtpCurrentDir,pcFilePath);
		else
			sprintf(acFtpCtrlBuf,"%s",pcFilePath);
		
		pcFtpCtrlMsg = (char*)acFtpReply;
		sFtpCtrlMsgLen = sizeof(acFtpReply)-1;
		
		return FTP_DATA_RETR;
	}
}



/**
	* @brief    iFtpCtrl_Cmd_DELE
	*           ftp 命令端口输入 RETR
	* @param    arg 命令所跟参数
	* @return   0 ，不需要调用 data port
*/
static uint32_t iFtpCtrl_Cmd_DELE(char * arg,uint16_t arglen)
{
	static const char acFtpReplyOK[] = "250 Operation successful\r\n" ;
	static const char acFtpReplyError[] = "450 Operation error\r\n" ;

	char * pcFilePath = arg;
	char * pcPathEnd = arg + arglen - 1;
	
	vFtp_GetLegalPath(pcFilePath,pcPathEnd);

	if (*pcFilePath != '/')//相对路径
	{
		sprintf(acFtpCtrlBuf,"%s/%s",acFtpCurrentDir,pcFilePath);
		pcFilePath = acFtpCtrlBuf;
	}

	if (FR_OK != f_unlink(pcFilePath))
	{
		pcFtpCtrlMsg = (char*)acFtpReplyError;
		sFtpCtrlMsgLen = sizeof(acFtpReplyError)-1;
	}
	else
	{
		pcFtpCtrlMsg = (char*)acFtpReplyOK;
		sFtpCtrlMsgLen = sizeof(acFtpReplyOK)-1;
	}
	
	return 0;
}



/**
	* @brief    iFtpCtrl_Cmd_STOR
	*           ftp 命令端口输入 STOR
	* @param    arg 命令所跟参数
	* @return   0 ，不需要调用 data port ，否则需要调用 data port 
*/
static uint32_t iFtpCtrl_Cmd_STOR(char * arg,uint16_t arglen)
{
	static const char acFtpReply[] = "125 Waiting\r\n" ;
	if (stFtpS.cPolling)//如果数据端口正在输出数据，控制端口返回错误，否则会出现数据冲突
	{
		pcFtpCtrlMsg = (char*)acFtpCtrlMsg421;
		sFtpCtrlMsgLen = sizeof(acFtpCtrlMsg421)-1;
		return 0;
	}
	else
	{
		char * pcFilePath = arg;
		char * pcPathEnd = arg + arglen - 1;	
		vFtp_GetLegalPath(pcFilePath,pcPathEnd);
		if (*pcFilePath != '/')//相对路径
			sprintf(acFtpCtrlBuf,"%s/%s",acFtpCurrentDir,pcFilePath);
		else
			strcpy(acFtpCtrlBuf,pcFilePath);
		
		pcFtpCtrlMsg = (char*)acFtpReply;
		sFtpCtrlMsgLen = sizeof(acFtpReply)-1;
		return FTP_DATA_STOR;
	}
}



void uIP_FtpCtrlPortCall(void)
{
	static const char acFtpUnknownCmd[] = "500 Unknown command\r\n";
	static const char acFtpConnect[] = "220 Operation successful\r\n";
	
	static char cNeedPoll = 0; //需不需要调用数据端口
	static uint16_t sRexmitLen = 0;//如需重发的长度
	
	if(uip_connected())
	{
		pcFtpCtrlMsg = (char*)acFtpConnect;
		sFtpCtrlMsgLen = sizeof(acFtpConnect)-1;
		goto FtpCtrlSend; //回复连接成功
	}

	if (uip_flags & (UIP_CLOSE|UIP_ABORT|UIP_TIMEDOUT)) //==if(uip_closed()||uip_aborted()||uip_timedout()) //
	{
		acFtpCurrentDir[0] = 0;//复位当前目录
		color_printk(green,"\r\n|!ftp disconnect!|\r\n");
	}

	// 数据端口传输结束，会 poll 控制端口
	if (uip_poll() && stFtpS.cNeedPoll && (!stFtpS.DataConn) && (uip_conn == stFtpS.CtrlConn))
	{
		stFtpS.cNeedPoll = 0;
		stFtpS.CtrlConn = NULL;//清空连接
		pcFtpCtrlMsg = (char *)acFtpCtrlMsg226;
		sFtpCtrlMsgLen = sizeof(acFtpCtrlMsg226) - 1;
		goto FtpCtrlSend;//回复传输结束
	}
	
	/*//需要调用数据端口，收到应答以后再调用*/
	if ( uip_acked() && cNeedPoll && (uip_conn == stFtpS.CtrlConn))
	{
		stFtpS.cNeedPoll = cNeedPoll;//标记信号量，在 uIP_FtpServerPro() 调动数据端口
		cNeedPoll = 0;
	}

	if(uip_newdata()) 
	{
 		struct ftp_cmd * pCmdMatch ;
	 	uint32_t iCtrlCmd = FTP_STR2ID(uip_appdata);// 把命令字符串转为命令码
		char *   arg = (char *)uip_appdata + 4;        //命令字符串所跟参数
		uint16_t arglen = uip_datalen() - 4;
		
		if ( *(arg-1) < 'A' || *(arg-1) > 'z' )//有些命令只有三个字节，需要判断
			iCtrlCmd &= 0x00ffffff;

		pCmdMatch = pFtp_SearchCmd(iCtrlCmd);//匹配命令号
		if (pCmdMatch)
		{
			cNeedPoll = pCmdMatch->Func(arg,arglen);
			if ( cNeedPoll )          //需要调用数据端口 ，先在控制端口进行信息回复，收到应答后再调用
				stFtpS.CtrlConn = uip_conn; //记录绑定控制端口，数据端口结束传输后需要回到此端口连接
		}
		else
		{
			pcFtpCtrlMsg = (char*)acFtpUnknownCmd;
			sFtpCtrlMsgLen = sizeof(acFtpUnknownCmd)-1;
		}
		goto FtpCtrlSend;
	}

	if(uip_rexmit())
	{
		Warnings("ftp ctrl rexmit\r\n");
		uip_send(pcFtpCtrlMsg,sRexmitLen);
	}
	
	if (sFtpCtrlMsgLen)
	{
FtpCtrlSend:
		uip_send(pcFtpCtrlMsg,sFtpCtrlMsgLen);
		sRexmitLen = sFtpCtrlMsgLen;
		sFtpCtrlMsgLen = 0;
	}
}



static uint32_t iFtpData_LIST (void)
{
	static char cContinueScan = 0;
	static DIR stFtpCurrentDir;
	
	if (0 == cContinueScan) 
	{
		cContinueScan = 1;
		if (FR_OK != f_opendir(&stFtpCurrentDir,acFtpCurrentDir))
			goto ScanDirDone ;
	}
	
	while(1)
	{
		struct FileDate * pStDate ;
		struct FileTime * pStTime ;
		char * pcBuf = &acFtpDataMsg[sFtpDataMsgLen];
		FILINFO fno;
	    FRESULT res = f_readdir(&stFtpCurrentDir, &fno); /* Read a directory item */
		
		if (res != FR_OK || fno.fname[0] == 0) /* Break on error or end of dir */
			break; 

		if ( (fno.fattrib & AM_DIR) && (fno.fattrib != AM_DIR))//不显示只读/系统/隐藏文件夹
			continue;

		pStDate = (struct FileDate *)(&fno.fdate);
		pStTime = (struct FileTime *)(&fno.ftime);
		
		if (fno.fdate == 0 || fno.ftime == 0) //没有日期的文件
			vFtp_NormalList(pcBuf,fno.fsize,1,1,1980,fno.fname);
		else
		if (pStDate->Year + 1980 == 2018) //同一年的文件
			vFtp_ThisYearList(pcBuf,fno.fsize,pStDate->Month,pStDate->Day,pStTime->Hour,pStTime->Min,fno.fname);
		else
			vFtp_NormalList(pcBuf,fno.fsize,pStDate->Month,pStDate->Day,pStDate->Year+1980,fno.fname);
		
		if (fno.fattrib & AM_DIR )   /* It is a directory */
			pcBuf[0] = 'd';

		sFtpDataMsgLen += strlen(pcBuf);
		if (uip_mss() - sFtpDataMsgLen < sListMinLen)//如果剩余空间不足以再扫描
			return FTP_DATA_LIST;
	}
	
	f_closedir(&stFtpCurrentDir); //把路径关闭

ScanDirDone:
	cContinueScan = 0;
	return 0;
}



static uint32_t iFtpData_RETR(void)
{
	static uint32_t iFileSize = 0;
	FRESULT res ;
	uint32_t iReadSize;
	char * pcFilePath = acFtpCtrlBuf;//路径在 iFtpCtrl_Cmd_RETR() 存于 acFtpCtrlBuf 中，

	if (0 == iFileSize) //上一次还没发送完，不需要重新打开文件
	{
		res = f_open(&stFtpFileHandle,pcFilePath,FA_READ);
		if (FR_OK != res)
		{
			Errors("cannot open \"%s\",code = %d",pcFilePath,res);
			goto SendEnd;
		}

		iFileSize = f_size(&stFtpFileHandle);
	}
	
	while(iFileSize)
	{
		res = f_read(&stFtpFileHandle,acFtpDataMsg,uip_mss(),&iReadSize);//以最大长度 mss 读取发送
		if ((FR_OK != res) || (0 == iReadSize)) //读取出错
		{
			iFileSize = 0; //跳出循环
			Errors("Cannot read \"%s\",error code :%d\r\n",pcFilePath,res);
		}
		else
		{
			sFtpDataMsgLen = iReadSize;
			iFileSize -= iReadSize;    //更新剩余数据大小
			if (iFileSize)
				return FTP_DATA_RETR; //未读取完返回，先发送此包数据
		}
	}
	
	f_close(&stFtpFileHandle);
SendEnd:
	return 0;
}



void uIP_FtpDataPortCall(void)
{
	static uint8_t cNeedAck = 0;//数据较多时一包发送不完，发送完一包后接收ack继续发送
	static uint16_t sRexmitLen = 0;
	static FRESULT enFileState = FR_NO_FILE;//接收文件状态
	
	if(uip_connected()) 
	{
		stFtpS.DataConn = uip_conn;//等待数据端口调用
		return ;
	}

	if(uip_newdata() && !enFileState) //接收数据，一般为接收文件
	{
		uint32_t byteswritten;
		memcpy(acFtpDataMsg , uip_appdata , uip_len);//需要拷出来，否则会出现不明错误
		enFileState = f_write(&stFtpFileHandle,(void*)acFtpDataMsg, uip_len, &byteswritten);
		if ((byteswritten == 0) || (enFileState != FR_OK))
		{
			f_close(&stFtpFileHandle);
			Errors("write file error\r\n");	
			goto CloseDataPort;
		}
	}

	//服务器接收完文件结束后，发送方会主动断开连接。
	if (uip_flags & (UIP_CLOSE|UIP_ABORT|UIP_TIMEDOUT)) //==if(uip_closed()||uip_aborted()||uip_timedout()) //
	{
		if (FR_OK == enFileState)//如果文件被打开
		{
			printk("recieve file completely\r\n");
			enFileState = FR_NO_FILE;
			f_close(&stFtpFileHandle);
		}
		goto CloseDataPort;
	}

	//服务器发送完一包信息收到应答，或者由控制端口调用
	if ( uip_acked() || stFtpS.cNeedPoll) 
	{
		if (stFtpS.cNeedPoll)
		{
			cNeedAck = stFtpS.cNeedPoll;
			stFtpS.cPolling = stFtpS.cNeedPoll;//保存数据端口状态值，记录正在运行事件
			stFtpS.cNeedPoll = 0; //清空需要运行事件
		}
		
		switch(cNeedAck) 
		{
			case FTP_DATA_LIST:
				cNeedAck = iFtpData_LIST();
				break;
			
			case FTP_DATA_RETR:
				cNeedAck = iFtpData_RETR();
				break;
			
			case FTP_DATA_STOR:
				enFileState = f_open(&stFtpFileHandle, acFtpCtrlBuf, FA_CREATE_ALWAYS | FA_WRITE);
				if (FR_OK != enFileState)
					Errors("cannot open/create \"%s\",error code = %d\r\n",acFtpCtrlBuf,enFileState);
				return ;
			default: ;	
		}
		if (!sFtpDataMsgLen) //没有信息需要发送，说明传输结束,服务器主动关闭端口
			goto CloseDataPort;
	}

	if(uip_rexmit())//数据重发
	{
		Warnings("ftp data rexmit\r\n");
		uip_send(acFtpDataMsg,sRexmitLen);
		return;
	}
	
	if (sFtpDataMsgLen) //有数据信息
	{
		uip_send(acFtpDataMsg,sFtpDataMsgLen);//发送此包数据
		sRexmitLen = sFtpDataMsgLen;//记录当前包长度，如果需要重发
		sFtpDataMsgLen = 0; 
		return;
	}

	return ;

CloseDataPort:
	uip_close(); //关闭连接
	stFtpS.cNeedPoll = stFtpS.cPolling;//传输完成后，需要调用控制端口进行信息回复
	stFtpS.DataConn = NULL;       //清空连接记录
	stFtpS.cPolling = 0;          //清空数据端口传输状态
}




/**
	* @brief    uIP_FtpServerPro
	*           ftp 轮询
	* @param    void
*/
void uIP_FtpServerPro(void)
{
	if (stFtpS.cNeedPoll) //有调动需求
	{
		if (stFtpS.DataConn)//如果是控制端口调动数据端口，DataConn 会有连接
			uip_poll_conn(stFtpS.DataConn);
		else                 //数据端口传输结束后（DataConn为空）才会调动控制端口
			uip_poll_conn(stFtpS.CtrlConn);//数据端口处理结束，poll 控制端口
		
		if (uip_len)
		{
			uip_arp_out();
			tapdev_send();
		}
	}
}


void uIP_FtpServerInit(void)
{	
	pfnFTPx_t iFtpCtrl_Cmd_TYPE = iFtpCtrl_Cmd_NOOP;

	//生成相关的命令二叉树
	vFTP_RegisterCommand(USER);
	vFTP_RegisterCommand(SYST);
	vFTP_RegisterCommand(PWD);
	vFTP_RegisterCommand(CWD);
	vFTP_RegisterCommand(PASV);
	vFTP_RegisterCommand(LIST);
	vFTP_RegisterCommand(NOOP);
	vFTP_RegisterCommand(TYPE);
	vFTP_RegisterCommand(SIZE);
	vFTP_RegisterCommand(RETR);
	vFTP_RegisterCommand(DELE);
	vFTP_RegisterCommand(STOR);
	
	uip_listen(HTONS(FTP_CTRL_PORT));
	uip_listen(HTONS(FTP_DATA_PORT));

	memset(&stFtpS,0,sizeof(ftpstate_t));//清空状态值
}



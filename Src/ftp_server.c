#include <string.h>

/* uip��� */
#include "uip.h"
#include "uip_arp.h"
#include "tapdev.h" 

/*�ļ�ϵͳ���� fatfs */
#include "fatfs.h" 

#include "ustdio.h"
#include "avltree.h"
#include "shell.h"
#include "gpio.h"
#include "ftp_server.h"

typedef uint32_t (*pfnFTPx_t)(char * arg,uint16_t arglen);
typedef struct ftp_cmd
{
	uint32_t	  Index;	 //�����ʶ��
	pfnFTPx_t	  Func;      //��¼�����ָ��
	struct avl_node cmd_node;//avl���ڵ�
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
	FTP_DATA_LIST ,   // (1) ���ݶ˿ڷ����ļ��б�
	FTP_DATA_RETR  ,  // (2) ���ݶ˿ڷ����ļ�
	FTP_DATA_STOR ,   // (3) ���ݶ˿ڽ����ļ�
};


/* Private macro ------------------------------------------------------------*/

//�ַ���ת���Σ��������� FTP �����Ϊһ�� ftp ����ֻ�� 3-4 ���ַ����պÿ���תΪ���ͣ�����Сд
#define FTP_STR2ID(str) ((*(int*)(str)) & 0xDFDFDFDF) 

// ftp ����������
#define vFTP_RegisterCommand(CMD) \
	do{\
		static struct ftp_cmd CmdBuf ;      \
		CmdBuf.Index = FTP_STR2ID(#CMD);    \
		CmdBuf.Func  = iFtpCtrl_Cmd_##CMD;\
		iFtp_InsertCmd(&CmdBuf);            \
	}while(0)


// ftp �ļ��б��ʽ
#define vFtp_NormalList(listbuf,filesize,month,day,year,filename)\
	sprintf((listbuf),acNormalListFormat,(filesize),acMonthList[(month)],(day),(year),(filename))

#define vFtp_ThisYearList(listbuf,filesize,month,day,hour,min,filename)\
	sprintf((listbuf),acThisYearListFormat,(filesize),acMonthList[(month)],(day),(hour),(min),(filename))


// ftp ��ʽһ��Ϊ xxxx /dir1/dir2/\r\n ,�����ո�ȥ��ĩβ�� /\r\n ��ȡ����·�� 	
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

static const char  * acMonthList[] = { //�·ݴ� 1 �� 12 ��0 ��� NULL 
	NULL,"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dez" };   

static struct avl_root stFtpCmdTreeRoot = {.avl_node = NULL};//����ƥ���ƽ����������� 

static struct ftp_state stFtpS;//ftp״ֵ̬

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
	*			����������
	* @param	pCmd		������ƿ�
	* @return	�ɹ����� 0
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
	*			���������ң����� Index ���ҵ���Ӧ�Ŀ��ƿ�
	* @param	Index		 �����
	* @return	�ɹ� Index �Ŷ�Ӧ�Ŀ��ƿ�
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
	*           ftp ����˿����� USER ��ϵͳ��½���û���
	* @param    arg ������������
	* @return   0 ������Ҫ���� data port
*/
static uint32_t iFtpCtrl_Cmd_USER(char * arg,uint16_t arglen)
{
	static const char acFtpReply[] = "230 Operation successful\r\n";  //230 ��½������
	pcFtpCtrlMsg = (char*)acFtpReply;
	sFtpCtrlMsgLen = sizeof(acFtpReply)-1;
	return 0;
}


/**
	* @brief    iFtpCtrl_Cmd_SYST 
	*           ftp ����˿����� SYST �����ط�����ʹ�õĲ���ϵͳ
	* @param    arg ������������
	* @return   0 ������Ҫ���� data port
*/
static uint32_t iFtpCtrl_Cmd_SYST(char * arg,uint16_t arglen)
{
	static const char acFtpReply[] = "215 UNIX Type: L8\r\n";  //215 ϵͳ���ͻظ�
	pcFtpCtrlMsg = (char*)acFtpReply;
	sFtpCtrlMsgLen = sizeof(acFtpReply)-1;
	return 0;
}



/**
	* @brief    vFtpCtrl_Cmd_NOOP 
	*           ftp ����˿����� NOOP
	* @param    arg ������������
	* @return   0 ������Ҫ���� data port
*/
static uint32_t iFtpCtrl_Cmd_NOOP(char * arg,uint16_t arglen) //��ʾ��ǰ����Ŀ¼
{
	static const char acFtpReply[] = "200 Operation successful\r\n";
	pcFtpCtrlMsg = (char*)acFtpReply;
	sFtpCtrlMsgLen = sizeof(acFtpReply)-1;
	return 0;
}



/**
	* @brief    iFtpCtrl_Cmd_PWD 
	*           ftp ����˿����� PWD
	* @param    arg ������������
	* @return   0 ������Ҫ���� data port
*/
static  uint32_t iFtpCtrl_Cmd_PWD(char * arg,uint16_t arglen) //��ʾ��ǰ����Ŀ¼
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
	*           ftp ����˿����� CWD
	* @param    arg ������������
	* @return   0 ������Ҫ���� data port
*/
static uint32_t iFtpCtrl_Cmd_CWD(char * arg,uint16_t arglen) //��ʾ��ǰ����Ŀ¼
{
	static const char acFtpReply[] = "250 Operation successful\r\n"; //257 ·��������
	DIR fsdir;
	char * pcFilePath = arg;
	char * pcPathEnd = arg + arglen - 1;
	
	vFtp_GetLegalPath(pcFilePath,pcPathEnd);
	if (FR_OK != f_opendir(&fsdir,pcFilePath))
		goto CWDdone;
	else
		f_closedir(&fsdir);
	
	if (pcPathEnd != pcFilePath)
		memcpy(acFtpCurrentDir,pcFilePath,pcPathEnd - pcFilePath); //���浱ǰ·��
		
	acFtpCurrentDir[pcPathEnd - pcFilePath] = 0;
CWDdone:
	pcFtpCtrlMsg = (char*)acFtpReply;
	sFtpCtrlMsgLen = sizeof(acFtpReply)-1;
	return 0;
}



/**
	* @brief    vFtpCtrl_Cmd_PASV 
	*           ftp ����˿����� PASV ������ģʽ
	*           
	* @param    arg ������������
	* @return   0 ������Ҫ���� data port
*/
static uint32_t iFtpCtrl_Cmd_PASV(char * arg,uint16_t arglen) //��ʾ��ǰ����Ŀ¼
{
	static char acFtpReply[64] = {0} ; //"227 PASV ok(192,168,40,104,185,198)\r\n"

	pcFtpCtrlMsg = (char*)acFtpReply;
	sFtpCtrlMsgLen = strlen(acFtpReply);
	if (0 == sFtpCtrlMsgLen) // δ��ʼ����Ϣ
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
	*           ftp ����˿����� SIZE , ��ȡ��ǰ�ļ��б�
	* @param    arg ������������
	* @return   0 ������Ҫ���� data port
*/
static uint32_t iFtpCtrl_Cmd_SIZE(char * arg,uint16_t arglen)
{	
	uint32_t iFileSize;
	char * pcFilePath = arg;
	char * pcPathEnd = arg + arglen - 1;

	vFtp_GetLegalPath(pcFilePath,pcPathEnd);
	vFtp_GetLegalPath(pcFilePath,pcPathEnd);

	if (*pcFilePath != '/')//���·����ȫΪ����·��
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
	*           ftp ����˿����� LIST , ��ȡ��ǰ�ļ��б�
	* @param    arg ������������
	* @return   0 ������Ҫ���� data port ��������Ҫ���� data port 
*/
static uint32_t iFtpCtrl_Cmd_LIST(char * arg,uint16_t arglen) //��ʾ��ǰ����Ŀ¼
{
	//1.�ڿ��ƶ˿ڶ� LIST ������лظ�
	//2.�����ݶ˿ڷ��� "total 0"�����ò�ƿ���û��
	//3.�����ݶ˿ڷ����ļ��б�
	//4.�ر����ݶ˿�
	if (stFtpS.cPolling) //������ݶ˿�����������ݣ����ƶ˿ڷ��ش��󣬷����������ݳ�ͻ
	{
		pcFtpCtrlMsg = (char*)acFtpCtrlMsg421;
		sFtpCtrlMsgLen = sizeof(acFtpCtrlMsg421)-1;
		return 0;
	}
	else
	{
		static const char acFtpReply[] = "150 Directory listing\r\n" ;//150 ������
		pcFtpCtrlMsg = (char*)acFtpReply;
		sFtpCtrlMsgLen = sizeof(acFtpReply)-1;
		return FTP_DATA_LIST;//���ʹ���Ϣ�����ݶ˿�����
	}
}



/**
	* @brief    vFtpCtrl_Cmd_RETR
	*           ftp ����˿����� RETR
	* @param    arg ������������
	* @return   0 ������Ҫ���� data port ��������Ҫ���� data port 
*/
static uint32_t iFtpCtrl_Cmd_RETR(char * arg,uint16_t arglen)
{
	if (stFtpS.cPolling)//������ݶ˿�����������ݣ����ƶ˿ڷ��ش��󣬷����������ݳ�ͻ
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
		if (*pcFilePath != '/')//���·��
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
	*           ftp ����˿����� RETR
	* @param    arg ������������
	* @return   0 ������Ҫ���� data port
*/
static uint32_t iFtpCtrl_Cmd_DELE(char * arg,uint16_t arglen)
{
	static const char acFtpReplyOK[] = "250 Operation successful\r\n" ;
	static const char acFtpReplyError[] = "450 Operation error\r\n" ;

	char * pcFilePath = arg;
	char * pcPathEnd = arg + arglen - 1;
	
	vFtp_GetLegalPath(pcFilePath,pcPathEnd);

	if (*pcFilePath != '/')//���·��
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
	*           ftp ����˿����� STOR
	* @param    arg ������������
	* @return   0 ������Ҫ���� data port ��������Ҫ���� data port 
*/
static uint32_t iFtpCtrl_Cmd_STOR(char * arg,uint16_t arglen)
{
	static const char acFtpReply[] = "125 Waiting\r\n" ;
	if (stFtpS.cPolling)//������ݶ˿�����������ݣ����ƶ˿ڷ��ش��󣬷����������ݳ�ͻ
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
		if (*pcFilePath != '/')//���·��
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
	
	static char cNeedPoll = 0; //�費��Ҫ�������ݶ˿�
	static uint16_t sRexmitLen = 0;//�����ط��ĳ���
	
	if(uip_connected())
	{
		pcFtpCtrlMsg = (char*)acFtpConnect;
		sFtpCtrlMsgLen = sizeof(acFtpConnect)-1;
		goto FtpCtrlSend; //�ظ����ӳɹ�
	}

	if (uip_flags & (UIP_CLOSE|UIP_ABORT|UIP_TIMEDOUT)) //==if(uip_closed()||uip_aborted()||uip_timedout()) //
	{
		acFtpCurrentDir[0] = 0;//��λ��ǰĿ¼
		color_printk(green,"\r\n|!ftp disconnect!|\r\n");
	}

	// ���ݶ˿ڴ���������� poll ���ƶ˿�
	if (uip_poll() && stFtpS.cNeedPoll && (!stFtpS.DataConn) && (uip_conn == stFtpS.CtrlConn))
	{
		stFtpS.cNeedPoll = 0;
		stFtpS.CtrlConn = NULL;//�������
		pcFtpCtrlMsg = (char *)acFtpCtrlMsg226;
		sFtpCtrlMsgLen = sizeof(acFtpCtrlMsg226) - 1;
		goto FtpCtrlSend;//�ظ��������
	}
	
	/*//��Ҫ�������ݶ˿ڣ��յ�Ӧ���Ժ��ٵ���*/
	if ( uip_acked() && cNeedPoll && (uip_conn == stFtpS.CtrlConn))
	{
		stFtpS.cNeedPoll = cNeedPoll;//����ź������� uIP_FtpServerPro() �������ݶ˿�
		cNeedPoll = 0;
	}

	if(uip_newdata()) 
	{
 		struct ftp_cmd * pCmdMatch ;
	 	uint32_t iCtrlCmd = FTP_STR2ID(uip_appdata);// �������ַ���תΪ������
		char *   arg = (char *)uip_appdata + 4;        //�����ַ�����������
		uint16_t arglen = uip_datalen() - 4;
		
		if ( *(arg-1) < 'A' || *(arg-1) > 'z' )//��Щ����ֻ�������ֽڣ���Ҫ�ж�
			iCtrlCmd &= 0x00ffffff;

		pCmdMatch = pFtp_SearchCmd(iCtrlCmd);//ƥ�������
		if (pCmdMatch)
		{
			cNeedPoll = pCmdMatch->Func(arg,arglen);
			if ( cNeedPoll )          //��Ҫ�������ݶ˿� �����ڿ��ƶ˿ڽ�����Ϣ�ظ����յ�Ӧ����ٵ���
				stFtpS.CtrlConn = uip_conn; //��¼�󶨿��ƶ˿ڣ����ݶ˿ڽ����������Ҫ�ص��˶˿�����
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

		if ( (fno.fattrib & AM_DIR) && (fno.fattrib != AM_DIR))//����ʾֻ��/ϵͳ/�����ļ���
			continue;

		pStDate = (struct FileDate *)(&fno.fdate);
		pStTime = (struct FileTime *)(&fno.ftime);
		
		if (fno.fdate == 0 || fno.ftime == 0) //û�����ڵ��ļ�
			vFtp_NormalList(pcBuf,fno.fsize,1,1,1980,fno.fname);
		else
		if (pStDate->Year + 1980 == 2018) //ͬһ����ļ�
			vFtp_ThisYearList(pcBuf,fno.fsize,pStDate->Month,pStDate->Day,pStTime->Hour,pStTime->Min,fno.fname);
		else
			vFtp_NormalList(pcBuf,fno.fsize,pStDate->Month,pStDate->Day,pStDate->Year+1980,fno.fname);
		
		if (fno.fattrib & AM_DIR )   /* It is a directory */
			pcBuf[0] = 'd';

		sFtpDataMsgLen += strlen(pcBuf);
		if (uip_mss() - sFtpDataMsgLen < sListMinLen)//���ʣ��ռ䲻������ɨ��
			return FTP_DATA_LIST;
	}
	
	f_closedir(&stFtpCurrentDir); //��·���ر�

ScanDirDone:
	cContinueScan = 0;
	return 0;
}



static uint32_t iFtpData_RETR(void)
{
	static uint32_t iFileSize = 0;
	FRESULT res ;
	uint32_t iReadSize;
	char * pcFilePath = acFtpCtrlBuf;//·���� iFtpCtrl_Cmd_RETR() ���� acFtpCtrlBuf �У�

	if (0 == iFileSize) //��һ�λ�û�����꣬����Ҫ���´��ļ�
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
		res = f_read(&stFtpFileHandle,acFtpDataMsg,uip_mss(),&iReadSize);//����󳤶� mss ��ȡ����
		if ((FR_OK != res) || (0 == iReadSize)) //��ȡ����
		{
			iFileSize = 0; //����ѭ��
			Errors("Cannot read \"%s\",error code :%d\r\n",pcFilePath,res);
		}
		else
		{
			sFtpDataMsgLen = iReadSize;
			iFileSize -= iReadSize;    //����ʣ�����ݴ�С
			if (iFileSize)
				return FTP_DATA_RETR; //δ��ȡ�귵�أ��ȷ��ʹ˰�����
		}
	}
	
	f_close(&stFtpFileHandle);
SendEnd:
	return 0;
}



void uIP_FtpDataPortCall(void)
{
	static uint8_t cNeedAck = 0;//���ݽ϶�ʱһ�����Ͳ��꣬������һ�������ack��������
	static uint16_t sRexmitLen = 0;
	static FRESULT enFileState = FR_NO_FILE;//�����ļ�״̬
	
	if(uip_connected()) 
	{
		stFtpS.DataConn = uip_conn;//�ȴ����ݶ˿ڵ���
		return ;
	}

	if(uip_newdata() && !enFileState) //�������ݣ�һ��Ϊ�����ļ�
	{
		uint32_t byteswritten;
		memcpy(acFtpDataMsg , uip_appdata , uip_len);//��Ҫ���������������ֲ�������
		enFileState = f_write(&stFtpFileHandle,(void*)acFtpDataMsg, uip_len, &byteswritten);
		if ((byteswritten == 0) || (enFileState != FR_OK))
		{
			f_close(&stFtpFileHandle);
			Errors("write file error\r\n");	
			goto CloseDataPort;
		}
	}

	//�������������ļ������󣬷��ͷ��������Ͽ����ӡ�
	if (uip_flags & (UIP_CLOSE|UIP_ABORT|UIP_TIMEDOUT)) //==if(uip_closed()||uip_aborted()||uip_timedout()) //
	{
		if (FR_OK == enFileState)//����ļ�����
		{
			printk("recieve file completely\r\n");
			enFileState = FR_NO_FILE;
			f_close(&stFtpFileHandle);
		}
		goto CloseDataPort;
	}

	//������������һ����Ϣ�յ�Ӧ�𣬻����ɿ��ƶ˿ڵ���
	if ( uip_acked() || stFtpS.cNeedPoll) 
	{
		if (stFtpS.cNeedPoll)
		{
			cNeedAck = stFtpS.cNeedPoll;
			stFtpS.cPolling = stFtpS.cNeedPoll;//�������ݶ˿�״ֵ̬����¼���������¼�
			stFtpS.cNeedPoll = 0; //�����Ҫ�����¼�
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
		if (!sFtpDataMsgLen) //û����Ϣ��Ҫ���ͣ�˵���������,�����������رն˿�
			goto CloseDataPort;
	}

	if(uip_rexmit())//�����ط�
	{
		Warnings("ftp data rexmit\r\n");
		uip_send(acFtpDataMsg,sRexmitLen);
		return;
	}
	
	if (sFtpDataMsgLen) //��������Ϣ
	{
		uip_send(acFtpDataMsg,sFtpDataMsgLen);//���ʹ˰�����
		sRexmitLen = sFtpDataMsgLen;//��¼��ǰ�����ȣ������Ҫ�ط�
		sFtpDataMsgLen = 0; 
		return;
	}

	return ;

CloseDataPort:
	uip_close(); //�ر�����
	stFtpS.cNeedPoll = stFtpS.cPolling;//������ɺ���Ҫ���ÿ��ƶ˿ڽ�����Ϣ�ظ�
	stFtpS.DataConn = NULL;       //������Ӽ�¼
	stFtpS.cPolling = 0;          //������ݶ˿ڴ���״̬
}




/**
	* @brief    uIP_FtpServerPro
	*           ftp ��ѯ
	* @param    void
*/
void uIP_FtpServerPro(void)
{
	if (stFtpS.cNeedPoll) //�е�������
	{
		if (stFtpS.DataConn)//����ǿ��ƶ˿ڵ������ݶ˿ڣ�DataConn ��������
			uip_poll_conn(stFtpS.DataConn);
		else                 //���ݶ˿ڴ��������DataConnΪ�գ��Ż�������ƶ˿�
			uip_poll_conn(stFtpS.CtrlConn);//���ݶ˿ڴ��������poll ���ƶ˿�
		
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

	//������ص����������
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

	memset(&stFtpS,0,sizeof(ftpstate_t));//���״ֵ̬
}



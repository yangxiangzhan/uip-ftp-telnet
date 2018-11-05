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

typedef uint32_t (*replyfunc_t)(char * arg,uint16_t arglen);
typedef struct ftp_cmd
{
	uint32_t	  index;	 //�����ʶ��
	replyfunc_t	  func;      //��¼�����ָ��
	struct avl_node cmd_node;//avl���ڵ�
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
	FTP_DATA_LIST ,   // (1) ���ݶ˿ڷ����ļ��б�
	FTP_DATA_RETR  ,  // (2) ���ݶ˿ڷ����ļ�
	FTP_DATA_STOR ,   // (3) ���ݶ˿ڽ����ļ�
};


/* Private macro ------------------------------------------------------------*/

//�ַ���ת���Σ��������� FTP �����Ϊһ�� ftp ����ֻ�� 3-4 ���ַ����պÿ���תΪ���ͣ�����Сд
#define FTP_STR2ID(str) ((*(int*)(str)) & 0xDFDFDFDF) 

// ftp ����������
#define FTP_REGISTER_COMMAND(CMD) \
	do{\
		static struct ftp_cmd cmd ;      \
		cmd.index = FTP_STR2ID(#CMD);    \
		cmd.func  = ctrl_port_reply_##CMD;\
		ftp_insert_cmd(&cmd);            \
	}while(0)


// ftp �ļ��б��ʽ
#define NORMAL_LIST(listbuf,filesize,month,day,year,filename)\
	sprintf((listbuf),normal_format,(filesize),month_list[(month)],(day),(year),(filename))

#define THIS_YEAR_LIST(listbuf,filesize,month,day,hour,min,filename)\
	sprintf((listbuf),this_year_format,(filesize),month_list[(month)],(day),(hour),(min),(filename))


// ftp ��ʽһ��Ϊ xxxx /dir1/dir2/\r\n ,�����ո�ȥ��ĩβ�� /\r\n ��ȡ����·�� 	
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

static const char  * month_list[] = { //�·ݴ� 1 �� 12 ��0 ��� NULL 
	NULL,
	"Jan","Feb","Mar","Apr","May","Jun",
	"Jul","Aug","Sep","Oct","Nov","Dez" };   

static struct avl_root ftp_root = {.avl_node = NULL};//����ƥ���ƽ����������� 

static struct ftp_state ftpstate;//ftp״ֵ̬

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
	*			����������
	* @param	ftpcmd		������ƿ�
	* @return	�ɹ����� 0
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
	*			���������ң����� index ���ҵ���Ӧ�Ŀ��ƿ�
	* @param	index		 �����
	* @return	�ɹ� index �Ŷ�Ӧ�Ŀ��ƿ�
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
	*           ftp ����˿����� USER ��ϵͳ��½���û���
	* @param    arg ������������
	* @return   0 ������Ҫ���� data port
*/
static uint32_t ctrl_port_reply_USER(char * arg,uint16_t arglen)
{
	static const char reply_msg[] = "230 Operation successful\r\n";  //230 ��½������
	ctrl_msg = (char*)reply_msg;
	ctrl_msg_len = sizeof(reply_msg)-1;
	return 0;
}


/**
	* @brief    ctrl_port_reply_SYST 
	*           ftp ����˿����� SYST �����ط�����ʹ�õĲ���ϵͳ
	* @param    arg ������������
	* @return   0 ������Ҫ���� data port
*/
static uint32_t ctrl_port_reply_SYST(char * arg,uint16_t arglen)
{
	static const char reply_msg[] = "215 UNIX Type: L8\r\n";  //215 ϵͳ���ͻظ�
	ctrl_msg = (char*)reply_msg;
	ctrl_msg_len = sizeof(reply_msg)-1;
	return 0;
}



/**
	* @brief    ctrl_port_reply_NOOP 
	*           ftp ����˿����� NOOP
	* @param    arg ������������
	* @return   0 ������Ҫ���� data port
*/
static uint32_t ctrl_port_reply_NOOP(char * arg,uint16_t arglen) //��ʾ��ǰ����Ŀ¼
{
	static const char reply_msg[] = "200 Operation successful\r\n";
	ctrl_msg = (char*)reply_msg;
	ctrl_msg_len = sizeof(reply_msg)-1;
	return 0;
}



/**
	* @brief    ctrl_port_reply_PWD 
	*           ftp ����˿����� PWD
	* @param    arg ������������
	* @return   0 ������Ҫ���� data port
*/
static  uint32_t ctrl_port_reply_PWD(char * arg,uint16_t arglen) //��ʾ��ǰ����Ŀ¼
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
	*           ftp ����˿����� CWD
	* @param    arg ������������
	* @return   0 ������Ҫ���� data port
*/
static uint32_t ctrl_port_reply_CWD(char * arg,uint16_t arglen) //��ʾ��ǰ����Ŀ¼
{
	static const char reply_msg[] = "250 Operation successful\r\n"; //257 ·��������
	DIR fsdir;
	char * file_path = arg;
	char * path_end = arg + arglen - 1;
	
	LEGAL_PATH(file_path,path_end);
	if (FR_OK != f_opendir(&fsdir,file_path))
		goto CWDdone;
	else
		f_closedir(&fsdir);
	
	if (path_end != file_path)
		memcpy(ftp_current_dir,file_path,path_end - file_path); //���浱ǰ·��
		
	ftp_current_dir[path_end - file_path] = 0;
CWDdone:
	ctrl_msg = (char*)reply_msg;
	ctrl_msg_len = sizeof(reply_msg)-1;
	return 0;
}



/**
	* @brief    ctrl_port_reply_PASV 
	*           ftp ����˿����� PASV ������ģʽ
	*           
	* @param    arg ������������
	* @return   0 ������Ҫ���� data port
*/
static uint32_t ctrl_port_reply_PASV(char * arg,uint16_t arglen) //��ʾ��ǰ����Ŀ¼
{
	static char reply_msg[64] = {0} ; //"227 PASV ok(192,168,40,104,185,198)\r\n"

	ctrl_msg = (char*)reply_msg;
	ctrl_msg_len = strlen(reply_msg);
	if (0 == ctrl_msg_len) // δ��ʼ����Ϣ
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
	*           ftp ����˿����� SIZE , ��ȡ��ǰ�ļ��б�
	* @param    arg ������������
	* @return   0 ������Ҫ���� data port
*/
static uint32_t ctrl_port_reply_SIZE(char * arg,uint16_t arglen)
{	
	uint32_t file_size;
	char * file_path = arg;
	char * path_end = arg + arglen - 1;

	LEGAL_PATH(file_path,path_end);

	if (*file_path != '/')//���·����ȫΪ����·��
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
	*           ftp ����˿����� LIST , ��ȡ��ǰ�ļ��б�
	* @param    arg ������������
	* @return   0 ������Ҫ���� data port ��������Ҫ���� data port 
*/
static uint32_t ctrl_port_reply_LIST(char * arg,uint16_t arglen) //��ʾ��ǰ����Ŀ¼
{
	//1.�ڿ��ƶ˿ڶ� LIST ������лظ�
	//2.�����ݶ˿ڷ��� "total 0"�����ò�ƿ���û��
	//3.�����ݶ˿ڷ����ļ��б�
	//4.�ر����ݶ˿�
	if (ftpstate.polling) //������ݶ˿�����������ݣ����ƶ˿ڷ��ش��󣬷����������ݳ�ͻ
	{
		ctrl_msg = (char*)ftp_msg_421;
		ctrl_msg_len = sizeof(ftp_msg_421)-1;
		return 0;
	}
	else
	{
		static const char reply_msg[] = "150 Directory listing\r\n" ;//150 ������
		ctrl_msg = (char*)reply_msg;
		ctrl_msg_len = sizeof(reply_msg)-1;
		return FTP_DATA_LIST;//���ʹ���Ϣ�����ݶ˿�����
	}
}



/**
	* @brief    ctrl_port_reply_RETR
	*           ftp ����˿����� RETR
	* @param    arg ������������
	* @return   0 ������Ҫ���� data port ��������Ҫ���� data port 
*/
static uint32_t ctrl_port_reply_RETR(char * arg,uint16_t arglen)
{
	if (ftpstate.polling)//������ݶ˿�����������ݣ����ƶ˿ڷ��ش��󣬷����������ݳ�ͻ
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
		if (*file_path != '/')//���·��
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
	*           ftp ����˿����� RETR
	* @param    arg ������������
	* @return   0 ������Ҫ���� data port
*/
static uint32_t ctrl_port_reply_DELE(char * arg,uint16_t arglen)
{
	static const char reply_msgOK[] = "250 Operation successful\r\n" ;
	static const char reply_msgError[] = "450 Operation error\r\n" ;

	char * file_path = arg;
	char * path_end = arg + arglen - 1;
	
	LEGAL_PATH(file_path,path_end);

	if (*file_path != '/')//���·��
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
	*           ftp ����˿����� STOR
	* @param    arg ������������
	* @return   0 ������Ҫ���� data port ��������Ҫ���� data port 
*/
static uint32_t ctrl_port_reply_STOR(char * arg,uint16_t arglen)
{
	if (ftpstate.polling)//������ݶ˿�����������ݣ����ƶ˿ڷ��ش��󣬷����������ݳ�ͻ
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
		
		if (*file_path != '/')//���·��
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
	
	static char needpoll = 0; //�費��Ҫ�������ݶ˿�
	static uint16_t rexmit = 0;//�����ط��ĳ���
	
	if(uip_connected())
	{
		ctrl_msg = (char*)connect_msg;
		ctrl_msg_len = sizeof(connect_msg)-1;
		goto FtpCtrlSend; //�ظ����ӳɹ�
	}

	if (uip_flags & (UIP_CLOSE|UIP_ABORT|UIP_TIMEDOUT)) //==if(uip_closed()||uip_aborted()||uip_timedout()) //
	{
		ftp_current_dir[0] = 0;//��λ��ǰĿ¼
		color_printk(green,"\r\n|!ftp disconnect!|\r\n");
	}

	// ���ݶ˿ڴ���������� poll ���ƶ˿�
	if (uip_poll() && ftpstate.needpoll && (!ftpstate.data_conn) && (uip_conn == ftpstate.ctrl_conn))
	{
		ftpstate.needpoll = 0;
		ftpstate.ctrl_conn = NULL;//�������
		ctrl_msg = (char *)ftp_msg_226;
		ctrl_msg_len = sizeof(ftp_msg_226) - 1;
		goto FtpCtrlSend;//�ظ��������
	}
	
	/*//��Ҫ�������ݶ˿ڣ��յ�Ӧ���Ժ��ٵ���*/
	if ( uip_acked() && needpoll && (uip_conn == ftpstate.ctrl_conn))
	{
		ftpstate.needpoll = needpoll;//����ź������� ftp_server_process() �������ݶ˿�
		needpoll = 0;
	}

	if(uip_newdata()) 
	{
 		struct ftp_cmd * ctrlcmd ;
	 	uint32_t cmdindex = FTP_STR2ID(uip_appdata);// �������ַ���תΪ������
		char *   arg = (char *)uip_appdata + 4;        //�����ַ�����������
		uint16_t arglen = uip_datalen() - 4;
		
		if ( *(arg-1) < 'A' || *(arg-1) > 'z' )//��Щ����ֻ�������ֽڣ���Ҫ�ж�
			cmdindex &= 0x00ffffff;

		ctrlcmd = ftp_search_cmd(cmdindex);//ƥ�������
		if (ctrlcmd)
		{
			needpoll = ctrlcmd->func(arg,arglen);
			if ( needpoll )          //��Ҫ�������ݶ˿� �����ڿ��ƶ˿ڽ�����Ϣ�ظ����յ�Ӧ����ٵ���
				ftpstate.ctrl_conn = uip_conn; //��¼�󶨿��ƶ˿ڣ����ݶ˿ڽ����������Ҫ�ص��˶˿�����
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

		if ( (fno.fattrib & AM_DIR) && (fno.fattrib != AM_DIR))//����ʾֻ��/ϵͳ/�����ļ���
			continue;

		file_date = (struct FileDate *)(&fno.fdate);
		file_time = (struct FileTime *)(&fno.ftime);
		
		if (fno.fdate == 0 || fno.ftime == 0) //û�����ڵ��ļ�
			NORMAL_LIST(buf,fno.fsize,1,1,1980,fno.fname);
		else
		if (file_date->Year + 1980 == 2018) //ͬһ����ļ�
			THIS_YEAR_LIST(buf,fno.fsize,file_date->Month,file_date->Day,file_time->Hour,file_time->Min,fno.fname);
		else
			NORMAL_LIST(buf,fno.fsize,file_date->Month,file_date->Day,file_date->Year+1980,fno.fname);
		
		if (fno.fattrib & AM_DIR )   /* It is a directory */
			buf[0] = 'd';

		data_msg_len += strlen(buf);
		if (uip_mss() - data_msg_len < list_min)//���ʣ��ռ䲻������ɨ��
			return FTP_DATA_LIST;
	}
	
	f_closedir(&ftp_dir); //��·���ر�

ScanDirDone:
	continue_scan = 0;
	return 0;
}



static uint32_t data_port_reply_RETR(void)
{
	static uint32_t file_size = 0;
	char * file_path = ftp_ctrl_buf;//·���� ctrl_port_reply_RETR() ���� ftp_ctrl_buf �У�
	uint32_t read_size;
	FRESULT res ;

	if (0 == file_size) //��һ�λ�û�����꣬����Ҫ���´��ļ�
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
		res = f_read(&ftp_file,data_msg,uip_mss(),&read_size);//����󳤶� mss ��ȡ����
		if ((FR_OK != res) || (0 == read_size)) //��ȡ����
		{
			file_size = 0; //����ѭ��
			Errors("Cannot read \"%s\",error code :%d\r\n",file_path,res);
		}
		else
		{
			data_msg_len = read_size;
			file_size -= read_size;    //����ʣ�����ݴ�С
			if (file_size)
				return FTP_DATA_RETR; //δ��ȡ�귵�أ��ȷ��ʹ˰�����
		}
	}
	
	f_close(&ftp_file);
SendEnd:
	return 0;
}



void ftp_data_port_call(void)
{
	static uint8_t need_ack = 0;//���ݽ϶�ʱһ�����Ͳ��꣬������һ�������ack��������
	static uint16_t rexmit = 0;
	static FRESULT file_state = FR_NO_FILE;//�����ļ�״̬
	
	if(uip_connected()) 
	{
		ftpstate.data_conn = uip_conn;//�ȴ����ݶ˿ڵ���
		return ;
	}

	if(uip_newdata() && !file_state) //�������ݣ�һ��Ϊ�����ļ�
	{
		uint32_t byteswritten;
		memcpy(data_msg , uip_appdata , uip_len);//uip_appdata �ڴ��ַû�ж��룬��Ҫ���������������ֲ�������
		file_state = f_write(&ftp_file,(void*)data_msg, uip_len, &byteswritten);
		if ((byteswritten == 0) || (file_state != FR_OK))
		{
			f_close(&ftp_file);
			Errors("write file error\r\n");	
			goto CloseDataPort;
		}
	}

	//�������������ļ������󣬷��ͷ��������Ͽ����ӡ�
	if (uip_flags & (UIP_CLOSE|UIP_ABORT|UIP_TIMEDOUT)) //==if(uip_closed()||uip_aborted()||uip_timedout()) //
	{
		if (FR_OK == file_state)//����ļ�����
		{
			printk("recieve file completely\r\n");
			file_state = FR_NO_FILE;
			f_close(&ftp_file);
		}
		goto CloseDataPort;
	}

	//������������һ����Ϣ�յ�Ӧ�𣬻����ɿ��ƶ˿ڵ���
	if ( uip_acked() || ftpstate.needpoll) 
	{
		if (ftpstate.needpoll)
		{
			need_ack = ftpstate.needpoll;
			ftpstate.polling = ftpstate.needpoll;//�������ݶ˿�״ֵ̬����¼���������¼�
			ftpstate.needpoll = 0; //�����Ҫ�����¼�
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
		if (!data_msg_len) //û����Ϣ��Ҫ���ͣ�˵���������,�����������رն˿�
			goto CloseDataPort;
	}

	if(uip_rexmit())//�����ط�
	{
		Warnings("ftp data rexmit\r\n");
		uip_send(data_msg,rexmit);
		return;
	}
	
	if (data_msg_len) //��������Ϣ
	{
		uip_send(data_msg,data_msg_len);//���ʹ˰�����
		rexmit = data_msg_len;//��¼��ǰ�����ȣ������Ҫ�ط�
		data_msg_len = 0; 
		return;
	}

	return ;

CloseDataPort:
	uip_close(); //�ر�����
	ftpstate.needpoll = ftpstate.polling;//������ɺ���Ҫ���ÿ��ƶ˿ڽ�����Ϣ�ظ�
	ftpstate.data_conn = NULL;       //������Ӽ�¼
	ftpstate.polling = 0;          //������ݶ˿ڴ���״̬
}




/** 
	* @brief    ftp_server_process
	*           ftp ��ѯ
	* @param    void
*/
void ftp_server_process(void)
{
	if (ftpstate.needpoll) //�е�������
	{
		if (ftpstate.data_conn)//����ǿ��ƶ˿ڵ������ݶ˿ڣ�data_conn ��������
			uip_poll_conn(ftpstate.data_conn);
		else                 //���ݶ˿ڴ��������data_connΪ�գ��Ż�������ƶ˿�
			uip_poll_conn(ftpstate.ctrl_conn);//���ݶ˿ڴ��������poll ���ƶ˿�
		
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

	//������ص����������
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

	memset(&ftpstate,0,sizeof(ftpstate_t));//���״ֵ̬
}



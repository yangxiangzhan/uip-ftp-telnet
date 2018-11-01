/**
  ******************************************************************************
  * @file           shell.c
  * @author         ��ô��
  * @brief          shell ���������
  ******************************************************************************
  *
  * COPYRIGHT(c) 2018 GoodMorning
  *
  ******************************************************************************
  */
/* Includes ---------------------------------------------------*/
#include <string.h>
#include <stdarg.h>
#include <stdint.h> //�����˺ܶ���������
#include <stdio.h>
#include "shell.h"

//--------------------��غ궨�弰�ṹ�嶨��--------------------
const static  uint8_t F_CRC8_Table[256] = {//����,��λ���� x^8+x^5+x^4+1
	0x00, 0x31, 0x62, 0x53, 0xc4, 0xf5, 0xa6, 0x97, 0xb9, 0x88, 0xdb, 0xea, 0x7d, 0x4c, 0x1f, 0x2e,
	0x43, 0x72, 0x21, 0x10, 0x87, 0xb6, 0xe5, 0xd4, 0xfa, 0xcb, 0x98, 0xa9, 0x3e, 0x0f, 0x5c, 0x6d,
	0x86, 0xb7, 0xe4, 0xd5, 0x42, 0x73, 0x20, 0x11, 0x3f, 0x0e, 0x5d, 0x6c, 0xfb, 0xca, 0x99, 0xa8,
	0xc5, 0xf4, 0xa7, 0x96, 0x01, 0x30, 0x63, 0x52, 0x7c, 0x4d, 0x1e, 0x2f, 0xb8, 0x89, 0xda, 0xeb,
	0x3d, 0x0c, 0x5f, 0x6e, 0xf9, 0xc8, 0x9b, 0xaa, 0x84, 0xb5, 0xe6, 0xd7, 0x40, 0x71, 0x22, 0x13,
	0x7e, 0x4f, 0x1c, 0x2d, 0xba, 0x8b, 0xd8, 0xe9, 0xc7, 0xf6, 0xa5, 0x94, 0x03, 0x32, 0x61, 0x50,
	0xbb, 0x8a, 0xd9, 0xe8, 0x7f, 0x4e, 0x1d, 0x2c, 0x02, 0x33, 0x60, 0x51, 0xc6, 0xf7, 0xa4, 0x95,
	0xf8, 0xc9, 0x9a, 0xab, 0x3c, 0x0d, 0x5e, 0x6f, 0x41, 0x70, 0x23, 0x12, 0x85, 0xb4, 0xe7, 0xd6,
	0x7a, 0x4b, 0x18, 0x29, 0xbe, 0x8f, 0xdc, 0xed, 0xc3, 0xf2, 0xa1, 0x90, 0x07, 0x36, 0x65, 0x54,
	0x39, 0x08, 0x5b, 0x6a, 0xfd, 0xcc, 0x9f, 0xae, 0x80, 0xb1, 0xe2, 0xd3, 0x44, 0x75, 0x26, 0x17,
	0xfc, 0xcd, 0x9e, 0xaf, 0x38, 0x09, 0x5a, 0x6b, 0x45, 0x74, 0x27, 0x16, 0x81, 0xb0, 0xe3, 0xd2,
	0xbf, 0x8e, 0xdd, 0xec, 0x7b, 0x4a, 0x19, 0x28, 0x06, 0x37, 0x64, 0x55, 0xc2, 0xf3, 0xa0, 0x91,
	0x47, 0x76, 0x25, 0x14, 0x83, 0xb2, 0xe1, 0xd0, 0xfe, 0xcf, 0x9c, 0xad, 0x3a, 0x0b, 0x58, 0x69,
	0x04, 0x35, 0x66, 0x57, 0xc0, 0xf1, 0xa2, 0x93, 0xbd, 0x8c, 0xdf, 0xee, 0x79, 0x48, 0x1b, 0x2a,
	0xc1, 0xf0, 0xa3, 0x92, 0x05, 0x34, 0x67, 0x56, 0x78, 0x49, 0x1a, 0x2b, 0xbc, 0x8d, 0xde, 0xef,
	0x82, 0xb3, 0xe0, 0xd1, 0x46, 0x77, 0x24, 0x15, 0x3b, 0x0a, 0x59, 0x68, 0xff, 0xce, 0x9d, 0xac
};

const static  uint8_t B_CRC8_Table[256] = {//����,��λ���� x^8+x^5+x^4+1
	0x00, 0x5e, 0xbc, 0xe2, 0x61, 0x3f, 0xdd, 0x83, 0xc2, 0x9c, 0x7e, 0x20, 0xa3, 0xfd, 0x1f, 0x41,
	0x9d, 0xc3, 0x21, 0x7f, 0xfc, 0xa2, 0x40, 0x1e, 0x5f, 0x01, 0xe3, 0xbd, 0x3e, 0x60, 0x82, 0xdc,
	0x23, 0x7d, 0x9f, 0xc1, 0x42, 0x1c, 0xfe, 0xa0, 0xe1, 0xbf, 0x5d, 0x03, 0x80, 0xde, 0x3c, 0x62,
	0xbe, 0xe0, 0x02, 0x5c, 0xdf, 0x81, 0x63, 0x3d, 0x7c, 0x22, 0xc0, 0x9e, 0x1d, 0x43, 0xa1, 0xff,
	0x46, 0x18, 0xfa, 0xa4, 0x27, 0x79, 0x9b, 0xc5, 0x84, 0xda, 0x38, 0x66, 0xe5, 0xbb, 0x59, 0x07,
	0xdb, 0x85, 0x67, 0x39, 0xba, 0xe4, 0x06, 0x58, 0x19, 0x47, 0xa5, 0xfb, 0x78, 0x26, 0xc4, 0x9a,
	0x65, 0x3b, 0xd9, 0x87, 0x04, 0x5a, 0xb8, 0xe6, 0xa7, 0xf9, 0x1b, 0x45, 0xc6, 0x98, 0x7a, 0x24,
	0xf8, 0xa6, 0x44, 0x1a, 0x99, 0xc7, 0x25, 0x7b, 0x3a, 0x64, 0x86, 0xd8, 0x5b, 0x05, 0xe7, 0xb9,
	0x8c, 0xd2, 0x30, 0x6e, 0xed, 0xb3, 0x51, 0x0f, 0x4e, 0x10, 0xf2, 0xac, 0x2f, 0x71, 0x93, 0xcd,
	0x11, 0x4f, 0xad, 0xf3, 0x70, 0x2e, 0xcc, 0x92, 0xd3, 0x8d, 0x6f, 0x31, 0xb2, 0xec, 0x0e, 0x50,
	0xaf, 0xf1, 0x13, 0x4d, 0xce, 0x90, 0x72, 0x2c, 0x6d, 0x33, 0xd1, 0x8f, 0x0c, 0x52, 0xb0, 0xee,
	0x32, 0x6c, 0x8e, 0xd0, 0x53, 0x0d, 0xef, 0xb1, 0xf0, 0xae, 0x4c, 0x12, 0x91, 0xcf, 0x2d, 0x73,
	0xca, 0x94, 0x76, 0x28, 0xab, 0xf5, 0x17, 0x49, 0x08, 0x56, 0xb4, 0xea, 0x69, 0x37, 0xd5, 0x8b,
	0x57, 0x09, 0xeb, 0xb5, 0x36, 0x68, 0x8a, 0xd4, 0x95, 0xcb, 0x29, 0x77, 0xf4, 0xaa, 0x48, 0x16,
	0xe9, 0xb7, 0x55, 0x0b, 0x88, 0xd6, 0x34, 0x6a, 0x2b, 0x75, 0x97, 0xc9, 0x4a, 0x14, 0xf6, 0xa8,
	0x74, 0x2a, 0xc8, 0x96, 0x15, 0x4b, 0xa9, 0xf7, 0xb6, 0xe8, 0x0a, 0x54, 0xd7, 0x89, 0x6b, 0x35
};





union uncmd
{
	struct // ����ŷ�Ϊ�����������
	{
		uint32_t CRC2      : 8;
		uint32_t CRC1      : 8;//��ʮ��λΪ���� crc У����
		uint32_t Sum       : 5;//�����ַ����ܺ�
		uint32_t Len       : 5;//�����ַ��ĳ��ȣ�5 bit ��������Ȳ��ܳ���31���ַ�
		uint32_t FirstChar : 6;//�����ַ��ĵ�һ���ַ�
	}part;

	uint32_t ID;//�ɴ˺ϲ�Ϊ 32 λ��������
};


//char * shell_input_sign = "shell >";
struct avl_root shell_avltree_root = {.avl_node = NULL};//����ƥ���ƽ����������� 
static struct shell_record
{
	char  buf[COMMANDLINE_MAX_RECORD][COMMANDLINE_MAX_LEN];
	uint8_t read;
	uint8_t write;
}
stShellHistory = {0};

char shell_input_sign[128] = DEFAULT_INPUTSIGN;
//------------------------------��غ�������------------------------------
struct shell_cmd * pShell_ParseCmdline(char  * cmdline);
static char * pcShell_Record(struct shell_buf * pStShellBuf);
void vShell_GetChar     (struct shell_buf * pStShellbuf , char ch);
void vShell_GetBackspace(struct shell_buf * pStShellBuf) ;
void vShell_GetTab      (struct shell_buf * pStShellBuf) ;
void vShell_ShowRecord  (struct shell_buf * pStShellBuf ,uint8_t LastOrNext);


//------------------------------�����ķָ���------------------------------
/**
	* @brief    pConsole_Search 
	*           ���������ң����� id ���ҵ���Ӧ�Ŀ��ƿ�
	* @param    CmdID        �����
	* @return   �ɹ� id �Ŷ�Ӧ�Ŀ��ƿ�
*/
static struct shell_cmd *pShell_SearchCmd(int CmdID)
{
    struct avl_node *node = shell_avltree_root.avl_node;

    while (node) 
	{
		struct shell_cmd *pCmd = container_of(node, struct shell_cmd, cmd_node);

		if (CmdID < pCmd->ID)
		    node = node->avl_left;
		else 
		if (CmdID > pCmd->ID)
		    node = node->avl_right;
  		else 
			return pCmd;
    }
    
    return NULL;
}



/**
	* @brief    iShell_InsertCmd 
	*           ����������
	* @param    pCmd        ������ƿ�
	* @return   �ɹ����� 0
*/
static int iShell_InsertCmd(struct shell_cmd * pCmd)
{
	struct avl_node **tmp = &shell_avltree_root.avl_node;
 	struct avl_node *parent = NULL;
	
	/* Figure out where to put new node */
	while (*tmp)
	{
		struct shell_cmd *this = container_of(*tmp, struct shell_cmd, cmd_node);

		parent = *tmp;
		if (pCmd->ID < this->ID)
			tmp = &((*tmp)->avl_left);
		else 
		if (pCmd->ID > this->ID)
			tmp = &((*tmp)->avl_right);
		else
			return 1;
	}

	/* Add new node and rebalance tree. */
	//rb_link_node(&pCmd->cmd_node, parent, tmp);
	//rb_insert_color(&pCmd->cmd_node, root);
	avl_insert(&shell_avltree_root,&pCmd->cmd_node,parent,tmp);
	
	return 0;
}


/**
	* @author   ��ô��
	* @brief    vShell_GetChar 
	*           �����м�¼����һ���ַ�
	* @param    
	* @return   
*/
void vShell_GetChar(struct shell_buf * pStShellbuf , char ch)
{
	char * ptr = pStShellbuf->bufmem + pStShellbuf->index;
	pStShellbuf->bufmem[pStShellbuf->index] = ch;
	pStShellbuf->index = (pStShellbuf->index + 1) % COMMANDLINE_MAX_LEN;
	pStShellbuf->bufmem[pStShellbuf->index] = 0;
	printl(ptr,1); //���������ӡ
}




/**
	* @author   ��ô��
	* @brief    vShell_GetBackspace 
	*           ����̨���� ���� ������
	* @param    void
	* @return   void
*/
void vShell_GetBackspace(struct shell_buf * pStShellBuf)
{
	if (pStShellBuf->index)//�����ǰ��ӡ�����������ݣ�����һ����λ
	{
		printk("\010 \010"); //KEYCODE_BACKSPACE 
		--pStShellBuf->index;
		pStShellBuf->bufmem[pStShellBuf->index] = 0;
	}
}



/** 
	* @brief vConsole_Input_TAB ����̨���� table ������
	* @param void
	*
	* @return NULL
*/
void vShell_GetTab(struct shell_buf * pStShellBuf)
{
	uint32_t iFirstChar;
	uint8_t cCnt ;
	
	uint8_t ucInputLen = pStShellBuf->index;
	char   * pInputStr = pStShellBuf->bufmem;
	
	struct shell_cmd * MatchCmd[10];//ƥ�䵽��������
	uint8_t            MatchNum = 0;//ƥ�䵽������Ÿ���
	
	while (*pInputStr == ' ')  //��ʱ�������ո���Ҫ����
	{
		++pInputStr;
		--ucInputLen;
	}
	
	if (*pInputStr == 0 || ucInputLen == 0) 
		return ;//û��������Ϣ����
	
	iFirstChar = (uint32_t)(*pInputStr)<<26;//ƥ������ĸ

    for (struct avl_node* node = avl_first(&shell_avltree_root); node ; node = avl_next(node))//����������
	{
		struct shell_cmd * pshell_cmd = avl_entry(node,struct shell_cmd, cmd_node);
		uint32_t  CmdFirstChar = (pshell_cmd->ID & (0xfc000000)); 
		
		if (iFirstChar == CmdFirstChar)//����ĸ��ͬ��ƥ������
		{
			if (memcmp(pshell_cmd->pName, pInputStr, ucInputLen) == 0) //�Ա������ַ��������ƥ�䵽��ͬ��
			{
				MatchCmd[MatchNum] = pshell_cmd;     //��ƥ�䵽�����������������
				if (++MatchNum > 10) 
					return ;    //����ʮ����ͬ����
			}
		}
		else
		if (CmdFirstChar > iFirstChar) // ��С���������������ƥ�䲻������ĸ�˳�ѭ��
		{
			break ;
		}
	}

	if (!MatchNum) 
		return ; //���û���������������ַ���������
	
	if (1 == MatchNum)  //���ֻ�ҵ���һ�����������ǰ������ַ�����ֱ�Ӳ�ȫ�������ӡ
	{
		for(char * ptr = MatchCmd[0]->pName + ucInputLen ;*ptr ;++ptr) //��ӡʣ����ַ�		
			vShell_GetChar(pStShellBuf,*ptr);
	}
	else   //�����ֹһ�����������ǰ������ַ�������ӡ������ͬ�ַ��������б�����ȫ�ַ������ֱ���������ֵ�
	{
		for(cCnt = 0;cCnt < MatchNum; ++cCnt) 
			printk("\r\n\t%s",MatchCmd[cCnt]->pName); //�����к��������ַ����������б��ӡ����
		
		printk("\r\n%s%s",shell_input_sign,pStShellBuf->bufmem); //���´�ӡ�����־����������ַ���
		
		while(1)  //��ȫ�����ÿ������������ַ���ȫ����ӡ
		{
			for (cCnt = 1;cCnt < MatchNum; ++cCnt)
			{
				if (MatchCmd[0]->pName[ucInputLen] != MatchCmd[cCnt]->pName[ucInputLen]) 
					return  ; //�ַ���һ��������
			}
			vShell_GetChar(pStShellBuf,MatchCmd[0]->pName[ucInputLen++]);  //����ͬ���ַ���ȫ�����뻺����
		}
	}
}






/**
	* @author   ��ô��
	* @brief    vShell_Input 
	*           �����н�������
	* @param    
	* @return   
*/
void vShell_Parse(struct shell_buf * pStShellBuf)
{
	uint8_t ucLen = 0;
	uint8_t fcrc8 = 0;
	uint8_t bcrc8 = 0;
	uint8_t sSum = 0;
	union uncmd unCmd ;
	
	char * cmdline = pStShellBuf->bufmem;
	int cmdline_len = pStShellBuf->index ;
	
	struct shell_cmd * cmdmatch;
	
	while (*cmdline == ' ')	// Shave off any leading spaces
	{
		++cmdline;
		--cmdline_len;
	}

	if (0 == cmdline[0] || 0 == cmdline_len)
		goto parseend;
	
	unCmd.part.FirstChar = *cmdline;
	
	while ((*cmdline != '\0') && (*cmdline != ' '))
	{
		sSum += *cmdline;
		fcrc8 = F_CRC8_Table[fcrc8^*cmdline];
		bcrc8 = B_CRC8_Table[bcrc8^*cmdline];
		++cmdline;
		++ucLen;
	}
	
	unCmd.part.Len = ucLen;
	unCmd.part.Sum = sSum;
	unCmd.part.CRC1 = fcrc8;
	unCmd.part.CRC2 = bcrc8;
	
	cmdmatch = pShell_SearchCmd(unCmd.ID);//ƥ�������

	if (cmdmatch != NULL)
	{
		char * record = pcShell_Record(pStShellBuf);  //��¼��ǰ�����������������
		char * arg = record + iShell_CmdLen(cmdmatch);
		cmdmatch->Func(arg);
	}
	else
	{
		printk("\r\n\r\n\tno reply:%s\r\n",pStShellBuf->bufmem);
	}
	
parseend:	
	pStShellBuf->index = 0;
	return ;
}




/**
	* @author   ��ô��
	* @brief    vShell_Input 
	*           Ӳ���Ͻ��յ������ݵ�����̨��Ĵ���
	* @param    pcHalRxBuf     Ӳ���������յ������ݻ�������ַ
	* @param    ucLen          Ӳ���������յ������ݳ���
	* @return   void
*/
void vShell_Input(struct shell_buf * pStShellbuf,char * ptr,uint8_t len)
{
	current_puts = pStShellbuf->puts; //shell ��ڶ�Ӧ����
	
	for ( ; len && *ptr; --len,++ptr)
	{
		switch (*ptr) //�ж��ַ��Ƿ�Ϊ�����ַ�
		{
			case KEYCODE_NEWLINE: //���� \r
				break;
				
			case KEYCODE_ENTER:
				printk("\r\n");
				if (pStShellbuf->index) 
					vShell_Parse(pStShellbuf);
				else
					printk("%s",shell_input_sign);
				break;
			
			case KEYCODE_TAB: 
				vShell_GetTab(pStShellbuf); 
				break;
			
			case KEYCODE_BACKSPACE : 
				vShell_GetBackspace(pStShellbuf); 
				break;
			
			case KEYCODE_ESC :
				if (ptr[1] == 0x5b)
				{
					switch(ptr[2])
					{
						case 0x41:vShell_ShowRecord(pStShellbuf,0);break;//�ϼ�ͷ
						case 0x42:vShell_ShowRecord(pStShellbuf,1);break;//�¼�ͷ
						default:;
					}
					
					len -= 2;
					ptr += 2;//��ͷ��3���ֽ��ַ�
				}
				
				break;
				
			case KEYCODE_CTRL_C:
				printk("^C");
				pStShellbuf->index = 0;
				break;
			
			default: // ��ͨ�ַ�
				vShell_GetChar(pStShellbuf,*ptr); //���뵽�ڴ滺����;
		}
	}
	
	current_puts = default_puts; //�ָ�Ĭ�ϴ�ӡ
}



/**
	* @author   ��ô��
	* @brief    pcShell_Record 
	*           ��¼�˴����е��������
	* @param    
	* @return   ���ؼ�¼��ַ
*/
static char * pcShell_Record(struct shell_buf * pStShellBuf)
{
	char * cmdline = pStShellBuf->bufmem;
	int cmdline_len = pStShellBuf->index ;
	
	char *  pcHistory = &stShellHistory.buf[stShellHistory.write][0];
	
	stShellHistory.write = (stShellHistory.write + 1) % COMMANDLINE_MAX_RECORD;
	stShellHistory.read = stShellHistory.write;
	
	memcpy(pcHistory,cmdline,cmdline_len);
	pcHistory[cmdline_len] = 0;
	
	return pcHistory;
}



/*******************************************************************
	* @author   ��ô��
	* @brief    vShell_ShowRecord 
	*           �����¼�ͷ����ʾ���������������˴�ֻ��¼������ε�����
	* @param    void
	* @return   void
*/
void vShell_ShowRecord(struct shell_buf * pStShellBuf,uint8_t LastOrNext)
{
	uint8_t ucLen;
	char *pcHistory;
	
	printk("\33[2K\r%s",shell_input_sign);//printk("\33[2K\r");��ʾ�����ǰ��

	if (!LastOrNext) //�ϼ�ͷ����һ������
	{
		stShellHistory.read = (!stShellHistory.read) ? (COMMANDLINE_MAX_RECORD - 1) : (stShellHistory.read - 1);
	}
	else
	{	
		if (stShellHistory.read == stShellHistory.write)
		{
			pStShellBuf->bufmem[0] = 0;
			pStShellBuf->index = 0 ;
			return ;
		}
		
		stShellHistory.read = (stShellHistory.read + 1) % COMMANDLINE_MAX_RECORD;
	}
	
	pcHistory = &stShellHistory.buf[stShellHistory.read][0];
	ucLen = strlen(pcHistory);
	if (ucLen)
	{
		memcpy(pStShellBuf->bufmem,pcHistory,ucLen);
		pStShellBuf->bufmem[ucLen] = 0;
		pStShellBuf->index = ucLen ;
		printl(pStShellBuf->bufmem,ucLen);
	}
	else
	{
		pStShellBuf->bufmem[0] = 0;
		pStShellBuf->index = 0 ;
	}
}


/********************************************************************
	* @brief    iShell_ParseParam 
	*           ת����ȡ����ź��������������ַ���תΪ����
	* @param    pcStr       �����ַ���������������������ָ��
	* @param    piValueBuf  ����ת���󻺴��ַ
	* @param    iParamNum   ���ݸ���
	* @return   void
		* @retval   PARAMETER_EMPTY         �������δ������ 
		* @retval   PARAMETER_CORRECT       �����������������ʽ��ȷ
		* @retval   PARAMETER_HELP          ���������� ? ��
		* @retval   PARAMETER_ERROR         �����������
*/
int iShell_ParseParam(char * pcStr,int * argc,int argv[])
{
	uint8_t ucCnt;
	uint8_t ucValue;

	while (' ' == *pcStr) ++pcStr;//�����ո�
	
	if (*pcStr == 0) //����������û�и������ַ����룬���ؿ�
	{
		*argc = 0;
		return PARAMETER_EMPTY;
	}

	if (*pcStr == '?')//��������������ʺţ�����help
	{
		*argc = 0;
		return PARAMETER_HELP;
	}

	for (ucCnt = 0; *pcStr && ucCnt < 4; ++ucCnt)//�ַ���Ϊ ��\0' ��ʱ��
	{
		argv[ucCnt] = 0;

		//ѭ�����ַ���תΪ���֣�ֱ���ַ���Ϊ 0 - 9
		for (ucValue = *pcStr - '0';ucValue < 10; ucValue = *(++pcStr) - '0')
		{
			argv[ucCnt] = argv[ucCnt] * 10 + ucValue;
		}

		if (*pcStr == '\0') //����Ҫ�ж� \r\n 
		{
			*argc = ucCnt + 1 ;
			return PARAMETER_CORRECT;
		}
		else
		if (*pcStr != ' ')	//������� 0 - 9 ���Ҳ��ǿո����Ǵ����ַ�
		{
			*argc = 0;
			return PARAMETER_ERROR;
		}
		
		while (' ' == *pcStr) ++pcStr;//�����ո�,�����ж���һ������

	}
	
	*argc = ucCnt;
	return PARAMETER_CORRECT;
}





/********************************************************************
	* @author   ��ô��
	* @brief    _Shell_RegisterCommand 
	*           ע��һ������źͶ�Ӧ������� ��ǰ׺Ϊ '_' ��ʾ������ֱ�ӵ��ô˺���
	* @param    pCmdName    ������
	* @param    Func        ��������Ӧ��ִ�к���
	* @param    newcmd      ������ƿ��Ӧ��ָ��
	* @return   void
*/
void _Shell_RegisterCommand(char * pCmdName, cmd_fn_def Func,struct shell_cmd * newcmd)//ע������
{
	char * pcStr = pCmdName;
	union uncmd unCmd ;

	uint8_t clen;
	uint8_t fcrc8 = 0;
	uint8_t bcrc8 = 0;
	uint8_t sSum = 0;

	for (clen = 0; *pcStr ; ++clen)
	{
		sSum += *pcStr;
		fcrc8 = F_CRC8_Table[fcrc8^*pcStr];
		bcrc8 = B_CRC8_Table[bcrc8^*pcStr];
		++pcStr;
	}

	unCmd.part.CRC1 = fcrc8;
	unCmd.part.CRC2 = bcrc8;
	unCmd.part.Len = clen;
	unCmd.part.Sum = sSum;
	unCmd.part.FirstChar = *pCmdName;
	
	newcmd->ID = unCmd.ID;
	newcmd->pName = pCmdName;
	newcmd->Func = Func;

	iShell_InsertCmd(newcmd);//�������������˽ڵ�

	return ;
}



/********************************************************************
	* @author   ��ô��
	* @brief    _Shell_RegisterCommand 
	*           ע��һ������źͶ�Ӧ���������ǰ׺Ϊ '_' ����������ֱ�ӵ���
	* @param    pCmdName    ������
	* @param    Func        ��������Ӧ��ִ�к���
	* @param    newcmd      ������ƿ��Ӧ��ָ��
	* @return   void
*/
void _Shell_RegisterCommand__(char * pCmdName, cmd_fn_def Func,void * cmdbuf)//ע������
{
	struct shell_cmd * newcmd = (struct shell_cmd *)cmdbuf;
	char * pcStr = pCmdName;
	union uncmd unCmd ;

	uint8_t clen;
	uint8_t fcrc8 = 0;
	uint8_t bcrc8 = 0;
	uint8_t sSum = 0;

	for (clen = 0; *pcStr ; ++clen)
	{
		sSum += *pcStr;
		fcrc8 = F_CRC8_Table[fcrc8^*pcStr];
		bcrc8 = B_CRC8_Table[bcrc8^*pcStr];
		++pcStr;
	}

	unCmd.part.CRC1 = fcrc8;
	unCmd.part.CRC2 = bcrc8;
	unCmd.part.Len = clen;
	unCmd.part.Sum = sSum;
	unCmd.part.FirstChar = *pCmdName;
	
	newcmd->ID = unCmd.ID;
	newcmd->pName = pCmdName;
	newcmd->Func = Func;

	iShell_InsertCmd(newcmd);//�������������˽ڵ�

	return ;
}


/********************************************************************
	* @author   ��ô��
	* @brief    vShell_CmdList 
	*           ��ʾ����ע���˵�����
	* @param    pcStr       �������������
	* @return   NULL
*/
void vShell_CmdList(void * arg)
{
	shellcmd_t * CmdNode;
	struct avl_node *node ;
	
	for (node = avl_first(&shell_avltree_root); node; node = avl_next(node))//���������
	{
		CmdNode = avl_entry(node,struct shell_cmd, cmd_node);
		printk("\r\n\t%s", CmdNode->pName);
	}
	
	printk("\r\n%s",shell_input_sign);
}


/********************************************************************
	* @brief vShell_CleanScreen ����̨����
	* @param void
	* @return NULL
*/
void vShell_CleanScreen(void * arg)
{
	printk("\033[2J\033[%d;%dH",0,0);
}



void vShell_DebugFrame(void * arg)
{
	default_puts = current_puts;
}



/**
	* @author   ��ô��
	* @brief    vShell_Init 
	*           shell ��ʼ��
	* @param    sign : shell �����־���� shell >
	* @param    puts : shell Ĭ���������Ӵ��������
	* @return   NULL
*/
void vShell_Init(char * sign,fnFmtOutDef puts)
{
//	shell_input_sign = sign;
	sprintf(shell_input_sign,sign);
	print_CurrentOut(puts);
	print_DefaultOut(puts);
	
	//ע���������
	vShell_RegisterCommand("cmd-list",vShell_CmdList);
	vShell_RegisterCommand("clear"   ,vShell_CleanScreen);
	vShell_RegisterCommand("debug-info",vShell_DebugFrame);
}



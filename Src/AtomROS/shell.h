
#ifndef __SHELL_H__
#define __SHELL_H__

// ����Ϊ shell �������Ļ�����
//#include "rbtree.h" //���������ú�������в���ƥ��
#include "avltree.h"//����������avl�����в���ƥ��
#include "ustdio.h"


#define     DEFAULT_INPUTSIGN       "shell>"

#define     KEYCODE_CTRL_C            0x03
#define     KEYCODE_NEWLINE           0x0A
#define     KEYCODE_ENTER             0x0D   //���̵Ļس���
#define     KEYCODE_BACKSPACE         0x08   //���̵Ļ��˼�
#define     KEYCODE_ESC               0x1b
#define     KEYCODE_TAB               '\t'   //���̵�tab��



enum INPUT_PARAMETER
{
	PARAMETER_EMPTY,
	PARAMETER_CORRECT,
	PARAMETER_HELP,
	PARAMETER_ERROR
};



/*
-----------------------------------------------------------------------
	���ú� vShell_RegisterCommand(pstr,pfunc) ע������
	ע��һ������ŵ�ͬʱ���½�һ���������Ӧ�Ŀ��ƿ�
	�� shell ע��ĺ�������ͳһΪ void(*CmdFuncDef)(void * arg);
	arg Ϊ����̨��������������Ĳ�������
-----------------------------------------------------------------------
*/
#define   vShell_RegisterCommand(pstr,pfunc)\
	do{\
		static struct shell_cmd st##pfunc = {0};\
		_Shell_RegisterCommand(pstr,pfunc,&st##pfunc);\
	}while(0)


#define COMMANDLINE_MAX_LEN    36  //������ϲ������ַ����������¼����
#define COMMANDLINE_MAX_RECORD 4      //����̨��¼��Ŀ��


//#define vShell_InitPrint(fn) do{print_CurrentOut(fn);print_DefaultOut(fn);}while(0)

#define iShell_CmdLen(pCommand)  (((pCommand)->ID >> 21) & 0x001F)



typedef void (*cmd_fn_def)(void * arg);


typedef struct shell_cmd
{
	uint32_t	  ID;	 //�����ʶ��
	char *		  pName; //��¼ÿ�������ַ������ڴ��ַ
	cmd_fn_def	  Func;  //��¼�����ָ��
	//struct rb_node cmd_node;//������ڵ�
	struct avl_node cmd_node;//avl���ڵ�
}
shellcmd_t;


typedef struct shell_buf 
{
  fnFmtOutDef puts;
  char   * bufmem;
  uint32_t index;
}
shellbuf_t;

#define vShell_InitBuf(pStShellBuf,shellputs) \
	do{\
		static char bufmem[COMMANDLINE_MAX_LEN] = {0};\
		(pStShellBuf)->bufmem = bufmem;    \
		(pStShellBuf)->index  = 0;         \
		(pStShellBuf)->puts = shellputs;   \
	}while(0)


//extern char * shell_input_sign;
extern char  shell_input_sign[];
extern struct avl_root shell_avltree_root;
	
void _Shell_RegisterCommand(char * cmdname, cmd_fn_def func,struct shell_cmd * newcmd);//ע������

void vShell_Input(struct shell_buf * pStShellbuf,char * ptr,uint8_t len);

int  iShell_ParseParam  (char * argStr,int * argc,int argv[]);

void vShell_Init(char * sign,fnFmtOutDef default_print);
	
void vShell_InputSign(char * sign);

#endif

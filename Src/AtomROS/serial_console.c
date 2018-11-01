/**
  ******************************************************************************
  * @file           serial_console.c
  * @author         ����տ
  * @brief          serial_console file
                    ���ڿ���̨�ļ����ļ���ֱ�Ӳ���Ӳ�������� serial_hal
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

#include "AtomROS.h"

#include "shell.h"
#include "serial_hal.h"
#include "serial_console.h"

#include "stm32f429xx.h" //for SCB->VTOR

//--------------------��غ궨�弰�ṹ�嶨��--------------------


#define UASRT_IAP_BUF_SIZE  1024

const static char acIAPtip[]=
"\r\n\
 ____   ___   ____\r\n\
|_  _| / _ \\ |  _ \\\r\n\
 _||_ |  _  ||  __/don't press any key now\r\n\
|____||_| |_||_|   ";



static struct st_console_iap
{
	uint32_t iFlashAddr;
	uint32_t iTimeOut;
}
stUsartIAP;

ros_task_t stSerialConsoleTask;
ros_task_t stUsartIapTask;
ros_task_t stUsartIapTimeoutTask;
ros_semaphore_t rosSerialRxSem;



struct shell_buf stUsartShellBuf;
//------------------------------��غ�������------------------------------




//------------------------------�����ķָ���------------------------------



/**
	* @brief task_UsartIapCompletePro iap ������ʱ�ж�����
	* @param void
	* @return NULL
*/
int task_UsartIapCompletePro(void * arg)
{
	TASK_BEGIN();//����ʼ
	
	printk("loading");
	
	task_cond_wait(OS_current_time - stUsartIAP.iTimeOut > 2000) ;//��ʱ 2.5 s
	
	task_cancel(&stUsartIapTask); // ɾ�� iap ����
	
	vUsartHal_LockFlash();   //����Ҫд�����һ�����ݲ��������������������� task_UsartIapCompletePro ��
	
	vUsartHal_RxPktMaxLen(COMMANDLINE_MAX_LEN);
	
	uint32_t filesize = (SCB->VTOR == FLASH_BASE) ? (stUsartIAP.iFlashAddr-APP_ADDR):(stUsartIAP.iFlashAddr-IAP_ADDR);
	printk("\r\nupdate completed!\r\nupdate package size:%d byte\r\n",filesize);

	TASK_END();
}


/** 
	* @brief task_UsartIapPro  iap ��������
	* @param void
	* @return NULL
*/
int task_UsartIapPro(void * arg)
{
	uint16_t pktsize;
	char   * pktdata;

	uint32_t * piData;
	
	TASK_BEGIN();//����ʼ
	
	vUsartHal_UnlockFlash();//����Ҫд�����һ�����ݲ��������������������� task_UsartIapCompletePro ��
	
	if (SCB->VTOR == FLASH_BASE)//����� iap ģʽ������ app ����
	{
		stUsartIAP.iFlashAddr = APP_ADDR;
		if(iUsartHal_IAP_Erase(5)) //app ��ַ�� 0x8020000,ɾ������5����
		{
			Error_Here();//����������	
			task_exit();
		}
	}	
 	else
 	{
		stUsartIAP.iFlashAddr = IAP_ADDR;
		if(iUsartHal_IAP_Erase(0)) //iap ��ַ�� 0x8000000,ɾ������0����
		{
			Error_Here();//����������	
			task_exit();
		}
		
		if(iUsartHal_IAP_Erase(1)) //����1����
		{
			Error_Here();//����������	
			task_exit();
		}
	
		if(iUsartHal_IAP_Erase(2)) //����2����
		{
			Error_Here();//����������	
			task_exit();
		}	
 	}
	
	color_printk(light_green,"\033[2J\033[%d;%dH%s",0,0,acIAPtip);//����
	
	while(1)
	{
		//task_cond_wait(iUsartHal_RxPktOut(&pktdata,&pktsize));
		task_semaphore_wait(&rosSerialRxSem);//�ȴ����յ�һ������
		
		while (iUsartHal_RxPktOut(&pktdata,&pktsize))
		{
			piData = (uint32_t*)pktdata;
			
			for (uint32_t iCnt = 0;iCnt < pktsize ; iCnt += 4) // f4 ������ word д��
			{
				vUsartHal_IAP_Write(stUsartIAP.iFlashAddr,*piData);
				stUsartIAP.iFlashAddr += 4;
				++piData;
			}
			
			stUsartIAP.iTimeOut = OS_current_time;//����ʱ���
			
			if (task_is_exited(&stUsartIapTimeoutTask))
				task_create(&stUsartIapTimeoutTask,NULL,task_UsartIapCompletePro,NULL);
			else
				printk(".");
		}
	}
	
	TASK_END();
}




/** 
	* @brief task_SerialConsole  ���ڿ���̨����
	* @param void
	* @return NULL
*/
int task_SerialConsole(void * arg)
{
	uint16_t ucLen ;
	char * HalPkt;

	TASK_BEGIN();//����ʼ

	task_semaphore_init(&rosSerialRxSem);
	
	while(1)
	{
		task_semaphore_wait(&rosSerialRxSem);
		//task_cond_wait(iUsartHal_RxPktOut(&HalPkt,&ucLen));

		while (iUsartHal_RxPktOut(&HalPkt,&ucLen))
			vShell_Input(&stUsartShellBuf,HalPkt,ucLen);//����֡����Ӧ�ò�
		
		task_join(&stUsartIapTask); //��������ʱ�������� iap ������
	}
	
	TASK_END();
}




/** 
	* @brief vShell_UpdateCmd  iap ��������
	* @param void
	* @return NULL
*/
void vShell_UpdateCmd(void * arg)
{
	task_create(&stUsartIapTask,NULL,task_UsartIapPro,NULL);
	vUsartHal_RxPktMaxLen(UASRT_IAP_BUF_SIZE);
}





void vSerialConsole_Init(char * info)
{
	vUsartHal_Init(); //�ȳ�ʼ��Ӳ����
	
	vShell_InitBuf(&stUsartShellBuf,vUsartHal_Output);


	if (SCB->VTOR != FLASH_BASE)
	{
		vShell_RegisterCommand("update-iap",vShell_UpdateCmd);	
	}
	else
	{
		vShell_RegisterCommand("update-app",vShell_UpdateCmd);	
		vShell_RegisterCommand("jump-app",vShell_JumpCmd);
	}

	vShell_RegisterCommand("reboot"  ,vShell_RebootSystem);
	
	task_create(&stSerialConsoleTask,NULL,task_SerialConsole,NULL);
	
	color_printk(purple,"%s",info);//��ӡ������Ϣ���߿���̨��Ϣ
	
	while(iUsartHal_TxBusy()); //�ȴ���ӡ����
}







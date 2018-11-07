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
#include "iap_hal.h"

#include "shell.h"
#include "serial_hal.h"
#include "serial_console.h"

#include "stm32f429xx.h" //for SCB->VTOR

//--------------------��غ궨�弰�ṹ�嶨��--------------------


#define UASRT_IAP_BUF_SIZE  1024

const static char iap_logo[]=
"\r\n\
 ____   ___   ____\r\n\
|_  _| / _ \\ |  _ \\\r\n\
 _||_ |  _  ||  __/don't press any key now\r\n\
|____||_| |_||_|   ";



static struct st_console_iap
{
	uint32_t flashaddr;
	uint32_t timestamp;
}
serial_iap;

ros_task_t stSerialConsoleTask;
ros_task_t stUsartIapTask;
ros_task_t stUsartIapTimeoutTask;
ros_semaphore_t rosSerialRxSem;



static struct shell_buf serial_shellbuf;
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
	
	task_cond_wait(OS_current_time - serial_iap.timestamp > 2000) ;//��ʱ 2.5 s
	
	task_cancel(&stUsartIapTask); // ɾ�� iap ����
	
	iap_lock_flash();   //����Ҫд�����һ�����ݲ��������������������� task_UsartIapCompletePro ��
	
	serial_rxpkt_max_len(COMMANDLINE_MAX_LEN);
	
	uint32_t filesize = (SCB->VTOR == FLASH_BASE) ? (serial_iap.flashaddr-APP_ADDR):(serial_iap.flashaddr-IAP_ADDR);
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

	uint32_t * value;
	
	TASK_BEGIN();//����ʼ
	
	iap_unlock_flash();//����Ҫд�����һ�����ݲ��������������������� task_UsartIapCompletePro ��
	
	if (SCB->VTOR == FLASH_BASE)//����� iap ģʽ������ app ����
	{
		serial_iap.flashaddr = APP_ADDR;
		if(iap_erase_flash(5)) //app ��ַ�� 0x8020000,ɾ������5����
		{
			Error_Here();//����������	
			task_exit();
		}
	}	
 	else
 	{
		serial_iap.flashaddr = IAP_ADDR; //iap ��ַ�� 0x8000000,ɾ������0����
		for (uint32_t  sector = 0 ; sector < 3 ; ++sector)
		{
			if(iap_erase_flash(sector))
			{
				Error_Here();//����������	
				task_exit();
			}
		}
 	}
	
	color_printk(light_green,"\033[2J\033[%d;%dH%s",0,0,iap_logo);//����
	
	while(1)
	{
		//task_cond_wait(iUsartHal_RxPktOut(&pktdata,&pktsize));
		task_semaphore_wait(&rosSerialRxSem);//�ȴ����յ�һ������
		
		while (serial_rxpkt_queue_out(&pktdata,&pktsize))
		{
			value = (uint32_t*)pktdata;
			
			for (uint32_t i = 0;i < pktsize ; i += 4) // f4 ������ word д��
			{
				iap_write_flash(serial_iap.flashaddr,*value);
				serial_iap.flashaddr += 4;
				++value;
			}
			
			serial_iap.timestamp = OS_current_time;//����ʱ���
			
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
	char  *  packet;
	uint16_t pktlen ;

	TASK_BEGIN();//����ʼ

	task_semaphore_init(&rosSerialRxSem);
	
	while(1)
	{
		task_semaphore_wait(&rosSerialRxSem);
		//task_cond_wait(iUsartHal_RxPktOut(&packet,&pktlen));

		while (serial_rxpkt_queue_out(&packet,&pktlen))
			shell_input(&serial_shellbuf,packet,pktlen);//����֡����Ӧ�ò�
		
		task_join(&stUsartIapTask); //��������ʱ�������� iap ������
	}
	
	TASK_END();
}




/** 
	* @brief shell_iap_command  iap ��������
	* @param void
	* @return NULL
*/
void shell_iap_command(void * arg)
{
	task_create(&stUsartIapTask,NULL,task_UsartIapPro,NULL);
	serial_rxpkt_max_len(UASRT_IAP_BUF_SIZE);
}





void serial_console_init(char * info)
{
	hal_serial_init(); //�ȳ�ʼ��Ӳ����
	
	SHELL_MALLOC(&serial_shellbuf,serial_puts);

	if (SCB->VTOR != FLASH_BASE)
	{
		shell_register_command("update-iap",shell_iap_command);	
	}
	else
	{
		shell_register_command("update-app",shell_iap_command);	
		shell_register_command("jump-app",shell_jump_command);
	}

	shell_register_command("reboot"  ,shell_reboot_command);
	
	task_create(&stSerialConsoleTask,NULL,task_SerialConsole,NULL);
	
	color_printk(purple,"%s",info);//��ӡ������Ϣ���߿���̨��Ϣ
	
	while(serial_busy()); //�ȴ���ӡ����
}







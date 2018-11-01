/**
  ******************************************************************************
  * @file           ustdio.c
  * @author         ����տ
  * @brief          �Ǳ�׼����ӡ���
  ******************************************************************************
  *
  * COPYRIGHT(c) 2018 GoodMorning
  *
  ******************************************************************************
  */
/* Includes ---------------------------------------------------*/
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>

#include "ustdio.h"



fnFmtOutDef current_puts = NULL;
fnFmtOutDef default_puts = NULL;


const char none        []= "\033[0m";  
const char black       []= "\033[0;30m";  
const char dark_gray   []= "\033[1;30m";  
const char blue        []= "\033[0;34m";  
const char light_blue  []= "\033[1;34m";  
const char green       []= "\033[0;32m";  
const char light_green []= "\033[1;32m";  
const char cyan        []= "\033[0;36m";  
const char light_cyan  []= "\033[1;36m";  
const char red         []= "\033[0;31m";  
const char light_red   []= "\033[1;31m";  
const char purple      []= "\033[0;35m";  
const char light_purple[]= "\033[1;35m";  
const char brown       []= "\033[0;33m";  
const char yellow      []= "\033[1;33m";  
const char light_gray  []= "\033[0;37m";  
const char white       []= "\033[1;37m"; 

char * default_color = (char *)none;



/**
	* @author   ��ô��
	* @brief    �ض��� printf ������������˵ printf �����ǱȽ����ģ�
	*           ��Ϊ printf Ҫ������ĸ�ʽ�жϣ�����ĸ�ʽ����һЩ��
	*           ����Ϊ��Ч�ʣ��ں���д�� printk ������
	* @return   NULL
*/
#if 1
#pragma import(__use_no_semihosting)             
//��׼����Ҫ��֧�ֺ���                 
struct __FILE 
{ 
	int handle; 
}; 

FILE __stdout;       
//����_sys_exit()�Ա���ʹ�ð�����ģʽ    
void _sys_exit(int x) 
{ 
	x = x; 
}

//�ض���fputc���� 
int fputc(int ch, FILE *f)
{
	char  cChar = (char)ch;
	if (current_puts)
		current_puts(&cChar,1);
	return ch;
}
#endif 




/**
	* @author   ��ô��
	* @brief    i_itoa 
	*           ����תʮ�����ַ���
	* @param    pcBuf   ת�ַ��������ڴ�
	* @param    iValue  ֵ
	* @return   ת�������ַ�������
*/	
int i_itoa(char * pcBuf,int iValue)		
{
	int iLen = 0;
	int iVal = (iValue<0)?(0-iValue) : iValue; 
	
	do
	{
		pcBuf[iLen++] = (char)(iVal % 10 + '0'); 
		iVal = iVal/10;
	}
	while(iVal);
	
	if (iValue < 0) 
		pcBuf[iLen++] = '-'; 
	
	for (uint8_t index = 1 ; index <= iLen/2; ++index)
	{
		char reverse = pcBuf[iLen  - index];  
		pcBuf[iLen - index] = pcBuf[index -1];   
		pcBuf[index - 1] = reverse; 
	}
	
	return iLen;
}




/**
	* @author   ��ô��
	* @brief    i_ftoa 
	*           ������ת�ַ���������4λС��
	* @param    pcBuf   ת�ַ��������ڴ�
	* @param    fValue  ֵ
	* @return   �ַ�������
*/
int i_ftoa(char * pcBuf,float fValue)		
{
	int iLen = 0;
	float fVal = (fValue < 0.0f )? (0.0f - fValue) : fValue; 
	int iIntVal   = (int)fVal;  
	int iFloatVal =  (int)(fVal * 10000) - iIntVal * 10000;

	for(uint32_t cnt = 0 ; cnt < 4 ; ++ cnt)
	{		
		pcBuf[iLen++] = (char)(iFloatVal % 10 + '0');
		iFloatVal = iFloatVal / 10;
	}
	
	pcBuf[iLen++] = '.';  

	do
	{
		pcBuf[iLen++] = (char)(iIntVal % 10 + '0'); 
		iIntVal = iIntVal/10;
	}
	while(iIntVal);            
	
	
	if (fValue < 0.0f) 
		pcBuf[iLen++] = '-'; 
	
	for (uint8_t index = 1 ; index <= iLen/2; ++index)
	{
		char reverse = pcBuf[iLen  - index];  
		pcBuf[iLen - index] = pcBuf[index -1];   
		pcBuf[index - 1] = reverse; 
	}
	
	return iLen;
}


/**
	* @author   ��ô��
	* @brief    i_itoa 
	*           ����תʮ�������ַ���
	* @param    pcBuf   ת�ַ��������ڴ�
	* @param    iValue  ֵ
	* @return   ת�������ַ�������
*/	
int i_xtoa(char * strbuf,uint32_t iValue)		
{
	int iLen = 0;
	
	do{
		char cChar = (char)((iValue & 0x0f) + '0');
		strbuf[iLen++] = (cChar > '9') ? (cChar + 7) : (cChar);
		iValue >>= 4;
	}
	while(iValue);
	
	for (uint8_t index = 1 ; index <= iLen/2; ++index)
	{
		char reverse = strbuf[iLen  - index];  
		strbuf[iLen - index] = strbuf[index -1];   
		strbuf[index - 1] = reverse; 
	}
	
	return iLen;
}



/**
	* @author   ��ô��
	* @brief    printk 
	*           ��ʽ������������� sprintf �� printf ,������
	*           �ñ�׼��� sprintf �� printf �ķ���̫���ˣ������Լ�д��һ�����ص�Ҫ��
	* @param    fmt     Ҫ��ʽ������Ϣ�ַ���ָ��
	* @param    ...     ������
	* @return   void
*/
void printk(char* fmt, ...)
{
	char * pcInput = fmt;
	char * pcOutput = fmt;

	if (NULL == current_puts) return ;
	
	va_list ap; 
	va_start(ap, fmt);

	while (*pcOutput) //��Ҫ��ֹ���ͻ������
	{
		if ('%' == *pcOutput) //������ʽ����־,Ϊ��Ч�ʽ�֧�� %d ,%f ,%s %x ,%c 
		{
			char  buf[64] = { 0 };//������תΪ�ַ����Ļ�����
			char *pStrbuf = buf;  //������תΪ�ַ����Ļ�����
			int   iStrlen = 0;   //����ת������
			
			if (pcOutput != pcInput)//�� % ǰ��Ĳ������
				current_puts(pcInput,pcOutput - pcInput);
	
			pcInput = pcOutput++;
			switch (*pcOutput++) // �������� ++, output ��Խ�� %? 
			{
				case 'd':
					iStrlen = i_itoa(pStrbuf,va_arg(ap, int));
					break;

				case 'f':
					iStrlen = i_ftoa(pStrbuf,(float)va_arg(ap, double));
					break;

				case 'x':
					iStrlen = i_xtoa(pStrbuf,va_arg(ap, int));
					break;
					
				case 'c' :
					pStrbuf[iStrlen++] = (char)va_arg(ap, int);
					break;
				
				case 's':
					pStrbuf = va_arg(ap, char*);
					iStrlen = strlen(pStrbuf);
					break;

				default:continue;
			}
			
			pcInput = pcOutput;
			current_puts(pStrbuf,iStrlen);
		}
		else
		{
			++pcOutput;
		}
	}

	va_end(ap);
	
	if (pcOutput != pcInput) 
		current_puts(pcInput,pcOutput - pcInput);
}




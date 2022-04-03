#include "stm32f10x.h"
#include "sys.h"
#include "delay.h"
#include "usart.h"
#include "led.h"
#include "bh1750.h"
#include "string.h"

//ESP8266WIFIʹ�����ͷ�ļ�
#include "uart2.h"
#include "wifi.h"
#include "timer3.h"
#include "structure.h"

//FreeRTOSϵͳ���ͷ�ļ�
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

//MQTTЭ�����ͷ�ļ�
#include "esp8266_mqtt.h"

//MQTT��ʼ������
void ES8266_MQTT_Init(void);


//�˴��ǰ����Ʒ������ĵ�½����
#define MQTT_BROKERADDRESS "a1VPyJEJRjJ.iot-as-mqtt.cn-shanghai.aliyuncs.com"
#define MQTT_CLIENTID "a1VPyJEJRjJ.test01|securemode=2,signmethod=hmacsha256,timestamp=1648914857644|"
#define MQTT_USARNAME "test01&a1VPyJEJRjJ"
#define MQTT_PASSWD "76599f069aff42f644669310490cdc3a9830c5431da41a81ff1467722d2bdb1b"
#define	MQTT_PUBLISH_TOPIC "/sys/a1VPyJEJRjJ/test01/thing/event/property/post"
#define MQTT_SUBSCRIBE_TOPIC "/sys/a1VPyJEJRjJ/test01/thing/service/property/set"


char mqtt_message[300];	//MQTT���ϱ���Ϣ����

//������IP��ַ�Ͷ˿ں�
char *IP = MQTT_BROKERADDRESS;
int Port = 1883;


//�������ȼ�
#define START_TASK_PRIO		1
//�����ջ��С	
#define START_STK_SIZE 		128  
//������
TaskHandle_t StartTask_Handler;
//������
void start_task(void *pvParameters);

//�������ȼ�
#define LED0_TASK_PRIO		2
//�����ջ��С	
#define LED0_STK_SIZE 		50  
//������
TaskHandle_t LED0Task_Handler;
//������
void led0_task(void *pvParameters);

//�������ȼ�
#define WIFI_TASK_PRIO		3
//�����ջ��С	
#define WIFI_STK_SIZE 		512  
//������
TaskHandle_t WIFITask_Handler;
//������
void wifi_task(void *pvParameters);


/* Uart2 - Wifi ����Ϣ���ն��� */
#define Wifi_MESSAGE_Q_NUM   4   		//�������ݵ���Ϣ���е�����
QueueHandle_t Wifi_Message_Queue;		//��Ϣ���о��

float light ;   //����ֵ
 
//������
int main(void)
{	
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_4);//����ϵͳ�ж����ȼ�����4 
    delay_init();               //��ʼ��ϵͳʱ��
	LED_Init();                 //LED��ʼ��
    uart_init(115200);     	    //��ʼ������1
    uart2_init(115200);         //��ʼ������2
    Timer3_Configuration(5);    //Tim3��ʱ��������wifi-uart2�Ľ������
    WiFi_ResetIO_Init();		//wifi - RST���ų�ʼ��
	
    printf("��ʼ����ɣ���ʼ��������\r\n");
     
    //������ʼ����
    xTaskCreate((TaskFunction_t )start_task,            //������
                (const char*    )"start_task",          //��������
                (uint16_t       )START_STK_SIZE,        //�����ջ��С
                (void*          )NULL,                  //���ݸ��������Ĳ���
                (UBaseType_t    )START_TASK_PRIO,       //�������ȼ�
                (TaskHandle_t*  )&StartTask_Handler);   //������              
    vTaskStartScheduler();          //�����������
}
 

 //��ʼ����������
void start_task(void *pvParameters)
{
    taskENTER_CRITICAL();           //�����ٽ���
    
    //���� Uart2 - Wifi ������Ϣ����
    Wifi_Message_Queue = xQueueCreate(Wifi_MESSAGE_Q_NUM,1); //��������Ŀ��Wifi_MESSAGE_Q_NUM����������Ǵ���DMA���ջ���������
    
    //����LED0����
    xTaskCreate((TaskFunction_t )led0_task,     	
                (const char*    )"led0_task",   	
                (uint16_t       )LED0_STK_SIZE, 
                (void*          )NULL,				
                (UBaseType_t    )LED0_TASK_PRIO,	
                (TaskHandle_t*  )&LED0Task_Handler); 

    //����wifi_task����
    xTaskCreate((TaskFunction_t )wifi_task,     	
                (const char*    )"wifi_task",   	
                (uint16_t       )WIFI_STK_SIZE, 
                (void*          )NULL,				
                (UBaseType_t    )WIFI_TASK_PRIO,	
                (TaskHandle_t*  )&WIFITask_Handler);   
                
    vTaskDelete(StartTask_Handler); //ɾ����ʼ����
    taskEXIT_CRITICAL();            //�˳��ٽ���
}

//LED0������ 
void led0_task(void *pvParameters)
{
	//���մ�������ʼ��
    BH1750_Init();
    while(1)
    {
        //printf("led0_task !!!\r\n");
		/* �ɼ����� */
		light = LIght_Intensity();	//��ȡ����ǿ�ȵ�ֵ
        vTaskDelay(500);
    }
}   
 

//WIFI������ 
void wifi_task(void *pvParameters)
{
	uint8_t pub_cnt = 0,pub_ret;
	uint16_t Counter_MQTT_Heart = 0;
    char *recv;
	
    //MQTTЭ���ʼ��
    ES8266_MQTT_Init();

    while(1)
    {
        //����������
		if(Counter_MQTT_Heart++>300)
		{
			Counter_MQTT_Heart = 0;
			MQTT_SentHeart();
		}

		/* �������� */
		 pub_cnt++;
		if(0 == pub_cnt%500) //Լ3S����һ������
		{
			pub_cnt = 0;
			memset(mqtt_message, 0, 300);
			//��װ����  
			sprintf(mqtt_message,
			"{\"method\":\"thing.service.property.post\",\"id\":\"1234\",\"params\":{\
			\"Light\":%.1f},\"version\":\"1.0.0\"}", light);
			 //��������
			pub_ret = MQTT_PublishData(MQTT_PUBLISH_TOPIC,mqtt_message,0);
			if(pub_ret > 0)
			{
				printf("��Ϣ�����ɹ�������data=%.1f\r\n", light);
			}
			else
			{
				printf("��Ϣ����ʧ�ܣ�����pub_ret=%d\r\n", pub_ret);
			}
		}
        //�յ�����
        if((WifiMsg.U2_RxCompleted == 1) && (Usart2_RxCounter > 1))
        {
            printf("���Է��������ݣ�%d\r\n", Usart2_RxCounter);
			recv = strstr(Usart2_RxBuff, "LED"); 
            //�·�����󣬴���2����յ����������ݣ�
			//...{"method":"thing.service.property.set","id":"1593428732","params":{"LED":1},"version":"1.0.0"}			
            if(recv != NULL)
            {	
				//����strstr������recvָ�����ַ�����LED":0}...
				//Ϊ�õ�LED�����״ֵ̬��ָ��ƫ��5���ֽ�
				recv = recv + 3 +2;  //LEDռ3���ֽ�  ��:ռ2���ֽ�
                printf("LED=%d\r\n", (*recv)-'0');
                LED0 = !((*recv)-'0');  //�����·����������PC13����LED��
            
                memset(mqtt_message, 0, 300);
                //��װ����  id 1454479553
                sprintf(mqtt_message,
                "{\"method\":\"thing.service.property.set\",\"id\":\"5678\",\"params\":{\
                \"LED\":%d},\"version\":\"1.0.0\"}", (*recv)-'0');
                
                //��������
                pub_ret = MQTT_PublishData(MQTT_PUBLISH_TOPIC,mqtt_message,0);
                if(pub_ret > 0)
                {
                    printf("��Ϣ�����ɹ�������pub_ret=%d\r\n", pub_ret);
                }
                else
                {
                    printf("��Ϣ����ʧ�ܣ�����pub_ret=%d\r\n", pub_ret);
                }
            }
            //����־λ���������
            memset(Usart2_RxBuff, 0, sizeof(Usart2_RxBuff));
            WifiMsg.U2_RxCompleted = 0;
            Usart2_RxCounter = 0;
        } 
        vTaskDelay(10);
    }
} 


//MQTT��ʼ������
void ES8266_MQTT_Init(void)
{
	uint8_t status=1;
    char conn=1;

	// ��λ���ɹ�����Ҫ���¸�λ
//    if(!WiFi_Init())
//    {
//        printf("ESP8266״̬��ʼ������\r\n");		//���������Ϣ
//        //��ȡWIFI��ǰIP��ַ
//        WiFi_GetIP(100);
//        WifiMsg.Mode = 1;							//r_flag��־��λ����ʾ8266״̬���������Լ���������TCP���� 
//        status++;
//    }
  
    printf("׼����λģ��\r\n");                     //������ʾ����
	if(WiFi_Reset(50))
	{                                //��λ��100ms��ʱ��λ���ܼ�5s��ʱʱ��
		printf("��λʧ�ܣ�׼������\r\n");           //���ط�0ֵ������if��������ʾ����
	}else printf("��λ�ɹ�\r\n");                   //������ʾ����
    
    printf("׼������·����\r\n");                   //������ʾ����	
   
	if(WiFi_JoinAP(10)){                               //����·����,1s��ʱ��λ���ܼ�10s��ʱʱ��
        printf("����·����ʧ�ܣ�׼������\r\n");     //���ط�0ֵ������if��������ʾ����
    }else printf("����·�����ɹ�\r\n");             //������ʾ����
    	printf("׼����ȡIP��ַ\r\n");                   //������ʾ����
	if(WiFi_GetIP(50)){                                //׼����ȡIP��ַ��100ms��ʱ��λ���ܼ�5s��ʱʱ��
		printf("��ȡIP��ַʧ�ܣ�׼������\r\n");     //���ط�0ֵ������if��������ʾ����
	}else printf("��ȡIP��ַ�ɹ�\r\n");             //������ʾ����
	
	printf("׼������͸��\r\n");                     //������ʾ����
	if(WiFi_SendCmd("AT+CIPMODE=1",50)){               //����͸����100ms��ʱ��λ���ܼ�5s��ʱʱ��
		printf("����͸��ʧ�ܣ�׼������\r\n");       //���ط�0ֵ������if��������ʾ����
	}else printf("����͸���ɹ�\r\n");               //������ʾ����
	
	printf("׼���رն�·����\r\n");                 //������ʾ����
	if(WiFi_SendCmd("AT+CIPMUX=0",50)){                //�رն�·���ӣ�100ms��ʱ��λ���ܼ�5s��ʱʱ��
		printf("�رն�·����ʧ�ܣ�׼������\r\n");   //���ط�0ֵ������if��������ʾ����
	}else printf("�رն�·���ӳɹ�\r\n");           //������ʾ����
    WifiMsg.Mode = 1;							//r_flag��־��λ����ʾ8266״̬���������Լ���������TCP���� 
    status++;
	
	//���Ӱ�����IOT������
	if(status==2)
	{
        printf("���ӷ�������IP=%s,Port=%d\r\n",IP, Port);
        conn = WiFi_Connect(IP, Port, 100);
        printf("���ӽ��conn=%d\r\n",conn);
        status++;
	}
    //�ر�WIFI����
    //printf("�رջ��ԣ�%d\r\n", WiFi_Send("ATE0"));
	
	//��½MQTT
	if(status==3)
	{
		if(MQTT_Connect(MQTT_CLIENTID, MQTT_USARNAME, MQTT_PASSWD) != 0)
		{
			printf("ESP8266������MQTT��½�ɹ���\r\n");
			status++;
		}
		else
        {
            printf("ESP8266������MQTT��½ʧ�ܣ�\r\n");
            status++;
        }
	}

	//��������
	if(status==4)
	{
		if(MQTT_SubscribeTopic(MQTT_SUBSCRIBE_TOPIC,0,1) != 0)
		{
			printf("ESP8266������MQTT��������ɹ���\r\n");
		}
		else
        {
			printf("ESP8266������MQTT��������ʧ�ܣ�\r\n");
        }
	}
}

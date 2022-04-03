#include "stm32f10x.h"
#include "sys.h"
#include "delay.h"
#include "usart.h"
#include "led.h"
#include "bh1750.h"
#include "string.h"

//ESP8266WIFI使用相关头文件
#include "uart2.h"
#include "wifi.h"
#include "timer3.h"
#include "structure.h"

//FreeRTOS系统相关头文件
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

//MQTT协议相关头文件
#include "esp8266_mqtt.h"

//MQTT初始化函数
void ES8266_MQTT_Init(void);


//此处是阿里云服务器的登陆配置
#define MQTT_BROKERADDRESS "a1VPyJEJRjJ.iot-as-mqtt.cn-shanghai.aliyuncs.com"
#define MQTT_CLIENTID "a1VPyJEJRjJ.test01|securemode=2,signmethod=hmacsha256,timestamp=1648914857644|"
#define MQTT_USARNAME "test01&a1VPyJEJRjJ"
#define MQTT_PASSWD "76599f069aff42f644669310490cdc3a9830c5431da41a81ff1467722d2bdb1b"
#define	MQTT_PUBLISH_TOPIC "/sys/a1VPyJEJRjJ/test01/thing/event/property/post"
#define MQTT_SUBSCRIBE_TOPIC "/sys/a1VPyJEJRjJ/test01/thing/service/property/set"


char mqtt_message[300];	//MQTT的上报消息缓存

//服务器IP地址和端口号
char *IP = MQTT_BROKERADDRESS;
int Port = 1883;


//任务优先级
#define START_TASK_PRIO		1
//任务堆栈大小	
#define START_STK_SIZE 		128  
//任务句柄
TaskHandle_t StartTask_Handler;
//任务函数
void start_task(void *pvParameters);

//任务优先级
#define LED0_TASK_PRIO		2
//任务堆栈大小	
#define LED0_STK_SIZE 		50  
//任务句柄
TaskHandle_t LED0Task_Handler;
//任务函数
void led0_task(void *pvParameters);

//任务优先级
#define WIFI_TASK_PRIO		3
//任务堆栈大小	
#define WIFI_STK_SIZE 		512  
//任务句柄
TaskHandle_t WIFITask_Handler;
//任务函数
void wifi_task(void *pvParameters);


/* Uart2 - Wifi 的消息接收队列 */
#define Wifi_MESSAGE_Q_NUM   4   		//接收数据的消息队列的数量
QueueHandle_t Wifi_Message_Queue;		//信息队列句柄

float light ;   //光照值
 
//主函数
int main(void)
{	
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_4);//设置系统中断优先级分组4 
    delay_init();               //初始化系统时钟
	LED_Init();                 //LED初始化
    uart_init(115200);     	    //初始化串口1
    uart2_init(115200);         //初始化串口2
    Timer3_Configuration(5);    //Tim3定时器，用于wifi-uart2的接收完成
    WiFi_ResetIO_Init();		//wifi - RST引脚初始化
	
    printf("初始化完成，开始创建任务\r\n");
     
    //创建开始任务
    xTaskCreate((TaskFunction_t )start_task,            //任务函数
                (const char*    )"start_task",          //任务名称
                (uint16_t       )START_STK_SIZE,        //任务堆栈大小
                (void*          )NULL,                  //传递给任务函数的参数
                (UBaseType_t    )START_TASK_PRIO,       //任务优先级
                (TaskHandle_t*  )&StartTask_Handler);   //任务句柄              
    vTaskStartScheduler();          //开启任务调度
}
 

 //开始任务任务函数
void start_task(void *pvParameters)
{
    taskENTER_CRITICAL();           //进入临界区
    
    //创建 Uart2 - Wifi 接收消息队列
    Wifi_Message_Queue = xQueueCreate(Wifi_MESSAGE_Q_NUM,1); //队列项数目是Wifi_MESSAGE_Q_NUM，队列项长度是串口DMA接收缓冲区长度
    
    //创建LED0任务
    xTaskCreate((TaskFunction_t )led0_task,     	
                (const char*    )"led0_task",   	
                (uint16_t       )LED0_STK_SIZE, 
                (void*          )NULL,				
                (UBaseType_t    )LED0_TASK_PRIO,	
                (TaskHandle_t*  )&LED0Task_Handler); 

    //创建wifi_task任务
    xTaskCreate((TaskFunction_t )wifi_task,     	
                (const char*    )"wifi_task",   	
                (uint16_t       )WIFI_STK_SIZE, 
                (void*          )NULL,				
                (UBaseType_t    )WIFI_TASK_PRIO,	
                (TaskHandle_t*  )&WIFITask_Handler);   
                
    vTaskDelete(StartTask_Handler); //删除开始任务
    taskEXIT_CRITICAL();            //退出临界区
}

//LED0任务函数 
void led0_task(void *pvParameters)
{
	//光照传感器初始化
    BH1750_Init();
    while(1)
    {
        //printf("led0_task !!!\r\n");
		/* 采集数据 */
		light = LIght_Intensity();	//读取光照强度的值
        vTaskDelay(500);
    }
}   
 

//WIFI任务函数 
void wifi_task(void *pvParameters)
{
	uint8_t pub_cnt = 0,pub_ret;
	uint16_t Counter_MQTT_Heart = 0;
    char *recv;
	
    //MQTT协议初始化
    ES8266_MQTT_Init();

    while(1)
    {
        //心跳包发送
		if(Counter_MQTT_Heart++>300)
		{
			Counter_MQTT_Heart = 0;
			MQTT_SentHeart();
		}

		/* 发送数据 */
		 pub_cnt++;
		if(0 == pub_cnt%500) //约3S发送一次数据
		{
			pub_cnt = 0;
			memset(mqtt_message, 0, 300);
			//组装数据  
			sprintf(mqtt_message,
			"{\"method\":\"thing.service.property.post\",\"id\":\"1234\",\"params\":{\
			\"Light\":%.1f},\"version\":\"1.0.0\"}", light);
			 //发布数据
			pub_ret = MQTT_PublishData(MQTT_PUBLISH_TOPIC,mqtt_message,0);
			if(pub_ret > 0)
			{
				printf("消息发布成功！！！data=%.1f\r\n", light);
			}
			else
			{
				printf("消息发布失败！！！pub_ret=%d\r\n", pub_ret);
			}
		}
        //收到数据
        if((WifiMsg.U2_RxCompleted == 1) && (Usart2_RxCounter > 1))
        {
            printf("来自服务器数据：%d\r\n", Usart2_RxCounter);
			recv = strstr(Usart2_RxBuff, "LED"); 
            //下发命令后，串口2会接收到这样的数据：
			//...{"method":"thing.service.property.set","id":"1593428732","params":{"LED":1},"version":"1.0.0"}			
            if(recv != NULL)
            {	
				//经过strstr函数后，recv指向了字符串：LED":0}...
				//为拿到LED后面的状态值，指针偏移5个字节
				recv = recv + 3 +2;  //LED占3个字节  ”:占2个字节
                printf("LED=%d\r\n", (*recv)-'0');
                LED0 = !((*recv)-'0');  //根据下发的命令控制PC13处的LED灯
            
                memset(mqtt_message, 0, 300);
                //组装数据  id 1454479553
                sprintf(mqtt_message,
                "{\"method\":\"thing.service.property.set\",\"id\":\"5678\",\"params\":{\
                \"LED\":%d},\"version\":\"1.0.0\"}", (*recv)-'0');
                
                //发布数据
                pub_ret = MQTT_PublishData(MQTT_PUBLISH_TOPIC,mqtt_message,0);
                if(pub_ret > 0)
                {
                    printf("消息发布成功！！！pub_ret=%d\r\n", pub_ret);
                }
                else
                {
                    printf("消息发布失败！！！pub_ret=%d\r\n", pub_ret);
                }
            }
            //将标志位和数据清空
            memset(Usart2_RxBuff, 0, sizeof(Usart2_RxBuff));
            WifiMsg.U2_RxCompleted = 0;
            Usart2_RxCounter = 0;
        } 
        vTaskDelay(10);
    }
} 


//MQTT初始化函数
void ES8266_MQTT_Init(void)
{
	uint8_t status=1;
    char conn=1;

	// 复位不成功，需要重新复位
//    if(!WiFi_Init())
//    {
//        printf("ESP8266状态初始化正常\r\n");		//串口输出信息
//        //获取WIFI当前IP地址
//        WiFi_GetIP(100);
//        WifiMsg.Mode = 1;							//r_flag标志置位，表示8266状态正常，可以继续，进行TCP连接 
//        status++;
//    }
  
    printf("准备复位模块\r\n");                     //串口提示数据
	if(WiFi_Reset(50))
	{                                //复位，100ms超时单位，总计5s超时时间
		printf("复位失败，准备重启\r\n");           //返回非0值，进入if，串口提示数据
	}else printf("复位成功\r\n");                   //串口提示数据
    
    printf("准备连接路由器\r\n");                   //串口提示数据	
   
	if(WiFi_JoinAP(10)){                               //连接路由器,1s超时单位，总计10s超时时间
        printf("连接路由器失败，准备重启\r\n");     //返回非0值，进入if，串口提示数据
    }else printf("连接路由器成功\r\n");             //串口提示数据
    	printf("准备获取IP地址\r\n");                   //串口提示数据
	if(WiFi_GetIP(50)){                                //准备获取IP地址，100ms超时单位，总计5s超时时间
		printf("获取IP地址失败，准备重启\r\n");     //返回非0值，进入if，串口提示数据
	}else printf("获取IP地址成功\r\n");             //串口提示数据
	
	printf("准备开启透传\r\n");                     //串口提示数据
	if(WiFi_SendCmd("AT+CIPMODE=1",50)){               //开启透传，100ms超时单位，总计5s超时时间
		printf("开启透传失败，准备重启\r\n");       //返回非0值，进入if，串口提示数据
	}else printf("开启透传成功\r\n");               //串口提示数据
	
	printf("准备关闭多路连接\r\n");                 //串口提示数据
	if(WiFi_SendCmd("AT+CIPMUX=0",50)){                //关闭多路连接，100ms超时单位，总计5s超时时间
		printf("关闭多路连接失败，准备重启\r\n");   //返回非0值，进入if，串口提示数据
	}else printf("关闭多路连接成功\r\n");           //串口提示数据
    WifiMsg.Mode = 1;							//r_flag标志置位，表示8266状态正常，可以继续，进行TCP连接 
    status++;
	
	//连接阿里云IOT服务器
	if(status==2)
	{
        printf("连接服务器：IP=%s,Port=%d\r\n",IP, Port);
        conn = WiFi_Connect(IP, Port, 100);
        printf("连接结果conn=%d\r\n",conn);
        status++;
	}
    //关闭WIFI回显
    //printf("关闭回显：%d\r\n", WiFi_Send("ATE0"));
	
	//登陆MQTT
	if(status==3)
	{
		if(MQTT_Connect(MQTT_CLIENTID, MQTT_USARNAME, MQTT_PASSWD) != 0)
		{
			printf("ESP8266阿里云MQTT登陆成功！\r\n");
			status++;
		}
		else
        {
            printf("ESP8266阿里云MQTT登陆失败！\r\n");
            status++;
        }
	}

	//订阅主题
	if(status==4)
	{
		if(MQTT_SubscribeTopic(MQTT_SUBSCRIBE_TOPIC,0,1) != 0)
		{
			printf("ESP8266阿里云MQTT订阅主题成功！\r\n");
		}
		else
        {
			printf("ESP8266阿里云MQTT订阅主题失败！\r\n");
        }
	}
}

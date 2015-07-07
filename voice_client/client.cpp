#include<winsock2.h>
#include <Windows.h> 
#include <stddef.h>
#include "proto.h"
#include <stdarg.h>
#include <iostream>
#include <stdio.h>
#include <limits>
//#include <signal.h>
//#include <climits>
#include"RtAudio.h"
#include "opus.h"

#pragma comment(lib,"ws2_32.lib")//注意添加


const int MAXBUFFERSIZE = 200;
const int PORT = 11000;
#define SERVER "123.58.176.6"  //ip address of udp server
//#define SERVER "127.0.0.1"  //ip address of udp server

typedef struct {
	char *host;
	char *port;

	uint32_t sample_rate;  // in Hz
	uint8_t channel_count;  // 1 or 2
	uint16_t frame_duration;  // in 0.1 ms units, 25 (2.5ms), 50, 100, 200, 400, 600

	int AudioInputId, AudioOutputId;

	size_t frame_samples_per_channel;
	size_t frame_size;  // in bytes用户每次可输入数据最大数

} options_t, *options_p;
options_t opts;

//全局变量
volatile bool UserCanSend = false; //用户数据输入完毕，可以发送
volatile bool UserCanRecieve = false;//有数据来自服务器，可以接受
uint8_t *BufferOut; //采集音频输出buff
uint8_t *BufferIn ; //接收音频播放buff
HANDLE hMutex1;
HANDLE hMutex2;
bool Quit = false;
void parse_options(int argc, char **argv, options_p opts);
void show_usage_and_exit(char *program_name);
void notice(const char *format, ...);
void error(const char *format, ...);
void die(int status, const char *format, ...);
void pdie(int status, const char *message);


//
// Output functions
//

void notice(const char *format, ...){
	va_list args;
	va_start(args, format);
	vfprintf(stderr, format, args);
	va_end(args);
}

void error(const char *format, ...){
	va_list args;
	va_start(args, format);
	vfprintf(stderr, format, args);
	va_end(args);
}

void die(int status, const char *format, ...){
	va_list args;
	va_start(args, format);
	vfprintf(stderr, format, args);
	va_end(args);
	exit(status);
}

void pdie(int status, const char *message){
	perror(message);
	exit(status);
}

//
// Argument parsing stuff
//
void parse_options(int argc, char **argv, options_p opts){
	// Set default options
	opts->host = NULL;
	opts->port = "8800";
	opts->sample_rate = 48000;
	opts->channel_count = 2;
	opts->frame_duration = 100;
	opts->AudioInputId = 0;
	opts->AudioOutputId = 0;
	//选取音频捕捉和播放设备
	std::cout << "\nFinding Audio Devices... \n";
	RtAudio audio;
	RtAudio::DeviceInfo info;
	unsigned int devices = audio.getDeviceCount();
	std::cout << "Found " << devices - 1 << " device(s) ...\n";
	std::cout << "========================================================\n";
	for (unsigned int i=1; i<devices; i++) {
		info = audio.getDeviceInfo(i);
		std::cout << "Device ID =  " << i << '\n';
		std::cout << "Device Name = " << info.name << '\n';
		if ( info.probed == false )
			std::cout << "Probe Status = UNsuccessful\n";
		else {
			std::cout << "Output Channels = " << info.outputChannels << '\n';
			std::cout << "Input Channels = " << info.inputChannels << '\n';
			std::cout << "Duplex Channels = " << info.duplexChannels << '\n';
			if ( info.isDefaultOutput ) std::cout << "This is the default output device.\n";
			if ( info.isDefaultInput ) std::cout << "This is the default input device.\n";
			//if ( info.nativeFormats == 0 )
			//	std::cout << "No natively supported data formats(?)!";
			//else {
			//	std::cout << "Natively supported data formats:\n";
			//	if ( info.nativeFormats & RTAUDIO_SINT8 )
			//		std::cout << "  8-bit int\n";
			//	if ( info.nativeFormats & RTAUDIO_SINT16 )
			//		std::cout << "  16-bit int\n";
			//	if ( info.nativeFormats & RTAUDIO_SINT24 )
			//		std::cout << "  24-bit int\n";
			//	if ( info.nativeFormats & RTAUDIO_SINT32 )
			//		std::cout << "  32-bit int\n";
			//	if ( info.nativeFormats & RTAUDIO_FLOAT32 )
			//		std::cout << "  32-bit float\n";
			//	if ( info.nativeFormats & RTAUDIO_FLOAT64 )
			//		std::cout << "  64-bit float\n";
			//}
			//if ( info.sampleRates.size() < 1 )
			//	std::cout << "No supported sample rates found!";
			//else {
			//	std::cout << "Supported sample rates = ";
			//	for (unsigned int j=0; j<info.sampleRates.size(); j++)
			//		std::cout << info.sampleRates[j] << " ";
			//}
			if(i<devices - 1)
			std::cout << "-------------------------------------\n";
		}
	}
	std::cout << "=========================================================\n";
	std::cout << "Please Input the Record Device ID (1-"<<devices - 1<<",or 0 to use the defalt):\n";
	std::cin>>opts->AudioInputId;
	std::cout << "Please Input the Play Device ID (1-"<<devices - 1<<",or 0 to use the defalt):\n";
	std::cin>>opts->AudioOutputId;
	//windows下省略自动输入参数解析,windows下host、port用默认参数
	// Calculate derived values
	opts->frame_samples_per_channel = 480;//(opts->sample_rate * opts->frame_duration) / 10000LL;
	opts->frame_size = opts->channel_count * opts->frame_samples_per_channel * sizeof(int16_t);
	// Print options
	notice("Audio Communication Parameters:\n"
		"  host: %s, port: %s\n"
		"  sample_rate: %u, channel_count: %hhu, frame_duration: %.1f\n"
		"  frame_samples_per_channel: %d, frame_size: %d\n",
		opts->host, opts->port,
		opts->sample_rate, opts->channel_count, opts->frame_duration / 10.0,
		opts->frame_samples_per_channel, opts->frame_size
		);
}

// Interleaved buffers
int input( void * /*outputBuffer*/, void *inputBuffer, unsigned int nBufferFrames,
	double /*streamTime*/, RtAudioStreamStatus /*status*/, void *data )
{
	unsigned int *bytes = (unsigned int *) data;
	while(true)
	{
		if(UserCanSend == false)
		{
			WaitForSingleObject(hMutex1, INFINITE);
			memcpy(BufferOut,inputBuffer,*bytes );
			UserCanSend = true;
			ReleaseMutex(hMutex1);
			break;
		}else
			Sleep(10);
	}

	return 0;
}

//采集音频
DWORD WINAPI recording_thread(LPVOID lpParameter) //
{
	notice("Recording thread started...\n");
	unsigned int channels, fs, bufferFrames, offset = 0;
	RtAudio adc;

	channels=opts.channel_count;
	fs=opts.sample_rate;
	bufferFrames = 480;
	// Let RtAudio print messages to stderr.
	adc.showWarnings( true );
	// Set our stream parameters for input only.

	RtAudio::StreamParameters iParams;
	if ( opts.AudioInputId == 0 )
		iParams.deviceId = adc.getDefaultInputDevice();
	else
		iParams.deviceId = opts.AudioInputId;
	iParams.nChannels = channels;
	iParams.firstChannel = offset;
	uint32_t data = opts.frame_size;
	try {
		adc.openStream( NULL, &iParams, RTAUDIO_SINT16, fs, &bufferFrames, &input, (void *)&data );
		adc.startStream();
	}
	catch ( RtAudioError& e ) {
		std::cout << '\n' << e.getMessage() << '\n' << std::endl;
		goto cleanup;
	}
	//后期可加上按键暂停功能
	while ( 1 ) {
		Sleep(100); // wake every 100 ms to check if we're done
	}
cleanup:
	if ( adc.isStreamOpen() ) adc.closeStream();

	return 0;
}
int startup_recording_thread()
{

	HANDLE hThread_1 = CreateThread(NULL, 0, recording_thread, NULL, 0, NULL);
	CloseHandle(hThread_1);
	return 0;
}
//播放处理函数
int output( void *outputBuffer, void * /*inputBuffer*/, unsigned int nBufferFrames,
	double /*streamTime*/, RtAudioStreamStatus /*status*/, void *data )
{
	unsigned int *bytes = (unsigned int *) data;
	memset(outputBuffer,0,(*bytes));
	while(true)
	{
		if(UserCanRecieve == true)
		{
			WaitForSingleObject(hMutex2, INFINITE);
			memcpy(outputBuffer,BufferIn,(*bytes));
			UserCanRecieve = false;
			ReleaseMutex(hMutex2);
			break;
		}else
			Sleep(10);
	}
	return 0;
}
//播放音频
DWORD WINAPI playback_thread(LPVOID lpParameter) //
{

	notice("Playback thread started...\n");
	unsigned int channels, fs, bufferFrames, offset = 0;
	RtAudio dac;
	channels=opts.channel_count;
	fs=opts.sample_rate;
	bufferFrames = 480;
	// Let RtAudio print messages to stderr.
	dac.showWarnings( true );
	// Set our stream parameters for input only.

	RtAudio::StreamParameters oParams;
	if ( opts.AudioOutputId == 0 )
		oParams.deviceId = dac.getDefaultOutputDevice();
	else
		oParams.deviceId = opts.AudioOutputId;
	oParams.nChannels = channels;
	oParams.firstChannel = offset;
	uint32_t data = opts.frame_size;
	try {
		dac.openStream(&oParams,NULL, RTAUDIO_SINT16, fs, &bufferFrames, &output, (void *)&data );
		dac.startStream();
	}
	catch ( RtAudioError& e ) {
		std::cout << '\n' << e.getMessage() << '\n' << std::endl;
		goto cleanup;
	}

	//后期可加上按键暂停功能
	while ( 1 ) {
		Sleep(100); // wake every 100 ms to check if we're done
	}
cleanup:
	if ( dac.isStreamOpen() ) dac.closeStream();

	return 0;

}
int startup_playback_thread()
{
	HANDLE hThread_2 = CreateThread(NULL, 0, playback_thread, NULL, 0, NULL);
	CloseHandle(hThread_2);
	return 0;
}

// 捕获控制台 Ctrl+C 事件的函数
BOOL CtrlHandler( DWORD fdwCtrlType )
{
	switch (fdwCtrlType)
	{
		/* Handle the CTRL-C signal. */
	case CTRL_C_EVENT:
		printf("CTRL_C_EVENT \n");
		break;
	case CTRL_CLOSE_EVENT:
		Quit = true;
		printf("Wait for exsting... \n");
		Sleep(500);
		return 0;
	default:
		return FALSE;
	}
	return (TRUE);
}

int main(int argc, char **argv)
{
	parse_options(argc, argv, &opts);
	//设置信号量
	SetConsoleCtrlHandler((PHANDLER_ROUTINE)CtrlHandler, TRUE);
	//分配语音数据传输内存
	BufferOut = new uint8_t[opts.frame_size];
	BufferIn = new uint8_t[opts.frame_size];
	//establish_signal_handlers();//原来信号注册函数，windows 用sendmessage()先不加
	//开启线程
	hMutex1 = CreateMutex(NULL, FALSE, NULL);
	hMutex2 = CreateMutex(NULL, FALSE, NULL);
	startup_recording_thread();
	startup_playback_thread();

	// Allocate frame buffers
	int16_t *in_frame =  (int16_t *)malloc(opts.frame_size);
	int16_t *out_frame = (int16_t *)malloc(opts.frame_size);
	
	// Init Opus encoder and decoder
	int error_code = 0;
	OpusEncoder *enc;
	enc = opus_encoder_create(opts.sample_rate, opts.channel_count, OPUS_APPLICATION_VOIP, &error_code);

	OpusDecoder *dec;
 	dec = opus_decoder_create(opts.sample_rate, opts.channel_count, &error_code);

	if (error_code != OPUS_OK || error_code != OPUS_OK)
	{
		std::cout<<std::endl<<"OPUS coder and decoder Initialized failed！！！"<<std::endl;
	}
	//---------------------------建立socket连接------------------------------------
	WSADATA wsa;
	//Initialise winsock
	//printf("\nInitialising Winsock...");
	if (WSAStartup(MAKEWORD(2,2),&wsa) != 0) // WSACleanup();注意使用后要释放掉
	{
		printf("Failed. Error Code : %d",WSAGetLastError());
		exit(EXIT_FAILURE);
	}
	//printf("Initialised.\n");
	//初始化服务器地址信息
	struct sockaddr_in AddrServer;
	int ClientId, AddrServerLen=sizeof(AddrServer);
	memset((char *) &AddrServer, 0, sizeof(AddrServer));
	AddrServer.sin_family = AF_INET;
	AddrServer.sin_port = htons(PORT);
	AddrServer.sin_addr.S_un.S_addr = inet_addr(SERVER);
	//create cliet socket
	if ( (ClientId=socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == SOCKET_ERROR)
	{
		printf("socket() failed with error code : %d" , WSAGetLastError());
		//exit(EXIT_FAILURE);
	}
	//向服务器发送连接请求
	//通信格式：1.char-信令，2.char-ID,后面的为数据，
	packet_t packet;
	packet.type = PACKET_HELLO;
	if (sendto(ClientId, (const char *)&packet, offsetof(packet_t, user), 0 , (struct sockaddr *) &AddrServer, AddrServerLen) == SOCKET_ERROR)
	{
		printf("sendto() failed with error code : %d" , WSAGetLastError());
		exit(EXIT_FAILURE); 
	}
	//接收服务器端应答信息
	std::cout<<"conecting server..."<<std::endl;
	uint8_t UserId = 0;
	do {
		if (recvfrom(ClientId, (char*)&packet, sizeof(packet), 0, (struct sockaddr *) &AddrServer, &AddrServerLen) == SOCKET_ERROR)
		{
			int derr = WSAGetLastError();
			if(derr != WSAECONNRESET ) //忽略掉winsock 的一个bug
			{
				printf("recvfrom() failed with error code : %d" , WSAGetLastError());
				exit(EXIT_FAILURE); 
			}
		}
	} while(packet.type != PACKET_WELCOME );
	UserId = packet.user;
	notice("Welcome from server, you're client %hhu\n", UserId);

	//进入主循环
	uint16_t send_seq = 0,recv_seq = 0;
	int ret;
	fd_set ReadFds;
	struct timeval TV;
	TV.tv_sec = 0;
	TV.tv_usec = 0;
	uint16_t bytes_received = 0;
	while(!Quit)//后期加入信号量控制
	{
		//select函数检测是否有数据可以接收
		FD_ZERO(&ReadFds);
		FD_SET(ClientId,&ReadFds);
		if ((ret = select(ClientId, &ReadFds, NULL, NULL, &TV)) == SOCKET_ERROR) //没有延时
		{
			printf("select() returned with error %d\n", WSAGetLastError());
			return -1;
		}
		//检查是否有来自服务器数据,并接收显示
		if(FD_ISSET(ClientId,&ReadFds))
		{
			
			//socket数据接收
			if (( bytes_received = recvfrom(ClientId, (char *)&packet, sizeof(packet), 0, (struct sockaddr *)&AddrServer, &AddrServerLen) ) == SOCKET_ERROR)
			{
				printf("recvfrom() failed with error code : %d" , WSAGetLastError());
				//exit(EXIT_FAILURE);
			}
			size_t data_len = bytes_received - offsetof(packet_t, data);

			if (packet.type == PACKET_DATA)
			{
				if (data_len != packet.len){
					std::cout<<"incomplete packet, expected "<<(int)packet.len<<" got "<<data_len<<" bytes!"<<std::endl;
					int decoded_samples = opus_decode(dec, NULL, 0, out_frame, opts.frame_samples_per_channel, 0);
					if (decoded_samples < 0)
						std::cout<<"opus_decode error!!!"<<std::endl;
					else
						while(true)
						{
							if(UserCanRecieve == false)
							{
								WaitForSingleObject(hMutex2, INFINITE);
								memcpy(BufferIn,out_frame,opts.frame_size);
								UserCanRecieve = true;
								ReleaseMutex(hMutex2);
								break;
							}else
								Sleep(10);
						}
					recv_seq ++;
					//conceal_loss();
				}else{
					//std::cout<<"packet user: "<<(int)packet.user<<"packet seq: "<<(int)packet.seq<<"   cur seq: "<<recv_seq<<std::endl;
					recv_seq ++;
					int decoded_samples = opus_decode(dec, packet.data, data_len, out_frame, opts.frame_samples_per_channel, 0);
					if (decoded_samples < 0)
						std::cout<<"opus_decode error!!!"<<std::endl;
					else{
						while(true)
						{
							if(UserCanRecieve == false)
							{
								WaitForSingleObject(hMutex2, INFINITE);
								memcpy(BufferIn,out_frame,opts.frame_size);
								UserCanRecieve = true;
								ReleaseMutex(hMutex2);
								break;
							}else
								Sleep(10);
						}
					}	
					

				}

			}else if (packet.type == PACKET_JOIN)
			{
				recv_seq = 0;
				std::cout<<"user"<<(int)packet.user<<" joined!"<<std::endl;
			}else if (packet.type == PACKET_BYE)
			{
				std::cout<<"user"<<(int)packet.user<<" disconnected!"<<std::endl;
			}else
			{
				std::cout<<"unknown packet from user"<<(int)packet.user<<std::endl;
			}
			
		}
		//检查音频数据是否可读并转发
		if(UserCanSend == true)
		{
			WaitForSingleObject(hMutex1, INFINITE);
			memcpy(in_frame,BufferOut,opts.frame_size);
			//调试	
			UserCanSend = false;
			ReleaseMutex(hMutex1);
			//socket数据发送			
			packet.type = PACKET_DATA;
			packet.user = UserId;
			packet.seq = send_seq;
			int32_t len = opus_encode(enc, in_frame, opts.frame_samples_per_channel, packet.data, sizeof(packet) - offsetof(packet_t, data));
			if (len < 0)
				std::cout<<"opus_encode error!"<<std::endl;
			else if (len == 1)
				continue;
			packet.len = len;

			if (sendto(ClientId, (const char *)&packet, offsetof(packet_t, data) + len, 0 , (struct sockaddr *) &AddrServer, AddrServerLen) == SOCKET_ERROR)
			{
				printf("sendto() failed with error code : %d" , WSAGetLastError());
				//exit(EXIT_FAILURE); 
			}
			send_seq++;
		}
	}
	//断开连接
	std::cout<<"Exiting..."<<std::endl;
	packet.type = PACKET_BYE;
	packet.user = UserId;
	if (sendto(ClientId, (const char *)&packet, offsetof(packet_t, seq), 0 , (struct sockaddr *) &AddrServer, AddrServerLen) == SOCKET_ERROR)
	{
		printf("sendto() failed with error code : %d" , WSAGetLastError());
		exit(EXIT_FAILURE); 
	}

	closesocket(ClientId);
	WSACleanup();
	delete []BufferIn;
	delete []BufferOut;
	free(in_frame);
	free(out_frame);
	//std::cout<<"test!!"<<std::endl;
	getchar();
	return 0;
}
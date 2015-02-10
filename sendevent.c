#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/input.h> // this does not compile
#include <errno.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <sys/wait.h>
#include <sys/types.h>


#define TOUCH_DEFAULT_AREA      0x18
#define TOUCH_DEFAULT_PRESSURE  0x18
#define POINT_NUM 3

#define SIMULATOR_TOUCH  0
#define SIMULATOR_HARDKEY 1
#define SIMULATOR_MEDIAPLAYER 2
#define SIMULATOR_A_TOUCH 3

#define TOUCH_2_MEDIA_PLAYER_NUM 7
#define HARDKEY_NUM 5

static int iDebug = 0;
int total=0;
int totalpoint[POINT_NUM];

int bTouch_or_Hardkey=SIMULATOR_TOUCH;

int sendevent(int fd,unsigned int type, unsigned int code, int val)
{

    struct input_event event;
    int ret = -1;
    memset(&event, 0, sizeof(event));
    event.type = type;
    event.code = code;
    event.value = val;
    if(iDebug){
        printf("===0x%04x 0x%04x 0x%08x\n",type,code,val);
    }
    ret = write(fd, &event, sizeof(event));
    if(ret < sizeof(event)) {
        fprintf(stderr, "write a event failed, %s\n", strerror(errno));
        return -1;
    }
    return 0;
}

void HardKeyDown(int fd, unsigned int key)
{
    sendevent(fd,EV_KEY,key,1);
    sendevent(fd,EV_SYN,SYN_REPORT,0);//0 0 0 
}

void HardKeyUp(int fd, unsigned int key)
{
    sendevent(fd,EV_KEY,key,0);
    sendevent(fd,EV_SYN,SYN_REPORT,0);//0 0 0 
}

void HardKey(int fd, unsigned int key)
{
    HardKeyDown(fd,key);
    usleep(300*1000);//Sleep for 300ms
    HardKeyUp(fd,key);
}

int touchXY(int fd,unsigned int x, unsigned int y,int area, int pressure)
{
    static unsigned count = 0;
    count ++;
    if(iDebug){
        printf("------count=0x%08x----\n",count);
    }
    //sendevent(fd,EV_KEY,BTN_TOUCH,1);//touch down

    sendevent(fd,EV_ABS,ABS_MT_TOUCH_MAJOR,area);//0x30
    sendevent(fd,EV_ABS,ABS_MT_PRESSURE,pressure);//0x3a

    sendevent(fd,EV_ABS,ABS_MT_POSITION_X,x);//0x35
    sendevent(fd,EV_ABS,ABS_MT_POSITION_Y,y);//0x36

    sendevent(fd,EV_SYN,SYN_MT_REPORT,0);//0 2 0
    sendevent(fd,EV_SYN,SYN_REPORT,0);//0 0 0 

    //sendevent(fd,EV_KEY,BTN_TOUCH,0);//Touch up

    sendevent(fd,EV_ABS,ABS_MT_TOUCH_MAJOR,0);
    sendevent(fd,EV_ABS,ABS_MT_PRESSURE,0);

    sendevent(fd,EV_SYN,SYN_MT_REPORT,0);
    sendevent(fd,EV_SYN,SYN_REPORT,0);

    return 0;
}

void StopHandler(int signo) 
{
    int i;
    if(bTouch_or_Hardkey == SIMULATOR_TOUCH){
        printf("\n=========Statistics==========\n");
        printf("total:%08d\n",total);
        for(i=0; i<POINT_NUM; i++){
            printf("total:%08d\tpercent:%04.2f%%\n",totalpoint[i],(float)totalpoint[i]*100.0/(float)total);
        }
        printf("=============================\n\n");
        exit(0);
    }
    printf("\n=============================\n\n");
    exit(0);
}

void PrintHardkeyMap(){
    printf("\n------------------------------------------\n");
    printf("HardKey map,eg Hit H to send Home hardKey:\n"
        "\tHome           --> H\n"
        "\tBack           --> B\n"
        "\tMute           --> M\n"
        "\tVolump Up      --> U\n"
        "\tVolump Down    --> D\n"
        "\tPlease input:  <=="
        );
}

void PrintTouchKeyMap(){
    printf("\n------------------------------------------\n");
    printf("Touch map,eg Hit P to touch the AV icon:\n"
        "\tAV                      --> A\n"
        "\tPLCaution               --> P\n"
        "\tMediaInAV               --> M\n"
        "\tUDiskInMediaPlayer      --> U\n"
        "\tSwitchMediaSource       --> S\n"
        "\tSelectVideoInSwitch     --> V\n"
        "\tSelectAudioInSwitch     --> O\n"
        "\tPlease input:           <=="
        );
}

int main(int argc, char *argv[])
{
    int key,bIsHardKeySupport, bIsTouchSupport ;
    int i;
    int fd,fd_hardkey;
    int ret;
    unsigned long sleepms = 200;
    int version;
    struct input_event event;
    int randIndex;
    int HardKeysMap[HARDKEY_NUM][2]={
        {'H',KEY_HOME},//home
        {'B',KEY_BACK},//Back
        {'U',KEY_VOLUMEUP},//Volunm Up
        {'D',KEY_VOLUMEDOWN},//Volumn Down
        {'M',KEY_MUTE},//Mute
    };
    int point[3][2]={
        {0x137,0x1b5},
        {0x18f,0x1c5},
        {0x1df,0x1c7}
    };

    int ToMediaPlayer[TOUCH_2_MEDIA_PLAYER_NUM][3]={
        {0x2be,0x1bd,'P'}, //PLCaution
        {0x19b,0x1ae,'A'}, //AV icon touch
        {0x11f,0x0b5,'M'}, //Media Player icon
        {0x0d1,0x10d,'U'}, //U disk selection
        {0x2e0,0x03c,'S'}, //Switch the source to Video instead of music
        {0x2c2,0x0b1,'V'}, //Select the Video list item
        {0x2d0,0x072,'O'}, //Select the Audio list item
    };
    if ((argc != 2) && (argc !=3) && (argc != 4) && (argc != 5) && (argc != 6))
    {
        //Just a touch, like touch the AV icon in desktop
        printf("Usage:\n");
        printf("1.Format1:\n");
        printf("%s <Touch device> [on|off] <Touching time,in ms> <touch1> \n", argv[0]);
        printf("Funtion:Simulate a touch event\n");
        printf("eg: Simulate touch the AV icon:\n"
                "\t%s /dev/input/event0 off 200 touch1\n", argv[0]);

        printf("\n2.Format2:\n");
        printf("%s <Touch device> [on|off] [Touch interval|HardKey press time,in ms] [touch or hardkey,mediaplayer] [Hardkey Device]\n", argv[0]);
        printf("Funtion:Simulate the random touch in Mediaplayer page\n");
        printf("eg: Rand touch between the Back|Next|Provious in Mediaplayer:\n"
                "\t%s /dev/input/event0 on 200\n", argv[0]);

        printf("\n3.Format3:\n");
        printf("%s <Hardkey input device> [on|off] <Pressing time,in ms> <hardkey> \n", argv[0]);
        printf("eg: Simulator hardkey input\n" 
                "\t%s /dev/input/event1 off 200 hardkey \n", argv[0]);
                //"\t%s /dev/input/event1 on 200 hardkey /dev/input/event1\n", argv[0]);
        //printf("eg: "
        //        "%s /dev/input/event1 on 200 mediaplayer\n", argv[0]);
        printf("\n4.Format4:\n");
        printf("%s <Touch device> <on|off> <Touch interval,in ms> <mediaplayer> <Hardkey Device>\n", argv[0]);
        printf("Funtion:Auto go to Mediaplayer page from the PLCaution,\n"
                "and random Switch between Back|Next|Pause\n");
        printf("eg: \n"
                "\t%s /dev/input/event0 on 200 mediaplayer /dev/input/event1\n", argv[0]);
        printf("This format is the extend from the format2 \n");

        printf("================================================================\n");
        return 0;
    }

    for(i = 0; i<argc; i++){
        printf("argv[%d]=%s\n",i,argv[i]);
        if(i==1){
            printf("\tTouch dev node is %s\n",argv[i]);
            fd = open(argv[i], O_RDWR);
            if(fd < 0) {
                fprintf(stderr, "could not open %s, %s\n", argv[i], strerror(errno));
                return 1;
            }
        }
        if(i==2 && 0 == strcmp(argv[i],"on")){
            iDebug = 1; 
            printf("\tSet the debug out flag\n");
        }
        if(i==3){
            sleepms = atoi(argv[i]);
            printf("\tPeriod is %dms\n",sleepms);
        }
#if 1
        if(i==4 ){
            //Touch random in mediaplayer
            if(0 == strcmp(argv[i],"touch")){
                bTouch_or_Hardkey = SIMULATOR_TOUCH;
                printf("\tSimulator type is touch\n");
            }
            //Only a touch
            else if(0 == strcmp(argv[i],"touch1")){
                bTouch_or_Hardkey = SIMULATOR_A_TOUCH;
                printf("\tSimulator type is hardkey\n");
            }
            else if(0 == strcmp(argv[i],"hardkey")){
                bTouch_or_Hardkey = SIMULATOR_HARDKEY;
                printf("\tSimulator type is hardkey\n");
            }
            else if(0 == strcmp(argv[i],"mediaplayer")){
                bTouch_or_Hardkey = SIMULATOR_MEDIAPLAYER;
                printf("\tSimulator type is mediaplayer\n");
            }
            else{
                bTouch_or_Hardkey = SIMULATOR_TOUCH;
                printf("\tSimulator type default is touch\n");
            }
        }
        if(i==5){
            if(bTouch_or_Hardkey==SIMULATOR_HARDKEY || bTouch_or_Hardkey ==SIMULATOR_MEDIAPLAYER){
                printf("\tHardkey dev node is %s\n",argv[i]);
                fd_hardkey = open(argv[i], O_RDWR);
                if(fd_hardkey < 0) {
                    fprintf(stderr, "could not open %s, %s\n", argv[i], strerror(errno));
                    return 1;
                }
            }
            if(bTouch_or_Hardkey==SIMULATOR_A_TOUCH){
                
            }
        }
#endif
    }

    //Register the Ctrl+c handler
    signal(SIGINT, StopHandler);

    //For a touch 
    if(bTouch_or_Hardkey == SIMULATOR_A_TOUCH){
        PrintTouchKeyMap();
        while(key=getchar()){
            if(key == '\n'){
                PrintTouchKeyMap();
                continue;
            }
            //printf("\n==>>>Get hardkey code=0x%08x=%c to be send.\n",
                    //key,key,HARDKEY_NUM);
            for(i=0; i<TOUCH_2_MEDIA_PLAYER_NUM;i++){
                if(ToMediaPlayer[i][2] == key){
                    bIsTouchSupport=1;
                    //printf("==>>>Got Supported hardkey=0x%08x %c to be send.\n",
                    //    HardKeysMap[i][1],HardKeysMap[i][0]);
                    break;
                }
                bIsTouchSupport = 0;
            }
            if(0==bIsTouchSupport){
                printf("\nError: Touch=0x%08x not supported!\n",key);
                PrintTouchKeyMap();
                continue;
            }
            touchXY(fd,ToMediaPlayer[i][0], ToMediaPlayer[i][1],
                    TOUCH_DEFAULT_AREA, TOUCH_DEFAULT_PRESSURE);
            printf("Sent supported hardkey code=0x%04x!\n",key);
            PrintTouchKeyMap();
        }
    }

    //For the HardKey Send
    if(bTouch_or_Hardkey == SIMULATOR_HARDKEY){
        PrintHardkeyMap();
        fd_hardkey = fd;
        while(key=getchar()){
            if(key == '\n'){
                PrintHardkeyMap();
                continue;
            }
            //printf("\n==>>>Get hardkey code=0x%08x=%c to be send.\n",
                    //key,key,HARDKEY_NUM);
            for(i=0; i<HARDKEY_NUM;i++){
                if(HardKeysMap[i][0] == key){
                    key = HardKeysMap[i][1];
                    bIsHardKeySupport = 1;
                    //printf("==>>>Got Supported hardkey=0x%08x %c to be send.\n",
                    //    HardKeysMap[i][1],HardKeysMap[i][0]);
                    break;
                }
                bIsHardKeySupport = 0;
            }
            if(0==bIsHardKeySupport){
                printf("\nError: hardkey=0x%08x not supported!\n",key);
                PrintHardkeyMap();
                continue;
            }
            HardKey(fd_hardkey, key);
            printf("Sent supported hardkey code=0x%04x!\n",key);
            PrintHardkeyMap();
        }
    }

    //For the Full simulator of the MediaPlayer switch form booting
    if(bTouch_or_Hardkey == SIMULATOR_MEDIAPLAYER){
        //PLCaution
        touchXY(fd,ToMediaPlayer[0][0],ToMediaPlayer[0][1],0x18,0x18);//WLAN
        HardKey(fd_hardkey, KEY_BACK);
        usleep(1000*900); 
        HardKey(fd_hardkey, KEY_BACK);
        usleep(1000*900); 
        HardKey(fd_hardkey, KEY_HOME);
        usleep(1000*900); 

        //Simulate touch to the mediaplayer 
        for(i=1; i<TOUCH_2_MEDIA_PLAYER_NUM;i++){
            touchXY(fd,ToMediaPlayer[i][0],ToMediaPlayer[i][1],0x18,0x18);//WLAN
            usleep(1000*900); 
        }
        //Now rand switch in Mediaplayer
        bTouch_or_Hardkey = SIMULATOR_TOUCH;
    }
    //For the rand touch (play Next back)
    if(bTouch_or_Hardkey == SIMULATOR_TOUCH){
        srand((int)time(0));
        while(1){
            randIndex = 0+(int)((3.0*rand())/(RAND_MAX+1.0));

            total++;
            totalpoint[randIndex]++;

            if(iDebug){
                printf("randIndex=0x%08x\n",randIndex);
            }

            if(bTouch_or_Hardkey == SIMULATOR_TOUCH){
                touchXY(fd,point[randIndex][0],point[randIndex][1],0x18,0x18);//WLAN
                usleep(1000*sleepms); 
            }
#if 0
            touchXY(fd,0x40,0x1a4,0x18,0x18);//WLAN
            sleep(1);
            touchXY(fd,0x2e4,0x92,0x18,0x18);//WLAN Setting
            sleep(1);
            touchXY(fd,0x50,0x9a,0x18,0x18);//AV
            sleep(1);
            touchXY(fd,0x2ce,0x88,0x18,0x18);//AV Setting
            sleep(1);
            break;
#endif
        }
    }

    return 0;
}

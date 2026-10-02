// RCOM 2026/2027
//
// Link layer protocol implementation

#include "link_layer.h"
#include "serial_port.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

// MISC
#define _POSIX_SOURCE 1 // POSIX compliant source
#define BUF_SIZE 256

#define FLAG 0x7E
#define A_TX 0x03
#define A_RX 0x01
#define C_SET 0x03
#define C_UA 0x07

#define FALSE 0
#define TRUE 1

#define SUP_FRAME_SIZE 5

typedef enum { START, FLAG_RCV, A_RCV, C_RCV, BCC_OK, STOP} frameState;


static int receiveSupervisionFrame(unsigned char a, unsigned char c)
{
    // ISSO TEM Q SAIR 
    /*
    const unsigned char expected[SUP_FRAME_SIZE] = {FLAG, a, c, a ^ c, FLAG};
    int matched = 0;
    */

    frameState state = START;
    
    do {
        unsigned char byte;
        int res = readByteSerialPort(&byte);
        if (res < 0)
            return -1;
        if (res == 0)
            continue;

            // TRATAR DADOS AQUI
        
        switch (state)
        {
        // START case
        case START:
            if (byte == FLAG){
                state = FLAG_RCV;
            } 
            break; 
            
        // FLAG_RCV case
        case FLAG_RCV:
            if (byte == a) {
                state = A_RCV;
            } else if (byte == FLAG){
                state = FLAG_RCV;
            } else {
                state = START;
            }
            break;

        // A_RCV case
        case A_RCV:
            if (byte == c){
                state = C_RCV;
            } else if (byte == FLAG){
                state = FLAG_RCV;
            } else {
                state = START;
            }
            break;
            
        // C_RCV case
        case C_RCV:
            if (byte == (a ^ c)){
                state = BCC_OK;
            } else if (byte == FLAG){
                state = FLAG_RCV;
            } else {
                state = START;
            }
            break;

        // BCC_OK case
        case BCC_OK:
            if (byte == FLAG){
                state = STOP;
            } else {
                state = START;
            }
            break;

        
        default:
            break;
        }




    } while (state != STOP);
    
    return 0;
}

int alarmCount = 0;
int alarmEnabled = FALSE;

void alarmHandler(int signal){
    // Can be used to change a flag that increases the number of alarms
    alarmEnabled = FALSE;
    alarmCount++;
    printf("Alarm #%d received\n", alarmCount);
}


////////////////////////////////////////////////
// LLOPEN
////////////////////////////////////////////////
int llOpenTx(LinkLayer llParameters){

    if (openSerialPort(llParameters.serialPort, llParameters.baudRate) < 0)
    {
        perror("openSerialPort");
        return -1;
    }

    printf("Serial port %s opened\n", llParameters.serialPort);

    struct sigaction act = {0};
    act.sa_handler = &alarmHandler;
    
    if (sigaction(SIGALRM, &act, NULL) == -1){
        perror("sigaction");
        closeSerialPort();
        return -1;
    }
    
    const unsigned char set[SUP_FRAME_SIZE] = {
        FLAG, A_TX, C_SET, A_TX ^ C_SET, FLAG
    };

    int retries = 3;
    while(alarmCount <= retries){
        if (writeBytesSerialPort(set, SUP_FRAME_SIZE) != SUP_FRAME_SIZE)
        {
            perror("writeBytesSerialPort");
            closeSerialPort();
            return -1;
        }

        printf("SET sent\n");

        int t = 3;
        alarm(t);
        alarmEnabled = TRUE;


        if (receiveSupervisionFrame(A_TX, C_UA) == 0){
            //perror("receiveSupervisionFrame");
            alarm(0);
            alarmEnabled = FALSE;
            //closeSerialPort();    
            printf("UA received. Connection established\n");
            return 0;
        }

    }


    return 0;
}

int llOpenRx(LinkLayer llParameters)
{
    if (openSerialPort(llParameters.serialPort, llParameters.baudRate) < 0)
    {
        perror("openSerialPort");
        return -1;
    }

    printf("Serial port %s opened\n", llParameters.serialPort);

    if (receiveSupervisionFrame(A_TX, C_SET) != 0)
    {
        perror("receiveSupervisionFrame");
        closeSerialPort();
        return -1;
    }

    const unsigned char ua[SUP_FRAME_SIZE] = {
        FLAG, A_TX, C_UA, A_TX ^ C_UA, FLAG
    };

    if (writeBytesSerialPort(ua, SUP_FRAME_SIZE) != SUP_FRAME_SIZE)
    {
        perror("writeBytesSerialPort");
        closeSerialPort();
        return -1;
    }

    printf("UA sent. Connection established\n");

    return 0;
}


////////////////////////////////////////////////
// LLSEND
////////////////////////////////////////////////
int llSend(const unsigned char *buf, int bufSize)
{
    // TODO: Implement this function

    return 0;
}

////////////////////////////////////////////////
// LLRECEIVE
////////////////////////////////////////////////
int llReceive(unsigned char *packet)
{
    // TODO: Implement this function

    return 0;
}

////////////////////////////////////////////////
// LLCLOSE
////////////////////////////////////////////////
int llCloseTx()
{
    // TODO: Implement this function

    return 0;
}

int llCloseRx()
{
    // TODO: Implement this function

    return 0;
}

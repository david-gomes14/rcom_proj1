// RCOM 2026/2027
//
// Application layer protocol implementation

#include "application_layer.h"
#include "link_layer.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>

#define START 0x01
#define DATA 0x02
#define END 0x03

#define FILE_SIZE 0x00
#define FILE_NAME 0x01

#define BUFFERZAO_SIZE 256
#define CONTROL_PACKET_SIZE 268

#define FALSE 0
#define TRUE 1

static int createControlPacket(unsigned char type, uint64_t fileSize, const char *filename, unsigned char *packet, size_t capacity) {
    if (type != START && type != END) {
        return -1;
    }

    const char *name = strrchr(filename, '/');
    name = (name != NULL) ? name + 1 : filename;

    size_t nameLen = strlen(name);

    if (nameLen > 255 || capacity < 13 + nameLen) {
        return -1;
    }

    int index = 0;
    packet[index++] = type;

    packet[index++] = FILE_SIZE;
    packet[index++] = 8;

    for (int i = 7; i >= 0; i--) {
        packet[index++] = (fileSize >> (i * 8)) & 0xFF;
    }

    packet[index++] = FILE_NAME;
    packet[index++] = (unsigned char) nameLen;

    memcpy(packet + index, name, nameLen);
    index += (int) nameLen;

    return index;
}


static int getFileSize(const unsigned char *packet, int packetSize, uint64_t *fileSize) {
    
    int index = 1;
    int found = FALSE;

    while (index < packetSize)
    {
        if (index + 2 > packetSize) {
            return -1;
        }

        unsigned char type = packet[index++];
        unsigned char length = packet[index++];

        if (length > packetSize - index) {
            return -1;
        }

        if (type == FILE_SIZE)
        {
            if (length != 8 || found) {
                return -1;
            }

            *fileSize = 0;

            for (int i = 0; i < 8; i++) {
                *fileSize = (*fileSize << 8) | packet[index + i];
            }

            found = TRUE;
        }

        index += length;
    }

    return found ? 0 : -1;
}


void applicationLayer(const char *serialPort, const char *role, int baudRate,
                      int nTries, int timeout, const char *filename)
{
    // ----------------------------------------------------
    // TODO: Adapt and extend this code according to the specifications of the project.
    // ----------------------------------------------------

    LinkLayer llParameters = {
        .baudRate = baudRate,
        .nRetransmissions = nTries,
        .timeout = timeout,
    };
    strcpy(llParameters.serialPort, serialPort);

    if (strcmp(role, "tx") == 0)
    {
        int fd = open(filename, O_RDONLY);
        if (fd == -1) {
		    perror("open");
		    return;
	    }

        struct stat st;

        if (fstat(fd, &st) == -1) { // abre o stat na porta fd
            perror("fstat");
            close(fd);
            return;
        }

        if (st.st_size < 0) {      // .st_size dá o tamanho do ficheiro
            close(fd);
            return;
        }

        uint64_t fileSize = (uint64_t) st.st_size;

        if (llOpenTx(llParameters) < 0) {
            close(fd);
            return;
        }

        int success = TRUE;

        unsigned char controlPacket[CONTROL_PACKET_SIZE];
        int startControlSize = createControlPacket(START, fileSize, filename, controlPacket, sizeof(controlPacket));

        if (startControlSize < 0) {
            perror("createControlPacket");
            success = FALSE;
        }

        if (success && llSend(controlPacket, startControlSize) < 0) {
            perror("llSend");
            success = FALSE;
        }

        unsigned char bufferzao[BUFFERZAO_SIZE];
        ssize_t bytesRead = 0;

        while (success && (bytesRead = read(fd, bufferzao, sizeof(bufferzao))) > 0) {
            unsigned char dataPacket[BUFFERZAO_SIZE + 3];

            dataPacket[0] = DATA;
            dataPacket[1] = (bytesRead >> 8) & 0xFF; // L2
            dataPacket[2] = bytesRead & 0xFF;        // L1

            memcpy(dataPacket + 3, bufferzao, bytesRead);

            if (llSend(dataPacket, bytesRead + 3) < 0) {
                perror("llSend");
                success = FALSE;
                break;
            }
        }

        if (bytesRead == -1) {
            perror("read");
            success = FALSE;
        }

        if (success) {
            int endControlSize = createControlPacket(END, fileSize, filename, controlPacket, sizeof(controlPacket));

            if (endControlSize < 0) {
                perror("createControlPacket");
                success = FALSE;
            }

            if (success && llSend(controlPacket, endControlSize) < 0) {
                perror("llSend");
                success = FALSE;
            }
        }

        close(fd);

        if (llCloseTx() < 0) {
            return;
        }

        if (success) {
            printf("SUCESSO\n");
        }

    }
    else if (strcmp(role, "rx") == 0)
    {
        int fd = open("received.bin", O_WRONLY | O_CREAT, 0644);
        if (fd == -1) {
		    perror("open");
		    return;
	    }

        if (llOpenRx(llParameters) < 0) {
            close(fd);
            return;
        }






        unsigned char received[CONTROL_PACKET_SIZE];

        int bytes_lidos = llReceive(received);        
        printf("Recebi %d bytes\n", bytes_lidos);
        
        for (int i = 0; i < bytes_lidos; i++) {
            printf("0x%02X ", received[i]);
        }
        printf("\n");

        int bytes_lidos2 = llReceive(received);        
        printf("Recebi %d bytes\n", bytes_lidos2);

        if (llCloseRx() < 0) {
            return;
        }
    }
    else
    {
        printf("Invalid role: %s. Must be 'tx' or 'rx'.\n", role);
        return;
    }
    
}

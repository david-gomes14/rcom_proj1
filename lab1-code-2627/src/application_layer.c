// RCOM 2026/2027
//
// Application layer protocol implementation

#include "application_layer.h"
#include "link_layer.h"

#include <stdio.h>
#include <string.h>

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
        unsigned char bufferzao[10] = {0x0A, 0x0B, 0x0C, 0x0D, 0x7E, 0x0F, 0x1A, 0x7D, 0x1C, 0x1D};

        if (llOpenTx(llParameters) < 0) {
            return;
        }

        if (llSend(bufferzao, 10) < 0) {
            return;
        }

        if (llSend(bufferzao, 10) < 0) {
            return;
        }

        if (llCloseTx() < 0) {
            return;
        }

    }
    else if (strcmp(role, "rx") == 0)
    {
        unsigned char received[256];

        llOpenRx(llParameters);
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

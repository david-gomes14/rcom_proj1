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
        // Abrir ligação na camada de enlace
        if (llOpenTx(llParameters) < 0) {
            printf("Erro: llOpenTx falhou.\n");
            return;
        }

        // Abrir o ficheiro para leitura
        FILE *file = fopen(filename, "rb");
        if (file == NULL) {
            printf("Erro: não foi possível abrir o ficheiro '%s'.\n", filename);
            return;
        }

        // Calcular o tamanho do ficheiro
        fseek(file, 0, SEEK_END);
        long fileSize = ftell(file);
        fseek(file, 0, SEEK_SET);

        // Determinar quantos bytes são necessários para representar o tamanho
        long tmp = fileSize;
        unsigned char fileSizeBytes[sizeof(long)];
        int fileSizeNumBytes = 0;
        do {
            fileSizeBytes[fileSizeNumBytes++] = tmp & 0xFF;
            tmp >>= 8;
        } while (tmp > 0);

        int filenameLen = strlen(filename);

        // -------------------------------------------------------
        // Construir e enviar o pacote START (C = 1)
        // Formato: C | T=0 | L(size) | V(size) | T=1 | L(name) | V(name)
        // -------------------------------------------------------
        unsigned char controlPacket[MAX_PAYLOAD_SIZE];
        int idx = 0;

        controlPacket[idx++] = 1; // C = 1 (START)

        // TLV para o tamanho do ficheiro (T = 0)
        controlPacket[idx++] = 0;                // T = 0
        controlPacket[idx++] = fileSizeNumBytes;  // L
        for (int i = 0; i < fileSizeNumBytes; i++) {
            controlPacket[idx++] = fileSizeBytes[i]; // V (little-endian)
        }

        // TLV para o nome do ficheiro (T = 1)
        controlPacket[idx++] = 1;            // T = 1
        controlPacket[idx++] = filenameLen;  // L
        memcpy(&controlPacket[idx], filename, filenameLen);
        idx += filenameLen;

        if (llSend(controlPacket, idx) < 0) {
            printf("Erro: falha ao enviar pacote START.\n");
            fclose(file);
            return;
        }

        // -------------------------------------------------------
        // Enviar pacotes de DADOS (C = 2)
        // Formato: C | L2 | L1 | dados[K]  onde K = 256*L2 + L1
        // -------------------------------------------------------
        int maxDataPerPacket = MAX_PAYLOAD_SIZE - 3; // 3 bytes: C + L2 + L1
        unsigned char dataPacket[MAX_PAYLOAD_SIZE];
        long bytesRemaining = fileSize;

        while (bytesRemaining > 0) {
            int chunkSize = (bytesRemaining > maxDataPerPacket)
                                ? maxDataPerPacket
                                : (int)bytesRemaining;

            int bytesRead = fread(&dataPacket[3], 1, chunkSize, file);
            if (bytesRead <= 0) {
                printf("Erro: falha na leitura do ficheiro.\n");
                fclose(file);
                return;
            }

            dataPacket[0] = 2;                        // C = 2 (DATA)
            dataPacket[1] = (bytesRead >> 8) & 0xFF;  // L2
            dataPacket[2] = bytesRead & 0xFF;          // L1

            if (llSend(dataPacket, 3 + bytesRead) < 0) {
                printf("Erro: falha ao enviar pacote de dados.\n");
                fclose(file);
                return;
            }

            bytesRemaining -= bytesRead;
        }

        // -------------------------------------------------------
        // Construir e enviar o pacote END (C = 3)
        // Mesma informação TLV que o pacote START
        // -------------------------------------------------------
        controlPacket[0] = 3; // C = 3 (END)

        if (llSend(controlPacket, idx) < 0) {
            printf("Erro: falha ao enviar pacote END.\n");
            fclose(file);
            return;
        }

        // Fechar o ficheiro e a ligação
        fclose(file);

        if (llCloseTx() < 0) {
            printf("Erro: llCloseTx falhou.\n");
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

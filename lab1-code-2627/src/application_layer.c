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
    LinkLayer llParameters = {
        .baudRate = baudRate,
        .nRetransmissions = nTries,
        .timeout = timeout,
    };
    strcpy(llParameters.serialPort, serialPort);

    if (strcmp(role, "tx") == 0)
    {
        // Abrir o ficheiro ANTES da ligação, para não deixar a ligação aberta
        // se o ficheiro não existir.
        FILE *file = fopen(filename, "rb");
        if (file == NULL) {
            printf("Error: could not open file '%s'.\n", filename);
            return;
        }

        fseek(file, 0, SEEK_END);
        long fileSize = ftell(file);
        rewind(file);

        printf("File size '%s': %ld bytes\n", filename, fileSize);

        if (fileSize <= 0) {
            printf("Error: file is empty or error obtaining size.\n");
            fclose(file);
            return;
        }

        int filenameLen = strlen(filename);
        // L é 1 byte (<=255) e o pacote START tem de caber em MAX_PAYLOAD_SIZE
        // (1 C + 2+4 TLV tamanho + 2 TLV nome + filenameLen)
        if (filenameLen > 255 || 9 + filenameLen > MAX_PAYLOAD_SIZE) {
            printf("Error: filename too long (%d).\n", filenameLen);
            fclose(file);
            return;
        }

        unsigned char fileSizeBytes[4];
        fileSizeBytes[0] = (fileSize >> 24) & 0xFF;
        fileSizeBytes[1] = (fileSize >> 16) & 0xFF;
        fileSizeBytes[2] = (fileSize >> 8)  & 0xFF;
        fileSizeBytes[3] =  fileSize        & 0xFF;
        int fileSizeNumBytes = 4;

        if (llOpenTx(llParameters) < 0) {
            printf("Error: llOpenTx failed.\n");
            fclose(file);
            return;
        }

        // C = 1
        unsigned char controlPacket[MAX_PAYLOAD_SIZE];
        int idx = 0;

        controlPacket[idx++] = 1;
        controlPacket[idx++] = 0;
        controlPacket[idx++] = fileSizeNumBytes;
        for (int i = 0; i < fileSizeNumBytes; i++) {
            controlPacket[idx++] = fileSizeBytes[i];
        }
        controlPacket[idx++] = 1;
        controlPacket[idx++] = filenameLen;
        memcpy(&controlPacket[idx], filename, filenameLen);
        idx += filenameLen;

        if (llSend(controlPacket, idx) < 0) {
            printf("Error sending START (llSend failed).\n");
            fclose(file);
            return;
        }
        printf("START sent (%d bytes).\n", idx);

        // C = 2
        int maxDataPerPacket = MAX_PAYLOAD_SIZE - 3;
        unsigned char dataPacket[MAX_PAYLOAD_SIZE];
        long bytesRemaining = fileSize;
        int packetNumber = 0;

        rewind(file);

        printf("Starting data transmission: bytesRemaining=%ld, maxDataPerPacket=%d\n",
               bytesRemaining, maxDataPerPacket);

        while (bytesRemaining > 0) {
            int chunkSize = (bytesRemaining > maxDataPerPacket)
                                ? maxDataPerPacket
                                : (int)bytesRemaining;

            int bytesRead = fread(&dataPacket[3], 1, chunkSize, file);
            if (bytesRead <= 0) {
                printf("Error reading file (data packet %d): read=%d, remaining=%ld.\n",
                       packetNumber, bytesRead, bytesRemaining);
                fclose(file);
                return;
            }

            dataPacket[0] = 2;
            dataPacket[1] = (bytesRead >> 8) & 0xFF;
            dataPacket[2] = bytesRead & 0xFF;

            if (llSend(dataPacket, 3 + bytesRead) < 0) {
                printf("Error sending Data (packet %d, %d bytes, remaining %ld).\n",
                       packetNumber, bytesRead, bytesRemaining);
                fclose(file);
                return;
            }

            bytesRemaining -= bytesRead;
            packetNumber++;
        }

        printf("Data sent: %d packets.\n", packetNumber);

        // C = 3
        controlPacket[0] = 3;

        if (llSend(controlPacket, idx) < 0) {
            printf("Error sending END (llSend failed).\n");
            fclose(file);
            return;
        }
        printf("END sent.\n");

        fclose(file);

        if (llCloseTx() < 0) {
            printf("Error: llCloseTx failed.\n");
            return;
        }
    }
    else if (strcmp(role, "rx") == 0)
    {
        if (llOpenRx(llParameters) < 0) {
            printf("Error: llOpenRx failed.\n");
            return;
        }

        // Margem de segurança: llReceive pode escrever o BCC2 (e bytes extra
        // em caso de erro) no buffer antes de o validar.
        unsigned char packet[MAX_PAYLOAD_SIZE * 2];
        FILE *file = NULL;
        long fileSize = 0;
        char receivedFilename[256] = {0};

        while (1) {
            int packetSize = llReceive(packet);
            if (packetSize < 0) {
                printf("Error: llReceive failed.\n");
                if (file != NULL) fclose(file);
                return;
            }
            if (packetSize < 1) {
                continue; // pacote vazio, ignorar
            }

            unsigned char control = packet[0];

            if (control == 1) {
                int idx = 1;
                fileSize = 0;
                receivedFilename[0] = '\0';

                while (idx < packetSize) {
                    // Precisamos de pelo menos T e L
                    if (idx + 2 > packetSize) {
                        printf("Error: truncated TLV header (idx=%d, packetSize=%d).\n",
                               idx, packetSize);
                        if (file != NULL) fclose(file);
                        return;
                    }

                    unsigned char type = packet[idx++];
                    unsigned char length = packet[idx++];

                    // O valor V tem de caber dentro do pacote
                    if (idx + length > packetSize) {
                        printf("Error: TLV (T=%d, L=%d) exceeds packet bounds (idx=%d, packetSize=%d).\n",
                               type, length, idx, packetSize);
                        if (file != NULL) fclose(file);
                        return;
                    }

                    if (type == 0) {
                        if (length > sizeof(long)) {
                            printf("Error: invalid file size with L=%d.\n", length);
                            if (file != NULL) fclose(file);
                            return;
                        }
                        fileSize = 0;
                        for (int i = 0; i < length; i++) {
                            fileSize = (fileSize << 8) | packet[idx + i];
                        }
                        idx += length;
                    }
                    else if (type == 1) {
                        // Garantir espaço para o '\0' em receivedFilename
                        if (length >= sizeof(receivedFilename)) {
                            printf("Error: filename too long (L=%d).\n", length);
                            if (file != NULL) fclose(file);
                            return;
                        }
                        memcpy(receivedFilename, &packet[idx], length);
                        receivedFilename[length] = '\0';
                        idx += length;
                    }
                    else {
                        idx += length; // tipo desconhecido — já validado acima
                    }
                }

                printf("File to receive: '%s' (%ld bytes)\n",
                       receivedFilename, fileSize);

                // Fechar ficheiro anterior se chegar um START repetido
                if (file != NULL) {
                    fclose(file);
                    file = NULL;
                }

                // Abrir estritamente com o argumento 'filename'
                file = fopen(filename, "wb");
                if (file == NULL) {
                    printf("Error: could not create file '%s'.\n", filename);
                    return;
                }
            }
            else if (control == 2) {
                if (packetSize < 3) {
                    printf("Error: truncated data packet (packetSize=%d).\n", packetSize);
                    continue;
                }

                int K = 256 * packet[1] + packet[2];

                if (K > packetSize - 3) {
                    printf("Error: K=%d exceeds packet data bounds (%d).\n",
                           K, packetSize - 3);
                    continue;
                }

                if (file != NULL) {
                    fwrite(&packet[3], 1, K, file);
                }
            }
            else if (control == 3) {
                printf("Reception complete.\n");

                if (file != NULL) {
                    fclose(file);
                    file = NULL;
                }
                break;
            }
        }

        if (llCloseRx() < 0) {
            printf("Error: llCloseRx failed.\n");
            return;
        }
    }
    else
    {
        printf("Invalid role: %s. Must be 'tx' or 'rx'.\n", role);
        return;
    }
}
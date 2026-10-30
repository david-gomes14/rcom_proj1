// RCOM 2026/2027
//
// Application layer protocol implementation

#define _POSIX_C_SOURCE 200809L // [STATS] 

#include "application_layer.h"
#include "link_layer.h"

#include <stdio.h>
#include <string.h>
#include <time.h> // [STATS]

#define START 0x01
#define DATA 0x02
#define END 0x03

#define FILE_SIZE 0x00
#define FILE_NAME 0x01

#define FILE_SIZE_NUM_BYTES 4
#define DATA_HEADER_SIZE 3

#define FALSE 0
#define TRUE 1

// Definida no link_layer.c (declarada aqui para não ser preciso mexer no .h)
void llPrintStatistics(int isTx);

// [STATS] Tempo monotónico em segundos
static double nowSeconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

// [STATS] Impressão das estatísticas da aplicação
static void printTransferStats(const char *label, long bytes, double elapsed,
                               int baudRate, int dataPackets, int controlPackets) {
    double bitrate = (elapsed > 0) ? (bytes * 8.0) / elapsed : 0;
    printf("\n========== APPLICATION STATISTICS (%s) ==========\n", label);
    printf("Data packets      : %d\n", dataPackets);
    printf("Control packets   : %d\n", controlPackets);
    printf("File size         : %ld bytes\n", bytes);
    printf("Transfer time     : %.3f s\n", elapsed);
    printf("Throughput        : %.2f bit/s\n", bitrate);
    printf("Efficiency (R/C)  : %.4f  (C = %d baud)\n", bitrate / baudRate, baudRate);
    printf("===================================================\n");
}

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

        if (fseek(file, 0, SEEK_END) != 0) {
            perror("fseek");
            fclose(file);
            return;
        }

        long fileSize = ftell(file);
        if (fileSize <= 0) {
            printf("Error: file is empty or error obtaining size.\n");
            fclose(file);
            return;
        }

        if (fseek(file, 0, SEEK_SET) != 0) {
            perror("fseek");
            fclose(file);
            return;
        }

        printf("File size '%s': %ld bytes\n", filename, fileSize);


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

        if (llOpenTx(llParameters) < 0) {
            printf("Error: llOpenTx failed.\n");
            fclose(file);
            return;
        }

        // C = START
        unsigned char controlPacket[MAX_PAYLOAD_SIZE];
        int idx = 0;

        controlPacket[idx++] = START;
        controlPacket[idx++] = FILE_SIZE;
        controlPacket[idx++] = FILE_SIZE_NUM_BYTES;

        for (int i = 0; i < FILE_SIZE_NUM_BYTES; i++) {
            controlPacket[idx++] = fileSizeBytes[i];
        }

        controlPacket[idx++] = FILE_NAME;
        controlPacket[idx++] = filenameLen;
        memcpy(&controlPacket[idx], filename, filenameLen);
        idx += filenameLen;

        double tStart = nowSeconds(); // [STATS]

        if (llSend(controlPacket, idx) < 0) {
            printf("Error sending START (llSend failed).\n");
            if (llCloseTx() < 0) {
                printf("Error: llCloseTx failed.\n");
            }
            fclose(file);
            return;
        }

        printf("START sent (%d bytes).\n", idx);

        // C = DATA
        int maxDataPerPacket = MAX_PAYLOAD_SIZE - DATA_HEADER_SIZE;
        unsigned char dataPacket[MAX_PAYLOAD_SIZE];
        long bytesRemaining = fileSize;
        int packetNumber = 0;

        printf("Starting data transmission: bytesRemaining=%ld, maxDataPerPacket=%d\n", bytesRemaining, maxDataPerPacket);

        while (bytesRemaining > 0) {
            int chunkSize = (bytesRemaining > maxDataPerPacket)
                                ? maxDataPerPacket
                                : (int)bytesRemaining;

            int bytesRead = fread(&dataPacket[3], 1, chunkSize, file);
            if (bytesRead <= 0) {
                printf("Error reading file (data packet %d): read=%d, remaining=%ld.\n", packetNumber, bytesRead, bytesRemaining);
                fclose(file);
                return;
            }

            dataPacket[0] = DATA;
            dataPacket[1] = (bytesRead >> 8) & 0xFF;
            dataPacket[2] = bytesRead & 0xFF;

            if (llSend(dataPacket, DATA_HEADER_SIZE + bytesRead) < 0) {
                printf("Error sending Data (packet %d, %d bytes, remaining %ld).\n", packetNumber, bytesRead, bytesRemaining);
                if (llCloseTx() < 0) {
                    printf("Error: llCloseTx failed.\n");
                }
                fclose(file);
                return;
            }

            bytesRemaining -= bytesRead;
            packetNumber++;
            printf("BytesRemaining=%ld\n", bytesRemaining);
        }

        printf("Data sent: %d packets.\n", packetNumber);

        // C = END
        controlPacket[0] = END;

        if (llSend(controlPacket, idx) < 0) {
            printf("Error sending END (llSend failed).\n");
            if (llCloseTx() < 0) {
                printf("Error: llCloseTx failed.\n");
            }
            fclose(file);
            return;
        }

        printf("END sent.\n");
        double tEnd = nowSeconds(); // [STATS] fim da transferência (START -> END)

        if (fclose(file) != 0) {
            perror("fclose");
            return;
        }

        if (llCloseTx() < 0) {
            printf("Error: llCloseTx failed.\n");
            return;
        }

        // [STATS]
        printTransferStats("TX", fileSize, tEnd - tStart, baudRate, packetNumber, 2);
        llPrintStatistics(TRUE);
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
        long totalReceived = 0;
        char receivedFilename[256] = {0};

        unsigned char startPacket[MAX_PAYLOAD_SIZE];
        int startPacketSize = 0;

        // [STATS]
        int dataPackets = 0, controlPackets = 0;
        double tStart = 0, tEnd = 0;

        while (TRUE) {
            int packetSize = llReceive(packet);
            if (packetSize == -2) {
                printf("Error: transfer aborted by transmitter.\n");
                if (file != NULL) {
                    fclose(file);
                    file = NULL;
                }

                if (llCloseRx() < 0) {
                    printf("Error completing disconnection.\n");
                } else {
                    printf("Connection closed after aborted transfer.\n");
                }

                return;
            }

            if (packetSize < 0) {
                printf("Error: llReceive failed.\n");
                if (file != NULL) fclose(file);
                return;
            }
            if (packetSize < 1) {
                continue; // pacote vazio, ignorar
            }

            unsigned char control = packet[0];

            if (control == START) {
                if (startPacketSize != 0) {
                    printf("Error: unexpected START packet.\n");
                    if (file != NULL) fclose(file);
                    return;
                }

                int idx = 1;
                fileSize = 0;
                int foundSize = FALSE;
                receivedFilename[0] = '\0';

                while (idx < packetSize) {
                    // Precisamos de pelo menos T e L
                    if (idx + 2 > packetSize) {
                        printf("Error: truncated TLV header (idx=%d, packetSize=%d).\n", idx, packetSize);
                        if (file != NULL) fclose(file);
                        return;
                    }

                    unsigned char type = packet[idx++];
                    unsigned char length = packet[idx++];

                    // O valor V tem de caber dentro do pacote
                    if (idx + length > packetSize) {
                        printf("Error: TLV (T=%d, L=%d) exceeds packet bounds (idx=%d, packetSize=%d).\n", type, length, idx, packetSize);
                        if (file != NULL) fclose(file);
                        return;
                    }

                    if (type == FILE_SIZE) {
                        if (length != FILE_SIZE_NUM_BYTES) {
                            printf("Error: invalid file size with L=%d.\n", length);
                            if (file != NULL) fclose(file);
                            return;
                        }

                        foundSize = TRUE;
                        fileSize = 0;
                        for (int i = 0; i < length; i++) {
                            fileSize = (fileSize << 8) | packet[idx + i];
                        }
                        idx += length;
                    }
                    else if (type == FILE_NAME) {
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

                if (!foundSize) {
                    printf("Error: START without FILE_SIZE.\n");
                    return;
                }

                printf("File to receive: '%s' (%ld bytes)\n", receivedFilename, fileSize);

                // Abrir estritamente com o argumento 'filename'
                file = fopen(filename, "wb");
                if (file == NULL) {
                    printf("Error: could not create file '%s'.\n", filename);
                    return;
                }

                if (packetSize > MAX_PAYLOAD_SIZE) {
                    printf("Error: START packet too large.\n");
                    fclose(file);
                    return;
                }
                memcpy(startPacket, packet, packetSize);
                startPacketSize = packetSize;

                controlPackets++;        // [STATS]
                tStart = nowSeconds();   // [STATS]
            }
            else if (control == DATA) {
                if (file == NULL) {
                    printf("Error: DATA received before START.\n");
                    return;
                }

                if (packetSize < DATA_HEADER_SIZE) {
                    printf("Error: truncated data packet (packetSize=%d).\n", packetSize);
                    return;
                }

                int K = 256 * packet[1] + packet[2];

                if (K != packetSize - DATA_HEADER_SIZE) {
                    printf("Error: K=%d is different to data (%d).\n", K, packetSize - DATA_HEADER_SIZE);
                    return;
                }

                if (totalReceived > fileSize || K > fileSize - totalReceived) {
                    printf("Error: received more bytes than expected.\n");
                    fclose(file);
                    return;
                }

                size_t written = fwrite(&packet[3], 1, K, file);
                if (written != (size_t)K) {
                    perror("fwrite");
                    fclose(file);
                    return;
                }
                
                totalReceived += written;
                dataPackets++; // [STATS]
            }
            else if (control == END) {
                if (startPacketSize == 0 || file == NULL) {
                    printf("Error: END received before START.\n");
                    return;
                }

                if (packetSize != startPacketSize) {
                    printf("Error: END size differs from START.\n");
                    fclose(file);
                    return;
                }

                if (memcmp(packet + 1, startPacket + 1, packetSize - 1) != 0) {
                    printf("Error: END does not match START.\n");
                    fclose(file);
                    return;
                }

                if (totalReceived != fileSize) {
                    printf("Error: received file size does not match START.\n");
                    fclose(file);
                    return;
                }

                if (fclose(file) != 0) {
                    perror("fclose");
                    file = NULL;
                    return;
                }

                file = NULL;
                controlPackets++;      // [STATS]
                tEnd = nowSeconds();   // [STATS]
                printf("Reception complete.\n");
                break;
            }
            else {
                printf("Error: unknown packet type 0x%02X.\n", control);
                if (file != NULL) fclose(file);
                return;
            }
        }

        if (llCloseRx() < 0) {
            printf("Error: llCloseRx failed.\n");
            return;
        }

        // [STATS]
        printTransferStats("RX", totalReceived, tEnd - tStart, baudRate, dataPackets, controlPackets);
        llPrintStatistics(FALSE);
    }
    else
    {
        printf("Invalid role: %s. Must be 'tx' or 'rx'.\n", role);
        return;
    }
}
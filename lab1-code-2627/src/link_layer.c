// RCOM 2026/2027
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
#define C_DISC 0x0B // Comando para fecho de ligação

#define FALSE 0

#define TRUE 1
#define SUP_FRAME_SIZE 5

typedef enum { START, FLAG_RCV, A_RCV, C_RCV, BCC_OK, STOP } frameState;

int alarmCount = 0;
int alarmEnabled = FALSE;

void alarmHandler(int signal){
    // Can be used to change a flag that increases the number of alarms
    alarmEnabled = FALSE;
    alarmCount++;
    printf("Alarm #%d received\n", alarmCount);
}

// Variáveis globais para guardar o estado da ligação
int ll_timeout = 0;
int ll_nRetransmissions = 0;
unsigned char tx_Ns = 0;          // Número de sequência enviado (0 ou 1)
unsigned char rx_expected_Ns = 0; // Número de sequência esperado (0 ou 1)

// Função auxiliar para enviar tramas de supervisão (S ou U)
int sendSupervisionFrame(unsigned char a, unsigned char c) {
    unsigned char frame[5] = {FLAG, a, c, a ^ c, FLAG};
    return writeBytesSerialPort(frame, 5);
}

// Função auxiliar genérica para ler tramas S ou U (útil para ler RR e REJ)
// Guarda o valor do campo de controlo lido no ponteiro 'c'
int readControlFrame(unsigned char expected_a, unsigned char *c) {
    frameState state = START;
    while (state != STOP) {
        unsigned char byte;
        int res = readByteSerialPort(&byte);
        
        // Se a porta for interrompida pelo alarme (timeout), retorna erro (-1)
        if (res < 0) return -1; 
        if (res == 0) continue;

        switch (state) {
            case START:
                if (byte == FLAG) state = FLAG_RCV;
                break;
            case FLAG_RCV:
                if (byte == expected_a) state = A_RCV;
                else if (byte == FLAG) state = FLAG_RCV;
                else state = START;
                break;
            case A_RCV:
                *c = byte; // Guardar o C (que pode ser RR0, RR1, REJ0, REJ1, etc...)
                state = C_RCV;
                break;
            case C_RCV:
                if (byte == (expected_a ^ *c)) state = BCC_OK;
                else if (byte == FLAG) state = FLAG_RCV;
                else state = START;
                break;
            case BCC_OK:
                if (byte == FLAG) state = STOP;
                else state = START;
                break;
            default: break;
        }
    }
    return 0;
}

////////////////////////////////////////////////
// LLOPEN
////////////////////////////////////////////////
int llOpenTx(LinkLayer llParameters) {
    if (openSerialPort(llParameters.serialPort, llParameters.baudRate) < 0) {
        perror("openSerialPort");
        return -1;
    }
    printf("Serial port %s opened\n", llParameters.serialPort);
    
    // Guardar configurações da camada de aplicação
    ll_timeout = llParameters.timeout;
    ll_nRetransmissions = llParameters.nRetransmissions;
    tx_Ns = 0;

    struct sigaction act = {0};
    act.sa_handler = &alarmHandler;
    if (sigaction(SIGALRM, &act, NULL) == -1) {
        perror("sigaction");
        closeSerialPort();
        return -1;
    }

    int connected = FALSE;
    alarmCount = 0;
    alarmEnabled = FALSE;

    // Ciclo de envio do SET com temporizador
    while (alarmCount <= ll_nRetransmissions && !connected) {
        if (!alarmEnabled) {
            sendSupervisionFrame(A_TX, C_SET); // Envia SET (A=0x03)
            printf("SET sent\n");
            alarm(ll_timeout);
            alarmEnabled = TRUE;
        }

        unsigned char c_rcv;
        // Fica à espera da resposta e valida-a
        if (readControlFrame(A_TX, &c_rcv) == 0) { 
            if (c_rcv == C_UA) { // Recebeu UA (A=0x03)
                connected = TRUE;
                alarm(0); // Desliga alarme
                alarmEnabled = FALSE;
                printf("UA received. Connection established\n");
            }
        }
    }

    if (!connected) {
        closeSerialPort();
        return -1; // Falha
    }
    
    return 0;
}

int llOpenRx(LinkLayer llParameters) {
    if (openSerialPort(llParameters.serialPort, llParameters.baudRate) < 0) {
        perror("openSerialPort");
        return -1;
    }
    printf("Serial port %s opened\n", llParameters.serialPort);

    ll_timeout = llParameters.timeout;
    ll_nRetransmissions = llParameters.nRetransmissions;
    rx_expected_Ns = 0;

    unsigned char c_rcv;
    int connected = FALSE;

    // Fica passivamente à espera do SET
    while (!connected) {
        if (readControlFrame(A_TX, &c_rcv) == 0) {
            if (c_rcv == C_SET) {
                sendSupervisionFrame(A_TX, C_UA); // Responde com UA
                printf("SET received, UA sent. Connection established\n");
                connected = TRUE;
            }
        }
    }
    
    return 0;
}

////////////////////////////////////////////////
// LLSEND
////////////////////////////////////////////////
int llSend(const unsigned char *buf, int bufSize) {
    // Alocar espaço para a trama com base no pior cenário (dobro do tamanho)
    int max_frame_size = 5 + (bufSize + 1) * 2; 
    unsigned char* frame = malloc(max_frame_size);
    
    // Construir o Cabeçalho
    frame[0] = FLAG;
    frame[1] = A_TX; // 0x03
    frame[2] = (tx_Ns == 0) ? 0x00 : 0x80; // Alternar número de sequência
    frame[3] = frame[1] ^ frame[2];        // Cálculo do BCC1
    
    // Calcular BCC2 (antes do stuffing)
    unsigned char bcc2 = 0;
    for(int i = 0; i < bufSize; i++) {
        bcc2 ^= buf[i];
    }
    
    // Copiar carga útil (payload) aplicando o Byte Stuffing
    int idx = 4;
    for(int i = 0; i < bufSize; i++) {
        if(buf[i] == FLAG || buf[i] == 0x7D) {
            frame[idx++] = 0x7D;
            frame[idx++] = buf[i] ^ 0x20;
        } else {
            frame[idx++] = buf[i];
        }
    }
    
    // Aplicar Byte Stuffing ao BCC2 e fechar a trama
    if(bcc2 == FLAG || bcc2 == 0x7D) {
        frame[idx++] = 0x7D;
        frame[idx++] = bcc2 ^ 0x20;
    } else {
        frame[idx++] = bcc2;
    }
    frame[idx++] = FLAG; //Fim de trama
    
    int frame_size = idx;

    // Ciclo Stop & Wait: RR ou REJ
    int accepted = FALSE;
    alarmCount = 0;
    alarmEnabled = FALSE;

    // Calcular as respostas esperadas para a próxima trama e trama atual
    unsigned char expected_rr = (tx_Ns == 0) ? 0xAB : 0xAA; // 0xAB = RR1 ou 0xAA = RR0
    unsigned char expected_rej = (tx_Ns == 0) ? 0x54 : 0x55; // 0x54 = REJ0 ou 0x55 = REJ1
    
    while (alarmCount <= ll_nRetransmissions && !accepted) {
        // Enviar trama
        if (!alarmEnabled) { 
            writeBytesSerialPort(frame, frame_size);
            alarm(ll_timeout); // Ativar alarme
            alarmEnabled = TRUE;
        }
        
        unsigned char c_rcv = 0;
        // Esperar e ler a resposta
        if (readControlFrame(A_TX, &c_rcv) == 0) {
            if (c_rcv == expected_rr) { // Recepção bem sucedida!
                accepted = TRUE;
                alarm(0); // Desligar alarme
                printf("chegou aqui");
                tx_Ns = (tx_Ns + 1) % 2; // Avançar para a próxima trama (0->1 ou 1->0)
            } else if (c_rcv == expected_rej) { // Trama com erros
                alarm(0); // Cancelar alarme para retransmitir imediatamente
                alarmEnabled = FALSE;
            }
        }
    }
    
    free(frame);
    if (!accepted) return -1; // Falha por excesso de retransmissões
    return frame_size;
}

////////////////////////////////////////////////
// LLRECEIVE
////////////////////////////////////////////////
int llReceive(unsigned char *packet) {
    // Loop infinito até ser recebida uma trama válida E nova
    while (1) {
        frameState state = START;
        unsigned char a, c;
        int payload_idx = 0;
        int escape = FALSE;
        
        // Máquina de estados para Trama I
        while (state != STOP) {
            unsigned char byte;
            if (readByteSerialPort(&byte) <= 0) continue;
            
            switch(state) {
                case START: 
                    if (byte == FLAG) state = FLAG_RCV; 
                    break;
                case FLAG_RCV: 
                    if (byte == A_TX) { a = byte; state = A_RCV; } // Tx envia A_TX=0x03
                    else if (byte != FLAG) state = START;
                    break;
                case A_RCV: 
                    if (byte == 0x00 || byte == 0x80) { c = byte; state = C_RCV; }
                    else if (byte == FLAG) state = FLAG_RCV;
                    else state = START; // Ignora se não for trama I
                    break;
                case C_RCV:
                    if (byte == (a ^ c)) state = BCC_OK;
                    else if (byte == FLAG) state = FLAG_RCV;
                    else state = START;
                    break;
                case BCC_OK: // Começam os dados
                    if (byte == FLAG) {
                        state = STOP; // Terminou a trama
                    } else if (byte == 0x7D) {
                        escape = TRUE; // Próximo byte sofre destuffing
                    } else {
                        if (escape) {
                            packet[payload_idx++] = byte ^ 0x20; // Destuffing em tempo real
                            escape = FALSE;
                        } else {
                            packet[payload_idx++] = byte;
                        }
                    }
                    break;
                default: break;
            }
        }
        
        // Verificar se os dados têm tamanho suficiente
        if (payload_idx < 1) continue; 
        
        // O último byte do pacote "Destuffed" é o BCC2
        unsigned char bcc2_received = packet[payload_idx - 1];
        unsigned char bcc2_calc = 0;
        
        // Calcular o BCC2 sob a carga útil de forma recursiva
        for (int i = 0; i < payload_idx - 1; i++) {
            bcc2_calc ^= packet[i];
        }
        
        // O valor do sequencial recebido
        unsigned char n_s = (c == 0x80) ? 1 : 0;
        
        // Tesstar os cenários:
        // 1º Cenário - Dados com Erro (BCC2 inválido) -> Enviar REJ
        if (bcc2_calc != bcc2_received) {
            unsigned char rej_c = (n_s == 0) ? 0x54 : 0x55;
            sendSupervisionFrame(A_TX, rej_c); // A=0x03 para respostas do Rx
            continue; // Voltar ao início e esperar que o Emissor retransmita
        }
        
        // 2º Cenário: Trama Correta E Esperada -> Enviar RR e entregar os dados
        if (n_s == rx_expected_Ns) {
            rx_expected_Ns = (rx_expected_Ns + 1) % 2; // Alternar a que esperamos (0->1, 1->0)
            unsigned char rr_c = (rx_expected_Ns == 0) ? 0xAA : 0xAB;
            sendSupervisionFrame(A_TX, rr_c);
            
            // Retorna o tamanho do buffer de dados menos o byte do BCC2
            return payload_idx - 1; 
        } 
        // 3º Cenário: Trama Correta mas DUPLICADA -> Enviar RR mas descartar dados
        else {
            unsigned char rr_c = (rx_expected_Ns == 0) ? 0xAA : 0xAB; // RR pede sempre a próxima
            sendSupervisionFrame(A_TX, rr_c);
            // Ignora o pacote (continue). O While faz com que fiquemos à espera da trama correta
            continue; 
        }
    }
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
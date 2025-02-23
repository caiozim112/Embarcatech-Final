#include "pico/stdlib.h" // Biblioteca padrão do Pico para funções básicas
#include "hardware/adc.h"
#include "hardware/pwm.h" // Biblioteca para controle de PWM (Modulação por Largura de Pulso)
#include "hardware/pio.h" // Biblioteca para controle de PIO (Entrada/Saída Paralela)
#include "hardware/clocks.h" // Biblioteca para controle de clocks
#include <stdio.h> // Biblioteca padrão de entrada/saída
#include <stdlib.h> // Biblioteca padrão para funções utilitárias

#include "ws2818b.pio.h" // Programa PIO para controle de LEDs WS2812B

// Definições de pinos e constantes
#define PINO_BUZZER 21 // Pino do buzzer
#define PINO_LED 13 // Pino do LED indicador
#define PINO_MICROFONE 28 // Pino do microfone
#define PINO_MATRIZ_LED 7 // Pino da matriz de LEDs
#define PINO_LED_RGB 11 // Pino do LED RGB
#define LIMIAR_RUIDO 50 // Limiar de ruído para detecção
#define TAMANHO_AMOSTRA 10 // Tamanho do buffer de amostras do ADC
#define INTERVALO_BUZZER 2000000 // Intervalo de tempo para o buzzer
#define AMOSTRAS_CALIBRACAO 50 // Número de amostras para calibração do microfone
#define NUMERO_LEDS 25 // Número de LEDs na matriz
#define TEMPO_DEBOUNCE 500 // Tempo de debounce em milissegundos
#define DURACAO_DETECCAO 1500 // Duração da detecção ativa em milissegundos
#define GANHO_AMPLITUDE 1.3 // Ganho aplicado à amplitude do sinal
#define FATOR_REDUCAO_POTENCIA 0.05 // Fator de redução de potência para LEDs

uint16_t OFFSET_MIC = 2048; // Offset inicial do microfone
PIO np_pio; // Variável para o PIO usado
uint sm; // Variável para o state machine do PIO

// Estrutura para representar um pixel RGB
struct pixel_t {
    uint8_t G, R, B; // Componentes de cor: Verde, Vermelho, Azul
};
typedef struct pixel_t pixel_t; // Definição de tipo para pixel_t
typedef pixel_t npLED_t; // Definição de tipo para npLED_t
npLED_t leds[NUMERO_LEDS]; // Array de LEDs

// Função para inicializar o PWM do buzzer
void inicializar_pwm_buzzer(uint pino) {
    gpio_set_function(pino, GPIO_FUNC_PWM); // Configura o pino para função PWM
    uint slice_num = pwm_gpio_to_slice_num(pino); // Obtém o número do slice PWM
    uint32_t clock = 125000000; // Frequência do clock
    uint32_t divisor16 = clock / 8 / 4096 + (clock % (8 * 4096) != 0); // Calcula o divisor
    pwm_set_clkdiv_int_frac(slice_num, divisor16 / 16, divisor16 & 0xF); // Configura o divisor do clock
    pwm_set_wrap(slice_num, 4095); // Configura o valor de wrap do PWM
    pwm_set_enabled(slice_num, true); // Habilita o PWM
}

// Função para ligar o buzzer
void ligar_buzzer(uint pino) {
    pwm_set_gpio_level(pino, 2048); // Define o nível PWM para ligar o buzzer
}

// Função para desligar o buzzer
void desligar_buzzer(uint pino) {
    pwm_set_gpio_level(pino, 0); // Define o nível PWM para desligar o buzzer
}

// Função para inicializar a matriz de LEDs
void inicializar_np(uint pino) {
    uint offset = pio_add_program(pio0, &ws2818b_program); // Adiciona o programa PIO
    np_pio = pio0; // Define o PIO usado
    sm = pio_claim_unused_sm(np_pio, true); // Reivindica uma state machine não utilizada
    ws2818b_program_init(np_pio, sm, offset, pino, 800000.f); // Inicializa o programa PIO
    for (uint i = 0; i < NUMERO_LEDS; ++i) { // Inicializa todos os LEDs como apagados
        leds[i].R = 0;
        leds[i].G = 0;
        leds[i].B = 0;
    }
}

// Função para definir a cor de um LED específico
void definirLED(uint indice, uint8_t r, uint8_t g, uint8_t b) {
    leds[indice].R = r; // Define o componente vermelho
    leds[indice].G = g; // Define o componente verde
    leds[indice].B = b; // Define o componente azul
}

// Função para apagar todos os LEDs
void limpar_np() {
    for (uint i = 0; i < NUMERO_LEDS; ++i)
        definirLED(i, 0, 0, 0); // Define todos os LEDs como apagados
}

// Função para enviar os dados de cor para a matriz de LEDs
void escrever_np() {
    for (uint i = 0; i < NUMERO_LEDS; ++i) {
        pio_sm_put_blocking(np_pio, sm, leds[i].G); // Envia o componente verde
        pio_sm_put_blocking(np_pio, sm, leds[i].R); // Envia o componente vermelho
        pio_sm_put_blocking(np_pio, sm, leds[i].B); // Envia o componente azul
    }
}

// Função para calibrar o microfone
void calibrar_microfone() {
    uint32_t soma_offset = 0; // Variável para somar os valores de offset
    for (int i = 0; i < AMOSTRAS_CALIBRACAO; i++) {
        soma_offset += adc_read(); // Lê o valor do ADC e soma
        sleep_ms(10); // Espera 10 ms entre as leituras
    }
    OFFSET_MIC = soma_offset / AMOSTRAS_CALIBRACAO; // Calcula o offset médio
    printf("OFFSET_MIC: %d\n", OFFSET_MIC); // Imprime o offset calculado
}

// Função principal
int main() {
    stdio_init_all(); // Inicializa a entrada e saída padrão
    sleep_ms(1000); // Espera 1 segundo

    adc_init(); // Inicializa o ADC
    adc_gpio_init(PINO_MICROFONE); // Configura o pino do microfone como entrada ADC
    adc_select_input(2); // Seleciona a entrada ADC 2

    gpio_init(PINO_LED); // Inicializa o pino do LED
    gpio_set_dir(PINO_LED, GPIO_OUT); // Define o pino do LED como saída
    gpio_init(PINO_LED_RGB); // Inicializa o pino do LED RGB
    gpio_set_dir(PINO_LED_RGB, GPIO_OUT); // Define o pino do LED RGB como saída
    gpio_put(PINO_LED_RGB, 1); // Liga o LED RGB

    inicializar_pwm_buzzer(PINO_BUZZER); // Inicializa o PWM do buzzer

    inicializar_np(PINO_MATRIZ_LED); // Inicializa a matriz de LEDs
    limpar_np(); // Apaga todos os LEDs
    escrever_np(); // Envia os dados para a matriz de LEDs

    calibrar_microfone(); // Calibra o microfone

    uint16_t valores_adc[TAMANHO_AMOSTRA] = {0}; // Buffer para valores do ADC
    uint8_t indice = 0; // Índice para o buffer de amostras
    uint32_t tempo_inicio_deteccao = 0; // Tempo de início da detecção
    bool detectando = false; // Flag para indicar se está detectando ruído
    uint32_t ultimo_tempo_deteccao = 0; // Último tempo de detecção

    while (true) { // Loop infinito
        valores_adc[indice] = adc_read(); // Lê o valor do ADC
        indice = (indice + 1) % TAMANHO_AMOSTRA; // Atualiza o índice circularmente

        uint32_t soma = 0; // Variável para somar os valores do buffer
        for (uint8_t i = 0; i < TAMANHO_AMOSTRA; i++) {
            soma += valores_adc[i]; // Soma os valores do buffer
        }
        uint16_t valor_adc_medio = soma / TAMANHO_AMOSTRA; // Calcula o valor médio do ADC
        int16_t amplitude = abs((int16_t)valor_adc_medio - OFFSET_MIC) * GANHO_AMPLITUDE; // Calcula a amplitude do sinal

        printf("Amplitude: %d\n", amplitude); // Imprime a amplitude

        if (time_us_32() - ultimo_tempo_deteccao >= TEMPO_DEBOUNCE * 1000) { // Verifica o tempo de debounce
            if (amplitude > LIMIAR_RUIDO && !detectando) { // Se a amplitude exceder o limiar e não estiver detectando
                detectando = true; // Define a flag de detecção como verdadeira
                tempo_inicio_deteccao = time_us_32(); // Armazena o tempo de início da detecção
                gpio_put(PINO_LED, 1); // Liga o LED indicador
                gpio_put(PINO_LED_RGB, 0); // Desliga o LED RGB
                limpar_np(); // Apaga todos os LEDs
                // Acende LEDs específicos em vermelho
                definirLED(24, (uint8_t)(255 * FATOR_REDUCAO_POTENCIA), 0, 0);
                definirLED(20, (uint8_t)(255 * FATOR_REDUCAO_POTENCIA), 0, 0);
                definirLED(18, (uint8_t)(255 * FATOR_REDUCAO_POTENCIA), 0, 0);
                definirLED(16, (uint8_t)(255 * FATOR_REDUCAO_POTENCIA), 0, 0);
                definirLED(12, (uint8_t)(255 * FATOR_REDUCAO_POTENCIA), 0, 0);
                definirLED(8, (uint8_t)(255 * FATOR_REDUCAO_POTENCIA), 0, 0);
                definirLED(6, (uint8_t)(255 * FATOR_REDUCAO_POTENCIA), 0, 0);
                definirLED(4, (uint8_t)(255 * FATOR_REDUCAO_POTENCIA), 0, 0);
                definirLED(0, (uint8_t)(255 * FATOR_REDUCAO_POTENCIA), 0, 0);
                definirLED(12, (uint8_t)(255 * FATOR_REDUCAO_POTENCIA), 0, 0);
                escrever_np(); // Envia os dados para a matriz de LEDs
                ligar_buzzer(PINO_BUZZER); // Liga o buzzer
            } else if (!detectando) { // Se não estiver detectando
                limpar_np(); // Apaga todos os LEDs
                for (uint i = 0; i < NUMERO_LEDS; ++i) {
                    definirLED(i, 0, (uint8_t)(255 * FATOR_REDUCAO_POTENCIA), 0); // Acende todos os LEDs em verde
                }
                // Apaga LEDs específicos
                definirLED(0, 0, 0, 0);
                definirLED(1, 0, 0, 0);
                definirLED(3, 0, 0, 0);
                definirLED(4, 0, 0, 0);
                definirLED(5, 0, 0, 0);
                definirLED(7, 0, 0, 0);
                definirLED(9, 0, 0, 0);
                definirLED(11, 0, 0, 0);
                definirLED(12, 0, 0, 0);
                definirLED(13, 0, 0, 0);
                definirLED(16, 0, 0, 0);
                definirLED(18, 0, 0, 0);
                definirLED(20, 0, 0, 0);
                definirLED(22, 0, 0, 0);
                definirLED(24, 0, 0, 0);
                escrever_np(); // Envia os dados para a matriz de LEDs
            }
        }

        if (detectando && (time_us_32() - tempo_inicio_deteccao >= DURACAO_DETECCAO * 1000)) { // Se a detecção estiver ativa e o tempo de detecção tiver passado
            detectando = false; // Define a flag de detecção como falsa
            gpio_put(PINO_LED, 0); // Desliga o LED indicador
            gpio_put(PINO_LED_RGB, 1); // Liga o LED RGB
            limpar_np(); // Apaga todos os LEDs
            escrever_np(); // Envia os dados para a matriz de LEDs
            desligar_buzzer(PINO_BUZZER); // Desliga o buzzer
            ultimo_tempo_deteccao = time_us_32(); // Atualiza o último tempo de detecção
        }

        sleep_ms(100); // Espera 100 ms antes de repetir o loop
    }
    return 0; // Retorna 0 (nunca alcançado devido ao loop infinito)
}
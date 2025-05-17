#include <stdio.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "inc/ssd1306.h"
#include "hardware/i2c.h"
#include "hardware/adc.h"
#include "string.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"
#include "ws2818b.pio.h" // Biblioteca gerada pelo arquivo .pio durante compilação.

// Definição do número de LEDs e pino.
#define LED_COUNT 25
#define LED_PIN 7

int estado = 1;
bool BTN_ESTADO = 0;         
bool BTN_ANTERIOR = 0;   

const uint I2C_SDA = 14;
const uint I2C_SCL = 15;

const int BTN = 22;           // Pino de leitura do botão do joystick
const uint LED_G = 11;        // Pino para controle do LED verde
const uint LED_R = 13;        // Pino para controle do LED vermelho 
bool GR = false;              // Giro reconhecido
bool HO = false;              // Horário permitido
bool DI = false;              // Dia permitido
bool PT = true;               // Portaria permitida (true pois o padrão é que inicialmente não esteja permitido)

void display_message(const char *lines[], int line_count);
int leitura_BTN();
void atualizar_matriz_leds();
void npInit(uint pin);
void npSetLED(const uint index, const uint8_t r, const uint8_t g, const uint8_t b);
void npClear();
void npWrite();

// Definição de pixel GRB
struct pixel_t {
    uint8_t G, R, B;
};
typedef struct pixel_t pixel_t;
typedef pixel_t npLED_t;

npLED_t leds[LED_COUNT];

PIO np_pio;
uint sm;

int main() {

    stdio_init_all();

    i2c_init(i2c1, ssd1306_i2c_clock * 1000);
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);

    gpio_init(BTN);
    gpio_set_dir(BTN, GPIO_IN);
    gpio_pull_up(BTN);
    adc_init();
    adc_gpio_init(27);
    gpio_init(LED_G);
    gpio_set_dir(LED_G, GPIO_OUT);
    gpio_put(LED_G, 0);

    gpio_init(LED_R);
    gpio_set_dir(LED_R, GPIO_OUT);
    gpio_put(LED_R, 0);

    ssd1306_init();

    npInit(LED_PIN);
    npClear();
    atualizar_matriz_leds();

    while (1) {

    // Seleciona o canal 0 do ADC (eixo Y do joystick)
    adc_select_input(0); 
    uint adc_y_raw = adc_read();  // Lê valor analógico do joystick (eixo Y)

    // Verifica se o usuário moveu o joystick para cima
    if (adc_y_raw > 3000) {
        estado -= 1; // Vai para a opção anterior
        if (estado < 1) { // Se passar do limite superior, volta para a última opção
            estado = 4;
        }
        sleep_ms(250); // Pequena pausa para evitar múltiplas leituras rápidas
    }
    // Verifica se o usuário moveu o joystick para baixo
    else if (adc_y_raw < 100) {
        estado += 1; // Vai para a próxima opção
        if (estado > 4) { // Se passar do limite inferior, volta para a primeira opção
            estado = 1;
        }
        sleep_ms(250); // Pausa para debounce do joystick
    }

    // Lê o estado atual do botão
    BTN_ESTADO = leitura_BTN();

    // Atualiza o LED RGB que representa a saida do circuito lógico (catraca permitida)
    if (!PT || (GR && HO && DI)) { // Catraca permitida
        gpio_put(LED_G, 1);  // Liga LED verde 
        gpio_put(LED_R, 0);  // Desliga LED vermelho
    } else {
        gpio_put(LED_G, 0);  // Desliga LED verde
        gpio_put(LED_R, 1);  // Liga LED vermelho
    }

    // Lógica de navegação e seleção do menu
    switch (estado) {
        case 1: {
            // Exibe o menu com "GR" destacado
            const char *msg1[] = { "1 GR", "\n", "  HO", "\n", "  DI", "\n", "  PT" };
            display_message(msg1, 7);
            if (BTN_ESTADO && !BTN_ANTERIOR) {
                GR = !GR; // Alterna o estado da entrada GR
                atualizar_matriz_leds(); // Atualiza a cor do LED correspondente
            }
            break;
        }
        case 2: {
            // Exibe o menu com "HO" destacado
            const char *msg2[] = { "  GR", "\n", "2 HO", "\n", "  DI", "\n", "  PT" };
            display_message(msg2, 7);
            if (BTN_ESTADO && !BTN_ANTERIOR) {
                HO = !HO; // Alterna o estado da entrada HO
                atualizar_matriz_leds();
            }
            break;
        }
        case 3: {
            // Exibe o menu com "DI" destacado
            const char *msg3[] = { "  GR", "\n", "  HO", "\n", "3 DI", "\n", "  PT" };
            display_message(msg3, 7);
            if (BTN_ESTADO && !BTN_ANTERIOR) {
                DI = !DI; // Alterna o estado da entrada DI
                atualizar_matriz_leds();
            }
            break;
        }
        case 4: {
            // Exibe o menu com "PT" destacado
            const char *msg4[] = { "  GR", "\n", "  HO", "\n", "  DI", "\n", "4 PT" };
            display_message(msg4, 7);
            if (BTN_ESTADO && !BTN_ANTERIOR) {
                PT = !PT; // Alterna o estado da entrada PT
                atualizar_matriz_leds();
            }
            break;
        }
    }

    // Armazena o estado do botão para detectar borda de subida na próxima iteração
    BTN_ANTERIOR = BTN_ESTADO;
}
}
// funções
void npInit(uint pin) {
    uint offset = pio_add_program(pio0, &ws2818b_program);
    np_pio = pio0;

    sm = pio_claim_unused_sm(np_pio, false);
    if (sm < 0) {
        np_pio = pio1;
        sm = pio_claim_unused_sm(np_pio, true);
    }

    ws2818b_program_init(np_pio, sm, offset, pin, 800000.f);

    for (uint i = 0; i < LED_COUNT; ++i) {
        leds[i].R = 0;
        leds[i].G = 0;
        leds[i].B = 0;
    }
}

void npSetLED(const uint index, const uint8_t r, const uint8_t g, const uint8_t b) {
    leds[index].R = r;
    leds[index].G = g;
    leds[index].B = b;
}

void npClear() {
    for (uint i = 0; i < LED_COUNT; ++i)
        npSetLED(i, 0, 0, 0);
}

void npWrite() {
    for (uint i = 0; i < LED_COUNT; ++i) {
        pio_sm_put_blocking(np_pio, sm, leds[i].G);
        pio_sm_put_blocking(np_pio, sm, leds[i].R);
        pio_sm_put_blocking(np_pio, sm, leds[i].B);
    }
    sleep_us(100);
}

void display_message(const char *lines[], int line_count) {
    struct render_area frame_area = {
        .start_column = 0,
        .end_column = ssd1306_width - 1,
        .start_page = 0,
        .end_page = ssd1306_n_pages - 1
    };

    calculate_render_area_buffer_length(&frame_area);

    uint8_t ssd[ssd1306_buffer_length];
    memset(ssd, 0, ssd1306_buffer_length);

    int y = 0;
    for (int i = 0; i < line_count; i++) {
        ssd1306_draw_string(ssd, 5, y, (char *) lines[i]); 
        y += 8;
    }

    render_on_display(ssd, &frame_area);
}

int leitura_BTN() {
    return !gpio_get(BTN);
}

void atualizar_matriz_leds() {
    npSetLED(4, GR ? 0 : 100, GR ? 100 : 0, 0);
    npSetLED(3, HO ? 0 : 100, HO ? 100 : 0, 0);
    npSetLED(2, DI ? 0 : 100, DI ? 100 : 0, 0);
    npSetLED(1, PT ? 0 : 100, PT ? 100 : 0, 0);

    npWrite();
}
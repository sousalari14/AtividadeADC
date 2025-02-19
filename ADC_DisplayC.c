#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "hardware/i2c.h"
#include "hardware/pwm.h"     
#include "lib/ssd1306.h"

#define I2C_PORT i2c1
#define I2C_SDA 14
#define I2C_SCL 15
#define endereco 0x3C
#define JOYSTICK_X_PIN 26  
#define JOYSTICK_Y_PIN 27  
#define JOYSTICK_PB 22 
#define LED_PIN_BLUE 12 
#define LED_PIN_RED 13 
#define LED_PIN_GREEN 11 

#include "pico/bootrom.h"
#define botaoB 6

volatile bool led_green_state = false;  
volatile uint32_t last_interrupt_time = 0;  

// Função que lida com o botão B
void gpio_irq_handler(uint gpio, uint32_t events) {
    reset_usb_boot(0, 0);
}

// Função que inicializa o PWM em um pino GPIO específico
uint pwm_init_gpio(uint gpio, uint wrap) {
    gpio_set_function(gpio, GPIO_FUNC_PWM);  // Configura o pino para função PWM
    uint slice_num = pwm_gpio_to_slice_num(gpio);  
    pwm_set_wrap(slice_num, wrap);  
    pwm_set_enabled(slice_num, true); 
    return slice_num;
}

// Função que lida com o botão do joystick (Botão PB) e alterna o estado do LED verde
void joystick_button_irq_handler(uint gpio, uint32_t events) {
    uint32_t current_time = to_ms_since_boot(get_absolute_time());  // Tempo atual
    if (current_time - last_interrupt_time < 800) return;  // Debounce
    last_interrupt_time = current_time;  // Atualiza o tempo da última interrupção

    // Alterna o estado do LED verde
    led_green_state = !led_green_state;
    pwm_set_gpio_level(LED_PIN_GREEN, led_green_state ? 255 : 0);  // Liga ou desliga o LED verde
}

// Função para desenhar bordas no display OLED
void draw_borders(ssd1306_t *ssd, bool is_double_border) {
    if (is_double_border) {
        // Desenha uma borda dupla
        ssd1306_rect(ssd, 0, 0, 128, 64, true, false);
        ssd1306_rect(ssd, 2, 2, 124, 60, true, false);
    } else {
        // Desenha uma borda simples
        ssd1306_rect(ssd, 0, 0, 128, 64, true, false);
    }
}

int main() {
    // Inicializa o botão B e configura interrupção
    gpio_init(botaoB);
    gpio_set_dir(botaoB, GPIO_IN);
    gpio_pull_up(botaoB);
    gpio_set_irq_enabled_with_callback(botaoB, GPIO_IRQ_EDGE_FALL, true, &gpio_irq_handler);

    // Inicializa o botão do joystick para controlar o LED verde
    gpio_init(JOYSTICK_PB);
    gpio_set_dir(JOYSTICK_PB, GPIO_IN);
    gpio_pull_up(JOYSTICK_PB); 
    gpio_set_irq_enabled_with_callback(JOYSTICK_PB, GPIO_IRQ_EDGE_FALL, true, &joystick_button_irq_handler);

    // Inicializa o barramento I2C para comunicação com o display OLED
    i2c_init(I2C_PORT, 400 * 1000);
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);

    // Inicializa o display OLED
    ssd1306_t ssd;
    ssd1306_init(&ssd, WIDTH, HEIGHT, false, endereco, I2C_PORT);
    ssd1306_config(&ssd);
    ssd1306_send_data(&ssd);
    ssd1306_fill(&ssd, false);
    ssd1306_send_data(&ssd);

    // Inicializa o ADC para ler os valores do joystick
    adc_init();
    adc_gpio_init(JOYSTICK_X_PIN);
    adc_gpio_init(JOYSTICK_Y_PIN);  

    // Inicializa o PWM nos pinos de LED
    uint pwm_wrap = 4095;  
    pwm_init_gpio(LED_PIN_BLUE, pwm_wrap);  // PWM para LED Azul
    pwm_init_gpio(LED_PIN_RED, pwm_wrap);   // PWM para LED Vermelho
    pwm_init_gpio(LED_PIN_GREEN, pwm_wrap); // PWM para LED Verde

    // Valores iniciais para as posições do joystick e o quadrado na tela
    uint16_t pos_central_x = 2121;  
    uint16_t pos_central_y = 2035;  
    int square_x = 28;  
    int square_y = 60;  

    uint16_t dead_zone = 100;  

    while (true) {
        // Lê os valores do joystick
        adc_select_input(0);
        uint16_t adc_value_x = adc_read();
        adc_select_input(1);
        uint16_t adc_value_y = adc_read();

        // Calcula o movimento do quadrado com base na diferença entre o valor lido e a posição central
        int movement_x = (abs(adc_value_x - pos_central_x) > dead_zone) ? (adc_value_x - pos_central_x) / 150 : 0;
        int movement_y = (abs(adc_value_y - pos_central_y) > dead_zone) ? (adc_value_y - pos_central_y) / 150 : 0;

        // Se o joystick estiver em repouso, centraliza o quadrado
        if (movement_x == 0 && movement_y == 0) {
            square_x = 28;  
            square_y = 60;
        } else {
            square_x -= movement_x;
            square_y += movement_y;
        }

        // Garante que o quadrado não saia da tela
        if (square_x < 0) square_x = 0;
        if (square_x > 56) square_x = 56;
        if (square_y < 0) square_y = 0;
        if (square_y > 120) square_y = 120;

        // Atualiza a tela OLED com os dados atuais
        ssd1306_fill(&ssd, false);
        draw_borders(&ssd, led_green_state);  
        ssd1306_rect(&ssd, square_x, square_y, 8, 8, true, true);  // Desenha o quadrado no display

        ssd1306_send_data(&ssd);

        // Controla o brilho do LED Vermelho
        uint16_t vrx_value_x = adc_value_y;
        uint16_t led_intensity_red = (vrx_value_x > pos_central_x + dead_zone) ? 
            (vrx_value_x - (pos_central_x + dead_zone)) * 255 / pos_central_x : 
            (vrx_value_x < pos_central_x - dead_zone) ? (pos_central_x - dead_zone - vrx_value_x) * 255 / pos_central_x : 0;
        pwm_set_gpio_level(LED_PIN_RED, led_intensity_red);

        // Controla o brilho do LED Azul
        uint16_t vrx_value_y = adc_value_x;
        uint16_t led_intensity_blue = (vrx_value_y > pos_central_y + dead_zone) ? 
            (vrx_value_y - (pos_central_y + dead_zone)) * 255 / pos_central_y : 
            (vrx_value_y < pos_central_y - dead_zone) ? (pos_central_y - dead_zone - vrx_value_y) * 255 / pos_central_y : 0;
        pwm_set_gpio_level(LED_PIN_BLUE, led_intensity_blue);

        sleep_ms(100);
    }
}


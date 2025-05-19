#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "pico/bootrom.h"
#include "hardware/adc.h"
#include "hardware/i2c.h"
#include "hardware/pwm.h"
#include "hardware/pio.h"
#include "ws2812.pio.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "lib/ssd1306.h"
#include "lib/font.h"
#include <stdio.h>

// Definições de pinos
#define I2C_PORT i2c1
#define I2C_SDA 14
#define I2C_SCL 15
#define OLED_ADDR 0x3C
#define ADC_JOYSTICK_X 26  // Simula nível de água
#define ADC_JOYSTICK_Y 27  // Simula volume de chuva

#define LED_GREEN 11
#define LED_BLUE 12
#define LED_RED 13
 
#define BUZZER_PIN 10
#define WS2812_PIN 7
#define NUM_PIXELS 25      // Matriz 5x5
#define BTN_B 6

// Definições de limites
#define WATER_LEVEL_WARNING 50    // 50% do nível máximo
#define WATER_LEVEL_ALERT 70      // 70% do nível máximo
#define WATER_LEVEL_CRITICAL 85   // 85% do nível máximo
#define RAIN_VOLUME_WARNING 60    // 60% do volume máximo
#define RAIN_VOLUME_ALERT 80      // 80% do volume máximo
#define RAIN_VOLUME_CRITICAL 90   // 90% do volume máximo

// Definições de estados do sistema
typedef enum {
    NORMAL_MODE,
    WARNING_MODE,
    ALERT_MODE,
    CRITICAL_MODE
} SystemMode;

// Estrutura para dados do sensor
typedef struct {
    uint16_t water_level;      // Nível da água (0-100%)
    uint16_t rain_volume;      // Volume de chuva (0-100%)
    float water_rate;          // Taxa de elevação da água (%/min)
    float rain_rate;           // Taxa de intensificação da chuva (%/min)
    SystemMode mode;           // Modo atual do sistema
    bool trend_worsening;      // Tendência de piora
    uint32_t timestamp;        // Timestamp para cálculos de taxa
} sensor_data_t;

// Estrutura para controle de alertas
typedef struct {
    SystemMode mode;           // Modo de alerta
    bool update_display;       // Flag para atualização do display
    bool update_matrix;        // Flag para atualização da matriz
    bool update_sound;         // Flag para atualização do som
} alert_control_t;

// Filas para comunicação entre tarefas
QueueHandle_t xQueueSensorData;     // Dados dos sensores
QueueHandle_t xQueueAlertControl;   // Controle de alertas
QueueHandle_t xQueueDisplayData;    // Dados para o display

// Variáveis globais
PIO pio = pio0;
int sm = 0;
static sensor_data_t last_sensor_data = {0};
static uint32_t last_alert_time = 0;

// Protótipos de funções
void vSensorTask(void *params);
void vProcessingTask(void *params);
void vDisplayTask(void *params);
void vLedRGBTask(void *params);
void vMatrixLedTask(void *params);
void vBuzzerTask(void *params);
void init_hardware(void);
void update_rgb_led(SystemMode mode, bool trend_worsening);
void play_alert_sound(SystemMode mode, bool trend_worsening);
void display_matrix_pattern(SystemMode mode, bool trend_worsening);
uint32_t urgb_u32(uint8_t r, uint8_t g, uint8_t b);
void put_pixel(uint32_t pixel_grb);

// Padrões para a matriz de LEDs
static const bool normal_pattern[NUM_PIXELS] = {
    0,0,0,0,0,
    0,1,0,0,0,
    0,0,1,0,1,
    0,0,0,1,0,
    1,0,0,0,0
};

static const bool warning_pattern[NUM_PIXELS] = {
    0,0,0,0,0,
    0,1,1,1,0,
    0,1,0,1,0,
    0,1,1,1,0,
    0,0,0,0,0
};

static const bool alert_pattern[NUM_PIXELS] = {
    1,0,0,0,1,
    0,1,0,1,0,
    0,0,1,0,0,
    0,1,0,1,0,
    1,0,0,0,1
};

static const bool critical_pattern[NUM_PIXELS] = {
    1,0,1,0,1,
    0,1,1,1,0,
    1,1,1,1,1,
    0,1,1,1,0,
    1,0,1,0,1
};

static const bool arrow_up_pattern[NUM_PIXELS] = {
    0,0,1,0,0,
    0,1,1,1,0,
    1,0,1,0,1,
    0,0,1,0,0,
    0,0,1,0,0
};

// Tarefa de leitura dos sensores (simulados pelo joystick)
void vSensorTask(void *params) {
    adc_init();
    adc_gpio_init(ADC_JOYSTICK_X);
    adc_gpio_init(ADC_JOYSTICK_Y);
    
    sensor_data_t sensor_data;
    sensor_data.timestamp = xTaskGetTickCount();
    sensor_data.water_rate = 0;
    sensor_data.rain_rate = 0;
    sensor_data.trend_worsening = false;
    
    uint16_t prev_water = 0;
    uint16_t prev_rain = 0;
    
    while (true) {
        // Leitura do nível de água (eixo X do joystick)
        adc_select_input(0); // ADC0 = GPIO26
        uint16_t raw_water = adc_read();
        sensor_data.water_level = (raw_water * 100) / 4095;
        
        // Leitura do volume de chuva (eixo Y do joystick)
        adc_select_input(1); // ADC1 = GPIO27
        uint16_t raw_rain = adc_read();
        sensor_data.rain_volume = (raw_rain * 100) / 4095;
        
        // Cálculo da taxa de variação a cada 5 segundos
        uint32_t current_time = xTaskGetTickCount();
        if (current_time - sensor_data.timestamp >= pdMS_TO_TICKS(5000)) {
            float time_diff = (current_time - sensor_data.timestamp) / 1000.0f / 60.0f; // em minutos
            
            // Calcula taxa de elevação da água (%/min)
            sensor_data.water_rate = (float)(sensor_data.water_level - prev_water) / time_diff;
            
            // Calcula taxa de intensificação da chuva (%/min)
            sensor_data.rain_rate = (float)(sensor_data.rain_volume - prev_rain) / time_diff;
            
            // Verifica tendência de piora
            sensor_data.trend_worsening = (sensor_data.water_rate > 2.0f || sensor_data.rain_rate > 3.0f);
            
            // Atualiza valores anteriores e timestamp
            prev_water = sensor_data.water_level;
            prev_rain = sensor_data.rain_volume;
            sensor_data.timestamp = current_time;
        }
        
        // Determina o modo do sistema com base nos níveis
        if (sensor_data.water_level >= WATER_LEVEL_CRITICAL || sensor_data.rain_volume >= RAIN_VOLUME_CRITICAL) {
            sensor_data.mode = CRITICAL_MODE;
        } else if (sensor_data.water_level >= WATER_LEVEL_ALERT || sensor_data.rain_volume >= RAIN_VOLUME_ALERT) {
            sensor_data.mode = ALERT_MODE;
        } else if (sensor_data.water_level >= WATER_LEVEL_WARNING || sensor_data.rain_volume >= RAIN_VOLUME_WARNING) {
            sensor_data.mode = WARNING_MODE;
        } else {
            sensor_data.mode = NORMAL_MODE;
        }
        
        // Envia dados para a fila
        xQueueSend(xQueueSensorData, &sensor_data, 0);
        
        // Atraso para próxima leitura (10 Hz)
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// Tarefa de processamento de dados e controle de alertas
void vProcessingTask(void *params) {
    sensor_data_t sensor_data;
    alert_control_t alert_control;
    SystemMode last_mode = NORMAL_MODE;
    uint32_t display_update_counter = 0;
    
    while (true) {
        // Recebe dados dos sensores
        if (xQueueReceive(xQueueSensorData, &sensor_data, pdMS_TO_TICKS(100)) == pdTRUE) {
            // Copia dados para uso global
            last_sensor_data = sensor_data;
            
            // Configura controle de alertas
            alert_control.mode = sensor_data.mode;
            
            // Atualiza display a cada ciclo em modo de alerta, ou a cada 5 ciclos em modo normal
            display_update_counter++;
            if (sensor_data.mode != NORMAL_MODE || display_update_counter >= 5) {
                alert_control.update_display = true;
                display_update_counter = 0;
            } else {
                alert_control.update_display = false;
            }
            
            // Atualiza matriz de LEDs quando o modo muda ou a cada 10 segundos em modo de alerta
            uint32_t current_time = xTaskGetTickCount();
            if (sensor_data.mode != last_mode || 
                (sensor_data.mode != NORMAL_MODE && current_time - last_alert_time >= pdMS_TO_TICKS(10000))) {
                alert_control.update_matrix = true;
                alert_control.update_sound = true;
                last_alert_time = current_time;
            } else {
                alert_control.update_matrix = false;
                alert_control.update_sound = false;
            }
            
            // Atualiza último modo
            last_mode = sensor_data.mode;
            
            // Envia dados para o display
            if (alert_control.update_display) {
                xQueueSend(xQueueDisplayData, &sensor_data, 0);
            }
            
            // Envia controle de alertas
            xQueueSend(xQueueAlertControl, &alert_control, 0);
        }
    }
}

// Tarefa de controle do display OLED
void vDisplayTask(void *params) {
    // Inicializa I2C para o display OLED
    i2c_init(I2C_PORT, 400 * 1000);
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);
    
    // Inicializa display OLED
    ssd1306_t display;
    ssd1306_init(&display, WIDTH, HEIGHT, false, OLED_ADDR, I2C_PORT);
    ssd1306_config(&display);
    
    // Buffer para strings
    char buffer[32];
    sensor_data_t sensor_data;
    
    while (true) {
        if (xQueueReceive(xQueueDisplayData, &sensor_data, portMAX_DELAY) == pdTRUE) {
            // Limpa o display
            ssd1306_fill(&display, false);
            
            // Título do sistema
            ///ssd1306_draw_string(&display, "MONITOR DE CHEIAS", 0, 0);
            //ssd1306_line(&display, 0, 10, 127, 10, true);
            
            // Exibe nível de água
            sprintf(buffer, "Nivel: %d%%", sensor_data.water_level);
            ssd1306_draw_string(&display, buffer, 0, 16);
            
            // Barra de progresso para nível de água
            ssd1306_rect(&display, 64, 16, 60, 8, true, false);
            uint8_t bar_width = (sensor_data.water_level * 58) / 100;
            if (bar_width > 0) {
                ssd1306_rect(&display, 64, 17, bar_width, 6, true, true);
            }
            
            // Exibe volume de chuva
            sprintf(buffer, "Chuva: %d%%", sensor_data.rain_volume);
            ssd1306_draw_string(&display, buffer, 0, 32);
            
            // Barra de progresso para volume de chuva
            ssd1306_rect(&display, 64, 32, 60, 8, true, false);
            bar_width = (sensor_data.rain_volume * 58) / 100;
            if (bar_width > 0) {
                ssd1306_rect(&display, 64, 33, bar_width, 6, true, true);
            }
            
            // Exibe status do sistema
            ssd1306_line(&display, 0, 48, 127, 48, true);
            switch (sensor_data.mode) {
                case NORMAL_MODE:
                    ssd1306_draw_string(&display, "STATUS: NORMAL", 0, 50);
                    break;
                case WARNING_MODE:
                    ssd1306_draw_string(&display, "STATUS: ATENCAO!", 0, 50);
                    break;
                case ALERT_MODE:
                    ssd1306_draw_string(&display, "STATUS: ALERTA!", 0, 50);
                    break;
                case CRITICAL_MODE:
                    ssd1306_draw_string(&display, "EVACUACAO IMEDIATA!", 0, 50);
                    break;
            }
            
            // Atualiza o display
            ssd1306_send_data(&display);
        }
    }
}

// Tarefa de controle do LED RGB
void vLedRGBTask(void *params) {
    // Configura pinos do LED RGB como PWM
    gpio_set_function(LED_RED, GPIO_FUNC_PWM);
    gpio_set_function(LED_GREEN, GPIO_FUNC_PWM);
    gpio_set_function(LED_BLUE, GPIO_FUNC_PWM);
    
    // Obtém os slices de PWM
    uint slice_red = pwm_gpio_to_slice_num(LED_RED);
    uint slice_green = pwm_gpio_to_slice_num(LED_GREEN);
    uint slice_blue = pwm_gpio_to_slice_num(LED_BLUE);
    
    // Obtém os canais corretos para cada pino
    uint chan_red = PWM_CHAN_A + (LED_RED & 1);
    uint chan_green = PWM_CHAN_A + (LED_GREEN & 1);
    uint chan_blue = PWM_CHAN_A + (LED_BLUE & 1);
    
    // Configura PWM
    pwm_set_wrap(slice_red, 255);
    pwm_set_wrap(slice_green, 255);
    pwm_set_wrap(slice_blue, 255);
    
    // Ativa PWM
    pwm_set_enabled(slice_red, true);
    pwm_set_enabled(slice_green, true);
    pwm_set_enabled(slice_blue, true);
    
    alert_control_t alert_control;
    uint32_t blink_counter = 0;
    bool blink_state = false;
    
    while (true) {
        if (xQueueReceive(xQueueAlertControl, &alert_control, pdMS_TO_TICKS(100)) == pdTRUE) {
            // Atualiza LED RGB com base no modo
            update_rgb_led(alert_control.mode, last_sensor_data.trend_worsening);
        }
        
        // Efeito de piscada para modos de alerta
        blink_counter++;
        if (blink_counter >= 5) {  // A cada 500ms
            blink_counter = 0;
            blink_state = !blink_state;
            
            if (last_sensor_data.mode == CRITICAL_MODE) {
                // Pisca vermelho em modo crítico
                if (blink_state) {
                    pwm_set_chan_level(slice_red, chan_red, 255);
                    pwm_set_chan_level(slice_green, chan_green, 0);
                    pwm_set_chan_level(slice_blue, chan_blue, 0);
                } else {
                    pwm_set_chan_level(slice_red, chan_red, 0);
                    pwm_set_chan_level(slice_green, chan_green, 0);
                    pwm_set_chan_level(slice_blue, chan_blue, 0);
                }
            } else if (last_sensor_data.mode == ALERT_MODE) {
                // Pisca vermelho em modo alerta
                if (blink_state) {
                    pwm_set_chan_level(slice_red, chan_red, 255);
                    pwm_set_chan_level(slice_green, chan_green, last_sensor_data.trend_worsening ? 128 : 0);
                    pwm_set_chan_level(slice_blue, chan_blue, 0);
                } else {
                    pwm_set_chan_level(slice_red, chan_red, 0);
                    pwm_set_chan_level(slice_green, chan_green, 0);
                    pwm_set_chan_level(slice_blue, chan_blue, 0);
                }
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// Tarefa de controle da matriz de LEDs - VERSÃO CORRIGIDA
void vMatrixLedTask(void *params) {
    alert_control_t alert_control;
    uint32_t animation_counter = 0;
    bool animation_frame = false;
    SystemMode last_displayed_mode = NORMAL_MODE;
    bool force_update = true;
    
    while (true) {
        bool update_needed = false;
        
        if (xQueueReceive(xQueueAlertControl, &alert_control, pdMS_TO_TICKS(100)) == pdTRUE) {
            // Verifica se o modo mudou desde a última atualização
            if (alert_control.mode != last_displayed_mode || alert_control.update_matrix || force_update) {
                update_needed = true;
                last_displayed_mode = alert_control.mode;
                force_update = false;
            }
        }
        
        // Animação para tendência de piora
        if (last_sensor_data.trend_worsening && 
            (last_sensor_data.mode == ALERT_MODE || last_sensor_data.mode == CRITICAL_MODE)) {
            animation_counter++;
            if (animation_counter >= 5) {  // A cada 500ms
                animation_counter = 0;
                animation_frame = !animation_frame;
                
                if (animation_frame) {
                    // Mostra padrão de seta para cima (tendência de piora)
                    for (int i = 0; i < NUM_PIXELS; i++) {
                        uint32_t color = arrow_up_pattern[i] ? urgb_u32(255, 0, 0) : 0;
                        put_pixel(color);
                    }
                } else {
                    // Volta ao padrão normal do modo atual
                    display_matrix_pattern(last_sensor_data.mode, false);
                }
            }
        } else {
            // Sem animação de tendência, verifica se precisa atualizar
            if (update_needed) {
                display_matrix_pattern(last_sensor_data.mode, false);
            }
            
            // Reinicia contador de animação quando não está em tendência de piora
            animation_counter = 0;
            animation_frame = false;
        }
        
        // Verifica periodicamente se o modo exibido corresponde ao modo atual
        static uint32_t verify_counter = 0;
        verify_counter++;
        if (verify_counter >= 20) {  // A cada 2 segundos
            verify_counter = 0;
            if (last_displayed_mode != last_sensor_data.mode) {
                // Detectou discrepância, força atualização
                force_update = true;
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// Tarefa de controle do buzzer 
void vBuzzerTask(void *params) {
    // Inicializa PWM para o buzzer
    gpio_set_function(BUZZER_PIN, GPIO_FUNC_PWM);
    uint slice_num = pwm_gpio_to_slice_num(BUZZER_PIN);
    pwm_config config = pwm_get_default_config();
    pwm_init(slice_num, &config, true);
    pwm_set_gpio_level(BUZZER_PIN, 0);
    
    alert_control_t alert_control;
    TickType_t last_sound_time = 0;
    
    while (true) {
        // Processa mensagens de controle de alerta
        if (xQueueReceive(xQueueAlertControl, &alert_control, pdMS_TO_TICKS(100)) == pdTRUE) {
            if (alert_control.update_sound) {
                // Toca som de alerta com base no modo
                play_alert_sound(alert_control.mode, last_sensor_data.trend_worsening);
                last_sound_time = xTaskGetTickCount(); // Atualiza o tempo do último som
            }
        }
        
        // Sons periódicos para modos de alerta
        if (last_sensor_data.mode >= WARNING_MODE) {
            TickType_t current_time = xTaskGetTickCount();
            TickType_t time_since_last_sound = current_time - last_sound_time;
            
            // Determina o intervalo com base no modo
            TickType_t sound_interval;
            switch (last_sensor_data.mode) {
                case WARNING_MODE:
                    sound_interval = pdMS_TO_TICKS(10000);  // 10 segundos
                    break;
                case ALERT_MODE:
                    sound_interval = pdMS_TO_TICKS(5000);   // 5 segundos
                    break;
                case CRITICAL_MODE:
                    sound_interval = pdMS_TO_TICKS(2000);   // 2 segundos
                    break;
                default:
                    sound_interval = pdMS_TO_TICKS(20000);  // 20 segundos (não deve ocorrer)
            }
            
            // Verifica se é hora de tocar o som novamente
            if (time_since_last_sound >= sound_interval) {
                play_alert_sound(last_sensor_data.mode, last_sensor_data.trend_worsening);
                last_sound_time = current_time; // Atualiza o tempo do último som
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// Função para atualizar o LED RGB com base no modo
void update_rgb_led(SystemMode mode, bool trend_worsening) {
    uint slice_red = pwm_gpio_to_slice_num(LED_RED);
    uint slice_green = pwm_gpio_to_slice_num(LED_GREEN);
    uint slice_blue = pwm_gpio_to_slice_num(LED_BLUE);
    
    switch (mode) {
        case NORMAL_MODE:
            // Verde
            pwm_set_chan_level(slice_red, PWM_CHAN_A, 0);
            pwm_set_chan_level(slice_green, PWM_CHAN_B, 255);
            pwm_set_chan_level(slice_blue, PWM_CHAN_A, 0);
            break;
            
        case WARNING_MODE:
            // Amarelo
            pwm_set_chan_level(slice_red, PWM_CHAN_A, 255);
            pwm_set_chan_level(slice_green, PWM_CHAN_B, 255);
            pwm_set_chan_level(slice_blue, PWM_CHAN_A, 0);
            break;
            
        case ALERT_MODE:
            if (trend_worsening) {
                // Laranja (tendência de piora)
                pwm_set_chan_level(slice_red, PWM_CHAN_A, 255);
                pwm_set_chan_level(slice_green, PWM_CHAN_B, 128);
                pwm_set_chan_level(slice_blue, PWM_CHAN_A, 0);
            } else {
                // Vermelho
                pwm_set_chan_level(slice_red, PWM_CHAN_A, 255);
                pwm_set_chan_level(slice_green, PWM_CHAN_B, 0);
                pwm_set_chan_level(slice_blue, PWM_CHAN_A, 0);
            }
            break;
            
        case CRITICAL_MODE:
            // Vermelho intenso
            pwm_set_chan_level(slice_red, PWM_CHAN_A, 255);
            pwm_set_chan_level(slice_green, PWM_CHAN_B, 0);
            pwm_set_chan_level(slice_blue, PWM_CHAN_A, 0);
            break;
    }
}

// Função para tocar som de alerta com base no modo
void play_alert_sound(SystemMode mode, bool trend_worsening) {
    uint slice_num = pwm_gpio_to_slice_num(BUZZER_PIN);
    
    switch (mode) {
        case NORMAL_MODE:
            // Sem som
            pwm_set_gpio_level(BUZZER_PIN, 0);
            break;
            
        case WARNING_MODE:
            // Bipe único
            pwm_set_clkdiv(slice_num, 100);
            pwm_set_wrap(slice_num, 1000);
            pwm_set_gpio_level(BUZZER_PIN, 500);
            vTaskDelay(pdMS_TO_TICKS(200));
            pwm_set_gpio_level(BUZZER_PIN, 0);
            break;
            
        case ALERT_MODE:
            // Dois bipes
            for (int i = 0; i < 2; i++) {
                pwm_set_clkdiv(slice_num, 50);
                pwm_set_wrap(slice_num, 1000);
                pwm_set_gpio_level(BUZZER_PIN, 500);
                vTaskDelay(pdMS_TO_TICKS(200));
                pwm_set_gpio_level(BUZZER_PIN, 0);
                vTaskDelay(pdMS_TO_TICKS(200));
            }
            
            // Som adicional para tendência de piora
            if (trend_worsening) {
                vTaskDelay(pdMS_TO_TICKS(300));
                pwm_set_clkdiv(slice_num, 20);
                pwm_set_wrap(slice_num, 1000);
                pwm_set_gpio_level(BUZZER_PIN, 500);
                vTaskDelay(pdMS_TO_TICKS(500));
                pwm_set_gpio_level(BUZZER_PIN, 0);
            }
            break;
            
        case CRITICAL_MODE:
            // Sirene (som oscilante)
            for (int i = 0; i < 3; i++) {
                // Tom ascendente
                for (int freq = 500; freq <= 2000; freq += 100) {
                    float divider = 125000000.0f / (freq * 1000.0f);
                    pwm_set_clkdiv(slice_num, divider);
                    pwm_set_wrap(slice_num, 1000);
                    pwm_set_gpio_level(BUZZER_PIN, 500);
                    vTaskDelay(pdMS_TO_TICKS(50));
                }
                
                // Tom descendente
                for (int freq = 2000; freq >= 500; freq -= 100) {
                    float divider = 125000000.0f / (freq * 1000.0f);
                    pwm_set_clkdiv(slice_num, divider);
                    pwm_set_wrap(slice_num, 1000);
                    pwm_set_gpio_level(BUZZER_PIN, 500);
                    vTaskDelay(pdMS_TO_TICKS(50));
                }
            }
            pwm_set_gpio_level(BUZZER_PIN, 0);
            break;
    }
}

// Função para exibir padrão na matriz de LEDs com base no modo
void display_matrix_pattern(SystemMode mode, bool trend_worsening) {
    uint8_t r = 0, g = 0, b = 0;
    const bool *pattern = NULL;
    
    switch (mode) {
        case NORMAL_MODE:
            // Verde
            r = 0; g = 32; b = 0;
            pattern = normal_pattern;
            break;
            
        case WARNING_MODE:
            // Amarelo
            r = 32; g = 32; b = 0;
            pattern = warning_pattern;
            break;
            
        case ALERT_MODE:
            // Vermelho
            r = 64; g = 0; b = 0;
            pattern = alert_pattern;
            break;
            
        case CRITICAL_MODE:
            // Vermelho intenso
            r = 255; g = 0; b = 0;
            pattern = critical_pattern;
            break;
    }
    
    // Exibe o padrão na matriz
    if (pattern) {
        for (int i = 0; i < NUM_PIXELS; i++) {
            uint32_t color = pattern[i] ? urgb_u32(r, g, b) : 0;
            put_pixel(color);
        }
    }
}

// Converte componentes RGB em GRB para o WS2812
uint32_t urgb_u32(uint8_t r, uint8_t g, uint8_t b) {
    return ((uint32_t) (r) << 8) | ((uint32_t) (g) << 16) | (uint32_t) (b);
}

// Envia um pixel para o PIO
void put_pixel(uint32_t pixel_grb) {
    pio_sm_put_blocking(pio, sm, pixel_grb << 8u);
}

// Inicialização do hardware
void init_hardware(void) {
    stdio_init_all();
    
    // Inicializa PIO para WS2812
    uint offset = pio_add_program(pio, &ws2812_program);
    ws2812_program_init(pio, sm, offset, WS2812_PIN, 800000, false);
    
    // Limpa a matriz de LEDs
    for (int i = 0; i < NUM_PIXELS; i++) {
        put_pixel(0);
    }
}

// Modo BOOTSEL com botão B
void gpio_irq_handler(uint gpio, uint32_t events)
{
    reset_usb_boot(0, 0);
}

int main()
{
    // Ativa BOOTSEL via botão
    gpio_init(BTN_B);
    gpio_set_dir(BTN_B, GPIO_IN);
    gpio_pull_up(BTN_B);
    gpio_set_irq_enabled_with_callback(BTN_B, GPIO_IRQ_EDGE_FALL, true, &gpio_irq_handler);

    // Inicializa hardware
    init_hardware();
    
    // Cria filas para comunicação entre tarefas
    xQueueSensorData = xQueueCreate(5, sizeof(sensor_data_t));
    xQueueAlertControl = xQueueCreate(5, sizeof(alert_control_t));
    xQueueDisplayData = xQueueCreate(3, sizeof(sensor_data_t));
    
    // Cria tarefas
    xTaskCreate(vSensorTask, "Sensor Task", 256, NULL, 3, NULL);
    xTaskCreate(vProcessingTask, "Processing Task", 256, NULL, 2, NULL);
    xTaskCreate(vDisplayTask, "Display Task", 512, NULL, 1, NULL);
    xTaskCreate(vLedRGBTask, "LED RGB Task", 256, NULL, 1, NULL);
    xTaskCreate(vMatrixLedTask, "Matrix LED Task", 256, NULL, 1, NULL);
    xTaskCreate(vBuzzerTask, "Buzzer Task", 256, NULL, 1, NULL);
    
    // Inicia o agendador
    vTaskStartScheduler();
    
    // Nunca deve chegar aqui
    while (1) {
        tight_loop_contents();
    }
    
    return 0;
}
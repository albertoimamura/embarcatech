#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "inc/ssd1306.h"
#include "hardware/adc.h"
#include "hardware/timer.h" // Biblioteca para gerenciamento de temporizadores de hardware.
#include "hardware/i2c.h"

// Biblioteca gerada pelo arquivo .pio durante compilação.
#include "ws2818b.pio.h"

// Definição do número de LEDs e pino.
#define LED_COUNT 25 // número total de leds endereçáveis
#define LED_PIN 7 // pino 7 - faz comunicação com os led endereçáveis

// Configuração do pino do buzzer
#define BUZZER_PIN 21

// Variáveis de controle do buzzer
volatile bool buzzer_state = false;  // Estado atual do pino buzzer
uint64_t buzzer_interval;            // Intervalo do buzzer (em microsegundos)

// Definição de pixel GRB
struct pixel_t {
  uint8_t G, R, B; // Três valores de 8-bits compõem um pixel.
};
typedef struct pixel_t pixel_t;
typedef pixel_t npLED_t; // Mudança de nome de "struct pixel_t" para "npLED_t" por clareza.

// Declaração do buffer de pixels que formam a matriz.
npLED_t leds[LED_COUNT];

// Variáveis para uso da máquina PIO.
PIO np_pio;
uint sm;
const uint I2C_SDA = 14; // Pino utilizado para comunicação I2C
const uint I2C_SCL = 15; // Pino utilizado para comunicação I2C
const uint BUTTON_A_PIN = 5;  // Botão A no GPIO 5
const uint BUTTON_B_PIN = 6;  // Botão B no GPIO 6
const int SW = 22;           // Pino de leitura do botão do joystick

bool ativatimer = 0;
int tela = 1;
/**
 * Inicializa a máquina PIO para controle da matriz de LEDs.
 */
void npInit(uint pin) {

    // Cria programa PIO.
    uint offset = pio_add_program(pio0, &ws2818b_program);
    np_pio = pio0;
  
    // Toma posse de uma máquina PIO.
    sm = pio_claim_unused_sm(np_pio, false);
    if (sm < 0) {
      np_pio = pio1;
      sm = pio_claim_unused_sm(np_pio, true); // Se nenhuma máquina estiver livre, panic!
    }  
    // Inicia programa na máquina PIO obtida.
    ws2818b_program_init(np_pio, sm, offset, pin, 800000.f);
  
    // Limpa buffer de pixels.
    for (uint i = 0; i < LED_COUNT; ++i) {
      leds[i].R = 0;
      leds[i].G = 0;
      leds[i].B = 0;
    }
  }
  
  /**
   * Atribui uma cor RGB a um LED.
   */
  void npSetLED(const uint index, const uint8_t r, const uint8_t g, const uint8_t b) {
    leds[index].R = r;
    leds[index].G = g;
    leds[index].B = b;
  }
  
  /**
   * Limpa o buffer de pixels.
   */
  void npClear() {
    for (uint i = 0; i < LED_COUNT; ++i)
      npSetLED(i, 0, 0, 0);
  }
  
  /**
   * Escreve os dados do buffer nos LEDs.
   */
  void npWrite() {
    // Escreve cada dado de 8-bits dos pixels em sequência no buffer da máquina PIO.
    for (uint i = 0; i < LED_COUNT; ++i) {
      pio_sm_put_blocking(np_pio, sm, leds[i].G);
      pio_sm_put_blocking(np_pio, sm, leds[i].R);
      pio_sm_put_blocking(np_pio, sm, leds[i].B);
    }
    sleep_us(100); // Espera 100us, sinal de RESET do datasheet.
  }  
 

  // Função de interrupção para alternar o estado do pino do buzzer
bool buzzer_interrupt_callback(struct repeating_timer *t) {
  buzzer_state = !buzzer_state;  // Inverter o estado do pino buzzer
  gpio_put(BUZZER_PIN, buzzer_state);  // Colocar o valor do estado no pino
  return true;  // Retornar true para continuar a interrupção
} 

// Função de interrupção que será chamada quando o botão for pressionado
void button_isr_handler(uint gpio, uint32_t events) {
  if (events & GPIO_IRQ_EDGE_FALL) {
      // Evento de borda de descida (botão pressionado)
      tela = 1;
  
      return tela;
  }
}


int main() {
    stdio_init_all();   // Inicializa os tipos stdio padrão presentes ligados ao binário
//--------------------------------------------------------------------------------------------
    // Inicialização do i2c
    i2c_init(i2c1, ssd1306_i2c_clock * 1000);
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);

    // Processo de inicialização completo do OLED SSD1306
    ssd1306_init();

    // Preparar área de renderização para o display (ssd1306_width pixels por ssd1306_n_pages páginas)
    struct render_area frame_area = {
        start_column : 0,
        end_column : ssd1306_width - 1,
        start_page : 0,
        end_page : ssd1306_n_pages - 1
    };

    calculate_render_area_buffer_length(&frame_area);

    // zera o display inteiro
    uint8_t ssd[ssd1306_buffer_length];
    memset(ssd, 0, ssd1306_buffer_length);
    render_on_display(ssd, &frame_area);
    sleep_ms(1000); // Pequeno atraso para evitar debounce
//--------------------------------------------------------------------------------------------
    gpio_init(BUTTON_A_PIN);
    gpio_init(BUTTON_B_PIN);
    gpio_init(SW);
    gpio_set_dir(BUTTON_A_PIN, GPIO_IN);
    gpio_set_dir(BUTTON_B_PIN, GPIO_IN);
    gpio_set_dir(SW, GPIO_IN);
    gpio_pull_up(BUTTON_A_PIN);
    gpio_pull_up(BUTTON_B_PIN);
    gpio_pull_up(SW);

    // Configuração do GPIO para o buzzer como saída
    gpio_init(BUZZER_PIN);
    gpio_set_dir(BUZZER_PIN, GPIO_OUT);

    ssd1306_draw_string(ssd, 0, 0,"TEMPO REACAO");
    render_on_display(ssd, &frame_area);  

    // Inicializa matriz de LEDs NeoPixel.
    npInit(LED_PIN);
    npClear();   

    // Inicializa o ADC
    adc_init();

    // Configura o pino GPIO27 como entrada analógica
    adc_gpio_init(27);

    // Configurar a interrupção no pino do botão A para detectar borda de descida (botão pressionado)
    gpio_set_irq_enabled_with_callback(BUTTON_A_PIN, GPIO_IRQ_EDGE_FALL, true, button_isr_handler);

    // Declaração de variável para armazenar o valor lido
    uint16_t raw_value;     

    while(true){

        if (tela == 1){
          ssd1306_draw_string(ssd, 0, 0,"JOYSTICK DIREITA");
          ssd1306_draw_string(ssd, 0, 8,"ESTIMULO VISUAL");
          ssd1306_draw_string(ssd, 0, 16,"JOYSTICK ESQUERDA");
          ssd1306_draw_string(ssd, 0, 24,"ESTIMULO SONORO");
          render_on_display(ssd, &frame_area); 
          // Lê o valor do ADC (será um valor entre 0 e 4095)
          adc_select_input(1);
          raw_value = adc_read();
          printf("valor analógico x: %d",raw_value);
          // Se o joystick foi movido para a direita
          if (raw_value > 4000) {
            tela = 2;
            ssd1306_draw_string(ssd, 0, 8,"                ");
            ssd1306_draw_string(ssd, 0, 16,"                ");
            ssd1306_draw_string(ssd, 0, 24,"                ");
            render_on_display(ssd, &frame_area);        
          }
          // Se o joystick foi movido para a esquerda 
          if (raw_value < 50) {
            tela = 3;
            ssd1306_draw_string(ssd, 0, 8,"                ");
            ssd1306_draw_string(ssd, 0, 16,"                ");
            ssd1306_draw_string(ssd, 0, 24,"                ");
            render_on_display(ssd, &frame_area);       
          }           
        }
        if (tela == 2){
          ssd1306_draw_string(ssd, 0, 0,"ESTIMULO VISUAL ");
          render_on_display(ssd, &frame_area);  

          // Inicializa o gerador de números aleatórios (semente baseada no tempo)
          srand(time(NULL));

          // Atraso aleatório entre 1 e 5 segundos para acender o LED
          int delay_time = (rand() % 5 + 1) * 1000;  // Entre 1000 e 3000 ms
          printf("Aguardando %d segundos antes de acender o LED...\n", delay_time / 1000);

          // Atraso aleatório antes de acender o LED
          sleep_ms(delay_time);

          for (uint i = 0; i < LED_COUNT; ++i){
              npSetLED(i, 0, 255, 0);            
          }                  
          npWrite(); // Escreve os dados nos LEDs.

          uint32_t led_start_time = time_us_32();  // Marca o tempo de início dos LEDs
          printf("LED aceso. Tempo total de acendimento: 5 segundos.\n");

          uint32_t button_press_time = 0;

          // Espera até o LED apagar ou o botão ser pressionado
          while (time_us_32() - led_start_time < 5000000) {  // 5 segundos
              if (gpio_get(BUTTON_B_PIN) == 0) {  // Se o botão for pressionado (nível baixo)              
                  button_press_time = time_us_32() - led_start_time;  
                  printf("Botão pressionado após %d milissegundos.\n", button_press_time / 1000);
                  ssd1306_draw_string(ssd, 0, 8,"                ");
                  ssd1306_draw_string(ssd, 0, 16,"                ");
                  ssd1306_draw_string(ssd, 0, 24,"                ");
                  render_on_display(ssd, &frame_area);                        
                  ssd1306_draw_string(ssd, 0, 8,"Demorou:       ");
                  char cont [20];
                  sprintf(cont, "%u", (button_press_time / 1000));
                  printf("O valor convertido é: %s\n",cont);
                  ssd1306_draw_string(ssd, 0, 16,cont);
                  ssd1306_draw_string(ssd, 40, 16,"ms   ");                  
                  render_on_display(ssd, &frame_area);
                  break;
              }
          }

          // Se o botão não foi pressionado até o LEDs apagarem
          if (button_press_time == 0) {
              printf("O botão não foi pressionado. LED apagado após 5 segundos.\n");
          }

          // Apaga o LEDs após o tempo de 5 segundos
          npClear();   
          npWrite(); // Escreve os dados nos LEDs.          
        }
        if (tela == 3){
          ssd1306_draw_string(ssd, 0, 0,"ESTIMULO SONORO ");
          render_on_display(ssd, &frame_area);  

          // Inicializa o gerador de números aleatórios (semente baseada no tempo)
          srand(time(NULL));

          // Atraso aleatório entre 1 e 5 segundos para acender o LED
          int delay_time = (rand() % 5 + 1) * 1000;  // Entre 1000 e 3000 ms
          printf("Aguardando %d segundos antes de acender o LED...\n", delay_time / 1000);

          // Atraso aleatório antes de acender o LED
          sleep_ms(delay_time);

          // Configurar a interrupção repetitiva para mudar estado do buzzer
          struct repeating_timer timer_2;
          add_repeating_timer_us(50, buzzer_interrupt_callback, NULL, &timer_2);

          uint32_t led_start_time = time_us_32();  // Marca o tempo de início do LED
          printf("LED aceso. Tempo total de acendimento: 5 segundos.\n");

          uint32_t button_press_time = 0;

          // Espera até o LED apagar ou o botão ser pressionado
          while (time_us_32() - led_start_time < 5000000) {  // 5 segundos
              if (gpio_get(BUTTON_B_PIN) == 0) {  // Se o botão for pressionado (nível baixo)              
                  button_press_time = time_us_32() - led_start_time;  
                  cancel_repeating_timer(&timer_2);
                  printf("Botão pressionado após %d milissegundos.\n", button_press_time / 1000);
                  ssd1306_draw_string(ssd, 0, 8,"                ");
                  ssd1306_draw_string(ssd, 0, 16,"                ");
                  ssd1306_draw_string(ssd, 0, 24,"                ");
                  render_on_display(ssd, &frame_area);                        
                  ssd1306_draw_string(ssd, 0, 8,"Demorou:       ");
                  char cont [20];
                  sprintf(cont, "%u", (button_press_time / 1000));
                  printf("O valor convertido é: %s\n",cont);
                  ssd1306_draw_string(ssd, 0, 16,cont);
                  ssd1306_draw_string(ssd, 40, 16,"ms   ");
                  render_on_display(ssd, &frame_area);
                  break;
              }
          }

          // Se o botão não foi pressionado até o LED apagar
          if (button_press_time == 0) {
              printf("O botão não foi pressionado. LED apagado após 5 segundos.\n");
          }

          // Desliga buzzer após o tempo de 5 segundos
          gpio_put(BUZZER_PIN, 0);
          cancel_repeating_timer(&timer_2); //finaliza interrupção do timer 2
        }        
        tight_loop_contents(); 
    }
    return 0;
    
}

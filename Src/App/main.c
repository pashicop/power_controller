#include <MDR_GPIO.h>
#include <MDR_Funcs.h>
#include <MDR_Config.h>
#include <MDR_RST_Clock.h>
#include "board.h"


#define HSE_PLL_MUL_MAX         MDR_x3   //  HSE_max = 12MHz * 3 = 36MHz (36MHz max)
#define HSE_LOW_SELRI_MAX       HSE_LOW_SRI
#define HSE_EEPROM_DELAY_MAX    HSE_DELAY_EEPROM

#define MDRB_CLK_PLL_HSE_RES_MAX     MDR_CPU_CFG_PLL_HSE_RES_DEF(HSE_PLL_MUL_MAX, HSE_EEPROM_DELAY_MAX, HSE_LOW_SELRI_MAX)

#define PB_LEN_MS 200

#define SENSE_DEBOUNCE_MS   200
#define SW_DEBOUNCE_MS      200

#define PIN_LOW  0
#define PIN_HIGH 1
#define PIN_ERR  -1

static void app_fault(uint16_t line_num, const uint8_t *file_name);
#define ASSERT(expr)                                                      \
if (expr)                                                                 \
{                                                                         \
}                                                                         \
else                                                                      \
{                                                                         \
    app_fault((uint16_t)__LINE__, (uint8_t *)__FILE__);                   \
}

#pragma GCC diagnostic ignored "-Wmissing-noreturn"
static void app_fault(uint16_t line_num, const uint8_t *file_name)
{
    NVIC_SystemReset();
}

static uint32_t freq = HSI_FREQ_HZ;

static uint32_t get_freq(void)
{
    return freq;
}

static void set_freq(uint32_t new_freq)
{
    ASSERT(new_freq > 0)
    ASSERT(new_freq <= 36000000UL)
    
    freq = new_freq;
}                                                        

typedef enum
{
    ON_STATE,
    OFF_STATE,
    WAIT_OFF_STATE
}State_t;

static int read_pin(const MDR_GPIO_Port *port, uint32_t pin, uint32_t debounce_ms)
{
    ASSERT(port != NULL)
    
    bool state = MDR_GPIO_GetMaskSet(port, pin);
    
    int ret_val = (state==true) ? PIN_HIGH : PIN_LOW;
        
    if (debounce_ms != 0)
    {
        MDR_Delay_ms(debounce_ms, get_freq());
        
        if (MDR_GPIO_GetMaskSet(port, pin) != state)
        {
            state = PIN_ERR;
        }
    }
    
    return ret_val;
}

static int read_sw(void)
{
    return read_pin(POWER_SW_Port, POWER_SW_Pin, SW_DEBOUNCE_MS);
}

static int read_sense(void)
{
    return read_pin(POWER_SENSE_Port, POWER_SENSE_Pin, SENSE_DEBOUNCE_MS);
}

static void pc_pwr_off(void)
{
    MDR_GPIO_SetPins(PB_CTRL_Port, PB_CTRL_Pin);
    MDR_Delay_ms(PB_LEN_MS, get_freq()); 
    MDR_GPIO_ClearPins(PB_CTRL_Port, PB_CTRL_Pin);
}

static void relay_ctrl(bool en)
{
    if (en)
       MDR_GPIO_SetPins(RLY_CTRL_Port, RLY_CTRL_Pin);       //power on
    else
       MDR_GPIO_ClearPins(RLY_CTRL_Port, RLY_CTRL_Pin);     // power off 
}

static void gpio_init(void)
{
    MDR_GPIO_Enable(RLY_CTRL_Port);
    MDR_GPIO_Init_PortOUT(RLY_CTRL_Port, RLY_CTRL_Pin, MDR_PIN_SLOW);
    MDR_GPIO_ClearPins(RLY_CTRL_Port, RLY_CTRL_Pin);
   
    MDR_GPIO_Enable(PB_CTRL_Port);
    MDR_GPIO_Init_PortOUT(PB_CTRL_Port, PB_CTRL_Pin, MDR_PIN_SLOW);
    MDR_GPIO_ClearPins(PB_CTRL_Port, PB_CTRL_Pin);

    MDR_GPIO_Enable(POWER_SW_Port);
    MDR_GPIO_Init_PortIN(POWER_SW_Port, POWER_SW_Pin);
    MDR_Port_SetPullUp(POWER_SW_Port->PORTx, POWER_SW_Pin);
    
    MDR_GPIO_Enable(POWER_SENSE_Port);
    MDR_GPIO_Init_PortIN(POWER_SENSE_Port, POWER_SENSE_Pin);
    MDR_Port_SetPullUp(POWER_SENSE_Port->PORTx, POWER_SENSE_Pin);
}

static void clk_init(void)
{
    MDR_CPU_SetClockResult res;    
    
    MDR_CPU_PLL_CfgHSE cfgPLL_HSE = MDRB_CLK_PLL_HSE_RES_MAX;
    res = MDR_CPU_SetClock_PLL_HSE(&cfgPLL_HSE, true);
    ASSERT(res == MDR_SET_CLOCK_OK)
      
    set_freq(MDR_CPU_GetFreqHz(true));
}

static State_t fsm(State_t state)
{
    State_t new_state = state;
    
    switch(state)
    {
        case OFF_STATE:
            if (read_sw() == PIN_LOW)
            {
                MDR_Delay_ms(750, get_freq());
                relay_ctrl(true);
                new_state = ON_STATE;
            }
            break;
        case ON_STATE:
            if (read_sw() == PIN_HIGH)
            {
                MDR_Delay_ms(100, get_freq());
							  if(read_sense() == PIN_HIGH)
                    pc_pwr_off();
                new_state = WAIT_OFF_STATE;
            }
            break;
        case WAIT_OFF_STATE:
            if (read_sense() == PIN_LOW)
            {
                MDR_Delay_ms(3000, get_freq());
                relay_ctrl(false);
                new_state = OFF_STATE; 
            }
            break;
    }
    
    return new_state;
}

int main(void)
{
    static State_t state = OFF_STATE;
    
    clk_init();
    gpio_init();

    while (1)
    {
        state = fsm(state);
    }
}

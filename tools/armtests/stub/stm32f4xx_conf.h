// Remplace l'en-tête du fabricant de la puce, pour compiler le code de synthèse
// d'Elements tout seul (tests). elements/drivers/debug_pin.h en a besoin pour se
// compiler, mais rien de ceci n'est appelé par la synthèse.
#pragma once
typedef struct { unsigned GPIO_Pin, GPIO_Mode, GPIO_OType, GPIO_Speed, GPIO_PuPd; } GPIO_InitTypeDef;
typedef struct { volatile unsigned short BSRRL, BSRRH; } GPIO_TypeDef;
#define GPIOA ((GPIO_TypeDef*) 0x40020018)
enum { RCC_AHB1Periph_GPIOA = 1, ENABLE = 1, GPIO_Pin_9 = 0x200, GPIO_Mode_OUT = 1, GPIO_OType_PP = 0,
       GPIO_Speed_25MHz = 1, GPIO_PuPd_NOPULL = 0 };
static inline void RCC_AHB1PeriphClockCmd(unsigned, int) {}
static inline void GPIO_Init(GPIO_TypeDef*, GPIO_InitTypeDef*) {}
